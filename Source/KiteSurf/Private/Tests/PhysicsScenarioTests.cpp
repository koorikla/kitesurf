#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurf.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfUnits.h"
#include "WindComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// Whole-rig scenarios with the numbers a rider would recognise, checked against the real-world
// ranges in docs/physics/research.md, plus the invariants every one of them must keep: nothing
// goes NaN, the kite is never further from the rider than its lines reach, and the lines never
// push.

namespace KiteScenario
{
	constexpr float DefaultFrameSeconds = 1.0f / 60.0f;

	/** Rider of 78 kg on a 3 kg board: the mass the board component carries. */
	constexpr float RiderAndBoardMassKg = 81.0f;

	/** The kite may be this much past line length (cm), on top of how far the rider moved in one step (see FInvariants). */
	constexpr float LineLengthToleranceCm = 1.0f;

	/** Speed at which the ride starts on a beam reach (kn), as the game mode starts it. */
	constexpr float StartSpeedKnots = 12.0f;

	/**
	 * What must hold at every frame of every scenario.
	 *
	 * The lines hold the kite at their length around the rider as the kite's step finds them; the
	 * board then moves the rider for the rest of that step. So between steps the kite can be past
	 * line length by at most the rider's travel in one step, and the check allows exactly that plus
	 * LineLengthToleranceCm.
	 */
	struct FInvariants
	{
		bool bNaN = false;
		float MaxLineExcessCm = -1.0e6f;   // kite distance minus line length minus the rider's travel in one step
		float MaxStretchCm = -1.0e6f;      // kite distance minus line length, for the log
		float MinTensionN = 1.0e6f;
		int32 Frames = 0;

		void Check(const AKiteRiderPawn* Pawn, const UKiteComponent* Kite, const UBoardMovementComponent* Board)
		{
			const FVector RiderPos = Pawn->GetActorLocation();
			const FVector RiderVel = Board->Velocity;
			const FVector KitePos = Kite->GetKiteWorldPosition();
			const FVector KiteVel = Kite->GetKiteVelocity();
			bNaN |= RiderPos.ContainsNaN() || RiderVel.ContainsNaN() || KitePos.ContainsNaN() || KiteVel.ContainsNaN()
				|| FMath::IsNaN(Kite->GetLineTensionN()) || Kite->GetLineForce().ContainsNaN();
			const float StretchCm = FVector::Dist(KitePos, RiderPos) - Kite->LineLengthCm;
			MaxStretchCm = FMath::Max(MaxStretchCm, StretchCm);
			MaxLineExcessCm = FMath::Max(MaxLineExcessCm, StretchCm - RiderVel.Size() * Pawn->SimStepSeconds);
			MinTensionN = FMath::Min(MinTensionN, Kite->GetLineTensionN());
			++Frames;
		}

		void Assert(FAutomationTestBase& Test, const FString& What) const
		{
			UE_LOG(LogKiteSurf, Log, TEXT("Invariants (%s): %d frames, NaN %d, most stretch %.3f cm, most excess over the one-step allowance %.3f cm, least tension %.1f N"),
				*What, Frames, bNaN, MaxStretchCm, MaxLineExcessCm, MinTensionN);
			Test.TestFalse(FString::Printf(TEXT("%s: nothing went NaN"), *What), bNaN);
			Test.TestTrue(FString::Printf(TEXT("%s: the kite was never past line length by more than %.0f cm plus the rider's travel in one step (worst %.2f cm; stretch %.2f cm)"),
				*What, LineLengthToleranceCm, MaxLineExcessCm, MaxStretchCm), MaxLineExcessCm <= LineLengthToleranceCm);
			Test.TestTrue(FString::Printf(TEXT("%s: the tension was never negative (least %.1f N)"), *What, MinTensionN), MinTensionN >= 0.0f);
			Test.TestTrue(FString::Printf(TEXT("%s: the invariants were checked (%d frames)"), *What, Frames), Frames > 0);
		}
	};

	/** A rider on the water in steady wind along +X, started on a beam reach as the game mode does, stepped one frame at a time. */
	struct FScenarioRide
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		UWindComponent* Wind = nullptr;
		float FrameSeconds = DefaultFrameSeconds;
		FInvariants Invariants;
		/** True while the lines have been tight at every frame since this was last set to true. */
		bool bTautThroughout = true;

		FScenarioRide(float WindKnots, float KiteAreaM2, float InFrameSeconds = DefaultFrameSeconds, float TackSide = 1.0f)
			: FrameSeconds(InFrameSeconds)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (!Pawn)
			{
				return;
			}
			Kite = Pawn->GetKite();
			Board = Pawn->GetBoardMovement();
			Wind = Pawn->GetWind();
			if (!Kite || !Board || !Wind)
			{
				return;
			}
			Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(WindKnots), 0.0f, 0.0f);
			Wind->GustStrength = 0.0f;
			Wind->DirectionDriftDeg = 0.0f;
			Board->MassKg = RiderAndBoardMassKg;
			Kite->SetKiteSize(KiteAreaM2);
			AKiteSurfGameMode::InitializeRide(Pawn, KiteUnits::KnotsToCmS(StartSpeedKnots), TackSide);
			// These rides last longer than the kite takes to drift up to 12 with the bar centred; the
			// park-hold assist keeps it where the start put it, as a rider's hands would, so the
			// numbers are about riding.
			Kite->bParkHoldAssist = true;
		}

		~FScenarioRide()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Wind; }

		void Frame()
		{
			Pawn->Tick(FrameSeconds);
			Invariants.Check(Pawn, Kite, Board);
			bTautThroughout &= Kite->AreLinesTaut();
		}

		void Simulate(float Seconds)
		{
			const int32 Frames = FMath::RoundToInt(Seconds / FrameSeconds);
			for (int32 Index = 0; Index < Frames; ++Index)
			{
				Frame();
			}
		}

		/** Runs frames until the simulation has reached this time (s), whatever the frame length. */
		void SimulateUntil(float SimSeconds)
		{
			while (!HasReached(SimSeconds))
			{
				Frame();
			}
		}

		bool HasReached(float SimSeconds) const
		{
			return Pawn->GetSimTimeSeconds() + 0.5f * Pawn->SimStepSeconds >= SimSeconds;
		}

		float SpeedKnots() const { return KiteUnits::CmSToKnots(Board->Velocity.Size2D()); }
	};
}

using namespace KiteScenario;

// A 12 m^2 kite and an 81 kg rider and board across the wind in 15 and 20 kn: the speed, pull and
// kite position a rider would expect. Research: riding pull 0.5 to 1.0 body weights
// (docs/physics/research.md 3.1).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPhysicsSteadyRideAcross, "KiteSurf.Physics.SteadyRideAcross", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPhysicsSteadyRideAcross::RunTest(const FString& Parameters)
{
	const float KiteAreaM2 = 12.0f;
	const float RideSeconds = 30.0f;
	for (const float WindKnots : { 15.0f, 20.0f })
	{
		FScenarioRide Ride(WindKnots, KiteAreaM2);
		TestTrue(TEXT("Ride created"), Ride.IsValid());
		if (!Ride.IsValid())
		{
			return false;
		}
		Ride.Simulate(RideSeconds);

		const float SpeedKnots = Ride.SpeedKnots();
		const float TensionN = Ride.Kite->GetLineTensionN();
		const float ElevationDeg = Ride.Kite->GetElevationDeg();
		UE_LOG(LogKiteSurf, Log, TEXT("SteadyRideAcross: %.0f kn wind, %.0f m^2, %.0f kg: %.1f kn, %.0f N (%.2f body weights), kite elevation %.1f deg, azimuth %.1f deg, clock %.1f, alpha %.1f deg, taut throughout %d, worst line excess %.2f cm"),
			WindKnots, KiteAreaM2, Ride.Board->MassKg, SpeedKnots, TensionN, TensionN / (Ride.Board->MassKg * KiteUnits::GravityMS2),
			ElevationDeg, Ride.Kite->GetAzimuthDeg(), Ride.Kite->GetClockDeg(), Ride.Kite->GetAngleOfAttackDeg(), Ride.bTautThroughout, Ride.Invariants.MaxLineExcessCm);

		const FString What = FString::Printf(TEXT("%.0f kn"), WindKnots);
		const float BodyWeights = TensionN / (Ride.Board->MassKg * KiteUnits::GravityMS2);
		TestTrue(FString::Printf(TEXT("%s: board speed %.1f kn is within 12 to 25 kn"), *What, SpeedKnots), SpeedKnots >= 12.0f && SpeedKnots <= 25.0f);
		TestTrue(FString::Printf(TEXT("%s: the pull, %.0f N, is 0.5 to 1.0 body weights (%.2f)"), *What, TensionN, BodyWeights), BodyWeights >= 0.5f && BodyWeights <= 1.0f);
		TestTrue(FString::Printf(TEXT("%s: kite elevation %.1f deg is within 15 to 50 deg"), *What, ElevationDeg), ElevationDeg >= 15.0f && ElevationDeg <= 50.0f);
		TestTrue(FString::Printf(TEXT("%s: the lines were tight throughout"), *What), Ride.bTautThroughout);
		TestTrue(FString::Printf(TEXT("%s: still planing"), *What), Ride.Board->IsPlaning());
		Ride.Invariants.Assert(*this, What);
	}
	return true;
}

// Held by the fins 25 deg above a beam reach, the rider makes ground against the wind.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPhysicsUpwindAtEdgeAngle, "KiteSurf.Physics.UpwindAtEdgeAngle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPhysicsUpwindAtEdgeAngle::RunTest(const FString& Parameters)
{
	const float WindKnots = 15.0f;
	const float AboveBeamReachDeg = 25.0f;
	const float RideSeconds = 15.0f;
	FScenarioRide Ride(WindKnots, 12.0f);
	TestTrue(TEXT("Ride created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}

	// The start is a beam reach to the right (+Y) with the wind along +X; turn the board 25 deg
	// towards the wind and leave the edge input neutral, so the fins hold the course.
	const FRotator BeamReach = Ride.Pawn->GetActorRotation();
	const FRotator Heading(0.0f, BeamReach.Yaw + AboveBeamReachDeg, 0.0f);
	Ride.Pawn->SetActorRotation(Heading);
	Ride.Board->Velocity = Heading.Vector() * Ride.Board->Velocity.Size2D();
	Ride.Pawn->EdgeBoard(0.0f);
	Ride.Simulate(RideSeconds);

	const FVector Velocity = Ride.Board->Velocity;
	const float UpwindMS = KiteUnits::CmToM(-Velocity.X);
	const float ForwardCmS = Ride.Board->GetForwardSpeed();
	const float LeewayDeg = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(Ride.Board->GetLateralSpeed()), FMath::Abs(ForwardCmS)));
	const float HeadingNowDeg = Ride.Pawn->GetActorRotation().Yaw;
	UE_LOG(LogKiteSurf, Log, TEXT("UpwindAtEdgeAngle: %.0f deg above a beam reach in %.0f kn: %.1f kn, %.2f m/s made good upwind, leeway %.1f deg, heading %.1f deg (started %.1f), %.0f N, kite clock %.0f, taut throughout %d"),
		AboveBeamReachDeg, WindKnots, Ride.SpeedKnots(), UpwindMS, LeewayDeg, HeadingNowDeg, Heading.Yaw, Ride.Kite->GetLineTensionN(), Ride.Kite->GetClockDeg(), Ride.bTautThroughout);

	TestTrue(FString::Printf(TEXT("Velocity made good against the wind is at least 1.5 m/s (%.2f m/s)"), UpwindMS), UpwindMS >= 1.5f);
	TestTrue(FString::Printf(TEXT("Leeway is under 10 deg (%.1f deg)"), LeewayDeg), LeewayDeg < 10.0f);
	TestTrue(TEXT("Still planing"), Ride.Board->IsPlaning());
	Ride.Invariants.Assert(*this, TEXT("Upwind"));
	return true;
}

// A 9 m^2 kite parked at the zenith in 30 kn over a standing rider, sheeted in and out.
//
// Research (docs/physics/research.md 1.3, an estimate): sheeted in about 0.94 kN, the lift of the
// projected area (0.72 of flat) at Cl 1.0 in 15.4 m/s; sheeted out about 0.38 kN. The kite here sits at
// 23 m, where the wind is 10% stronger than the 10 m figure, and the target range is 0.85 to 1.1 kN
// (docs/physics/plan-2.md item 1). Before phase 2 the forces used the flat area and this pulled 1.66 kN.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPhysicsParkedAtZenith, "KiteSurf.Physics.ParkedAtZenith", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPhysicsParkedAtZenith::RunTest(const FString& Parameters)
{
	const float WindKnots = 30.0f;
	const float KiteAreaM2 = 9.0f;
	const float SettleSeconds = 10.0f;
	const float AverageSeconds = 2.0f;
	const float SheetedInMinN = 850.0f;   // research about 0.94 kN at the 10 m wind (estimate)
	const float SheetedInMaxN = 1100.0f;
	const float SheetedOutMaxN = 450.0f;  // research about 0.38 kN

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteRiderPawn* Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
	UKiteComponent* Kite = Pawn ? Pawn->GetKite() : nullptr;
	UWindComponent* Wind = Pawn ? Pawn->GetWind() : nullptr;
	UBoardMovementComponent* Board = Pawn ? Pawn->GetBoardMovement() : nullptr;
	TestTrue(TEXT("Rider, kite and wind created"), Kite && Wind && Board);
	if (!Kite || !Wind || !Board)
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}
	Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(WindKnots), 0.0f, 0.0f);
	Wind->GustStrength = 0.0f;
	Wind->DirectionDriftDeg = 0.0f;
	Kite->SetKiteSize(KiteAreaM2);
	Kite->bParkHoldAssist = true; // held at 12, where it was put

	// The rider stands still: only the kite is stepped.
	FInvariants Invariants;
	struct FSettled { float TensionN; float AlphaDeg; float ElevationDeg; float SpeedCmS; bool bTaut; };
	auto Settle = [&](float Sheet) -> FSettled
	{
		Kite->SheetKite(Sheet);
		const int32 SettleFrames = FMath::RoundToInt(SettleSeconds / DefaultFrameSeconds);
		const int32 AverageFrames = FMath::RoundToInt(AverageSeconds / DefaultFrameSeconds);
		float TensionSum = 0.0f;
		bool bTaut = true;
		for (int32 Index = 0; Index < SettleFrames + AverageFrames; ++Index)
		{
			Kite->UpdateKite(DefaultFrameSeconds);
			Invariants.Check(Pawn, Kite, Board);
			if (Index >= SettleFrames)
			{
				TensionSum += Kite->GetLineTensionN();
				bTaut &= Kite->AreLinesTaut();
			}
		}
		return { TensionSum / AverageFrames, Kite->GetAngleOfAttackDeg(), Kite->GetElevationDeg(), Kite->GetKiteVelocity().Size(), bTaut };
	};

	Kite->SetWindowPosition(0.0f, 10.0f);
	const FSettled In = Settle(1.0f);
	const FSettled Out = Settle(0.0f);
	UE_LOG(LogKiteSurf, Log, TEXT("ParkedAtZenith: %.0f m^2 (%.1f m^2 projected) in %.0f kn, sheeted in %.0f N (alpha %.1f deg, elevation %.1f deg, %.0f cm/s, taut %d), sheeted out %.0f N (alpha %.1f deg, elevation %.1f deg, %.0f cm/s, taut %d)"),
		KiteAreaM2, Kite->GetProjectedAreaM2(), WindKnots, In.TensionN, In.AlphaDeg, In.ElevationDeg, In.SpeedCmS, In.bTaut,
		Out.TensionN, Out.AlphaDeg, Out.ElevationDeg, Out.SpeedCmS, Out.bTaut);

	TestTrue(FString::Printf(TEXT("Sheeted in it pulls %.0f to %.0f N (%.0f N)"), SheetedInMinN, SheetedInMaxN, In.TensionN), In.TensionN >= SheetedInMinN && In.TensionN <= SheetedInMaxN);
	TestTrue(TEXT("Sheeted in the lines are tight"), In.bTaut);
	TestTrue(FString::Printf(TEXT("Sheeted in it is not stalled (alpha %.1f deg)"), In.AlphaDeg), In.AlphaDeg < Kite->StallAngleDeg);
	TestTrue(FString::Printf(TEXT("Sheeted in it is overhead (elevation %.1f deg)"), In.ElevationDeg), In.ElevationDeg > 60.0f);
	TestTrue(FString::Printf(TEXT("Sheeted out it pulls under %.0f N (%.0f N)"), SheetedOutMaxN, Out.TensionN), Out.TensionN < SheetedOutMaxN);
	TestTrue(TEXT("Sheeted out the lines are tight"), Out.bTaut);
	TestTrue(FString::Printf(TEXT("Sheeted out it is not stalled (alpha %.1f deg)"), Out.AlphaDeg), Out.AlphaDeg < Kite->StallAngleDeg);
	Invariants.Assert(*this, TEXT("Parked"));

	World->DestroyWorld(false);
	return true;
}

namespace KiteScenario
{
	/** A standing rider whose kite is stepped on its own, in steady wind along +X. */
	struct FStandingKite
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		FInvariants Invariants;

		FStandingKite(float WindKnots, float KiteAreaM2)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			Kite = Pawn ? Pawn->GetKite() : nullptr;
			Board = Pawn ? Pawn->GetBoardMovement() : nullptr;
			UWindComponent* Wind = Pawn ? Pawn->GetWind() : nullptr;
			if (!Kite || !Board || !Wind)
			{
				Kite = nullptr;
				return;
			}
			Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(WindKnots), 0.0f, 0.0f);
			Wind->GustStrength = 0.0f;
			Wind->DirectionDriftDeg = 0.0f;
			Kite->SetKiteSize(KiteAreaM2);
			Kite->bParkHoldAssist = true;
		}

		~FStandingKite()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Kite != nullptr; }

		void Fly(float Seconds)
		{
			for (int32 Index = 0, Frames = FMath::RoundToInt(Seconds / DefaultFrameSeconds); Index < Frames; ++Index)
			{
				Kite->UpdateKite(DefaultFrameSeconds);
				Invariants.Check(Pawn, Kite, Board);
			}
		}
	};
}

// The bar's trim, measured where it was set: a 9 m^2 kite parked at the window edge in 20 kn. Bar in
// it flies 5 to 6 deg short of the stall; bar out, in the same place, the flow meets the canopy at
// about -5 deg, a luff margin and not a collapse, and the kite then settles deeper in the window and
// keeps flying. The difference, the bar's throw, is 15 to 20 deg (research 12 to 20,
// docs/physics/research.md 1.5; docs/physics/plan-2.md item 1).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteTrimSetsTheAngleOfAttack, "KiteSurf.Kite.TrimSetsTheAngleOfAttack", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteTrimSetsTheAngleOfAttack::RunTest(const FString& Parameters)
{
	const float WindKnots = 20.0f;
	const float KiteAreaM2 = 9.0f;
	const float SettleSeconds = 8.0f;
	for (const float ClockDeg : { 45.0f, 70.0f })
	{
		FStandingKite Standing(WindKnots, KiteAreaM2);
		TestTrue(TEXT("Rider and kite created"), Standing.IsValid());
		if (!Standing.IsValid())
		{
			return false;
		}
		UKiteComponent* Kite = Standing.Kite;
		Kite->SetWindowPosition(ClockDeg, 10.0f);
		Kite->SheetKite(1.0f);
		Standing.Fly(SettleSeconds);
		const float InAlphaDeg = Kite->GetAngleOfAttackDeg();
		const float InDepthDeg = Kite->GetWindowDepthDeg();

		// The bar right out, before the kite has moved: a zero-length step works out the air on the
		// canopy where it is.
		Kite->SheetKite(0.0f);
		Kite->UpdateKite(0.0f);
		const float OutAlphaDeg = Kite->GetAngleOfAttackDeg();
		const float ThrowDeg = InAlphaDeg - OutAlphaDeg;

		Standing.Fly(SettleSeconds);
		const float SettledOutAlphaDeg = Kite->GetAngleOfAttackDeg();
		UE_LOG(LogKiteSurf, Log, TEXT("TrimSetsTheAngleOfAttack: %.0f m^2 in %.0f kn at clock %.0f: bar in alpha %.1f deg (%.1f short of the %.0f deg stall) at depth %.1f deg, bar out there %.1f deg, throw %.1f deg; bar out settled at alpha %.1f deg, depth %.1f deg, %.0f N"),
			KiteAreaM2, WindKnots, ClockDeg, InAlphaDeg, Kite->StallAngleDeg - InAlphaDeg, Kite->StallAngleDeg, InDepthDeg, OutAlphaDeg, ThrowDeg,
			SettledOutAlphaDeg, Kite->GetWindowDepthDeg(), Kite->GetLineTensionN());

		const FString What = FString::Printf(TEXT("Clock %.0f"), ClockDeg);
		TestTrue(FString::Printf(TEXT("%s: bar in, the kite flies 5 to 6 deg short of the stall (%.1f deg)"), *What, Kite->StallAngleDeg - InAlphaDeg),
			InAlphaDeg >= Kite->StallAngleDeg - 6.0f && InAlphaDeg <= Kite->StallAngleDeg - 5.0f);
		TestTrue(FString::Printf(TEXT("%s: bar out in the same place, about -5 deg (%.1f deg)"), *What, OutAlphaDeg), OutAlphaDeg >= -6.0f && OutAlphaDeg <= -4.0f);
		TestTrue(FString::Printf(TEXT("%s: the bar's throw is 15 to 20 deg (%.1f deg)"), *What, ThrowDeg), ThrowDeg >= 15.0f && ThrowDeg <= 20.0f);
		TestTrue(FString::Printf(TEXT("%s: bar out, it settles deeper and keeps flying: lines tight, lifting, not stalled (alpha %.1f deg)"), *What, SettledOutAlphaDeg),
			Kite->AreLinesTaut() && !Kite->IsCrashed() && SettledOutAlphaDeg > Kite->ZeroLiftAngleDeg && SettledOutAlphaDeg < Kite->StallAngleDeg);
		Standing.Invariants.Assert(*this, What);
	}
	return true;
}

// The air acts on the projected area of the arched canopy, AreaM2 * ProjectedAreaRatio; the mass does
// not change with it. Read back from the step: lift over (0.5 rho Cl v^2) is the area the air saw.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteForceFollowsProjectedArea, "KiteSurf.Kite.ForceFollowsProjectedArea", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteForceFollowsProjectedArea::RunTest(const FString& Parameters)
{
	const float KiteAreaM2 = 9.0f;
	float TensionN[2] = { 0.0f, 0.0f };
	const float Ratios[2] = { 1.0f, 0.0f }; // 0: the default
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FStandingKite Standing(20.0f, KiteAreaM2);
		TestTrue(TEXT("Rider and kite created"), Standing.IsValid());
		if (!Standing.IsValid())
		{
			return false;
		}
		UKiteComponent* Kite = Standing.Kite;
		const float MassKg = Kite->MassKg;
		if (Ratios[Index] > 0.0f)
		{
			Kite->ProjectedAreaRatio = Ratios[Index];
		}
		TestNearlyEqual(TEXT("The projected area is the flat area times the ratio (m^2)"), Kite->GetProjectedAreaM2(), KiteAreaM2 * Kite->ProjectedAreaRatio, 1.0e-4f);
		Kite->SetWindowPosition(0.0f, 10.0f);
		Kite->SheetKite(0.7f);
		Standing.Fly(6.0f);

		const FKiteStepDebug& Step = Kite->GetLastStepDebug();
		const float AirspeedMS = Kite->GetAirspeedCmS() / KiteUnits::CmPerM;
		const float AreaSeenM2 = Step.LiftN.Size() / FMath::Max(0.5f * KiteUnits::AirDensityKgM3 * Step.LiftCoefficient * FMath::Square(AirspeedMS), KINDA_SMALL_NUMBER);
		TensionN[Index] = Kite->GetLineTensionN();
		UE_LOG(LogKiteSurf, Log, TEXT("ForceFollowsProjectedArea: %.0f m^2 at ratio %.2f: the lift saw %.3f m^2 (projected %.3f m^2), %.0f N at alpha %.1f deg, %.1f m/s, mass %.2f kg"),
			KiteAreaM2, Kite->ProjectedAreaRatio, AreaSeenM2, Kite->GetProjectedAreaM2(), TensionN[Index], Kite->GetAngleOfAttackDeg(), AirspeedMS, Kite->MassKg);
		TestNearlyEqual(FString::Printf(TEXT("At ratio %.2f the lift acts on the projected area (m^2)"), Kite->ProjectedAreaRatio), AreaSeenM2, Kite->GetProjectedAreaM2(), 0.01f * Kite->GetProjectedAreaM2());
		TestEqual(TEXT("The ratio does not change the kite's mass (kg)"), Kite->MassKg, MassKg);
		TestTrue(TEXT("and it flies: lines tight"), Kite->AreLinesTaut());
		Standing.Invariants.Assert(*this, FString::Printf(TEXT("Ratio %.2f"), Kite->ProjectedAreaRatio));
	}
	TestTrue(FString::Printf(TEXT("The kite on its projected area pulls less than on its flat area (%.0f N against %.0f N)"), TensionN[1], TensionN[0]), TensionN[1] < 0.8f * TensionN[0]);
	return true;
}

// The assist judges where "out of the window" is from the wind the rider feels across the water: the
// true wind less their horizontal velocity. A rider going straight up or down at 5 m/s (as in a
// jump) gets the same command from it as one standing still; before phase 2 the climb and the fall
// swung the assist's frame and flipped its command from side to side (docs/physics/plan-2.md item 2,
// A1). Each case is one zero-length step from the same placed kite, so only the rider's motion
// differs. The kites are high enough that the floor rule, which does use the real vertical speeds,
// does not come in.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPhysicsAssistFrameIsHorizontal, "KiteSurf.Physics.AssistFrameIsHorizontal", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPhysicsAssistFrameIsHorizontal::RunTest(const FString& Parameters)
{
	const float WindKnots = 15.0f;
	const float KiteAreaM2 = 12.0f;
	const float VerticalSpeedCmS = 500.0f;
	const float SameCommand = 1.0e-4f;
	struct FCase
	{
		float ClockDeg;
		float Bar;
		bool bParkHold;
	};
	const FCase Cases[] = {
		{ 0.0f, 0.0f, false },   // bar centred at the zenith
		{ 45.0f, 0.0f, false },  // bar centred, drifting to 12
		{ -30.0f, 0.0f, false },
		{ 45.0f, 0.0f, true },   // bar centred, held where it is
		{ 45.0f, -0.4f, false }, // bar part way over: travel
		{ -30.0f, 0.3f, false },
	};
	const float VerticalSpeeds[3] = { 0.0f, VerticalSpeedCmS, -VerticalSpeedCmS };
	int32 Unsaturated = 0;
	for (const FCase& Case : Cases)
	{
		float Command[3] = { 0.0f, 0.0f, 0.0f };
		for (int32 Motion = 0; Motion < 3; ++Motion)
		{
			FStandingKite Standing(WindKnots, KiteAreaM2);
			TestTrue(TEXT("Rider and kite created"), Standing.IsValid());
			if (!Standing.IsValid())
			{
				return false;
			}
			UKiteComponent* Kite = Standing.Kite;
			Kite->bParkHoldAssist = Case.bParkHold;
			Kite->SetWindowPosition(Case.ClockDeg, 10.0f);
			Kite->SheetKite(0.7f);
			Kite->UpdateKite(0.0f); // placed, with the rider at rest
			Standing.Board->Velocity = FVector(0.0f, 0.0f, VerticalSpeeds[Motion]);
			Kite->SteerKite(Case.Bar);
			Kite->UpdateKite(0.0f);
			Command[Motion] = Kite->GetAppliedSteer();
		}
		UE_LOG(LogKiteSurf, Log, TEXT("AssistFrameIsHorizontal: clock %.0f, bar %.1f, park-hold %d: command standing %.4f, climbing at %.0f m/s %.4f, falling %.4f"),
			Case.ClockDeg, Case.Bar, Case.bParkHold, Command[0], VerticalSpeedCmS / 100.0f, Command[1], Command[2]);
		const FString What = FString::Printf(TEXT("Clock %.0f, bar %.1f, park-hold %d"), Case.ClockDeg, Case.Bar, Case.bParkHold);
		TestNearlyEqual(FString::Printf(TEXT("%s: climbing, the assist's command is the standing one"), *What), Command[1], Command[0], SameCommand);
		TestNearlyEqual(FString::Printf(TEXT("%s: falling, the assist's command is the standing one"), *What), Command[2], Command[0], SameCommand);
		Unsaturated += FMath::Abs(Command[0]) < 0.99f ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("Most cases are inside the assist's range, where a different frame would show (%d)"), Unsaturated), Unsaturated >= 4);
	return true;
}

// A gust arriving mid-ride: the pull and the speed go up with it, and nothing breaks.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPhysicsGustHitsMidRide, "KiteSurf.Physics.GustHitsMidRide", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPhysicsGustHitsMidRide::RunTest(const FString& Parameters)
{
	const float WindKnots = 15.0f;
	const float GustKnots = 22.0f;
	const float RampSeconds = 1.0f;
	const float WindowSeconds = 5.0f;
	FScenarioRide Ride(WindKnots, 12.0f);
	TestTrue(TEXT("Ride created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	Ride.Simulate(10.0f);
	const float SteadyTensionN = Ride.Kite->GetLineTensionN();
	const float SteadySpeedKnots = Ride.SpeedKnots();
	Ride.bTautThroughout = true;

	// The gust: the base wind rises from 15 to 22 kn over a second and stays there.
	float PeakTensionN = 0.0f;
	float PeakSpeedKnots = 0.0f;
	const int32 Frames = FMath::RoundToInt(WindowSeconds / Ride.FrameSeconds);
	for (int32 Index = 1; Index <= Frames; ++Index)
	{
		const float Ramp = FMath::Clamp(Index * Ride.FrameSeconds / RampSeconds, 0.0f, 1.0f);
		Ride.Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(FMath::Lerp(WindKnots, GustKnots, Ramp)), 0.0f, 0.0f);
		Ride.Frame();
		PeakTensionN = FMath::Max(PeakTensionN, Ride.Kite->GetLineTensionN());
		PeakSpeedKnots = FMath::Max(PeakSpeedKnots, Ride.SpeedKnots());
	}
	const float GustTensionN = Ride.Kite->GetLineTensionN();
	const float GustSpeedKnots = Ride.SpeedKnots();
	UE_LOG(LogKiteSurf, Log, TEXT("GustHitsMidRide: %.0f -> %.0f kn over %.0f s: tension %.0f N -> %.0f N after %.0f s (peak %.0f N), speed %.1f -> %.1f kn (peak %.1f kn), taut throughout %d, state %d"),
		WindKnots, GustKnots, RampSeconds, SteadyTensionN, GustTensionN, WindowSeconds, PeakTensionN, SteadySpeedKnots, GustSpeedKnots, PeakSpeedKnots, Ride.bTautThroughout, static_cast<int32>(Ride.Board->GetBoardState()));

	TestTrue(FString::Printf(TEXT("Within %.0f s the tension has risen by at least half (%.0f N -> %.0f N)"), WindowSeconds, SteadyTensionN, GustTensionN), GustTensionN >= 1.5f * SteadyTensionN);
	TestTrue(FString::Printf(TEXT("and the board is at least 2 kn faster (%.1f -> %.1f kn)"), SteadySpeedKnots, GustSpeedKnots), GustSpeedKnots >= SteadySpeedKnots + 2.0f);
	TestTrue(TEXT("The lines stayed tight through the gust"), Ride.bTautThroughout);
	Ride.Invariants.Assert(*this, TEXT("Gust"));
	return true;
}

// The wind dies mid-ride: within a few seconds the lines go slack or the kite is in the water,
// and slack lines do not pull.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPhysicsLullDropsKite, "KiteSurf.Physics.LullDropsKite", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPhysicsLullDropsKite::RunTest(const FString& Parameters)
{
	const float WindKnots = 15.0f;
	const float LullKnots = 3.0f;
	const float WindowSeconds = 10.0f;
	FScenarioRide Ride(WindKnots, 12.0f);
	TestTrue(TEXT("Ride created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	Ride.Simulate(10.0f);
	TestTrue(TEXT("Riding with tight lines before the lull"), Ride.Kite->AreLinesTaut() && Ride.Kite->GetLineTensionN() > 100.0f);

	Ride.Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(LullKnots), 0.0f, 0.0f);
	float SecondsToDrop = -1.0f;
	float MostTensionWhileSlackN = 0.0f;
	int32 SlackFrames = 0;
	const int32 Frames = FMath::RoundToInt(WindowSeconds / Ride.FrameSeconds);
	for (int32 Index = 1; Index <= Frames; ++Index)
	{
		Ride.Frame();
		const bool bDropped = !Ride.Kite->AreLinesTaut() || Ride.Kite->IsCrashed();
		if (bDropped && SecondsToDrop < 0.0f)
		{
			SecondsToDrop = Index * Ride.FrameSeconds;
		}
		if (!Ride.Kite->AreLinesTaut())
		{
			++SlackFrames;
			MostTensionWhileSlackN = FMath::Max(MostTensionWhileSlackN, Ride.Kite->GetLineTensionN());
		}
	}
	UE_LOG(LogKiteSurf, Log, TEXT("LullDropsKite: %.0f -> %.0f kn: slack or down after %.2f s, %d slack frames, most tension while slack %.1f N, kite crashed %d, board %.1f kn"),
		WindKnots, LullKnots, SecondsToDrop, SlackFrames, MostTensionWhileSlackN, Ride.Kite->IsCrashed(), Ride.SpeedKnots());

	TestTrue(FString::Printf(TEXT("Within %.0f s the lines go slack or the kite is down (%.2f s)"), WindowSeconds, SecondsToDrop), SecondsToDrop > 0.0f);
	TestTrue(TEXT("Slack lines were seen"), SlackFrames > 0);
	TestEqual(TEXT("Slack lines never pull"), MostTensionWhileSlackN, 0.0f);
	Ride.Invariants.Assert(*this, TEXT("Lull"));
	return true;
}

namespace KiteScenario
{
	struct FScriptedRideResult
	{
		FVector Travelled = FVector::ZeroVector;
		float SpeedKnots = 0.0f;
		float ApexCm = 0.0f;
		float EndSimSeconds = 0.0f;
		bool bPopped = false;
		FInvariants Invariants;
	};

	/**
	 * The same ride at any frame length: 10 s steady, a send with the bar hard left and the weight
	 * on the tail for 1.2 s, then the bar in, a pop and the bar centred, and on to 20 s. Inputs go in
	 * at the same simulation time whatever the frame length.
	 */
	FScriptedRideResult RunScriptedRide(float FrameSeconds)
	{
		const float WindKnots = 20.0f;
		const float KiteAreaM2 = 9.0f;
		const float SendAtSeconds = 10.0f;
		const float PopAtSeconds = 11.2f;
		const float EndAtSeconds = 20.0f;

		FScriptedRideResult Result;
		FScenarioRide Ride(WindKnots, KiteAreaM2, FrameSeconds);
		if (!Ride.IsValid())
		{
			return Result;
		}
		const FVector Start = Ride.Pawn->GetActorLocation();
		Ride.SimulateUntil(SendAtSeconds);
		Ride.Pawn->SteerKite(-1.0f);
		Ride.Board->SetWeightShift(-1.0f);
		Ride.SimulateUntil(PopAtSeconds);
		Ride.Pawn->SheetKite(1.0f);
		Result.bPopped = Ride.Board->Jump() == EJumpRejectReason::None;
		Ride.Board->SetWeightShift(0.0f);
		Ride.Pawn->SteerKite(0.0f);
		while (!Ride.HasReached(EndAtSeconds))
		{
			Ride.Frame();
			Result.ApexCm = FMath::Max(Result.ApexCm, Ride.Board->GetCurrentJumpHeight());
		}
		Result.Travelled = Ride.Pawn->GetActorLocation() - Start;
		Result.SpeedKnots = Ride.SpeedKnots();
		Result.EndSimSeconds = Ride.Pawn->GetSimTimeSeconds();
		Result.Invariants = Ride.Invariants;
		return Result;
	}
}

// The same ride at 30, 60 and 120 frames a second comes out the same: the rig is stepped at a fixed
// rate whatever the frame rate.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPhysicsStepRateIndependent, "KiteSurf.Physics.StepRateIndependent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPhysicsStepRateIndependent::RunTest(const FString& Parameters)
{
	const float FrameRates[3] = { 30.0f, 60.0f, 120.0f };
	FScriptedRideResult Results[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Results[Index] = RunScriptedRide(1.0f / FrameRates[Index]);
		const FScriptedRideResult& Run = Results[Index];
		UE_LOG(LogKiteSurf, Log, TEXT("StepRateIndependent: %.0f fps: popped %d, apex %.1f cm, travelled (%.1f, %.1f) cm, %.2f kn at %.4f s"),
			FrameRates[Index], Run.bPopped, Run.ApexCm, Run.Travelled.X, Run.Travelled.Y, Run.SpeedKnots, Run.EndSimSeconds);
		TestTrue(FString::Printf(TEXT("%.0f fps: the pop was taken"), FrameRates[Index]), Run.bPopped);
		TestTrue(FString::Printf(TEXT("%.0f fps: it is a real jump (%.1f m)"), FrameRates[Index], Run.ApexCm / 100.0f), Run.ApexCm > 200.0f);
		Run.Invariants.Assert(*this, FString::Printf(TEXT("%.0f fps"), FrameRates[Index]));
	}
	for (int32 A = 0; A < 3; ++A)
	{
		for (int32 B = A + 1; B < 3; ++B)
		{
			const FString Pair = FString::Printf(TEXT("%.0f against %.0f fps"), FrameRates[A], FrameRates[B]);
			const float XYDifferenceCm = FVector::Dist2D(Results[A].Travelled, Results[B].Travelled);
			TestTrue(FString::Printf(TEXT("%s: final position within 50 cm (%.2f cm)"), *Pair, XYDifferenceCm), XYDifferenceCm <= 50.0f);
			TestNearlyEqual(FString::Printf(TEXT("%s: final speed within 0.2 kn"), *Pair), Results[A].SpeedKnots, Results[B].SpeedKnots, 0.2f);
			TestNearlyEqual(FString::Printf(TEXT("%s: apex within 2%%"), *Pair), Results[A].ApexCm, Results[B].ApexCm, 0.02f * FMath::Max(Results[A].ApexCm, Results[B].ApexCm));
			TestNearlyEqual(FString::Printf(TEXT("%s: both ran to the same simulation time"), *Pair), Results[A].EndSimSeconds, Results[B].EndSimSeconds, 0.001f);
		}
	}
	return true;
}

// A one-second hitch does not throw the rig: the frame is clamped and the steps capped, so the
// simulation slows down for that frame instead of taking a giant step.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPhysicsHitchIsBounded, "KiteSurf.Physics.HitchIsBounded", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPhysicsHitchIsBounded::RunTest(const FString& Parameters)
{
	const float HitchSeconds = 1.0f;
	FScenarioRide Ride(15.0f, 12.0f);
	TestTrue(TEXT("Ride created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	Ride.Simulate(5.0f);

	// The frame is clamped to MaxFrameSeconds first: with the defaults (0.1 s, 24 steps of 1/240 s,
	// against MaxSimStepsPerFrame 32) that is the limit that binds. In float, 0.1 s holds a hair
	// under 24 steps, so the 24th waits for the next frame.
	const float FrameStepsAtClamp = Pawn->MaxFrameSeconds / Pawn->SimStepSeconds;
	float SimBefore = Pawn->GetSimTimeSeconds();
	Ride.FrameSeconds = HitchSeconds;
	Ride.Frame();
	const float HitchSimSeconds = Pawn->GetSimTimeSeconds() - SimBefore;
	UE_LOG(LogKiteSurf, Log, TEXT("HitchIsBounded: a %.1f s frame ran %d steps (%.4f s of simulation; MaxFrameSeconds %.2f holds %.3f steps, MaxSimStepsPerFrame %d)"),
		HitchSeconds, Pawn->GetLastFrameSimSteps(), HitchSimSeconds, Pawn->MaxFrameSeconds, FrameStepsAtClamp, Pawn->MaxSimStepsPerFrame);
	TestTrue(FString::Printf(TEXT("A 1 s frame runs only the steps MaxFrameSeconds holds (%d steps, %.3f fit)"), Pawn->GetLastFrameSimSteps(), FrameStepsAtClamp),
		Pawn->GetLastFrameSimSteps() <= FMath::CeilToInt(FrameStepsAtClamp) && Pawn->GetLastFrameSimSteps() >= FMath::FloorToInt(FrameStepsAtClamp) - 1);
	TestTrue(FString::Printf(TEXT("and advances the simulation by no more than MaxFrameSeconds (%.4f s)"), HitchSimSeconds),
		HitchSimSeconds <= Pawn->MaxFrameSeconds + 0.5f * Pawn->SimStepSeconds);

	// With the clamp out of the way, MaxSimStepsPerFrame is the limit, and the rest of the frame is dropped.
	Pawn->MaxFrameSeconds = HitchSeconds;
	SimBefore = Pawn->GetSimTimeSeconds();
	Ride.Frame();
	UE_LOG(LogKiteSurf, Log, TEXT("HitchIsBounded: with MaxFrameSeconds %.1f the frame ran %d steps (%.4f s of simulation)"), Pawn->MaxFrameSeconds, Pawn->GetLastFrameSimSteps(), Pawn->GetSimTimeSeconds() - SimBefore);
	TestEqual(TEXT("With the frame clamp lifted, a 1 s frame runs MaxSimStepsPerFrame steps"), Pawn->GetLastFrameSimSteps(), Pawn->MaxSimStepsPerFrame);

	// The ride carries on as if nothing happened.
	Ride.FrameSeconds = DefaultFrameSeconds;
	Ride.Simulate(1.0f);
	TestEqual(TEXT("and the next frame runs its normal steps"), Pawn->GetLastFrameSimSteps(), FMath::RoundToInt(DefaultFrameSeconds / Pawn->SimStepSeconds));
	Ride.Simulate(4.0f);
	UE_LOG(LogKiteSurf, Log, TEXT("HitchIsBounded: 5 s after the hitches: %.1f kn, %.0f N, taut %d, worst line excess %.2f cm"), Ride.SpeedKnots(), Ride.Kite->GetLineTensionN(), Ride.Kite->AreLinesTaut(), Ride.Invariants.MaxLineExcessCm);
	TestTrue(TEXT("Still riding with tight lines after the hitches"), Ride.Board->IsPlaning() && Ride.Kite->AreLinesTaut());
	Ride.Invariants.Assert(*this, TEXT("Hitch"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
