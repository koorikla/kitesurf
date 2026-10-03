#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurf.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfHUD.h"
#include "KiteSurfUnits.h"
#include "RiderRig.h"
#include "WindComponent.h"
#include "Tricks/BoardGrabPoints.h"
#include "Tricks/BoardOffState.h"
#include "Tricks/GrabState.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/LandingEvaluator.h"
#include "Tricks/RiderAttitudeComponent.h"
#include "Tricks/TrickNaming.h"
#include "Tricks/TrickRecognition.h"
#include "Tricks/TrickScoring.h"
#include "Tricks/TrickTrackerComponent.h"
#include "UI/KiteSurfControlsLegend.h"

#if WITH_DEV_AUTOMATION_TESTS

// T2.3 the board-off (docs/tricks/T2.md): the pure board-off state behind the chord of the two grab
// buttons, its catch window and the landing evaluator's verdict, the names, then the pawn on the
// phase 2 timed jump (30 kn, the recommended kite) with both grab buttons held in the air.

// Named, not anonymous, so a unity build cannot merge these with another file's helpers.
namespace TrickBoardOffTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float BoardOffFrameSeconds = 1.0f / 60.0f;

	/** A rider on the water in steady wind along +X, started on a beam reach as the game mode does (TrickGrabTests' fixture). */
	struct FBoardOffRide
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		URiderAttitudeComponent* Attitude = nullptr;
		UTrickTrackerComponent* Tracker = nullptr;
		USpringArmComponent* Boom = nullptr;
		/** The HUD lives in its own world: it only reads the tracker. */
		UWorld* HudWorld = nullptr;
		AKiteSurfHUD* HUD = nullptr;

		explicit FBoardOffRide(float WindKnots = 30.0f)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			HudWorld = UWorld::CreateWorld(EWorldType::Game, false);
			HUD = HudWorld ? HudWorld->SpawnActor<AKiteSurfHUD>() : nullptr;
			if (!Pawn)
			{
				return;
			}
			Kite = Pawn->GetKite();
			Board = Pawn->GetBoardMovement();
			Attitude = Pawn->GetRiderAttitude();
			Tracker = Pawn->GetTrickTracker();
			Boom = Pawn->FindComponentByClass<USpringArmComponent>();
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
			Kite->SetKiteSize(UKiteComponent::RecommendKiteSizeM2(WindKnots));
			if (Boom)
			{
				Boom->bEnableCameraLag = false;
				Boom->bEnableCameraRotationLag = false;
			}
		}

		~FBoardOffRide()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
			if (HudWorld)
			{
				HudWorld->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Attitude && Tracker && HUD && Pawn->GetBoardVisual(); }

		void Frame()
		{
			Pawn->Tick(BoardOffFrameSeconds);
			if (Boom)
			{
				Boom->TickComponent(BoardOffFrameSeconds, LEVELTICK_All, nullptr);
			}
			HUD->UpdateJumpCard(Tracker, BoardOffFrameSeconds);
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

		bool IsAirborne() const { return Board->GetBoardState() == EBoardState::Airborne; }

		/** TrickGrabTests' timed jump up to the pop: 8 s of riding, the bar hard over, the weight on the tail, the jump held and let go 0.66 s after the bar reaches the kite. */
		void SendAndPop()
		{
			const float SendAt = 8.0f;
			SimulateUntil(SendAt);
			Pawn->SteerKite(-1.0f);
			Board->SetWeightShift(-1.0f);
			Pawn->SetLoadHeld(true);
			const float ReleaseAt = SendAt + FMath::RoundToFloat((0.66f + Kite->GetSteeringDeadTimeSeconds()) * 30.0f) / 30.0f;
			while (!HasReached(ReleaseAt) && !IsAirborne())
			{
				Frame();
			}
			Pawn->SheetKite(1.0f);
			Pawn->ReleaseLoadAndPop();
			Board->SetWeightShift(0.0f);
			Pawn->SteerKite(0.0f);
		}
	};

	/** The nose's side of the drawn rider (+1 right): the drawn board's +X against the torso's right, as the pawn works it out. Holds for a plain board-off and a superman, whose held boards keep the nose on that side. */
	float NoseSideOf(const AKiteRiderPawn* Pawn)
	{
		const FTransform BoardTransform = Pawn->GetBoardVisual()->GetComponentTransform();
		return FVector::DotProduct(BoardTransform.GetUnitAxis(EAxis::X), Pawn->GetRiderRigPose().Torso.GetAxisY()) >= 0.0f ? 1.0f : -1.0f;
	}

	/** The rig side (0 left, 1 right) of a hand: the front hand is on the nose's side. */
	int32 RigSideOf(ETrickHand Hand, float NoseSide)
	{
		return (Hand == ETrickHand::Front) == (NoseSide > 0.0f) ? 1 : 0;
	}

	/** How far each ankle is from its strap on the drawn board (cm): the worse of the two. */
	float WorstStrapError(const AKiteRiderPawn* Pawn)
	{
		const FTransform BoardTransform = Pawn->GetBoardVisual()->GetComponentTransform();
		const float NoseSide = NoseSideOf(Pawn);
		const int32 FrontSide = RigSideOf(ETrickHand::Front, NoseSide);
		const FRiderRigPose& Pose = Pawn->GetRiderRigPose();
		const float Front = static_cast<float>(FVector::Dist(Pose.Legs[FrontSide].End, BoardTransform.TransformPosition(BoardGrabPoints::ToBoardLocal(BoardGrabPoints::FrontStrap, NoseSide))));
		const float Back = static_cast<float>(FVector::Dist(Pose.Legs[1 - FrontSide].End, BoardTransform.TransformPosition(BoardGrabPoints::ToBoardLocal(BoardGrabPoints::BackStrap, NoseSide))));
		return FMath::Max(Front, Back);
	}

	/**
	 * Steps a grab state at Hz in the air: both buttons pressed together and held HeldSeconds with the
	 * stick at Stick, then let go of (both, or only the front one) for AfterSeconds. Returns what a
	 * touchdown at the end would find.
	 */
	EBoardCatchState ChordThenLetGo(FGrabState& State, float HeldSeconds, float AfterSeconds, float Hz, const FVector2D& Stick = FVector2D::ZeroVector, bool bLetGoFrontOnly = false)
	{
		const float Dt = 1.0f / Hz;
		FGrabStateInput In;
		In.bAirborne = true;
		State.Step(In, Dt); // in the air, nothing held
		In.bFront = true;
		In.bBack = true;
		In.ZoneStick = Stick;
		for (int32 Step = 0; Step < FMath::RoundToInt(HeldSeconds * Hz); ++Step)
		{
			State.Step(In, Dt);
		}
		In.bFront = false;
		In.bBack = bLetGoFrontOnly;
		In.ZoneStick = FVector2D::ZeroVector;
		for (int32 Step = 0; Step < FMath::RoundToInt(AfterSeconds * Hz); ++Step)
		{
			State.Step(In, Dt);
		}
		return State.GetBoardOff().GetCatchAtTouchdown();
	}

	/** A landed record for the pure naming and scoring checks: a 5 m straight air, landed clean with the kite high. */
	FJumpRecord MakeBoardOffRecord(ETrickBoardOff BoardOff, float Seconds = 0.8f)
	{
		FJumpRecord Record;
		Record.Outcome = EJumpOutcome::Landed;
		Record.ApexHeightCm = 500.0f;
		Record.LandingG = 2.0f;
		Record.KiteElevationAtLandingDeg = 60.0f;
		Record.BoardOff = BoardOff;
		Record.BoardOffSeconds = Seconds;
		return Record;
	}

	FTrickLoop MakeLoop(ETrickLoopKind Kind)
	{
		FTrickLoop Loop;
		Loop.Kind = Kind;
		return Loop;
	}

	/** One board-off ride's measurements. */
	struct FBoardOffRun
	{
		bool bTookOff = false;
		bool bLanded = false;
		bool bLetGo = false;
		float Air = 0.0f;
		/** Board simulation time of the release and of the touchdown (s). */
		float LetGoAt = -1.0f;
		float TouchdownAt = -1.0f;
		int32 HeldFrames = 0;
		/** While held: the worst hand off its grip on the drawn board (cm), and the nearest the drawn board's centre came to the strapped board's (cm). */
		float WorstHandCm = 0.0f;
		float MinBoardOffStrapsCm = TNumericLimits<float>::Max();
		float MinAnkleOffStrapCm = TNumericLimits<float>::Max();
		float MaxTuck = 0.0f;
		/** After the catch, in the air: the worst ankle off its strap (cm) and drawn board off the attitude's board (deg). */
		int32 CaughtFrames = 0;
		float WorstFeetAfterCatchCm = 0.0f;
		float WorstBoardAfterCatchDeg = 0.0f;
		FLandingVerdict Verdict;
		FJumpRecord Record;
		FString Card;
		FString Ticker;
		bool CrashingHalfSecondLater = false;
	};

	/**
	 * The timed jump with both grab buttons pressed through the player's handlers PressAt seconds
	 * after the take-off, the stick at Stick, and let go when coming down with the attitude's time to
	 * contact under LetGoTtc (never, when LetGoTtc is 0). Measures the drawn board, hands and feet.
	 */
	FBoardOffRun RunBoardOff(FBoardOffRide& Ride, float PressAt, float LetGoTtc, ETrickBoardOff ExpectVariant, const FVector2D& Stick = FVector2D::ZeroVector)
	{
		FBoardOffRun Run;
		AKiteRiderPawn* Pawn = Ride.Pawn;
		Ride.SendAndPop();
		bool bPressed = false;
		bool bCrashChecked = false;
		const float StartAt = Pawn->GetSimTimeSeconds();
		while (!Ride.HasReached(StartAt + 15.0f))
		{
			const float Ttc = Ride.Attitude->GetLastStepDebug().TimeToContactSeconds;
			if (Run.bTookOff && !bPressed && Run.Air >= PressAt)
			{
				Pawn->SetTrickInput(false, false, false, Stick);
				Pawn->OnGrabFrontPressed(FInputActionValue(true));
				Pawn->OnGrabBackPressed(FInputActionValue(true));
				bPressed = true;
			}
			if (bPressed && !Run.bLetGo && LetGoTtc > 0.0f && Run.Air >= 1.0f && Ride.Board->Velocity.Z < 0.0f && Ttc < LetGoTtc)
			{
				Pawn->OnGrabFrontReleased(FInputActionValue(false));
				Pawn->OnGrabBackReleased(FInputActionValue(false));
				Pawn->SetTrickInput(false, false, false);
				Run.bLetGo = true;
				Run.LetGoAt = Pawn->GetSimTimeSeconds();
			}
			Ride.Frame();
			if (Run.bLanded)
			{
				if (!bCrashChecked && Pawn->GetSimTimeSeconds() >= Run.TouchdownAt + 0.5f)
				{
					Run.CrashingHalfSecondLater = Ride.Board->IsCrashing();
					bCrashChecked = true;
					break;
				}
				continue;
			}
			if (Ride.IsAirborne())
			{
				Run.bTookOff = true;
				Run.Air += BoardOffFrameSeconds;
				if (Ride.Board->Velocity.Z < 0.0f && Run.Air > 1.0f)
				{
					Pawn->SetLoadHeld(true); // coming down: crouch for the landing
				}
				const FBoardOffState& BoardOff = Pawn->GetGrabState().GetBoardOff();
				Run.MaxTuck = FMath::Max(Run.MaxTuck, Ride.Attitude->GetTuckAmount());
				const FTransform Drawn = Pawn->GetBoardVisual()->GetComponentTransform();
				if (BoardOff.IsHeld() && BoardOff.GetPrevOffWeight() >= 1.0f)
				{
					++Run.HeldFrames;
					// The nose's side from the strapped board (the attitude's), which the held board may have turned away from.
					const float NoseSide = FVector::DotProduct(Ride.Attitude->GetRenderBoardQuat(1.0f).GetAxisX(), Pawn->GetRiderRigPose().Torso.GetAxisY()) >= 0.0f ? 1.0f : -1.0f;
					const FBoardOffPose Pose = BoardOffPose::Evaluate(ExpectVariant, BoardOff.GetTicTacDeg(), BoardOff.GetPassPhase(), NoseSide);
					const FBoardOffPose PrevPose = BoardOffPose::Evaluate(ExpectVariant, BoardOff.GetPrevTicTacDeg(), BoardOff.GetPrevPassPhase(), NoseSide);
					for (int32 HandIndex = 0; HandIndex < 2; ++HandIndex)
					{
						// A hand on the board at both ends of the step (the pawn draws between them).
						if (Pose.HandOnBoard[HandIndex] >= 1.0f && PrevPose.HandOnBoard[HandIndex] >= 1.0f)
						{
							const int32 Side = RigSideOf(static_cast<ETrickHand>(HandIndex), NoseSide);
							const FVector Grip = Drawn.TransformPosition(Pose.GripLocal[HandIndex]);
							Run.WorstHandCm = FMath::Max(Run.WorstHandCm, static_cast<float>(FVector::Dist(Pawn->GetRiderRigPose().Arms[Side].End, Grip)));
						}
					}
					// Where the strapped board would be: the attitude's board at the centre of mass offset.
					const FQuat Body = Ride.Attitude->GetBodyQuat();
					const FVector Strapped = Pawn->GetActorLocation() + Ride.Attitude->GetVisualComOffsetCm(1.0f) - Body.RotateVector(FVector(0.0f, 0.0f, Ride.Attitude->ComAboveBoardCm));
					Run.MinBoardOffStrapsCm = FMath::Min(Run.MinBoardOffStrapsCm, static_cast<float>(FVector::Dist(Drawn.GetLocation(), Strapped)));
					Run.MinAnkleOffStrapCm = FMath::Min(Run.MinAnkleOffStrapCm, WorstStrapError(Pawn));
				}
				if (Run.bLetGo && !BoardOff.IsBoardOff() && BoardOff.GetPrevOffWeight() <= 0.0f)
				{
					// Caught, still in the air: the board under the feet again, on the attitude's board.
					++Run.CaughtFrames;
					Run.WorstFeetAfterCatchCm = FMath::Max(Run.WorstFeetAfterCatchCm, WorstStrapError(Pawn));
					// The pawn draws between the last two steps at its render alpha: the nearest of the in-betweens.
					float AngleDeg = TNumericLimits<float>::Max();
					for (int32 Sample = 0; Sample <= 20; ++Sample)
					{
						AngleDeg = FMath::Min(AngleDeg, FMath::RadiansToDegrees(static_cast<float>(Drawn.GetRotation().AngularDistance(Ride.Attitude->GetRenderBoardQuat(Sample / 20.0f)))));
					}
					Run.WorstBoardAfterCatchDeg = FMath::Max(Run.WorstBoardAfterCatchDeg, AngleDeg);
				}
				const FString Now = Ride.HUD->GetTrickTickerText();
				if (!Now.IsEmpty())
				{
					Run.Ticker = Now;
				}
			}
			else if (Run.bTookOff)
			{
				Run.bLanded = true;
				Run.TouchdownAt = Pawn->GetSimTimeSeconds();
				Run.Card = Ride.HUD->GetJumpCardText();
				Run.Verdict = Ride.Board->GetLastLandingVerdict();
				Ride.Tracker->GetLastJumpRecord(Run.Record);
				Pawn->OnGrabFrontReleased(FInputActionValue(false));
				Pawn->OnGrabBackReleased(FInputActionValue(false));
				Pawn->SetTrickInput(false, false, false);
			}
		}
		Pawn->SetLoadHeld(false);
		return Run;
	}
}

// Not caught by the touchdown: a crash, cause BoardOff ("Board not caught").
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickBoardOffNotCaughtCrashes, "KiteSurf.Trick.BoardOffNotCaughtCrashes", TrickBoardOffTest::Flags)

bool FKiteSurfTrickBoardOffNotCaughtCrashes::RunTest(const FString& Parameters)
{
	using namespace TrickBoardOffTest;
	const float Hz = 240.0f;

	// Pure: the chord held to the contact, or let go too late.
	{
		FGrabState State;
		TestEqual(TEXT("Chord held to the contact: not caught"), ChordThenLetGo(State, 1.0f, 0.0f, Hz), EBoardCatchState::NotCaught);
		TestTrue(TEXT("and the board is held"), State.GetBoardOff().IsHeld());
	}
	{
		FGrabState State;
		TestEqual(TEXT("Let go 0.10 s before the contact: not caught"), ChordThenLetGo(State, 1.0f, 0.10f, Hz), EBoardCatchState::NotCaught);
		TestTrue(TEXT("the board is being caught"), State.GetBoardOff().IsCatching());
	}
	{
		FGrabState State;
		TestEqual(TEXT("Let go 0.17 s before the contact: not caught (the grace is the last 0.12 s of the 0.3 s catch)"), ChordThenLetGo(State, 1.0f, 0.17f, Hz), EBoardCatchState::NotCaught);
	}
	{
		FGrabState State;
		TestEqual(TEXT("Still coming off (0.1 s into the 0.25 s removal): not caught"), ChordThenLetGo(State, 0.1f, 0.0f, Hz), EBoardCatchState::NotCaught);
	}

	// Pure: the evaluator crashes a board that is not attached, before anything else.
	{
		FLandingInputs In;
		In.KiteElevationDeg = 60.0f;
		In.LandingG = 2.0f;
		In.bBoardAttached = false;
		In.BackFoot = EFootStrapState::Out;
		const FLandingVerdict Verdict = LandingEvaluator::Evaluate(In);
		TestEqual(TEXT("Board not attached: a crash"), Verdict.Grade, ELandingGrade::Crash);
		TestEqual(TEXT("cause BoardOff, named before the foot"), Verdict.Cause, ELandingCause::BoardOff);
		TestEqual(TEXT("The card's line"), AKiteSurfHUD::LandingCauseLine(ELandingCause::BoardOff), FString(TEXT("Board not caught")));
	}

	// The ride: both buttons pressed 0.3 s after the take-off and held to the water.
	FBoardOffRide Ride;
	if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
	{
		return false;
	}
	const FBoardOffRun Run = RunBoardOff(Ride, 0.3f, 0.0f, ETrickBoardOff::Plain);
	AddInfo(FString::Printf(TEXT("Held to the water: air %.2f s, %d held frames, hands off their grips %.2f cm at worst, board %.0f cm from the straps at least, tuck %.2f; verdict %s (%s), crashing 0.5 s later %d; record '%s' %s, board-off %s %.2f s; card '%s'"),
		Run.Air, Run.HeldFrames, Run.WorstHandCm, Run.MinBoardOffStrapsCm, Run.MaxTuck, *UEnum::GetValueAsString(Run.Verdict.Grade), *UEnum::GetValueAsString(Run.Verdict.Cause),
		Run.CrashingHalfSecondLater, *Run.Record.TrickName, *UEnum::GetValueAsString(Run.Record.Outcome), *UEnum::GetValueAsString(Run.Record.BoardOff), Run.Record.BoardOffSeconds,
		*Run.Card.Replace(TEXT("\n"), TEXT(" / "))));
	if (!TestTrue(TEXT("Took off and came down"), Run.bTookOff && Run.bLanded))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("The board was held off for most of the flight (%d frames)"), Run.HeldFrames), Run.HeldFrames >= 30);
	TestEqual(TEXT("The landing is a crash"), Run.Verdict.Grade, ELandingGrade::Crash);
	TestEqual(TEXT("because the board was not caught"), Run.Verdict.Cause, ELandingCause::BoardOff);
	TestTrue(TEXT("The board is crashing 0.5 s after the contact"), Run.CrashingHalfSecondLater);
	TestEqual(TEXT("Recorded as a crash"), Run.Record.Outcome, EJumpOutcome::Crashed);
	TestEqual(TEXT("with the cause"), Run.Record.LandingCause, ELandingCause::BoardOff);
	TestEqual(TEXT("The record still names the board-off"), Run.Record.TrickName, FString(TEXT("Board-off")));
	TestTrue(FString::Printf(TEXT("The card says why ('%s')"), *Run.Card), Run.Card.EndsWith(TEXT("\nBoard not caught")));
	return true;
}

// Let go in time: caught and landed; in the last 0.12 s of the catch: caught late, sketchy. The drawn
// board goes to the hands while held and back under the feet after the catch.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickBoardOffCaughtLands, "KiteSurf.Trick.BoardOffCaughtLands", TrickBoardOffTest::Flags)

bool FKiteSurfTrickBoardOffCaughtLands::RunTest(const FString& Parameters)
{
	using namespace TrickBoardOffTest;

	// Pure: the catch window at every step rate.
	for (const float Hz : { 60.0f, 120.0f, 240.0f })
	{
		{
			FGrabState State;
			TestEqual(FString::Printf(TEXT("%.0f Hz: let go 0.5 s before: attached"), Hz), ChordThenLetGo(State, 1.0f, 0.5f, Hz), EBoardCatchState::Attached);
			TestFalse(FString::Printf(TEXT("%.0f Hz: the board is back on the feet"), Hz), State.GetBoardOff().IsBoardOff());
		}
		{
			FGrabState State;
			TestEqual(FString::Printf(TEXT("%.0f Hz: let go 0.30 s before: attached"), Hz), ChordThenLetGo(State, 1.0f, 0.30f, Hz), EBoardCatchState::Attached);
		}
		{
			FGrabState State;
			TestEqual(FString::Printf(TEXT("%.0f Hz: let go 0.22 s before: caught late"), Hz), ChordThenLetGo(State, 1.0f, 0.22f, Hz), EBoardCatchState::CaughtLate);
		}
		{
			FGrabState State;
			TestEqual(FString::Printf(TEXT("%.0f Hz: only the front button let go 0.5 s before: attached"), Hz), ChordThenLetGo(State, 1.0f, 0.5f, Hz, FVector2D::ZeroVector, true), EBoardCatchState::Attached);
			TestEqual(FString::Printf(TEXT("%.0f Hz: and the back button still held does not grab"), Hz), State.GetGrabs().Num(), 0);
		}
	}
	{
		// Let go during the removal: the catch starts from where it got to.
		FGrabState State;
		TestEqual(TEXT("Let go 0.1 s into the removal, 0.15 s before: attached"), ChordThenLetGo(State, 0.1f, 0.15f, 240.0f), EBoardCatchState::Attached);
	}

	// Pure: the evaluator.
	{
		FLandingInputs In;
		In.KiteElevationDeg = 60.0f;
		In.LandingG = 2.0f;
		TestEqual(TEXT("Attached: a level landing is stomped"), LandingEvaluator::Evaluate(In).Grade, ELandingGrade::Stomped);
		In.bBoardCaughtLate = true;
		const FLandingVerdict Late = LandingEvaluator::Evaluate(In);
		TestEqual(TEXT("Caught late: sketchy"), Late.Grade, ELandingGrade::Sketchy);
		TestEqual(TEXT("cause BoardCaughtLate"), Late.Cause, ELandingCause::BoardCaughtLate);
		In.KiteElevationDeg = 30.0f;
		TestEqual(TEXT("Caught late with the kite low: still named the board"), LandingEvaluator::Evaluate(In).Cause, ELandingCause::BoardCaughtLate);
		TestFalse(TEXT("The card has a line for it"), AKiteSurfHUD::LandingCauseLine(ELandingCause::BoardCaughtLate).IsEmpty());
	}

	// Pure: the chord and the grabs. The chord takes over from a grab; the board-off starts only in
	// the air and only on a fresh press; nothing grabs while the board is off the feet.
	{
		FGrabState State;
		FGrabStateInput In;
		In.bFront = true;
		In.bBack = true;
		for (int32 Step = 0; Step < 60; ++Step)
		{
			State.Step(In, 1.0f / 240.0f);
		}
		TestFalse(TEXT("On the water: the chord does nothing"), State.GetBoardOff().IsBoardOff());
		In.bAirborne = true;
		for (int32 Step = 0; Step < 60; ++Step)
		{
			State.Step(In, 1.0f / 240.0f);
		}
		TestFalse(TEXT("Held from the water into the air: still nothing"), State.GetBoardOff().IsBoardOff());
		In.bFront = false;
		State.Step(In, 1.0f / 240.0f);
		TestFalse(TEXT("One let go: no grab from the other, already held"), State.IsHandOffBar());
		In.bFront = true;
		State.Step(In, 1.0f / 240.0f);
		TestTrue(TEXT("Pressed again: the board comes off"), State.GetBoardOff().IsBoardOff());
	}
	{
		FGrabState State;
		FGrabStateInput In;
		In.bAirborne = true;
		State.Step(In, 1.0f / 240.0f);
		In.bBack = true;
		for (int32 Step = 0; Step < 96; ++Step) // 0.4 s: reached and held 0.2 s
		{
			State.Step(In, 1.0f / 240.0f);
		}
		TestTrue(TEXT("The back hand grabs"), State.IsHolding());
		In.bFront = true;
		State.Step(In, 1.0f / 240.0f);
		TestTrue(TEXT("Front pressed too: the board comes off"), State.GetBoardOff().IsBoardOff());
		TestFalse(TEXT("and the grab has ended"), State.IsHolding() || State.IsReaching());
		TestEqual(TEXT("logged once"), State.GetGrabs().Num(), 1);
		if (State.GetGrabs().Num() == 1)
		{
			TestNearlyEqual(TEXT("with its hold (s)"), State.GetGrabs()[0].HoldSeconds, 0.2f, 0.01f);
		}
	}

	// The ride, twice: both buttons pressed 0.2 s after the take-off and let go coming down, at a time
	// to contact of 0.6 s (caught) and of 0.25 s (in the grace, or not caught).
	for (int32 Late = 0; Late < 2; ++Late)
	{
		FBoardOffRide Ride;
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		const FBoardOffRun Run = RunBoardOff(Ride, 0.2f, Late == 0 ? 0.6f : 0.25f, ETrickBoardOff::Plain);
		const float LetGoBefore = Run.TouchdownAt - Run.LetGoAt;
		const FString What = Late == 0 ? TEXT("Let go early") : TEXT("Let go late");
		AddInfo(FString::Printf(TEXT("%s: air %.2f s, let go %.3f s before the touchdown; %d held frames, hands off their grips %.2f cm at worst, board %.0f cm from the straps and ankles %.0f cm from theirs at least, tuck %.2f; after the catch (%d frames) feet off the straps %.3f cm, board off the attitude %.3f deg; verdict %s (%s); record '%s' %s, board-off %s %.2f s; ticker '%s'; card '%s'"),
			*What, Run.Air, LetGoBefore, Run.HeldFrames, Run.WorstHandCm, Run.MinBoardOffStrapsCm, Run.MinAnkleOffStrapCm, Run.MaxTuck, Run.CaughtFrames, Run.WorstFeetAfterCatchCm, Run.WorstBoardAfterCatchDeg,
			*UEnum::GetValueAsString(Run.Verdict.Grade), *UEnum::GetValueAsString(Run.Verdict.Cause), *Run.Record.TrickName, *UEnum::GetValueAsString(Run.Record.Outcome),
			*UEnum::GetValueAsString(Run.Record.BoardOff), Run.Record.BoardOffSeconds, *Run.Ticker, *Run.Card.Replace(TEXT("\n"), TEXT(" / "))));
		if (!TestTrue(FString::Printf(TEXT("%s: took off, let go and came down"), *What), Run.bTookOff && Run.bLanded && Run.bLetGo))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: the board was held off (%d frames)"), *What, Run.HeldFrames), Run.HeldFrames >= 30);
		TestTrue(FString::Printf(TEXT("%s: both hands on their grips on the drawn board while held (%.2f cm)"), *What, Run.WorstHandCm), Run.WorstHandCm < 2.0f);
		TestTrue(FString::Printf(TEXT("%s: the drawn board came up off the feet (%.0f cm)"), *What, Run.MinBoardOffStrapsCm), Run.MinBoardOffStrapsCm > 40.0f && Run.MinBoardOffStrapsCm < 1000.0f);
		TestTrue(FString::Printf(TEXT("%s: the feet were out of the straps (%.0f cm)"), *What, Run.MinAnkleOffStrapCm), Run.MinAnkleOffStrapCm > 20.0f && Run.MinAnkleOffStrapCm < 1000.0f);
		TestTrue(FString::Printf(TEXT("%s: the held board tucked the body (%.2f)"), *What, Run.MaxTuck), Run.MaxTuck > 0.4f);
		TestEqual(FString::Printf(TEXT("%s: the record has the board-off"), *What), Run.Record.BoardOff, ETrickBoardOff::Plain);
		TestTrue(FString::Printf(TEXT("%s: held for its seconds (%.2f s)"), *What, Run.Record.BoardOffSeconds), Run.Record.BoardOffSeconds > 0.5f);
		TestEqual(FString::Printf(TEXT("%s: named"), *What), Run.Record.TrickName, FString(TEXT("Board-off")));
		TestEqual(FString::Printf(TEXT("%s: the ticker named it in the air"), *What), Run.Ticker, FString(TEXT("Board-off")));
		if (LetGoBefore >= 0.30f)
		{
			TestTrue(FString::Printf(TEXT("%s: caught in the air (%d frames after the catch)"), *What, Run.CaughtFrames), Run.CaughtFrames > 0);
			TestTrue(FString::Printf(TEXT("%s: the feet back in the straps after the catch (%.3f cm)"), *What, Run.WorstFeetAfterCatchCm), Run.WorstFeetAfterCatchCm < 1.0f);
			TestTrue(FString::Printf(TEXT("%s: the drawn board back on the attitude's board (%.3f deg)"), *What, Run.WorstBoardAfterCatchDeg), Run.WorstBoardAfterCatchDeg < 0.5f);
			TestNotEqual(FString::Printf(TEXT("%s: the landing stands"), *What), Run.Verdict.Grade, ELandingGrade::Crash);
			TestTrue(FString::Printf(TEXT("%s: and the board is not the cause"), *What), Run.Verdict.Cause != ELandingCause::BoardOff && Run.Verdict.Cause != ELandingCause::BoardCaughtLate);
			TestEqual(FString::Printf(TEXT("%s: landed"), *What), Run.Record.Outcome, EJumpOutcome::Landed);
			TestTrue(FString::Printf(TEXT("%s: the board-off scores technicality"), *What), Run.Record.Score.Technicality >= FTrickScoringSettings().BoardOff - 1e-4f);
			TestTrue(FString::Printf(TEXT("%s: the card names it ('%s')"), *What, *Run.Card), Run.Card.StartsWith(TEXT("Board-off  ")));
		}
		else if (LetGoBefore >= 0.18f + 1.0f / 120.0f)
		{
			TestEqual(FString::Printf(TEXT("%s: caught late, sketchy"), *What), Run.Verdict.Grade, ELandingGrade::Sketchy);
			TestEqual(FString::Printf(TEXT("%s: cause BoardCaughtLate"), *What), Run.Verdict.Cause, ELandingCause::BoardCaughtLate);
			TestEqual(FString::Printf(TEXT("%s: landed"), *What), Run.Record.Outcome, EJumpOutcome::Landed);
		}
		else if (LetGoBefore < 0.18f - 1.0f / 120.0f)
		{
			TestEqual(FString::Printf(TEXT("%s: too late, a crash"), *What), Run.Verdict.Cause, ELandingCause::BoardOff);
		}
		if (Late == 1)
		{
			TestTrue(FString::Printf(TEXT("%s: let go inside the 0.3 s catch (%.3f s), so the late case is exercised"), *What, LetGoBefore), LetGoBefore < 0.30f);
		}
	}
	return true;
}

// The variants: the stick picks them, the credit and the names (with loops and rotations), the scores,
// and every grip in the arms' reach.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickBoardOffVariantsNamed, "KiteSurf.Trick.BoardOffVariantsNamed", TrickBoardOffTest::Flags)

bool FKiteSurfTrickBoardOffVariantsNamed::RunTest(const FString& Parameters)
{
	using namespace TrickBoardOffTest;
	const float Hz = 240.0f;

	// The controls legend has a row for it, with keys and buttons.
	{
		bool bInLegend = false;
		for (const FKiteSurfControlBinding& Binding : KiteSurfControlsLegend::GetBindings())
		{
			if (FString(Binding.Action).Contains(TEXT("board-off")))
			{
				bInLegend = FString(Binding.Keyboard).Contains(TEXT("Q + E")) && FString(Binding.Gamepad).Contains(TEXT("LB + RB"));
			}
		}
		TestTrue(TEXT("The controls legend shows Q + E and LB + RB for the board-off"), bInLegend);
	}

	// The stick during the removal picks the variant.
	TestEqual(TEXT("Centred: plain"), FBoardOffState::ResolveVariant(FVector2D::ZeroVector, 0.5f), ETrickBoardOff::Plain);
	TestEqual(TEXT("Up: superman"), FBoardOffState::ResolveVariant(FVector2D(0.0f, 1.0f), 0.5f), ETrickBoardOff::Superman);
	TestEqual(TEXT("Down: tic tac"), FBoardOffState::ResolveVariant(FVector2D(0.2f, -0.9f), 0.5f), ETrickBoardOff::TicTac);
	TestEqual(TEXT("Sideways: board pass"), FBoardOffState::ResolveVariant(FVector2D(-1.0f, 0.3f), 0.5f), ETrickBoardOff::BoardPass);

	struct FCase
	{
		const TCHAR* What;
		FVector2D Stick;
		float HeldSeconds;
		ETrickBoardOff Expected;
	};
	const FCase Cases[] =
	{
		{ TEXT("Plain held 0.6 s"), FVector2D::ZeroVector, 0.6f, ETrickBoardOff::Plain },
		{ TEXT("Superman held 0.6 s"), FVector2D(0.0f, 1.0f), 0.6f, ETrickBoardOff::Superman },
		{ TEXT("Tic tac held 0.9 s (0.25 s off + 0.5 s spin)"), FVector2D(0.0f, -1.0f), 0.9f, ETrickBoardOff::TicTac },
		{ TEXT("Tic tac let go mid-spin: plain"), FVector2D(0.0f, -1.0f), 0.55f, ETrickBoardOff::Plain },
		{ TEXT("Board pass held 1.3 s (0.25 s off + 0.9 s round)"), FVector2D(1.0f, 0.0f), 1.3f, ETrickBoardOff::BoardPass },
		{ TEXT("Board pass let go half way: plain"), FVector2D(1.0f, 0.0f), 0.7f, ETrickBoardOff::Plain },
		{ TEXT("Let go 0.1 s after it came off: too short to count"), FVector2D::ZeroVector, 0.35f, ETrickBoardOff::None },
	};
	for (const FCase& Case : Cases)
	{
		FGrabState State;
		ChordThenLetGo(State, Case.HeldSeconds, 0.5f, Hz, Case.Stick);
		TestEqual(FString::Printf(TEXT("%s: credited %s"), Case.What, *UEnum::GetValueAsString(Case.Expected)), State.GetBoardOff().GetFlightBoardOff(), Case.Expected);
		if (Case.Expected != ETrickBoardOff::None)
		{
			TestNearlyEqual(FString::Printf(TEXT("%s: held from the end of the removal (s)"), Case.What), State.GetBoardOff().GetFlightBoardOffSeconds(), Case.HeldSeconds - 0.25f, 0.01f);
		}
	}
	{
		// The stick is read during the removal and latched once the board is in the hands.
		FGrabState State;
		FGrabStateInput In;
		In.bAirborne = true;
		State.Step(In, 1.0f / Hz);
		In.bFront = true;
		In.bBack = true;
		In.ZoneStick = FVector2D(0.0f, 1.0f);
		for (int32 Step = 0; Step < 72; ++Step) // 0.3 s: off and held
		{
			State.Step(In, 1.0f / Hz);
		}
		In.ZoneStick = FVector2D(0.0f, -1.0f);
		for (int32 Step = 0; Step < 48; ++Step)
		{
			State.Step(In, 1.0f / Hz);
		}
		TestEqual(TEXT("Latched at the hands: the stick after does not change it"), State.GetBoardOff().GetVariant(), ETrickBoardOff::Superman);
	}
	{
		// A new flight clears the board-off.
		FGrabState State;
		ChordThenLetGo(State, 0.6f, 0.5f, Hz);
		FGrabStateInput In;
		State.Step(In, 1.0f / Hz); // on the water
		TestEqual(TEXT("Still the last flight's on the water"), State.GetBoardOff().GetFlightBoardOff(), ETrickBoardOff::Plain);
		In.bAirborne = true;
		State.Step(In, 1.0f / Hz);
		TestEqual(TEXT("A new flight: none"), State.GetBoardOff().GetFlightBoardOff(), ETrickBoardOff::None);
	}

	// Names, from the record through the signature, alone and with loops and rotations.
	struct FNameCase
	{
		ETrickBoardOff BoardOff;
		const TCHAR* Name;
	};
	const FNameCase Names[] =
	{
		{ ETrickBoardOff::Plain, TEXT("Board-off") },
		{ ETrickBoardOff::Superman, TEXT("Superman") },
		{ ETrickBoardOff::TicTac, TEXT("Tic tac") },
		{ ETrickBoardOff::BoardPass, TEXT("Board pass") },
	};
	float LastTechnicality = 0.0f;
	for (const FNameCase& Case : Names)
	{
		const FJumpRecord Record = MakeBoardOffRecord(Case.BoardOff);
		const FTrickSignature Signature = TrickRecognition::SignatureFromJump(Record);
		TestEqual(FString::Printf(TEXT("%s: the signature has it"), Case.Name), Signature.BoardOff, Case.BoardOff);
		TestNearlyEqual(FString::Printf(TEXT("%s: with its seconds"), Case.Name), Signature.BoardOffSeconds, 0.8f, 1e-4f);
		TestEqual(FString::Printf(TEXT("%s: named"), Case.Name), TrickNaming::Name(Signature), FString(Case.Name));
		const float Technicality = TrickScoring::ScoreJump(Record, Signature).Technicality;
		TestTrue(FString::Printf(TEXT("%s: scores more than the variant before (%.2f > %.2f)"), Case.Name, Technicality, LastTechnicality), Technicality > LastTechnicality);
		LastTechnicality = Technicality;
	}
	{
		FTrickSignature Signature = TrickRecognition::SignatureFromJump(MakeBoardOffRecord(ETrickBoardOff::Plain));
		Signature.Loops.Add(MakeLoop(ETrickLoopKind::Megaloop));
		TestEqual(TEXT("Megaloop and board-off"), TrickNaming::Name(Signature), FString(TEXT("Megaloop board-off")));
		Signature.Loops.Reset();
		Signature.Loops.Add(MakeLoop(ETrickLoopKind::Kiteloop));
		TestEqual(TEXT("Kiteloop and board-off"), TrickNaming::Name(Signature), FString(TEXT("Kiteloop board-off")));
		Signature.BoardOff = ETrickBoardOff::Superman;
		TestEqual(TEXT("Kiteloop and superman"), TrickNaming::Name(Signature), FString(TEXT("Kiteloop superman")));
	}
	{
		FJumpRecord Record = MakeBoardOffRecord(ETrickBoardOff::TicTac);
		Record.Inversions.Add(ETrickInversion::BackRoll);
		const FString Name = TrickNaming::Name(TrickRecognition::SignatureFromJump(Record));
		TestEqual(TEXT("Back roll and tic tac"), Name, FString(TEXT("Back roll tic tac")));
		Record.BoardOff = ETrickBoardOff::None;
		Record.BoardOffSeconds = 0.8f;
		const FTrickSignature Without = TrickRecognition::SignatureFromJump(Record);
		TestEqual(TEXT("No board-off: no seconds in the signature"), Without.BoardOffSeconds, 0.0f);
		TestEqual(TEXT("No board-off: the back roll alone"), TrickNaming::Name(Without), FString(TEXT("Back roll")));
	}

	// Every grip of every variant is within the arms' reach of the shoulders (folded as the pose asks),
	// either nose side, across the tic tac's spin and the pass's way round.
	const float ArmReachCm = RiderRig::UpperArmLengthCm + RiderRig::ForearmLengthCm;
	float WorstReachShare = 0.0f;
	FString WorstReachWhere;
	float WorstTicTacGripDriftCm = 0.0f;
	for (const float NoseSide : { 1.0f, -1.0f })
	{
		for (const ETrickBoardOff Variant : { ETrickBoardOff::Plain, ETrickBoardOff::Superman, ETrickBoardOff::TicTac, ETrickBoardOff::BoardPass })
		{
			const FVector FirstGrip = BoardOffPose::Evaluate(Variant, 0.0f, 0.0f, NoseSide).BoardInRider.TransformPosition(BoardOffPose::Evaluate(Variant, 0.0f, 0.0f, NoseSide).GripLocal[1]);
			for (int32 Sample = 0; Sample <= 40; ++Sample)
			{
				const float T = Sample / 40.0f;
				const FBoardOffPose Pose = BoardOffPose::Evaluate(Variant, 360.0f * T, T, NoseSide);
				FRiderRigPose Body;
				Body.Torso = FQuat(FVector::YAxisVector, FMath::DegreesToRadians(Pose.TorsoPitchDeg));
				int32 HandsOn = 0;
				for (int32 HandIndex = 0; HandIndex < 2; ++HandIndex)
				{
					if (Pose.HandOnBoard[HandIndex] <= 0.0f)
					{
						continue;
					}
					++HandsOn;
					const FVector Shoulder = RiderRig::ShoulderPosition(Body, RigSideOf(static_cast<ETrickHand>(HandIndex), NoseSide));
					const FVector Grip = Pose.BoardInRider.TransformPosition(Pose.GripLocal[HandIndex]);
					const float Share = static_cast<float>(FVector::Dist(Shoulder, Grip)) / ArmReachCm;
					if (Share > WorstReachShare)
					{
						WorstReachShare = Share;
						WorstReachWhere = FString::Printf(TEXT("%s, nose %+.0f, %s hand at %.3f"), *UEnum::GetValueAsString(Variant), NoseSide, HandIndex == 0 ? TEXT("front") : TEXT("back"), T);
					}
				}
				TestTrue(FString::Printf(TEXT("%s, nose %+.0f, at %.3f: at least one hand on the board"), *UEnum::GetValueAsString(Variant), NoseSide, T), HandsOn >= 1);
				if (Variant == ETrickBoardOff::TicTac)
				{
					WorstTicTacGripDriftCm = FMath::Max(WorstTicTacGripDriftCm, static_cast<float>(FVector::Dist(Pose.BoardInRider.TransformPosition(Pose.GripLocal[1]), FirstGrip)));
				}
				// The legs reach their ankles from the hips.
				for (int32 Side = 0; Side < 2; ++Side)
				{
					const FVector Hip(0.0f, (Side == 0 ? -1.0f : 1.0f) * RiderRig::HipHalfWidthCm, 0.0f);
					TestTrue(FString::Printf(TEXT("%s: ankle %d within the leg's reach"), *UEnum::GetValueAsString(Variant), Side),
						FVector::Dist(Hip, Pose.AnkleInRider[Side]) <= RiderRig::ThighLengthCm + RiderRig::ShinLengthCm);
				}
			}
		}
	}
	{
		// The tic tac ends where it started, the same rail in the hand; the pass hands over behind the back.
		const FBoardOffPose Start = BoardOffPose::Evaluate(ETrickBoardOff::TicTac, 0.0f, 0.0f, 1.0f);
		const FBoardOffPose End = BoardOffPose::Evaluate(ETrickBoardOff::TicTac, 360.0f, 0.0f, 1.0f);
		TestTrue(TEXT("Tic tac: 360 deg brings the board back"), Start.BoardInRider.GetRotation().AngularDistance(End.BoardInRider.GetRotation()) < 1e-3f);
		const FBoardOffPose Half = BoardOffPose::Evaluate(ETrickBoardOff::TicTac, 180.0f, 0.0f, 1.0f);
		TestTrue(TEXT("Tic tac: half way the board is upside down about its long axis"),
			FVector::DotProduct(Half.BoardInRider.GetUnitAxis(EAxis::Z), Start.BoardInRider.GetUnitAxis(EAxis::Z)) < -0.99f);
		const FBoardOffPose HandOver = BoardOffPose::Evaluate(ETrickBoardOff::BoardPass, 0.0f, 0.5f, 1.0f);
		TestTrue(TEXT("Board pass: both hands on it at the hand-over"), HandOver.HandOnBoard[0] >= 1.0f && HandOver.HandOnBoard[1] >= 1.0f);
		TestTrue(TEXT("Board pass: behind the back at the hand-over"), HandOver.BoardInRider.GetLocation().X < -25.0f);
		const FBoardOffPose Done = BoardOffPose::Evaluate(ETrickBoardOff::BoardPass, 0.0f, 1.0f, 1.0f);
		TestTrue(TEXT("Board pass: finished in the front hand"), Done.HandOnBoard[0] >= 1.0f && Done.HandOnBoard[1] <= 0.0f);
	}
	AddInfo(FString::Printf(TEXT("Grips at %.0f%% of the arm's reach at worst (%s); tic tac grip drift %.4f cm"), 100.0f * WorstReachShare, *WorstReachWhere, WorstTicTacGripDriftCm));
	TestTrue(FString::Printf(TEXT("Every grip within 95%% of the arm's reach (%.0f%%)"), 100.0f * WorstReachShare), WorstReachShare < 0.95f);
	TestTrue(FString::Printf(TEXT("The tic tac turns about the hand (%.4f cm)"), WorstTicTacGripDriftCm), WorstTicTacGripDriftCm < 0.01f);
	return true;
}

// The variants on the ride: the stick picks them as the board comes off, the drawn board stays in the
// hand that holds it (the tic tac turning in it, the pass going round the back), the board is caught
// and the jump is named after the variant.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickBoardOffFollowsHands, "KiteSurf.Trick.BoardOffFollowsHands", TrickBoardOffTest::Flags)

bool FKiteSurfTrickBoardOffFollowsHands::RunTest(const FString& Parameters)
{
	using namespace TrickBoardOffTest;
	struct FVariantCase
	{
		ETrickBoardOff Variant;
		FVector2D Stick;
		const TCHAR* Name;
	};
	const FVariantCase Cases[] =
	{
		{ ETrickBoardOff::Superman, FVector2D(0.0f, 1.0f), TEXT("Superman") },
		{ ETrickBoardOff::TicTac, FVector2D(0.0f, -1.0f), TEXT("Tic tac") },
		{ ETrickBoardOff::BoardPass, FVector2D(1.0f, 0.0f), TEXT("Board pass") },
	};
	for (const FVariantCase& Case : Cases)
	{
		FBoardOffRide Ride;
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		const FBoardOffRun Run = RunBoardOff(Ride, 0.2f, 0.6f, Case.Variant, Case.Stick);
		AddInfo(FString::Printf(TEXT("%s: air %.2f s, let go %.3f s before the touchdown; %d held frames, hand off its grip %.2f cm at worst, board %.0f cm from the straps at least, tuck %.2f; after the catch (%d frames) feet off the straps %.3f cm; verdict %s (%s); record '%s', board-off %s %.2f s"),
			Case.Name, Run.Air, Run.TouchdownAt - Run.LetGoAt, Run.HeldFrames, Run.WorstHandCm, Run.MinBoardOffStrapsCm, Run.MaxTuck, Run.CaughtFrames, Run.WorstFeetAfterCatchCm,
			*UEnum::GetValueAsString(Run.Verdict.Grade), *UEnum::GetValueAsString(Run.Verdict.Cause), *Run.Record.TrickName, *UEnum::GetValueAsString(Run.Record.BoardOff), Run.Record.BoardOffSeconds));
		if (!TestTrue(FString::Printf(TEXT("%s: took off, let go and came down"), Case.Name), Run.bTookOff && Run.bLanded && Run.bLetGo))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: held long enough for the variant (%d frames)"), Case.Name, Run.HeldFrames), Run.HeldFrames >= 60);
		TestTrue(FString::Printf(TEXT("%s: the holding hand on its grip on the drawn board (%.2f cm)"), Case.Name, Run.WorstHandCm), Run.WorstHandCm < 2.0f);
		TestTrue(FString::Printf(TEXT("%s: the drawn board off the feet (%.0f cm)"), Case.Name, Run.MinBoardOffStrapsCm), Run.MinBoardOffStrapsCm > 30.0f && Run.MinBoardOffStrapsCm < 1000.0f);
		TestTrue(FString::Printf(TEXT("%s: caught in the air, feet back in the straps (%.3f cm)"), Case.Name, Run.WorstFeetAfterCatchCm), Run.CaughtFrames > 0 && Run.WorstFeetAfterCatchCm < 1.0f);
		TestNotEqual(FString::Printf(TEXT("%s: the landing stands"), Case.Name), Run.Verdict.Grade, ELandingGrade::Crash);
		TestEqual(FString::Printf(TEXT("%s: the record has the variant"), Case.Name), Run.Record.BoardOff, Case.Variant);
		TestEqual(FString::Printf(TEXT("%s: named"), Case.Name), Run.Record.TrickName, FString(Case.Name));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
