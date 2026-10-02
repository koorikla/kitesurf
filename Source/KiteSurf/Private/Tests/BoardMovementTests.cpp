#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "BoardMovementComponent.h"
#include "KiteWaterSurface.h"
#include "KiteRiderPawn.h"
#include "KiteComponent.h"
#include "WindComponent.h"
#include "KiteSurf.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfBoardSettlesAtRest, "KiteSurf.Board.SettlesAtRest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfBoardSettlesAtRest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn)
		{
			UBoardMovementComponent* BoardComp = Pawn->FindComponentByClass<UBoardMovementComponent>();
			TestNotNull(TEXT("BoardMovementComponent found"), BoardComp);

			if (BoardComp)
			{
				// Place board displaced above water surface (Z=30cm)
				Pawn->SetActorLocation(FVector(0.0f, 0.0f, 30.0f));
				BoardComp->Velocity = FVector::ZeroVector;

				// Simulate 3.0 seconds at 30 Hz (90 frames)
				const float DeltaTime = 3.0f / 90.0f;
				for (int32 i = 0; i < 90; ++i)
				{
					Pawn->Tick(DeltaTime);
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
				}

				const FVector FinalLocation = Pawn->GetActorLocation();
				const FVector FinalVelocity = Pawn->GetBoardVelocity();

				UE_LOG(LogKiteSurf, Log, TEXT("SettlesAtRest: Final Z = %.2f cm, Velocity.Z = %.2f cm/s"), FinalLocation.Z, FinalVelocity.Z);

				// Board settles to Z ≈ 0 within 3 s simulated
				TestNearlyEqual(TEXT("Board settles to Z ≈ 0 cm"), FinalLocation.Z, 0.0, 2.0);
				TestNearlyEqual(TEXT("Vertical velocity settles to 0 cm/s"), FinalVelocity.Z, 0.0, 5.0);
			}
		}

		World->DestroyWorld(false);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfBoardReachesPlaning, "KiteSurf.Board.ReachesPlaning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfBoardReachesPlaning::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn)
		{
			UBoardMovementComponent* BoardComp = Pawn->FindComponentByClass<UBoardMovementComponent>();
			TestNotNull(TEXT("BoardMovementComponent found"), BoardComp);

			if (BoardComp)
			{
				Pawn->SetActorLocation(FVector::ZeroVector);
				BoardComp->Velocity = FVector::ZeroVector;

				TestFalse(TEXT("Initially not planing"), BoardComp->IsPlaning());
				TestEqual(TEXT("Initial drag regime is Displacement"), (uint8)BoardComp->GetCurrentDragRegime(), (uint8)EBoardDragRegime::Displacement);

				// Constant forward force (250 N = 25000 kg*cm/s^2)
				const FVector ForwardForce(25000.0f, 0.0f, 0.0f);
				const float DeltaTime = 0.0333f;
				bool bObservedRegimeTransition = false;

				for (int32 i = 0; i < 90; ++i) // 3 seconds
				{
					BoardComp->AddExternalForce(ForwardForce);
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);

					if (BoardComp->IsPlaning())
					{
						bObservedRegimeTransition = true;
					}
				}

				const float FinalSpeed = BoardComp->GetForwardSpeed();
				UE_LOG(LogKiteSurf, Log, TEXT("ReachesPlaning: Final Speed = %.1f cm/s (Threshold: %.1f cm/s), Planing = %d"), FinalSpeed, BoardComp->PlaningThresholdCmS, BoardComp->IsPlaning());

				TestTrue(TEXT("Speed reached planing threshold (>= 400 cm/s)"), FinalSpeed >= BoardComp->PlaningThresholdCmS);
				TestTrue(TEXT("Drag regime transitioned to Planing"), bObservedRegimeTransition);
				TestEqual(TEXT("Final regime is Planing"), (uint8)BoardComp->GetCurrentDragRegime(), (uint8)EBoardDragRegime::Planing);
			}
		}

		World->DestroyWorld(false);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfBoardEdgeResistsLateralForce, "KiteSurf.Board.EdgeResistsLateralForce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfBoardEdgeResistsLateralForce::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn)
		{
			UBoardMovementComponent* BoardComp = Pawn->FindComponentByClass<UBoardMovementComponent>();
			TestNotNull(TEXT("BoardMovementComponent found"), BoardComp);

			if (BoardComp)
			{
				const float DeltaTime = 0.0333f;
				const FVector LateralForce(0.0f, 15000.0f, 0.0f); // 150 N sideways

				// Run 1: No Edge
				Pawn->SetActorLocation(FVector::ZeroVector);
				Pawn->SetActorRotation(FRotator::ZeroRotator);
				BoardComp->Velocity = FVector::ZeroVector;
				BoardComp->SetEdgeInput(0.0f);

				for (int32 i = 0; i < 45; ++i) // 1.5 seconds
				{
					BoardComp->AddExternalForce(LateralForce);
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
				}
				const float LateralSpeedNoEdge = BoardComp->GetLateralSpeed();

				// Run 2: Full Edge (EdgeInput = 1.0f)
				Pawn->SetActorLocation(FVector::ZeroVector);
				Pawn->SetActorRotation(FRotator::ZeroRotator);
				BoardComp->Velocity = FVector::ZeroVector;
				BoardComp->SetEdgeInput(1.0f);

				for (int32 i = 0; i < 45; ++i) // 1.5 seconds
				{
					BoardComp->AddExternalForce(LateralForce);
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
				}
				const float LateralSpeedFullEdge = BoardComp->GetLateralSpeed();

				const float Ratio = LateralSpeedFullEdge / FMath::Max(LateralSpeedNoEdge, 0.001f);
				UE_LOG(LogKiteSurf, Log, TEXT("EdgeResistsLateralForce: No Edge = %.1f cm/s, Full Edge = %.1f cm/s, Ratio = %.1f%%"),
					LateralSpeedNoEdge, LateralSpeedFullEdge, Ratio * 100.0f);

				// Spec: lateral force with full edge yields < 25% of the lateral speed vs no edge
				TestTrue(TEXT("Lateral speed with full edge is < 25% of no edge"), LateralSpeedFullEdge < 0.25f * LateralSpeedNoEdge);
			}
		}

		World->DestroyWorld(false);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfBoardDepowersAndStops, "KiteSurf.Board.DepowersAndStops", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfBoardDepowersAndStops::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn)
		{
			UBoardMovementComponent* BoardComp = Pawn->FindComponentByClass<UBoardMovementComponent>();
			TestNotNull(TEXT("BoardMovementComponent found"), BoardComp);

			if (BoardComp)
			{
				// Start in displacement mode moving forward at 350 cm/s (~6.8 knots)
				Pawn->SetActorLocation(FVector::ZeroVector);
				BoardComp->Velocity = FVector(350.0f, 0.0f, 0.0f);
				BoardComp->SetEdgeInput(0.0f);

				const float DeltaTime = 0.0333f;
				// 5 seconds of zero external force (depowered)
				for (int32 i = 0; i < 150; ++i)
				{
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
				}

				const float FinalSpeed = BoardComp->GetForwardSpeed();
				const float TwoKnotsCmS = 2.0f * 51.44f; // ~102.88 cm/s
				UE_LOG(LogKiteSurf, Log, TEXT("DepowersAndStops: Final Speed = %.1f cm/s (%.2f kn, target < %.2f kn)"),
					FinalSpeed, FinalSpeed / 51.44f, 2.0f);

				TestTrue(TEXT("Depowering stops rider to under 2 knots within 5 seconds in displacement mode"), FinalSpeed < TwoKnotsCmS);
			}
		}

		World->DestroyWorld(false);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfBoardSpeedCappedAtMax, "KiteSurf.Board.SpeedCappedAtMax", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfBoardSpeedCappedAtMax::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn)
		{
			UBoardMovementComponent* BoardComp = Pawn->FindComponentByClass<UBoardMovementComponent>();
			TestNotNull(TEXT("BoardMovementComponent found"), BoardComp);

			if (BoardComp)
			{
				Pawn->SetActorLocation(FVector::ZeroVector);
				BoardComp->Velocity = FVector(1000.0f, 0.0f, 0.0f);

				const float DeltaTime = 0.0333f;
				const FVector HugeForwardForce(5000000.0f, 0.0f, 0.0f);

				for (int32 i = 0; i < 90; ++i) // 3 seconds
				{
					BoardComp->AddExternalForce(HugeForwardForce);
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
				}

				const float FinalSpeed = BoardComp->GetForwardSpeed();
				const float MaxSpeedCmS = BoardComp->GetMaxBoardSpeedCmS();
				UE_LOG(LogKiteSurf, Log, TEXT("SpeedCappedAtMax: Final Speed = %.1f cm/s (Max = %.1f cm/s)"), FinalSpeed, MaxSpeedCmS);

				TestTrue(TEXT("Speed capped at 35 knots"), FinalSpeed <= MaxSpeedCmS + 0.1f);
			}
		}

		World->DestroyWorld(false);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfJumpOnlyFromPlaning, "KiteSurf.Jump.OnlyFromPlaning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfJumpOnlyFromPlaning::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn)
		{
			UBoardMovementComponent* BoardComp = Pawn->FindComponentByClass<UBoardMovementComponent>();
			TestNotNull(TEXT("BoardMovementComponent found"), BoardComp);

			if (BoardComp)
			{
				Pawn->SetActorLocation(FVector::ZeroVector);

				// Case 1: In displacement state with low speed (< 8 knots)
				BoardComp->Velocity = FVector(200.0f, 0.0f, 0.0f);
				BoardComp->SetBoardState(EBoardState::Displacement);
				BoardComp->SetEdgeInput(0.8f);
				TestEqual(TEXT("Jump rejected when in Displacement state"), BoardComp->Jump(), EJumpRejectReason::NotPlaning);
				TestEqual(TEXT("State remains Displacement"), BoardComp->GetBoardState(), EBoardState::Displacement);

				// Case 2: In Planing state, but edge input is below minimum (0.4)
				BoardComp->Velocity = FVector(772.0f, 0.0f, 0.0f); // 15 kn
				BoardComp->SetBoardState(EBoardState::Planing);
				BoardComp->SetEdgeInput(0.2f);
				TestEqual(TEXT("Jump rejected when EdgeInput < 0.4"), BoardComp->Jump(), EJumpRejectReason::NotEdged);
				TestEqual(TEXT("State remains Planing"), BoardComp->GetBoardState(), EBoardState::Planing);

				// Case 3: In Planing state, but speed is below minimum (8 kn = 411.5 cm/s)
				BoardComp->Velocity = FVector(350.0f, 0.0f, 0.0f);
				BoardComp->SetBoardState(EBoardState::Planing);
				BoardComp->SetEdgeInput(0.8f);
				TestEqual(TEXT("Jump rejected when Speed < 8 knots"), BoardComp->Jump(), EJumpRejectReason::TooSlow);
				TestEqual(TEXT("State remains Planing"), BoardComp->GetBoardState(), EBoardState::Planing);

				// Case 4: Planing, speed >= 8 knots, edge input >= 0.4 -> Success
				BoardComp->Velocity = FVector(772.0f, 0.0f, 0.0f); // 15 kn
				BoardComp->SetBoardState(EBoardState::Planing);
				BoardComp->SetEdgeInput(0.8f);
				TestEqual(TEXT("Jump succeeds when planing, fast enough, and edging hard"), BoardComp->Jump(), EJumpRejectReason::None);
				TestEqual(TEXT("State transitions to Airborne"), BoardComp->GetBoardState(), EBoardState::Airborne);
				TestTrue(TEXT("Vertical velocity positive on takeoff"), BoardComp->Velocity.Z > 200.0f);
			}
		}

		World->DestroyWorld(false);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfJumpApexEnvelope, "KiteSurf.Jump.ApexEnvelope", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfJumpApexEnvelope::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn)
		{
			UBoardMovementComponent* BoardComp = Pawn->FindComponentByClass<UBoardMovementComponent>();
			TestNotNull(TEXT("BoardMovementComponent found"), BoardComp);

			UKiteComponent* KiteComp = Pawn->FindComponentByClass<UKiteComponent>();
			TestNotNull(TEXT("KiteComponent found"), KiteComp);

			UWindComponent* WindComp = Pawn->FindComponentByClass<UWindComponent>();
			TestNotNull(TEXT("WindComponent found"), WindComp);

			if (BoardComp && KiteComp && WindComp)
			{
				Pawn->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));
				WindComp->BaseWind = FVector(772.0f, 0.0f, 0.0f); // 15 kn
				KiteComp->SheetKite(1.0f);
				KiteComp->SetElevationDeg(80.0f);
				KiteComp->UpdateKite(0.0333f);

				BoardComp->Velocity = FVector(772.0f, 0.0f, 0.0f); // 15 kn
				BoardComp->SetBoardState(EBoardState::Planing);
				BoardComp->SetEdgeInput(0.8f);

				TestEqual(TEXT("Jump pop succeeds"), BoardComp->Jump(), EJumpRejectReason::None);
				TestEqual(TEXT("Enters Airborne"), BoardComp->GetBoardState(), EBoardState::Airborne);

				// Flight simulation with continuous kite lift
				const float DeltaTime = 0.0333f;
				float MaxHeightReached = 0.0f;
				for (int32 i = 0; i < 90; ++i) // 3 seconds
				{
					KiteComp->UpdateKite(DeltaTime);
					Pawn->Tick(DeltaTime);
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
					MaxHeightReached = FMath::Max(MaxHeightReached, Pawn->GetActorLocation().Z);
				}

				UE_LOG(LogKiteSurf, Log, TEXT("ApexEnvelope: Max Height = %.1f cm (%.2f m), BestJump = %.1f cm"),
					MaxHeightReached, MaxHeightReached / 100.0f, BoardComp->GetBestJumpHeight());

				// Standard conditions yield 2m to 6m apex
				TestTrue(TEXT("Apex reached at least 2m (200 cm)"), MaxHeightReached >= 200.0f);
				TestTrue(TEXT("Apex within 12m clamp"), MaxHeightReached <= 1200.0f);
				TestTrue(TEXT("Best jump height recorded"), BoardComp->GetBestJumpHeight() >= 200.0f);

				// Test hard ceiling clamp at MaxJumpHeight (1200 cm)
				Pawn->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));
				BoardComp->Velocity = FVector(0.0f, 0.0f, 10000.0f);
				BoardComp->SetBoardState(EBoardState::Airborne);
				for (int32 i = 0; i < 30; ++i)
				{
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
				}
				TestTrue(TEXT("Pawn height clamped at MaxJumpHeight"), Pawn->GetActorLocation().Z <= BoardComp->MaxJumpHeight + 0.1f);
			}
		}

		World->DestroyWorld(false);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfJumpCleanLanding, "KiteSurf.Jump.CleanLanding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfJumpCleanLanding::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn)
		{
			UBoardMovementComponent* BoardComp = Pawn->FindComponentByClass<UBoardMovementComponent>();
			TestNotNull(TEXT("BoardMovementComponent found"), BoardComp);

			if (BoardComp)
			{
				// Align heading with forward velocity (+X)
				Pawn->SetActorRotation(FRotator(0.0f, 0.0f, 0.0f));
				Pawn->SetActorLocation(FVector(0.0f, 0.0f, 8.0f));
				const float PreLandSpeed = 600.0f;
				BoardComp->Velocity = FVector(PreLandSpeed, 0.0f, -50.0f);
				BoardComp->SetBoardState(EBoardState::Airborne);
				BoardComp->SetCurrentJumpAirtime(0.5f);

				// Step downward into water surface
				const float DeltaTime = 0.0333f;
				BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);

				TestTrue(TEXT("Last landing was clean"), BoardComp->WasLastLandingClean());
				TestFalse(TEXT("Did not crash on aligned landing"), BoardComp->IsCrashing());
				TestEqual(TEXT("Board enters Landing state"), BoardComp->GetBoardState(), EBoardState::Landing);

				const float PostLandSpeed = BoardComp->GetForwardSpeed();
				UE_LOG(LogKiteSurf, Log, TEXT("CleanLanding: PostLandSpeed = %.1f, Expected ≈ %.1f"), PostLandSpeed, PreLandSpeed * 0.8f);
				TestNearlyEqual(TEXT("Speed retained ~80% on clean landing"), PostLandSpeed, PreLandSpeed * 0.8f, 25.0f);
				TestNearlyEqual(TEXT("Vertical velocity reset to 0"), (float)BoardComp->Velocity.Z, 0.0f, 1.0f);
			}
		}

		World->DestroyWorld(false);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfJumpCrashRecovery, "KiteSurf.Jump.CrashRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfJumpCrashRecovery::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn)
		{
			UBoardMovementComponent* BoardComp = Pawn->FindComponentByClass<UBoardMovementComponent>();
			TestNotNull(TEXT("BoardMovementComponent found"), BoardComp);

			if (BoardComp)
			{
				// Rotate board 90 degrees away from velocity (+X vs +Y heading)
				Pawn->SetActorRotation(FRotator(0.0f, 90.0f, 0.0f));
				Pawn->SetActorLocation(FVector(0.0f, 0.0f, 8.0f));
				BoardComp->Velocity = FVector(600.0f, 0.0f, -50.0f);
				BoardComp->SetBoardState(EBoardState::Airborne);
				BoardComp->SetCurrentJumpAirtime(0.5f);

				const float DeltaTime = 0.0333f;
				BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);

				TestFalse(TEXT("Last landing was not clean"), BoardComp->WasLastLandingClean());
				TestTrue(TEXT("Crash triggered when landing angle > 30 deg"), BoardComp->IsCrashing());
				TestEqual(TEXT("Board enters Landing state"), BoardComp->GetBoardState(), EBoardState::Landing);

				// Simulate crash deceleration and respawn (recovers within 2.0 seconds)
				int32 RecoveryTicks = 0;
				while (BoardComp->IsCrashing() && RecoveryTicks < 60)
				{
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
					RecoveryTicks++;
				}

				TestFalse(TEXT("Crash recovery completes and clears crash flag"), BoardComp->IsCrashing());
				TestTrue(TEXT("Resets to rideable state after crash"), BoardComp->GetBoardState() == EBoardState::Planing || BoardComp->GetBoardState() == EBoardState::Displacement);
				TestTrue(TEXT("Has 8 kn speed after crash recovery"), (float)BoardComp->Velocity.Size2D() >= 400.0f);
			}
		}

		World->DestroyWorld(false);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfMovementCrashReset, "KiteSurf.Movement.CrashReset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfMovementCrashReset::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn)
		{
			UBoardMovementComponent* BoardComp = Pawn->FindComponentByClass<UBoardMovementComponent>();
			TestNotNull(TEXT("BoardMovementComponent found"), BoardComp);

			UKiteComponent* KiteComp = Pawn->FindComponentByClass<UKiteComponent>();
			TestNotNull(TEXT("KiteComponent found"), KiteComp);

			if (BoardComp)
			{
				// 1. Initial riding state on tack
				Pawn->SetActorRotation(FRotator(0.0f, 45.0f, 0.0f));
				BoardComp->Velocity = FVector(500.0f, 500.0f, 0.0f);
				BoardComp->SetBoardState(EBoardState::Planing);

				// 2. Trigger crash
				BoardComp->TriggerCrash(1.0f);
				TestTrue(TEXT("Board is crashing"), BoardComp->IsCrashing());

				// 3. Simulate forward in time until crash clears (within 2.0s)
				const float DeltaTime = 0.0333f;
				int32 TicksToRecover = 0;
				while (BoardComp->IsCrashing() && TicksToRecover < 60)
				{
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
					TicksToRecover++;
				}

				// 4. Verify rideable state (>= 8 kn, Planing or Displacement) within 2 s
				TestFalse(TEXT("Crash cleared within 2s"), BoardComp->IsCrashing());
				TestTrue(TEXT("Recovered within 2s"), TicksToRecover * DeltaTime <= 2.0f);
				const bool bRideable = (BoardComp->GetBoardState() == EBoardState::Planing || BoardComp->GetBoardState() == EBoardState::Displacement);
				TestTrue(TEXT("Board returned to rideable state"), bRideable);
				TestTrue(TEXT("Board has >= 8 kn speed (~411 cm/s)"), BoardComp->Velocity.Size2D() >= 400.0f);

				// 5. Verify board alignment matches tack heading
				const FVector ExpectedHeading = FVector(500.0f, 500.0f, 0.0f).GetSafeNormal2D();
				const FVector ActualHeading = BoardComp->UpdatedComponent->GetForwardVector().GetSafeNormal2D();
				TestNearlyEqual(TEXT("Board heading aligned with tack"), (float)FVector::DotProduct(ExpectedHeading, ActualHeading), 1.0f, 0.05f);

				// 6. Verify kite parked at 45 deg on the side the board is riding towards.
				// The tack heads to the right of the default +X wind, so that is 1:30 (Azimuth +45 deg).
				if (KiteComp)
				{
					TestNearlyEqual(TEXT("Kite elevation reset to 45 deg"), KiteComp->GetElevationDeg(), 45.0f, 1.0f);
					TestNearlyEqual(TEXT("Kite azimuth reset to +45 deg (1:30 park on the tack side)"), KiteComp->GetAzimuthDeg(), 45.0f, 1.0f);
				}

				// 7. Verify manual reset (R key) resets mid-ride
				BoardComp->Velocity = FVector(100.0f, 0.0f, 0.0f);
				Pawn->ResetRider();
				TestTrue(TEXT("Manual reset restores >= 8 kn"), BoardComp->Velocity.Size2D() >= 400.0f);
				TestFalse(TEXT("Manual reset not crashing"), BoardComp->IsCrashing());
			}
		}

		World->DestroyWorld(false);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfJumpPawnIntegration, "KiteSurf.Jump.PawnIntegration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfJumpPawnIntegration::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn)
		{
			UBoardMovementComponent* BoardComp = Pawn->FindComponentByClass<UBoardMovementComponent>();
			TestNotNull(TEXT("BoardMovementComponent found"), BoardComp);

			if (BoardComp)
			{
				Pawn->EdgeBoard(0.8f);
				BoardComp->Velocity = FVector(772.0f, 0.0f, 0.0f);
				BoardComp->SetBoardState(EBoardState::Planing);

				TestTrue(TEXT("Pawn Jump() succeeds when conditions met"), Pawn->Jump());
				TestEqual(TEXT("Pawn board enters Airborne state"), BoardComp->GetBoardState(), EBoardState::Airborne);
				TestTrue(TEXT("Pawn board velocity has positive vertical component"), Pawn->GetBoardVelocity().Z > 0.0f);
			}
		}

		World->DestroyWorld(false);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWaterSurfaceInterface, "KiteSurf.Water.SurfaceInterface", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWaterSurfaceInterface::RunTest(const FString& Parameters)
{
	// 1. Flat implementation returns 0 (and UpVector normal)
	FKiteFlatWaterSurface FlatSurface(0.0f);
	float FlatHeight = 100.0f;
	FVector FlatNormal = FVector::ZeroVector;
	FlatSurface.SampleWaterSurface(FVector2D(123.0f, 456.0f), FlatHeight, FlatNormal);
	TestNearlyEqual(TEXT("Flat water height = 0 cm"), (double)FlatHeight, 0.0, 0.01);
	TestEqual(TEXT("Flat water normal = UpVector"), FlatNormal, FVector::UpVector);

	// 2. Mock wave implementation returns A*sin(kx) within 1 cm at 100 points
	// Amplitude = 80 cm, Wavelength = 2000 cm, Direction = (1, 0)
	const float Amplitude = 80.0f;
	const float Wavelength = 2000.0f;
	FKiteWaveWaterSurface WaveSurface(0.0f, Amplitude, Wavelength, FVector2D(1.0f, 0.0f));

	for (int32 i = 0; i < 100; ++i)
	{
		const float SampleX = (float)i * 20.0f; // 0 to 2000 cm across full wavelength
		const float SampleY = (float)(i % 10) * 100.0f;
		float H = 0.0f;
		FVector N = FVector::ZeroVector;
		WaveSurface.SampleWaterSurface(FVector2D(SampleX, SampleY), H, N);

		const float ExpectedK = 2.0f * PI / Wavelength;
		const float ExpectedH = Amplitude * FMath::Sin(ExpectedK * SampleX);
		TestNearlyEqual(TEXT("Wave height matches A*sin(kx) within 1 cm at 100 sample points"), (double)H, (double)ExpectedH, 0.01);
		TestNearlyEqual(TEXT("Wave normal is unit length"), (double)N.Size(), 1.0, 0.01);
	}

	// 3. Board Z tracks wave surface height in physics simulation
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn)
		{
			UBoardMovementComponent* BoardComp = Pawn->FindComponentByClass<UBoardMovementComponent>();
			TestNotNull(TEXT("BoardMovementComponent found"), BoardComp);

			if (BoardComp)
			{
				TSharedPtr<FKiteWaveWaterSurface> Wave = MakeShared<FKiteWaveWaterSurface>(0.0f, Amplitude, Wavelength, FVector2D(1.0f, 0.0f));
				BoardComp->SetWaterSurface(Wave);

				// Crest is at X = 500 cm (height = +80 cm)
				float CrestWaterHeight = 0.0f;
				FVector CrestWaterNormal = FVector::UpVector;
				BoardComp->SampleWaterSurface(FVector(500.0f, 0.0f, 0.0f), CrestWaterHeight, CrestWaterNormal);
				TestNearlyEqual(TEXT("Board component samples crest height ≈ 80 cm"), (double)CrestWaterHeight, 80.0, 0.1);

				// Trough is at X = 1500 cm (height = -80 cm)
				float TroughWaterHeight = 0.0f;
				FVector TroughWaterNormal = FVector::UpVector;
				BoardComp->SampleWaterSurface(FVector(1500.0f, 0.0f, 0.0f), TroughWaterHeight, TroughWaterNormal);
				TestNearlyEqual(TEXT("Board component samples trough height ≈ -80 cm"), (double)TroughWaterHeight, -80.0, 0.1);

				// Place board at wave crest and simulate settling (3.0 seconds at 30 Hz)
				Pawn->SetActorLocation(FVector(500.0f, 0.0f, 100.0f));
				BoardComp->Velocity = FVector::ZeroVector;
				const float DeltaTime = 3.0f / 90.0f;
				for (int32 i = 0; i < 90; ++i)
				{
					Pawn->Tick(DeltaTime);
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
				}

				const FVector FinalCrestLoc = Pawn->GetActorLocation();
				UE_LOG(LogKiteSurf, Log, TEXT("Board on wave crest: Final Z = %.2f cm (target ≈ 80 cm)"), FinalCrestLoc.Z);
				TestNearlyEqual(TEXT("Board settles near wave crest (Z ≈ 80 cm)"), (double)FinalCrestLoc.Z, 80.0, 5.0);

				// Place board at wave trough and simulate settling
				Pawn->SetActorLocation(FVector(1500.0f, 0.0f, -60.0f));
				BoardComp->Velocity = FVector::ZeroVector;
				for (int32 i = 0; i < 90; ++i)
				{
					Pawn->Tick(DeltaTime);
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
				}

				const FVector FinalTroughLoc = Pawn->GetActorLocation();
				UE_LOG(LogKiteSurf, Log, TEXT("Board on wave trough: Final Z = %.2f cm (target ≈ -80 cm)"), FinalTroughLoc.Z);
				TestNearlyEqual(TEXT("Board settles near wave trough (Z ≈ -80 cm)"), (double)FinalTroughLoc.Z, -80.0, 5.0);
			}
		}

		World->DestroyWorld(false);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
