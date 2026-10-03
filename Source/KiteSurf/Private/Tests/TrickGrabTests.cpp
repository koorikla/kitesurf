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
#include "Tricks/GrabState.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/LandingEvaluator.h"
#include "Tricks/RiderAttitudeComponent.h"
#include "Tricks/TrickNaming.h"
#include "Tricks/TrickRecognition.h"
#include "Tricks/TrickScoring.h"
#include "Tricks/TrickTrackerComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// T2.1 grabs and T2.2 the one-footer (docs/tricks/T2.md): the pure grab state, naming and scoring,
// then the pawn: the phase 2 timed jump (30 kn, the recommended kite) with the grab and one-footer
// buttons pressed in the air, the HUD following the tracker frame by frame.

namespace TrickGrabTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float FrameSeconds = 1.0f / 60.0f;

	/** A rider on the water in steady wind along +X, started on a beam reach as the game mode does (TrickLiveRotationTests' fixture). */
	struct FGrabRide
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

		explicit FGrabRide(float WindKnots = 30.0f)
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

		~FGrabRide()
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
			Pawn->Tick(FrameSeconds);
			if (Boom)
			{
				Boom->TickComponent(FrameSeconds, LEVELTICK_All, nullptr);
			}
			HUD->UpdateJumpCard(Tracker, FrameSeconds);
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

		/** The player's left stick as Enhanced Input hands it over. */
		void Stick(float X, float Y)
		{
			Pawn->OnEdgeTriggered(FInputActionValue(X));
			Pawn->OnWeightShiftTriggered(FInputActionValue(Y));
		}

		/**
		 * TrickLiveRotationTests' timed jump up to the pop: 8 s of riding, the bar hard over, the weight
		 * on the tail and the jump button held with the pre-wind, let go to pop 0.66 s after the bar
		 * reaches the kite; the bar centred.
		 */
		void SendAndPop(const FVector2D& PreWind)
		{
			const float SendAt = 8.0f;
			SimulateUntil(SendAt);
			Pawn->SteerKite(-1.0f);
			Board->SetWeightShift(-1.0f);
			Pawn->SetLoadHeld(true);
			Pawn->SetPreWind(PreWind);
			const float ReleaseAt = SendAt + FMath::RoundToFloat((0.66f + Kite->GetSteeringDeadTimeSeconds()) * 30.0f) / 30.0f;
			while (!HasReached(ReleaseAt) && !IsAirborne())
			{
				Frame();
			}
			Pawn->SheetKite(1.0f);
			Pawn->ReleaseLoadAndPop();
			Board->SetWeightShift(0.0f);
			Pawn->SteerKite(0.0f);
			Pawn->SetPreWind(FVector2D::ZeroVector);
		}
	};

	/** The nose's side of the drawn rider (+1 right): the drawn board's +X against the torso's right, as the pawn works it out. */
	float DrawnNoseSide(const AKiteRiderPawn* Pawn)
	{
		const FTransform BoardTransform = Pawn->GetBoardVisual()->GetComponentTransform();
		return FVector::DotProduct(BoardTransform.GetUnitAxis(EAxis::X), Pawn->GetRiderRigPose().Torso.GetAxisY()) >= 0.0f ? 1.0f : -1.0f;
	}

	/** The rig side (0 left, 1 right) of a hand: the front hand is on the nose's side. */
	int32 RigSide(ETrickHand Hand, float NoseSide)
	{
		return (Hand == ETrickHand::Front) == (NoseSide > 0.0f) ? 1 : 0;
	}

	/** How far each ankle is from its strap on the drawn board (cm): [front foot, back foot]. */
	void StrapErrors(const AKiteRiderPawn* Pawn, float& OutFront, float& OutBack)
	{
		const FTransform BoardTransform = Pawn->GetBoardVisual()->GetComponentTransform();
		const float NoseSide = DrawnNoseSide(Pawn);
		const int32 FrontSide = RigSide(ETrickHand::Front, NoseSide);
		const FRiderRigPose& Pose = Pawn->GetRiderRigPose();
		OutFront = FVector::Dist(Pose.Legs[FrontSide].End, BoardTransform.TransformPosition(BoardGrabPoints::ToBoardLocal(BoardGrabPoints::FrontStrap, NoseSide)));
		OutBack = FVector::Dist(Pose.Legs[1 - FrontSide].End, BoardTransform.TransformPosition(BoardGrabPoints::ToBoardLocal(BoardGrabPoints::BackStrap, NoseSide)));
	}

	/** A landed record for the pure scoring checks: a 5 m straight air, landed clean with the kite high. */
	FJumpRecord MakeRecord()
	{
		FJumpRecord Record;
		Record.Outcome = EJumpOutcome::Landed;
		Record.ApexHeightCm = 500.0f;
		Record.LandingG = 2.0f;
		Record.KiteElevationAtLandingDeg = 60.0f;
		return Record;
	}

	/** Steps a grab state at Hz in the air with the back button held for HeldSeconds (stick ZoneStick), then let go for a while. */
	FGrabState HoldBack(float HeldSeconds, float Hz, const FVector2D& ZoneStick = FVector2D::ZeroVector)
	{
		FGrabState State;
		const float Dt = 1.0f / Hz;
		FGrabStateInput In;
		In.bAirborne = true;
		State.Step(In, Dt); // in the air, nothing held
		In.bBack = true;
		In.ZoneStick = ZoneStick;
		const int32 HeldSteps = FMath::RoundToInt(HeldSeconds * Hz);
		for (int32 Step = 0; Step < HeldSteps; ++Step)
		{
			State.Step(In, Dt);
		}
		In.bBack = false;
		In.ZoneStick = FVector2D::ZeroVector;
		for (int32 Step = 0; Step < FMath::RoundToInt(0.3f * Hz); ++Step)
		{
			State.Step(In, Dt);
		}
		return State;
	}
}

// Every hand and zone has its name (docs/tricks.md 3.2), the zone stick picks the zone, and a
// record with one grab held long enough is named after it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickGrabNamesByHandAndZone, "KiteSurf.Trick.GrabNamesByHandAndZone", TrickGrabTest::Flags)

bool FKiteSurfTrickGrabNamesByHandAndZone::RunTest(const FString& Parameters)
{
	using namespace TrickGrabTest;
	struct FCase { ETrickHand Hand; ETrickGrabZone Zone; const TCHAR* Name; };
	const FCase Cases[] =
	{
		{ ETrickHand::Front, ETrickGrabZone::Nose, TEXT("Nose") },
		{ ETrickHand::Front, ETrickGrabZone::ToeEdge, TEXT("Mute") },
		{ ETrickHand::Front, ETrickGrabZone::HeelEdge, TEXT("Melon") },
		{ ETrickHand::Front, ETrickGrabZone::Tail, TEXT("Seatbelt") },
		{ ETrickHand::Back, ETrickGrabZone::Nose, TEXT("Crail") },
		{ ETrickHand::Back, ETrickGrabZone::ToeEdge, TEXT("Indy") },
		{ ETrickHand::Back, ETrickGrabZone::HeelEdge, TEXT("Stalefish") },
		{ ETrickHand::Back, ETrickGrabZone::Tail, TEXT("Tail") },
	};
	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("%s hand, %s"), Case.Hand == ETrickHand::Front ? TEXT("front") : TEXT("back"), *UEnum::GetValueAsString(Case.Zone)),
			TrickNaming::GrabName(Case.Hand, Case.Zone), FString(Case.Name));

		// A record with that grab held 0.5 s is named after it, alone ("Indy", not "Straight air indy").
		FJumpRecord Record = MakeRecord();
		FTrickGrab& Grab = Record.Grabs.AddDefaulted_GetRef();
		Grab.Hand = Case.Hand;
		Grab.Zone = Case.Zone;
		Grab.HoldSeconds = 0.5f;
		const FTrickSignature Signature = TrickRecognition::SignatureFromJump(Record);
		TestEqual(FString::Printf(TEXT("A jump with only a %s is named %s"), Case.Name, Case.Name), TrickNaming::Name(Signature), FString(Case.Name));
	}

	// The zone stick (rotation axes): up the nose, down the tail, towards the chest (-X) the toe edge,
	// towards the back (+X) the heel edge; inside the deadzone the toe edge.
	const float Deadzone = FGrabStateTuning().StickDeadzone;
	TestEqual(TEXT("Stick up: nose"), FGrabState::ResolveZone(FVector2D(0.0f, 0.9f), Deadzone), ETrickGrabZone::Nose);
	TestEqual(TEXT("Stick down: tail"), FGrabState::ResolveZone(FVector2D(0.0f, -0.9f), Deadzone), ETrickGrabZone::Tail);
	TestEqual(TEXT("Stick towards the chest: toe edge"), FGrabState::ResolveZone(FVector2D(-0.9f, 0.0f), Deadzone), ETrickGrabZone::ToeEdge);
	TestEqual(TEXT("Stick towards the back: heel edge"), FGrabState::ResolveZone(FVector2D(0.9f, 0.0f), Deadzone), ETrickGrabZone::HeelEdge);
	TestEqual(TEXT("Stick in the deadzone: toe edge"), FGrabState::ResolveZone(FVector2D(0.3f, 0.3f), Deadzone), ETrickGrabZone::ToeEdge);
	TestEqual(TEXT("Mostly up: nose"), FGrabState::ResolveZone(FVector2D(0.5f, 0.7f), Deadzone), ETrickGrabZone::Nose);

	// Through the grab state: the stick held during the reach picks the zone, and it is latched when
	// the hand gets there: moving the stick while holding changes nothing.
	{
		FGrabState State;
		FGrabStateInput In;
		In.bAirborne = true;
		State.Step(In, 1.0f / 240.0f);
		In.bFront = true;
		In.ZoneStick = FVector2D(0.9f, 0.0f);
		for (int32 Step = 0; Step < 60; ++Step)
		{
			State.Step(In, 1.0f / 240.0f);
		}
		TestTrue(TEXT("The front hand holds"), State.IsHolding() && State.GetHand() == ETrickHand::Front);
		TestEqual(TEXT("Stick to the back while reaching: the heel edge (melon)"), State.GetZone(), ETrickGrabZone::HeelEdge);
		In.ZoneStick = FVector2D(0.0f, 1.0f);
		for (int32 Step = 0; Step < 60; ++Step)
		{
			State.Step(In, 1.0f / 240.0f);
		}
		TestEqual(TEXT("Latched at the reach: still the heel edge"), State.GetZone(), ETrickGrabZone::HeelEdge);
		TestTrue(TEXT("While held the stick picks the zone, not the rotation"), State.IsZoneStickActive());
		TestEqual(TEXT("One grab logged"), State.GetGrabs().Num(), 1);
		if (State.GetGrabs().Num() == 1)
		{
			TestEqual(TEXT("It is a melon"), TrickNaming::GrabName(State.GetGrabs()[0].Hand, State.GetGrabs()[0].Zone), FString(TEXT("Melon")));
		}
	}
	return true;
}

// The hold counts from the moment the hand reaches the board; under 0.3 s a grab is neither named
// nor scored, and a longer hold scores more. One hand at a time, in the air only.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickGrabHoldCounts, "KiteSurf.Trick.GrabHoldCounts", TrickGrabTest::Flags)

bool FKiteSurfTrickGrabHoldCounts::RunTest(const FString& Parameters)
{
	using namespace TrickGrabTest;
	const FGrabStateTuning Tuning;
	const float Reach = Tuning.ReachSeconds;

	struct FHoldCase { float HoldSeconds; bool bCounts; };
	const FHoldCase HoldCases[] = { { 0.2f, false }, { 0.5f, true }, { 1.0f, true } };
	float PreviousTotal = -1.0f;
	float PreviousTechnicality = -1.0f;
	for (const FHoldCase& Case : HoldCases)
	{
		for (const float Hz : { 240.0f, 60.0f })
		{
			const FGrabState State = HoldBack(Reach + Case.HoldSeconds, Hz, FVector2D(-1.0f, 0.0f));
			const FString What = FString::Printf(TEXT("Held %.1f s on the board at %.0f Hz"), Case.HoldSeconds, Hz);
			if (!TestEqual(FString::Printf(TEXT("%s: one grab logged"), *What), State.GetGrabs().Num(), 1))
			{
				continue;
			}
			const FTrickGrab& Grab = State.GetGrabs()[0];
			TestEqual(FString::Printf(TEXT("%s: the back hand"), *What), Grab.Hand, ETrickHand::Back);
			TestEqual(FString::Printf(TEXT("%s: on the toe edge"), *What), Grab.Zone, ETrickGrabZone::ToeEdge);
			TestNearlyEqual(FString::Printf(TEXT("%s: the hold is counted from the reach (s)"), *What), Grab.HoldSeconds, Case.HoldSeconds, 1.01f / Hz);
			TestFalse(FString::Printf(TEXT("%s: the hand is back on the bar after letting go"), *What), State.IsHandOffBar());

			FJumpRecord Record = MakeRecord();
			Record.Grabs = State.GetGrabs();
			const FTrickSignature Signature = TrickRecognition::SignatureFromJump(Record);
			const FTrickScore Score = TrickScoring::ScoreJump(Record, Signature);
			if (Case.bCounts)
			{
				TestEqual(FString::Printf(TEXT("%s: named Indy"), *What), TrickNaming::Name(Signature), FString(TEXT("Indy")));
				const FTrickScoringSettings Scoring;
				const float Expected = Scoring.GrabBase + Scoring.GrabHoldBonusMax
					* FMath::Clamp((Grab.HoldSeconds - Scoring.GrabMinHoldSeconds) / (Scoring.GrabFullHoldSeconds - Scoring.GrabMinHoldSeconds), 0.0f, 1.0f);
				TestNearlyEqual(FString::Printf(TEXT("%s: technicality 0.2 plus the hold bonus"), *What), Score.Technicality, Expected, 1e-4f);
			}
			else
			{
				TestEqual(FString::Printf(TEXT("%s: too short to name"), *What), TrickNaming::Name(Signature), FString(TEXT("Straight air")));
				TestEqual(FString::Printf(TEXT("%s: and to score"), *What), Score.Technicality, 0.0f);
			}
			if (Hz == 240.0f)
			{
				// The hold scales the score: each longer hold scores strictly more.
				TestTrue(FString::Printf(TEXT("%s: scores more than the shorter hold (%.3f > %.3f)"), *What, Score.Total, PreviousTotal), Score.Total > PreviousTotal);
				TestTrue(FString::Printf(TEXT("%s: more technicality (%.3f > %.3f)"), *What, Score.Technicality, PreviousTechnicality),
					Case.bCounts ? Score.Technicality > PreviousTechnicality : Score.Technicality == 0.0f);
				PreviousTotal = Score.Total;
				PreviousTechnicality = Score.Technicality;
			}
		}
	}

	// Let go during the reach: logged with no hold, not named.
	{
		const FGrabState State = HoldBack(0.1f, 240.0f);
		TestEqual(TEXT("Let go while reaching: one grab logged"), State.GetGrabs().Num(), 1);
		TestEqual(TEXT("with no hold"), State.GetGrabs().Num() == 1 ? State.GetGrabs()[0].HoldSeconds : -1.0f, 0.0f);
	}

	// On the water the buttons do nothing; a button held from the water into the air does not grab
	// until pressed again.
	{
		FGrabState State;
		FGrabStateInput In;
		In.bBack = true;
		for (int32 Step = 0; Step < 120; ++Step)
		{
			State.Step(In, 1.0f / 240.0f);
		}
		TestFalse(TEXT("On the water: no hand leaves the bar"), State.IsHandOffBar());
		In.bAirborne = true;
		for (int32 Step = 0; Step < 120; ++Step)
		{
			State.Step(In, 1.0f / 240.0f);
		}
		TestFalse(TEXT("Held from the water into the air: still no grab"), State.IsHandOffBar());
		In.bBack = false;
		State.Step(In, 1.0f / 240.0f);
		In.bBack = true;
		State.Step(In, 1.0f / 240.0f);
		TestTrue(TEXT("Pressed again in the air: the hand reaches"), State.IsReaching());
	}

	// Both buttons are the board-off's chord (T2.3): not a double grab.
	{
		FGrabState State;
		FGrabStateInput In;
		In.bAirborne = true;
		State.Step(In, 1.0f / 240.0f);
		In.bFront = true;
		In.bBack = true;
		for (int32 Step = 0; Step < 240; ++Step)
		{
			State.Step(In, 1.0f / 240.0f);
		}
		TestFalse(TEXT("Both pressed together: no hand leaves the bar"), State.IsHandOffBar());
		TestEqual(TEXT("and nothing is logged"), State.GetGrabs().Num(), 0);

		FGrabState Second;
		FGrabStateInput In2;
		In2.bAirborne = true;
		Second.Step(In2, 1.0f / 240.0f);
		In2.bBack = true;
		for (int32 Step = 0; Step < 120; ++Step)
		{
			Second.Step(In2, 1.0f / 240.0f);
		}
		In2.bFront = true;
		for (int32 Step = 0; Step < 120; ++Step)
		{
			Second.Step(In2, 1.0f / 240.0f);
		}
		TestTrue(TEXT("The back hand grabbing, front pressed too: still only the back hand"), Second.IsHolding() && Second.GetHand() == ETrickHand::Back);
		TestEqual(TEXT("one grab, not two"), Second.GetGrabs().Num(), 1);
	}

	// The grab tucks the body: its zone's tuck once the hand is there, nothing on the bar.
	{
		FGrabState State = HoldBack(0.0f, 240.0f);
		TestEqual(TEXT("No grab: no tuck"), State.GetTuckTarget(), 0.0f);
		FGrabState Holding;
		FGrabStateInput In;
		In.bAirborne = true;
		Holding.Step(In, 1.0f / 240.0f);
		In.bBack = true;
		for (int32 Step = 0; Step < 120; ++Step)
		{
			Holding.Step(In, 1.0f / 240.0f);
		}
		TestEqual(TEXT("Holding the toe edge: a full tuck"), Holding.GetTuckTarget(), FGrabState::TuckForZone(ETrickGrabZone::ToeEdge));
		TestEqual(TEXT("which is 1"), FGrabState::TuckForZone(ETrickGrabZone::ToeEdge), 1.0f);
	}
	return true;
}

// The ride: the timed jump with RB and the stick towards the toe edge (the player's handlers) is an
// Indy: the back hand on its socket while held, both feet in the straps, the other hand on the bar,
// the name in the record and on the card. With a pre-wind roll, the grab's tuck spins the rider faster.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickGrabOnRide, "KiteSurf.Trick.GrabOnRide", TrickGrabTest::Flags)

bool FKiteSurfTrickGrabOnRide::RunTest(const FString& Parameters)
{
	using namespace TrickGrabTest;
	{
		FGrabRide Ride;
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		AKiteRiderPawn* Pawn = Ride.Pawn;
		Ride.SendAndPop(FVector2D::ZeroVector);

		float Air = 0.0f;
		bool bTookOff = false;
		bool bLanded = false;
		bool bPressed = false;
		bool bReleased = false;
		int32 HeldFrames = 0;
		float WorstHandCm = 0.0f;
		float WorstFrontFootCm = 0.0f;
		float WorstBackFootCm = 0.0f;
		float MaxPullCm = 0.0f;
		float MaxTuck = 0.0f;
		bool bRotationSuppressed = true;
		bool bBarHandStayed = true;
		FString Ticker;
		FString Card;
		const float StartAt = Pawn->GetSimTimeSeconds();
		while (!Ride.HasReached(StartAt + 15.0f))
		{
			if (bTookOff && !bPressed && Air >= 0.2f)
			{
				// RB, then the stick towards the chest's side of the screen (the toe edge).
				Pawn->OnGrabBackPressed(FInputActionValue(true));
				Ride.Stick(-Pawn->GetScreenBackSign(), 0.0f);
				bPressed = true;
			}
			if (bPressed && !bReleased && Air >= 1.0f)
			{
				Ride.Stick(0.0f, 0.0f);
				Pawn->OnGrabBackReleased(FInputActionValue(false));
				bReleased = true;
			}
			Ride.Frame();
			if (Ride.IsAirborne())
			{
				bTookOff = true;
				Air += FrameSeconds;
				if (Ride.Board->Velocity.Z < 0.0f && bReleased)
				{
					Pawn->SetLoadHeld(true); // coming down: crouch for the landing
				}
				const FGrabState& Grabs = Pawn->GetGrabState();
				MaxTuck = FMath::Max(MaxTuck, Ride.Attitude->GetTuckAmount());
				if (bPressed && !bReleased)
				{
					bRotationSuppressed &= Ride.Attitude->GetLastStepDebug().ControlTorqueNm.IsNearlyZero();
				}
				if (Grabs.IsHolding() && Grabs.GetPrevReachWeight() >= 1.0f)
				{
					++HeldFrames;
					const float NoseSide = DrawnNoseSide(Pawn);
					const int32 Side = RigSide(ETrickHand::Back, NoseSide);
					const FVector Socket = BoardGrabPoints::SocketWorld(Pawn->GetBoardVisual()->GetComponentTransform(), ETrickGrabZone::ToeEdge, ETrickHand::Back, NoseSide);
					WorstHandCm = FMath::Max(WorstHandCm, static_cast<float>(FVector::Dist(Pawn->GetRiderRigPose().Arms[Side].End, Socket)));
					float FrontFoot = 0.0f;
					float BackFoot = 0.0f;
					StrapErrors(Pawn, FrontFoot, BackFoot);
					WorstFrontFootCm = FMath::Max(WorstFrontFootCm, FrontFoot);
					WorstBackFootCm = FMath::Max(WorstBackFootCm, BackFoot);
					MaxPullCm = FMath::Max(MaxPullCm, static_cast<float>(Pawn->GetGrabBoardPullCm().Size()));
					// The other hand stays on the bar: its two ends are 50 cm apart and the hand is within reach of the bar's line.
					const FVector BarLeft = Ride.Kite->GetBarEndWorldPosition(true);
					const FVector BarRight = Ride.Kite->GetBarEndWorldPosition(false);
					const FVector OtherHand = Pawn->GetRiderRigPose().Arms[1 - Side].End;
					const FVector Closest = FMath::ClosestPointOnSegment(OtherHand, BarLeft, BarRight);
					bBarHandStayed &= FVector::Dist(OtherHand, Closest) < 2.5f;
				}
				const FString Now = Ride.HUD->GetTrickTickerText();
				if (!Now.IsEmpty())
				{
					Ticker = Now;
				}
			}
			else if (bTookOff)
			{
				bLanded = true;
				Card = Ride.HUD->GetJumpCardText();
				break;
			}
		}
		Pawn->SetLoadHeld(false);
		FJumpRecord Record;
		Ride.Tracker->GetLastJumpRecord(Record);
		AddInfo(FString::Printf(TEXT("Indy ride: air %.2f s, %d held frames, hand off the socket %.2f cm at worst, feet off the straps %.3f / %.3f cm, board pulled %.1f cm, tuck %.2f; record '%s' %s, %d grab(s) (hold %.2f s); ticker '%s'; card '%s'"),
			Air, HeldFrames, WorstHandCm, WorstFrontFootCm, WorstBackFootCm, MaxPullCm, MaxTuck, *Record.TrickName, *UEnum::GetValueAsString(Record.Grade),
			Record.Grabs.Num(), Record.Grabs.Num() > 0 ? Record.Grabs[0].HoldSeconds : 0.0f, *Ticker, *Card.Replace(TEXT("\n"), TEXT(" / "))));
		if (!TestTrue(TEXT("Took off and landed"), bTookOff && bLanded && bReleased))
		{
			return false;
		}
		TestTrue(FString::Printf(TEXT("The hand held the board for most of the 0.8 s press (%d frames)"), HeldFrames), HeldFrames >= 30);
		TestTrue(FString::Printf(TEXT("The back hand is within 3 cm of the Indy socket while held (%.2f cm)"), WorstHandCm), WorstHandCm < 3.0f);
		TestTrue(FString::Printf(TEXT("The front foot stays in its strap (%.3f cm)"), WorstFrontFootCm), WorstFrontFootCm < 0.5f);
		TestTrue(FString::Printf(TEXT("The back foot stays in its strap (%.3f cm)"), WorstBackFootCm), WorstBackFootCm < 0.5f);
		TestTrue(TEXT("The front hand stays on the bar"), bBarHandStayed);
		TestTrue(FString::Printf(TEXT("The board came up to the hand (%.1f cm), within the limit"), MaxPullCm), MaxPullCm > 5.0f && MaxPullCm <= Pawn->GrabMaxBoardPullCm + 0.01f);
		TestTrue(FString::Printf(TEXT("The grab tucked the body (%.2f)"), MaxTuck), MaxTuck > 0.9f);
		TestTrue(TEXT("While the grab button was held the stick did not rotate the rider"), bRotationSuppressed);
		TestEqual(TEXT("One grab in the record"), Record.Grabs.Num(), 1);
		if (Record.Grabs.Num() == 1)
		{
			TestEqual(TEXT("with the back hand"), Record.Grabs[0].Hand, ETrickHand::Back);
			TestEqual(TEXT("on the toe edge"), Record.Grabs[0].Zone, ETrickGrabZone::ToeEdge);
			TestNearlyEqual(TEXT("held 0.6 s after the 0.2 s reach (s)"), Record.Grabs[0].HoldSeconds, 0.6f, 0.05f);
		}
		TestEqual(TEXT("The record names it Indy"), Record.TrickName, FString(TEXT("Indy")));
		TestEqual(TEXT("The ticker named it in the air"), Ticker, FString(TEXT("Indy")));
		TestTrue(FString::Printf(TEXT("The card names it ('%s')"), *Card), Card.StartsWith(TEXT("Indy  ")));
		TestTrue(TEXT("The grab scores technicality"), Record.Score.Technicality > 0.2f - 1e-4f);
	}

	// The tuck: the same pre-wind back roll twice, once with the back hand grabbing (default zone, the
	// toe edge) from 0.15 s after the take-off. With the grab the rider turns faster.
	float MeanRate[2] = { 0.0f, 0.0f };
	float MeanInertia[2] = { 0.0f, 0.0f };
	FString Names[2];
	for (int32 WithGrab = 0; WithGrab < 2; ++WithGrab)
	{
		FGrabRide Ride;
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		AKiteRiderPawn* Pawn = Ride.Pawn;
		Ride.SendAndPop(FVector2D(1.0f, 0.0f));
		float Air = 0.0f;
		bool bTookOff = false;
		int32 Samples = 0;
		const float StartAt = Pawn->GetSimTimeSeconds();
		while (!Ride.HasReached(StartAt + 15.0f))
		{
			if (WithGrab == 1 && bTookOff && Air >= 0.15f && Air < 0.15f + 0.5f * FrameSeconds)
			{
				Pawn->SetTrickInput(false, true, false);
			}
			if (WithGrab == 1 && Air >= 1.2f)
			{
				Pawn->SetTrickInput(false, false, false);
			}
			Ride.Frame();
			if (Ride.IsAirborne())
			{
				bTookOff = true;
				Air += FrameSeconds;
				if (Air >= 0.5f && Air < 1.0f)
				{
					MeanRate[WithGrab] += FMath::RadiansToDegrees(static_cast<float>(Ride.Attitude->GetAngularVelocity().Size()));
					const FVector I = Ride.Attitude->GetBodyInertiaKgM2();
					MeanInertia[WithGrab] += static_cast<float>(I.X);
					++Samples;
				}
				if (Ride.Board->Velocity.Z < 0.0f && Air > 1.2f)
				{
					Pawn->SetLoadHeld(true);
				}
			}
			else if (bTookOff)
			{
				break;
			}
		}
		Pawn->SetLoadHeld(false);
		if (Samples > 0)
		{
			MeanRate[WithGrab] /= Samples;
			MeanInertia[WithGrab] /= Samples;
		}
		FJumpRecord Record;
		Ride.Tracker->GetLastJumpRecord(Record);
		Names[WithGrab] = Record.TrickName;
	}
	AddInfo(FString::Printf(TEXT("Back roll 0.5..1.0 s after take-off: %.0f deg/s (I front %.1f kg m2, '%s') without the grab, %.0f deg/s (I %.1f, '%s') with it: x%.2f"),
		MeanRate[0], MeanInertia[0], *Names[0], MeanRate[1], MeanInertia[1], *Names[1], MeanRate[0] > 0.0f ? MeanRate[1] / MeanRate[0] : 0.0f));
	TestTrue(FString::Printf(TEXT("The grab tucks: less inertia (%.1f < %.1f kg m2)"), MeanInertia[1], MeanInertia[0]), MeanInertia[1] < 0.8f * MeanInertia[0]);
	TestTrue(FString::Printf(TEXT("and the rider rotates faster with the grab (%.0f > 1.2 x %.0f deg/s)"), MeanRate[1], MeanRate[0]), MeanRate[1] > 1.2f * MeanRate[0]);
	return true;
}

// The one-footer: the back foot leaves its strap while held and must be back by the touchdown. Back
// in time, the landing stands and the jump is a one-footer; on its way back in, the landing is
// sketchy (FootLate); still out, it is a crash (FootOutOfStrap), on the card as well.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickOneFooterFootReturns, "KiteSurf.Trick.OneFooterFootReturns", TrickGrabTest::Flags)

bool FKiteSurfTrickOneFooterFootReturns::RunTest(const FString& Parameters)
{
	using namespace TrickGrabTest;
	const float Hz = 240.0f;
	const float Dt = 1.0f / Hz;

	// Pure: the foot's state at touchdown after letting go.
	auto FootAfter = [Dt, Hz](float HeldSeconds, float AfterSeconds, FGrabState& State)
	{
		FGrabStateInput In;
		In.bAirborne = true;
		State.Step(In, Dt);
		In.bOneFoot = true;
		for (int32 Step = 0; Step < FMath::RoundToInt(HeldSeconds * Hz); ++Step)
		{
			State.Step(In, Dt);
		}
		In.bOneFoot = false;
		for (int32 Step = 0; Step < FMath::RoundToInt(AfterSeconds * Hz); ++Step)
		{
			State.Step(In, Dt);
		}
		return State.GetFootAtTouchdown();
	};
	{
		FGrabState State;
		TestEqual(TEXT("Held 0.6 s, let go 0.2 s before: back in"), FootAfter(0.6f, 0.2f, State), EFootStrapState::In);
		TestTrue(FString::Printf(TEXT("and a one-footer (%.2f s out)"), State.GetOneFootSeconds()), State.IsOneFooter());
	}
	{
		FGrabState State;
		TestEqual(TEXT("Let go 0.1 s before: on its way back"), FootAfter(0.6f, 0.1f, State), EFootStrapState::Returning);
	}
	{
		FGrabState State;
		TestEqual(TEXT("Let go 0.05 s before: still out"), FootAfter(0.6f, 0.05f, State), EFootStrapState::Out);
	}
	{
		FGrabState State;
		TestEqual(TEXT("Never let go: out"), FootAfter(0.6f, 0.0f, State), EFootStrapState::Out);
	}
	{
		FGrabState State;
		FootAfter(0.3f, 0.2f, State);
		TestFalse(FString::Printf(TEXT("Held 0.3 s: out only %.2f s, not a one-footer"), State.GetOneFootSeconds()), State.IsOneFooter());
	}

	// Pure: the evaluator grades the foot.
	{
		FLandingInputs Good;
		Good.KiteElevationDeg = 60.0f;
		Good.LandingG = 2.0f;
		TestEqual(TEXT("Foot in: a level landing is stomped"), LandingEvaluator::Evaluate(Good).Grade, ELandingGrade::Stomped);
		FLandingInputs Late = Good;
		Late.BackFoot = EFootStrapState::Returning;
		const FLandingVerdict LateVerdict = LandingEvaluator::Evaluate(Late);
		TestEqual(TEXT("Foot on its way back: sketchy"), LateVerdict.Grade, ELandingGrade::Sketchy);
		TestEqual(TEXT("cause FootLate"), LateVerdict.Cause, ELandingCause::FootLate);
		FLandingInputs Out = Good;
		Out.BackFoot = EFootStrapState::Out;
		const FLandingVerdict OutVerdict = LandingEvaluator::Evaluate(Out);
		TestEqual(TEXT("Foot out: a crash"), OutVerdict.Grade, ELandingGrade::Crash);
		TestEqual(TEXT("cause FootOutOfStrap"), OutVerdict.Cause, ELandingCause::FootOutOfStrap);
	}

	// The ride, twice: C pressed 0.2 s after the take-off and let go about half a second before the
	// touchdown (the attitude's time to contact), or held to the water.
	for (int32 HoldToWater = 0; HoldToWater < 2; ++HoldToWater)
	{
		FGrabRide Ride;
		if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
		{
			return false;
		}
		AKiteRiderPawn* Pawn = Ride.Pawn;
		Ride.SendAndPop(FVector2D::ZeroVector);
		float Air = 0.0f;
		bool bTookOff = false;
		bool bLanded = false;
		bool bPressed = false;
		bool bReleased = false;
		float ReleasedTtc = 0.0f;
		float MinBackFootOutCm = TNumericLimits<float>::Max();
		float WorstFrontFootCm = 0.0f;
		float FootBackInCm = -1.0f;
		FString Card;
		const float StartAt = Pawn->GetSimTimeSeconds();
		while (!Ride.HasReached(StartAt + 15.0f))
		{
			const float Ttc = Ride.Attitude->GetLastStepDebug().TimeToContactSeconds;
			if (bTookOff && !bPressed && Air >= 0.2f)
			{
				Pawn->OnOneFootPressed(FInputActionValue(true));
				bPressed = true;
			}
			if (HoldToWater == 0 && bPressed && !bReleased && Air >= 1.0f && Ride.Board->Velocity.Z < 0.0f && Ttc < 0.5f)
			{
				Pawn->OnOneFootReleased(FInputActionValue(false));
				bReleased = true;
				ReleasedTtc = Ttc;
			}
			Ride.Frame();
			if (Ride.IsAirborne())
			{
				bTookOff = true;
				Air += FrameSeconds;
				if (Ride.Board->Velocity.Z < 0.0f)
				{
					Pawn->SetLoadHeld(true);
				}
				float FrontFoot = 0.0f;
				float BackFoot = 0.0f;
				StrapErrors(Pawn, FrontFoot, BackFoot);
				if (Pawn->GetGrabState().GetFootOut() >= 1.0f && Pawn->GetGrabState().GetPrevFootOut() >= 1.0f)
				{
					MinBackFootOutCm = FMath::Min(MinBackFootOutCm, BackFoot);
					WorstFrontFootCm = FMath::Max(WorstFrontFootCm, FrontFoot);
				}
				if (bReleased && Pawn->GetGrabState().GetFootOut() <= 0.0f && Pawn->GetGrabState().GetPrevFootOut() <= 0.0f)
				{
					FootBackInCm = BackFoot;
				}
			}
			else if (bTookOff)
			{
				bLanded = true;
				Card = Ride.HUD->GetJumpCardText();
				break;
			}
		}
		Pawn->SetLoadHeld(false);
		Pawn->OnOneFootReleased(FInputActionValue(false));
		const FLandingVerdict Verdict = Ride.Board->GetLastLandingVerdict();
		FJumpRecord Record;
		Ride.Tracker->GetLastJumpRecord(Record);
		const FString What = HoldToWater == 0 ? TEXT("Let go before the touchdown") : TEXT("Held to the water");
		AddInfo(FString::Printf(TEXT("%s: air %.2f s, let go at a time to contact of %.2f s; back ankle %.1f cm from its strap while out, front %.3f cm; back in %.3f cm; verdict %s (%s); record '%s', one-footer %d (%.2f s out); card '%s'"),
			*What, Air, ReleasedTtc, MinBackFootOutCm, WorstFrontFootCm, FootBackInCm, *UEnum::GetValueAsString(Verdict.Grade), *UEnum::GetValueAsString(Verdict.Cause),
			*Record.TrickName, Record.bOneFooter, Record.OneFootSeconds, *Card.Replace(TEXT("\n"), TEXT(" / "))));
		if (!TestTrue(FString::Printf(TEXT("%s: took off and came down"), *What), bTookOff && bLanded))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: the back ankle left its strap (%.1f cm)"), *What, MinBackFootOutCm), MinBackFootOutCm >= 20.0f && MinBackFootOutCm < 1000.0f);
		TestTrue(FString::Printf(TEXT("%s: the front foot stayed in its strap (%.3f cm)"), *What, WorstFrontFootCm), WorstFrontFootCm < 0.5f);
		TestTrue(FString::Printf(TEXT("%s: the record is a one-footer"), *What), Record.bOneFooter);
		if (HoldToWater == 0)
		{
			TestTrue(FString::Printf(TEXT("%s: the back foot went back in its strap (%.3f cm)"), *What, FootBackInCm), FootBackInCm >= 0.0f && FootBackInCm < 0.5f);
			TestNotEqual(FString::Printf(TEXT("%s: the landing stands"), *What), Verdict.Grade, ELandingGrade::Crash);
			TestTrue(FString::Printf(TEXT("%s: and the foot is not the cause"), *What), Verdict.Cause != ELandingCause::FootOutOfStrap && Verdict.Cause != ELandingCause::FootLate);
			TestEqual(FString::Printf(TEXT("%s: landed"), *What), Record.Outcome, EJumpOutcome::Landed);
			TestEqual(FString::Printf(TEXT("%s: named a one-footer"), *What), Record.TrickName, FString(TEXT("One-footer")));
			TestTrue(TEXT("The one-footer scores technicality"), Record.Score.Technicality >= FTrickScoringSettings().OneFooter - 1e-4f);
		}
		else
		{
			TestEqual(FString::Printf(TEXT("%s: a crash"), *What), Verdict.Grade, ELandingGrade::Crash);
			TestEqual(FString::Printf(TEXT("%s: because the foot was out"), *What), Verdict.Cause, ELandingCause::FootOutOfStrap);
			TestEqual(FString::Printf(TEXT("%s: recorded as a crash"), *What), Record.Outcome, EJumpOutcome::Crashed);
			TestEqual(FString::Printf(TEXT("%s: the record has the cause"), *What), Record.LandingCause, ELandingCause::FootOutOfStrap);
			TestTrue(FString::Printf(TEXT("%s: the card says why ('%s')"), *What, *Card), AKiteSurfHUD::FormatJumpCard(Record).EndsWith(TEXT("\nBack foot still out of the strap")));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
