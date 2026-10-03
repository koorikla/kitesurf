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
#include "Tricks/LandingMath.h"
#include "Tricks/TrickTrackerComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the trick tracker wired into the pawn (docs/tricks/T0.md sections 4 and 6). The tracker
// reads the board's take-off and landing facts and the kite's loop records (TrickFeedTests.cpp tests
// those feeds); the heading-difference test that was here went with the tracker's own derivation. The ride
// fixture and the timed send-and-pop are copied from RideLoopTests.cpp's RunJump and fly the jump
// the way it does since physics phase 2: the jump button held and let go, and a crouched landing.
namespace TrickTrackerTestsLocal
{
	const float RideDeltaTime = 1.0f / 60.0f;

	/** A pawn in a throwaway world, set up the way the game mode starts a ride, in steady wind along +X. */
	struct FTrickRideFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		UTrickTrackerComponent* Tracker = nullptr;

		explicit FTrickRideFixture(float WindKnots = 30.0f)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (Pawn)
			{
				Kite = Pawn->GetKite();
				Board = Pawn->GetBoardMovement();
				Tracker = Pawn->GetTrickTracker();
				if (UWindComponent* Wind = Pawn->GetWind())
				{
					Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(WindKnots), 0.0f, 0.0f);
					Wind->GustStrength = 0.0f;
					Wind->DirectionDriftDeg = 0.0f;
				}
				AKiteSurfGameMode::InitializeRide(Pawn, KiteUnits::KnotsToCmS(12.0f), 1.0f);
				Kite->bParkHoldAssist = true;
				// Sampled positions are the simulation's, not the drawn ones between steps.
				Pawn->bInterpolateRendering = false;
				Kite->SetKiteModel(EKiteModel::Loop);
				Kite->SetKiteSize(UKiteComponent::RecommendKiteSizeM2(WindKnots));
			}
		}

		~FTrickRideFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Tracker; }

		void Simulate(float Seconds)
		{
			const int32 Steps = FMath::RoundToInt(Seconds / RideDeltaTime);
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				Pawn->Tick(RideDeltaTime);
			}
		}
	};

	/** What a scripted jump did, as the test saw it frame by frame and as the tracker recorded it. */
	struct FTrackedJump
	{
		bool bValid = false;
		bool bCameDown = false;
		bool bPulledOffEdge = false;
		/** Highest board Z sampled in the air (cm). */
		float MaxZCm = 0.0f;
		float BoardLastAirtime = 0.0f;
		float BoardLastApexCm = 0.0f;
		float BoardLastDistanceCm = 0.0f;
		/** The board's own judgement of the touchdown: its load (g) and the distance it was taken out over (cm). */
		float BoardLastLandingG = 0.0f;
		float BoardLastLandingAbsorbCm = 0.0f;
		/** The board's landing angle and apex time, which the record now carries (deg, s). */
		float BoardLastLandingAngleDeg = 0.0f;
		float BoardApexTimeSeconds = 0.0f;
		int32 BoardTakeoffCount = 0;
		bool bBoardLandingClean = false;
		int32 BoardJumpCount = 0;
		int32 RecordCount = 0;
		bool bHasRecord = false;
		FJumpRecord Record;
		/** The sum of the kite's per-step turns (GetLastStepTurnDeg) while it was looping, and its own loop count (deg). */
		float TrackerLoopTurnDeg = 0.0f;
		float KiteTurnDeg = 0.0f;
		/** What the HUD showed, when one was passed in. */
		FString TickerSeen;
		FString CardOnLanding;
		FString ReadoutOnLanding;
		/** A second, parked pop after the first jump, when asked for. */
		bool bSecondPopped = false;
		int32 SecondRecordCount = 0;
		int32 SecondBoardJumpCount = 0;
		FJumpRecord SecondRecord;
	};

	/**
	 * When the timed jump lets go of the jump button, after the send reaches the kite (s): RideLoopTests'
	 * TrackerTimedReleaseSeconds. Since physics phase 2 the loaded rider hangs on until the lines pull up 2.5
	 * body weights, so the 0.7 s of phase 1 is a frame too late (the kite plucks them off first).
	 */
	constexpr float TrackerTimedReleaseSeconds = 0.66f;

	/**
	 * The timed send and pop of RideLoopTests' RunJump (30 kn, recommended kite): the jump button held
	 * (crouched, loading the edge) with the weight back, let go TrackerTimedReleaseSeconds after the bar reaches
	 * the kite, and held again from the apex to crouch for the landing (physics phase 2: landed standing,
	 * a jump this big is a crash). With LoopSteer non-zero the bar is held over with the loop forced
	 * through (SetLoopHeld) from take-off until touchdown. A HUD passed in is updated each frame as
	 * DrawHUD would.
	 */
	FTrackedJump RunTrackedJump(float LoopSteer = 0.0f, AKiteSurfHUD* HUD = nullptr, bool bSecondPop = false)
	{
		FTrackedJump Out;
		FTrickRideFixture Ride(30.0f);
		if (!Ride.IsValid())
		{
			return Out;
		}
		Out.bValid = true;
		Ride.Simulate(8.0f);

		Ride.Pawn->SteerKite(-1.0f);
		const float SendDeadTimeSeconds = Ride.Kite->GetSteeringDeadTimeSeconds();
		const float ReleaseSeconds = TrackerTimedReleaseSeconds;
		Ride.Board->SetWeightShift(-1.0f);
		Ride.Pawn->SetLoadHeld(true);

		bool bLeftWater = false;
		bool bLooping = false;
		float AirSeconds = 0.0f;
		for (float Elapsed = 0.0f; Elapsed < 90.0f; Elapsed += RideDeltaTime)
		{
			Ride.Simulate(RideDeltaTime);
			const bool bAir = Ride.Board->GetBoardState() == EBoardState::Airborne;
			if (bAir && !bLeftWater)
			{
				bLeftWater = true;
				Out.bPulledOffEdge = true;
				Ride.Board->SetWeightShift(0.0f);
				Ride.Pawn->SetLoadHeld(false);
				Ride.Pawn->SheetKite(1.0f);
			}
			if (!bLeftWater && Elapsed >= ReleaseSeconds + SendDeadTimeSeconds)
			{
				Ride.Board->SetWeightShift(-1.0f);
				Ride.Pawn->SheetKite(1.0f);
				Ride.Pawn->ReleaseLoadAndPop();
				Ride.Board->SetWeightShift(0.0f);
				bLeftWater = true;
			}
			if (LoopSteer != 0.0f && bAir && !bLooping)
			{
				bLooping = true;
				Ride.Kite->SetLoopHeld(true);
				Ride.Pawn->SteerKite(LoopSteer);
			}
			else if (!bLooping && Ride.Kite->GetClockDeg() < 0.0f)
			{
				Ride.Pawn->SteerKite(0.0f); // keep the kite overhead
			}
			if (bAir && Ride.Board->Velocity.Z < 0.0f)
			{
				// Coming down: crouch for the landing.
				Ride.Pawn->SetLoadHeld(true);
			}
			if (bAir)
			{
				AirSeconds += RideDeltaTime;
				Out.MaxZCm = FMath::Max(Out.MaxZCm, static_cast<float>(Ride.Pawn->GetActorLocation().Z));
				if (Ride.Kite->IsLooping())
				{
					// Frames run four fixed steps each; this is only the last step's turn, so scale it.
					Out.TrackerLoopTurnDeg += Ride.Kite->GetLastStepTurnDeg() * RideDeltaTime / Ride.Pawn->SimStepSeconds;
					Out.KiteTurnDeg = Ride.Kite->GetTurnDeg();
				}
			}
			if (HUD)
			{
				HUD->UpdateJumpReadout(Ride.Board, RideDeltaTime);
				HUD->UpdateJumpCard(Ride.Tracker, RideDeltaTime);
				if (Out.TickerSeen.IsEmpty())
				{
					Out.TickerSeen = HUD->GetTrickTickerText();
				}
			}
			if (bLeftWater && !bAir && AirSeconds > 0.3f)
			{
				Out.bCameDown = true;
				if (HUD)
				{
					Out.CardOnLanding = HUD->GetJumpCardText();
					Out.ReadoutOnLanding = HUD->GetJumpReadoutText();
				}
				break;
			}
		}
		Ride.Pawn->SetLoadHeld(false); // landed: up out of the crouch (letting go of the button, no pop)
		Ride.Kite->SetLoopHeld(false);
		Ride.Pawn->SteerKite(0.0f);

		Out.BoardLastLandingG = Ride.Board->GetLastLandingG();
		Out.BoardLastLandingAbsorbCm = Ride.Board->GetLastLandingAbsorbCm();
		Out.BoardLastLandingAngleDeg = Ride.Board->GetLastLandingAngleDeg();
		Out.BoardApexTimeSeconds = Ride.Board->GetCurrentJumpApexTimeSeconds();
		Out.BoardTakeoffCount = Ride.Board->GetTakeoffCount();
		Out.BoardLastAirtime = Ride.Board->GetLastJumpAirtime();
		Out.BoardLastApexCm = Ride.Board->GetLastJumpApexHeight();
		Out.BoardLastDistanceCm = Ride.Board->GetLastJumpDistance();
		Out.bBoardLandingClean = Ride.Board->WasLastLandingClean();
		Out.BoardJumpCount = Ride.Board->GetJumpCount();
		Out.RecordCount = Ride.Tracker->GetJumpRecordCount();
		Out.bHasRecord = Ride.Tracker->GetLastJumpRecord(Out.Record);

		if (bSecondPop)
		{
			// Ride on (through any crash recovery), then a plain pop from the water.
			Ride.Simulate(6.0f);
			if (Ride.Board->Jump() == EJumpRejectReason::None)
			{
				Out.bSecondPopped = true;
				Ride.Simulate(4.0f);
			}
			Out.SecondRecordCount = Ride.Tracker->GetJumpRecordCount();
			Out.SecondBoardJumpCount = Ride.Board->GetJumpCount();
			Ride.Tracker->GetLastJumpRecord(Out.SecondRecord);
		}
		return Out;
	}

	/** A record as the tracker would finish it, for the pure card tests. */
	FJumpRecord MakeRecord(const TCHAR* Name, ELandingGrade Grade, float ScoreTotal, float LandingG, float RepeatFactor = 1.0f)
	{
		FJumpRecord Record;
		Record.Index = 0;
		Record.Outcome = Grade == ELandingGrade::Crash ? EJumpOutcome::Crashed : EJumpOutcome::Landed;
		Record.TrickName = Name;
		Record.Grade = Grade;
		Record.Score.Total = ScoreTotal;
		Record.LandingG = LandingG;
		Record.RepeatFactor = RepeatFactor;
		Record.ApexHeightCm = 800.0f;
		return Record;
	}

	/** A completed kiteloop placed in a jump: starts StartS after take-off, before the apex, kite down to 40 deg, rider 5 m up. */
	FJumpLoop MakeKiteloop(float StartS, float DurationS = 1.2f)
	{
		FJumpLoop Result;
		Result.Loop.Direction = 1;
		Result.Loop.StartTimeSeconds = 100.0f + StartS;
		Result.Loop.DurationSeconds = DurationS;
		Result.Loop.TurnDeg = 360.0f;
		Result.Loop.bCompleted = true;
		Result.Loop.StartElevationDeg = 70.0f;
		Result.Loop.MinElevationDeg = 40.0f;
		Result.Loop.PeakTensionN = 1500.0f;
		Result.Loop.PeakTensionTimeSeconds = Result.Loop.StartTimeSeconds + 0.5f * DurationS;
		Result.StartSinceTakeoffSeconds = StartS;
		Result.StartSinceApexSeconds = StartS - 5.0f;
		Result.RiderHeightAtStartCm = 500.0f;
		return Result;
	}
}

using namespace TrickTrackerTestsLocal;

// The tracker on the pawn records a scripted send and pop as one jump, with the board's numbers,
// and a later parked pop as the second.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickTrackerRecordsJump, "KiteSurf.Trick.TrackerRecordsJump", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickTrackerRecordsJump::RunTest(const FString& Parameters)
{
	const FTrackedJump Jump = RunTrackedJump(0.0f, nullptr, true);
	TestTrue(TEXT("Ride fixture created"), Jump.bValid);
	if (!Jump.bValid)
	{
		return false;
	}
	const FJumpRecord& R = Jump.Record;
	UE_LOG(LogKiteSurf, Log, TEXT("TrackerRecordsJump: '%s' %s, apex %.0f cm (sampled max Z %.0f), air %.2f s, %.0f m, sink %.0f cm/s, %.2f g, peak %.0f N, popped %d (pulled off %d), %.1f pts"),
		*R.TrickName, *AKiteSurfHUD::GradeText(R.Grade), R.ApexHeightCm, Jump.MaxZCm, R.AirtimeSeconds, R.DistanceCm / 100.0f, R.SinkRateCmS, R.LandingG,
		R.PeakTensionN, R.bPopped, Jump.bPulledOffEdge, R.Score.Total);

	TestTrue(TEXT("The rider came back down"), Jump.bCameDown);
	TestEqual(TEXT("Exactly one record for one jump"), Jump.RecordCount, 1);
	TestTrue(TEXT("The record can be read back"), Jump.bHasRecord);
	TestEqual(TEXT("The record count matches the board's jump count"), Jump.RecordCount, Jump.BoardJumpCount);
	TestEqual(TEXT("The first record has index 0"), R.Index, 0);
	TestTrue(FString::Printf(TEXT("A real jump (apex %.0f cm)"), R.ApexHeightCm), R.ApexHeightCm > 500.0f);
	TestNearlyEqual(TEXT("The apex is within 10 cm of the highest sampled board Z (cm)"), R.ApexHeightCm, Jump.MaxZCm, 10.0f);
	TestNearlyEqual(TEXT("The airtime is the board's (s)"), R.AirtimeSeconds, Jump.BoardLastAirtime, 1e-4f);
	TestNearlyEqual(TEXT("The distance is the board's (cm)"), R.DistanceCm, Jump.BoardLastDistanceCm, 0.1f);
	TestTrue(TEXT("Take-off before the apex before the landing"), R.TakeoffTimeSeconds < R.ApexTimeSeconds && R.ApexTimeSeconds < R.LandingTimeSeconds);
	TestNearlyEqual(TEXT("Landing minus take-off is the airtime (s)"), R.LandingTimeSeconds - R.TakeoffTimeSeconds, R.AirtimeSeconds, 0.01f);
	TestTrue(FString::Printf(TEXT("The rider was coming down at touchdown (%.0f cm/s)"), R.SinkRateCmS), R.SinkRateCmS > 0.0f);
	TestNearlyEqual(TEXT("The landing g is the board's"), R.LandingG, Jump.BoardLastLandingG, 1e-4f);
	TestNearlyEqual(FString::Printf(TEXT("1 + v^2/(2 g s) over the board's absorb distance (%.0f cm, crouched)"), Jump.BoardLastLandingAbsorbCm),
		R.LandingG, LandingMath::ComputeLandingG(R.SinkRateCmS, Jump.BoardLastLandingAbsorbCm), 1e-3f);
	TestTrue(TEXT("The lines pulled in the air"), R.PeakTensionN > 0.0f);
	TestEqual(TEXT("Popped when the pop, not the kite, took the rider off"), R.bPopped, !Jump.bPulledOffEdge);
	// Since the tracker reads the board's own landing facts: the landing yaw is the board's landing
	// angle (it was 0 when the tracker synthesised it), and the apex time is the board's.
	TestNearlyEqual(TEXT("The landing yaw is the board's landing angle (deg)"), R.LandingYawDeg, Jump.BoardLastLandingAngleDeg, 1e-4f);
	TestNearlyEqual(TEXT("The apex time is the board's (s)"), R.ApexTimeSeconds, Jump.BoardApexTimeSeconds, 1e-4f);
	TestEqual(TEXT("One take-off on the board for the one jump"), Jump.BoardTakeoffCount, 1);
	TestFalse(TEXT("The jump has a name"), R.TrickName.IsEmpty());
	TestFalse(TEXT("and a family key"), R.FamilyKey.IsEmpty());
	TestTrue(TEXT("The board called the landing clean"), Jump.bBoardLandingClean);
	if (Jump.bBoardLandingClean)
	{
		TestEqual(TEXT("A clean landing is recorded as landed"), R.Outcome, EJumpOutcome::Landed);
		TestNotEqual(TEXT("and is not graded a crash"), R.Grade, ELandingGrade::Crash);
		TestTrue(FString::Printf(TEXT("and scores (%.1f pts)"), R.Score.Total), R.Score.Total > 0.0f);
	}

	TestTrue(TEXT("A parked pop left the water after the first jump"), Jump.bSecondPopped);
	if (Jump.bSecondPopped)
	{
		TestEqual(TEXT("The pop is the second record"), Jump.SecondRecordCount, 2);
		TestEqual(TEXT("and the board counted two jumps"), Jump.SecondBoardJumpCount, 2);
		TestEqual(TEXT("Its index is 1"), Jump.SecondRecord.Index, 1);
		TestTrue(TEXT("A pop from the water is recorded as popped"), Jump.SecondRecord.bPopped);
		TestTrue(TEXT("It took off after the first jump landed"), Jump.SecondRecord.TakeoffTimeSeconds > R.LandingTimeSeconds);
	}
	return true;
}

// Looping the kite in the air on a big jump is recorded as a kite loop and named for it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickTrackerNamesKiteLoop, "KiteSurf.Trick.TrackerNamesKiteLoop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickTrackerNamesKiteLoop::RunTest(const FString& Parameters)
{
	UWorld* HudWorld = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteSurfHUD* HUD = HudWorld ? HudWorld->SpawnActor<AKiteSurfHUD>() : nullptr;
	TestNotNull(TEXT("HUD spawned"), HUD);

	const FTrackedJump Jump = RunTrackedJump(1.0f, HUD);
	TestTrue(TEXT("Ride fixture created"), Jump.bValid);
	const FJumpRecord& R = Jump.Record;
	FString LoopText;
	for (const FJumpLoop& JumpLoop : R.Loops)
	{
		LoopText += FString::Printf(TEXT(" [dir %+d, %.0f deg, %s, %.2f s after take-off, %.1f s long, elev %.0f->%.0f, %s]"), JumpLoop.Loop.Direction,
			JumpLoop.Loop.TurnDeg, JumpLoop.Loop.bCompleted ? TEXT("complete") : TEXT("part"), JumpLoop.StartSinceTakeoffSeconds,
			JumpLoop.Loop.DurationSeconds, JumpLoop.Loop.StartElevationDeg, JumpLoop.Loop.MinElevationDeg, JumpLoop.Loop.bKiteCrashed ? TEXT("kite crashed") : TEXT("flying"));
	}
	UE_LOG(LogKiteSurf, Log, TEXT("TrackerNamesKiteLoop: '%s' %s, apex %.0f cm, air %.2f s, %.2f g, %.1f pts, ticker '%s', card '%s', tracker loop turn %.0f deg vs kite %.0f deg, loops:%s"),
		*R.TrickName, *AKiteSurfHUD::GradeText(R.Grade), R.ApexHeightCm, R.AirtimeSeconds, R.LandingG, R.Score.Total, *Jump.TickerSeen,
		*Jump.CardOnLanding.Replace(TEXT("\n"), TEXT(" / ")), Jump.TrackerLoopTurnDeg, Jump.KiteTurnDeg, *LoopText);

	TestTrue(TEXT("The rider came back down"), Jump.bCameDown);
	TestEqual(TEXT("One record"), Jump.RecordCount, 1);
	TestTrue(FString::Printf(TEXT("At least one completed kite loop in the air (%d)"), R.CountCompletedLoops()), R.CountCompletedLoops() >= 1);
	TestTrue(FString::Printf(TEXT("The name says loop ('%s')"), *R.TrickName), R.TrickName.Contains(TEXT("loop"), ESearchCase::IgnoreCase));
	TestTrue(FString::Printf(TEXT("The heading turn the tracker counts goes the kite's way (%.0f vs %.0f deg)"), Jump.TrackerLoopTurnDeg, Jump.KiteTurnDeg),
		Jump.TrackerLoopTurnDeg * Jump.KiteTurnDeg > 0.0f);
	TestTrue(FString::Printf(TEXT("The ticker named the loop in the air ('%s')"), *Jump.TickerSeen), Jump.TickerSeen.Contains(TEXT("loop"), ESearchCase::IgnoreCase));
	TestTrue(FString::Printf(TEXT("The card on landing carries the name ('%s')"), *Jump.CardOnLanding), !R.TrickName.IsEmpty() && Jump.CardOnLanding.StartsWith(R.TrickName));

	if (HudWorld)
	{
		HudWorld->DestroyWorld(false);
	}
	return true;
}

// After a jump the HUD shows a card under the jump readout: the trick, its grade, the points and
// the landing g. It goes after a few seconds, and the readout above it is unchanged.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDJumpCardShowsAfterLanding, "KiteSurf.HUD.JumpCardShowsAfterLanding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfHUDJumpCardShowsAfterLanding::RunTest(const FString& Parameters)
{
	// 1. Pure formatting.
	TestEqual(TEXT("A clean kiteloop"), AKiteSurfHUD::FormatJumpCard(MakeRecord(TEXT("Kiteloop"), ELandingGrade::Clean, 41.3f, 3.24f)),
		FString(TEXT("Kiteloop  CLEAN  41 pts\n3.2 g landing")));
	const FString CrashCard = AKiteSurfHUD::FormatJumpCard(MakeRecord(TEXT("Straight air"), ELandingGrade::Crash, 0.0f, 9.5f));
	TestTrue(FString::Printf(TEXT("A crash says so and pays nothing ('%s')"), *CrashCard), CrashCard.Contains(TEXT("CRASH")) && CrashCard.Contains(TEXT("0 pts")));
	TestEqual(TEXT("A repeat shows what it paid"), AKiteSurfHUD::FormatJumpCard(MakeRecord(TEXT("Kiteloop"), ELandingGrade::Stomped, 40.0f, 2.0f, 0.75f)),
		FString(TEXT("Kiteloop  STOMPED  30 pts  (repeat 75%)\n2.0 g landing")));
	TestEqual(TEXT("Grade words"), AKiteSurfHUD::GradeText(ELandingGrade::Stomped) + AKiteSurfHUD::GradeText(ELandingGrade::Clean) + AKiteSurfHUD::GradeText(ELandingGrade::Sketchy),
		FString(TEXT("STOMPEDCLEANSKETCHY")));
	TestEqual(TEXT("A crash is red, as the rejection text"), AKiteSurfHUD::GradeColor(ELandingGrade::Crash), FLinearColor(1.0f, 0.35f, 0.35f));
	TestEqual(TEXT("A clean landing is white"), AKiteSurfHUD::GradeColor(ELandingGrade::Clean), FLinearColor::White);
	TestTrue(TEXT("Stomped is green"), AKiteSurfHUD::GradeColor(ELandingGrade::Stomped).G > 0.9f && AKiteSurfHUD::GradeColor(ELandingGrade::Stomped).R < 0.5f);
	TestTrue(TEXT("Sketchy is amber"), AKiteSurfHUD::GradeColor(ELandingGrade::Sketchy).R > 0.9f && AKiteSurfHUD::GradeColor(ELandingGrade::Sketchy).B < 0.3f);

	FJumpRecord Live;
	TestTrue(TEXT("A jump with nothing in it yet has no ticker"), AKiteSurfHUD::FormatTrickTicker(Live).IsEmpty());
	Live.Loops = { MakeKiteloop(0.5f), MakeKiteloop(1.7f) };
	TestEqual(TEXT("Two loops back to back tick as a double"), AKiteSurfHUD::FormatTrickTicker(Live), FString(TEXT("Double kiteloop")));

	UWorld* HudWorld = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteSurfHUD* HUD = HudWorld ? HudWorld->SpawnActor<AKiteSurfHUD>() : nullptr;
	TestNotNull(TEXT("HUD spawned"), HUD);
	if (HUD)
	{
		TestTrue(TEXT("No card before a jump"), HUD->GetJumpCardText().IsEmpty());
		HUD->ShowJumpCard(MakeRecord(TEXT("Kiteloop"), ELandingGrade::Sketchy, 20.0f, 9.0f));
		TestEqual(TEXT("ShowJumpCard puts the card up"), HUD->GetJumpCardText(), FString(TEXT("Kiteloop  SKETCHY  20 pts\n9.0 g landing")));
		TestEqual(TEXT("with its grade"), HUD->GetJumpCardGrade(), ELandingGrade::Sketchy);
		TestTrue(TEXT("and no ticker"), HUD->GetTrickTickerText().IsEmpty());
		HUD->UpdateJumpCard(nullptr, RideDeltaTime);
		TestTrue(TEXT("No rider, no card"), HUD->GetJumpCardText().IsEmpty());

		// 2. Integration: the scripted send and pop with the HUD following.
		const FTrackedJump Jump = RunTrackedJump(0.0f, HUD);
		const FJumpRecord& R = Jump.Record;
		UE_LOG(LogKiteSurf, Log, TEXT("JumpCardShowsAfterLanding: readout '%s', card '%s'"), *Jump.ReadoutOnLanding, *Jump.CardOnLanding.Replace(TEXT("\n"), TEXT(" / ")));
		TestTrue(TEXT("The jump was recorded"), Jump.bHasRecord);
		TestFalse(TEXT("A card is up on landing"), Jump.CardOnLanding.IsEmpty());
		TestTrue(FString::Printf(TEXT("It shows the grade %s"), *AKiteSurfHUD::GradeText(R.Grade)), Jump.CardOnLanding.Contains(AKiteSurfHUD::GradeText(R.Grade)));
		TestTrue(FString::Printf(TEXT("and the landing g (%.1f g)"), R.LandingG), Jump.CardOnLanding.Contains(FString::Printf(TEXT("%.1f g"), R.LandingG)));
		TestEqual(TEXT("It is the record's card"), Jump.CardOnLanding, AKiteSurfHUD::FormatJumpCard(R));
		if (Jump.bBoardLandingClean)
		{
			TestNotEqual(TEXT("A clean landing is not graded a crash"), R.Grade, ELandingGrade::Crash);
		}
		TestEqual(TEXT("The readout above still shows what the jump came to"), Jump.ReadoutOnLanding,
			AKiteSurfHUD::FormatJumpResult(Jump.BoardLastApexCm, Jump.BoardLastDistanceCm, Jump.BoardLastAirtime));

		// 3. Expiry. The ride's world has gone; another rider sitting on the water stands in, with
		// a tracker that has recorded nothing.
		FTrickRideFixture Other(15.0f);
		if (Other.IsValid())
		{
			for (int32 Step = 0; Step < 120; ++Step)
			{
				HUD->UpdateJumpCard(Other.Tracker, RideDeltaTime);
			}
			TestEqual(TEXT("Two seconds later the card is still up"), HUD->GetJumpCardText(), Jump.CardOnLanding);
			for (int32 Step = 0; Step < 180; ++Step)
			{
				HUD->UpdateJumpCard(Other.Tracker, RideDeltaTime);
			}
			TestTrue(TEXT("and after five it has gone"), HUD->GetJumpCardText().IsEmpty());
		}
	}
	if (HudWorld)
	{
		HudWorld->DestroyWorld(false);
	}
	return true;
}

#endif
