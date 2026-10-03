#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "BoardMovementComponent.h"
#include "KiteWaterSurface.h"
#include "KiteRiderPawn.h"
#include "KiteComponent.h"
#include "WindComponent.h"
#include "KiteSurf.h"
#include "KiteSurfUnits.h"
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
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
				}

				const FVector FinalLocation = Pawn->GetActorLocation();
				const FVector FinalVelocity = Pawn->GetBoardVelocity();

				UE_LOG(LogKiteSurf, Log, TEXT("SettlesAtRest: Final Z = %.2f cm, Velocity.Z = %.2f cm/s"), FinalLocation.Z, FinalVelocity.Z);

				// With no speed the board does not carry the rider: it settles at the floating depth within 3 s simulated
				TestNearlyEqual(TEXT("Board settles at the floating depth"), FinalLocation.Z, static_cast<double>(-BoardComp->FloatSubmersionCm), 2.0);
				TestTrue(TEXT("The rider is floating"), BoardComp->IsFloating());
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
				// A board planing at 8 m/s, a forward pull about its drag and 150 N sideways. Ridden flat
				// (no auto-edge) only the fins hold the sideways force, from leeway; edged, the rider heels
				// the board to the balance and the water's normal force carries it (plan-2 item 3). Before
				// item 3 this compared the carve input's grip on a stopped board: 31.8 cm/s without the
				// edge, 7.7 cm/s at full edge (24%); the carve input no longer sets the grip.
				const float DeltaTime = 0.0333f;
				const FVector Pull(25000.0f, 15000.0f, 0.0f); // 250 N forward, 150 N sideways
				const FVector StartVelocity(800.0f, 0.0f, 0.0f);

				auto RideWithPull = [&](bool bAutoEdge, float& OutHeelDeg) -> float
				{
					Pawn->SetActorLocation(FVector::ZeroVector);
					Pawn->SetActorRotation(FRotator::ZeroRotator);
					BoardComp->Velocity = StartVelocity;
					BoardComp->SetBoardState(EBoardState::Planing);
					BoardComp->bAutoEdge = bAutoEdge;
					for (int32 i = 0; i < 45; ++i) // 1.5 seconds
					{
						BoardComp->AddExternalForce(Pull);
						BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
					}
					OutHeelDeg = BoardComp->GetHeelDeg();
					return FMath::Abs(BoardComp->Velocity.Y);
				};
				float FlatHeelDeg = 0.0f;
				float EdgedHeelDeg = 0.0f;
				const float LateralSpeedNoEdge = RideWithPull(false, FlatHeelDeg);
				const float LateralSpeedFullEdge = RideWithPull(true, EdgedHeelDeg);

				const float Ratio = LateralSpeedFullEdge / FMath::Max(LateralSpeedNoEdge, 0.001f);
				UE_LOG(LogKiteSurf, Log, TEXT("EdgeResistsLateralForce: flat (heel %.1f deg) = %.1f cm/s, edged (heel %.1f deg) = %.1f cm/s, Ratio = %.1f%%, %.1f kn"),
					FlatHeelDeg, LateralSpeedNoEdge, EdgedHeelDeg, LateralSpeedFullEdge, Ratio * 100.0f, KiteUnits::CmSToKnots(BoardComp->Velocity.Size2D()));

				TestTrue(FString::Printf(TEXT("Ridden flat the board heels no more than a degree (%.1f deg)"), FlatHeelDeg), FMath::Abs(FlatHeelDeg) < 1.0f);
				TestTrue(FString::Printf(TEXT("Edged, it heels to carry the pull (%.1f deg)"), EdgedHeelDeg), FMath::Abs(EdgedHeelDeg) > 5.0f);
				TestTrue(TEXT("Lateral speed with the edge is < 25% of riding flat"), LateralSpeedFullEdge < 0.25f * LateralSpeedNoEdge);
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

				// A rider can always pop while they are up on the board on the water.

				// No edge at all: still a pop.
				BoardComp->Velocity = FVector(772.0f, 0.0f, 0.0f); // 15 kn
				BoardComp->SetBoardState(EBoardState::Planing);
				BoardComp->SetEdgeInput(0.0f);
				TestEqual(TEXT("Popping with no edge works"), BoardComp->Jump(), EJumpRejectReason::None);
				TestEqual(TEXT("and the rider is in the air"), BoardComp->GetBoardState(), EBoardState::Airborne);
				const float PlainPopVz = BoardComp->Velocity.Z;
				TestTrue(FString::Printf(TEXT("from the legs alone: 2 to 3 m/s (%.0f cm/s)"), PlainPopVz), PlainPopVz > 200.0f && PlainPopVz < 300.0f);

				// Already in the air: there is nothing to pop from.
				TestEqual(TEXT("Popping again in the air is refused"), BoardComp->Jump(), EJumpRejectReason::NotPlaning);

				// Slow, below planing speed but still up on the board: a pop.
				Pawn->SetActorLocation(FVector::ZeroVector);
				BoardComp->Velocity = FVector(350.0f, 0.0f, 0.0f);
				BoardComp->SetBoardState(EBoardState::Displacement);
				TestEqual(TEXT("Popping below planing speed works"), BoardComp->Jump(), EJumpRejectReason::None);

				// Weight back on the tail pushes off harder.
				Pawn->SetActorLocation(FVector::ZeroVector);
				BoardComp->Velocity = FVector(772.0f, 0.0f, 0.0f);
				BoardComp->SetBoardState(EBoardState::Planing);
				BoardComp->SetWeightShift(-1.0f);
				TestEqual(TEXT("Popping with the weight back works"), BoardComp->Jump(), EJumpRejectReason::None);
				TestTrue(TEXT("and is stronger than a plain pop"), BoardComp->Velocity.Z > PlainPopVz + 50.0f);
				BoardComp->SetWeightShift(0.0f);

				// Stopped and floating, the board is under the water: no pop.
				Pawn->SetActorLocation(FVector::ZeroVector);
				BoardComp->Velocity = FVector::ZeroVector;
				BoardComp->SetBoardState(EBoardState::Displacement);
				for (int32 Step = 0; Step < 120; ++Step)
				{
					BoardComp->Velocity.X = 0.0f;
					BoardComp->Velocity.Y = 0.0f;
					BoardComp->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
				}
				TestTrue(TEXT("At rest the rider is floating"), BoardComp->IsFloating());
				TestEqual(TEXT("Popping while floating is refused"), BoardComp->Jump(), EJumpRejectReason::NotPlaning);
				TestTrue(TEXT("and the rider stays in the water"), BoardComp->GetBoardState() != EBoardState::Airborne);
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
				KiteComp->bParkHoldAssist = true; // held where it is placed through the hop
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
					Pawn->Tick(DeltaTime);
					MaxHeightReached = FMath::Max(MaxHeightReached, Pawn->GetActorLocation().Z);
				}

				UE_LOG(LogKiteSurf, Log, TEXT("ApexEnvelope: Max Height = %.1f cm (%.2f m), BestJump = %.1f cm"),
					MaxHeightReached, MaxHeightReached / 100.0f, BoardComp->GetBestJumpHeight());

				// A pop on its own, under a kite that is just parked overhead, is a hop: the legs give
				// about 2.5 m/s. Height comes from loading the edge against a moving kite (KiteSurf.Jump.TimedReleaseBeatsPop).
				TestTrue(FString::Printf(TEXT("A plain pop is a hop of 30 cm to 2 m (%.0f cm)"), MaxHeightReached), MaxHeightReached >= 30.0f && MaxHeightReached <= 200.0f);
				TestTrue(TEXT("Best jump height recorded"), BoardComp->GetBestJumpHeight() >= 30.0f);

				// Test hard ceiling clamp at MaxJumpHeight
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

// In the air the rider and board are a body in the wind (research: drag area 0.5 to 1.0 m^2,
// docs/physics/research.md 3.5): still air slows them by 0.5 rho CdA v^2, a wind pushes a rider
// hanging still along with it, and on the water there is no air drag at all.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfJumpBodyDragInTheAir, "KiteSurf.Jump.BodyDragInTheAir", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfJumpBodyDragInTheAir::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteRiderPawn* Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
	UBoardMovementComponent* Board = Pawn ? Pawn->GetBoardMovement() : nullptr;
	UWindComponent* Wind = Pawn ? Pawn->GetWind() : nullptr;
	UKiteComponent* Kite = Pawn ? Pawn->GetKite() : nullptr;
	TestTrue(TEXT("Rider, board, wind and kite created"), Board && Wind && Kite);
	if (!Board || !Wind || !Kite)
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}
	Wind->GustStrength = 0.0f;
	Wind->DirectionDriftDeg = 0.0f;
	const float Step = 1.0f / 240.0f;
	const float HalfRhoCdA = 0.5f * KiteUnits::AirDensityKgM3 * Board->RiderDragAreaM2;
	const FVector HighUp(0.0f, 0.0f, 2000.0f);

	// Still air, flying level at 15 m/s, 20 m up: the drag is 0.5 rho CdA v^2 against the motion.
	Wind->BaseWind = FVector::ZeroVector;
	const float FlightSpeedMS = 15.0f;
	auto FlyLevel = [&](float Seconds)
	{
		Pawn->SetActorLocation(HighUp);
		Board->Velocity = FVector(KiteUnits::MToCm(FlightSpeedMS), 0.0f, 0.0f);
		Board->SetBoardState(EBoardState::Airborne);
		Board->Simulate(Step);
		const FVector FirstStepDragN = Board->GetLastStepDebug().AirDragN;
		Board->Simulate(FMath::Max(Seconds - Step, 0.0f));
		return FirstStepDragN;
	};
	const FVector StillAirDragN = FlyLevel(1.0f);
	const float SpeedAfterDragMS = KiteUnits::CmToM(Board->Velocity.X);
	const float ExpectedDragN = HalfRhoCdA * FMath::Square(FlightSpeedMS);
	TestNearlyEqual(FString::Printf(TEXT("In still air at 15 m/s the drag is 0.5 rho CdA v^2 = %.1f N against the motion (%.1f N)"), ExpectedDragN, -StillAirDragN.X),
		static_cast<float>(-StillAirDragN.X), ExpectedDragN, 0.01f * ExpectedDragN);
	TestTrue(TEXT("and only against the motion"), FMath::Abs(StillAirDragN.Y) < 0.01f && FMath::Abs(StillAirDragN.Z) < 0.01f);
	TestTrue(FString::Printf(TEXT("A second of it takes about a metre a second off (%.2f m/s left of 15)"), SpeedAfterDragMS), SpeedAfterDragMS < FlightSpeedMS - 0.9f && SpeedAfterDragMS > FlightSpeedMS - 1.6f);

	Board->RiderDragAreaM2 = 0.0f;
	FlyLevel(1.0f);
	TestNearlyEqual(TEXT("With no drag area nothing slows the rider in the air"), static_cast<float>(KiteUnits::CmToM(Board->Velocity.X)), FlightSpeedMS, 0.001f);
	Board->RiderDragAreaM2 = 0.7f;

	// A 10 m/s wind and a rider hanging still: pushed downwind by the wind at chest height.
	Wind->BaseWind = FVector(1000.0f, 0.0f, 0.0f);
	Pawn->SetActorLocation(HighUp);
	Board->Velocity = FVector::ZeroVector;
	Board->SetBoardState(EBoardState::Airborne);
	Board->Simulate(Step);
	const FVector WindAtChestCmS = Wind->GetWindAtTime(HighUp + FVector(0.0f, 0.0f, Kite->RiderWindHeightCm), Board->GetSimTimeSeconds());
	const float ExpectedPushN = HalfRhoCdA * FMath::Square(KiteUnits::CmToM(WindAtChestCmS.Size()));
	const FVector PushN = Board->GetLastStepDebug().AirDragN;
	TestNearlyEqual(FString::Printf(TEXT("A wind pushes a rider hanging still downwind with 0.5 rho CdA w^2 = %.1f N (%.1f N)"), ExpectedPushN, PushN.X),
		static_cast<float>(PushN.X), ExpectedPushN, 0.01f * ExpectedPushN);

	// On the water the hull is the model: no air drag.
	Pawn->SetActorLocation(FVector::ZeroVector);
	Board->Velocity = FVector(800.0f, 0.0f, 0.0f);
	Board->SetBoardState(EBoardState::Planing);
	Board->Simulate(Step);
	TestTrue(TEXT("On the water there is no air drag"), Board->GetLastStepDebug().AirDragN.IsZero());
	UE_LOG(LogKiteSurf, Log, TEXT("BodyDragInTheAir: CdA %.2f m^2: %.1f N at 15 m/s in still air (1 s leaves %.2f m/s), %.1f N from a %.1f m/s wind at chest height"),
		Board->RiderDragAreaM2, -StillAirDragN.X, SpeedAfterDragMS, PushN.X, KiteUnits::CmToM(WindAtChestCmS.Size()));

	World->DestroyWorld(false);
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
				// The tack heads to the right of the default +X wind, so that is 1:30.
				if (KiteComp)
				{
					TestNearlyEqual(TEXT("Kite reset to 1:30 in the window the rider feels, on the tack side"), KiteComp->GetClockDeg(), 45.0f, 2.0f);
					TestTrue(FString::Printf(TEXT("Kite reset well above the water (elevation %.1f deg)"), KiteComp->GetElevationDeg()), KiteComp->GetElevationDeg() > 30.0f);
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
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
				}

				const FVector FinalCrestLoc = Pawn->GetActorLocation();
				UE_LOG(LogKiteSurf, Log, TEXT("Board on wave crest: Final Z = %.2f cm (target ≈ 80 cm)"), FinalCrestLoc.Z);
				// At rest the rider floats, so the board rides the wave at the floating depth below its surface.
				TestNearlyEqual(TEXT("Board settles at the floating depth below the wave crest"), (double)FinalCrestLoc.Z, 80.0 - BoardComp->FloatSubmersionCm, 5.0);

				// Place board at wave trough and simulate settling
				Pawn->SetActorLocation(FVector(1500.0f, 0.0f, -60.0f));
				BoardComp->Velocity = FVector::ZeroVector;
				for (int32 i = 0; i < 90; ++i)
				{
					BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
				}

				const FVector FinalTroughLoc = Pawn->GetActorLocation();
				UE_LOG(LogKiteSurf, Log, TEXT("Board on wave trough: Final Z = %.2f cm (target ≈ -80 cm)"), FinalTroughLoc.Z);
				TestNearlyEqual(TEXT("Board settles at the floating depth below the wave trough"), (double)FinalTroughLoc.Z, -80.0 - BoardComp->FloatSubmersionCm, 5.0);
			}
		}

		World->DestroyWorld(false);
	}

	return true;
}


// The board reads the water at five points (docs/physics/plan-2.md item 4): the centre, the nose and
// tail at WaterSampleAlongFraction of its length, and both rails at WaterSampleAcrossCm. It fits a plane
// to them: the height under the board is the plane's (their mean), its pitch and roll follow the plane's
// slope, and the surface's vertical speed under it is the plane's height change along its path. On a
// 1 m, 20 m sine swell (FKiteWaveWaterSurface) at its steepest point, 17.4 deg: the plane has the chord
// slope across the samples, the board pitches with it heading along the swell and rolls with it heading
// across, and moving at 15 m/s along the swell the surface rises under it at the slope times the speed.
// On flat water nothing rises and the board is level.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWaterFittedPlaneUnderTheBoard, "KiteSurf.Water.FittedPlaneUnderTheBoard", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWaterFittedPlaneUnderTheBoard::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteRiderPawn* Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
	UBoardMovementComponent* Board = Pawn ? Pawn->GetBoardMovement() : nullptr;
	TestTrue(TEXT("Rider and board created"), Board != nullptr);
	if (!Board)
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}
	const float AmplitudeCm = 100.0f;
	const float WavelengthCm = 2000.0f;
	const float Step = 1.0f / 240.0f;
	const float K = 2.0f * PI / WavelengthCm;
	const TSharedPtr<FKiteWaveWaterSurface> Swell = MakeShared<FKiteWaveWaterSurface>(0.0f, AmplitudeCm, WavelengthCm, FVector2D(1.0f, 0.0f));
	Board->SetWaterSurface(Swell);
	auto Height = [&](float X) { return AmplitudeCm * FMath::Sin(K * X); };
	const float AlongCm = Board->WaterSampleAlongFraction * Board->BoardLengthCm;
	TestNearlyEqual(TEXT("The nose and tail samples are 0.45 of the 140 cm board from its centre (cm)"), AlongCm, 63.0f, 0.01f);
	TestNearlyEqual(TEXT("and the rail samples 18 cm either side (cm)"), Board->WaterSampleAcrossCm, 18.0f, 0.01f);

	// At rest on the steepest point of the swell, heading along it: x = 0, where it rises at A k.
	auto StandAt = [&](float X, float Yaw, const FVector& Velocity)
	{
		Pawn->SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
		Pawn->SetActorLocation(FVector(X, 0.0f, Height(X)));
		Board->Velocity = Velocity;
		Board->SetBoardState(EBoardState::Planing);
		Board->Simulate(Step);
	};
	StandAt(0.0f, 0.0f, FVector::ZeroVector);
	const float ChordSlope = (Height(AlongCm) - Height(-AlongCm)) / (2.0f * AlongCm);
	const float ExpectedHeightCm = (Height(0.0f) + Height(AlongCm) + Height(-AlongCm) + 2.0f * Height(0.0f)) / 5.0f;
	const FVector ExpectedNormal = FVector(-ChordSlope, 0.0f, 1.0f).GetSafeNormal();
	const float SlopeDeg = FMath::RadiansToDegrees(FMath::Atan(ChordSlope));
	const FBoardStepDebug& Water = Board->GetLastStepDebug();
	TestNearlyEqual(TEXT("The height under the board is the mean of the five samples (cm)"), Board->GetWaterSurfaceHeightCm(), ExpectedHeightCm, 0.01f);
	TestTrue(FString::Printf(TEXT("The plane's normal has the chord slope across the samples, %.2f deg (%s)"), SlopeDeg, *Board->GetWaterSurfaceNormal().ToString()), Board->GetWaterSurfaceNormal().Equals(ExpectedNormal, 1.0e-4f));
	TestNearlyEqual(TEXT("The nose sample is 63 cm ahead, on the surface (cm)"), static_cast<float>(Water.WaterSamplesCm[1].X), AlongCm, 0.01f);
	TestNearlyEqual(TEXT("and on the surface there (cm)"), static_cast<float>(Water.WaterSamplesCm[1].Z), Height(AlongCm), 0.01f);
	TestNearlyEqual(TEXT("The right rail sample is 18 cm to the right (cm)"), static_cast<float>(Water.WaterSamplesCm[3].Y), Board->WaterSampleAcrossCm, 0.01f);
	const float PitchAlongDeg = Pawn->GetActorRotation().Pitch;
	const float RollAlongDeg = Pawn->GetActorRotation().Roll;
	TestNearlyEqual(TEXT("Heading up the slope the board pitches nose up with it (deg)"), PitchAlongDeg, SlopeDeg, 0.05f);
	TestNearlyEqual(TEXT("At rest nothing rises under the board (cm/s)"), Board->GetSurfaceVerticalSpeedCmS(), 0.0f, 0.01f);

	// Heading across the swell (+Y) on the same spot: the slope is now across the board, measured
	// between the rail samples.
	StandAt(0.0f, 90.0f, FVector::ZeroVector);
	const float AcrossCm = Board->WaterSampleAcrossCm;
	const float RailSlopeDeg = FMath::RadiansToDegrees(FMath::Atan((Height(AcrossCm) - Height(-AcrossCm)) / (2.0f * AcrossCm)));
	const float PitchAcrossDeg = Pawn->GetActorRotation().Pitch;
	const float RollAcrossDeg = Pawn->GetActorRotation().Roll;
	TestTrue(FString::Printf(TEXT("Heading across the swell the board rolls with the slope between its rails, %.2f deg (%.2f deg), and does not pitch (%.2f deg)"), RailSlopeDeg, RollAcrossDeg, PitchAcrossDeg),
		FMath::Abs(FMath::Abs(RollAcrossDeg) - RailSlopeDeg) < 0.05f && FMath::Abs(PitchAcrossDeg) < 0.05f);

	// Moving along the swell at 15 m/s from 1 m before the steepest point: after a few steps the
	// surface rises under the board at about the slope times the speed.
	const float SpeedCmS = 1500.0f;
	StandAt(-100.0f, 0.0f, FVector(SpeedCmS, 0.0f, 0.0f));
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Board->Velocity = FVector(SpeedCmS, 0.0f, Board->Velocity.Z);
		Board->Simulate(Step);
	}
	const float X = Pawn->GetActorLocation().X;
	// The plane's height is the mean of the five samples, so it rises at the speed times the slope of
	// that mean: A k cos(k x) (3 + 2 cos(k L')) / 5, L' the nose offset, at the middle of the last step.
	const float MidX = X - 0.5f * SpeedCmS * Step;
	const float ExpectedRiseCmS = SpeedCmS * AmplitudeCm * K * FMath::Cos(K * MidX) * (3.0f + 2.0f * FMath::Cos(K * AlongCm)) / 5.0f;
	UE_LOG(LogKiteSurf, Log, TEXT("FittedPlaneUnderTheBoard: on a %.1f m, %.0f m swell at its steepest the plane's height %.2f cm (centre %.2f), slope %.2f deg (steepest %.2f), pitch %.2f / roll %.2f heading along, pitch %.2f / roll %.2f across; at %.0f m/s the surface rises %.1f cm/s under the board (expected %.1f)"),
		AmplitudeCm / 100.0f, WavelengthCm / 100.0f, ExpectedHeightCm, Height(0.0f), SlopeDeg, FMath::RadiansToDegrees(FMath::Atan(AmplitudeCm * K)), PitchAlongDeg, RollAlongDeg, PitchAcrossDeg, RollAcrossDeg,
		SpeedCmS / 100.0f, Board->GetSurfaceVerticalSpeedCmS(), ExpectedRiseCmS);
	TestNearlyEqual(TEXT("Riding along the swell the surface rises under the board at the speed times the plane's slope (cm/s)"), Board->GetSurfaceVerticalSpeedCmS(), ExpectedRiseCmS, 0.01f * FMath::Abs(ExpectedRiseCmS));

	// Flat water: level, and nothing rises.
	Board->SetWaterSurface(MakeShared<FKiteFlatWaterSurface>(0.0f));
	StandAt(0.0f, 30.0f, FVector(SpeedCmS, 0.0f, 0.0f));
	Board->Simulate(Step);
	TestNearlyEqual(TEXT("On flat water nothing rises under a moving board (cm/s)"), Board->GetSurfaceVerticalSpeedCmS(), 0.0f, 0.001f);
	TestTrue(TEXT("and the plane is level"), Board->GetWaterSurfaceNormal().Equals(FVector::UpVector, 1.0e-6f));

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
