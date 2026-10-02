#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "BoardMovementComponent.h"
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
				TestFalse(TEXT("Jump rejected when in Displacement state"), BoardComp->Jump());
				TestEqual(TEXT("State remains Displacement"), BoardComp->GetBoardState(), EBoardState::Displacement);

				// Case 2: In Planing state, but edge input is below minimum (0.4)
				BoardComp->Velocity = FVector(772.0f, 0.0f, 0.0f); // 15 kn
				BoardComp->SetBoardState(EBoardState::Planing);
				BoardComp->SetEdgeInput(0.2f);
				TestFalse(TEXT("Jump rejected when EdgeInput < 0.4"), BoardComp->Jump());
				TestEqual(TEXT("State remains Planing"), BoardComp->GetBoardState(), EBoardState::Planing);

				// Case 3: In Planing state, but speed is below minimum (8 kn = 411.5 cm/s)
				BoardComp->Velocity = FVector(350.0f, 0.0f, 0.0f);
				BoardComp->SetBoardState(EBoardState::Planing);
				BoardComp->SetEdgeInput(0.8f);
				TestFalse(TEXT("Jump rejected when Speed < 8 knots"), BoardComp->Jump());
				TestEqual(TEXT("State remains Planing"), BoardComp->GetBoardState(), EBoardState::Planing);

				// Case 4: Planing, speed >= 8 knots, edge input >= 0.4 -> Success
				BoardComp->Velocity = FVector(772.0f, 0.0f, 0.0f); // 15 kn
				BoardComp->SetBoardState(EBoardState::Planing);
				BoardComp->SetEdgeInput(0.8f);
				TestTrue(TEXT("Jump succeeds when planing, fast enough, and edging hard"), BoardComp->Jump());
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

				TestTrue(TEXT("Jump pop succeeds"), BoardComp->Jump());
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

				// Simulate crash deceleration and respawn (~2.3 seconds)
				for (int32 i = 0; i < 70; ++i)
				{
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
				}

				TestFalse(TEXT("Crash recovery completes and clears crash flag"), BoardComp->IsCrashing());
				TestEqual(TEXT("Resets to Displacement state after crash"), BoardComp->GetBoardState(), EBoardState::Displacement);
				TestNearlyEqual(TEXT("Velocity zeroed after crash recovery"), (float)BoardComp->Velocity.Size(), 0.0f, 1.0f);
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

#endif // WITH_DEV_AUTOMATION_TESTS
