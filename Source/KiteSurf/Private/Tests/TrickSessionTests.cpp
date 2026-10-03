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
#include "Tricks/JumpRecord.h"
#include "Tricks/SessionScoring.h"
#include "Tricks/TrickSessionSubsystem.h"
#include "Tricks/TrickTrackerComponent.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfPauseMenuWidget.h"
#include "UI/KiteSurfSaveGame.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the best-three session (T2.5, docs/tricks/T2.md, backlog F5): the pure FBestThreeSession
// rules, the session subsystem following a rider's trick tracker on a spawned pawn, the HUD's session
// lines, and the local best in the save game. Nothing here touches the player's own "Settings" slot:
// test worlds have no game instance, and saves go through memory or the "SessionBestAutomationTest"
// slot, which is deleted afterwards. The ride fixture and the timed send-and-pop are minimal copies of
// TrickTrackerTests.cpp's FTrickRideFixture and RunTrackedJump.
namespace TrickSessionTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const FString TestSlot = TEXT("SessionBestAutomationTest");

	/** A finished jump as the tracker would hand it over: family Key, raw Score, took off at TakeoffS and landed 3 s later. */
	FJumpRecord MakeJump(const TCHAR* Key, float Score, float TakeoffS = 10.0f, EJumpOutcome Outcome = EJumpOutcome::Landed, float ApexCm = 800.0f)
	{
		FJumpRecord Record;
		Record.FamilyKey = Key;
		Record.TrickName = FString::Printf(TEXT("Trick %s"), Key);
		Record.Outcome = Outcome;
		Record.Grade = Outcome == EJumpOutcome::Crashed ? ELandingGrade::Crash : ELandingGrade::Clean;
		Record.Score.Total = Score;
		Record.TakeoffTimeSeconds = TakeoffS;
		Record.LandingTimeSeconds = TakeoffS + 3.0f;
		Record.AirtimeSeconds = 3.0f;
		Record.ApexHeightCm = ApexCm;
		return Record;
	}

	/** The family keys of the counting jumps, best first. */
	FString CountingKeys(const FBestThreeSession& Session)
	{
		TArray<FString> Keys;
		for (const FJumpRecord& Jump : Session.GetCounting())
		{
			Keys.Add(Jump.FamilyKey);
		}
		return FString::Join(Keys, TEXT(","));
	}

	const float RideDeltaTime = 1.0f / 60.0f;

	/** A pawn in a throwaway world, set up the way the game mode starts a ride, in steady wind along +X; a HUD in the same world. */
	struct FSessionRideFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		UTrickTrackerComponent* Tracker = nullptr;
		UTrickSessionSubsystem* Sessions = nullptr;
		AKiteSurfHUD* HUD = nullptr;

		explicit FSessionRideFixture(float WindKnots = 30.0f)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (Pawn)
			{
				Kite = Pawn->GetKite();
				Board = Pawn->GetBoardMovement();
				Tracker = Pawn->GetTrickTracker();
				Sessions = World->GetSubsystem<UTrickSessionSubsystem>();
				HUD = World->SpawnActor<AKiteSurfHUD>();
				if (UWindComponent* Wind = Pawn->GetWind())
				{
					Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(WindKnots), 0.0f, 0.0f);
					Wind->GustStrength = 0.0f;
					Wind->DirectionDriftDeg = 0.0f;
				}
				AKiteSurfGameMode::InitializeRide(Pawn, KiteUnits::KnotsToCmS(12.0f), 1.0f);
				Kite->bParkHoldAssist = true;
				Pawn->bInterpolateRendering = false;
				Kite->SetKiteModel(EKiteModel::Loop);
				Kite->SetKiteSize(UKiteComponent::RecommendKiteSizeM2(WindKnots));
			}
		}

		~FSessionRideFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Tracker && Sessions && HUD; }

		/** Ticks the pawn and then steps the session, as the world would with the subsystem ticking after the pawn. */
		void Simulate(float Seconds)
		{
			const int32 Steps = FMath::RoundToInt(Seconds / RideDeltaTime);
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				Pawn->Tick(RideDeltaTime);
				Sessions->StepSession(RideDeltaTime);
			}
		}
	};

	/** RideLoopTests' TimedReleaseSeconds: the release of the jump button after the send reaches the kite (s). */
	constexpr float TimedReleaseSeconds = 0.66f;

	/** What a session around one scripted jump did. */
	struct FSessionRun
	{
		bool bValid = false;
		bool bStarted = false;
		bool bCameDown = false;
		bool bSawOvertime = false;
		bool bFinished = false;
		/** The session clock against the board's: elapsed minus board time since the start (s). */
		float ClockDriftSeconds = 0.0f;
		/** When the jump took off and landed, in session seconds. */
		float TakeoffSessionSeconds = 0.0f;
		float LandingSessionSeconds = 0.0f;
		int32 RecordCount = 0;
		FJumpRecord Record;
		int32 SessionJumpsAfterLanding = 0;
		float TotalAfterLanding = 0.0f;
		float TotalAtEnd = 0.0f;
		bool bNewBest = false;
		bool bHadPreviousBest = true;
		TArray<FString> LinesAtStart;
		TArray<FString> LinesAfterLanding;
		TArray<FString> LinesAtResults;
	};

	/**
	 * Settles the ride for 8 s, starts a session of SessionSeconds (the subsystem finds the rider
	 * itself), flies RunTrackedJump's timed send and pop with a crouched landing, then rides on until
	 * the session's results are up (at most SessionSeconds + 20 s).
	 */
	FSessionRun RunSessionJump(float SessionSeconds)
	{
		FSessionRun Out;
		FSessionRideFixture Ride(30.0f);
		if (!Ride.IsValid())
		{
			return Out;
		}
		Out.bValid = true;
		Ride.Simulate(8.0f);

		const float StartBoardSeconds = Ride.Board->GetSimTimeSeconds();
		Out.bStarted = Ride.Sessions->StartSession(SessionSeconds);
		Out.LinesAtStart = Ride.HUD->GetSessionLines();
		const FBestThreeSession& Session = Ride.Sessions->GetSession();

		Ride.Pawn->SteerKite(-1.0f);
		const float SendDeadTimeSeconds = Ride.Kite->GetSteeringDeadTimeSeconds();
		Ride.Board->SetWeightShift(-1.0f);
		Ride.Pawn->SetLoadHeld(true);
		bool bLeftWater = false;
		float AirSeconds = 0.0f;
		for (float Elapsed = 0.0f; Elapsed < 60.0f; Elapsed += RideDeltaTime)
		{
			Ride.Simulate(RideDeltaTime);
			const bool bAir = Ride.Board->GetBoardState() == EBoardState::Airborne;
			Out.bSawOvertime |= Session.GetPhase() == EBestThreePhase::Overtime;
			if (bAir && !bLeftWater)
			{
				bLeftWater = true;
				Ride.Board->SetWeightShift(0.0f);
				Ride.Pawn->SetLoadHeld(false);
				Ride.Pawn->SheetKite(1.0f);
			}
			if (!bLeftWater && Elapsed >= TimedReleaseSeconds + SendDeadTimeSeconds)
			{
				Ride.Board->SetWeightShift(-1.0f);
				Ride.Pawn->SheetKite(1.0f);
				Ride.Pawn->ReleaseLoadAndPop();
				Ride.Board->SetWeightShift(0.0f);
				bLeftWater = true;
			}
			if (Ride.Kite->GetClockDeg() < 0.0f)
			{
				Ride.Pawn->SteerKite(0.0f); // keep the kite overhead
			}
			if (bAir && Ride.Board->Velocity.Z < 0.0f)
			{
				Ride.Pawn->SetLoadHeld(true); // coming down: crouch for the landing
			}
			if (bAir)
			{
				AirSeconds += RideDeltaTime;
			}
			if (bLeftWater && !bAir && AirSeconds > 0.3f)
			{
				Out.bCameDown = true;
				break;
			}
		}
		Ride.Pawn->SetLoadHeld(false);
		Ride.Pawn->SteerKite(0.0f);

		Out.RecordCount = Ride.Tracker->GetJumpRecordCount();
		Ride.Tracker->GetLastJumpRecord(Out.Record);
		Out.TakeoffSessionSeconds = Session.SessionTimeOf(Out.Record.TakeoffTimeSeconds);
		Out.LandingSessionSeconds = Session.SessionTimeOf(Out.Record.LandingTimeSeconds);
		Out.SessionJumpsAfterLanding = Session.GetJumps().Num();
		Out.TotalAfterLanding = Session.GetTotal();
		Out.LinesAfterLanding = Ride.HUD->GetSessionLines();
		Out.ClockDriftSeconds = Session.GetElapsed() - (Ride.Board->GetSimTimeSeconds() - StartBoardSeconds);

		for (float Elapsed = 0.0f; Elapsed < SessionSeconds + 20.0f && !Ride.Sessions->IsShowingResults(); Elapsed += RideDeltaTime)
		{
			Ride.Simulate(RideDeltaTime);
			Out.bSawOvertime |= Session.GetPhase() == EBestThreePhase::Overtime;
		}
		Out.bFinished = Session.IsFinished();
		Out.TotalAtEnd = Session.GetTotal();
		Out.bNewBest = Ride.Sessions->IsNewBest();
		Out.bHadPreviousBest = Ride.Sessions->HadPreviousBest();
		Out.LinesAtResults = Ride.HUD->GetSessionLines();
		return Out;
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
}

using namespace TrickSessionTest;

// The total is the best three paid scores, one per family key; repeats are paid by the session's own
// repeat count and a repeat of the same quality never raises the total (backlog F5).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSessionBestThree, "KiteSurf.Trick.SessionBestThree", TrickSessionTest::Flags)

bool FKiteSurfTrickSessionBestThree::RunTest(const FString& Parameters)
{
	FBestThreeSession Session;
	TestEqual(TEXT("A new session is idle"), Session.GetPhase(), EBestThreePhase::Idle);
	TestFalse(TEXT("An idle session takes no jumps"), Session.AddJump(MakeJump(TEXT("A"), 10.0f)));
	Session.Start(90.0f);
	TestEqual(TEXT("Started, it runs"), Session.GetPhase(), EBestThreePhase::Running);
	TestEqual(TEXT("with the whole time left (s)"), Session.GetTimeLeft(), 90.0f);

	TestTrue(TEXT("A jump in time is taken"), Session.AddJump(MakeJump(TEXT("A"), 10.0f)));
	Session.AddJump(MakeJump(TEXT("B"), 8.0f, 20.0f));
	Session.AddJump(MakeJump(TEXT("C"), 6.0f, 30.0f));
	Session.AddJump(MakeJump(TEXT("D"), 5.0f, 40.0f));
	TestEqual(TEXT("A10 B8 C6 D5: the best three sum to 24"), Session.GetTotal(), 24.0f);
	TestEqual(TEXT("The counting three are A, B, C, best first"), CountingKeys(Session), FString(TEXT("A,B,C")));
	TestEqual(TEXT("Four jumps were taken"), Session.GetJumps().Num(), 4);

	// Repeats of A: paid 0.75, 0.5, then 0.25 of their raw score. None beats the counted 10.
	TestTrue(TEXT("A repeat is taken"), Session.AddJump(MakeJump(TEXT("A"), 10.0f, 50.0f)));
	TestEqual(TEXT("The session paid the second A x0.75"), Session.GetJumps().Last().RepeatFactor, 0.75f);
	TestEqual(TEXT("An identical repeat does not raise the total (exactly 24)"), Session.GetTotal(), 24.0f);
	Session.AddJump(MakeJump(TEXT("A"), 12.0f, 55.0f));
	TestEqual(TEXT("The third A is paid x0.5"), Session.GetJumps().Last().RepeatFactor, 0.5f);
	TestEqual(TEXT("A better third A (12 x 0.5 = 6) does not raise it either"), Session.GetTotal(), 24.0f);
	Session.AddJump(MakeJump(TEXT("A"), 30.0f, 60.0f));
	TestEqual(TEXT("The fourth A is paid x0.25"), Session.GetJumps().Last().RepeatFactor, 0.25f);
	TestEqual(TEXT("Nor a much bigger fourth A (30 x 0.25 = 7.5)"), Session.GetTotal(), 24.0f);
	TestEqual(TEXT("Still A, B, C counting"), CountingKeys(Session), FString(TEXT("A,B,C")));
	for (const FJumpRecord& Counting : Session.GetCounting())
	{
		TestEqual(FString::Printf(TEXT("Counting %s is its first, full-value landing"), *Counting.FamilyKey), Counting.RepeatFactor, 1.0f);
	}

	// One family alone: three identical straight jumps count once, not 10 + 7.5 + 5.
	FBestThreeSession Single;
	Single.Start(90.0f);
	Single.AddJump(MakeJump(TEXT("S"), 10.0f, 5.0f));
	Single.AddJump(MakeJump(TEXT("S"), 10.0f, 15.0f));
	Single.AddJump(MakeJump(TEXT("S"), 10.0f, 25.0f));
	TestEqual(TEXT("Three repeats of one family total one landing (10)"), Single.GetTotal(), 10.0f);
	TestEqual(TEXT("and only one jump counts"), Single.GetCounting().Num(), 1);

	// A repeat counts only by beating the counted landing once devalued: 14 x 0.75 = 10.5 > 10.
	FBestThreeSession Better;
	Better.Start(90.0f);
	Better.AddJump(MakeJump(TEXT("A"), 10.0f, 5.0f));
	Better.AddJump(MakeJump(TEXT("B"), 8.0f, 10.0f));
	Better.AddJump(MakeJump(TEXT("A"), 13.0f, 15.0f));
	TestEqual(TEXT("A repeat of 13 (9.75 paid) leaves the total at 18"), Better.GetTotal(), 18.0f);
	Better.AddJump(MakeJump(TEXT("C"), 1.0f, 20.0f));
	FBestThreeSession Beaten;
	Beaten.Start(90.0f);
	Beaten.AddJump(MakeJump(TEXT("A"), 10.0f, 5.0f));
	Beaten.AddJump(MakeJump(TEXT("A"), 14.0f, 15.0f));
	TestNearlyEqual(TEXT("A repeat of 14 (10.5 paid) replaces the 10: GKA's best version (pts)"), Beaten.GetTotal(), 10.5f, 1e-4f);
	TestEqual(TEXT("and still counts as one jump"), Beaten.GetCounting().Num(), 1);

	// Free-ride repeats do not carry into the session: the session pays its own factor.
	FBestThreeSession Fresh;
	Fresh.Start(90.0f);
	FJumpRecord Devalued = MakeJump(TEXT("A"), 10.0f, 5.0f);
	Devalued.RepeatFactor = 0.25f; // what free ride paid it
	Fresh.AddJump(Devalued);
	TestEqual(TEXT("A family's first landing in the session is paid in full"), Fresh.GetJumps().Last().RepeatFactor, 1.0f);
	TestEqual(TEXT("and counts in full (pts)"), Fresh.GetTotal(), 10.0f);

	// A hop under a metre is ignored and does not use up a family's full-value landing.
	FBestThreeSession Hops;
	Hops.Start(90.0f);
	TestFalse(TEXT("A 60 cm hop is not taken"), Hops.AddJump(MakeJump(TEXT("S"), 0.5f, 5.0f, EJumpOutcome::Landed, 60.0f)));
	Hops.AddJump(MakeJump(TEXT("S"), 10.0f, 15.0f));
	TestEqual(TEXT("The jump after the hop is paid in full"), Hops.GetJumps().Last().RepeatFactor, 1.0f);

	// Restarting forgets the jumps and the repeats.
	Session.Start(60.0f);
	TestEqual(TEXT("A restart forgets the jumps"), Session.GetJumps().Num(), 0);
	TestEqual(TEXT("and the total"), Session.GetTotal(), 0.0f);
	Session.AddJump(MakeJump(TEXT("A"), 10.0f, 5.0f));
	TestEqual(TEXT("and the repeats: A is paid in full again"), Session.GetJumps().Last().RepeatFactor, 1.0f);
	return true;
}

// A crash is taken but scores 0: it never counts and does not use up the family's full-value landing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSessionCrashScoresZero, "KiteSurf.Trick.SessionCrashScoresZero", TrickSessionTest::Flags)

bool FKiteSurfTrickSessionCrashScoresZero::RunTest(const FString& Parameters)
{
	FBestThreeSession Session;
	Session.Start(90.0f);
	Session.AddJump(MakeJump(TEXT("A"), 10.0f, 5.0f));
	Session.AddJump(MakeJump(TEXT("B"), 8.0f, 10.0f));

	// Even a crash record carrying a raw score pays nothing.
	TestTrue(TEXT("A crash in time is taken"), Session.AddJump(MakeJump(TEXT("E"), 20.0f, 15.0f, EJumpOutcome::Crashed)));
	TestEqual(TEXT("It is in the jump list"), Session.GetJumps().Num(), 3);
	TestEqual(TEXT("It is paid 0"), FBestThreeSession::Paid(Session.GetJumps().Last()), 0.0f);
	TestEqual(TEXT("The total is unchanged by a crash (pts)"), Session.GetTotal(), 18.0f);
	TestEqual(TEXT("Only the two landings count"), CountingKeys(Session), FString(TEXT("A,B")));

	// Landing E after crashing it: still its first landing, paid in full.
	Session.AddJump(MakeJump(TEXT("E"), 9.0f, 20.0f));
	TestEqual(TEXT("The crash did not use up E's first landing (x1)"), Session.GetJumps().Last().RepeatFactor, 1.0f);
	TestEqual(TEXT("A10 E9 B8 count: 27"), Session.GetTotal(), 27.0f);
	TestEqual(TEXT("in that order"), CountingKeys(Session), FString(TEXT("A,E,B")));

	// A crashed repeat of A pays nothing and leaves A's repeat count alone.
	Session.AddJump(MakeJump(TEXT("A"), 30.0f, 25.0f, EJumpOutcome::Crashed));
	Session.AddJump(MakeJump(TEXT("A"), 10.0f, 30.0f));
	TestEqual(TEXT("A's landing after its crash is its second (x0.75)"), Session.GetJumps().Last().RepeatFactor, 0.75f);
	TestEqual(TEXT("The total is still 27"), Session.GetTotal(), 27.0f);

	// Only crashes: nothing counts.
	FBestThreeSession Crashes;
	Crashes.Start(90.0f);
	Crashes.AddJump(MakeJump(TEXT("A"), 10.0f, 5.0f, EJumpOutcome::Crashed));
	Crashes.AddJump(MakeJump(TEXT("B"), 10.0f, 10.0f, EJumpOutcome::Crashed));
	TestEqual(TEXT("A session of crashes totals 0"), Crashes.GetTotal(), 0.0f);
	TestEqual(TEXT("and has no counting jumps"), Crashes.GetCounting().Num(), 0);
	return true;
}

// The horn: a jump in the air at the horn counts if it took off before it; a later take-off never does.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSessionTakeoffBeforeHorn, "KiteSurf.Trick.SessionTakeoffBeforeHorn", TrickSessionTest::Flags)

bool FKiteSurfTrickSessionTakeoffBeforeHorn::RunTest(const FString& Parameters)
{
	// The records' clock reads 100 s at the start: take-off at 189.95 is 89.95 s into the session.
	const float StartClock = 100.0f;
	FBestThreeSession Session;
	Session.Start(90.0f, StartClock);
	Session.Tick(89.9f);
	TestEqual(TEXT("Before the horn it runs"), Session.GetPhase(), EBestThreePhase::Running);
	TestNearlyEqual(TEXT("with 0.1 s left"), Session.GetTimeLeft(), 0.1f, 1e-3f);

	FJumpRecord Late = MakeJump(TEXT("A"), 12.0f, StartClock + 89.95f);
	TestTrue(TEXT("A take-off 0.05 s before the horn is in the window"), Session.TookOffInWindow(Late));
	Session.Tick(0.2f, &Late);
	TestEqual(TEXT("The horn with that jump in the air: overtime"), Session.GetPhase(), EBestThreePhase::Overtime);
	TestEqual(TEXT("No time left in overtime (s)"), Session.GetTimeLeft(), 0.0f);
	TestFalse(TEXT("Overtime is not finished"), Session.IsFinished());
	Session.Tick(2.0f, &Late);
	TestEqual(TEXT("Still overtime while it is in the air"), Session.GetPhase(), EBestThreePhase::Overtime);
	TestTrue(TEXT("Its landing after the horn is counted"), Session.AddJump(Late));
	Session.Tick(1.0f / 240.0f, nullptr);
	TestTrue(TEXT("Landed, the session finishes"), Session.IsFinished());
	TestEqual(TEXT("and the overtime jump is in the total (pts)"), Session.GetTotal(), 12.0f);
	TestFalse(TEXT("A finished session takes nothing more"), Session.AddJump(MakeJump(TEXT("B"), 8.0f, StartClock + 50.0f)));

	// A take-off after the horn never counts, even if its landing is offered.
	FBestThreeSession After;
	After.Start(90.0f, StartClock);
	After.Tick(89.9f);
	const FJumpRecord TooLate = MakeJump(TEXT("A"), 12.0f, StartClock + 90.05f);
	TestFalse(TEXT("A take-off 0.05 s after the horn is out of the window"), After.TookOffInWindow(TooLate));
	After.Tick(0.2f, &TooLate);
	TestTrue(TEXT("The horn with only a late jump in the air finishes the session"), After.IsFinished());
	TestFalse(TEXT("The late jump's landing is not taken"), After.AddJump(TooLate));
	TestEqual(TEXT("and the total is 0"), After.GetTotal(), 0.0f);

	// A jump already in the air at the start is not the session's.
	FBestThreeSession Early;
	Early.Start(90.0f, StartClock);
	const FJumpRecord BeforeStart = MakeJump(TEXT("A"), 12.0f, StartClock - 0.5f);
	TestFalse(TEXT("A take-off before the start is out of the window"), Early.TookOffInWindow(BeforeStart));
	TestFalse(TEXT("and its landing is not taken"), Early.AddJump(BeforeStart));
	Early.Tick(90.0f, &BeforeStart);
	TestTrue(TEXT("Nor does it hold the session in overtime"), Early.IsFinished());

	// Overtime is capped.
	FBestThreeSession Capped;
	Capped.Start(30.0f, 0.0f);
	const FJumpRecord Floating = MakeJump(TEXT("A"), 12.0f, 29.0f);
	Capped.Tick(30.0f, &Floating);
	TestEqual(TEXT("Overtime for a jump that took off at 29 s"), Capped.GetPhase(), EBestThreePhase::Overtime);
	Capped.Tick(Capped.Settings.OvertimeMaxSeconds - 0.1f, &Floating);
	TestEqual(TEXT("Still overtime just inside the cap"), Capped.GetPhase(), EBestThreePhase::Overtime);
	Capped.Tick(0.2f, &Floating);
	TestTrue(TEXT("Past the cap the session finishes, jump or no jump"), Capped.IsFinished());

	// A jump dropped in the air (a skip or a reset) ends overtime too.
	FBestThreeSession Dropped;
	Dropped.Start(30.0f, 0.0f);
	Dropped.Tick(30.0f, &Floating);
	Dropped.Tick(0.1f, nullptr);
	TestTrue(TEXT("Overtime ends when the jump is no longer in the air"), Dropped.IsFinished());
	Dropped.Tick(1.5f);
	TestNearlyEqual(TEXT("The time since finishing counts from there (s)"), Dropped.GetSecondsSinceFinished(), 1.5f, 1e-4f);
	return true;
}

// On a spawned pawn: the subsystem follows the rider's tracker by polling its record count, pays a
// scripted jump in full, keeps its clock on the board's, and puts up the results at the end.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSessionOnPawn, "KiteSurf.Trick.SessionOnPawn", TrickSessionTest::Flags)

bool FKiteSurfTrickSessionOnPawn::RunTest(const FString& Parameters)
{
	const float SessionSeconds = 20.0f;
	const FSessionRun Run = RunSessionJump(SessionSeconds);
	if (!TestTrue(TEXT("Ride fixture created"), Run.bValid))
	{
		return false;
	}
	UE_LOG(LogKiteSurf, Log, TEXT("SessionOnPawn: '%s' %.1f m, took off at %.2f s, landed at %.2f s, %.1f pts; total %.1f; overtime %d"),
		*Run.Record.TrickName, KiteUnits::CmToM(Run.Record.ApexHeightCm), Run.TakeoffSessionSeconds, Run.LandingSessionSeconds,
		Run.Record.Score.Total, Run.TotalAtEnd, Run.bSawOvertime);

	TestTrue(TEXT("The subsystem found the rider and started"), Run.bStarted);
	TestTrue(TEXT("The HUD shows the session clock at the start"), Run.LinesAtStart.Num() == 4 && Run.LinesAtStart[0] == TEXT("SESSION 0:20"));
	TestTrue(TEXT("with three empty slots"), Run.LinesAtStart.Num() == 4 && Run.LinesAtStart[1] == TEXT("1  --") && Run.LinesAtStart[3] == TEXT("3  --"));
	TestTrue(TEXT("The rider came back down"), Run.bCameDown);
	TestEqual(TEXT("The tracker recorded the one jump"), Run.RecordCount, 1);
	TestTrue(FString::Printf(TEXT("A real jump (apex %.0f cm)"), Run.Record.ApexHeightCm), Run.Record.ApexHeightCm > 500.0f);
	TestEqual(TEXT("The session took it"), Run.SessionJumpsAfterLanding, 1);
	TestTrue(FString::Printf(TEXT("It took off inside the session (%.2f s)"), Run.TakeoffSessionSeconds), Run.TakeoffSessionSeconds >= 0.0f && Run.TakeoffSessionSeconds < SessionSeconds);
	TestEqual(TEXT("Landed, it is paid in full: the total is its score (pts)"), Run.TotalAfterLanding,
		Run.Record.Outcome == EJumpOutcome::Landed ? Run.Record.Score.Total : 0.0f);
	TestTrue(FString::Printf(TEXT("and scores (%.1f pts)"), Run.TotalAfterLanding), Run.TotalAfterLanding > 0.0f);
	TestTrue(TEXT("The HUD's first slot shows its points"), Run.LinesAfterLanding.Num() == 4
		&& Run.LinesAfterLanding[1] == FString::Printf(TEXT("1  %.1f"), Run.TotalAfterLanding));
	TestNearlyEqual(TEXT("The session clock is the board's simulation clock (s)"), Run.ClockDriftSeconds, 0.0f, 1e-3f);
	TestFalse(TEXT("A jump landed well inside the time needs no overtime"), Run.bSawOvertime);

	TestTrue(TEXT("Time ran out and the session finished"), Run.bFinished);
	TestEqual(TEXT("The total stands at the end (pts)"), Run.TotalAtEnd, Run.TotalAfterLanding);
	TestFalse(TEXT("A test world has no game instance, so no local best before"), Run.bHadPreviousBest);
	TestTrue(TEXT("so a scoring session is a new best"), Run.bNewBest);
	const TArray<FString>& Results = Run.LinesAtResults;
	TestTrue(TEXT("The results card is up"), Results.Num() == 5);
	if (Results.Num() == 5)
	{
		TestEqual(TEXT("Results title"), Results[0], FString(TEXT("SESSION OVER  (20 s)")));
		TestEqual(TEXT("Results total to 1 dp"), Results[1], FString::Printf(TEXT("TOTAL  %.1f"), Run.TotalAtEnd));
		TestEqual(TEXT("NEW BEST badge"), Results[2], FString(TEXT("NEW BEST")));
		TestTrue(TEXT("The counting jump with its name, height and points"), Results[3] == FString::Printf(TEXT("1  %s  %.1f m  %.1f pts"),
			*Run.Record.TrickName, KiteUnits::CmToM(Run.Record.ApexHeightCm), Run.TotalAtEnd));
		TestEqual(TEXT("The local best line"), Results[4], FString(TEXT("First 20 s session")));
	}
	return true;
}

// The horn on a real jump: the same ride with the horn 0.5 s after the jump's take-off. It goes to
// overtime and the jump counts at its landing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSessionHornMidAirOnPawn, "KiteSurf.Trick.SessionHornMidAirOnPawn", TrickSessionTest::Flags)

bool FKiteSurfTrickSessionHornMidAirOnPawn::RunTest(const FString& Parameters)
{
	// A first run finds when the jump takes off; the ride is deterministic, so the second run's take-off
	// is at the same time.
	const FSessionRun Probe = RunSessionJump(30.0f);
	if (!TestTrue(TEXT("Probe ride ran and jumped"), Probe.bValid && Probe.bCameDown && Probe.RecordCount == 1))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("The jump is in the air over 1 s (%.2f s)"), Probe.Record.AirtimeSeconds), Probe.Record.AirtimeSeconds > 1.0f);
	const float HornSeconds = Probe.TakeoffSessionSeconds + 0.5f;
	const FSessionRun Run = RunSessionJump(HornSeconds);
	UE_LOG(LogKiteSurf, Log, TEXT("SessionHornMidAirOnPawn: horn at %.2f s, took off at %.2f s, landed at %.2f s, total %.1f"),
		HornSeconds, Run.TakeoffSessionSeconds, Run.LandingSessionSeconds, Run.TotalAtEnd);
	TestNearlyEqual(TEXT("The same take-off as the probe (s)"), Run.TakeoffSessionSeconds, Probe.TakeoffSessionSeconds, 1e-3f);
	TestTrue(TEXT("It landed after the horn"), Run.LandingSessionSeconds > HornSeconds);
	TestTrue(TEXT("The session went to overtime"), Run.bSawOvertime);
	TestEqual(TEXT("The jump counted"), Run.SessionJumpsAfterLanding, 1);
	TestTrue(TEXT("and the session finished at its landing"), Run.bFinished);
	TestEqual(TEXT("with the jump's score as the total (pts)"), Run.TotalAtEnd, Run.Record.Outcome == EJumpOutcome::Landed ? Run.Record.Score.Total : 0.0f);
	return true;
}

// The pause menu's "Best-three session (90 s)" entry starts a 90 s session for the rider.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSessionStartsFromPauseMenu, "KiteSurf.Trick.SessionStartsFromPauseMenu", TrickSessionTest::Flags)

bool FKiteSurfTrickSessionStartsFromPauseMenu::RunTest(const FString& Parameters)
{
	FSessionRideFixture Ride(20.0f);
	if (!TestTrue(TEXT("Ride fixture created"), Ride.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("No session runs at first"), Ride.HUD->GetSessionLines().Num() == 0 && !Ride.Sessions->IsSessionActive());
	UKiteSurfPauseMenuWidget* PauseMenu = CreateWidget<UKiteSurfPauseMenuWidget>(Ride.World, UKiteSurfPauseMenuWidget::StaticClass());
	if (!TestNotNull(TEXT("Pause menu created"), PauseMenu))
	{
		return false;
	}
	FKiteMenuNavigator& Navigator = PauseMenu->GetNavigator();
	Navigator.Select(3); // RESUME, RESTART, GEAR, then the session
	Navigator.HandleKey(EKeys::Enter);
	TestTrue(TEXT("The fourth pause menu entry starts a session"), Ride.Sessions->IsSessionActive());
	TestEqual(TEXT("of 90 s"), Ride.Sessions->GetSession().GetDuration(), UTrickSessionSubsystem::DefaultSessionSeconds);
	TestEqual(TEXT("which the HUD shows"), Ride.HUD->GetSessionLines().Num() > 0 ? Ride.HUD->GetSessionLines()[0] : FString(), FString(TEXT("SESSION 1:30")));
	Ride.Simulate(1.0f);
	TestNearlyEqual(TEXT("and which runs with the ride (s left)"), Ride.Sessions->GetSession().GetTimeLeft(), 89.0f, 0.02f);
	return true;
}

// The HUD's session lines: the clock, the panel slots and the results card.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSessionHUDFormatting, "KiteSurf.Trick.SessionHUDFormatting", TrickSessionTest::Flags)

bool FKiteSurfTrickSessionHUDFormatting::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("90 s"), AKiteSurfHUD::FormatSessionClock(90.0f), FString(TEXT("1:30")));
	TestEqual(TEXT("71.2 s rounds up"), AKiteSurfHUD::FormatSessionClock(71.2f), FString(TEXT("1:12")));
	TestEqual(TEXT("9.01 s rounds up"), AKiteSurfHUD::FormatSessionClock(9.01f), FString(TEXT("0:10")));
	TestEqual(TEXT("0.5 s"), AKiteSurfHUD::FormatSessionClock(0.5f), FString(TEXT("0:01")));
	TestEqual(TEXT("Time up"), AKiteSurfHUD::FormatSessionClock(0.0f), FString(TEXT("0:00")));
	TestEqual(TEXT("Never negative"), AKiteSurfHUD::FormatSessionClock(-3.0f), FString(TEXT("0:00")));

	FBestThreeSession Session;
	Session.Start(90.0f);
	Session.Tick(18.4f);
	TArray<FString> Panel = AKiteSurfHUD::FormatSessionPanel(Session);
	TestEqual(TEXT("An empty panel: clock and three empty slots"), FString::Join(Panel, TEXT("|")), FString(TEXT("SESSION 1:12|1  --|2  --|3  --")));

	Session.AddJump(MakeJump(TEXT("A"), 42.04f, 5.0f));
	Session.AddJump(MakeJump(TEXT("B"), 12.5f, 8.0f));
	Session.AddJump(MakeJump(TEXT("B"), 20.0f, 12.0f)); // 15.0 paid
	Panel = AKiteSurfHUD::FormatSessionPanel(Session);
	TestEqual(TEXT("Slots show the points to 1 dp, best first, with the repeat factor"), FString::Join(Panel, TEXT("|")),
		FString(TEXT("SESSION 1:12|1  42.0|2  15.0 x0.75|3  --")));

	FJumpRecord InAir = MakeJump(TEXT("C"), 0.0f, 89.0f);
	Session.Tick(72.0f, &InAir);
	TestEqual(TEXT("Overtime is named in the clock line"), AKiteSurfHUD::FormatSessionPanel(Session)[0], FString(TEXT("SESSION 0:00  OVERTIME")));
	FJumpRecord Landed = InAir;
	Landed.Score.Total = 30.0f;
	Landed.TrickName = TEXT("Kiteloop");
	Landed.ApexHeightCm = 1234.0f;
	Session.AddJump(Landed);
	Session.Tick(0.1f, nullptr);
	TestTrue(TEXT("Finished"), Session.IsFinished());

	const FString NewBestWithPrevious = FString::Join(AKiteSurfHUD::FormatSessionResults(Session, 80.4f, true, true), TEXT("|"));
	TestEqual(TEXT("Results: new best over a previous one"), NewBestWithPrevious,
		FString(TEXT("SESSION OVER  (90 s)|TOTAL  87.0|NEW BEST|1  Trick A  8.0 m  42.0 pts|2  Kiteloop  12.3 m  30.0 pts|3  Trick B  8.0 m  15.0 pts|Previous best  80.4")));
	const FString NotBeaten = FString::Join(AKiteSurfHUD::FormatSessionResults(Session, 92.4f, true, false), TEXT("|"));
	TestTrue(TEXT("Results: not beaten shows no badge and the local best"), !NotBeaten.Contains(TEXT("NEW BEST")) && NotBeaten.EndsWith(TEXT("|Local best  92.4")));
	const FString First = FString::Join(AKiteSurfHUD::FormatSessionResults(Session, 0.0f, false, true), TEXT("|"));
	TestTrue(TEXT("Results: the first session of a length"), First.Contains(TEXT("|NEW BEST|")) && First.EndsWith(TEXT("|First 90 s session")));

	FBestThreeSession Empty;
	Empty.Start(30.0f);
	Empty.Tick(30.0f);
	TestEqual(TEXT("Results with nothing counted"), FString::Join(AKiteSurfHUD::FormatSessionResults(Empty, 0.0f, false, false), TEXT("|")),
		FString(TEXT("SESSION OVER  (30 s)|TOTAL  0.0|No jumps counted|First 30 s session")));

	// A HUD in a world with no session shows nothing.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteSurfHUD* HUD = World ? World->SpawnActor<AKiteSurfHUD>() : nullptr;
	if (TestNotNull(TEXT("HUD spawned"), HUD))
	{
		TestNotNull(TEXT("A game world has the session subsystem"), World->GetSubsystem<UTrickSessionSubsystem>());
		TestEqual(TEXT("No session, no session lines"), HUD->GetSessionLines().Num(), 0);
		TestFalse(TEXT("Starting with no rider in the world fails"), World->GetSubsystem<UTrickSessionSubsystem>()->StartSession(30.0f));
		TestEqual(TEXT("and still shows nothing"), HUD->GetSessionLines().Num(), 0);
	}
	if (World)
	{
		World->DestroyWorld(false);
	}
	return true;
}

// The local best: the game instance keeps the best total per session length, and it goes through the
// save game with the settings, in memory, through ApplySaveGame / WriteToSaveGame and in a test slot.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSessionBestPersists, "KiteSurf.Trick.SessionBestPersists", TrickSessionTest::Flags)

bool FKiteSurfTrickSessionBestPersists::RunTest(const FString& Parameters)
{
	UKiteSurfGameInstance* GI = NewObject<UKiteSurfGameInstance>();
	TestFalse(TEXT("A new game instance has no 90 s best"), GI->HasBestSessionTotal(90));
	TestEqual(TEXT("and reads it as 0"), GI->GetBestSessionTotal(90), 0.0f);
	TestFalse(TEXT("A session scoring 0 is not a best"), GI->RecordSessionTotal(90, 0.0f));
	TestFalse(TEXT("and leaves no best"), GI->HasBestSessionTotal(90));
	TestTrue(TEXT("The first scoring 90 s session is a new best"), GI->RecordSessionTotal(90, 55.5f));
	TestFalse(TEXT("A lower one is not"), GI->RecordSessionTotal(90, 40.0f));
	TestFalse(TEXT("Nor an equal one"), GI->RecordSessionTotal(90, 55.5f));
	TestTrue(TEXT("A higher one is"), GI->RecordSessionTotal(90, 61.25f));
	TestEqual(TEXT("The best is the highest (pts)"), GI->GetBestSessionTotal(90), 61.25f);
	TestTrue(TEXT("Lengths are kept apart: the first 30 s session is a best"), GI->RecordSessionTotal(30, 20.0f));
	TestEqual(TEXT("without touching the 90 s best (pts)"), GI->GetBestSessionTotal(90), 61.25f);

	// Through the save game, as SaveSettingsToDisk writes it and LoadSettingsFromDisk reads it, minus the disk.
	const float VolumeBefore = FApp::GetVolumeMultiplier(); // ApplySaveGame sets it from the save
	GI->PendingWindKnots = 27.0f;
	UKiteSurfSaveGame* Written = NewObject<UKiteSurfSaveGame>();
	GI->WriteToSaveGame(*Written);
	TestEqual(TEXT("WriteToSaveGame writes both bests"), Written->BestSessionTotalBySeconds.Num(), 2);
	TestEqual(TEXT("WriteToSaveGame writes the settings as before (kn)"), Written->WindStrengthKnots, 27.0f);
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
	TestEqual(TEXT("The 90 s best survives the round trip (pts)"), Reader->GetBestSessionTotal(90), 61.25f);
	TestEqual(TEXT("The 30 s best survives the round trip (pts)"), Reader->GetBestSessionTotal(30), 20.0f);
	TestFalse(TEXT("No best appears for a length never played"), Reader->HasBestSessionTotal(60));
	TestEqual(TEXT("The settings survive next to the bests (kn)"), Reader->PendingWindKnots, 27.0f);
	TestFalse(TEXT("The loaded best is beaten only by more"), Reader->RecordSessionTotal(90, 61.0f));
	FApp::SetVolumeMultiplier(VolumeBefore);

	// On disk, in a slot of the test's own: the default slot is the player's real save.
	TestNotEqual(TEXT("The test slot is not the player's slot"), TestSlot, UKiteSurfSaveGame::DefaultSaveSlot);
	UGameplayStatics::DeleteGameInSlot(TestSlot, UKiteSurfSaveGame::DefaultUserIndex);
	TestTrue(TEXT("The save is written to the test slot"), Written->SaveSettings(TestSlot));
	const UKiteSurfSaveGame* FromDisk = UKiteSurfSaveGame::LoadOrCreateSettings(TestSlot);
	const float* DiskBest = FromDisk ? FromDisk->BestSessionTotalBySeconds.Find(90) : nullptr;
	TestTrue(TEXT("The 90 s best survives the round trip through the test slot"), DiskBest && *DiskBest == 61.25f);
	TestTrue(TEXT("The settings survive the round trip through the test slot"), FromDisk && FromDisk->WindStrengthKnots == 27.0f);
	UGameplayStatics::DeleteGameInSlot(TestSlot, UKiteSurfSaveGame::DefaultUserIndex);
	TestFalse(TEXT("The test slot is cleaned up"), UGameplayStatics::DoesSaveGameExist(TestSlot, UKiteSurfSaveGame::DefaultUserIndex));
	return true;
}

// A save from before sessions has no BestSessionTotalBySeconds property: it loads with no best and its
// settings and trick book as they were.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSessionOldSaveLoads, "KiteSurf.Trick.SessionOldSaveLoads", TrickSessionTest::Flags)

bool FKiteSurfTrickSessionOldSaveLoads::RunTest(const FString& Parameters)
{
	const FName BestProperty = GET_MEMBER_NAME_CHECKED(UKiteSurfSaveGame, BestSessionTotalBySeconds);

	UKiteSurfSaveGame* Save = NewObject<UKiteSurfSaveGame>();
	Save->WindStrengthKnots = 27.0f;
	Save->BoardSizeIndex = 2;
	Save->bHaptics = false;
	Save->BestSessionTotalBySeconds.Add(90, 61.25f);
	FJumpRecord Landed = MakeJump(TEXT("HH|L|-|I0|S-0|G"), 40.0f);
	Save->TrickBook.RecordLanding(Landed, ETrickBoardCategory::TwinTip, FDateTime(2025, 9, 1, 12, 0, 0));
	TArray<uint8> Full;
	UGameplayStatics::SaveGameToMemory(Save, Full);

	// Rebuild it as an older build wrote it: the same header, the object without the bests.
	const TArray<uint8> ObjectFull = SerializeObject(*Save);
	const TArray<uint8> ObjectOld = SerializeObject(*Save, BestProperty);
	const int32 HeaderBytes = Full.Num() - ObjectFull.Num();
	const bool bSplits = HeaderBytes > 0 && FMemory::Memcmp(Full.GetData() + HeaderBytes, ObjectFull.GetData(), ObjectFull.Num()) == 0;
	if (!TestTrue(TEXT("The save is a header followed by the object, as SaveGameToMemory writes it"), bSplits))
	{
		return false;
	}
	TestTrue(TEXT("Leaving the bests out makes the object smaller (they are really gone)"), ObjectOld.Num() < ObjectFull.Num());
	TArray<uint8> Old(Full.GetData(), HeaderBytes);
	Old.Append(ObjectOld);

	UKiteSurfSaveGame* Loaded = Cast<UKiteSurfSaveGame>(UGameplayStatics::LoadGameFromMemory(Old));
	if (!TestNotNull(TEXT("A save without session bests loads"), Loaded))
	{
		return false;
	}
	TestEqual(TEXT("It loads with no session best"), Loaded->BestSessionTotalBySeconds.Num(), 0);
	TestEqual(TEXT("Its wind is kept (kn)"), Loaded->WindStrengthKnots, 27.0f);
	TestEqual(TEXT("Its board is kept"), Loaded->BoardSizeIndex, 2);
	TestFalse(TEXT("Its haptics setting is kept"), Loaded->bHaptics);
	TestEqual(TEXT("Its trick book is kept"), Loaded->TrickBook.Num(), 1);

	UKiteSurfSaveGame* LoadedFull = Cast<UKiteSurfSaveGame>(UGameplayStatics::LoadGameFromMemory(Full));
	TestTrue(TEXT("The control: the same save with the property loads with its best"), LoadedFull && LoadedFull->BestSessionTotalBySeconds.FindRef(90) == 61.25f);

	const float VolumeBefore = FApp::GetVolumeMultiplier(); // ApplySaveGame sets it from the save
	UKiteSurfGameInstance* GI = NewObject<UKiteSurfGameInstance>();
	GI->RecordSessionTotal(90, 10.0f);
	GI->ApplySaveGame(*Loaded);
	TestFalse(TEXT("A game instance loading an old save has no 90 s best"), GI->HasBestSessionTotal(90));
	TestEqual(TEXT("and keeps its wind (kn)"), GI->PendingWindKnots, 27.0f);
	TestTrue(TEXT("Its first scoring session is then a new best"), GI->RecordSessionTotal(90, 1.0f));
	FApp::SetVolumeMultiplier(VolumeBefore);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
