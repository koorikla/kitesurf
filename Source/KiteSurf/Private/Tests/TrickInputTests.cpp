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
#include "Tricks/LandingEvaluator.h"
#include "Tricks/RiderAttitudeComponent.h"
#include "Tricks/RiderAxes.h"
#include "UI/KiteSurfControlsLegend.h"

#if WITH_DEV_AUTOMATION_TESTS

// Player input for the rider's rotation (T1.4). The left stick and WASD (IA_Edge, IA_WeightShift)
// are read by state: the board on the water, the pre-wind while jump is held, the rotation in the
// air; jump pressed and held in the air is the tuck. These tests drive the pawn through the same
// handlers Enhanced Input calls (OnEdgeTriggered, OnWeightShiftTriggered, OnJumpPressed,
// OnJumpReleased). Only the bar (the kite) is scripted, as the player's other hand.

namespace TrickInputTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float FrameSeconds = 1.0f / 60.0f;

	/** Inversion count as in docs/tricks.md section 6.6: body Up below -0.3 after being above +0.3. */
	struct FInversionTally
	{
		bool bArmed = true;
		int32 Count = 0;
		float FirstInvertedAt = -1.0f;
		float BackUprightAt = -1.0f;

		void Add(double UpDotWorldUp, float Time)
		{
			if (bArmed && UpDotWorldUp < -0.3)
			{
				++Count;
				bArmed = false;
				if (FirstInvertedAt < 0.0f) { FirstInvertedAt = Time; }
			}
			else if (!bArmed && UpDotWorldUp > 0.3)
			{
				bArmed = true;
				if (BackUprightAt < 0.0f) { BackUprightAt = Time; }
			}
		}
	};

	/**
	 * A rider on the water in steady wind along +X, started on a beam reach on either tack as the game
	 * mode does, stepped one frame at a time, with the camera boom's lag off and the boom ticked after
	 * the pawn (the bare world does not tick it). A copy of the T1.2 ride fixture, with the tack.
	 */
	struct FInputRide
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		URiderAttitudeComponent* Attitude = nullptr;
		USpringArmComponent* Boom = nullptr;
		UCameraComponent* Camera = nullptr;

		explicit FInputRide(float WindKnots = 30.0f, float TackSide = 1.0f)
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
			AKiteSurfGameMode::InitializeRide(Pawn, KiteUnits::KnotsToCmS(12.0f), TackSide);
			Kite->bParkHoldAssist = true;
			Kite->SetKiteModel(EKiteModel::Loop);
			Kite->SetKiteSize(UKiteComponent::RecommendKiteSizeM2(WindKnots));
			if (Boom)
			{
				Boom->bEnableCameraLag = false;
				Boom->bEnableCameraRotationLag = false;
			}
		}

		~FInputRide()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Attitude && Boom && Camera; }

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

		/** IA_Rotate (batch A): held, the stick reaches the pre-wind and the air rotation. */
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

	/** What the air stick does in a player-path jump. */
	enum class EAirStickPolicy : uint8
	{
		/** Never touched in the air. */
		None,
		/** Held from the take-off until the body has turned SpinTargetDeg about up, then let go. */
		UntilSpun,
		/** Held from the take-off for HoldSeconds, then let go. */
		ForSeconds
	};

	struct FPlayerJumpScript
	{
		/** Pre-wind in rotation axes (+X back roll); turned into the raw stick by the latched screen side. */
		FVector2D PreWind = FVector2D::ZeroVector;
		/** Air stick in rotation axes. */
		FVector2D AirStick = FVector2D::ZeroVector;
		EAirStickPolicy Policy = EAirStickPolicy::None;
		float SpinTargetDeg = 200.0f;
		float HoldSeconds = 0.0f;
	};

	struct FPlayerJumpResult
	{
		bool bValid = false;
		bool bTookOff = false;
		bool bLanded = false;
		bool bNaN = false;
		float AirSeconds = 0.0f;
		float BackSign = 0.0f;
		float PreWindAtRelease = 0.0f;
		/** The board's carve and weight shift held still through the load (the latch). */
		bool bBoardLatchedThroughLoad = true;
		int32 Inversions = 0;
		float FirstInvertedAt = -1.0f;
		float BackUprightAt = -1.0f;
		float ChestToTail = 0.0f;
		/** Rotation about world up over the airtime (deg), from the attitude's angular velocity. */
		float SpinAboutUpDeg = 0.0f;
		/** Rotation about the body's own up over the airtime (deg). */
		float SpinAboutBodyUpDeg = 0.0f;
		float MaxTiltDeg = 0.0f;
		/** The air stick was routed to the attitude, and what family it asked for while held. */
		bool bAirStickRouted = false;
		bool bFamilySpinWhileHeld = false;
		bool bFamilyRollWhileHeld = false;
		float StickReleasedAt = -1.0f;
		bool bAssistActedAfterRelease = false;
		bool bTookOffRotating = false;
		FLandingVerdict Verdict;
		FLandingInputs LandingInputs;
		/** After the landing, the stick carves the board again. */
		float BoardEdgeAfterLanding = 0.0f;
		bool bCrashedAfter = false;
	};

	struct FSendResult
	{
		float BackSign = 0.0f;
		float PreWindAtRelease = 0.0f;
		bool bBoardLatchedThroughLoad = true;
		float ReleaseAt = 0.0f;
	};

	/**
	 * The phase 2 timed jump (30 kn, the recommended kite) up to the pop, through the player's handlers:
	 * 8 s of riding, weight on the tail (S), jump pressed to load, the stick to the pre-wind (given in
	 * rotation axes and turned into the raw stick by the latched screen side), the bar hard over
	 * (scripted, the other hand), the stick let go and jump let go to pop 0.66 s after the bar reaches
	 * the kite, and the bar centred.
	 */
	FSendResult SendAndPop(FInputRide& Ride, const FVector2D& PreWind)
	{
		FSendResult S;
		AKiteRiderPawn* Pawn = Ride.Pawn;
		const bool bWantsRotation = !PreWind.IsZero();
		const float SendAt = 8.0f;
		Ride.SimulateUntil(SendAt);
		Pawn->SteerKite(-1.0f);
		Ride.Stick(0.0f, -1.0f);   // S: the weight on the tail
		Ride.JumpButton(true);     // load: the board keeps the tail weight from here
		S.BackSign = Pawn->GetScreenBackSign();
		// IA_Rotate (batch A): held whenever a pre-wind is wanted, so the stick reaches it.
		if (bWantsRotation)
		{
			Ride.Rotate(true);
		}
		Ride.Stick(PreWind.X * S.BackSign, PreWind.Y);
		const float LatchedEdge = Ride.Board->GetEdgeInput();
		const float LatchedWeight = Ride.Board->GetWeightShift();
		S.ReleaseAt = SendAt + FMath::RoundToFloat((0.66f + Ride.Kite->GetSteeringDeadTimeSeconds()) * 30.0f) / 30.0f;
		while (!Ride.HasReached(S.ReleaseAt) && !Ride.IsAirborne())
		{
			Ride.Frame();
			S.bBoardLatchedThroughLoad &= Ride.Board->GetEdgeInput() == LatchedEdge && Ride.Board->GetWeightShift() == LatchedWeight;
		}
		S.PreWindAtRelease = Pawn->GetPreWindAmount();
		Pawn->SheetKite(1.0f);
		Ride.Stick(0.0f, 0.0f);
		Ride.JumpButton(false);    // pop
		// Released after the pop (not before): releasing IA_Rotate while still loading would unlatch
		// the board a step early (the gate on EdgeBoard/SetWeightShift is bLoading && bRotateHeld), which
		// would lose the tail weight right as the board pops. The grace (RotateReleaseGraceSeconds)
		// means the pre-wind this built is unaffected either way, since nothing reads it between here
		// and the take-off.
		if (bWantsRotation)
		{
			Ride.Rotate(false);
		}
		Pawn->SteerKite(0.0f);
		return S;
	}

	/**
	 * The timed jump played through the player's handlers (SendAndPop), then the air stick by the
	 * policy, and the scripted crouch for the landing from the apex.
	 */
	FPlayerJumpResult RunPlayerJump(const FPlayerJumpScript& Script)
	{
		FPlayerJumpResult R;
		FInputRide Ride(30.0f);
		if (!Ride.IsValid())
		{
			return R;
		}
		R.bValid = true;
		AKiteRiderPawn* Pawn = Ride.Pawn;
		const FSendResult Send = SendAndPop(Ride, Script.PreWind);
		R.BackSign = Send.BackSign;
		R.PreWindAtRelease = Send.PreWindAtRelease;
		R.bBoardLatchedThroughLoad = Send.bBoardLatchedThroughLoad;
		const float ReleaseAt = Send.ReleaseAt;

		const bool bUseStick = Script.Policy != EAirStickPolicy::None;
		bool bStickHeld = false;
		FInversionTally Tally;
		FVector Front0 = FVector::ZeroVector;
		FVector Travel0 = FVector::ZeroVector;
		bool bChestMeasured = false;
		bool bWasAir = false;
		const float EndAt = ReleaseAt + 20.0f;
		while (!Ride.HasReached(EndAt))
		{
			if (bWasAir && bUseStick && !bStickHeld && R.StickReleasedAt < 0.0f)
			{
				// IA_Rotate (batch A): held in the air too, so the stick reaches the attitude.
				Ride.Rotate(true);
				Ride.Stick(Script.AirStick.X * R.BackSign, Script.AirStick.Y);
				bStickHeld = true;
			}
			Ride.Frame();
			const bool bAir = Ride.IsAirborne();
			R.bNaN |= Pawn->GetActorLocation().ContainsNaN() || Ride.Attitude->GetBodyQuat().ContainsNaN();
			if (bAir)
			{
				if (!bWasAir)
				{
					R.bTookOff = true;
					Front0 = Ride.Attitude->GetBodyQuat().GetAxisX();
					Travel0 = FVector(Ride.Board->Velocity.X, Ride.Board->Velocity.Y, 0.0f).GetSafeNormal();
				}
				bWasAir = true;
				R.AirSeconds += FrameSeconds;
				R.bTookOffRotating |= Ride.Attitude->TookOffRotating();
				const FQuat Body = Ride.Attitude->GetBodyQuat();
				const double UpZ = Body.GetAxisZ().Z;
				Tally.Add(UpZ, R.AirSeconds);
				R.MaxTiltDeg = FMath::Max(R.MaxTiltDeg, static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(UpZ, -1.0, 1.0)))));
				const FVector Omega = Ride.Attitude->GetAngularVelocity();
				R.SpinAboutUpDeg += static_cast<float>(FMath::RadiansToDegrees(Omega.Z) * FrameSeconds);
				R.SpinAboutBodyUpDeg += static_cast<float>(FMath::RadiansToDegrees(Omega | Body.GetAxisZ()) * FrameSeconds);
				if (!bChestMeasured && R.AirSeconds >= 0.15f)
				{
					R.ChestToTail = static_cast<float>((Body.GetAxisX() - Front0) | -Travel0);
					bChestMeasured = true;
				}
				const FAttitudeDebug& Debug = Ride.Attitude->GetLastStepDebug();
				if (bStickHeld)
				{
					R.bAirStickRouted |= Pawn->GetAirRotationInput().Size() > Pawn->AirRotationDeadzone;
					R.bFamilySpinWhileHeld |= Debug.Family == RiderAxes::ERotationFamily::Spin && !Debug.ControlTorqueNm.IsZero();
					R.bFamilyRollWhileHeld |= Debug.Family == RiderAxes::ERotationFamily::Roll && !Debug.ControlTorqueNm.IsZero();
					const bool bLetGo = (Script.Policy == EAirStickPolicy::UntilSpun && FMath::Abs(R.SpinAboutBodyUpDeg) >= Script.SpinTargetDeg)
						|| (Script.Policy == EAirStickPolicy::ForSeconds && R.AirSeconds >= Script.HoldSeconds - 0.5f * FrameSeconds);
					if (bLetGo)
					{
						Ride.Stick(0.0f, 0.0f);
						Ride.Rotate(false);
						bStickHeld = false;
						R.StickReleasedAt = R.AirSeconds;
					}
				}
				else if (R.StickReleasedAt >= 0.0f)
				{
					R.bAssistActedAfterRelease |= Debug.bAssistActive;
				}
				if (Ride.Board->Velocity.Z < 0.0f)
				{
					Pawn->SetLoadHeld(true); // coming down: crouch for the landing (only changes the absorb distance)
				}
			}
			else if (bWasAir)
			{
				R.bLanded = true;
				R.Verdict = Ride.Board->GetLastLandingVerdict();
				R.LandingInputs = Ride.Board->GetLastLandingInputs();
				break;
			}
		}
		R.Inversions = Tally.Count;
		R.FirstInvertedAt = Tally.FirstInvertedAt;
		R.BackUprightAt = Tally.BackUprightAt;
		Pawn->SetLoadHeld(false);
		Ride.Stick(0.0f, 0.0f);
		Ride.Frames(1.0f);
		R.bCrashedAfter = Ride.Board->IsCrashing();
		// Back on the water the stick is the board again.
		Ride.Stick(0.5f, 0.0f);
		Ride.Frame();
		R.BoardEdgeAfterLanding = Ride.Board->GetEdgeInput();
		Ride.Stick(0.0f, 0.0f);
		return R;
	}

	FString Describe(const FPlayerJumpResult& R)
	{
		const UEnum* GradeEnum = StaticEnum<ELandingGrade>();
		const UEnum* CauseEnum = StaticEnum<ELandingCause>();
		return FString::Printf(TEXT("back sign %+.0f, pre-wind %.2f, air %.2f s, %d inversion(s) (first %.2f s, upright again %.2f s), stick let go at %.2f s, spin about up %.0f deg (about body up %.0f), max tilt %.0f deg, chest to tail %.3f, assist after let go %d; landed %s %s: tilt %.1f deg, yaw %.1f deg, %.2f g"),
			R.BackSign, R.PreWindAtRelease, R.AirSeconds, R.Inversions, R.FirstInvertedAt, R.BackUprightAt, R.StickReleasedAt, R.SpinAboutUpDeg, R.SpinAboutBodyUpDeg, R.MaxTiltDeg, R.ChestToTail, R.bAssistActedAfterRelease,
			GradeEnum ? *GradeEnum->GetNameStringByValue(static_cast<int64>(R.Verdict.Grade)) : TEXT("?"),
			CauseEnum ? *CauseEnum->GetNameStringByValue(static_cast<int64>(R.Verdict.Cause)) : TEXT("?"),
			R.LandingInputs.TiltDeg, R.LandingInputs.YawOffVelocityDeg, R.LandingInputs.LandingG);
	}

	/** Airborne attitude inputs far above the water (assist window closed), upright, with a line force in N straight up. */
	FAttitudeInputs BareAir(float LineForceN)
	{
		FAttitudeInputs In;
		In.bAirborne = true;
		In.SlavedBodyQuat = FQuat::Identity;
		In.SlavedBoardQuat = URiderAttitudeComponent::MakeCanonicalStrapOffset(-1.0f);
		In.VelocityCmS = FVector(0.0, 800.0, 0.0);
		In.LineForceUU = FVector(0.0, 0.0, LineForceN) * KiteUnits::UnrealForcePerN;
		In.bLinesTaut = true;
		In.HeightAboveWaterCm = 1.0e6f;
		return In;
	}
}

// The tests sit inside the namespace rather than under a using-directive, which would leak into the
// next file of a unity build.
namespace TrickInputTest
{

// Holding jump on the water, the stick winds up the pre-wind and the board keeps the carve and weight
// shift it had when the load started; no stick, no pre-wind; letting go pops with the pre-wind.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputPreWindLatchesBoard, "KiteSurf.Input.PreWindLatchesBoard", TrickInputTest::Flags)

bool FKiteSurfInputPreWindLatchesBoard::RunTest(const FString& Parameters)
{
	{
		FInputRide Ride;
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		AKiteRiderPawn* Pawn = Ride.Pawn;
		TestTrue(TEXT("The pre-wind latches the board by default"), Pawn->bPreWindLatchesBoardInput);
		Ride.SimulateUntil(4.0f);

		// On the water the stick is the board, as always.
		Ride.Stick(0.4f, 0.2f);
		TestEqual(TEXT("On the water stick X carves"), Ride.Board->GetEdgeInput(), 0.4f);
		TestEqual(TEXT("On the water stick Y shifts the weight"), Ride.Board->GetWeightShift(), 0.2f);
		TestTrue(TEXT("On the water there is no pre-wind stick"), Pawn->GetPreWindStick().IsZero());
		Ride.Frames(0.1f);

		// Load, then the stick: the pre-wind takes it and the board stays where it was.
		Ride.JumpButton(true);
		TestTrue(TEXT("Jump held loads the board"), Ride.Board->IsLoadHeld());
		const float BackSign = Pawn->GetScreenBackSign();
		Ride.Rotate(true); // IA_Rotate (batch A): held, so the stick reaches the pre-wind
		Ride.Stick(BackSign, 0.0f); // towards the rider's back on screen: a back roll
		TestEqual(TEXT("Loading: the stick is the pre-wind (+1 back roll)"), static_cast<float>(Pawn->GetPreWindStick().X), 1.0f);
		TestEqual(TEXT("Loading: the carve stays at its value when the load started"), Ride.Board->GetEdgeInput(), 0.4f);
		TestEqual(TEXT("Loading: the weight shift stays at its value when the load started"), Ride.Board->GetWeightShift(), 0.2f);

		float FullAt = -1.0f;
		bool bLatched = true;
		for (float T = FrameSeconds; T < 0.7f; T += FrameSeconds)
		{
			Ride.Frame();
			bLatched &= Ride.Board->GetEdgeInput() == 0.4f && Ride.Board->GetWeightShift() == 0.2f;
			if (FullAt < 0.0f && Pawn->GetPreWindAmount() >= 1.0f)
			{
				FullAt = T;
			}
		}
		TestTrue(TEXT("The carve and weight shift did not move through the load"), bLatched);
		TestTrue(FString::Printf(TEXT("The pre-wind built to full in 0.5 s +- 0.05 (%.3f s)"), FullAt), FullAt > 0.45f && FullAt < 0.55f);
		TestFalse(TEXT("Still on the water while loading"), Ride.IsAirborne());

		// Letting go pops, and the take-off hands the pre-wind to the attitude: a roll is committed.
		Ride.JumpButton(false);
		TestTrue(TEXT("Letting go of jump pops"), Ride.IsAirborne());
		Ride.Frame();
		TestTrue(TEXT("The attitude is live after the pop"), Ride.Attitude->IsSimulating());
		TestTrue(TEXT("The take-off used the pre-wind: the jump left the water rotating"), Ride.Attitude->TookOffRotating());
		TestTrue(TEXT("A rotation axis is committed"), !Ride.Attitude->GetCommittedAxisBody().IsZero());
		TestEqual(TEXT("It is a roll"), Ride.Attitude->GetLastStepDebug().Family, RiderAxes::ERotationFamily::Roll);
		TestTrue(TEXT("In the air the held stick is the rotation stick"), Pawn->GetAirRotationInput().X > 0.99f);
		TestTrue(TEXT("In the air there is no pre-wind stick"), Pawn->GetPreWindStick().IsZero());
		Ride.Stick(0.0f, 0.0f);
		Ride.Rotate(false);
	}

	// No stick while loading: no pre-wind.
	{
		FInputRide Ride;
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		Ride.SimulateUntil(4.0f);
		Ride.JumpButton(true);
		Ride.Stick(0.0f, 0.0f);
		Ride.Frames(1.0f);
		TestEqual(TEXT("No stick while loading: no pre-wind"), Ride.Pawn->GetPreWindAmount(), 0.0f);
		Ride.JumpButton(false);
		Ride.Frame();
		TestTrue(TEXT("Popped"), Ride.IsAirborne());
		TestFalse(TEXT("No pre-wind: the jump left the water with no rotation"), Ride.Attitude->TookOffRotating());
	}

	// Latch off: the stick carves while loading as well as winding up.
	{
		FInputRide Ride;
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		Ride.Pawn->bPreWindLatchesBoardInput = false;
		Ride.SimulateUntil(4.0f);
		Ride.JumpButton(true);
		Ride.Rotate(true); // IA_Rotate (batch A): held, so the stick reaches the pre-wind
		Ride.Stick(0.7f, 0.0f);
		TestEqual(TEXT("Latch off: the stick carves while loading"), Ride.Board->GetEdgeInput(), 0.7f);
		TestEqual(TEXT("Latch off: the pre-wind still takes the stick"), static_cast<float>(FMath::Abs(Ride.Pawn->GetPreWindStick().X)), 0.7f);
		Ride.Stick(0.0f, 0.0f);
		Ride.JumpButton(false);
		Ride.Rotate(false); // after the pop: releasing it while still loading would unlatch the board a step early
	}

	// The scripted setters take the controls back from the player's stick.
	{
		FInputRide Ride;
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		Ride.Stick(0.3f, 0.0f);
		TestTrue(TEXT("The handlers give the player the rider's controls"), Ride.Pawn->IsPlayerRiderInputActive());
		Ride.Pawn->SetPreWind(FVector2D(1.0f, 0.0f));
		TestFalse(TEXT("A scripted setter takes them back"), Ride.Pawn->IsPlayerRiderInputActive());
		Ride.Frame();
		TestEqual(TEXT("Tick does not overwrite the scripted pre-wind"), static_cast<float>(Ride.Pawn->GetPreWindStick().X), 1.0f);
	}
	return true;
}

// Through the player's handlers: weight on the tail, load, the stick towards the rider's back for the
// pre-wind, pop, and the same stick held in the air until the rider is over: one back roll.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputAirStickRolls, "KiteSurf.Input.AirStickRolls", TrickInputTest::Flags)

bool FKiteSurfInputAirStickRolls::RunTest(const FString& Parameters)
{
	FPlayerJumpScript Script;
	Script.PreWind = FVector2D(1.0f, 0.0f);
	Script.AirStick = FVector2D(1.0f, 0.0f);
	// Held on through the pop and let go once the roll is going. On this kite the pre-wind alone goes
	// over; holding the stick on for 0.3 s or more adds enough to over-rotate into a crash 2.5 s later.
	Script.Policy = EAirStickPolicy::ForSeconds;
	Script.HoldSeconds = 0.15f;
	const FPlayerJumpResult R = RunPlayerJump(Script);
	AddInfo(FString::Printf(TEXT("Player back roll: %s"), *Describe(R)));
	TestTrue(TEXT("Ride fixture created"), R.bValid);
	TestTrue(TEXT("The rider took off and landed"), R.bTookOff && R.bLanded);
	TestFalse(TEXT("Nothing went NaN"), R.bNaN);
	TestTrue(TEXT("The board kept its carve and tail weight through the load"), R.bBoardLatchedThroughLoad);
	TestTrue(FString::Printf(TEXT("The stick wound the pre-wind up fully by the pop (%.2f)"), R.PreWindAtRelease), R.PreWindAtRelease > 0.99f);
	TestTrue(TEXT("The jump left the water rotating"), R.bTookOffRotating);
	TestTrue(TEXT("The held stick reached the attitude in the air"), R.bAirStickRouted);
	TestTrue(TEXT("With a pre-wound roll, stick X drives the roll (family Roll), not a spin"), R.bFamilyRollWhileHeld && !R.bFamilySpinWhileHeld);
	if (!TestTrue(FString::Printf(TEXT("Precondition: at least 3 s in the air (%.2f s)"), R.AirSeconds), R.AirSeconds >= 3.0f))
	{
		return false;
	}
	TestEqual(TEXT("Exactly one inversion"), R.Inversions, 1);
	TestTrue(TEXT("Upright again before the water"), R.BackUprightAt > R.FirstInvertedAt && R.BackUprightAt < R.AirSeconds);
	TestTrue(TEXT("A back roll turns the chest towards the tail first"), R.ChestToTail > 0.0f);
	TestTrue(TEXT("Letting go of the stick lets the landing assist work"), R.bAssistActedAfterRelease);
	TestEqual(TEXT("Back on the water the stick carves again"), R.BoardEdgeAfterLanding, 0.5f);
	return true;
}

// Stick X alone in the air, on a jump with no pre-wind, spins the rider flat about up: the spin family
// (AirStickTiltWithoutPreWindDeg 0). With a pre-wind the same stick keeps the roll.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputAirSpinReachable, "KiteSurf.Input.AirSpinReachable", TrickInputTest::Flags)

bool FKiteSurfInputAirSpinReachable::RunTest(const FString& Parameters)
{
	// The mapping (batch A): AirStickTiltWithoutPreWindDeg is unified with the roll's
	// DefaultRollAxisTiltDeg, so X alone is now a roll whether or not the jump left the water
	// rotating; RollAxisTiltRangeDeg and SpinAxisTiltMaxDeg widen so the up-diagonal still tilts
	// into the spin family and the down-diagonal into a more inverted roll.
	{
		URiderAttitudeComponent* A = NewObject<URiderAttitudeComponent>();
		TestEqual(TEXT("Batch A: the air stick's tilt with no pre-wind matches the roll's (unified)"), A->AirStickTiltWithoutPreWindDeg, A->DefaultRollAxisTiltDeg);
		const RiderAxes::FRotationAxisChoice Roll = RiderAxes::ChooseAxisBody(FVector2D(1.0f, 0.0f), 1.0f, A->AirStickTiltWithoutPreWindDeg,
			A->RollAxisTiltRangeDeg, A->FlipSectorDeg, A->SpinAxisTiltMaxDeg, A->FlipSectorHysteresisDeg, false);
		TestEqual(TEXT("X alone, with or without a pre-wind, is a roll"), Roll.Family, RiderAxes::ERotationFamily::Roll);
		TestEqual(FString::Printf(TEXT("X alone tilts to the roll's default (%.0f deg)"), Roll.TiltDeg), Roll.TiltDeg, A->DefaultRollAxisTiltDeg);
		const RiderAxes::FRotationAxisChoice Spin = RiderAxes::ChooseAxisBody(FVector2D(1.0f, 1.0f), 1.0f, A->AirStickTiltWithoutPreWindDeg,
			A->RollAxisTiltRangeDeg, A->FlipSectorDeg, A->SpinAxisTiltMaxDeg, A->FlipSectorHysteresisDeg, false);
		TestEqual(TEXT("The up-diagonal tilts into the spin family"), Spin.Family, RiderAxes::ERotationFamily::Spin);
		const RiderAxes::FRotationAxisChoice DownRoll = RiderAxes::ChooseAxisBody(FVector2D(1.0f, -1.0f), 1.0f, A->AirStickTiltWithoutPreWindDeg,
			A->RollAxisTiltRangeDeg, A->FlipSectorDeg, A->SpinAxisTiltMaxDeg, A->FlipSectorHysteresisDeg, false);
		TestEqual(TEXT("The down-diagonal is still a roll, more inverted"), DownRoll.Family, RiderAxes::ERotationFamily::Roll);
		TestTrue(FString::Printf(TEXT("The down-diagonal tilts past the roll's default (%.0f deg)"), DownRoll.TiltDeg), DownRoll.TiltDeg > Roll.TiltDeg);
	}

	// The bare attitude under 800 N straight up: no pre-wind, the up-diagonal stick for 2.5 s (batch
	// A: X alone is now a roll, so reaching the spin family needs the diagonal).
	{
		URiderAttitudeComponent* A = NewObject<URiderAttitudeComponent>();
		FAttitudeInputs In = BareAir(800.0f);
		In.RotationStick = FVector2D(1.0f, 1.0f).GetSafeNormal();
		In.bRotationInput = true;
		const float Dt = 1.0f / 240.0f;
		double Turned = 0.0;
		double MinUpZ = 1.0;
		bool bSpinFamily = true;
		for (int32 I = 0; I < FMath::RoundToInt(2.5f / Dt); ++I)
		{
			A->Step(Dt, In);
			Turned += FMath::RadiansToDegrees(A->GetAngularVelocity().Z) * Dt;
			MinUpZ = FMath::Min(MinUpZ, A->GetBodyQuat().GetAxisZ().Z);
			bSpinFamily &= A->GetLastStepDebug().Family == RiderAxes::ERotationFamily::Spin;
		}
		AddInfo(FString::Printf(TEXT("Bare attitude, up-diagonal stick for 2.5 s at 800 N: turned %.0f deg about up, lowest up.z %.2f"), Turned, MinUpZ));
		TestTrue(TEXT("Bare: the family is Spin all the way"), bSpinFamily);
		TestTrue(FString::Printf(TEXT("Bare: at least 180 deg about up in 2.5 s (%.0f)"), Turned), FMath::Abs(Turned) >= 180.0);
		TestTrue(TEXT("Bare: a backside spin (the back roll's sense, -sigma about up)"), Turned < 0.0);
		TestTrue(FString::Printf(TEXT("Bare: never past 60 deg of tilt (lowest up.z %.2f)"), MinUpZ), MinUpZ > 0.5);
	}

	// With a pre-wound back roll the same stick keeps the roll's mapping.
	{
		URiderAttitudeComponent* A = NewObject<URiderAttitudeComponent>();
		FAttitudeInputs In = BareAir(800.0f);
		In.PreWindStick = FVector2D(1.0f, 0.0f);
		In.PreWindAmount = 1.0f;
		In.TakeoffLoad = 1.0f;
		In.RotationStick = FVector2D(1.0f, 0.0f);
		In.bRotationInput = true;
		A->Step(1.0f / 240.0f, In);
		TestTrue(TEXT("Pre-wound: the jump left the water rotating"), A->TookOffRotating());
		TestEqual(TEXT("Pre-wound: stick X is a roll"), A->GetLastStepDebug().Family, RiderAxes::ERotationFamily::Roll);
	}

	// On the timed jump through the player's handlers: no pre-wind, the modifier and the up-diagonal
	// stick held in the air (batch A: reaching the spin family needs the diagonal now) until the
	// rider has turned 200 deg about their own up, then let go for the landing.
	FPlayerJumpScript Script;
	Script.AirStick = FVector2D(1.0f, 1.0f);
	Script.Policy = EAirStickPolicy::UntilSpun;
	Script.SpinTargetDeg = 200.0f;
	const FPlayerJumpResult R = RunPlayerJump(Script);
	AddInfo(FString::Printf(TEXT("Player air spin: %s"), *Describe(R)));
	TestTrue(TEXT("Ride fixture created"), R.bValid);
	TestTrue(TEXT("The rider took off and landed"), R.bTookOff && R.bLanded);
	TestFalse(TEXT("Nothing went NaN"), R.bNaN);
	TestFalse(TEXT("No pre-wind: the jump left the water with no rotation"), R.bTookOffRotating);
	TestTrue(TEXT("The held stick reached the attitude in the air"), R.bAirStickRouted);
	TestTrue(TEXT("The up-diagonal asked for the spin family"), R.bFamilySpinWhileHeld && !R.bFamilyRollWhileHeld);
	TestTrue(FString::Printf(TEXT("At least 180 deg about the body's up (%.0f)"), R.SpinAboutBodyUpDeg), FMath::Abs(R.SpinAboutBodyUpDeg) >= 180.0f);
	TestTrue(FString::Printf(TEXT("At least 180 deg about world up (%.0f)"), R.SpinAboutUpDeg), FMath::Abs(R.SpinAboutUpDeg) >= 180.0f);
	TestEqual(TEXT("No inversion"), R.Inversions, 0);
	TestTrue(FString::Printf(TEXT("Never tilted past 90 deg (%.0f)"), R.MaxTiltDeg), R.MaxTiltDeg < 90.0f);
	TestTrue(TEXT("Letting go of the stick lets the landing assist work"), R.bAssistActedAfterRelease);
	return true;
}

// Jump pressed and held in the air is the tuck; let go, the rider stretches out. The press that loads
// and pops on the water is not a tuck.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputTuckWhileJumpHeld, "KiteSurf.Input.TuckWhileJumpHeld", TrickInputTest::Flags)

bool FKiteSurfInputTuckWhileJumpHeld::RunTest(const FString& Parameters)
{
	FInputRide Ride;
	if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	// A back-roll pre-wind on the timed jump: jump held to load and let go to pop.
	const FSendResult Send = SendAndPop(Ride, FVector2D(1.0f, 0.0f));
	TestTrue(FString::Printf(TEXT("The pre-wind wound up (%.2f)"), Send.PreWindAtRelease), Send.PreWindAtRelease > 0.99f);
	if (!TestTrue(TEXT("Popped"), Ride.IsAirborne()))
	{
		return false;
	}
	TestEqual(TEXT("The press that loaded and popped is not a tuck"), Pawn->GetTuck(), 0.0f);
	Ride.Frames(0.2f);
	TestEqual(TEXT("In the air with jump up: no tuck"), Pawn->GetTuck(), 0.0f);
	TestTrue(TEXT("Stretched out"), Ride.Attitude->GetTuckAmount() < 0.05f);
	const FVector Ib0 = Ride.Attitude->GetBodyInertiaKgM2();
	const FVector Axis = Ride.Attitude->GetCommittedAxisBody();
	auto RateOnAxis = [&Ride, &Axis]() { return Ride.Attitude->GetBodyQuat().UnrotateVector(Ride.Attitude->GetAngularVelocity()) | Axis; };
	const double Rate0 = RateOnAxis();

	Ride.JumpButton(true);
	TestEqual(TEXT("Jump pressed in the air: tuck"), Pawn->GetTuck(), 1.0f);
	TestTrue(TEXT("Still in the air"), Ride.IsAirborne());
	Ride.Frames(0.3f);
	const float Tucked = Ride.Attitude->GetTuckAmount();
	const FVector Ib1 = Ride.Attitude->GetBodyInertiaKgM2();
	const double Rate1 = RateOnAxis();
	AddInfo(FString::Printf(TEXT("Tuck %.2f after 0.3 s held; inertia (%.1f, %.1f, %.1f) -> (%.1f, %.1f, %.1f) kg m^2; rate on the roll axis %.2f -> %.2f rad/s"),
		Tucked, Ib0.X, Ib0.Y, Ib0.Z, Ib1.X, Ib1.Y, Ib1.Z, Rate0, Rate1));
	TestTrue(TEXT("Still in the air after 0.3 s"), Ride.IsAirborne());
	TestTrue(FString::Printf(TEXT("Held 0.3 s: the attitude is tucked (%.2f > 0.8)"), Tucked), Tucked > 0.8f);
	TestTrue(TEXT("Tucked: less inertia about the roll"), Ib1.X < Ib0.X - 3.0);

	// The same jump without the tuck, measured at the same moment: the tuck spins the rider faster.
	{
		FInputRide Plain;
		if (TestTrue(TEXT("Second ride fixture created"), Plain.IsValid()))
		{
			SendAndPop(Plain, FVector2D(1.0f, 0.0f));
			Plain.Frames(0.2f + 0.3f);
			const FVector PlainAxis = Plain.Attitude->GetCommittedAxisBody();
			const double PlainRate = Plain.Attitude->GetBodyQuat().UnrotateVector(Plain.Attitude->GetAngularVelocity()) | PlainAxis;
			AddInfo(FString::Printf(TEXT("Rate on the roll axis 0.5 s after the pop: %.2f rad/s tucked, %.2f stretched"), Rate1, PlainRate));
			TestTrue(FString::Printf(TEXT("Tucked: the roll is at least 30%% faster than stretched (%.2f against %.2f rad/s)"), Rate1, PlainRate), PlainRate > 0.5 && Rate1 > 1.3 * PlainRate);
		}
	}

	Ride.JumpButton(false);
	TestEqual(TEXT("Jump let go in the air: no tuck"), Pawn->GetTuck(), 0.0f);
	TestTrue(TEXT("Letting go in the air does not end the jump"), Ride.IsAirborne());
	Ride.Frames(0.5f);
	TestTrue(TEXT("Still in the air 0.5 s later"), Ride.IsAirborne());
	TestTrue(FString::Printf(TEXT("Let go 0.5 s: stretched out again (%.2f < 0.1)"), Ride.Attitude->GetTuckAmount()), Ride.Attitude->GetTuckAmount() < 0.1f);
	return true;
}

// Stick X towards the side of the screen the rider's back is on is a back roll, on both tacks: the
// screen side is latched as the load starts, and the take-off turns the chest towards the tail first.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputBackRollSideFollowsScreen, "KiteSurf.Input.BackRollSideFollowsScreen", TrickInputTest::Flags)

bool FKiteSurfInputBackRollSideFollowsScreen::RunTest(const FString& Parameters)
{
	// The pure rule: the back on the right of the screen is +1.
	TestEqual(TEXT("Facing +X, camera right -X: the back is on the right (+1)"), AKiteRiderPawn::ComputeScreenBackSign(FVector(1, 0, 0), FVector(-1, 0, 0), -1.0f), 1.0f);
	TestEqual(TEXT("Facing +X, camera right +X: the back is on the left (-1)"), AKiteRiderPawn::ComputeScreenBackSign(FVector(1, 0, 0), FVector(1, 0, 0), 1.0f), -1.0f);
	TestEqual(TEXT("Facing along the view: the fallback"), AKiteRiderPawn::ComputeScreenBackSign(FVector(1, 0, 0), FVector(0, 1, 0), -1.0f), -1.0f);

	float SignByTack[2] = { 0.0f, 0.0f };
	for (int32 K = 0; K < 2; ++K)
	{
		const float Tack = K == 0 ? 1.0f : -1.0f;
		FInputRide Ride(30.0f, Tack);
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		AKiteRiderPawn* Pawn = Ride.Pawn;
		Ride.SimulateUntil(4.0f);
		Ride.JumpButton(true);
		// Where the back is on the screen, from the camera the player sees and the body's front.
		const FVector Front = FRotator(0.0f, Pawn->GetRiderBodyYawDeg(), 0.0f).Vector();
		const FVector Back2D = FVector(-Front.X, -Front.Y, 0.0f).GetSafeNormal();
		const FVector CamRight = Ride.Camera->GetRightVector();
		const double BackOnRight = Back2D | FVector(CamRight.X, CamRight.Y, 0.0f).GetSafeNormal();
		const float Expected = BackOnRight > 0.0 ? 1.0f : -1.0f;
		SignByTack[K] = Pawn->GetScreenBackSign();
		AddInfo(FString::Printf(TEXT("Tack %+.0f: board yaw %.0f, body yaw %.0f, camera yaw %.0f, back . camera right %.2f, back sign %+.0f"),
			Tack, Pawn->GetActorRotation().Yaw, Pawn->GetRiderBodyYawDeg(), Ride.Camera->GetComponentRotation().Yaw, BackOnRight, SignByTack[K]));
		TestTrue(FString::Printf(TEXT("Tack %+.0f: the back is clearly to one side of the screen (%.2f)"), Tack, BackOnRight), FMath::Abs(BackOnRight) > 0.5);
		TestEqual(FString::Printf(TEXT("Tack %+.0f: the latched side is the screen side of the rider's back"), Tack), SignByTack[K], Expected);

		Ride.Rotate(true); // IA_Rotate (batch A): held, so the stick reaches the pre-wind
		Ride.Stick(-Expected, 0.0f);
		TestEqual(FString::Printf(TEXT("Tack %+.0f: stick away from the back is a front roll"), Tack), static_cast<float>(Pawn->GetPreWindStick().X), -1.0f);
		Ride.Stick(Expected, 0.0f);
		TestEqual(FString::Printf(TEXT("Tack %+.0f: stick towards the back is a back roll"), Tack), static_cast<float>(Pawn->GetPreWindStick().X), 1.0f);
		Ride.Frames(0.55f);
		Ride.Stick(0.0f, 0.0f);
		Ride.JumpButton(false);
		Ride.Rotate(false); // after the pop: releasing it while still loading would unlatch the board a step early
		if (!TestTrue(FString::Printf(TEXT("Tack %+.0f: popped"), Tack), Ride.IsAirborne()))
		{
			continue;
		}
		Ride.Frame();
		const FVector Front0 = Ride.Attitude->GetBodyQuat().GetAxisX();
		const FVector Travel0 = FVector(Ride.Board->Velocity.X, Ride.Board->Velocity.Y, 0.0f).GetSafeNormal();
		Ride.Frames(0.15f);
		const double ChestToTail = (Ride.Attitude->GetBodyQuat().GetAxisX() - Front0) | -Travel0;
		AddInfo(FString::Printf(TEXT("Tack %+.0f: chest to tail %.3f in the first 0.15 s, still airborne %d"), Tack, ChestToTail, Ride.IsAirborne()));
		TestTrue(FString::Printf(TEXT("Tack %+.0f: the chest turns towards the tail first (%.3f)"), Tack, ChestToTail), ChestToTail > 0.0);
		TestEqual(FString::Printf(TEXT("Tack %+.0f: the screen side holds through the air"), Tack), Pawn->GetScreenBackSign(), SignByTack[K]);
	}
	TestTrue(FString::Printf(TEXT("The back is on opposite sides of the screen on the two tacks (%+.0f, %+.0f)"), SignByTack[0], SignByTack[1]), SignByTack[0] == -SignByTack[1]);
	return true;
}

// The controls legend names the new controls with a key and a button each.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputLegendShowsAirControls, "KiteSurf.Input.LegendShowsAirControls", TrickInputTest::Flags)

bool FKiteSurfInputLegendShowsAirControls::RunTest(const FString& Parameters)
{
	const TCHAR* Wanted[] = { TEXT("Pre-wind"), TEXT("Air, with Shift / LT"), TEXT("Hold jump in the air: tuck") };
	for (const TCHAR* Action : Wanted)
	{
		const FKiteSurfControlBinding* Found = nullptr;
		for (const FKiteSurfControlBinding& Binding : KiteSurfControlsLegend::GetBindings())
		{
			if (FString(Binding.Action).Contains(Action))
			{
				Found = &Binding;
				break;
			}
		}
		if (TestNotNull(FString::Printf(TEXT("The legend has a row for \"%s\""), Action), Found))
		{
			TestTrue(FString::Printf(TEXT("\"%s\" has a keyboard binding"), Action), FCString::Strlen(Found->Keyboard) > 0);
			TestTrue(FString::Printf(TEXT("\"%s\" has a gamepad binding"), Action), FCString::Strlen(Found->Gamepad) > 0);
		}
	}
	return true;
}

} // namespace TrickInputTest

#endif // WITH_DEV_AUTOMATION_TESTS
