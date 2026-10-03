#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurf.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfHUD.h"
#include "KiteSurfUnits.h"
#include "WindComponent.h"
#include "InputActionValue.h"
#include "School/LessonCatalog.h"
#include "School/LessonDirector.h"
#include "Tricks/BarState.h"
#include "Tricks/FreestyleHeat.h"
#include "Tricks/FreestyleHeatSubsystem.h"
#include "Tricks/FreestyleScoring.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/RiderAttitudeComponent.h"
#include "Tricks/TrickNaming.h"
#include "Tricks/TrickRecognition.h"
#include "Tricks/TrickSessionSubsystem.h"
#include "Tricks/TrickTrackerComponent.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfMenuNavigator.h"
#include "UI/KiteSurfPauseMenuWidget.h"
#include "UI/KiteSurfSaveGame.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the freestyle heat (T3.6, docs/tricks/T3.md, docs/tricks.md 3.6 and 6.8): the pure
// FFreestyleHeat rules (what is an attempt, the countdown, the attempt limit), the heat subsystem
// following a rider's trick tracker on a spawned pawn, the HUD's heat lines, the pause-menu entry and
// the local best in the save game. KiteSurf.Trick.FreestyleHeatScore (the pure ScoreFreestyleHeat) is
// in TrickFreestyleTests.cpp. Nothing here touches the player's own "Settings" slot: test worlds have
// no game instance, and saves go through memory or the "FreestyleHeatBestAutomationTest" slot, which
// is deleted afterwards.
namespace TrickHeatTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const FString TestSlot = TEXT("FreestyleHeatBestAutomationTest");

	FTrickPass Pass(ETrickSense Sense, int32 Degrees)
	{
		FTrickPass Result;
		Result.Sense = Sense;
		Result.Degrees = Degrees;
		Result.Kind = ETrickPassKind::Air;
		return Result;
	}

	/** A finished unhooked jump as the tracker hands it over: took off at TakeoffS, in the air AirtimeS, ApexCm high, graded Grade. */
	FJumpRecord MakeJump(const TCHAR* Name, float TakeoffS = 10.0f, float AirtimeS = 1.2f, float ApexCm = 200.0f,
		ELandingGrade Grade = ELandingGrade::Clean, bool bHooked = false)
	{
		FJumpRecord Record;
		Record.TrickName = Name;
		Record.bHooked = bHooked;
		Record.Outcome = Grade == ELandingGrade::Crash ? EJumpOutcome::Crashed : EJumpOutcome::Landed;
		Record.Grade = Grade;
		Record.TakeoffTimeSeconds = TakeoffS;
		Record.AirtimeSeconds = AirtimeS;
		Record.LandingTimeSeconds = TakeoffS + AirtimeS;
		Record.ApexHeightCm = ApexCm;
		return Record;
	}

	/** A KGB as the record carries it: a back roll with a backside handle pass of 360. */
	FJumpRecord MakeKgb(float TakeoffS, float ApexCm, ELandingGrade Grade)
	{
		FJumpRecord Record = MakeJump(TEXT("KGB"), TakeoffS, 1.2f, ApexCm, Grade);
		Record.Inversions.Add(ETrickInversion::BackRoll);
		Record.Passes.Add(Pass(ETrickSense::Backside, 360));
		return Record;
	}

	/** A mobe: a back roll with a frontside pass of 360. */
	FJumpRecord MakeMobe(float TakeoffS, float ApexCm, ELandingGrade Grade)
	{
		FJumpRecord Record = MakeJump(TEXT("Mobe"), TakeoffS, 1.2f, ApexCm, Grade);
		Record.Inversions.Add(ETrickInversion::BackRoll);
		Record.Passes.Add(Pass(ETrickSense::Frontside, 360));
		return Record;
	}

	/** A frontside 3: no inversion, a frontside pass of 360 (Combos). */
	FJumpRecord MakeFrontside3(float TakeoffS, float ApexCm, ELandingGrade Grade)
	{
		FJumpRecord Record = MakeJump(TEXT("Frontside 3"), TakeoffS, 1.2f, ApexCm, Grade);
		Record.Passes.Add(Pass(ETrickSense::Frontside, 360));
		return Record;
	}

	FString FamilyName(EGkaFamily Family)
	{
		return StaticEnum<EGkaFamily>()->GetNameStringByValue(static_cast<int64>(Family));
	}

	/** Archive that writes like UGameplayStatics::SaveGameToMemory but leaves one property out: how a save from an older build looks. */
	class FSkipPropertyArchive : public FObjectAndNameAsStringProxyArchive
	{
	public:
		FSkipPropertyArchive(FArchive& InInner, FName InSkipped)
			: FObjectAndNameAsStringProxyArchive(InInner, false)
			, Skipped(InSkipped)
		{
		}

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
		{
			return (InProperty && InProperty->GetFName() == Skipped) || FObjectAndNameAsStringProxyArchive::ShouldSkipProperty(InProperty);
		}

	private:
		FName Skipped;
	};

	/** The object part of a save game blob, written as SaveGameToMemory writes it, optionally without one property. */
	TArray<uint8> SerializeObject(USaveGame& Save, FName Skipped = NAME_None)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		if (Skipped.IsNone())
		{
			FObjectAndNameAsStringProxyArchive Ar(Writer, false);
			Save.Serialize(Ar);
		}
		else
		{
			FSkipPropertyArchive Ar(Writer, Skipped);
			Save.Serialize(Ar);
		}
		return Bytes;
	}

	/**
	 * A rider on the water in steady wind along +X, started as the game mode starts a ride, with the
	 * heat subsystem stepped after each pawn tick and a HUD in the same world (TrickUnhookTests.cpp's
	 * FUnhookRide plus the subsystem).
	 */
	struct FHeatRide
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		UTrickTrackerComponent* Tracker = nullptr;
		UFreestyleHeatSubsystem* Heats = nullptr;
		AKiteSurfHUD* HUD = nullptr;
		float FrameSeconds = 1.0f / 60.0f;

		explicit FHeatRide(float WindKnots, float KiteM2 = 0.0f)
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
			Heats = World->GetSubsystem<UFreestyleHeatSubsystem>();
			HUD = World->SpawnActor<AKiteSurfHUD>();
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
			Kite->SetKiteSize(KiteM2 > 0.0f ? KiteM2 : UKiteComponent::RecommendKiteSizeM2(WindKnots));
			Pawn->bInterpolateRendering = false;
		}

		~FHeatRide()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Tracker && Heats && HUD; }

		void Frame()
		{
			Pawn->Tick(FrameSeconds);
			Heats->StepHeat(FrameSeconds);
		}

		bool HasReached(float SimSeconds) const { return Pawn->GetSimTimeSeconds() + 0.5f * Pawn->SimStepSeconds >= SimSeconds; }

		void SimulateUntil(float SimSeconds)
		{
			while (!HasReached(SimSeconds))
			{
				Frame();
			}
		}

		bool IsAirborne() const { return Board->GetBoardState() == EBoardState::Airborne; }

		/** The HUD's notice line after it has read the heat's notices. */
		FString Notice()
		{
			HUD->UpdateHeatNotice();
			return HUD->GetJumpRejectionText();
		}
	};
}

// The tests sit inside the namespace rather than under a using-directive, which would leak into the
// next file of a unity build.
namespace TrickHeatTest
{

// What counts as an attempt: an unhooked jump of more than 0.4 s, or any crash (a crash scores 0);
// hooked landings, short unhooked hops and jumps from before the start do not; the heat ends at its
// attempt limit and takes nothing after.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickFreestyleHeatAttempts, "KiteSurf.Trick.FreestyleHeatAttempts", TrickHeatTest::Flags)

bool FKiteSurfTrickFreestyleHeatAttempts::RunTest(const FString& Parameters)
{
	// Classify, record by record.
	TestEqual(TEXT("An unhooked jump of 1.2 s is an attempt"), FFreestyleHeat::Classify(MakeJump(TEXT("Pop"))), EHeatRecordVerdict::Attempt);
	TestEqual(TEXT("An unhooked hop of exactly 0.4 s is not"), FFreestyleHeat::Classify(MakeJump(TEXT("Pop"), 10.0f, 0.4f)), EHeatRecordVerdict::TooShort);
	TestEqual(TEXT("0.41 s is"), FFreestyleHeat::Classify(MakeJump(TEXT("Pop"), 10.0f, 0.41f)), EHeatRecordVerdict::Attempt);
	TestEqual(TEXT("A landed hooked jump is not, however long"),
		FFreestyleHeat::Classify(MakeJump(TEXT("Back roll"), 10.0f, 3.0f, 900.0f, ELandingGrade::Stomped, true)), EHeatRecordVerdict::Hooked);
	TestEqual(TEXT("A hooked crash is"), FFreestyleHeat::Classify(MakeJump(TEXT("Back roll"), 10.0f, 3.0f, 900.0f, ELandingGrade::Crash, true)),
		EHeatRecordVerdict::Crash);
	{
		FJumpRecord BarLost = MakeJump(TEXT("Pop"), 10.0f, 0.3f, 100.0f, ELandingGrade::Crash);
		BarLost.LandingCause = ELandingCause::BarLost;
		TestEqual(TEXT("A bar lost is a crash, even on a short hop"), FFreestyleHeat::Classify(BarLost), EHeatRecordVerdict::Crash);
		BarLost.Outcome = EJumpOutcome::Landed;
		BarLost.Grade = ELandingGrade::Sketchy;
		TestEqual(TEXT("A bar-lost cause alone makes it a crash"), FFreestyleHeat::Classify(BarLost), EHeatRecordVerdict::Crash);
	}
	{
		FJumpRecord GradedCrash = MakeJump(TEXT("Pop"));
		GradedCrash.Grade = ELandingGrade::Crash;
		TestEqual(TEXT("A landing the board graded a crash is a crash"), FFreestyleHeat::Classify(GradedCrash), EHeatRecordVerdict::Crash);
	}

	// Scoring a record: the signature rebuilt from the record, the family and difficulty from naming.
	{
		const FJumpRecord Kgb = MakeKgb(10.0f, 300.0f, ELandingGrade::Clean);
		const FScoredTrick Scored = FFreestyleHeat::ScoreRecord(Kgb);
		TestEqual(TEXT("A KGB record is in the KGB family"), FamilyName(Scored.Family), FamilyName(EGkaFamily::KgbSlim));
		// 2 x 4.0 x 0.85 (clean) x (0.8 + 0.1 x 3 m) = 7.48
		TestNearlyEqual(TEXT("and scores 2 x difficulty x execution x height (pts)"), Scored.Score, 7.48f, 1e-4f);
		TestEqual(TEXT("with the record's own name"), Scored.Name, FString(TEXT("KGB")));
		FLandingVerdict Verdict;
		Verdict.Grade = ELandingGrade::Clean;
		TestNearlyEqual(TEXT("the same as FreestyleTrickScore on the record's signature (pts)"), Scored.Score,
			FreestyleScoring::FreestyleTrickScore(TrickRecognition::SignatureFromJump(Kgb), Verdict, 3.0f), 1e-5f);
		FJumpRecord Crashed = MakeKgb(10.0f, 300.0f, ELandingGrade::Crash);
		TestEqual(TEXT("The same trick crashed scores 0 (pts)"), FFreestyleHeat::ScoreRecord(Crashed).Score, 0.0f);
		const FScoredTrick Plain = FFreestyleHeat::ScoreRecord(MakeJump(TEXT("Unhooked pop")));
		TestEqual(TEXT("A plain unhooked pop has no family"), FamilyName(Plain.Family), FamilyName(EGkaFamily::None));
	}

	// A heat of three, started at board time 100 s.
	FFreestyleHeat Heat;
	TestEqual(TEXT("Idle before the start"), Heat.GetPhase(), EFreestyleHeatPhase::Idle);
	TestEqual(TEXT("An idle heat takes nothing"), Heat.OfferRecord(MakeKgb(110.0f, 300.0f, ELandingGrade::Clean)), EHeatRecordVerdict::OutsideHeat);
	Heat.Start(3, 100.0f);
	TestTrue(TEXT("Running"), Heat.IsActive());
	TestEqual(TEXT("Three attempts"), Heat.GetAttemptLimit(), 3);
	TestEqual(TEXT("On trick 1"), Heat.GetCurrentTrickNumber(), 1);
	TestEqual(TEXT("A jump that took off before the start is not taken"), Heat.OfferRecord(MakeKgb(99.5f, 300.0f, ELandingGrade::Clean)),
		EHeatRecordVerdict::OutsideHeat);
	TestEqual(TEXT("A hooked landing is ignored"), Heat.OfferRecord(MakeJump(TEXT("Back roll"), 101.0f, 2.5f, 800.0f, ELandingGrade::Stomped, true)),
		EHeatRecordVerdict::Hooked);
	TestEqual(TEXT("A short unhooked hop is ignored"), Heat.OfferRecord(MakeJump(TEXT("Pop"), 102.0f, 0.3f, 20.0f)), EHeatRecordVerdict::TooShort);
	TestEqual(TEXT("None of them used an attempt"), Heat.GetAttemptCount(), 0);

	TestEqual(TEXT("A KGB is attempt 1"), Heat.OfferRecord(MakeKgb(103.0f, 300.0f, ELandingGrade::Clean)), EHeatRecordVerdict::Attempt);
	TestEqual(TEXT("now on trick 2"), Heat.GetCurrentTrickNumber(), 2);
	TestNearlyEqual(TEXT("The total is the KGB plus the one-family bonus (pts)"), Heat.GetTotal(), 7.48f + 1.0f, 1e-4f);

	TestEqual(TEXT("A crash is attempt 2"), Heat.OfferRecord(MakeMobe(106.0f, 400.0f, ELandingGrade::Crash)), EHeatRecordVerdict::Crash);
	if (TestEqual(TEXT("Two attempts"), Heat.GetAttemptCount(), 2))
	{
		TestEqual(TEXT("The crash is kept as a crash"), Heat.GetAttempts()[1].Kind, EHeatAttemptKind::Crash);
		TestEqual(TEXT("scoring 0 (pts)"), Heat.GetAttempts()[1].Trick.Score, 0.0f);
	}
	TestNearlyEqual(TEXT("A crash adds nothing (pts)"), Heat.GetTotal(), 8.48f, 1e-4f);

	TestEqual(TEXT("An unhooked pop with no trick is attempt 3"), Heat.OfferRecord(MakeJump(TEXT("Unhooked pop"), 109.0f)), EHeatRecordVerdict::Attempt);
	TestTrue(TEXT("The third attempt ends a three-attempt heat"), Heat.IsFinished());
	TestEqual(TEXT("Trick 3/3 at the end"), Heat.GetCurrentTrickNumber(), 3);
	TestNearlyEqual(TEXT("A pop with no family never counts (pts)"), Heat.GetTotal(), 8.48f, 1e-4f);
	TestEqual(TEXT("After the end nothing is taken"), Heat.OfferRecord(MakeMobe(112.0f, 400.0f, ELandingGrade::Stomped)), EHeatRecordVerdict::OutsideHeat);
	TestEqual(TEXT("and the attempts stay three"), Heat.GetAttemptCount(), 3);

	// A heat of seven that sees eight tricks: the eighth is never offered as an attempt.
	FFreestyleHeat Seven;
	Seven.Settings.TrickCountdownSeconds = 0.0f;
	Seven.Start(FFreestyleHeat::DefaultAttempts);
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Seven.OfferRecord(MakeKgb(1.0f + Index * 5.0f, 300.0f + Index * 10.0f, ELandingGrade::Clean));
	}
	TestEqual(TEXT("A default heat takes seven attempts"), Seven.GetAttemptCount(), 7);
	TestTrue(TEXT("and is over"), Seven.IsFinished());
	TestEqual(TEXT("Seven KGBs: only the best counts (one family)"), Seven.GetResult().CountingIdx.Num(), 1);
	TestEqual(TEXT("the best is the highest, the seventh"), Seven.GetResult().CountingIdx.Num() > 0 ? Seven.GetResult().CountingIdx[0] : -1, 6);

	// The limit is clamped.
	FFreestyleHeat Clamped;
	Clamped.Start(0);
	TestEqual(TEXT("At least one attempt"), Clamped.GetAttemptLimit(), 1);
	Clamped.Start(99);
	TestEqual(TEXT("At most MaxAttempts"), Clamped.GetAttemptLimit(), FFreestyleHeat::MaxAttempts);
	Clamped.Cancel();
	TestEqual(TEXT("A cancelled heat is idle"), Clamped.GetPhase(), EFreestyleHeatPhase::Idle);
	return true;
}

// The trick countdown (GKA format): 90 s per attempt, reset by each attempt; an attempt is lost when it
// runs out with nothing tried; a jump in the air holds it until its record decides; off at 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickFreestyleHeatCountdown, "KiteSurf.Trick.FreestyleHeatCountdown", TrickHeatTest::Flags)

bool FKiteSurfTrickFreestyleHeatCountdown::RunTest(const FString& Parameters)
{
	FFreestyleHeat Heat;
	TestEqual(TEXT("The countdown is 90 s by default (s)"), Heat.Settings.TrickCountdownSeconds, 90.0f);
	Heat.Start(3, 0.0f);
	TestTrue(TEXT("and on"), Heat.IsCountdownOn());
	TestEqual(TEXT("Full at the start (s)"), Heat.GetCountdownLeft(), 90.0f);
	Heat.Tick(0.0f);
	TestEqual(TEXT("A paused step (0 s) does not run it (s)"), Heat.GetCountdownLeft(), 90.0f);
	Heat.Tick(89.0f);
	TestNearlyEqual(TEXT("It runs with the clock (s)"), Heat.GetCountdownLeft(), 1.0f, 1e-4f);
	TestEqual(TEXT("No attempt used yet"), Heat.GetAttemptCount(), 0);
	Heat.Tick(1.5f);
	if (TestEqual(TEXT("Running out on the water loses the attempt"), Heat.GetAttemptCount(), 1))
	{
		TestEqual(TEXT("as timed out"), Heat.GetAttempts()[0].Kind, EHeatAttemptKind::TimedOut);
		TestEqual(TEXT("scoring 0 (pts)"), Heat.GetAttempts()[0].Trick.Score, 0.0f);
		TestEqual(TEXT("with no family"), FamilyName(Heat.GetAttempts()[0].Trick.Family), FamilyName(EGkaFamily::None));
	}
	TestEqual(TEXT("The countdown starts again for the next attempt (s)"), Heat.GetCountdownLeft(), 90.0f);
	TestEqual(TEXT("on trick 2"), Heat.GetCurrentTrickNumber(), 2);

	// An attempt resets it.
	Heat.Tick(50.0f);
	Heat.OfferRecord(MakeKgb(140.0f, 300.0f, ELandingGrade::Clean));
	TestEqual(TEXT("A trick is attempt 2"), Heat.GetAttemptCount(), 2);
	TestEqual(TEXT("and resets the countdown (s)"), Heat.GetCountdownLeft(), 90.0f);

	// Running out with a jump in the air that took off in the heat: held until the record decides.
	const FJumpRecord InAir = MakeJump(TEXT(""), 229.5f);
	Heat.Tick(89.8f, &InAir);
	Heat.Tick(0.5f, &InAir);
	TestTrue(TEXT("A jump in the air holds a countdown that has run out"), Heat.IsCountdownHeld());
	TestEqual(TEXT("at 0 (s)"), Heat.GetCountdownLeft(), 0.0f);
	TestEqual(TEXT("without losing the attempt"), Heat.GetAttemptCount(), 2);
	Heat.Tick(2.0f, &InAir);
	TestEqual(TEXT("however long it stays up"), Heat.GetAttemptCount(), 2);
	// The jump lands hooked: not an attempt, so on the next step on the water the attempt is lost.
	TestEqual(TEXT("It lands hooked: no attempt"), Heat.OfferRecord(MakeJump(TEXT("Back roll"), 229.5f, 2.0f, 600.0f, ELandingGrade::Clean, true)),
		EHeatRecordVerdict::Hooked);
	Heat.Tick(1.0f / 240.0f, nullptr);
	TestEqual(TEXT("then the attempt is lost on the water"), Heat.GetAttemptCount(), 3);
	TestEqual(TEXT("as timed out"), Heat.GetAttempts().Last().Kind, EHeatAttemptKind::TimedOut);
	TestTrue(TEXT("and that was the last of three"), Heat.IsFinished());
	TestNearlyEqual(TEXT("The total is the KGB and its bonus (pts)"), Heat.GetTotal(), 8.48f, 1e-4f);

	// A held countdown and a jump that is an attempt: it counts.
	FFreestyleHeat Held;
	Held.Start(2, 0.0f);
	const FJumpRecord Flying = MakeJump(TEXT(""), 89.0f);
	Held.Tick(90.5f, &Flying);
	TestTrue(TEXT("Held by the jump in the air"), Held.IsCountdownHeld());
	TestEqual(TEXT("Its landing is the attempt"), Held.OfferRecord(MakeMobe(89.0f, 400.0f, ELandingGrade::Stomped)), EHeatRecordVerdict::Attempt);
	Held.Tick(0.01f, nullptr);
	TestEqual(TEXT("and nothing is lost"), Held.GetAttemptCount(), 1);
	TestEqual(TEXT("it is a trick"), Held.GetAttempts()[0].Kind, EHeatAttemptKind::Trick);
	TestFalse(TEXT("the countdown runs again"), Held.IsCountdownHeld());

	// A jump that took off before the start does not hold it.
	FFreestyleHeat Early;
	Early.Start(2, 100.0f);
	const FJumpRecord FromBefore = MakeJump(TEXT(""), 99.0f);
	Early.Tick(91.0f, &FromBefore);
	TestEqual(TEXT("A jump from before the start holds nothing: the attempt is lost"), Early.GetAttemptCount(), 1);

	// Off.
	FFreestyleHeat Off;
	Off.Settings.TrickCountdownSeconds = 0.0f;
	Off.Start(3);
	TestFalse(TEXT("At 0 the countdown is off"), Off.IsCountdownOn());
	Off.Tick(1000.0f);
	TestEqual(TEXT("and nothing is lost to time"), Off.GetAttemptCount(), 0);
	TestEqual(TEXT("It reads 0 when off (s)"), Off.GetCountdownLeft(), 0.0f);

	// Finished: the results clock runs.
	FFreestyleHeat One;
	One.Start(1);
	One.Tick(90.0f);
	TestTrue(TEXT("A one-attempt heat ends when its countdown runs out"), One.IsFinished());
	One.Tick(5.0f);
	TestNearlyEqual(TEXT("and counts the seconds since (s)"), One.GetSecondsSinceFinished(), 5.0f, 1e-4f);
	return true;
}

// The HUD's heat lines: the row ("Trick 3/7", the countdown, the total with the bonus), the counting
// list with families, and the results card with the families, the bonus and the local best.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickFreestyleHeatHUD, "KiteSurf.Trick.FreestyleHeatHUD", TrickHeatTest::Flags)

bool FKiteSurfTrickFreestyleHeatHUD::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Family labels are short"), FreestyleHeat::FamilyLabel(EGkaFamily::HinterHeart), FString(TEXT("Hinter/Heart")));
	TestEqual(TEXT("None has a dash"), FreestyleHeat::FamilyLabel(EGkaFamily::None), FString(TEXT("--")));
	TestEqual(TEXT("A whole bonus has no decimals"), FreestyleHeat::FormatBonus(7.0f), FString(TEXT("7")));
	TestEqual(TEXT("A tuned bonus shows one"), FreestyleHeat::FormatBonus(2.5f), FString(TEXT("2.5")));

	FFreestyleHeat Heat;
	Heat.Start(4, 0.0f);
	Heat.Tick(18.4f);
	TestEqual(TEXT("An empty heat's row"), FString::Join(AKiteSurfHUD::FormatHeatRow(Heat), TEXT("|")),
		FString(TEXT("FREESTYLE HEAT|Trick 1/4|1:12|TOTAL 0.0")));
	TestEqual(TEXT("An empty counting list"), FString::Join(AKiteSurfHUD::FormatHeatList(Heat), TEXT("|")),
		FString(TEXT("COUNTING|1  --|2  --|3  --|4  --")));

	Heat.OfferRecord(MakeKgb(20.0f, 300.0f, ELandingGrade::Clean));        // 7.48, KGB/Slim
	Heat.OfferRecord(MakeMobe(30.0f, 400.0f, ELandingGrade::Stomped));     // 2 x 4 x 1 x 1.2 = 9.6, Mobes
	Heat.OfferRecord(MakeFrontside3(40.0f, 200.0f, ELandingGrade::Clean)); // 2 x 3 x 0.85 x 1.0 = 5.1, Combos
	TestEqual(TEXT("Three families: the bonus is 4 and the row shows it"), FString::Join(AKiteSurfHUD::FormatHeatRow(Heat), TEXT("|")),
		FString(TEXT("FREESTYLE HEAT|Trick 4/4|1:30|TOTAL 26.2 (+4 variety)")));
	TestEqual(TEXT("The counting list: best first, with the score and the family"), FString::Join(AKiteSurfHUD::FormatHeatList(Heat), TEXT("|")),
		FString(TEXT("COUNTING|1  Mobe  9.6  [Mobes]|2  KGB  7.5  [KGB/Slim]|3  Frontside 3  5.1  [Combos]|4  --")));

	FFreestyleHeat NoClock;
	NoClock.Settings.TrickCountdownSeconds = 0.0f;
	NoClock.Start(7);
	TestEqual(TEXT("With the countdown off the row has no clock"), FString::Join(AKiteSurfHUD::FormatHeatRow(NoClock), TEXT("|")),
		FString(TEXT("FREESTYLE HEAT|Trick 1/7|TOTAL 0.0")));

	Heat.OfferRecord(MakeKgb(50.0f, 300.0f, ELandingGrade::Crash));
	TestTrue(TEXT("The crash is the fourth and last attempt"), Heat.IsFinished());
	const FString NewBest = FString::Join(AKiteSurfHUD::FormatHeatResults(Heat, 20.0f, true, true), TEXT("|"));
	TestEqual(TEXT("Results: a new best over a previous one"), NewBest,
		FString(TEXT("FREESTYLE HEAT OVER  (4 tricks)|TOTAL  26.2|NEW BEST|1  Mobe  9.6 pts  Mobes|2  KGB  7.5 pts  KGB/Slim|3  Frontside 3  5.1 pts  Combos|Families  3: Mobes, KGB/Slim, Combos|Variety bonus  +4|Previous best  20.0")));
	const FString NotBeaten = FString::Join(AKiteSurfHUD::FormatHeatResults(Heat, 30.5f, true, false), TEXT("|"));
	TestTrue(TEXT("Results: not beaten shows no badge and the local best"), !NotBeaten.Contains(TEXT("NEW BEST")) && NotBeaten.EndsWith(TEXT("|Local best  30.5")));
	const FString First = FString::Join(AKiteSurfHUD::FormatHeatResults(Heat, 0.0f, false, true), TEXT("|"));
	TestTrue(TEXT("Results: the first heat of an attempt count"), First.Contains(TEXT("|NEW BEST|")) && First.EndsWith(TEXT("|First 4-trick heat")));

	FFreestyleHeat Empty;
	Empty.Start(1);
	Empty.Tick(90.0f);
	TestEqual(TEXT("Results with nothing counted"), FString::Join(AKiteSurfHUD::FormatHeatResults(Empty, 0.0f, false, false), TEXT("|")),
		FString(TEXT("FREESTYLE HEAT OVER  (1 trick)|TOTAL  0.0|No tricks counted|Families  0|Variety bonus  +0|First 1-trick heat")));

	// A HUD in a world with no heat shows nothing.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteSurfHUD* HUD = World ? World->SpawnActor<AKiteSurfHUD>() : nullptr;
	if (TestNotNull(TEXT("HUD spawned"), HUD))
	{
		UFreestyleHeatSubsystem* Heats = World->GetSubsystem<UFreestyleHeatSubsystem>();
		if (TestNotNull(TEXT("A game world has the heat subsystem"), Heats))
		{
			TestEqual(TEXT("No heat, no heat lines"), HUD->GetHeatLines().Num(), 0);
			TestEqual(TEXT("Starting with no rider in the world fails"), Heats->StartHeat(3), EHeatStartResult::NoRider);
			TestEqual(TEXT("and still shows nothing"), HUD->GetHeatLines().Num(), 0);
		}
	}
	if (World)
	{
		World->DestroyWorld(false);
	}
	return true;
}

// On a spawned pawn: a hooked pop during a heat is no attempt and says "Unhook for freestyle"; then an
// unhooked pop with a handle pass (TrickUnhookTests' PassOnRide) is attempt 1, scored from its record;
// the trick countdown loses the rest, and the results card comes up.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickFreestyleHeatOnPawn, "KiteSurf.Trick.FreestyleHeatOnPawn", TrickHeatTest::Flags)

bool FKiteSurfTrickFreestyleHeatOnPawn::RunTest(const FString& Parameters)
{
	// 1. Hooked: the pop is ignored.
	{
		FHeatRide Ride(20.0f, 9.0f);
		if (!TestTrue(TEXT("Hooked ride set up"), Ride.IsValid()))
		{
			return false;
		}
		Ride.SimulateUntil(0.5f);
		TestEqual(TEXT("The heat starts for the rider the subsystem finds"), Ride.Heats->StartHeat(3), EHeatStartResult::Started);
		TestEqual(TEXT("Hooked in at the start, the notice says to unhook"), Ride.Notice(), FString(UFreestyleHeatSubsystem::UnhookNotice));
		Ride.HUD->ShowNotice(FString());
		Ride.SimulateUntil(8.0f);
		Ride.Board->SetWeightShift(-1.0f);
		Ride.Pawn->SetLoadHeld(true);
		Ride.SimulateUntil(8.5f);
		const int32 Records = Ride.Tracker->GetJumpRecordCount();
		Ride.Pawn->ReleaseLoadAndPop();
		Ride.Board->SetWeightShift(0.0f);
		const float GiveUp = Ride.Pawn->GetSimTimeSeconds() + 6.0f;
		while (Ride.Tracker->GetJumpRecordCount() == Records && !Ride.HasReached(GiveUp))
		{
			Ride.Frame();
		}
		FJumpRecord Record;
		if (TestTrue(TEXT("The hooked pop was recorded"), Ride.Tracker->GetJumpRecordCount() > Records && Ride.Tracker->GetLastJumpRecord(Record)))
		{
			AddInfo(FString::Printf(TEXT("Hooked pop: \"%s\" %.2f s, %.1f m, %s"), *Record.TrickName, Record.AirtimeSeconds,
				KiteUnits::CmToM(Record.ApexHeightCm), *UEnum::GetDisplayValueAsText(Record.Grade).ToString()));
			TestTrue(TEXT("hooked in"), Record.bHooked);
			TestTrue(FString::Printf(TEXT("a real jump (%.2f s)"), Record.AirtimeSeconds), Record.AirtimeSeconds > 0.4f);
			if (Record.Outcome == EJumpOutcome::Landed)
			{
				TestEqual(TEXT("A landed hooked jump is no attempt"), Ride.Heats->GetHeat().GetAttemptCount(), 0);
				TestEqual(TEXT("and the notice says to unhook"), Ride.Notice(), FString(UFreestyleHeatSubsystem::UnhookNotice));
				const TArray<FString> Lines = Ride.HUD->GetHeatLines();
				TestTrue(TEXT("The HUD is still on trick 1/3"), Lines.Num() > 1 && Lines[1] == TEXT("Trick 1/3"));
			}
		}
	}

	// 2. Unhooked with a pass: attempt 1, then the countdown.
	FHeatRide Ride(20.0f, 9.0f);
	if (!TestTrue(TEXT("Unhooked ride set up"), Ride.IsValid()))
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	Ride.SimulateUntil(0.25f);
	const float CountdownSeconds = 5.0f;
	TestEqual(TEXT("A three-attempt heat with a 5 s countdown starts"), Ride.Heats->StartHeat(3, Ride.Tracker, CountdownSeconds), EHeatStartResult::Started);
	TestEqual(TEXT("The settings keep the default countdown for the next heat (s)"), Ride.Heats->GetSettings().TrickCountdownSeconds, 90.0f);
	TArray<FString> Lines = Ride.HUD->GetHeatLines();
	TestEqual(TEXT("The HUD row and list at the start"), FString::Join(Lines, TEXT("|")),
		FString(TEXT("FREESTYLE HEAT|Trick 1/3|0:05|TOTAL 0.0|COUNTING|1  --|2  --|3  --|4  --")));
	Ride.SimulateUntil(0.5f);
	Pawn->PressHook();
	Ride.Frame();
	TestFalse(TEXT("Unhooked"), Pawn->IsHooked());
	Ride.SimulateUntil(4.0f);
	TestEqual(TEXT("The countdown runs on the board's clock (s)"), Ride.Heats->GetHeat().GetCountdownLeft(), CountdownSeconds - (4.0f - 0.25f), 0.02f);
	// PassOnRide's pop at 8.5 s would be past a 5 s countdown: restart the heat just before the load.
	Ride.SimulateUntil(7.9f);
	Ride.Heats->StartHeat(3, Ride.Tracker, CountdownSeconds);
	TestEqual(TEXT("Restarted with nothing in it"), Ride.Heats->GetHeat().GetAttemptCount(), 0);
	Ride.SimulateUntil(8.0f);
	const FVector2D Spin(1.0f, 1.0f);
	Ride.Board->SetWeightShift(-1.0f);
	Pawn->SetLoadHeld(true);
	Pawn->SetPreWind(Spin);
	Ride.SimulateUntil(8.5f);
	const int32 JumpsBefore = Ride.Board->GetJumpCount();
	const int32 RecordsBefore = Ride.Tracker->GetJumpRecordCount();
	Pawn->ReleaseLoadAndPop();
	Ride.Board->SetWeightShift(0.0f);
	Pawn->SetPreWind(FVector2D::ZeroVector);
	Pawn->SetAirRotationInput(Spin);
	bool bPressed = false;
	const float GiveUp = Pawn->GetSimTimeSeconds() + 5.0f;
	while (Ride.Board->GetJumpCount() == JumpsBefore && !Ride.HasReached(GiveUp))
	{
		const FVector LineDir = (Ride.Kite->GetKiteWorldPosition() - Pawn->GetActorLocation()).GetSafeNormal();
		const float BackToKite = BarStateMachine::BackToKiteDeg(Pawn->GetRiderAttitude()->GetBodyQuat(), LineDir);
		if (!bPressed && Ride.IsAirborne() && BackToKite <= Pawn->BarTunables.PassBackToKiteDeg)
		{
			Pawn->OnPassPressed(FInputActionValue(true));
			bPressed = true;
		}
		if (Pawn->GetBarState().JumpPasses.Num() > 0)
		{
			Pawn->SetAirRotationInput(FVector2D::ZeroVector);
		}
		Ride.Frame();
	}
	Pawn->SetAirRotationInput(FVector2D::ZeroVector);
	// The record is finalised on the touchdown step; one more frame and the heat has read it.
	Ride.Frame();
	FJumpRecord Record;
	if (!TestTrue(TEXT("The unhooked jump was recorded"), Ride.Tracker->GetJumpRecordCount() > RecordsBefore && Ride.Tracker->GetLastJumpRecord(Record)))
	{
		return false;
	}
	EGkaFamily Family = EGkaFamily::None;
	TrickNaming::FreestyleFamily(TrickRecognition::SignatureFromJump(Record), &Family, nullptr);
	AddInfo(FString::Printf(TEXT("Unhooked jump: \"%s\" %.2f s, %.1f m, %d pass(es), %s, family %s"), *Record.TrickName, Record.AirtimeSeconds,
		KiteUnits::CmToM(Record.ApexHeightCm), Record.Passes.Num(), *UEnum::GetDisplayValueAsText(Record.Grade).ToString(), *FamilyName(Family)));
	TestFalse(TEXT("Recorded unhooked"), Record.bHooked);
	TestTrue(FString::Printf(TEXT("in the air over 0.4 s (%.2f s)"), Record.AirtimeSeconds), Record.AirtimeSeconds > 0.4f);
	const FFreestyleHeat& Heat = Ride.Heats->GetHeat();
	if (!TestEqual(TEXT("The jump is attempt 1"), Heat.GetAttemptCount(), 1))
	{
		return false;
	}
	const FHeatAttempt& Attempt = Heat.GetAttempts()[0];
	const FScoredTrick Expected = FFreestyleHeat::ScoreRecord(Record, Ride.Tracker->GetJumpSession().GetRecorder().Settings.Scoring);
	TestEqual(TEXT("named from its record"), Attempt.Trick.Name, Record.TrickName);
	TestEqual(TEXT("in the family naming gives it"), FamilyName(Attempt.Trick.Family), FamilyName(Family));
	TestNearlyEqual(TEXT("scored with FreestyleTrickScore from its record (pts)"), Attempt.Trick.Score, Expected.Score, 1e-5f);
	TestEqual(TEXT("its kind follows the landing"), Attempt.Kind, Record.Grade == ELandingGrade::Crash ? EHeatAttemptKind::Crash : EHeatAttemptKind::Trick);
	const bool bCounts = Family != EGkaFamily::None && Attempt.Trick.Score > 0.0f;
	TestNearlyEqual(TEXT("The total is the trick and its one-family bonus, or 0 (pts)"), Heat.GetTotal(), bCounts ? Attempt.Trick.Score + 1.0f : 0.0f, 1e-4f);
	Lines = Ride.HUD->GetHeatLines();
	TestTrue(TEXT("The HUD is on trick 2/3"), Lines.Num() > 1 && Lines[1] == TEXT("Trick 2/3"));
	TestTrue(TEXT("with the countdown full again"), Lines.Num() > 2 && Lines[2] == TEXT("0:05"));

	// Riding on without trying anything: the countdown loses attempts 2 and 3, and the heat ends.
	const float LandedAt = Pawn->GetSimTimeSeconds();
	while (!Heat.IsFinished() && !Ride.HasReached(LandedAt + 3.0f * CountdownSeconds))
	{
		Ride.Frame();
	}
	TestTrue(TEXT("The heat is over"), Heat.IsFinished());
	TestEqual(TEXT("after three attempts"), Heat.GetAttemptCount(), 3);
	if (Heat.GetAttemptCount() == 3)
	{
		AddInfo(FString::Printf(TEXT("Attempts 2 and 3: %s, %s"), *UEnum::GetDisplayValueAsText(Heat.GetAttempts()[1].Kind).ToString(),
			*UEnum::GetDisplayValueAsText(Heat.GetAttempts()[2].Kind).ToString()));
		TestEqual(TEXT("Attempt 2 was lost to the countdown"), Heat.GetAttempts()[1].Kind, EHeatAttemptKind::TimedOut);
		TestEqual(TEXT("and so was attempt 3"), Heat.GetAttempts()[2].Kind, EHeatAttemptKind::TimedOut);
		TestEqual(TEXT("The notice names the lost trick"), Ride.Notice(), FString(TEXT("Trick 3 lost: time ran out")));
	}
	TestTrue(TEXT("The results card is up"), Ride.Heats->IsShowingResults());
	Lines = Ride.HUD->GetHeatLines();
	TestTrue(TEXT("with its title"), Lines.Num() > 0 && Lines[0] == TEXT("FREESTYLE HEAT OVER  (3 tricks)"));
	TestTrue(TEXT("and the total"), Lines.Num() > 1 && Lines[1] == FString::Printf(TEXT("TOTAL  %.1f"), Heat.GetTotal()));
	TestFalse(TEXT("A test world has no game instance, so no local best before"), Ride.Heats->HadPreviousBest());
	TestEqual(TEXT("so a scoring heat is a new best"), Ride.Heats->IsNewBest(), Heat.GetTotal() > 0.0f);
	TestTrue(TEXT("The card ends with the first-heat line"), Lines.Num() > 0 && Lines.Last() == TEXT("First 3-trick heat"));
	return true;
}

// The pause menu's "FREESTYLE HEAT (7 tricks)" entry, after SCHOOL, starts a seven-attempt heat; the
// items before it keep their places. One mode at a time: a heat is refused while a session or a lesson
// runs, and a running heat ends when either starts.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickFreestyleHeatStartsFromPauseMenu, "KiteSurf.Trick.FreestyleHeatStartsFromPauseMenu", TrickHeatTest::Flags)

bool FKiteSurfTrickFreestyleHeatStartsFromPauseMenu::RunTest(const FString& Parameters)
{
	FHeatRide Ride(20.0f);
	if (!TestTrue(TEXT("Ride set up"), Ride.IsValid()))
	{
		return false;
	}
	UTrickSessionSubsystem* Sessions = Ride.World->GetSubsystem<UTrickSessionSubsystem>();
	TestEqual(TEXT("The label"), UKiteSurfPauseMenuWidget::GetHeatLabel(), FString(TEXT("FREESTYLE HEAT (7 tricks)")));
	TestFalse(TEXT("No heat at first"), Ride.Heats->IsHeatActive());

	UKiteSurfPauseMenuWidget* PauseMenu = CreateWidget<UKiteSurfPauseMenuWidget>(Ride.World, UKiteSurfPauseMenuWidget::StaticClass());
	if (!TestNotNull(TEXT("Pause menu created"), PauseMenu))
	{
		return false;
	}
	FKiteMenuNavigator& Navigator = PauseMenu->GetNavigator();
	TestEqual(TEXT("Free ride: RESUME, RESTART, GEAR, SESSION, SCHOOL, FREESTYLE HEAT, SETTINGS, MAIN MENU, QUIT"), Navigator.Num(), 9);
	Navigator.Select(5);
	Navigator.HandleKey(EKeys::Enter);
	TestTrue(TEXT("The sixth entry starts a heat"), Ride.Heats->IsHeatActive());
	TestEqual(TEXT("of seven attempts"), Ride.Heats->GetHeat().GetAttemptLimit(), FFreestyleHeat::DefaultAttempts);
	TestFalse(TEXT("not a session"), Sessions && Sessions->IsSessionActive());
	TestEqual(TEXT("which the HUD shows"), FString::Join(Ride.HUD->GetHeatLines(), TEXT("|")),
		FString(TEXT("FREESTYLE HEAT|Trick 1/7|1:30|TOTAL 0.0|COUNTING|1  --|2  --|3  --|4  --")));
	Ride.Frame();
	Ride.Frame();
	TestTrue(TEXT("and which runs with the ride"), Ride.Heats->GetHeat().GetCountdownLeft() < 90.0f);

	// A session starting ends the heat, with a notice.
	if (TestNotNull(TEXT("The session subsystem"), Sessions))
	{
		TestTrue(TEXT("A session starts"), Sessions->StartSession(30.0f));
		Ride.Frame();
		TestFalse(TEXT("and the heat is cancelled"), Ride.Heats->IsHeatActive());
		TestEqual(TEXT("with nothing to show"), Ride.HUD->GetHeatLines().Num(), 0);
		TestEqual(TEXT("and a notice"), Ride.Notice(), FString(TEXT("Freestyle heat cancelled")));

		// While the session runs, the entry is refused with a notice.
		UKiteSurfPauseMenuWidget* Again = CreateWidget<UKiteSurfPauseMenuWidget>(Ride.World, UKiteSurfPauseMenuWidget::StaticClass());
		if (TestNotNull(TEXT("Pause menu again"), Again))
		{
			Again->GetNavigator().Select(5);
			Again->GetNavigator().HandleKey(EKeys::Enter);
		}
		TestFalse(TEXT("No heat during a session"), Ride.Heats->IsHeatActive());
		TestTrue(TEXT("The session runs on"), Sessions->IsSessionActive());
		TestEqual(TEXT("The notice says why"), Ride.Notice(), FString(TEXT("Finish the session before a freestyle heat")));
		TestEqual(TEXT("StartHeat says why"), Ride.Heats->StartHeat(), EHeatStartResult::SessionActive);
	}

	// A lesson: a running heat ends when one starts, and none starts while it runs.
	FHeatRide LessonRide(20.0f);
	const FLessonDef* A3 = LessonCatalog::Find(TEXT("A3"));
	if (TestTrue(TEXT("Lesson ride set up"), LessonRide.IsValid()) && TestNotNull(TEXT("Lesson A3"), A3))
	{
		TestEqual(TEXT("A heat starts in free ride"), LessonRide.Heats->StartHeat(), EHeatStartResult::Started);
		ALessonDirector* Director = ALessonDirector::StartInWorld(LessonRide.World, *A3, LessonRide.Pawn);
		if (TestNotNull(TEXT("A3 runs in the ride"), Director))
		{
			LessonRide.Frame();
			TestFalse(TEXT("The lesson ends the heat"), LessonRide.Heats->IsHeatActive());
			TestEqual(TEXT("StartHeat refuses during the lesson"), LessonRide.Heats->StartHeat(), EHeatStartResult::LessonActive);
			TestEqual(TEXT("with a notice"), LessonRide.Notice(), FString(TEXT("Leave the lesson before a freestyle heat")));
		}
	}
	return true;
}

// The local best: the game instance keeps the best total per attempt count, and it goes through the
// save game with the settings, in memory and in a test slot.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickFreestyleHeatBestPersists, "KiteSurf.Trick.FreestyleHeatBestPersists", TrickHeatTest::Flags)

bool FKiteSurfTrickFreestyleHeatBestPersists::RunTest(const FString& Parameters)
{
	UKiteSurfGameInstance* GI = NewObject<UKiteSurfGameInstance>();
	TestFalse(TEXT("A new game instance has no 7-trick best"), GI->HasBestHeatTotal(7));
	TestEqual(TEXT("and reads it as 0"), GI->GetBestHeatTotal(7), 0.0f);
	TestFalse(TEXT("A heat scoring 0 is not a best"), GI->RecordHeatTotal(7, 0.0f));
	TestFalse(TEXT("and leaves no best"), GI->HasBestHeatTotal(7));
	TestTrue(TEXT("The first scoring 7-trick heat is a best"), GI->RecordHeatTotal(7, 26.2f));
	TestFalse(TEXT("A lower one is not"), GI->RecordHeatTotal(7, 20.0f));
	TestFalse(TEXT("Nor an equal one"), GI->RecordHeatTotal(7, 26.2f));
	TestTrue(TEXT("A higher one is"), GI->RecordHeatTotal(7, 34.0f));
	TestEqual(TEXT("The best is the highest (pts)"), GI->GetBestHeatTotal(7), 34.0f);
	TestTrue(TEXT("Attempt counts are kept apart: the first 3-trick heat is a best"), GI->RecordHeatTotal(3, 12.5f));
	TestEqual(TEXT("without touching the 7-trick best (pts)"), GI->GetBestHeatTotal(7), 34.0f);
	TestFalse(TEXT("Heat bests are not session bests"), GI->HasBestSessionTotal(7));
	GI->RecordSessionTotal(90, 61.25f);

	// Through the save game, as SaveSettingsToDisk writes it and LoadSettingsFromDisk reads it, minus the disk.
	const float VolumeBefore = FApp::GetVolumeMultiplier(); // ApplySaveGame sets it from the save
	GI->PendingWindKnots = 27.0f;
	UKiteSurfSaveGame* Written = NewObject<UKiteSurfSaveGame>();
	GI->WriteToSaveGame(*Written);
	TestEqual(TEXT("WriteToSaveGame writes both heat bests"), Written->BestFreestyleHeatTotalByAttempts.Num(), 2);
	TestEqual(TEXT("and the session best beside them"), Written->BestSessionTotalBySeconds.Num(), 1);
	TArray<uint8> Bytes;
	TestTrue(TEXT("The save serialises to memory"), UGameplayStatics::SaveGameToMemory(Written, Bytes));
	UKiteSurfSaveGame* Loaded = Cast<UKiteSurfSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("The save loads back from memory"), Loaded))
	{
		FApp::SetVolumeMultiplier(VolumeBefore);
		return false;
	}
	UKiteSurfGameInstance* Reader = NewObject<UKiteSurfGameInstance>();
	Reader->ApplySaveGame(*Loaded);
	TestEqual(TEXT("The 7-trick best survives the round trip (pts)"), Reader->GetBestHeatTotal(7), 34.0f);
	TestEqual(TEXT("The 3-trick best survives the round trip (pts)"), Reader->GetBestHeatTotal(3), 12.5f);
	TestFalse(TEXT("No best appears for a count never played"), Reader->HasBestHeatTotal(5));
	TestEqual(TEXT("The session best survives next to them (pts)"), Reader->GetBestSessionTotal(90), 61.25f);
	TestEqual(TEXT("The settings survive next to the bests (kn)"), Reader->PendingWindKnots, 27.0f);
	TestFalse(TEXT("The loaded best is beaten only by more"), Reader->RecordHeatTotal(7, 33.0f));
	FApp::SetVolumeMultiplier(VolumeBefore);

	// On disk, in a slot of the test's own: the default slot is the player's real save.
	TestNotEqual(TEXT("The test slot is not the player's slot"), TestSlot, UKiteSurfSaveGame::DefaultSaveSlot);
	UGameplayStatics::DeleteGameInSlot(TestSlot, UKiteSurfSaveGame::DefaultUserIndex);
	TestTrue(TEXT("The save is written to the test slot"), Written->SaveSettings(TestSlot));
	const UKiteSurfSaveGame* FromDisk = UKiteSurfSaveGame::LoadOrCreateSettings(TestSlot);
	const float* DiskBest = FromDisk ? FromDisk->BestFreestyleHeatTotalByAttempts.Find(7) : nullptr;
	TestTrue(TEXT("The 7-trick best survives the round trip through the test slot"), DiskBest && *DiskBest == 34.0f);
	UGameplayStatics::DeleteGameInSlot(TestSlot, UKiteSurfSaveGame::DefaultUserIndex);
	TestFalse(TEXT("The test slot is cleaned up"), UGameplayStatics::DoesSaveGameExist(TestSlot, UKiteSurfSaveGame::DefaultUserIndex));
	return true;
}

// A save from before heats has no BestFreestyleHeatTotalByAttempts property: it loads with no heat best
// and its settings, session bests and trick book as they were.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickFreestyleHeatOldSaveLoads, "KiteSurf.Trick.FreestyleHeatOldSaveLoads", TrickHeatTest::Flags)

bool FKiteSurfTrickFreestyleHeatOldSaveLoads::RunTest(const FString& Parameters)
{
	const FName BestProperty = GET_MEMBER_NAME_CHECKED(UKiteSurfSaveGame, BestFreestyleHeatTotalByAttempts);

	UKiteSurfSaveGame* Save = NewObject<UKiteSurfSaveGame>();
	Save->WindStrengthKnots = 27.0f;
	Save->BoardSizeIndex = 2;
	Save->BestSessionTotalBySeconds.Add(90, 61.25f);
	Save->BestFreestyleHeatTotalByAttempts.Add(7, 34.0f);
	FJumpRecord Landed = MakeJump(TEXT("Backroll"));
	Landed.FamilyKey = TEXT("HH|L|-|I0|S-0|G");
	Save->TrickBook.RecordLanding(Landed, ETrickBoardCategory::TwinTip, FDateTime(2025, 9, 1, 12, 0, 0));
	TArray<uint8> Full;
	UGameplayStatics::SaveGameToMemory(Save, Full);

	// Rebuild it as an older build wrote it: the same header, the object without the heat bests.
	const TArray<uint8> ObjectFull = SerializeObject(*Save);
	const TArray<uint8> ObjectOld = SerializeObject(*Save, BestProperty);
	const int32 HeaderBytes = Full.Num() - ObjectFull.Num();
	const bool bSplits = HeaderBytes > 0 && FMemory::Memcmp(Full.GetData() + HeaderBytes, ObjectFull.GetData(), ObjectFull.Num()) == 0;
	if (!TestTrue(TEXT("The save is a header followed by the object, as SaveGameToMemory writes it"), bSplits))
	{
		return false;
	}
	TestTrue(TEXT("Leaving the heat bests out makes the object smaller (they are really gone)"), ObjectOld.Num() < ObjectFull.Num());
	TArray<uint8> Old(Full.GetData(), HeaderBytes);
	Old.Append(ObjectOld);

	UKiteSurfSaveGame* Loaded = Cast<UKiteSurfSaveGame>(UGameplayStatics::LoadGameFromMemory(Old));
	if (!TestNotNull(TEXT("A save without heat bests loads"), Loaded))
	{
		return false;
	}
	TestEqual(TEXT("It loads with no heat best"), Loaded->BestFreestyleHeatTotalByAttempts.Num(), 0);
	TestEqual(TEXT("Its wind is kept (kn)"), Loaded->WindStrengthKnots, 27.0f);
	TestEqual(TEXT("Its board is kept"), Loaded->BoardSizeIndex, 2);
	TestEqual(TEXT("Its session best is kept (pts)"), Loaded->BestSessionTotalBySeconds.FindRef(90), 61.25f);
	TestEqual(TEXT("Its trick book is kept"), Loaded->TrickBook.Num(), 1);

	UKiteSurfSaveGame* LoadedFull = Cast<UKiteSurfSaveGame>(UGameplayStatics::LoadGameFromMemory(Full));
	TestTrue(TEXT("The control: the same save with the property loads with its best"), LoadedFull && LoadedFull->BestFreestyleHeatTotalByAttempts.FindRef(7) == 34.0f);

	const float VolumeBefore = FApp::GetVolumeMultiplier(); // ApplySaveGame sets it from the save
	UKiteSurfGameInstance* GI = NewObject<UKiteSurfGameInstance>();
	GI->RecordHeatTotal(7, 10.0f);
	GI->ApplySaveGame(*Loaded);
	TestFalse(TEXT("A game instance loading an old save has no 7-trick best"), GI->HasBestHeatTotal(7));
	TestEqual(TEXT("and keeps its session best (pts)"), GI->GetBestSessionTotal(90), 61.25f);
	TestTrue(TEXT("Its first scoring heat is then a new best"), GI->RecordHeatTotal(7, 1.0f));
	FApp::SetVolumeMultiplier(VolumeBefore);
	return true;
}

} // namespace TrickHeatTest

#endif // WITH_DEV_AUTOMATION_TESTS
