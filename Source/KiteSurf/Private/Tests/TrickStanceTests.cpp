#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "InputActionValue.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfUnits.h"
#include "RiderRig.h"
#include "WindComponent.h"
#include "Tricks/BarState.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/RiderAttitudeComponent.h"
#include "Tricks/TrickTrackerComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// Landing stances (docs/tricks/T3.md T3.5): after an unhooked backside 180 the rider lands blind (back to
// the kite, the bar behind the back), after a frontside 180 toeside (chest away from the kite, the bar in
// front, the torso twisted back towards it), and holds it for ToesideHoldSeconds or BlindHoldSeconds
// instead of sliding round at once. X on the water ends it: from blind a surface pass and the half turn on
// to heelside, from toeside the half turn back. Hooked riding slides round as it always has.

// Not brought in with a using-directive: in a unity build this file is compiled with the other test files,
// whose own using-directives would make names such as Flags ambiguous.
namespace TrickStanceTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** The flat backside spin of KiteSurf.Trick.PassOnRide (stick x towards the back roll's side, y up). */
	const FVector2D BacksideSpin(1.0f, 1.0f);
	/** A frontside spin (x towards the front roll's side), tilted flatter than a front roll. */
	const FVector2D FrontsideSpin(-0.5f, 1.0f);

	/** An unhooked rider in steady 20 kn along +X on the default 9 m, started on a beam reach as the game mode does. */
	struct FStanceRide
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		UTrickTrackerComponent* Tracker = nullptr;
		float FrameSeconds = 1.0f / 60.0f;

		explicit FStanceRide(float InFrameSeconds = 1.0f / 60.0f)
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
			Tracker = Pawn->GetTrickTracker();
			if (!Kite || !Board)
			{
				return;
			}
			if (UWindComponent* Wind = Pawn->GetWind())
			{
				Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(20.0f), 0.0f, 0.0f);
				Wind->GustStrength = 0.0f;
				Wind->DirectionDriftDeg = 0.0f;
			}
			AKiteSurfGameMode::InitializeRide(Pawn, KiteUnits::KnotsToCmS(12.0f), 1.0f);
			Kite->bParkHoldAssist = false;
			Kite->SetKiteSize(9.0f);
			// Sampled positions are the simulation's, not the drawn ones between steps.
			Pawn->bInterpolateRendering = false;
		}

		~FStanceRide()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Tracker; }
		void Frame() { Pawn->Tick(FrameSeconds); }
		bool HasReached(float SimSeconds) const { return Pawn->GetSimTimeSeconds() + 0.5f * Pawn->SimStepSeconds >= SimSeconds; }
		void SimulateUntil(float SimSeconds)
		{
			while (!HasReached(SimSeconds))
			{
				Frame();
			}
		}
		float TensionBW() const { return Kite->GetLineTensionN() / (Board->MassKg * KiteUnits::GravityMS2); }
		FVector ToKite2D() const { return (Kite->GetKiteWorldPosition() - Pawn->GetActorLocation()).GetSafeNormal2D(); }
		/** The drawn body's facing against the kite, level: +1 facing it, -1 the back square to it. */
		float BodyFacingKite() const { return FVector::DotProduct(FRotator(0.0f, Pawn->GetRiderBodyYawDeg(), 0.0f).Vector(), ToKite2D()); }
		bool IsSlidingRound() const { return !FMath::IsNearlyZero(FRotator::NormalizeAxis(Pawn->GetRiderBodyYawDeg() - Pawn->GetRiderFacingYawDeg()), 0.5f); }

		/**
		 * Rides, unhooks at 0.5 s, loads at 8 s with the weight back and Spin wound up, pops at 8.5 s with
		 * the air stick held at Spin until the body has turned StopTurnDeg (level, either way), and steps to
		 * the frame the board counts the landing. False if it did not land.
		 */
		bool PopWithSpin(const FVector2D& Spin, float StopTurnDeg)
		{
			SimulateUntil(0.5f);
			Pawn->PressHook();
			Frame();
			SimulateUntil(8.0f);
			Board->SetWeightShift(-1.0f);
			Pawn->SetLoadHeld(true);
			Pawn->SetPreWind(Spin);
			SimulateUntil(8.5f);
			const int32 JumpsBefore = Board->GetJumpCount();
			const FVector StartFacing = Pawn->GetRiderAttitude()->GetBodyQuat().GetAxisX().GetSafeNormal2D();
			Pawn->ReleaseLoadAndPop();
			Board->SetWeightShift(0.0f);
			Pawn->SetPreWind(FVector2D::ZeroVector);
			Pawn->SetAirRotationInput(Spin);
			const float GiveUp = Pawn->GetSimTimeSeconds() + 5.0f;
			while (Board->GetJumpCount() == JumpsBefore && !HasReached(GiveUp))
			{
				Frame();
				const FVector Facing = Pawn->GetRiderAttitude()->GetBodyQuat().GetAxisX().GetSafeNormal2D();
				if (FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(StartFacing, Facing), -1.0, 1.0))) > StopTurnDeg)
				{
					Pawn->SetAirRotationInput(FVector2D::ZeroVector);
				}
			}
			Pawn->SetAirRotationInput(FVector2D::ZeroVector);
			return Board->GetJumpCount() != JumpsBefore;
		}

		/** An unhooked backside 180 with no pass, landed with the lines round the back: blind. */
		bool LandBackToBlind() { return PopWithSpin(BacksideSpin, 150.0f); }

		/** An unhooked frontside 180, landed with the lines round the front: toeside. */
		bool LandToeside() { return PopWithSpin(FrontsideSpin, 120.0f); }
	};

	FString StanceName(ETrickStance Stance) { return UEnum::GetDisplayValueAsText(Stance).ToString(); }
	FString PlaceName(EBarPlace Place) { return UEnum::GetDisplayValueAsText(Place).ToString(); }

	/** Steps until the stance is Heelside, the slide round is over and the bar is in front, for at most Seconds; how long it took (s), or -1. */
	float RideBackToHeelside(FStanceRide& Ride, float Seconds)
	{
		const float Start = Ride.Pawn->GetSimTimeSeconds();
		while (!Ride.HasReached(Start + Seconds))
		{
			Ride.Frame();
			if (Ride.Pawn->GetRidingStance() == ETrickStance::Heelside && !Ride.IsSlidingRound() && Ride.Pawn->GetBarState().Place == EBarPlace::Front)
			{
				return Ride.Pawn->GetSimTimeSeconds() - Start;
			}
		}
		return -1.0f;
	}
}


// tricks.md: after a back to blind (an unhooked backside 180, no pass) the rider rides blind with the bar
// behind the back, and X is the surface pass and the half turn on to heelside.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickLandsBlindAfterBackToBlind, "KiteSurf.Trick.LandsBlindAfterBackToBlind", TrickStanceTest::Flags)

bool FKiteSurfTrickLandsBlindAfterBackToBlind::RunTest(const FString& Parameters)
{
	TrickStanceTest::FStanceRide Ride;
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	const int32 RecordsBefore = Ride.Tracker->GetJumpRecordCount();
	if (!TestTrue(TEXT("The backside 180 lands"), Ride.LandBackToBlind()))
	{
		return true;
	}
	TestNotEqual(TEXT("...not a crash"), Ride.Board->GetLastLandingVerdict().Grade, ELandingGrade::Crash);
	TestTrue(FString::Printf(TEXT("...with the lines round the back (wrap %.0f deg)"), Pawn->GetBarState().WrapDeg), Pawn->GetBarState().WrapDeg >= 90.0f && Pawn->GetBarState().bRouteBehind);

	// Blind for 1.5 s: the stance held, the bar behind the back the whole time, no slide round.
	const float LandedAt = Pawn->GetSimTimeSeconds();
	bool bBlind = true;
	bool bBehind = true;
	bool bNoSlide = true;
	float MaxTensionBW = 0.0f;
	while (!Ride.HasReached(LandedAt + 1.5f))
	{
		Ride.Frame();
		bBlind &= Pawn->GetRidingStance() == ETrickStance::Blind;
		bBehind &= Pawn->GetBarState().Place == EBarPlace::BehindBack;
		bNoSlide &= !Ride.IsSlidingRound();
		MaxTensionBW = FMath::Max(MaxTensionBW, Ride.TensionBW());
	}
	AddInfo(FString::Printf(TEXT("Riding blind 1.5 s: wrap %.0f deg, back to the kite %.2f, stance %.2f s, tension up to %.2f body weights"),
		Pawn->GetBarState().WrapDeg, Ride.BodyFacingKite(), Pawn->GetStanceSeconds(), MaxTensionBW));
	TestTrue(TEXT("Blind for 1.5 s after the landing"), bBlind);
	TestTrue(TEXT("...with the bar behind the back"), bBehind);
	TestTrue(TEXT("...and no slide round"), bNoSlide);
	TestTrue(FString::Printf(TEXT("...the back to the kite (%.2f)"), Ride.BodyFacingKite()), Ride.BodyFacingKite() < -0.3f);
	TestFalse(TEXT("...the bar kept"), Pawn->IsBarLost());

	// The jump was recorded once no surface pass could join it any more: back to blind, no pass.
	FJumpRecord Record;
	if (TestTrue(TEXT("The jump was recorded"), Ride.Tracker->GetJumpRecordCount() == RecordsBefore + 1 && Ride.Tracker->GetLastJumpRecord(Record)))
	{
		AddInfo(FString::Printf(TEXT("Recorded \"%s\", %d pass(es), the lines land %s, graded %s"), *Record.TrickName, Record.Passes.Num(), *TrickStanceTest::StanceName(Record.BarLandingStance),
			*UEnum::GetDisplayValueAsText(Record.Grade).ToString()));
		TestEqual(TEXT("...with no pass"), Record.Passes.Num(), 0);
		TestEqual(TEXT("...landing blind"), Record.BarLandingStance, ETrickStance::Blind);
	}

	// X: the surface pass (the tension is under SurfacePassMaxTensionBW), then the half turn on to heelside.
	Pawn->OnPassPressed(FInputActionValue(true));
	bool bPassed = false;
	bool bKept = true;
	const float PressedAt = Pawn->GetSimTimeSeconds();
	float HeelsideAfter = -1.0f;
	while (!Ride.HasReached(PressedAt + 1.5f))
	{
		Ride.Frame();
		bPassed |= Pawn->GetBarState().Place == EBarPlace::Passing;
		bKept &= !Pawn->IsBarLost();
		if (HeelsideAfter < 0.0f && Pawn->GetRidingStance() == ETrickStance::Heelside && !Ride.IsSlidingRound())
		{
			HeelsideAfter = Pawn->GetSimTimeSeconds() - PressedAt;
		}
	}
	AddInfo(FString::Printf(TEXT("X riding blind: heelside after %.2f s, facing the kite %.2f, wrap %.0f deg"), HeelsideAfter, Ride.BodyFacingKite(), Pawn->GetBarState().WrapDeg));
	TestTrue(TEXT("X riding blind starts a surface pass"), bPassed);
	TestTrue(TEXT("...the bar is kept"), bKept);
	TestTrue(FString::Printf(TEXT("...and the rider is heelside within 1 s (%.2f s)"), HeelsideAfter), HeelsideAfter > 0.0f && HeelsideAfter < 1.0f);
	TestEqual(TEXT("...the stance is Heelside"), *TrickStanceTest::StanceName(Pawn->GetRidingStance()), *TrickStanceTest::StanceName(ETrickStance::Heelside));
	TestEqual(TEXT("...with the bar in front"), *TrickStanceTest::PlaceName(Pawn->GetBarState().Place), *TrickStanceTest::PlaceName(EBarPlace::Front));
	TestTrue(FString::Printf(TEXT("...facing the kite (%.2f)"), Ride.BodyFacingKite()), Ride.BodyFacingKite() > 0.3f);
	// 1.5 s after the touchdown the pass is a ridden one, not part of the jump.
	if (Ride.Tracker->GetLastJumpRecord(Record))
	{
		TestEqual(TEXT("A pass long after the landing does not join the jump"), Record.Passes.Num(), 0);
	}
	TestEqual(TEXT("...nor make another record"), Ride.Tracker->GetJumpRecordCount(), RecordsBefore + 1);
	return true;
}

// The surface pass from blind made within the surface grace of the touchdown joins the jump: the record waits for it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSurfacePassFromBlindCounts, "KiteSurf.Trick.SurfacePassFromBlindCounts", TrickStanceTest::Flags)

bool FKiteSurfTrickSurfacePassFromBlindCounts::RunTest(const FString& Parameters)
{
	// The bar machine alone: riding blind on the water a pass starts under SurfacePassMaxTensionBW (not the
	// air's PassSlackTensionBW), takes SurfacePassSeconds, and joins the jump only inside the grace.
	{
		const float Dt = 1.0f / 240.0f;
		const FVector KiteDir = FVector(1.0f, 0.0f, 1.0f).GetSafeNormal();
		const FBarTunables T;
		auto Blind = [&](bool bAirborne, float TensionBW)
		{
			FBarInputs In;
			In.TensionN = TensionBW * In.BodyWeightN;
			In.LineDirWorld = KiteDir;
			In.Body = FQuat(FVector::UpVector, FMath::DegreesToRadians(180.0f));
			In.bAirborne = bAirborne;
			return In;
		};
		// Riding blind: unhooked, turned backside 180 in the air, landed, on the water for WaterSeconds.
		auto Landed = [&](float WaterSeconds, float TensionBW)
		{
			FBarState S;
			FBarInputs Hook = Blind(false, 0.2f);
			Hook.Body = FQuat::Identity;
			Hook.bHookPressed = true;
			BarStateMachine::Step(S, Hook, T, Dt);
			for (int32 K = 0; K <= 60; ++K)
			{
				FBarInputs In = Blind(true, 0.2f);
				// Backside with the nose on the body's right (FBarInputs::NoseSideSign +1) is a negative turn about Up.
				In.Body = FQuat(FVector::UpVector, FMath::DegreesToRadians(-3.0f * K));
				BarStateMachine::Step(S, In, T, Dt);
			}
			for (float Time = 0.0f; Time < WaterSeconds - 0.5f * Dt; Time += Dt)
			{
				BarStateMachine::Step(S, Blind(false, TensionBW), T, Dt);
			}
			return S;
		};
		auto PressAndRun = [&](FBarState& S, float TensionBW, float& OutDoneAfter)
		{
			FBarInputs Press = Blind(false, TensionBW);
			Press.bPassPressed = true;
			OutDoneAfter = -1.0f;
			BarStateMachine::Step(S, Press, T, Dt);
			for (int32 K = 1; K < 240 && OutDoneAfter < 0.0f; ++K)
			{
				if (BarStateMachine::Step(S, Blind(false, TensionBW), T, Dt).bPassDone)
				{
					OutDoneAfter = K * Dt;
				}
			}
		};
		FBarState Early = Landed(0.1f, 0.5f);
		TestTrue(FString::Printf(TEXT("Bar machine: riding blind, the bar is behind the back (wrap %.0f deg)"), Early.WrapDeg), Early.Place == EBarPlace::BehindBack && !Early.bHooked);
		TestTrue(TEXT("...and inside the grace a surface pass may still join the jump"), BarStateMachine::MaySurfacePassJoinJump(Early));
		float DoneAfter = 0.0f;
		PressAndRun(Early, 0.5f, DoneAfter);
		TestNearlyEqual(TEXT("Bar machine: at 0.5 body weights on the water the pass goes, in SurfacePassSeconds (s)"), DoneAfter, T.SurfacePassSeconds, 2.0f * Dt);
		TestTrue(TEXT("...and joins the jump as a surface pass"), Early.JumpPasses.Num() == 1 && Early.JumpPasses[0].Kind == ETrickPassKind::Surface
			&& Early.JumpPasses[0].Sense == ETrickSense::Backside);
		TestFalse(TEXT("...after which no more can"), BarStateMachine::MaySurfacePassJoinJump(Early));

		FBarState Late = Landed(1.0f, 0.5f);
		TestFalse(TEXT("Bar machine: 1 s after the touchdown the grace is over"), BarStateMachine::MaySurfacePassJoinJump(Late));
		PressAndRun(Late, 0.5f, DoneAfter);
		TestTrue(TEXT("...a pass still goes"), DoneAfter > 0.0f && Late.Place == EBarPlace::BehindBack);
		TestEqual(TEXT("...but does not join the jump"), Late.JumpPasses.Num(), 0);

		FBarState Loaded = Landed(0.1f, 0.7f);
		PressAndRun(Loaded, 0.7f, DoneAfter);
		TestTrue(TEXT("Bar machine: over SurfacePassMaxTensionBW the press does not start a pass"), DoneAfter < 0.0f && Loaded.Place == EBarPlace::BehindBack);
	}

	// On the ride: X pressed as the board lands from a back to blind.
	TrickStanceTest::FStanceRide Ride;
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	const int32 RecordsBefore = Ride.Tracker->GetJumpRecordCount();
	if (!TestTrue(TEXT("The backside 180 lands"), Ride.LandBackToBlind()))
	{
		return true;
	}
	TestNotEqual(TEXT("...not a crash"), Ride.Board->GetLastLandingVerdict().Grade, ELandingGrade::Crash);
	TestEqual(TEXT("Landing back to blind, the record waits for a surface pass"), Ride.Tracker->GetJumpRecordCount(), RecordsBefore);
	Pawn->OnPassPressed(FInputActionValue(true));
	const float PressedAt = Pawn->GetSimTimeSeconds();
	float PassDoneAfter = -1.0f;
	float RecordedAfter = -1.0f;
	bool bKept = true;
	while (!Ride.HasReached(PressedAt + 1.5f))
	{
		const int32 PassesBefore = Pawn->GetBarState().JumpPasses.Num();
		Ride.Frame();
		bKept &= !Pawn->IsBarLost();
		if (PassDoneAfter < 0.0f && Pawn->GetBarState().JumpPasses.Num() > PassesBefore)
		{
			PassDoneAfter = Pawn->GetSimTimeSeconds() - PressedAt;
		}
		if (RecordedAfter < 0.0f && Ride.Tracker->GetJumpRecordCount() > RecordsBefore)
		{
			RecordedAfter = Pawn->GetSimTimeSeconds() - PressedAt;
		}
	}
	TestTrue(TEXT("The bar is kept"), bKept);
	TestTrue(FString::Printf(TEXT("The surface pass is done (%.2f s after the press)"), PassDoneAfter), PassDoneAfter > 0.0f);
	TestTrue(FString::Printf(TEXT("...and the jump is recorded on that frame (%.2f s)"), RecordedAfter), RecordedAfter > 0.0f && FMath::IsNearlyEqual(RecordedAfter, PassDoneAfter, 0.001f));
	FJumpRecord Record;
	if (TestTrue(TEXT("One record"), Ride.Tracker->GetJumpRecordCount() == RecordsBefore + 1 && Ride.Tracker->GetLastJumpRecord(Record)))
	{
		AddInfo(FString::Printf(TEXT("Recorded \"%s\", %d pass(es), the lines land %s, graded %s"), *Record.TrickName, Record.Passes.Num(), *TrickStanceTest::StanceName(Record.BarLandingStance),
			*UEnum::GetDisplayValueAsText(Record.Grade).ToString()));
		if (TestEqual(TEXT("The surface pass is counted in the record"), Record.Passes.Num(), 1))
		{
			TestEqual(TEXT("...a backside pass"), Record.Passes[0].Sense, ETrickSense::Backside);
			TestEqual(TEXT("...on the surface"), Record.Passes[0].Kind, ETrickPassKind::Surface);
			TestEqual(TEXT("...completing the backside 180"), Record.Passes[0].Degrees, 180);
		}
		TestEqual(TEXT("Named from the bar"), Record.TrickName, FString(TEXT("Backside 1 to blind")));
		TestNotEqual(TEXT("Not a crash"), Record.Grade, ELandingGrade::Crash);
	}
	TestEqual(TEXT("After the pass the rider has turned on to heelside"), *TrickStanceTest::StanceName(Pawn->GetRidingStance()), *TrickStanceTest::StanceName(ETrickStance::Heelside));
	TestEqual(TEXT("...the bar in front"), *TrickStanceTest::PlaceName(Pawn->GetBarState().Place), *TrickStanceTest::PlaceName(EBarPlace::Front));
	TestTrue(FString::Printf(TEXT("...facing the kite (%.2f)"), Ride.BodyFacingKite()), Ride.BodyFacingKite() > 0.3f);
	return true;
}

// After a frontside 180 the rider rides toeside: held with no slide round, then X turns them back.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickToesideLandingHeld, "KiteSurf.Trick.ToesideLandingHeld", TrickStanceTest::Flags)

bool FKiteSurfTrickToesideLandingHeld::RunTest(const FString& Parameters)
{
	TrickStanceTest::FStanceRide Ride;
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	if (!TestTrue(TEXT("The frontside 180 lands"), Ride.LandToeside()))
	{
		return true;
	}
	TestNotEqual(TEXT("...not a crash"), Ride.Board->GetLastLandingVerdict().Grade, ELandingGrade::Crash);
	const float LandedAt = Pawn->GetSimTimeSeconds();
	Ride.SimulateUntil(LandedAt + 0.25f);
	TestTrue(FString::Printf(TEXT("...with the lines round the front (wrap %.0f deg)"), Pawn->GetBarState().WrapDeg), Pawn->GetBarState().WrapDeg <= -90.0f && !Pawn->GetBarState().bRouteBehind);
	bool bToeside = true;
	bool bFront = true;
	bool bNoSlide = true;
	bool bBackToKite = true;
	const float HoldFor = Pawn->ToesideHoldSeconds - 0.5f;
	while (!Ride.HasReached(LandedAt + HoldFor))
	{
		Ride.Frame();
		bToeside &= Pawn->GetRidingStance() == ETrickStance::Toeside;
		bFront &= Pawn->GetBarState().Place == EBarPlace::Front;
		bNoSlide &= !Ride.IsSlidingRound();
		bBackToKite &= Ride.BodyFacingKite() < 0.0f;
	}
	AddInfo(FString::Printf(TEXT("Riding toeside %.1f s: wrap %.0f deg, facing the kite %.2f, torso twist %.0f deg, %.1f kn"), HoldFor, Pawn->GetBarState().WrapDeg, Ride.BodyFacingKite(),
		Pawn->GetDrawnTorsoTwistDeg(), KiteUnits::CmSToKnots(static_cast<float>(Ride.Board->Velocity.Size2D()))));
	TestTrue(FString::Printf(TEXT("Toeside held for %.1f s"), HoldFor), bToeside);
	TestTrue(TEXT("...the bar in front"), bFront);
	TestTrue(TEXT("...no slide round"), bNoSlide);
	TestTrue(TEXT("...the chest away from the kite"), bBackToKite);
	TestFalse(TEXT("...the bar kept"), Pawn->IsBarLost());

	// X: the half turn back to heelside.
	Pawn->OnPassPressed(FInputActionValue(true));
	Ride.Frame();
	TestEqual(TEXT("X riding toeside: heelside at once"), *TrickStanceTest::StanceName(Pawn->GetRidingStance()), *TrickStanceTest::StanceName(ETrickStance::Heelside));
	TestTrue(TEXT("...turning round"), Ride.IsSlidingRound());
	const float Back = TrickStanceTest::RideBackToHeelside(Ride, 1.0f);
	TestTrue(FString::Printf(TEXT("...and round within 1 s (%.2f s)"), Back), Back >= 0.0f);
	TestTrue(FString::Printf(TEXT("...facing the kite (%.2f)"), Ride.BodyFacingKite()), Ride.BodyFacingKite() > 0.3f);
	TestTrue(FString::Printf(TEXT("...the lines unwound (wrap %.0f deg)"), Pawn->GetBarState().WrapDeg), FMath::Abs(Pawn->GetBarState().WrapDeg) < 90.0f);
	TestFalse(TEXT("...the bar kept"), Pawn->IsBarLost());
	return true;
}

// Left alone, blind and toeside last their hold and then the slide round brings the rider back to heelside.
// At 60 and 30 frames a second: the hold runs on the fixed step.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickStanceTimesOutToHeelside, "KiteSurf.Trick.StanceTimesOutToHeelside", TrickStanceTest::Flags)

bool FKiteSurfTrickStanceTimesOutToHeelside::RunTest(const FString& Parameters)
{
	for (const bool bBlind : { true, false })
	{
		for (const float Fps : { 60.0f, 30.0f })
		{
			const FString Label = FString::Printf(TEXT("%s, %.0f fps:"), bBlind ? TEXT("Blind") : TEXT("Toeside"), Fps);
			TrickStanceTest::FStanceRide Ride(1.0f / Fps);
			if (!TestTrue(Label + TEXT(" ride set up"), Ride.IsValid()))
			{
				continue;
			}
			AKiteRiderPawn* Pawn = Ride.Pawn;
			if (!TestTrue(Label + TEXT(" landed"), bBlind ? Ride.LandBackToBlind() : Ride.LandToeside()))
			{
				continue;
			}
			const ETrickStance Expected = bBlind ? ETrickStance::Blind : ETrickStance::Toeside;
			const float Hold = bBlind ? Pawn->BlindHoldSeconds : Pawn->ToesideHoldSeconds;
			const float LandedAt = Pawn->GetSimTimeSeconds();
			Ride.Frame();
			TestEqual(Label + TEXT(" the landing's stance"), *TrickStanceTest::StanceName(Pawn->GetRidingStance()), *TrickStanceTest::StanceName(Expected));
			float HeldFor = -1.0f;
			while (!Ride.HasReached(LandedAt + Hold + 1.0f))
			{
				const float StanceSeconds = Pawn->GetStanceSeconds();
				Ride.Frame();
				if (Pawn->GetRidingStance() != Expected)
				{
					HeldFor = StanceSeconds;
					break;
				}
			}
			AddInfo(FString::Printf(TEXT("%s held %.3f s (hold %.1f s), then %s"), *Label, HeldFor, Hold, *TrickStanceTest::StanceName(Pawn->GetRidingStance())));
			TestTrue(FString::Printf(TEXT("%s the stance is held for its hold time (%.3f s)"), *Label, HeldFor), FMath::IsNearlyEqual(HeldFor, Hold, 1.0f / Fps + Pawn->SimStepSeconds));
			TestEqual(Label + TEXT(" then heelside"), *TrickStanceTest::StanceName(Pawn->GetRidingStance()), *TrickStanceTest::StanceName(ETrickStance::Heelside));
			TestTrue(Label + TEXT(" by the slide round"), Ride.IsSlidingRound());
			const float Back = TrickStanceTest::RideBackToHeelside(Ride, 1.0f);
			TestTrue(FString::Printf(TEXT("%s round within 1 s (%.2f s)"), *Label, Back), Back >= 0.0f);
			TestTrue(FString::Printf(TEXT("%s facing the kite (%.2f)"), *Label, Ride.BodyFacingKite()), Ride.BodyFacingKite() > 0.3f);
			TestFalse(Label + TEXT(" the bar kept"), Pawn->IsBarLost());
		}
	}
	return true;
}

// Riding blind the drawn bar is behind the back, both hands on it, and the body faces away from the kite.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRiderBlindPoseBarBehindBack, "KiteSurf.Rider.BlindPoseBarBehindBack", TrickStanceTest::Flags)

bool FKiteSurfRiderBlindPoseBarBehindBack::RunTest(const FString& Parameters)
{
	TrickStanceTest::FStanceRide Ride;
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	if (!TestTrue(TEXT("The backside 180 lands"), Ride.LandBackToBlind()))
	{
		return true;
	}
	Ride.SimulateUntil(Pawn->GetSimTimeSeconds() + 0.6f);
	if (!TestEqual(TEXT("Riding blind"), *TrickStanceTest::StanceName(Pawn->GetRidingStance()), *TrickStanceTest::StanceName(ETrickStance::Blind)))
	{
		return true;
	}
	const FRiderRigPose& Pose = Pawn->GetRiderRigPose();
	const FVector Bar = Pawn->GetDrawnBarCentre();
	const FVector Front = Pose.Torso.GetAxisX();
	const float BarAheadCm = static_cast<float>(FVector::DotProduct(Bar - Pose.Pelvis, Front));
	const FVector Hands = (Pose.Arms[0].End + Pose.Arms[1].End) * 0.5f;
	const float HandsAheadCm = static_cast<float>(FVector::DotProduct(Hands - Pose.Pelvis, Front));
	const float HandsToBarCm = static_cast<float>(FVector::Dist(Hands, Bar));
	const FVector ToKite = Ride.ToKite2D();
	const float HipsDotKite = static_cast<float>(FVector::DotProduct(Pose.Hips.GetAxisX().GetSafeNormal2D(), ToKite));
	AddInfo(FString::Printf(TEXT("Blind: bar %.0f cm ahead of the pelvis, hands %.0f cm ahead and %.0f cm from the bar; hips . kite %.2f, torso up . kite %.2f; line attach (%.0f, %.0f, %.0f) cm"),
		BarAheadCm, HandsAheadCm, HandsToBarCm, HipsDotKite, FVector::DotProduct(Pose.Torso.GetAxisZ(), ToKite),
		Pawn->GetLineAttachBodyCm().X, Pawn->GetLineAttachBodyCm().Y, Pawn->GetLineAttachBodyCm().Z));
	TestEqual(TEXT("The bar is behind the back"), *TrickStanceTest::PlaceName(Pawn->GetBarState().Place), *TrickStanceTest::PlaceName(EBarPlace::BehindBack));
	TestTrue(FString::Printf(TEXT("...drawn behind the body (%.0f cm)"), BarAheadCm), BarAheadCm < -10.0f);
	TestTrue(FString::Printf(TEXT("...held there with both hands (%.0f cm behind the pelvis, %.0f cm from the bar)"), -HandsAheadCm, HandsToBarCm), HandsAheadCm < 0.0f && HandsToBarCm < 8.0f);
	TestTrue(TEXT("...the lines pull at the lower back"), Pawn->GetLineAttachBodyCm().X < 0.0f);
	TestTrue(FString::Printf(TEXT("The hips face away from the kite (%.2f)"), HipsDotKite), HipsDotKite < -0.3f);
	TestTrue(TEXT("...with no twist: the torso faces the same way"), Pose.Torso.GetAxisX().Equals(Pose.Hips.GetAxisX(), 1e-3));
	TestTrue(TEXT("The body leans away from the kite"), FVector::DotProduct(Pose.Torso.GetAxisZ(), ToKite) < 0.0f);
	return true;
}

// Riding toeside the hips face away from the kite and the torso is twisted back towards it over them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRiderToesideTorsoTwisted, "KiteSurf.Rider.ToesideTorsoTwisted", TrickStanceTest::Flags)

bool FKiteSurfRiderToesideTorsoTwisted::RunTest(const FString& Parameters)
{
	// The rig alone: the twist turns the torso and the arms about the body's Up, and leaves the hips and legs.
	{
		FRiderRigInput In;
		In.Board = FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, 10.0f));
		In.Facing = FVector::RightVector;
		In.BodyUp = FVector(0.0f, -0.3f, 1.0f).GetSafeNormal();
		In.Crouch = 0.3f;
		const FRiderRigPose Straight = RiderRig::SolveBody(In);
		In.TorsoTwistDeg = 70.0f;
		const FRiderRigPose Twisted = RiderRig::SolveBody(In);
		bool bLegsKept = true;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			bLegsKept &= Straight.Legs[Side].Root.Equals(Twisted.Legs[Side].Root, 0.01) && Straight.Legs[Side].Joint.Equals(Twisted.Legs[Side].Joint, 0.01)
				&& Straight.Legs[Side].End.Equals(Twisted.Legs[Side].End, 0.01);
		}
		TestTrue(TEXT("Rig: the twist leaves the pelvis and the legs where they were"), bLegsKept && Straight.Pelvis.Equals(Twisted.Pelvis, 0.01) && Straight.Hips.Equals(Twisted.Hips, 1e-4));
		TestTrue(TEXT("...the hips' frame is the untwisted torso"), Straight.Hips.Equals(Straight.Torso, 1e-4));
		const float TwistedByDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Twisted.Torso.GetAxisX(), Twisted.Hips.GetAxisX()), -1.0, 1.0)));
		TestNearlyEqual(TEXT("...and turns the torso by it (deg)"), TwistedByDeg, 70.0f, 0.1f);
		TestTrue(TEXT("...about the body's Up"), Twisted.Torso.GetAxisZ().Equals(Twisted.Hips.GetAxisZ(), 1e-3));
		TestTrue(TEXT("...towards Right for a positive twist"), FVector::DotProduct(Twisted.Torso.GetAxisX(), Twisted.Hips.GetAxisY()) > 0.9f);
		TestFalse(TEXT("...the shoulders go with it"), Twisted.Arms[0].Root.Equals(Straight.Arms[0].Root, 1.0));
	}

	TrickStanceTest::FStanceRide Ride;
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	if (!TestTrue(TEXT("The frontside 180 lands"), Ride.LandToeside()))
	{
		return true;
	}
	Ride.SimulateUntil(Pawn->GetSimTimeSeconds() + 0.8f);
	if (!TestEqual(TEXT("Riding toeside"), *TrickStanceTest::StanceName(Pawn->GetRidingStance()), *TrickStanceTest::StanceName(ETrickStance::Toeside)))
	{
		return true;
	}
	const FRiderRigPose& Pose = Pawn->GetRiderRigPose();
	const FVector ToKite = Ride.ToKite2D();
	const FVector HipsFront = Pose.Hips.GetAxisX().GetSafeNormal2D();
	const FVector TorsoFront = Pose.Torso.GetAxisX().GetSafeNormal2D();
	const float HipsOffKiteDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(HipsFront, ToKite), -1.0, 1.0)));
	const float TorsoOffKiteDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(TorsoFront, ToKite), -1.0, 1.0)));
	const float BarAheadCm = static_cast<float>(FVector::DotProduct(Pawn->GetDrawnBarCentre() - Pose.Pelvis, Pose.Torso.GetAxisX()));
	AddInfo(FString::Printf(TEXT("Toeside: the hips face %.0f deg from the kite, the torso %.0f deg (twist %.0f deg); bar %.0f cm ahead of the pelvis"), HipsOffKiteDeg, TorsoOffKiteDeg,
		Pawn->GetDrawnTorsoTwistDeg(), BarAheadCm));
	TestTrue(FString::Printf(TEXT("The pelvis faces away from the kite (%.0f deg off it)"), HipsOffKiteDeg), HipsOffKiteDeg > 90.0f);
	TestTrue(FString::Printf(TEXT("...and the torso's X is within 90 deg of it (%.0f deg)"), TorsoOffKiteDeg), TorsoOffKiteDeg < 90.0f);
	TestNearlyEqual(TEXT("...twisted by ToesideTorsoTwistDeg (deg)"), FMath::Abs(Pawn->GetDrawnTorsoTwistDeg()), Pawn->ToesideTorsoTwistDeg, 0.5f);
	TestTrue(TEXT("The knees still point along the hips"), FVector::DotProduct(Pose.Legs[0].Pole, Pose.Hips.GetAxisX()) > 0.5f && FVector::DotProduct(Pose.Legs[1].Pole, Pose.Hips.GetAxisX()) > 0.5f);
	TestEqual(TEXT("The bar is in front"), *TrickStanceTest::PlaceName(Pawn->GetBarState().Place), *TrickStanceTest::PlaceName(EBarPlace::Front));
	TestTrue(FString::Printf(TEXT("...drawn in front of the twisted torso (%.0f cm)"), BarAheadCm), BarAheadCm > 10.0f);

	// Back to heelside, the twist goes.
	Pawn->OnPassPressed(FInputActionValue(true));
	TrickStanceTest::RideBackToHeelside(Ride, 1.0f);
	Ride.SimulateUntil(Pawn->GetSimTimeSeconds() + 0.5f);
	TestNearlyEqual(TEXT("Heelside again, the torso untwisted (deg)"), Pawn->GetDrawnTorsoTwistDeg(), 0.0f, 0.01f);
	return true;
}

// Hooked in nothing changes: a rider left with their back to the kite slides the board round after
// RiderSwitchDelaySeconds and faces it again (the check KiteSurf.Rider.SpinsWithBoard makes), and the
// stance stays heelside.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRiderHookedBackToKiteStillSlidesRound, "KiteSurf.Rider.HookedBackToKiteStillSlidesRound", TrickStanceTest::Flags)

bool FKiteSurfRiderHookedBackToKiteStillSlidesRound::RunTest(const FString& Parameters)
{
	TrickStanceTest::FStanceRide Ride;
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	Ride.SimulateUntil(2.0f);
	TestTrue(TEXT("Riding hooked in"), Pawn->IsHooked());
	TestTrue(FString::Printf(TEXT("...facing the kite (%.2f)"), Ride.BodyFacingKite()), Ride.BodyFacingKite() > 0.3f);

	// Carve half a turn on the water, by hand (as KiteSurf.Rider.SpinsWithBoard does): the rider turns
	// with the board and is left with their back to the kite.
	Pawn->bStepSimulation = false;
	for (int32 Step = 0; Step < 12; ++Step)
	{
		Pawn->SetActorRotation(FRotator(0.0f, Pawn->GetActorRotation().Yaw + 15.0f, 0.0f));
		Ride.Frame();
	}
	TestTrue(FString::Printf(TEXT("After half a turn the back is to the kite (%.2f)"), Ride.BodyFacingKite()), Ride.BodyFacingKite() < -0.3f);
	float SlidAfter = -1.0f;
	bool bHeelside = true;
	for (int32 Step = 1; Step <= 90; ++Step)
	{
		Ride.Frame();
		bHeelside &= Pawn->GetRidingStance() == ETrickStance::Heelside;
		if (SlidAfter < 0.0f && Ride.IsSlidingRound())
		{
			SlidAfter = Step * Ride.FrameSeconds;
		}
	}
	// The back was already to the kite for the last frames of the carve, so the delay started then.
	AddInfo(FString::Printf(TEXT("Hooked, back to the kite: the slide round started %.2f s after the carve"), SlidAfter));
	TestTrue(FString::Printf(TEXT("The slide round starts within RiderSwitchDelaySeconds (%.2f s after the carve)"), SlidAfter),
		SlidAfter > 0.0f && SlidAfter <= Pawn->RiderSwitchDelaySeconds + 2.0f * Ride.FrameSeconds);
	TestTrue(FString::Printf(TEXT("A moment later they face the kite (%.2f)"), Ride.BodyFacingKite()), Ride.BodyFacingKite() > 0.3f);
	TestFalse(TEXT("...and the slide round has finished"), Ride.IsSlidingRound());
	TestTrue(TEXT("The stance stayed heelside"), bHeelside);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
