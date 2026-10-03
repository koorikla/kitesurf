#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurf.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfHUD.h"
#include "KiteSurfUnits.h"
#include "WindComponent.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/LandingEvaluator.h"
#include "Tricks/RiderAttitudeComponent.h"
#include "Tricks/TrickTrackerComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// The trick tracker reading the rider's rotation live (T1.6) and the card's failure cause (T2.6), on
// the pawn: the phase 2 timed jump with a scripted pre-wind, as TrickRotationTests.cpp flies it (30 kn,
// the recommended kite, the jump button held through the send and let go to pop), with the HUD
// following the tracker frame by frame.

namespace TrickLiveRotationTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float FrameSeconds = 1.0f / 60.0f;

	/** A rider on the water in steady wind along +X, started on a beam reach as the game mode does. */
	struct FLiveRide
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		URiderAttitudeComponent* Attitude = nullptr;
		UTrickTrackerComponent* Tracker = nullptr;
		USpringArmComponent* Boom = nullptr;
		/** The HUD lives in its own world, as in TrickTrackerTests: it only reads the tracker. */
		UWorld* HudWorld = nullptr;
		AKiteSurfHUD* HUD = nullptr;

		explicit FLiveRide(float WindKnots)
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

		~FLiveRide()
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

		bool IsValid() const { return Pawn && Kite && Board && Attitude && Tracker && HUD; }

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
	};

	/** What a scripted jump came to, as the tracker recorded it and the HUD showed it. */
	struct FLiveJump
	{
		bool bValid = false;
		bool bTookOff = false;
		bool bLanded = false;
		float AirSeconds = 0.0f;
		int32 RecordCount = 0;
		FJumpRecord Record;
		FLandingVerdict Verdict;
		/** Every distinct ticker text seen in the air, in order. */
		TArray<FString> Tickers;
		/** The air time at which the ticker first named an inversion (s); negative when it never did. */
		float TickerInversionAt = -1.0f;
		FString Card;
		FString CardCause;
	};

	FString Join(const TArray<FString>& Lines)
	{
		return Lines.Num() == 0 ? FString(TEXT("(none)")) : FString::Join(Lines, TEXT(" | "));
	}

	/**
	 * TrickRotationTests' timed jump: 8 s of riding, then the bar hard over, the weight on the tail and
	 * the jump button held with the pre-wind stick, let go to pop 0.66 s after the bar reaches the
	 * kite; the bar centred in the air and a crouch for the landing from the apex.
	 */
	FLiveJump RunLiveJump(const FVector2D& PreWind)
	{
		FLiveJump R;
		FLiveRide Ride(30.0f);
		if (!Ride.IsValid())
		{
			return R;
		}
		R.bValid = true;
		AKiteRiderPawn* Pawn = Ride.Pawn;
		const float SendAt = 8.0f;
		Ride.SimulateUntil(SendAt);
		Pawn->SteerKite(-1.0f);
		Ride.Board->SetWeightShift(-1.0f);
		Pawn->SetLoadHeld(true);
		Pawn->SetPreWind(PreWind);
		const float ReleaseAt = SendAt + FMath::RoundToFloat((0.66f + Ride.Kite->GetSteeringDeadTimeSeconds()) * 30.0f) / 30.0f;
		while (!Ride.HasReached(ReleaseAt) && !Ride.IsAirborne())
		{
			Ride.Frame();
		}
		Pawn->SheetKite(1.0f);
		Pawn->ReleaseLoadAndPop();
		Ride.Board->SetWeightShift(0.0f);
		Pawn->SteerKite(0.0f);
		Pawn->SetPreWind(FVector2D::ZeroVector);

		bool bWasAir = false;
		const float EndAt = ReleaseAt + 20.0f;
		while (!Ride.HasReached(EndAt))
		{
			Ride.Frame();
			if (Ride.IsAirborne())
			{
				R.bTookOff = true;
				bWasAir = true;
				R.AirSeconds += FrameSeconds;
				if (Ride.Board->Velocity.Z < 0.0f)
				{
					Pawn->SetLoadHeld(true); // coming down: crouch for the landing
				}
				const FString Ticker = Ride.HUD->GetTrickTickerText();
				if (!Ticker.IsEmpty() && (R.Tickers.Num() == 0 || R.Tickers.Last() != Ticker))
				{
					R.Tickers.Add(Ticker);
				}
				if (R.TickerInversionAt < 0.0f && Ride.Tracker->GetLiveJump().Inversions.Num() > 0)
				{
					R.TickerInversionAt = R.AirSeconds;
				}
			}
			else if (bWasAir)
			{
				R.bLanded = true;
				R.Verdict = Ride.Board->GetLastLandingVerdict();
				R.Card = Ride.HUD->GetJumpCardText();
				R.CardCause = Ride.HUD->GetJumpCardCauseText();
				break;
			}
		}
		R.RecordCount = Ride.Tracker->GetJumpRecordCount();
		Ride.Tracker->GetLastJumpRecord(R.Record);
		Pawn->SetLoadHeld(false);
		return R;
	}

	FString Describe(const FLiveJump& R)
	{
		const FJumpRecord& Rec = R.Record;
		FString Inversions;
		for (const ETrickInversion Inversion : Rec.Inversions)
		{
			Inversions += (Inversions.IsEmpty() ? TEXT("") : TEXT(", ")) + UEnum::GetValueAsString(Inversion);
		}
		return FString::Printf(TEXT("air %.2f s, %d record(s): '%s' %s (verdict %s, cause %s), inversions [%s], spin %.0f deg -> %d half turns %s, heading %.0f deg, landed %s, roll start %.2f s; tickers: %s; card: '%s'"),
			R.AirSeconds, R.RecordCount, *Rec.TrickName, *UEnum::GetValueAsString(Rec.Grade), *UEnum::GetValueAsString(R.Verdict.Grade),
			*UEnum::GetValueAsString(Rec.LandingCause), *Inversions, Rec.SpinDeg, Rec.SpinHalfTurns, *UEnum::GetValueAsString(Rec.SpinSense),
			Rec.NetHeadingDeg, *UEnum::GetValueAsString(Rec.LandingStance), Rec.RollStartSinceTakeoffSeconds, *Join(R.Tickers), *R.Card.Replace(TEXT("\n"), TEXT(" / ")));
	}
}


// A scripted pre-wind back roll on the timed jump is named "Back roll" in the record, live in the
// ticker as it is counted, and on the card at the landing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickCardNamesBackRoll, "KiteSurf.Trick.CardNamesBackRoll", TrickLiveRotationTest::Flags)

bool FKiteSurfTrickCardNamesBackRoll::RunTest(const FString& Parameters)
{
	using namespace TrickLiveRotationTest;
	const FLiveJump R = RunLiveJump(FVector2D(1.0f, 0.0f));
	AddInfo(FString::Printf(TEXT("Back roll, tracked live: %s"), *Describe(R)));
	TestTrue(TEXT("Ride fixture created"), R.bValid);
	if (!TestTrue(TEXT("The rider took off and landed"), R.bTookOff && R.bLanded))
	{
		return false;
	}
	TestEqual(TEXT("One record for the jump"), R.RecordCount, 1);
	TestTrue(TEXT("The rotation was tracked"), R.Record.bRotationTracked);
	TestEqual(TEXT("One inversion in the record"), R.Record.Inversions.Num(), 1);
	if (R.Record.Inversions.Num() == 1)
	{
		TestEqual(TEXT("and it is a back roll"), R.Record.Inversions[0], ETrickInversion::BackRoll);
	}
	TestEqual(TEXT("No spin on top"), R.Record.SpinHalfTurns, 0);
	TestEqual(TEXT("Landed heelside"), R.Record.LandingStance, ETrickStance::Heelside);
	TestEqual(TEXT("The record names it"), R.Record.TrickName, FString(TEXT("Back roll")));
	TestTrue(FString::Printf(TEXT("The roll started at the take-off, with the pre-wind (%.2f s)"), R.Record.RollStartSinceTakeoffSeconds),
		R.Record.RollStartSinceTakeoffSeconds >= 0.0f && R.Record.RollStartSinceTakeoffSeconds < 0.3f);
	TestTrue(FString::Printf(TEXT("The ticker named the back roll in the air (%s)"), *Join(R.Tickers)), R.Tickers.Contains(TEXT("Back roll")));
	TestTrue(FString::Printf(TEXT("as it was counted, before the landing (%.2f of %.2f s)"), R.TickerInversionAt, R.AirSeconds),
		R.TickerInversionAt > 0.0f && R.TickerInversionAt < R.AirSeconds);
	TestTrue(FString::Printf(TEXT("The card names it ('%s')"), *R.Card), R.Card.StartsWith(TEXT("Back roll  ")));
	TestEqual(TEXT("The card is the record's"), R.Card, AKiteSurfHUD::FormatJumpCard(R.Record));
	TestTrue(TEXT("An inversion scores technicality"), R.Record.Score.Technicality > 0.0f);
	return true;
}

// The same jump with no rotation input stays a straight air: nothing inverted, no spin from the
// flight's own turn, heelside.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickStraightJumpStaysStraightAir, "KiteSurf.Trick.StraightJumpStaysStraightAir", TrickLiveRotationTest::Flags)

bool FKiteSurfTrickStraightJumpStaysStraightAir::RunTest(const FString& Parameters)
{
	using namespace TrickLiveRotationTest;
	const FLiveJump R = RunLiveJump(FVector2D::ZeroVector);
	AddInfo(FString::Printf(TEXT("Straight jump, tracked live: %s"), *Describe(R)));
	TestTrue(TEXT("Ride fixture created"), R.bValid);
	if (!TestTrue(TEXT("The rider took off and landed"), R.bTookOff && R.bLanded))
	{
		return false;
	}
	TestTrue(TEXT("The rotation was tracked"), R.Record.bRotationTracked);
	TestEqual(TEXT("No inversion"), R.Record.Inversions.Num(), 0);
	TestEqual(TEXT("No spin"), R.Record.SpinHalfTurns, 0);
	TestEqual(TEXT("Landed heelside"), R.Record.LandingStance, ETrickStance::Heelside);
	TestEqual(TEXT("Named Straight air"), R.Record.TrickName, FString(TEXT("Straight air")));
	TestEqual(TEXT("No ticker in the air"), R.Tickers.Num(), 0);
	TestTrue(FString::Printf(TEXT("The card says Straight air ('%s')"), *R.Card), R.Card.StartsWith(TEXT("Straight air  ")));
	return true;
}

// A roll that comes down 80 deg short crashes, and the card says why: the board's verdict's cause,
// one line. No wind or line torque, the assist off (TrickRotationTests' UnderRotatedRollCrashesOnPawn).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickUnderRotatedCrashShowsCause, "KiteSurf.Trick.UnderRotatedCrashShowsCause", TrickLiveRotationTest::Flags)

bool FKiteSurfTrickUnderRotatedCrashShowsCause::RunTest(const FString& Parameters)
{
	using namespace TrickLiveRotationTest;
	FLiveRide Ride(0.0f);
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	Ride.Attitude->AssistStrength = 0.0f;
	Ride.Attitude->LineTorqueScale = 0.0f;
	Ride.Attitude->TravelAlignNaturalFreqHz = 0.0f;
	Ride.Frame(); // on the water: the attitude takes the riding pose
	TestEqual(TEXT("Pop accepted"), Ride.Board->Jump(), EJumpRejectReason::None);
	const float Yaw = static_cast<float>(Pawn->GetActorRotation().Yaw);
	Pawn->SetActorLocation(FVector(Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y, 150.0f));
	Ride.Board->Velocity = FRotator(0.0f, Yaw, 0.0f).Vector() * 800.0f + FVector(0.0f, 0.0f, -300.0f);
	const FQuat Body0 = Ride.Attitude->GetBodyQuat();
	const FVector Axis = Body0.GetAxisX();
	Ride.Attitude->SetState(FQuat(Axis, FMath::DegreesToRadians(-80.0f)) * Body0, Axis * (0.3f * Ride.Attitude->InertiaStretchedKgM2.X));

	bool bLanded = false;
	for (int32 Frame = 0; Frame < 240 && !bLanded; ++Frame)
	{
		Ride.Frame();
		bLanded = !Ride.IsAirborne();
	}
	TestTrue(TEXT("Came down to the water"), bLanded);
	const FLandingVerdict Verdict = Ride.Board->GetLastLandingVerdict();
	TestEqual(TEXT("The board's verdict: a crash"), Verdict.Grade, ELandingGrade::Crash);
	TestEqual(TEXT("under-rotated"), Verdict.Cause, ELandingCause::UnderRotated);

	FJumpRecord Record;
	TestTrue(TEXT("The tracker recorded the jump"), Ride.Tracker->GetLastJumpRecord(Record));
	const FString Card = AKiteSurfHUD::FormatJumpCard(Record);
	AddInfo(FString::Printf(TEXT("Under-rotated crash: '%s' %s, cause %s, apex %.0f cm, %d inversion(s); card '%s'; HUD card '%s'"), *Record.TrickName,
		*UEnum::GetValueAsString(Record.Grade), *UEnum::GetValueAsString(Record.LandingCause), Record.ApexHeightCm, Record.Inversions.Num(),
		*Card.Replace(TEXT("\n"), TEXT(" / ")), *Ride.HUD->GetJumpCardText().Replace(TEXT("\n"), TEXT(" / "))));
	TestEqual(TEXT("Recorded as a crash"), Record.Outcome, EJumpOutcome::Crashed);
	TestEqual(TEXT("graded a crash"), Record.Grade, ELandingGrade::Crash);
	TestEqual(TEXT("The record carries the verdict's cause"), Record.LandingCause, ELandingCause::UnderRotated);
	TestEqual(TEXT("Nothing inverted: it never got upright and over"), Record.Inversions.Num(), 0);
	TestTrue(FString::Printf(TEXT("The card's last line is the cause ('%s')"), *Card), Card.EndsWith(TEXT("\nUnder-rotated: commit the roll earlier")));

	// On the HUD: the card is up when the jump cleared a metre (the card's threshold), and either way
	// the record's card shows the cause.
	if (Record.ApexHeightCm >= 100.0f)
	{
		TestEqual(TEXT("The HUD card shows the cause line"), Ride.HUD->GetJumpCardCauseText(), FString(TEXT("Under-rotated: commit the roll earlier")));
	}
	Ride.HUD->ShowJumpCard(Record);
	TestEqual(TEXT("The card shows one cause line"), Ride.HUD->GetJumpCardCauseText(), FString(TEXT("Under-rotated: commit the roll earlier")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
