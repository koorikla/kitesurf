#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "BoardMovementComponent.h"
#include "KiteRiderPawn.h"
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

#endif // WITH_DEV_AUTOMATION_TESTS
