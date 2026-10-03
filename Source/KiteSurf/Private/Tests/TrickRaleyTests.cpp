#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfUnits.h"
#include "RiderRig.h"
#include "WindComponent.h"
#include "Tricks/BarState.h"
#include "Tricks/BoardGrabPoints.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/JumpRecorder.h"
#include "Tricks/RaleyRecognizer.h"
#include "Tricks/RiderAttitudeComponent.h"
#include "Tricks/RiderAxes.h"
#include "Tricks/TrickNaming.h"
#include "Tricks/TrickTrackerComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// Unhooked freestyle from the line torque (docs/tricks/T3.md T3.2 and T3.3): the raley the line pull at
// the hands makes, the S-bend and hinterberger about the lines, the tantrum and the front flip, and one
// hand off the bar (the tantrum's back hand, a one-hand grab).

namespace TrickRaleyTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** A rider on the water in steady wind along +X, started on a beam reach as the game mode does (TrickUnhookTests' ride). */
	struct FRaleyRide
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		UTrickTrackerComponent* Tracker = nullptr;
		URiderAttitudeComponent* Attitude = nullptr;
		float FrameSeconds = 1.0f / 60.0f;

		explicit FRaleyRide(float WindKnots = 20.0f, float KiteM2 = 9.0f, float InFrameSeconds = 1.0f / 60.0f)
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
			Attitude = Pawn->GetRiderAttitude();
			if (!Kite || !Board)
			{
				return;
			}
			if (UWindComponent* Wind = Pawn->GetWind())
			{
				Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(WindKnots), 0.0f, 0.0f);
				Wind->GustStrength = 0.0f;
				Wind->DirectionDriftDeg = 0.0f;
			}
			AKiteSurfGameMode::InitializeRide(Pawn, KiteUnits::KnotsToCmS(12.0f), 1.0f);
			Kite->bParkHoldAssist = false;
			Kite->SetKiteSize(KiteM2);
			// Sampled positions are the simulation's, not the drawn ones between steps.
			Pawn->bInterpolateRendering = false;
		}

		~FRaleyRide()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Tracker && Attitude; }
		void Frame() { Pawn->Tick(FrameSeconds); }
		bool HasReached(float SimSeconds) const { return Pawn->GetSimTimeSeconds() + 0.5f * Pawn->SimStepSeconds >= SimSeconds; }
		void SimulateUntil(float SimSeconds) { while (!HasReached(SimSeconds)) { Frame(); } }
	};

	/** How a pop is flown: hooked or not, where the bar is, the pre-wind, and the air stick and tuck held for a while after the pop. */
	struct FPopPlan
	{
		bool bUnhook = true;
		/** The bar's position (SheetKite) from 8 s: 0 is the arms straight out unhooked, 1 the bar at the hips. */
		float Bar = 0.5f;
		FVector2D PreWind = FVector2D::ZeroVector;
		FVector2D AirStick = FVector2D::ZeroVector;
		float Tuck = 0.0f;
		/** How long after the pop the air stick and the tuck are held (s). */
		float HoldSeconds = 0.0f;
		/** A grab button held from this long after the pop for GrabSeconds (negative: none). */
		float GrabAfterSeconds = -1.0f;
		float GrabSeconds = 0.0f;
		bool bGrabBack = true;
	};

	/** What a pop did, sampled frame by frame in the air. */
	struct FPopOutcome
	{
		bool bTookOff = false;
		bool bLanded = false;
		bool bRecorded = false;
		FJumpRecord Record;
		/** Largest tilt of the body's Up from world up (deg), the test's own measure. */
		float MaxTiltDeg = 0.0f;
		/** The rotation about the body's Right axis, the flip axis, summed (deg). */
		float PitchDeg = 0.0f;
		/** The test's own inversion count (docs/tricks.md 6.6: Up below -0.3 after above +0.3). */
		int32 Inversions = 0;
		int32 AirFrames = 0;
		/** Air frames with the front hand only on the bar, and the hands on the first and last air frames. */
		int32 FrontOnlyFrames = 0;
		EBarHands FirstAirHands = EBarHands::None;
		EBarHands LastAirHands = EBarHands::None;
		bool bFlipArmsInAir = false;
		/** Frames in the air with the grab hand on its socket, and the worst distance of the drawn hand from it (cm). */
		int32 GrabHeldFrames = 0;
		float WorstGrabHandCm = 0.0f;
		/** While the grab was held: the other hand alone on the bar every frame, and the line attach on that hand's side. */
		bool bGrabHandOffBar = true;
		bool bGrabAttachOtherSide = true;
	};

	/** Unhooks (or not) at 0.5 s, rides to 8 s, sets the bar, loads for 0.5 s with the weight back and the pre-wind, pops, and flies to the touchdown. */
	FPopOutcome FlyPop(FRaleyRide& Ride, const FPopPlan& Plan)
	{
		FPopOutcome Out;
		AKiteRiderPawn* Pawn = Ride.Pawn;
		Ride.SimulateUntil(0.5f);
		if (Plan.bUnhook)
		{
			Pawn->PressHook();
			Ride.Frame();
		}
		Ride.SimulateUntil(8.0f);
		Pawn->SheetKite(Plan.Bar);
		Ride.Board->SetWeightShift(-1.0f);
		Pawn->SetLoadHeld(true);
		Pawn->SetPreWind(Plan.PreWind);
		Ride.SimulateUntil(8.5f);
		const int32 JumpsBefore = Ride.Board->GetJumpCount();
		const int32 RecordsBefore = Ride.Tracker->GetJumpRecordCount();
		Pawn->ReleaseLoadAndPop();
		Ride.Board->SetWeightShift(0.0f);
		Pawn->SetPreWind(FVector2D::ZeroVector);
		const float PopAt = Pawn->GetSimTimeSeconds();
		Pawn->SetAirRotationInput(Plan.AirStick);
		Pawn->SetTuck(Plan.Tuck);
		bool bArmed = true;
		bool bGrabbing = false;
		const float GiveUp = PopAt + 5.0f;
		while (Ride.Board->GetJumpCount() == JumpsBefore && !Ride.HasReached(GiveUp))
		{
			if (Ride.HasReached(PopAt + Plan.HoldSeconds))
			{
				Pawn->SetAirRotationInput(FVector2D::ZeroVector);
				Pawn->SetTuck(0.0f);
			}
			if (Plan.GrabAfterSeconds >= 0.0f)
			{
				const bool bWant = Ride.HasReached(PopAt + Plan.GrabAfterSeconds) && !Ride.HasReached(PopAt + Plan.GrabAfterSeconds + Plan.GrabSeconds);
				if (bWant != bGrabbing)
				{
					bGrabbing = bWant;
					// The zone stick towards the chest: the toe edge (the back hand's Indy, the front hand's mute).
					Pawn->SetTrickInput(!Plan.bGrabBack && bWant, Plan.bGrabBack && bWant, false, FVector2D(-1.0f, 0.0f));
				}
			}
			Ride.Frame();
			if (!Ride.Attitude->IsSimulating())
			{
				continue;
			}
			Out.bTookOff = true;
			const FQuat Body = Ride.Attitude->GetBodyQuat();
			const FVector Up = Body.GetAxisZ();
			Out.MaxTiltDeg = FMath::Max(Out.MaxTiltDeg, FRaleyRecognizer::TiltDeg(Body));
			Out.PitchDeg += FMath::RadiansToDegrees(FMath::Abs(static_cast<float>(FVector::DotProduct(Ride.Attitude->GetAngularVelocity(), Body.GetAxisY())))) * Ride.FrameSeconds;
			if (bArmed && Up.Z < -0.3)
			{
				++Out.Inversions;
				bArmed = false;
			}
			else if (!bArmed && Up.Z > 0.3)
			{
				bArmed = true;
			}
			const EBarHands Hands = Pawn->GetBarState().Hands;
			if (Out.AirFrames == 0)
			{
				Out.FirstAirHands = Hands;
			}
			++Out.AirFrames;
			Out.LastAirHands = Hands;
			Out.FrontOnlyFrames += Hands == EBarHands::FrontOnly ? 1 : 0;
			Out.bFlipArmsInAir |= Pawn->AreFlipArmsIn();
			const FGrabState& Grabs = Pawn->GetGrabState();
			if (bGrabbing && Grabs.IsHolding())
			{
				// The hand still on the bar: the front one for a back hand grab. Its shoulder is on the nose's
				// side for the front hand (LineAttach), the other side for the back hand.
				Out.bGrabHandOffBar &= Hands == (Plan.bGrabBack ? EBarHands::FrontOnly : EBarHands::BackOnly);
				// The nose's side as the bar has it in the air (AKiteRiderPawn::GetBarNoseSideSign): the attitude's strap offset.
				const float BarNose = Ride.Attitude->GetStrapOffset().GetAxisX().Y >= 0.0 ? 1.0f : -1.0f;
				Out.bGrabAttachOtherSide &= Pawn->GetLineAttachBodyCm().Y * BarNose * (Plan.bGrabBack ? 1.0f : -1.0f) > 0.0f;
				UStaticMeshComponent* BoardVisual = Pawn->GetBoardVisual();
				if (Grabs.GetPrevReachWeight() >= 1.0f && BoardVisual)
				{
					// As UpdateRiderPose: the nose is on the side of the body the drawn board's +X points to.
					const float Nose = FVector::DotProduct(BoardVisual->GetComponentTransform().GetUnitAxis(EAxis::X), Pawn->GetRiderRigPose().Torso.GetAxisY()) >= 0.0f ? 1.0f : -1.0f;
					const ETrickHand Hand = Plan.bGrabBack ? ETrickHand::Back : ETrickHand::Front;
					const int32 FrontRigSide = Nose > 0.0f ? 1 : 0;
					const int32 Side = Hand == ETrickHand::Front ? FrontRigSide : 1 - FrontRigSide;
					const FVector Socket = BoardGrabPoints::SocketWorld(BoardVisual->GetComponentTransform(), Grabs.GetZone(), Hand, Nose);
					Out.WorstGrabHandCm = FMath::Max(Out.WorstGrabHandCm, static_cast<float>(FVector::Dist(Pawn->GetRiderRigPose().Arms[Side].End, Socket)));
					++Out.GrabHeldFrames;
				}
			}
		}
		Pawn->SetAirRotationInput(FVector2D::ZeroVector);
		Pawn->SetTuck(0.0f);
		Pawn->SetTrickInput(false, false, false);
		Out.bLanded = Ride.Board->GetJumpCount() != JumpsBefore;
		Out.bRecorded = Ride.Tracker->GetJumpRecordCount() > RecordsBefore && Ride.Tracker->GetLastJumpRecord(Out.Record);
		return Out;
	}

	FString Describe(const FPopOutcome& O)
	{
		const FJumpRecord& R = O.Record;
		return FString::Printf(TEXT("'%s' (%s, cause %s): move %s, tilt %.1f deg (record %.1f), %.0f deg about the lines, pitch %.0f deg, %d inversion(s) (record %d), spin %d %s, lands %s; %d air frames, %d front hand only"),
			*R.TrickName, *UEnum::GetDisplayValueAsText(R.Grade).ToString(), *UEnum::GetDisplayValueAsText(R.LandingCause).ToString(),
			*UEnum::GetDisplayValueAsText(R.TakeoffMove).ToString(), O.MaxTiltDeg, R.MaxTiltDeg, R.LineSpinDeg, O.PitchDeg, O.Inversions, R.Inversions.Num(),
			R.SpinHalfTurns, *UEnum::GetDisplayValueAsText(R.SpinSense).ToString(), *UEnum::GetDisplayValueAsText(R.LandingStance).ToString(), O.AirFrames, O.FrontOnlyFrames);
	}

	/** The raley: unhooked, the arms straight out, a loaded pop with no rotation input. */
	FPopPlan RaleyPlan()
	{
		FPopPlan Plan;
		Plan.bUnhook = true;
		Plan.Bar = 0.0f;
		return Plan;
	}

	/**
	 * A flip: stick Y pre-wind (-1 pulled, the backflip), then the stick and the tuck held for a moment
	 * after the pop, as a rider pulls through it (the T1 back roll on a ride takes its air stick and tuck
	 * the same way).
	 */
	FPopPlan FlipPlan(bool bUnhook, float StickY, float HoldSeconds)
	{
		FPopPlan Plan;
		Plan.bUnhook = bUnhook;
		Plan.Bar = 0.5f;
		Plan.PreWind = FVector2D(0.0f, StickY);
		Plan.AirStick = FVector2D(0.0f, StickY);
		Plan.Tuck = 1.0f;
		Plan.HoldSeconds = HoldSeconds;
		return Plan;
	}

	/** Steps a synthetic rotation about a fixed world axis through the raley recogniser. */
	FRaleyResult RunSynthetic(const FVector& AxisWorld, float RateDegS, float Seconds, const FVector& LineDir, bool bArms, bool bUnhooked, int32 Inversions, int32 Passes,
		float Sigma = 1.0f)
	{
		FRaleyRecognizer Recognizer;
		Recognizer.Begin(Sigma);
		const float Dt = 1.0f / 240.0f;
		const FVector Axis = AxisWorld.GetSafeNormal();
		const FVector Omega = Axis * FMath::DegreesToRadians(RateDegS);
		FQuat Body = FQuat::Identity;
		const int32 Steps = FMath::RoundToInt(Seconds / Dt);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Body = (FQuat(Axis, FMath::DegreesToRadians(RateDegS) * Dt) * Body).GetNormalized();
			Recognizer.Step(Body, Omega, LineDir, bArms, Dt);
		}
		return Recognizer.Get(bUnhooked, Inversions, Passes);
	}
}

using namespace TrickRaleyTest;

// tricks.md 6.4 and T3.2: 20 kn on the 9 m2 kite, unhooked with the low park, the arms straight out and a
// loaded pop with no rotation input. The line pull at the hands swings the body out past 60 deg from
// upright towards the kite, with no inversion: the tracker sets bRaley and the jump is named "Raley".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRaleyFromUnhookedPop, "KiteSurf.Trick.RaleyFromUnhookedPop", Flags)

bool FKiteSurfTrickRaleyFromUnhookedPop::RunTest(const FString& Parameters)
{
	FRaleyRide Ride;
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	const FPopOutcome O = FlyPop(Ride, RaleyPlan());
	AddInfo(Describe(O));
	if (!TestTrue(TEXT("Popped, landed and recorded"), O.bTookOff && O.bLanded && O.bRecorded))
	{
		return true;
	}
	TestFalse(TEXT("Unhooked for the jump"), O.Record.bHooked);
	TestTrue(FString::Printf(TEXT("The body swung out past 60 deg from upright (%.1f deg)"), O.MaxTiltDeg), O.MaxTiltDeg > 60.0f);
	TestNearlyEqual(TEXT("The record's tilt is the attitude's (deg)"), O.Record.MaxTiltDeg, O.MaxTiltDeg, 0.5f);
	TestEqual(TEXT("No inversion"), O.Inversions, 0);
	TestEqual(TEXT("No inversion in the record"), O.Record.Inversions.Num(), 0);
	TestTrue(TEXT("The tracker set bRaley"), O.Record.bRaley);
	TestFalse(TEXT("...not an S-bend"), O.Record.bSBend);
	TestEqual(TEXT("The take-off move is the raley"), O.Record.TakeoffMove, ETrickMove::Raley);
	TestEqual(TEXT("Named \"Raley\""), O.Record.TrickName, FString(TEXT("Raley")));
	return true;
}

// T3.2: the same pop hooked in. The hook is close to the centre of mass and the hooked line torque is
// scaled down, so the body stays near upright: under 35 deg, and no raley.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickHookedPopNoRaley, "KiteSurf.Trick.HookedPopNoRaley", Flags)

bool FKiteSurfTrickHookedPopNoRaley::RunTest(const FString& Parameters)
{
	FRaleyRide Ride;
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	FPopPlan Plan = RaleyPlan();
	Plan.bUnhook = false;
	// Hooked, the bar is the kite's sheet: in the middle, where the rider rides (pushed out it would
	// depower the kite and there would be no pop to compare).
	Plan.Bar = Ride.Pawn->BarNeutralSheet;
	const FPopOutcome O = FlyPop(Ride, Plan);
	AddInfo(Describe(O));
	if (!TestTrue(TEXT("Popped, landed and recorded"), O.bTookOff && O.bLanded && O.bRecorded))
	{
		return true;
	}
	TestTrue(TEXT("Hooked for the jump"), O.Record.bHooked);
	TestTrue(FString::Printf(TEXT("The body stayed within 35 deg of upright (%.1f deg)"), O.MaxTiltDeg), O.MaxTiltDeg < 35.0f);
	TestFalse(TEXT("No raley"), O.Record.bRaley);
	TestNotEqual(TEXT("Not named \"Raley\""), O.Record.TrickName, FString(TEXT("Raley")));
	return true;
}

// T3.2: the arms make the raley. With the bar towards the hips (arm extension 0.1) the lines pull close in
// front of the centre of mass and the landing assist is not held off: the swing is far smaller, under
// 45 deg and strictly less than with the arms straight out.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRaleyNeedsExtendedArms, "KiteSurf.Trick.RaleyNeedsExtendedArms", Flags)

bool FKiteSurfTrickRaleyNeedsExtendedArms::RunTest(const FString& Parameters)
{
	FRaleyRide OutRide;
	FRaleyRide InRide;
	if (!TestTrue(TEXT("Rides set up"), OutRide.IsValid() && InRide.IsValid()))
	{
		return false;
	}
	const FPopOutcome Out = FlyPop(OutRide, RaleyPlan());
	// The bar position that gives an arm extension of 0.1: past the middle towards the hips.
	FPopPlan InPlan = RaleyPlan();
	const float N = InRide.Pawn->BarNeutralSheet;
	const float D = InRide.Pawn->UnhookedArmExtensionDefault;
	InPlan.Bar = N + (1.0f - N) * (1.0f - 0.1f / D);
	TestNearlyEqual(TEXT("That bar gives arms at 0.1"), AKiteRiderPawn::ArmExtensionForBar(InPlan.Bar, N, D), 0.1f, 1e-4f);
	const FPopOutcome In = FlyPop(InRide, InPlan);
	AddInfo(FString::Printf(TEXT("Arms out: %s"), *Describe(Out)));
	AddInfo(FString::Printf(TEXT("Arms 0.1: %s"), *Describe(In)));
	if (!TestTrue(TEXT("Both popped and landed"), Out.bLanded && In.bLanded))
	{
		return true;
	}
	TestTrue(FString::Printf(TEXT("Arms at 0.1: under 45 deg (%.1f)"), In.MaxTiltDeg), In.MaxTiltDeg < 45.0f);
	TestTrue(FString::Printf(TEXT("...strictly less than arms out (%.1f < %.1f deg)"), In.MaxTiltDeg, Out.MaxTiltDeg), In.MaxTiltDeg < Out.MaxTiltDeg);
	TestFalse(TEXT("Arms at 0.1: no raley"), In.Record.bRaley);
	return true;
}

// T3.2: the raley's swing is the same at 60 and 240 frames a second (the fixed step, split differently
// by the frames): the peak tilt within 3 deg.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRaleyStepRateIndependent, "KiteSurf.Trick.RaleyStepRateIndependent", Flags)

bool FKiteSurfTrickRaleyStepRateIndependent::RunTest(const FString& Parameters)
{
	float Peak[2] = { 0.0f, 0.0f };
	const float Rates[2] = { 60.0f, 240.0f };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FRaleyRide Ride(20.0f, 9.0f, 1.0f / Rates[Index]);
		if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
		{
			return false;
		}
		const FPopOutcome O = FlyPop(Ride, RaleyPlan());
		AddInfo(FString::Printf(TEXT("%.0f fps: %s"), Rates[Index], *Describe(O)));
		TestTrue(FString::Printf(TEXT("%.0f fps: landed"), Rates[Index]), O.bLanded);
		Peak[Index] = O.MaxTiltDeg;
	}
	TestTrue(FString::Printf(TEXT("Peak tilt %.2f deg at 60 fps and %.2f at 240 fps are within 3 deg"), Peak[0], Peak[1]), FMath::Abs(Peak[0] - Peak[1]) <= 3.0f);
	return true;
}

// T3.2, pure: the raley recogniser on synthetic rotations. A turn about the lines of 270 deg or more with
// the arms out and no pass is an S-bend, backside about -Sigma x the line (the back roll's sense) and the
// hinterberger the other way; less than 270, a pass, the arms in or hooked is none. A swing towards the
// kite past 60 deg with the arms out and no inversion is a raley; away from the kite, with an inversion,
// with the arms in or hooked it is not.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickClassifiesSBendSense, "KiteSurf.Trick.ClassifiesSBendSense", Flags)

bool FKiteSurfTrickClassifiesSBendSense::RunTest(const FString& Parameters)
{
	// The kite at 45 deg in front along +X; the rider upright.
	const FVector Line = FVector(1.0f, 0.0f, 1.0f).GetSafeNormal();
	for (const float Sigma : { 1.0f, -1.0f })
	{
		const FString S = FString::Printf(TEXT("Sigma %+.0f:"), Sigma);
		// Backside is about -Sigma x the line.
		const FRaleyResult Bs = RunSynthetic(-Sigma * Line, 360.0f, 1.0f, Line, true, true, 0, 0, Sigma);
		TestTrue(S + TEXT(" 360 backside about the line is an S-bend"), Bs.bSBend);
		TestEqual(S + TEXT(" ...backside"), Bs.SBendSense, ETrickSense::Backside);
		TestNearlyEqual(S + TEXT(" ...turned 360 about the line (deg)"), Bs.LineSpinDeg, 360.0f, 1.0f);
		TestFalse(S + TEXT(" ...and not also a raley"), Bs.bRaley);
		TestTrue(S + TEXT(" ...whose overhead turn tilted the body past 60 deg"), Bs.MaxTiltDeg > 60.0f);

		const FRaleyResult Fs = RunSynthetic(Sigma * Line, 300.0f, 1.0f, Line, true, true, 0, 0, Sigma);
		TestTrue(S + TEXT(" 300 frontside about the line is an S-bend"), Fs.bSBend);
		TestEqual(S + TEXT(" ...frontside: the hinterberger"), Fs.SBendSense, ETrickSense::Frontside);
		TestNearlyEqual(S + TEXT(" ...turned -300 about the line (deg)"), Fs.LineSpinDeg, -300.0f, 1.0f);

		TestFalse(S + TEXT(" 250 about the line is short of an S-bend"), RunSynthetic(-Sigma * Line, 250.0f, 1.0f, Line, true, true, 0, 0, Sigma).bSBend);
		TestFalse(S + TEXT(" 360 with a pass is not an S-bend"), RunSynthetic(-Sigma * Line, 360.0f, 1.0f, Line, true, true, 0, 1, Sigma).bSBend);
		TestFalse(S + TEXT(" 360 hooked is nothing"), RunSynthetic(-Sigma * Line, 360.0f, 1.0f, Line, true, false, 0, 0, Sigma).bSBend);
		const FRaleyResult NoArms = RunSynthetic(-Sigma * Line, 360.0f, 1.0f, Line, false, true, 0, 0, Sigma);
		TestFalse(S + TEXT(" 360 with the arms in is not an S-bend"), NoArms.bSBend);
		TestNearlyEqual(S + TEXT(" ...and counts nothing about the line (deg)"), NoArms.LineSpinDeg, 0.0f, 1e-3f);
	}

	// The raley: Up swung 70 deg towards the kite. A rotation about +Y tips Up towards +X.
	const FVector TowardsKite = FVector::YAxisVector;
	const FRaleyResult Raley = RunSynthetic(TowardsKite, 70.0f, 1.0f, Line, true, true, 0, 0);
	TestTrue(TEXT("70 deg towards the kite with the arms out is a raley"), Raley.bRaley);
	TestNearlyEqual(TEXT("...the most tilted (deg)"), Raley.MaxTiltDeg, 70.0f, 0.5f);
	TestTrue(TEXT("...leaning towards the kite"), Raley.SwingFromKiteDeg < 1.0f);
	TestFalse(TEXT("50 deg is short of a raley"), RunSynthetic(TowardsKite, 50.0f, 1.0f, Line, true, true, 0, 0).bRaley);
	TestFalse(TEXT("70 deg away from the kite is no raley"), RunSynthetic(-TowardsKite, 70.0f, 1.0f, Line, true, true, 0, 0).bRaley);
	TestFalse(TEXT("70 deg with an inversion counted is no raley"), RunSynthetic(TowardsKite, 70.0f, 1.0f, Line, true, true, 1, 0).bRaley);
	TestFalse(TEXT("70 deg with the arms in is no raley"), RunSynthetic(TowardsKite, 70.0f, 1.0f, Line, false, true, 0, 0).bRaley);
	TestFalse(TEXT("70 deg hooked is no raley"), RunSynthetic(TowardsKite, 70.0f, 1.0f, Line, true, false, 0, 0).bRaley);

	// The take-off move a record gets, and the names the freestyle table gives the S-bend's two senses.
	FJumpRecord Record;
	Record.bHooked = false;
	Record.bSBend = true;
	TestEqual(TEXT("An S-bend record's move"), FJumpRecorder::TakeoffMoveOf(Record), ETrickMove::SBend);
	Record.bSBend = false;
	Record.bRaley = true;
	TestEqual(TEXT("A raley record's move"), FJumpRecorder::TakeoffMoveOf(Record), ETrickMove::Raley);
	Record.bRaley = false;
	Record.Inversions.Add(ETrickInversion::BackFlip);
	TestEqual(TEXT("A backflip record's move"), FJumpRecorder::TakeoffMoveOf(Record), ETrickMove::BackFlip);
	FTrickSignature Signature;
	Signature.bHooked = false;
	Signature.bSBend = true;
	Signature.SpinHalfTurns = 2;
	Signature.SpinSense = ETrickSense::Backside;
	TestEqual(TEXT("A backside S-bend is named"), TrickNaming::Name(Signature), FString(TEXT("S-bend")));
	Signature.SpinSense = ETrickSense::Frontside;
	TestEqual(TEXT("A frontside one is named"), TrickNaming::Name(Signature), FString(TEXT("Hinterberger")));
	return true;
}

// T3.2 on a ride: unhooked with the arms straight out, a roll pre-wind and the roll stick held through
// the flight turn the body about the lines. Frontside it is named "Hinterberger". Backside the tracker
// finds the S-bend (backside, 270 deg or more about the lines, take-off move S-bend), but the name
// carries the bar's landing stance: the body starts its turn about the lines upright, before the raley
// has lined it up with them (the lines are slack for the first half of this pop), so the bar's wrap
// counts part of the turn and the lines can land round the front ("S-bend + backside 360 to toeside").
// That is the documented gap: the turn should come once the raley has the body along the lines.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSBendFromRaleyRoll, "KiteSurf.Trick.SBendFromRaleyRoll", Flags)

bool FKiteSurfTrickSBendFromRaleyRoll::RunTest(const FString& Parameters)
{
	for (const float StickX : { -1.0f, 1.0f })
	{
		const bool bBackside = StickX > 0.0f;
		const FString What = bBackside ? TEXT("Backside:") : TEXT("Frontside:");
		FRaleyRide Ride;
		if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
		{
			return false;
		}
		FPopPlan Plan = RaleyPlan();
		Plan.PreWind = FVector2D(StickX, 0.0f);
		Plan.AirStick = FVector2D(StickX, 0.0f);
		Plan.HoldSeconds = 3.0f;
		const FPopOutcome O = FlyPop(Ride, Plan);
		AddInfo(What + TEXT(" ") + Describe(O));
		if (!TestTrue(What + TEXT(" popped, landed and recorded"), O.bTookOff && O.bLanded && O.bRecorded))
		{
			continue;
		}
		TestTrue(What + TEXT(" the tracker found an S-bend"), O.Record.bSBend);
		TestEqual(What + TEXT(" ...of that sense"), O.Record.SBendSense, bBackside ? ETrickSense::Backside : ETrickSense::Frontside);
		TestTrue(FString::Printf(TEXT("%s ...turning 270 deg or more about the lines (%.0f)"), *What, O.Record.LineSpinDeg), FMath::Abs(O.Record.LineSpinDeg) >= 270.0f);
		TestEqual(What + TEXT(" ...the take-off move"), O.Record.TakeoffMove, ETrickMove::SBend);
		TestEqual(What + TEXT(" ...its inversions are the S-bend's own"), O.Record.Inversions.Num(), 0);
		if (bBackside)
		{
			TestTrue(What + TEXT(" named as an S-bend"), O.Record.TrickName.StartsWith(TEXT("S-bend")));
		}
		else
		{
			TestEqual(What + TEXT(" named"), O.Record.TrickName, FString(TEXT("Hinterberger")));
		}
	}
	return true;
}

// tricks.md 6.4 and T3.3: unhooked, the stick pulled for the pre-wind, a pop, the stick and the tuck held
// for half a second. One inversion, a backflip; the arms went in to the flip's extension; the back hand
// came off at the take-off, so only the front hand held the bar in the flight, and it was back on before
// the touchdown; named "Tantrum".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickTantrumIsBackFlip, "KiteSurf.Trick.TantrumIsBackFlip", Flags)

bool FKiteSurfTrickTantrumIsBackFlip::RunTest(const FString& Parameters)
{
	FRaleyRide Ride;
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	const FPopOutcome O = FlyPop(Ride, FlipPlan(true, -1.0f, 0.5f));
	AddInfo(Describe(O));
	if (!TestTrue(TEXT("Popped, landed and recorded"), O.bTookOff && O.bLanded && O.bRecorded))
	{
		return true;
	}
	TestEqual(TEXT("One inversion"), O.Inversions, 1);
	if (TestEqual(TEXT("One inversion in the record"), O.Record.Inversions.Num(), 1))
	{
		TestEqual(TEXT("...a flip, back: the backflip"), O.Record.Inversions[0], ETrickInversion::BackFlip);
	}
	TestEqual(TEXT("The take-off move is the backflip"), O.Record.TakeoffMove, ETrickMove::BackFlip);
	TestTrue(TEXT("The arms went in for the flip"), O.bFlipArmsInAir);
	TestEqual(TEXT("The back hand was off from the take-off"), O.FirstAirHands, EBarHands::FrontOnly);
	TestTrue(FString::Printf(TEXT("Only the front hand held the bar for most of the flight (%d of %d frames)"), O.FrontOnlyFrames, O.AirFrames), O.FrontOnlyFrames * 2 > O.AirFrames);
	TestEqual(TEXT("...and both were back on before the touchdown"), O.LastAirHands, EBarHands::Both);
	TestEqual(TEXT("Named \"Tantrum\""), O.Record.TrickName, FString(TEXT("Tantrum")));
	return true;
}

// T3.3: unhooked, the stick pushed for the pre-wind: a front flip, with both hands on the bar.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickFrontFlipUnhooked, "KiteSurf.Trick.FrontFlipUnhooked", Flags)

bool FKiteSurfTrickFrontFlipUnhooked::RunTest(const FString& Parameters)
{
	FRaleyRide Ride;
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	const FPopOutcome O = FlyPop(Ride, FlipPlan(true, 1.0f, 0.7f));
	AddInfo(Describe(O));
	if (!TestTrue(TEXT("Popped, landed and recorded"), O.bTookOff && O.bLanded && O.bRecorded))
	{
		return true;
	}
	if (TestEqual(TEXT("One inversion in the record"), O.Record.Inversions.Num(), 1))
	{
		TestEqual(TEXT("...a flip, front"), O.Record.Inversions[0], ETrickInversion::FrontFlip);
	}
	TestEqual(TEXT("The take-off move is the front flip"), O.Record.TakeoffMove, ETrickMove::FrontFlip);
	TestEqual(TEXT("Both hands on the bar throughout"), O.FrontOnlyFrames, 0);
	TestEqual(TEXT("Named through the freestyle table"), O.Record.TrickName, FString(TEXT("Front flip")));
	return true;
}

// T3.3: hooked in, the flip pre-wind is scaled by HookedFlipScale (0.35): the same inputs turn the rider
// strictly less about the flip axis than unhooked.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickHookedFlipUnderRotates, "KiteSurf.Trick.HookedFlipUnderRotates", Flags)

bool FKiteSurfTrickHookedFlipUnderRotates::RunTest(const FString& Parameters)
{
	FRaleyRide UnhookedRide;
	FRaleyRide HookedRide;
	if (!TestTrue(TEXT("Rides set up"), UnhookedRide.IsValid() && HookedRide.IsValid()))
	{
		return false;
	}
	const FPopOutcome Unhooked = FlyPop(UnhookedRide, FlipPlan(true, -1.0f, 0.5f));
	const FPopOutcome Hooked = FlyPop(HookedRide, FlipPlan(false, -1.0f, 0.5f));
	AddInfo(FString::Printf(TEXT("Unhooked: %s"), *Describe(Unhooked)));
	AddInfo(FString::Printf(TEXT("Hooked: %s"), *Describe(Hooked)));
	TestTrue(TEXT("Both popped and landed"), Unhooked.bLanded && Hooked.bLanded);
	TestTrue(TEXT("The hooked pop was hooked in"), Hooked.Record.bHooked);
	TestTrue(FString::Printf(TEXT("Hooked total pitch %.0f deg is strictly less than unhooked %.0f deg"), Hooked.PitchDeg, Unhooked.PitchDeg), Hooked.PitchDeg < Unhooked.PitchDeg);
	TestEqual(TEXT("Hooked, both hands stay on the bar"), Hooked.FrontOnlyFrames, 0);
	return true;
}

// T3.3, pure: one hand on the bar pulls from its own shoulder, off the body's middle, so a line straight in
// front rolls the body (a torque about Front); with both hands the attach is on the middle and it does not.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickOneHandAttachRolls, "KiteSurf.Trick.OneHandAttachRolls", Flags)

bool FKiteSurfTrickOneHandAttachRolls::RunTest(const FString& Parameters)
{
	const FLineAttachTunables T;
	const FVector Line = FVector(1.0f, 0.0f, 0.36f).GetSafeNormal();
	const FVector ForceN = Line * 600.0f;
	FBarState Both;
	Both.bHooked = false;
	FBarState FrontOnly = Both;
	FrontOnly.Hands = EBarHands::FrontOnly;
	for (const float Extension : { 1.0f, 0.15f })
	{
		for (const float Nose : { 1.0f, -1.0f })
		{
			const FString Case = FString::Printf(TEXT("Arms %.2f, nose %+.0f:"), Extension, Nose);
			const FVector TauBoth = FVector::CrossProduct(LineAttach::AttachPointBody(Both, Line, Extension, Nose, T) / 100.0f, ForceN);
			const FVector TauOne = FVector::CrossProduct(LineAttach::AttachPointBody(FrontOnly, Line, Extension, Nose, T) / 100.0f, ForceN);
			TestTrue(FString::Printf(TEXT("%s both hands: no roll (|tau . Front| %.2e N*m)"), *Case, FMath::Abs(TauBoth.X)), FMath::Abs(TauBoth.X) < 1e-3f);
			TestTrue(FString::Printf(TEXT("%s the front hand alone rolls the body (tau . Front %.1f N*m)"), *Case, TauOne.X), FMath::Abs(TauOne.X) > 1.0f);
			// The front hand is on the nose's side, so the roll's sign follows the nose.
			TestTrue(Case + TEXT(" ...one way for one nose side, the other way for the other"), TauOne.X * Nose > 0.0f);
		}
	}
	return true;
}

// T3.3: unhooked, a grab button takes that hand off the bar first: the back hand's Indy leaves the front
// hand alone on the bar (the lines pull at the front shoulder), the drawn hand reaches the board, and
// both hands are back on the bar for the landing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickUnhookedGrabTakesHandOff, "KiteSurf.Trick.UnhookedGrabTakesHandOff", Flags)

bool FKiteSurfTrickUnhookedGrabTakesHandOff::RunTest(const FString& Parameters)
{
	FRaleyRide Ride;
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	FPopPlan Plan;
	Plan.bUnhook = true;
	Plan.Bar = 0.5f;
	Plan.GrabAfterSeconds = 0.1f;
	Plan.GrabSeconds = 0.6f;
	Plan.bGrabBack = true;
	const FPopOutcome O = FlyPop(Ride, Plan);
	AddInfo(FString::Printf(TEXT("%s; grab held %d frames, hand off the socket %.2f cm at worst"), *Describe(O), O.GrabHeldFrames, O.WorstGrabHandCm));
	if (!TestTrue(TEXT("Popped, landed and recorded"), O.bTookOff && O.bLanded && O.bRecorded))
	{
		return true;
	}
	TestTrue(FString::Printf(TEXT("The hand held the board (%d frames)"), O.GrabHeldFrames), O.GrabHeldFrames >= 10);
	TestTrue(TEXT("While held, only the front hand was on the bar"), O.bGrabHandOffBar);
	TestTrue(TEXT("...and the lines pulled at the front hand's shoulder"), O.bGrabAttachOtherSide);
	TestTrue(FString::Printf(TEXT("The drawn back hand is on its socket (%.2f cm)"), O.WorstGrabHandCm), O.WorstGrabHandCm < 3.0f);
	TestEqual(TEXT("Both hands back on the bar before the touchdown"), O.LastAirHands, EBarHands::Both);
	if (TestEqual(TEXT("One grab in the record"), O.Record.Grabs.Num(), 1))
	{
		TestEqual(TEXT("...with the back hand"), O.Record.Grabs[0].Hand, ETrickHand::Back);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
