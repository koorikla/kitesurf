#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfUnits.h"
#include "WindComponent.h"
#include "Tricks/GrabState.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/LandingEvaluator.h"
#include "Tricks/RiderAttitudeComponent.h"
#include "Tricks/RiderAxes.h"
#include "Tricks/TrickTrackerComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// Batch A (docs/tricks/review.md section 4): IA_Rotate gates the rotation stick. Without it the left
// stick / WASD is always the board's carve and weight shift, so a plain jump stays straight; held
// while loading it reaches the pre-wind, and held in the air it reaches the rotation stick. These
// tests drive the player path through the same handlers Enhanced Input calls (OnEdgeTriggered,
// OnWeightShiftTriggered, OnJumpPressed, OnRotatePressed), as TrickInputTests.cpp does.

namespace TrickRotateGateTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float FrameSeconds = 1.0f / 60.0f;

	/** Inversion count as in docs/tricks.md section 6.6: body Up below -0.3 after being above +0.3. */
	struct FInversionTally
	{
		bool bArmed = true;
		int32 Count = 0;

		void Add(double UpDotWorldUp)
		{
			if (bArmed && UpDotWorldUp < -0.3)
			{
				++Count;
				bArmed = false;
			}
			else if (!bArmed && UpDotWorldUp > 0.3)
			{
				bArmed = true;
			}
		}
	};

	/**
	 * A rider on the water in steady wind along +X, started on a beam reach as the game mode does, with
	 * the camera boom's lag off (a copy of TrickInputTests.cpp's FInputRide, for a file of its own per
	 * docs/tricks/README.md decision 1). Player-path helpers (Stick, JumpButton, Rotate) go through the
	 * same Enhanced Input handlers the keys and the pad use.
	 */
	struct FGateRide
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		URiderAttitudeComponent* Attitude = nullptr;
		UTrickTrackerComponent* Tracker = nullptr;
		USpringArmComponent* Boom = nullptr;
		UCameraComponent* Camera = nullptr;

		explicit FGateRide(float WindKnots = 30.0f, float KiteM2 = 0.0f)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (!Pawn)
			{
				return;
			}
			Kite = Pawn->GetKite();
			Board = Pawn->GetBoardMovement();
			Attitude = Pawn->GetRiderAttitude();
			Tracker = Pawn->GetTrickTracker();
			Boom = Pawn->FindComponentByClass<USpringArmComponent>();
			Camera = Pawn->FindComponentByClass<UCameraComponent>();
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
			Kite->bParkHoldAssist = true;
			Kite->SetKiteModel(EKiteModel::Loop);
			Kite->SetKiteSize(KiteM2 > 0.0f ? KiteM2 : UKiteComponent::RecommendKiteSizeM2(WindKnots));
			if (Boom)
			{
				Boom->bEnableCameraLag = false;
				Boom->bEnableCameraRotationLag = false;
			}
		}

		~FGateRide()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Attitude && Tracker && Boom && Camera; }

		void Frame()
		{
			Pawn->Tick(FrameSeconds);
			Boom->TickComponent(FrameSeconds, LEVELTICK_All, nullptr);
		}

		bool HasReached(float SimSeconds) const
		{
			return Pawn->GetSimTimeSeconds() + 0.5f * Pawn->SimStepSeconds >= SimSeconds;
		}

		void SimulateUntil(float SimSeconds)
		{
			while (!HasReached(SimSeconds))
			{
				Frame();
			}
		}

		void Frames(float Seconds)
		{
			for (float T = 0.0f; T < Seconds - 0.5f * FrameSeconds; T += FrameSeconds)
			{
				Frame();
			}
		}

		bool IsAirborne() const { return Board->GetBoardState() == EBoardState::Airborne; }

		/** The left stick (or A/D and W/S) as Enhanced Input hands it to the pawn: X to IA_Edge, Y to IA_WeightShift. */
		void Stick(float X, float Y)
		{
			Pawn->OnEdgeTriggered(FInputActionValue(X));
			Pawn->OnWeightShiftTriggered(FInputActionValue(Y));
		}

		void JumpButton(bool bDown)
		{
			if (bDown)
			{
				Pawn->OnJumpPressed(FInputActionValue(true));
			}
			else
			{
				Pawn->OnJumpReleased(FInputActionValue(false));
			}
		}

		/** IA_Rotate: held, the stick reaches the pre-wind and the air rotation. */
		void Rotate(bool bHeld)
		{
			if (bHeld)
			{
				Pawn->OnRotatePressed(FInputActionValue(true));
			}
			else
			{
				Pawn->OnRotateReleased(FInputActionValue(false));
			}
		}
	};

	/**
	 * Rides to 8 s, steers the kite over, weights the tail for a clean pop, then holds LoadStick
	 * through the load (with IA_Rotate held too when bModifier is set), and pops at the timed jump's
	 * usual release point (0.66 s after the bar reaches the kite, rounded to 1/30 s). The tail weight
	 * is applied, and with the modifier latched, before LoadStick: the same order SendAndPop uses in
	 * TrickInputTests.cpp, so a rotation direction with no Y of its own (a pure roll) does not
	 * overwrite the pop's tail weight once the latch takes hold. With bModifier set,
	 * ReleaseLeadSeconds controls when the modifier is let go: negative (the default) releases it
	 * right after the pop, in the same input frame (no simulation step runs between them), which
	 * still gives the trick; 0 or more releases it that long before the intended pop while still on
	 * the water. Releasing the modifier while still loading unlatches the board a step early (the
	 * gate on EdgeBoard/SetWeightShift is bLoading && bRotateHeld), so it is never done before the pop
	 * itself. Returns the pre-wind at release; leaves the stick at zero and the rider airborne if the
	 * jump went ahead.
	 */
	float LoadAndPop(FGateRide& Ride, bool bModifier, const FVector2D& LoadStick, float ReleaseLeadSeconds = -1.0f)
	{
		AKiteRiderPawn* Pawn = Ride.Pawn;
		const float SendAt = 8.0f;
		Ride.SimulateUntil(SendAt);
		Pawn->SteerKite(-1.0f);
		Ride.Stick(0.0f, -1.0f); // S: the weight on the tail, applied before the modifier can latch it
		Ride.JumpButton(true);
		if (bModifier)
		{
			Ride.Rotate(true);
		}
		Ride.Stick(LoadStick.X, LoadStick.Y);
		const float ReleaseAt = SendAt + FMath::RoundToFloat((0.66f + Ride.Kite->GetSteeringDeadTimeSeconds()) * 30.0f) / 30.0f;
		bool bModifierReleased = !bModifier;
		while (!Ride.HasReached(ReleaseAt) && !Ride.IsAirborne())
		{
			if (!bModifierReleased && ReleaseLeadSeconds >= 0.0f && Ride.HasReached(ReleaseAt - ReleaseLeadSeconds))
			{
				Ride.Rotate(false);
				bModifierReleased = true;
			}
			Ride.Frame();
		}
		const float PreWindAtRelease = Pawn->GetPreWindAmount();
		Pawn->SheetKite(1.0f);
		// Pop first, then zero the stick and release the modifier if it is still held: either one,
		// done while still loading, would unlatch the board and overwrite the tail weight with the
		// now-zero stick a step early (see the note above).
		Ride.JumpButton(false); // pop
		Ride.Stick(0.0f, 0.0f);
		if (!bModifierReleased)
		{
			Ride.Rotate(false);
		}
		Pawn->SteerKite(0.0f);
		return PreWindAtRelease;
	}
}

// The tests sit inside the namespace rather than under a using-directive, which would leak into the
// next file of a unity build.
namespace TrickRotateGateTest
{

// A plain jump: full carve and the tail weight held from before the load, through the pop and 1 s
// into the air, with no modifier. Without IA_Rotate the stick never reaches the pre-wind or the air
// stick, so the jump leaves the water with no rotation and stays close to the flight's own turn.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickPlainJumpStaysStraightWithStickHeld, "KiteSurf.Trick.PlainJumpStaysStraightWithStickHeld", TrickRotateGateTest::Flags)

bool FKiteSurfTrickPlainJumpStaysStraightWithStickHeld::RunTest(const FString& Parameters)
{
	FGateRide Ride;
	if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	const float SendAt = 8.0f;
	Ride.SimulateUntil(SendAt);
	Pawn->SteerKite(-1.0f);
	// Full carve and the tail weight, held from before the load starts; no modifier at any point.
	Ride.Stick(1.0f, -1.0f);
	TestEqual(TEXT("On the water the carve follows the stick"), Ride.Board->GetEdgeInput(), 1.0f);
	Ride.JumpButton(true);
	const float ReleaseAt = SendAt + FMath::RoundToFloat((0.66f + Ride.Kite->GetSteeringDeadTimeSeconds()) * 30.0f) / 30.0f;
	bool bFollowedStickThroughLoad = true;
	while (!Ride.HasReached(ReleaseAt) && !Ride.IsAirborne())
	{
		Ride.Frame();
		bFollowedStickThroughLoad &= Ride.Board->GetEdgeInput() == 1.0f && Ride.Board->GetWeightShift() == -1.0f;
	}
	TestTrue(TEXT("With no modifier the board's weight shift stays -1 and its carve follows the stick through the load (no latch)"), bFollowedStickThroughLoad);
	const float PreWindAtPop = Pawn->GetPreWindAmount();
	TestEqual(TEXT("Pre-wind 0 at the pop: no modifier was ever held"), PreWindAtPop, 0.0f);
	Pawn->SheetKite(1.0f);
	Pawn->SteerKite(0.0f);
	Ride.JumpButton(false); // pop, stick still held
	if (!TestTrue(TEXT("Popped"), Ride.IsAirborne()))
	{
		return false;
	}

	FInversionTally Tally;
	float AirSeconds = 0.0f;
	float MaxTiltDeg = 0.0f;
	double SpinAboutUpDeg = 0.0;
	bool bAirRotationEverNonzero = false;
	bool bTookOffRotating = false;
	FVector Travel0 = FVector(Ride.Board->Velocity.X, Ride.Board->Velocity.Y, 0.0f).GetSafeNormal();
	float LastTravelYawDeg = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Travel0.Y, Travel0.X)));
	bool bFirstAirFrame = true;
	while (AirSeconds < 1.0f - 0.5f * FrameSeconds && Ride.IsAirborne())
	{
		Ride.Frame();
		if (!Ride.IsAirborne())
		{
			break;
		}
		if (bFirstAirFrame)
		{
			bTookOffRotating = Ride.Attitude->TookOffRotating();
			bFirstAirFrame = false;
		}
		AirSeconds += FrameSeconds;
		const FQuat Body = Ride.Attitude->GetBodyQuat();
		const double UpZ = Body.GetAxisZ().Z;
		Tally.Add(UpZ);
		MaxTiltDeg = FMath::Max(MaxTiltDeg, static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(UpZ, -1.0, 1.0)))));
		SpinAboutUpDeg += FMath::RadiansToDegrees(Ride.Attitude->GetAngularVelocity().Z) * FrameSeconds;
		bAirRotationEverNonzero |= Pawn->GetAirRotationInput().Size() > Pawn->AirRotationDeadzone;
		LastTravelYawDeg = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Ride.Board->Velocity.Y, Ride.Board->Velocity.X)));
	}
	const float TravelTurnDeg = FMath::FindDeltaAngleDegrees(static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Travel0.Y, Travel0.X))), LastTravelYawDeg);
	AddInfo(FString::Printf(TEXT("1 s in: tookoff rotating %d, air rotation ever nonzero %d, %d inversion(s), spin about up %.1f deg (flight turned %.1f), max tilt %.1f deg, crashing %d"),
		bTookOffRotating, bAirRotationEverNonzero, Tally.Count, SpinAboutUpDeg, TravelTurnDeg, MaxTiltDeg, Ride.Board->IsCrashing()));
	TestFalse(TEXT("The jump left the water with no rotation (no pre-wind was ever wound up)"), bTookOffRotating);
	TestFalse(TEXT("The held stick never reached the air rotation input"), bAirRotationEverNonzero);
	TestEqual(TEXT("No inversions in the first second"), Tally.Count, 0);
	TestTrue(FString::Printf(TEXT("Rotation about up within 30 deg of the flight's turn (%.1f against %.1f)"), SpinAboutUpDeg, TravelTurnDeg),
		FMath::Abs(SpinAboutUpDeg - TravelTurnDeg) < 30.0);
	TestTrue(FString::Printf(TEXT("Max tilt under 50 deg (%.1f)"), MaxTiltDeg), MaxTiltDeg < 50.0f);
	TestFalse(TEXT("Not crashing"), Ride.Board->IsCrashing());
	return true;
}

// The tail weight (S) alone, held through the load with no modifier, must not wind a backflip: the
// hooked flip pre-wind that used to crash the rider Inverted (HookedFlipUnderRotates) never builds.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickTailWeightIsNotABackflip, "KiteSurf.Trick.TailWeightIsNotABackflip", TrickRotateGateTest::Flags)

bool FKiteSurfTrickTailWeightIsNotABackflip::RunTest(const FString& Parameters)
{
	// Two scenarios: the HookedFlipUnderRotates recipe's wind and kite, and the 30 kn timed jump.
	struct FCase { const TCHAR* Name; float WindKnots; float KiteM2; };
	const FCase Cases[] = { { TEXT("HookedFlipUnderRotates' kite"), 20.0f, 9.0f }, { TEXT("the 30 kn timed jump"), 30.0f, 0.0f } };
	for (const FCase& C : Cases)
	{
		FGateRide Ride(C.WindKnots, C.KiteM2);
		if (!TestTrue(FString::Printf(TEXT("%s: ride fixture created"), C.Name), Ride.IsValid()))
		{
			continue;
		}
		AKiteRiderPawn* Pawn = Ride.Pawn;
		TestTrue(FString::Printf(TEXT("%s: hooked in by default"), C.Name), Pawn->IsHooked());
		const float PreWindAtRelease = LoadAndPop(Ride, /*bModifier*/ false, FVector2D(0.0f, -1.0f));
		TestEqual(FString::Printf(TEXT("%s: pre-wind 0, no modifier was held"), C.Name), PreWindAtRelease, 0.0f);
		if (!TestTrue(FString::Printf(TEXT("%s: popped"), C.Name), Ride.IsAirborne()))
		{
			continue;
		}
		FInversionTally Tally;
		float AirSeconds = 0.0f;
		bool bFlipFamilyEver = false;
		bool bFirstAirFrame = true;
		bool bTookOffRotating = false;
		while (AirSeconds < 1.5f - 0.5f * FrameSeconds && Ride.IsAirborne())
		{
			Ride.Frame();
			if (!Ride.IsAirborne())
			{
				break;
			}
			if (bFirstAirFrame)
			{
				bTookOffRotating = Ride.Attitude->TookOffRotating();
				bFirstAirFrame = false;
			}
			AirSeconds += FrameSeconds;
			Tally.Add(Ride.Attitude->GetBodyQuat().GetAxisZ().Z);
			bFlipFamilyEver |= Ride.Attitude->GetLastStepDebug().Family == RiderAxes::ERotationFamily::Flip;
		}
		AddInfo(FString::Printf(TEXT("%s: tookoff rotating %d, flip family ever %d, %d inversion(s), crashing %d"), C.Name, bTookOffRotating, bFlipFamilyEver, Tally.Count, Ride.Board->IsCrashing()));
		TestFalse(FString::Printf(TEXT("%s: no Flip family"), C.Name), bFlipFamilyEver);
		TestFalse(FString::Printf(TEXT("%s: no rotation at the take-off"), C.Name), bTookOffRotating);
		TestEqual(FString::Printf(TEXT("%s: no inversion"), C.Name), Tally.Count, 0);
		TestFalse(FString::Printf(TEXT("%s: not crashing (never Inverted)"), C.Name), Ride.Board->IsCrashing());
	}
	return true;
}

/** Flies the modifier-held pre-wind to landing, holding the air stick for AirHoldSeconds after the
 * pop, and reads the tracker's record of it. */
struct FGateJumpOutcome
{
	bool bTookOff = false;
	bool bLanded = false;
	bool bRecorded = false;
	float PreWindAtRelease = 0.0f;
	float ChestToTail = 0.0f;
	bool bCrashedAfter = false;
	FJumpRecord Record;
};

FGateJumpOutcome FlyRotateJump(FGateRide& Ride, const FVector2D& Dir, float AirHoldSeconds)
{
	FGateJumpOutcome Out;
	AKiteRiderPawn* Pawn = Ride.Pawn;
	const int32 RecordsBefore = Ride.Tracker->GetJumpRecordCount();
	Out.PreWindAtRelease = LoadAndPop(Ride, /*bModifier*/ true, Dir);
	if (!Ride.IsAirborne())
	{
		return Out;
	}
	Out.bTookOff = true;
	Ride.Rotate(true);
	Ride.Stick(Dir.X, Dir.Y);
	FVector Front0 = Ride.Attitude->GetBodyQuat().GetAxisX();
	FVector Travel0 = FVector(Ride.Board->Velocity.X, Ride.Board->Velocity.Y, 0.0f).GetSafeNormal();
	bool bChestMeasured = false;
	bool bStickHeld = true;
	float AirSeconds = 0.0f;
	const float GiveUp = Pawn->GetSimTimeSeconds() + 20.0f;
	while (!Ride.HasReached(GiveUp))
	{
		Ride.Frame();
		if (!Ride.IsAirborne())
		{
			Out.bLanded = true;
			break;
		}
		AirSeconds += FrameSeconds;
		if (bStickHeld && AirSeconds >= AirHoldSeconds)
		{
			Ride.Stick(0.0f, 0.0f);
			Ride.Rotate(false);
			bStickHeld = false;
		}
		if (!bChestMeasured && AirSeconds >= 0.15f)
		{
			const FQuat Body = Ride.Attitude->GetBodyQuat();
			Out.ChestToTail = static_cast<float>((Body.GetAxisX() - Front0) | -Travel0);
			bChestMeasured = true;
		}
		if (Ride.Board->Velocity.Z < 0.0f)
		{
			Pawn->SetLoadHeld(true); // coming down: crouch for the landing
		}
	}
	Pawn->SetLoadHeld(false);
	Ride.Frames(1.0f);
	Out.bCrashedAfter = Ride.Board->IsCrashing();
	Out.bRecorded = Ride.Tracker->GetJumpRecordCount() > RecordsBefore && Ride.Tracker->GetLastJumpRecord(Out.Record);
	return Out;
}

FString Describe(const FGateJumpOutcome& O)
{
	return FString::Printf(TEXT("pre-wind %.2f, chest to tail %.3f, landed %d, crashed after %d, recorded %d: '%s' (%s), %d inversion(s)"),
		O.PreWindAtRelease, O.ChestToTail, O.bLanded, O.bCrashedAfter, O.bRecorded, *O.Record.TrickName,
		*UEnum::GetDisplayValueAsText(O.Record.Grade).ToString(), O.Record.Inversions.Num());
}

// The modifier plus the stick towards the rider's back while loading: a full pre-wind, one inversion
// recorded as a back roll, the chest turning to the tail first, landed clean (not crashed).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRotateHeldPicksBackRoll, "KiteSurf.Trick.RotateHeldPicksBackRoll", TrickRotateGateTest::Flags)

bool FKiteSurfTrickRotateHeldPicksBackRoll::RunTest(const FString& Parameters)
{
	FGateRide Ride;
	if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
	{
		return false;
	}
	const float BackSign = Ride.Pawn->GetScreenBackSign();
	const FGateJumpOutcome O = FlyRotateJump(Ride, FVector2D(BackSign, 0.0f), 0.15f);
	AddInfo(Describe(O));
	TestTrue(TEXT("Popped and landed"), O.bTookOff && O.bLanded);
	TestTrue(FString::Printf(TEXT("Pre-wind full by the pop (%.2f)"), O.PreWindAtRelease), O.PreWindAtRelease > 0.99f);
	if (!TestTrue(TEXT("Recorded"), O.bRecorded))
	{
		return true;
	}
	TestEqual(TEXT("Exactly one inversion"), O.Record.Inversions.Num(), 1);
	if (O.Record.Inversions.Num() == 1)
	{
		TestEqual(TEXT("Recorded as a back roll"), O.Record.Inversions[0], ETrickInversion::BackRoll);
	}
	TestEqual(TEXT("The take-off move is the back roll"), O.Record.TakeoffMove, ETrickMove::BackRoll);
	TestTrue(TEXT("The chest turns towards the tail first"), O.ChestToTail > 0.0f);
	TestFalse(TEXT("Not crashed"), O.bCrashedAfter);
	TestTrue(TEXT("Not graded Crash"), O.Record.Grade != ELandingGrade::Crash);
	return true;
}

// The same, with the stick away from the rider's back: a front roll.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRotateHeldPicksFrontRoll, "KiteSurf.Trick.RotateHeldPicksFrontRoll", TrickRotateGateTest::Flags)

bool FKiteSurfTrickRotateHeldPicksFrontRoll::RunTest(const FString& Parameters)
{
	FGateRide Ride;
	if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
	{
		return false;
	}
	const float BackSign = Ride.Pawn->GetScreenBackSign();
	const FGateJumpOutcome O = FlyRotateJump(Ride, FVector2D(-BackSign, 0.0f), 0.15f);
	AddInfo(Describe(O));
	TestTrue(TEXT("Popped and landed"), O.bTookOff && O.bLanded);
	TestTrue(FString::Printf(TEXT("Pre-wind full by the pop (%.2f)"), O.PreWindAtRelease), O.PreWindAtRelease > 0.99f);
	if (!TestTrue(TEXT("Recorded"), O.bRecorded))
	{
		return true;
	}
	TestEqual(TEXT("Exactly one inversion"), O.Record.Inversions.Num(), 1);
	if (O.Record.Inversions.Num() == 1)
	{
		TestEqual(TEXT("Recorded as a front roll"), O.Record.Inversions[0], ETrickInversion::FrontRoll);
	}
	TestEqual(TEXT("The take-off move is the front roll"), O.Record.TakeoffMove, ETrickMove::FrontRoll);
	return true;
}

// Letting the modifier go well before the pop cancels the trick; letting it go in the pop's own frame
// (the grace, RotateReleaseGraceSeconds) still gives it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRotateLetGoBeforePopCancels, "KiteSurf.Trick.RotateLetGoBeforePopCancels", TrickRotateGateTest::Flags)

bool FKiteSurfTrickRotateLetGoBeforePopCancels::RunTest(const FString& Parameters)
{
	// Let go 0.3 s before the pop: well past RotateReleaseGraceSeconds (0.1 s default).
	{
		FGateRide Ride;
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		const float PreWindAtRelease = LoadAndPop(Ride, /*bModifier*/ true, FVector2D(1.0f, 0.0f), 0.3f);
		AddInfo(FString::Printf(TEXT("Let go 0.3 s early: pre-wind at release %.2f"), PreWindAtRelease));
		if (TestTrue(TEXT("Popped"), Ride.IsAirborne()))
		{
			Ride.Frame();
			TestFalse(TEXT("No rotation: the modifier was let go well before the pop"), Ride.Attitude->TookOffRotating());
		}
	}
	// Let go in the pop's own frame: the grace still gives the roll.
	{
		FGateRide Ride;
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		const float PreWindAtRelease = LoadAndPop(Ride, /*bModifier*/ true, FVector2D(1.0f, 0.0f));
		AddInfo(FString::Printf(TEXT("Let go in the pop's frame: pre-wind at release %.2f"), PreWindAtRelease));
		TestTrue(FString::Printf(TEXT("Pre-wind full by the pop (%.2f)"), PreWindAtRelease), PreWindAtRelease > 0.99f);
		if (TestTrue(TEXT("Popped"), Ride.IsAirborne()))
		{
			Ride.Frame();
			TestTrue(TEXT("Still rolls: the grace covers letting go in the same frame as the pop"), Ride.Attitude->TookOffRotating());
			TestEqual(TEXT("It is a roll"), Ride.Attitude->GetLastStepDebug().Family, RiderAxes::ERotationFamily::Roll);
		}
	}
	return true;
}

// In the air, the stick alone (no modifier) must not reach the attitude: the air rotation input stays
// zero and the landing assist is free to act in its window. With the modifier pressed in the air, the
// control torque is non-zero and the family is a roll, not a spin (X alone is a roll since batch A).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickAirStickNeedsRotate, "KiteSurf.Trick.AirStickNeedsRotate", TrickRotateGateTest::Flags)

bool FKiteSurfTrickAirStickNeedsRotate::RunTest(const FString& Parameters)
{
	// A plain pop (no pre-wind, no modifier), then the stick alone in the air.
	{
		FGateRide Ride;
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		LoadAndPop(Ride, /*bModifier*/ false, FVector2D::ZeroVector);
		if (!TestTrue(TEXT("Popped"), Ride.IsAirborne()))
		{
			return false;
		}
		Ride.Frame();
		Ride.Stick(1.0f, 0.0f); // no IA_Rotate held
		TestEqual(TEXT("Stick alone gives zero air rotation input"), Ride.Pawn->GetAirRotationInput(), FVector2D::ZeroVector);
		TestTrue(TEXT("No control torque without the modifier"), Ride.Attitude->GetLastStepDebug().ControlTorqueNm.IsZero());
		// Run to near the landing so the assist's time-to-contact window opens; it should act since
		// bRotationInput stays false throughout.
		bool bAssistActed = false;
		const float GiveUp = Ride.Pawn->GetSimTimeSeconds() + 20.0f;
		while (!Ride.HasReached(GiveUp) && Ride.IsAirborne())
		{
			Ride.Frame();
			if (!Ride.IsAirborne())
			{
				break;
			}
			bAssistActed |= Ride.Attitude->GetLastStepDebug().bAssistActive;
			if (Ride.Board->Velocity.Z < 0.0f)
			{
				Ride.Pawn->SetLoadHeld(true);
			}
		}
		TestTrue(TEXT("The landing assist acted in its window"), bAssistActed);
	}
	// A plain pop, then the modifier and the stick together in the air.
	{
		FGateRide Ride;
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		LoadAndPop(Ride, /*bModifier*/ false, FVector2D::ZeroVector);
		if (!TestTrue(TEXT("Popped"), Ride.IsAirborne()))
		{
			return false;
		}
		Ride.Frame();
		Ride.Rotate(true);
		Ride.Stick(1.0f, 0.0f);
		Ride.Frame();
		TestTrue(TEXT("The stick reaches the air rotation input with the modifier held"), Ride.Pawn->GetAirRotationInput().X > 0.99f);
		TestTrue(TEXT("Non-zero control torque with the modifier held"), !Ride.Attitude->GetLastStepDebug().ControlTorqueNm.IsZero());
		TestEqual(TEXT("The family is Roll, not Spin (X alone is a roll since batch A)"), Ride.Attitude->GetLastStepDebug().Family, RiderAxes::ERotationFamily::Roll);
		Ride.Stick(0.0f, 0.0f);
		Ride.Rotate(false);
	}
	return true;
}

// Pure: RiderAxes::ChooseAxisBody with the batch A defaults (AirStickTiltWithoutPreWindDeg 65,
// RollAxisTiltRangeDeg 65, SpinAxisTiltMaxDeg 25).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRotateStickMapping, "KiteSurf.Trick.RotateStickMapping", TrickRotateGateTest::Flags)

bool FKiteSurfTrickRotateStickMapping::RunTest(const FString& Parameters)
{
	URiderAttitudeComponent* A = NewObject<URiderAttitudeComponent>();
	auto Choose = [A](const FVector2D& Stick)
	{
		return RiderAxes::ChooseAxisBody(Stick, 1.0f, A->DefaultRollAxisTiltDeg, A->RollAxisTiltRangeDeg, A->FlipSectorDeg, A->SpinAxisTiltMaxDeg, A->FlipSectorHysteresisDeg, false);
	};

	const RiderAxes::FRotationAxisChoice Roll = Choose(FVector2D(1.0f, 0.0f));
	TestEqual(TEXT("(1, 0) is a Roll"), Roll.Family, RiderAxes::ERotationFamily::Roll);
	TestEqual(TEXT("(1, 0) tilts to 65 deg"), Roll.TiltDeg, 65.0f);

	const RiderAxes::FRotationAxisChoice Spin = Choose(FVector2D(0.71f, 0.71f));
	TestEqual(TEXT("(0.71, 0.71) is a Spin"), Spin.Family, RiderAxes::ERotationFamily::Spin);

	const RiderAxes::FRotationAxisChoice InvertedRoll = Choose(FVector2D(0.71f, -0.71f));
	TestEqual(TEXT("(0.71, -0.71) is a Roll"), InvertedRoll.Family, RiderAxes::ERotationFamily::Roll);
	TestTrue(FString::Printf(TEXT("(0.71, -0.71) tilts over 90 deg (%.1f)"), InvertedRoll.TiltDeg), InvertedRoll.TiltDeg > 90.0f);

	const RiderAxes::FRotationAxisChoice FlipUp = Choose(FVector2D(0.0f, 1.0f));
	TestEqual(TEXT("(0, 1) is a Flip"), FlipUp.Family, RiderAxes::ERotationFamily::Flip);
	const RiderAxes::FRotationAxisChoice FlipDown = Choose(FVector2D(0.0f, -1.0f));
	TestEqual(TEXT("(0, -1) is a Flip"), FlipDown.Family, RiderAxes::ERotationFamily::Flip);

	const RiderAxes::FRotationAxisChoice Full = Choose(FVector2D(1.0f, 0.0f));
	const RiderAxes::FRotationAxisChoice Half = Choose(FVector2D(0.5f, 0.0f));
	TestTrue(FString::Printf(TEXT("Half stick gives half the take-off rate, within 5%% (%.3f against %.3f)"), Half.Magnitude, Full.Magnitude),
		FMath::IsNearlyEqual(Half.Magnitude, Full.Magnitude * 0.5f, Full.Magnitude * 0.05f));
	return true;
}

// Holding the modifier and a full stick for 0.25 s of a 0.5 s build gives half the pre-wind.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRotateAmountFollowsWindUp, "KiteSurf.Trick.RotateAmountFollowsWindUp", TrickRotateGateTest::Flags)

bool FKiteSurfTrickRotateAmountFollowsWindUp::RunTest(const FString& Parameters)
{
	FGateRide Ride;
	if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	Ride.SimulateUntil(8.0f);
	Pawn->SteerKite(-1.0f);
	Ride.Rotate(true);
	Ride.Stick(1.0f, 0.0f);
	Ride.JumpButton(true);
	Ride.Frames(0.25f);
	const float PreWind = Pawn->GetPreWindAmount();
	AddInfo(FString::Printf(TEXT("Pre-wind after 0.25 s of a 0.5 s build, held with the modifier: %.3f"), PreWind));
	TestTrue(FString::Printf(TEXT("Pre-wind 0.5 +- 0.05 (%.3f)"), PreWind), PreWind > 0.45f && PreWind < 0.55f);
	return true;
}

// A grab button held picks its zone from the stick whatever IA_Rotate is doing: the grab already
// commits to picking a zone, not a rotation (RoutePlayerRiderInput never gates GrabZoneStick).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickGrabZoneWithoutRotate, "KiteSurf.Trick.GrabZoneWithoutRotate", TrickRotateGateTest::Flags)

bool FKiteSurfTrickGrabZoneWithoutRotate::RunTest(const FString& Parameters)
{
	FGateRide Ride;
	if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	LoadAndPop(Ride, /*bModifier*/ false, FVector2D::ZeroVector);
	if (!TestTrue(TEXT("Popped"), Ride.IsAirborne()))
	{
		return false;
	}
	Ride.Frame();
	// GrabZoneStick is the raw stick times the latched screen-back sign (towards the back is +X, the
	// heel edge); away from the back (-X here) is the toe edge, whichever way that sign fell.
	const float AwayFromBack = -Pawn->GetScreenBackSign();

	// Without the modifier: the grab back hand held, stick away from the back (the toe edge).
	Pawn->OnGrabBackPressed(FInputActionValue(true));
	Ride.Stick(AwayFromBack, 0.0f);
	Ride.Frames(0.25f);
	TestTrue(TEXT("Without the modifier: the stick still picks the zone"), Pawn->GetGrabState().IsZoneStickActive());
	TestEqual(TEXT("Without the modifier: away from the back is the toe edge"), Pawn->GetGrabState().GetZone(), ETrickGrabZone::ToeEdge);
	Ride.Stick(0.0f, 0.0f);
	Pawn->OnGrabBackReleased(FInputActionValue(false));

	// With the modifier held: the same stick still picks the same zone.
	Ride.Rotate(true);
	Pawn->OnGrabBackPressed(FInputActionValue(true));
	Ride.Stick(AwayFromBack, 0.0f);
	Ride.Frames(0.25f);
	TestTrue(TEXT("With the modifier: the stick still picks the zone"), Pawn->GetGrabState().IsZoneStickActive());
	TestEqual(TEXT("With the modifier: away from the back is still the toe edge"), Pawn->GetGrabState().GetZone(), ETrickGrabZone::ToeEdge);
	Ride.Stick(0.0f, 0.0f);
	Pawn->OnGrabBackReleased(FInputActionValue(false));
	Ride.Rotate(false);
	return true;
}

// The raley needs no stick or modifier: it is pure line torque once the rider is unhooked with the
// arms out. The S-bend, which reads the roll stick, gates on IA_Rotate like any other stick-driven
// rotation (the attitude's reaction to the stick is unchanged; only whether the stick reaches it).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRaleyStaysPhysical, "KiteSurf.Trick.RaleyStaysPhysical", TrickRotateGateTest::Flags)

bool FKiteSurfTrickRaleyStaysPhysical::RunTest(const FString& Parameters)
{
	auto FlyUnhookedPop = [](FGateRide& Ride, bool bModifier, float StickX, float HoldSeconds) -> FGateJumpOutcome
	{
		FGateJumpOutcome Out;
		AKiteRiderPawn* Pawn = Ride.Pawn;
		const int32 RecordsBefore = Ride.Tracker->GetJumpRecordCount();
		Ride.SimulateUntil(0.5f);
		Pawn->PressHook();
		Ride.Frame(); // unhooks
		Ride.SimulateUntil(8.0f);
		Pawn->SheetKite(0.0f); // the bar fully out: the arms straight along the lines (the raley's extension)
		// A stronger pop (not part of the rotation gate), applied and, with the modifier, latched
		// before the rotation direction: the same order LoadAndPop uses above.
		Ride.Stick(0.0f, -1.0f);
		Ride.JumpButton(true);
		if (bModifier)
		{
			Ride.Rotate(true);
		}
		if (StickX != 0.0f)
		{
			Ride.Stick(StickX, 0.0f);
		}
		Ride.SimulateUntil(8.5f);
		Out.PreWindAtRelease = Pawn->GetPreWindAmount();
		// Pop first, then zero the stick and release the modifier: either one, done while still
		// loading, would unlatch the board (bRotateGate goes false) and overwrite the tail weight
		// with the now-zero stick a step early, the same bug LoadAndPop's comment above describes.
		Ride.JumpButton(false); // pop
		Ride.Stick(0.0f, 0.0f);
		if (bModifier)
		{
			Ride.Rotate(false);
		}
		Ride.Board->SetWeightShift(0.0f);
		if (!Ride.IsAirborne())
		{
			return Out;
		}
		Out.bTookOff = true;
		if (bModifier && StickX != 0.0f)
		{
			Ride.Rotate(true);
			Ride.Stick(StickX, 0.0f);
		}
		bool bStickHeld = bModifier && StickX != 0.0f;
		float AirSeconds = 0.0f;
		const float GiveUp = Pawn->GetSimTimeSeconds() + 5.0f;
		while (!Ride.HasReached(GiveUp))
		{
			Ride.Frame();
			if (!Ride.IsAirborne())
			{
				Out.bLanded = true;
				break;
			}
			AirSeconds += FrameSeconds;
			if (bStickHeld && AirSeconds >= HoldSeconds)
			{
				Ride.Stick(0.0f, 0.0f);
				Ride.Rotate(false);
				bStickHeld = false;
			}
		}
		Ride.Stick(0.0f, 0.0f);
		Ride.Rotate(false);
		Out.bRecorded = Ride.Tracker->GetJumpRecordCount() > RecordsBefore && Ride.Tracker->GetLastJumpRecord(Out.Record);
		return Out;
	};

	// Unhooked, arms out, no stick and no modifier: still the raley.
	{
		FGateRide Ride(20.0f, 9.0f);
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		const FGateJumpOutcome O = FlyUnhookedPop(Ride, false, 0.0f, 0.0f);
		AddInfo(FString::Printf(TEXT("No stick, no modifier: %s"), *Describe(O)));
		if (TestTrue(TEXT("Popped, landed and recorded"), O.bTookOff && O.bLanded && O.bRecorded))
		{
			TestTrue(FString::Printf(TEXT("Still a raley (tilt %.1f deg)"), O.Record.MaxTiltDeg), O.Record.bRaley && O.Record.MaxTiltDeg > 60.0f);
		}
	}
	// Unhooked, arms out, stick X held without the modifier: no S-bend (the stick never reaches the
	// pre-wind or the air rotation).
	{
		FGateRide Ride(20.0f, 9.0f);
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		const FGateJumpOutcome O = FlyUnhookedPop(Ride, false, 1.0f, 3.0f);
		AddInfo(FString::Printf(TEXT("Stick held, no modifier: %s"), *Describe(O)));
		if (TestTrue(TEXT("Popped, landed and recorded"), O.bTookOff && O.bLanded && O.bRecorded))
		{
			TestFalse(TEXT("No S-bend without the modifier"), O.Record.bSBend);
		}
	}
	// Unhooked, arms out, stick X held with the modifier (pre-wind and air stick, 3 s, as
	// SBendFromRaleyRoll): the S-bend.
	{
		FGateRide Ride(20.0f, 9.0f);
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		const FGateJumpOutcome O = FlyUnhookedPop(Ride, true, 1.0f, 3.0f);
		AddInfo(FString::Printf(TEXT("Stick held with the modifier: %s"), *Describe(O)));
		if (TestTrue(TEXT("Popped, landed and recorded"), O.bTookOff && O.bLanded && O.bRecorded))
		{
			TestTrue(TEXT("The S-bend, with the modifier held"), O.Record.bSBend);
			TestEqual(TEXT("The take-off move is the S-bend"), O.Record.TakeoffMove, ETrickMove::SBend);
		}
	}
	return true;
}

} // namespace TrickRotateGateTest

#endif // WITH_DEV_AUTOMATION_TESTS
