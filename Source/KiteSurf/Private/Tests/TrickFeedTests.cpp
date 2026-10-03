#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurf.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfUnits.h"
#include "WindComponent.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/KiteLoopRecord.h"
#include "Tricks/LandingMath.h"
#include "Tricks/TrickTrackerComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for what the trick tracker is fed (docs/tricks/T0.md sections 3 and 4): the kite's loop
// records from its own per-step turn, and the board's take-off, apex and landing facts. The fixtures
// are minimal copies of RideLoopTests.cpp's FStandingFixture, FKiteFlight and FRideFixture, and the
// timed send and pop of its RunJump (as TrickTrackerTests.cpp has it).
namespace TrickFeedTestsLocal
{
	const float FeedDeltaTime = 1.0f / 60.0f;

	/** A stationary rider in steady wind along +X; the kite is stepped on its own with UpdateKite. */
	struct FFeedStandingFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;

		explicit FFeedStandingFixture(float WindKnots = 15.0f)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (Pawn)
			{
				Kite = Pawn->GetKite();
				if (UWindComponent* Wind = Pawn->GetWind())
				{
					Wind->BaseWind = FVector(KiteUnits::KnotsToCmS(WindKnots), 0.0f, 0.0f);
					Wind->GustStrength = 0.0f;
					Wind->DirectionDriftDeg = 0.0f;
				}
				Kite->SheetKite(0.7f);
			}
		}

		~FFeedStandingFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		/** Holds the bar at Steer for Seconds, stepping the kite alone. */
		void Fly(float Steer, float Seconds)
		{
			Kite->SteerKite(Steer);
			for (float Elapsed = 0.0f; Elapsed < Seconds; Elapsed += FeedDeltaTime)
			{
				Kite->UpdateKite(FeedDeltaTime);
			}
		}
	};

	/** A pawn in a throwaway world, set up the way the game mode starts a ride, in steady wind along +X. */
	struct FFeedRideFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		UTrickTrackerComponent* Tracker = nullptr;

		explicit FFeedRideFixture(float WindKnots = 30.0f)
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

		~FFeedRideFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Tracker; }

		void Simulate(float Seconds)
		{
			const int32 Steps = FMath::RoundToInt(Seconds / FeedDeltaTime);
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				Pawn->Tick(FeedDeltaTime);
			}
		}

		bool IsAirborne() const { return Board->GetBoardState() == EBoardState::Airborne; }
	};

	/** RideLoopTests' TimedReleaseSeconds: the release of the jump button after the send reaches the kite (s). */
	constexpr float TimedReleaseSeconds = 0.66f;

	/** One loop flown with the bar held over and the loop forced through, until the kite has a new completed record. */
	struct FFlownLoop
	{
		bool bCompleted = false;
		float Seconds = 0.0f;
		/** Every frame that the kite was looping had GetLoopSide equal to the steer, and there was at least one. */
		int32 LoopingFrames = 0;
		int32 WrongSideFrames = 0;
		/** GetLoopDirection as the record completed (the run is still open then). */
		int32 DirectionAtCompletion = 0;
	};

	int32 CountCompleted(const TArray<FKiteLoopRecord>& Records, int32 FirstIndex)
	{
		int32 Count = 0;
		for (const FKiteLoopRecord& Record : Records)
		{
			Count += (Record.Index >= FirstIndex && Record.bCompleted) ? 1 : 0;
		}
		return Count;
	}

	FFlownLoop FlyOneLoop(FFeedStandingFixture& Standing, float Steer, int32 FirstIndex, float TimeoutSeconds)
	{
		FFlownLoop Out;
		UKiteComponent* Kite = Standing.Kite;
		const int32 CompletedBefore = CountCompleted(Kite->GetLoopRecords(), FirstIndex);
		Kite->SetLoopHeld(true);
		Kite->SteerKite(Steer);
		for (; Out.Seconds < TimeoutSeconds && !Kite->IsCrashed(); Out.Seconds += FeedDeltaTime)
		{
			Kite->UpdateKite(FeedDeltaTime);
			if (Kite->IsLooping())
			{
				++Out.LoopingFrames;
				Out.WrongSideFrames += Kite->GetLoopSide() == Steer ? 0 : 1;
			}
			if (CountCompleted(Kite->GetLoopRecords(), FirstIndex) > CompletedBefore)
			{
				Out.bCompleted = true;
				Out.DirectionAtCompletion = Kite->GetLoopDirection();
				break;
			}
		}
		Kite->SetLoopHeld(false);
		Kite->SteerKite(0.0f);
		return Out;
	}
}

using namespace TrickFeedTestsLocal;

// T0 section 3: the kite records its own loops from its per-step turn, independent of the looping
// state. A standing rider at 15 kn loops the kite twice from the zenith each way, letting go of the
// bar for 6 s after each: the old turn counter clears while the records stay, so the records do not
// depend on bLooping. Before the second loop the kite is parked at the zenith again (SetWindowPosition
// drops an open run, never a record), so that both loops start alike; T0's calibration note allows
// re-parking.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickLoopRecordCountsAndDirection, "KiteSurf.Trick.LoopRecordCountsAndDirection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickLoopRecordCountsAndDirection::RunTest(const FString& Parameters)
{
	for (float Steer : { 1.0f, -1.0f })
	{
		FFeedStandingFixture Standing(15.0f);
		UKiteComponent* Kite = Standing.Kite;
		TestNotNull(TEXT("Kite created"), Kite);
		if (!Kite)
		{
			return false;
		}
		Kite->bParkHoldAssist = true;

		// 1. Parked at the zenith.
		Kite->SetWindowPosition(0.0f, 10.0f);
		Standing.Fly(0.0f, 5.0f);
		const float ParkedTensionN = Kite->GetLineTensionN();
		const int32 FirstIndex = Kite->GetLoopRecordCount();
		TestEqual(FString::Printf(TEXT("Steer %+.0f: no loop run open while parked"), Steer), Kite->GetLoopDirection(), 0);

		// 2. Loop 1.
		const FFlownLoop Loop1 = FlyOneLoop(Standing, Steer, FirstIndex, 6.0f);
		TestTrue(FString::Printf(TEXT("Steer %+.0f: the first loop completes a record (%.1f s)"), Steer, Loop1.Seconds), Loop1.bCompleted);
		TestTrue(FString::Printf(TEXT("Steer %+.0f: GetLoopSide is the steer while looping (%d of %d frames wrong)"), Steer, Loop1.WrongSideFrames, Loop1.LoopingFrames),
			Loop1.LoopingFrames > 0 && Loop1.WrongSideFrames == 0);
		TestEqual(FString::Printf(TEXT("Steer %+.0f: the open run turns the steer's way"), Steer), static_cast<float>(Loop1.DirectionAtCompletion), Steer);

		// 3. Bar released: the old counter clears, the records stay.
		const int32 CompletedAfterLoop1 = CountCompleted(Kite->GetLoopRecords(), FirstIndex);
		Standing.Fly(0.0f, 6.0f);
		TestNearlyEqual(FString::Printf(TEXT("Steer %+.0f: the turn counter has cleared (deg)"), Steer), Kite->GetTurnDeg(), 0.0f, 0.1f);
		TestFalse(FString::Printf(TEXT("Steer %+.0f: the kite is no longer looping"), Steer), Kite->IsLooping());
		TestEqual(FString::Printf(TEXT("Steer %+.0f: GetLoopSide is 0 once released"), Steer), Kite->GetLoopSide(), 0.0f);
		TestEqual(FString::Printf(TEXT("Steer %+.0f: the completed record is still there"), Steer), CountCompleted(Kite->GetLoopRecords(), FirstIndex), CompletedAfterLoop1);

		// 4. Loop 2, from the zenith again.
		Kite->SetWindowPosition(0.0f, 10.0f);
		Standing.Fly(0.0f, 3.0f);
		const FFlownLoop Loop2 = FlyOneLoop(Standing, Steer, FirstIndex, 6.0f);
		TestTrue(FString::Printf(TEXT("Steer %+.0f: the second loop completes a record (%.1f s)"), Steer, Loop2.Seconds), Loop2.bCompleted);
		TestTrue(FString::Printf(TEXT("Steer %+.0f: GetLoopSide is the steer in the second loop (%d of %d frames wrong)"), Steer, Loop2.WrongSideFrames, Loop2.LoopingFrames),
			Loop2.LoopingFrames > 0 && Loop2.WrongSideFrames == 0);
		Standing.Fly(0.0f, 6.0f);
		TestEqual(FString::Printf(TEXT("Steer %+.0f: GetLoopSide is 0 after the second release"), Steer), Kite->GetLoopSide(), 0.0f);
		TestFalse(TEXT("The kite stayed out of the water"), Kite->IsCrashed());

		// 5. The records.
		int32 Completed = 0;
		FString Text;
		for (const FKiteLoopRecord& Record : Kite->GetLoopRecords())
		{
			if (Record.Index < FirstIndex)
			{
				continue;
			}
			Text += FString::Printf(TEXT(" [#%d dir %+d %.0f deg %s, %.2f s, elev %.0f->%.0f, peak %.0f N]"), Record.Index, Record.Direction, Record.TurnDeg,
				Record.bCompleted ? TEXT("complete") : TEXT("part"), Record.DurationSeconds, Record.StartElevationDeg, Record.MinElevationDeg, Record.PeakTensionN);
			if (!Record.bCompleted)
			{
				continue;
			}
			++Completed;
			const FString Which = FString::Printf(TEXT("Steer %+.0f, record %d"), Steer, Record.Index);
			TestEqual(Which + TEXT(": turns the steer's way"), static_cast<float>(Record.Direction), Steer);
			TestTrue(FString::Printf(TEXT("%s: the kite dips in the loop (%.0f -> %.0f deg)"), *Which, Record.StartElevationDeg, Record.MinElevationDeg),
				Record.MinElevationDeg < Record.StartElevationDeg - 15.0f);
			TestTrue(FString::Printf(TEXT("%s: the loop pulls at least 1.5 times the parked tension (%.0f vs %.0f N)"), *Which, Record.PeakTensionN, ParkedTensionN),
				Record.PeakTensionN >= 1.5f * ParkedTensionN);
			TestTrue(FString::Printf(TEXT("%s: lasts between 1 and 6 s (%.2f s)"), *Which, Record.DurationSeconds), Record.DurationSeconds > 1.0f && Record.DurationSeconds < 6.0f);
			TestNearlyEqual(Which + TEXT(": a whole turn (deg)"), Record.TurnDeg, 360.0f, 0.01f);
			TestFalse(Which + TEXT(": not a kite crash"), Record.bKiteCrashed);
		}
		UE_LOG(LogKiteSurf, Log, TEXT("LoopRecordCountsAndDirection (steer %+.0f): parked %.0f N, loops %.1f s and %.1f s, records:%s"),
			Steer, ParkedTensionN, Loop1.Seconds, Loop2.Seconds, *Text);
		TestEqual(FString::Printf(TEXT("Steer %+.0f: exactly two completed loop records"), Steer), Completed, 2);
	}
	return true;
}

// T0 section 4: the timed send and pop at 30 kn makes one jump record whose facts are the board's
// own (take-off, apex time, landing g and angle) and match the trajectory sampled every frame; a
// parked pop after it is the second record, and the board's counters agree.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickJumpRecordMatchesTrajectory, "KiteSurf.Trick.JumpRecordMatchesTrajectory", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickJumpRecordMatchesTrajectory::RunTest(const FString& Parameters)
{
	FFeedRideFixture Ride(30.0f);
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	Ride.Simulate(8.0f);
	const int32 TakeoffsBefore = Ride.Board->GetTakeoffCount();
	const int32 ApexesBefore = Ride.Board->GetApexCount();
	TestEqual(TEXT("No take-off in the run-up"), TakeoffsBefore, 0);

	// The timed send and pop of RunJump, the bar centred in the air and a crouch from the apex.
	Ride.Pawn->SteerKite(-1.0f);
	const float SendDeadTimeSeconds = Ride.Kite->GetSteeringDeadTimeSeconds();
	Ride.Board->SetWeightShift(-1.0f);
	Ride.Pawn->SetLoadHeld(true);

	bool bLeftWater = false;
	bool bPulledOffEdge = false;
	bool bCameDown = false;
	int32 AirFrames = 0;
	float MaxZCm = -UE_BIG_NUMBER;
	float MaxZBoardTime = 0.0f;
	float MaxAirTensionN = 0.0f;
	bool bHasFirstAir = false;
	FVector FirstAirLocation = FVector::ZeroVector;
	FVector LandingLocation = FVector::ZeroVector;
	for (float Elapsed = 0.0f; Elapsed < 90.0f; Elapsed += FeedDeltaTime)
	{
		Ride.Simulate(FeedDeltaTime);
		const bool bAir = Ride.IsAirborne();
		if (bAir && !bLeftWater)
		{
			bLeftWater = true;
			bPulledOffEdge = true;
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
		if (bAir || Ride.Kite->GetClockDeg() < 0.0f)
		{
			Ride.Pawn->SteerKite(0.0f); // the assist flies the kite overhead
		}
		if (bAir && Ride.Board->Velocity.Z < 0.0f)
		{
			Ride.Pawn->SetLoadHeld(true); // coming down: crouch for the landing
		}
		const FVector Location = Ride.Pawn->GetActorLocation();
		if (bAir)
		{
			++AirFrames;
			if (!bHasFirstAir)
			{
				bHasFirstAir = true;
				FirstAirLocation = Location;
			}
			if (Location.Z > MaxZCm)
			{
				MaxZCm = static_cast<float>(Location.Z);
				MaxZBoardTime = Ride.Board->GetSimTimeSeconds();
			}
			MaxAirTensionN = FMath::Max(MaxAirTensionN, Ride.Kite->GetLineTensionN());
		}
		if (bLeftWater && !bAir && AirFrames > 18)
		{
			bCameDown = true;
			LandingLocation = Location;
			break;
		}
	}
	Ride.Pawn->SetLoadHeld(false);
	Ride.Pawn->SteerKite(0.0f);
	TestTrue(TEXT("The rider came back down"), bCameDown);

	FJumpRecord R;
	const bool bHasRecord = Ride.Tracker->GetLastJumpRecord(R);
	UE_LOG(LogKiteSurf, Log, TEXT("JumpRecordMatchesTrajectory: '%s', popped %d (pulled off %d), take-off %.3f s, apex %.0f cm at %.3f s (sampled max Z %.0f cm at %.3f s), landing %.3f s, air %.3f s (%d frames), %.0f m, sink %.0f cm/s, %.2f g, angle %.1f deg, peak %.0f N (sampled %.0f N)"),
		*R.TrickName, R.bPopped, bPulledOffEdge, R.TakeoffTimeSeconds, R.ApexHeightCm, R.ApexTimeSeconds, MaxZCm, MaxZBoardTime, R.LandingTimeSeconds,
		R.AirtimeSeconds, AirFrames, R.DistanceCm / 100.0f, R.SinkRateCmS, R.LandingG, R.LandingYawDeg, R.PeakTensionN, MaxAirTensionN);

	TestTrue(TEXT("The jump was recorded"), bHasRecord);
	TestEqual(TEXT("One record"), Ride.Tracker->GetJumpRecordCount(), 1);
	TestEqual(TEXT("Index 0"), R.Index, 0);
	TestEqual(TEXT("Landed"), R.Outcome, EJumpOutcome::Landed);
	TestFalse(TEXT("The timed release, not the kite, took the rider off"), bPulledOffEdge);
	TestTrue(TEXT("so the record says popped"), R.bPopped);
	TestEqual(TEXT("One take-off on the board"), Ride.Board->GetTakeoffCount(), TakeoffsBefore + 1);
	TestTrue(TEXT("and the board says it was popped"), Ride.Board->WasLastTakeoffPopped());
	TestTrue(TEXT("The board saw at least one apex"), Ride.Board->GetApexCount() >= ApexesBefore + 1);

	// Against the sampled trajectory.
	TestNearlyEqual(TEXT("The apex is within 10 cm of the highest sampled Z (cm)"), R.ApexHeightCm, MaxZCm, 10.0f);
	TestNearlyEqual(TEXT("The apex time is within 0.1 s of the frame with the highest Z (s)"), R.ApexTimeSeconds, MaxZBoardTime, 0.1f);
	TestNearlyEqual(TEXT("The airtime is the board's (s)"), R.AirtimeSeconds, Ride.Board->GetLastJumpAirtime(), 1e-4f);
	TestNearlyEqual(TEXT("and within two frames of the airborne frames counted (s)"), R.AirtimeSeconds, AirFrames * FeedDeltaTime, 0.034f);
	TestNearlyEqual(TEXT("The distance is the board's (cm)"), R.DistanceCm, Ride.Board->GetLastJumpDistance(), 0.1f);
	TestNearlyEqual(TEXT("and within 1 m of the first airborne frame to the landing frame (cm)"), R.DistanceCm, static_cast<float>(FVector::Dist2D(FirstAirLocation, LandingLocation)), 100.0f);

	// The board's own facts.
	TestNearlyEqual(TEXT("Take-off time is the board's (s)"), R.TakeoffTimeSeconds, Ride.Board->GetLastTakeoffTimeSeconds(), 1e-4f);
	TestNearlyEqual(TEXT("Apex time is the board's (s)"), R.ApexTimeSeconds, Ride.Board->GetCurrentJumpApexTimeSeconds(), 1e-4f);
	TestTrue(TEXT("Take-off before the apex before the landing"), R.TakeoffTimeSeconds < R.ApexTimeSeconds && R.ApexTimeSeconds < R.LandingTimeSeconds);
	TestNearlyEqual(TEXT("Landing minus take-off is the airtime (s)"), R.LandingTimeSeconds - R.TakeoffTimeSeconds, R.AirtimeSeconds, 0.01f);
	TestTrue(FString::Printf(TEXT("Coming down at touchdown (%.0f cm/s)"), R.SinkRateCmS), R.SinkRateCmS > 0.0f);
	TestNearlyEqual(TEXT("The sink is the board's (cm/s)"), R.SinkRateCmS, KiteUnits::MToCm(Ride.Board->GetLastLandingSinkMS()), 1e-2f);
	TestNearlyEqual(TEXT("The landing g is 1 + v^2/(2 g s) over the board's absorb distance"), R.LandingG,
		LandingMath::ComputeLandingG(R.SinkRateCmS, Ride.Board->GetLastLandingAbsorbCm()), 1e-3f);
	TestNearlyEqual(TEXT("and is the board's"), R.LandingG, Ride.Board->GetLastLandingG(), 1e-4f);
	TestTrue(TEXT("At least one g"), R.LandingG >= 1.0f);
	TestNearlyEqual(TEXT("The landing yaw is the board's landing angle (deg)"), R.LandingYawDeg, Ride.Board->GetLastLandingAngleDeg(), 1e-4f);
	TestTrue(FString::Printf(TEXT("and under the board's crash angle for a clean landing (%.1f deg)"), R.LandingYawDeg), R.LandingYawDeg >= 0.0f && R.LandingYawDeg <= Ride.Board->MaxLandingAngle);
	TestTrue(FString::Printf(TEXT("Peak tension at least the highest sampled in the air (%.0f vs %.0f N)"), R.PeakTensionN, MaxAirTensionN), R.PeakTensionN >= MaxAirTensionN - 1.0f);
	TestTrue(TEXT("and within the cap"), R.PeakTensionN <= Ride.Kite->MaxLineTensionN + 0.1f);

	// A parked pop after it, once the rider is riding again. The kite may pluck the rider off the
	// water in between: such a skip is a take-off (popped false) but not a jump, and makes no record.
	Ride.Simulate(6.0f);
	int32 TakeoffsBeforePop = Ride.Board->GetTakeoffCount();
	bool bPopped = false;
	for (float Elapsed = 0.0f; Elapsed < 10.0f && !bPopped; Elapsed += FeedDeltaTime)
	{
		TakeoffsBeforePop = Ride.Board->GetTakeoffCount();
		bPopped = Ride.Board->Jump() == EJumpRejectReason::None;
		if (!bPopped)
		{
			Ride.Simulate(FeedDeltaTime);
		}
	}
	TestTrue(TEXT("A parked pop leaves the water"), bPopped);
	TestEqual(TEXT("The pop is one take-off"), Ride.Board->GetTakeoffCount(), TakeoffsBeforePop + 1);
	Ride.Simulate(4.0f);
	FJumpRecord Second;
	Ride.Tracker->GetLastJumpRecord(Second);
	UE_LOG(LogKiteSurf, Log, TEXT("JumpRecordMatchesTrajectory: second '%s', popped %d, apex %.0f cm, take-offs %d (%d before the pop), apexes %d, jumps %d"),
		*Second.TrickName, Second.bPopped, Second.ApexHeightCm, Ride.Board->GetTakeoffCount(), TakeoffsBeforePop, Ride.Board->GetApexCount(), Ride.Board->GetJumpCount());
	TestEqual(TEXT("Two records"), Ride.Tracker->GetJumpRecordCount(), 2);
	TestEqual(TEXT("the board counted two jumps"), Ride.Board->GetJumpCount(), 2);
	TestTrue(TEXT("At least two take-offs: the two jumps and any skip"), Ride.Board->GetTakeoffCount() >= 2);
	TestEqual(TEXT("No take-off since the pop's"), Ride.Board->GetTakeoffCount(), TakeoffsBeforePop + 1);
	TestTrue(TEXT("At least two apexes"), Ride.Board->GetApexCount() >= 2);
	TestEqual(TEXT("The second record has index 1"), Second.Index, 1);
	TestTrue(TEXT("and was popped"), Second.bPopped);
	TestTrue(TEXT("It took off after the first landed"), Second.TakeoffTimeSeconds > R.LandingTimeSeconds);
	return true;
}

// The board counts every take-off in BeginAirborne: a pop is popped, the kite lifting the rider off
// is not. Each comes with its time, an apex in the air, and the record says the same.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickTakeoffEventsCountAndPop, "KiteSurf.Trick.TakeoffEventsCountAndPop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfTrickTakeoffEventsCountAndPop::RunTest(const FString& Parameters)
{
	// 1. A pop with the kite parked.
	{
		FFeedRideFixture Ride(30.0f);
		TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
		if (!Ride.IsValid())
		{
			return false;
		}
		Ride.Simulate(8.0f);
		const int32 Takeoffs = Ride.Board->GetTakeoffCount();
		const int32 Apexes = Ride.Board->GetApexCount();
		const float PopTime = Ride.Board->GetSimTimeSeconds();
		TestEqual(TEXT("Pop: the jump is accepted"), Ride.Board->Jump(), EJumpRejectReason::None);
		TestEqual(TEXT("Pop: counted at once"), Ride.Board->GetTakeoffCount(), Takeoffs + 1);
		TestTrue(TEXT("Pop: popped"), Ride.Board->WasLastTakeoffPopped());
		TestNearlyEqual(TEXT("Pop: the take-off time is the board's time at the pop (s)"), Ride.Board->GetLastTakeoffTimeSeconds(), PopTime, 1e-5f);
		TestNearlyEqual(TEXT("Pop: the apex time starts at the take-off (s)"), Ride.Board->GetCurrentJumpApexTimeSeconds(), PopTime, 1e-5f);

		float MaxHeightCm = 0.0f;
		float LandingTime = -1.0f;
		for (float Elapsed = 0.0f; Elapsed < 6.0f && LandingTime < 0.0f; Elapsed += FeedDeltaTime)
		{
			Ride.Simulate(FeedDeltaTime);
			MaxHeightCm = FMath::Max(MaxHeightCm, Ride.Board->GetCurrentJumpHeight());
			if (!Ride.IsAirborne())
			{
				LandingTime = Ride.Board->GetSimTimeSeconds();
			}
		}
		Ride.Simulate(1.0f);
		FJumpRecord Record;
		const bool bHasRecord = Ride.Tracker->GetLastJumpRecord(Record);
		UE_LOG(LogKiteSurf, Log, TEXT("TakeoffEventsCountAndPop: pop at %.3f s, apex %.0f cm at %.3f s, landed %.3f s, apexes %d, record popped %d"),
			PopTime, Ride.Board->GetCurrentJumpApexHeight(), Ride.Board->GetCurrentJumpApexTimeSeconds(), LandingTime, Ride.Board->GetApexCount() - Apexes, Record.bPopped);
		TestTrue(TEXT("Pop: came down"), LandingTime > PopTime);
		TestEqual(TEXT("Pop: still one take-off"), Ride.Board->GetTakeoffCount(), Takeoffs + 1);
		TestEqual(TEXT("Pop: one apex"), Ride.Board->GetApexCount(), Apexes + 1);
		TestNearlyEqual(TEXT("Pop: the apex height is the highest the board went (cm)"), Ride.Board->GetCurrentJumpApexHeight(), MaxHeightCm, 10.0f);
		TestTrue(TEXT("Pop: the apex is between take-off and landing"), Ride.Board->GetCurrentJumpApexTimeSeconds() > PopTime && Ride.Board->GetCurrentJumpApexTimeSeconds() < LandingTime);
		TestTrue(TEXT("Pop: recorded"), bHasRecord);
		TestTrue(TEXT("Pop: the record is popped"), Record.bPopped);
		TestNearlyEqual(TEXT("Pop: the record's take-off time is the pop's (s)"), Record.TakeoffTimeSeconds, PopTime, 1e-5f);
	}

	// 2. The kite sent hard up with no edge and no pop lifts the rider off (RideLoopTests' send only).
	{
		FFeedRideFixture Ride(30.0f);
		if (!Ride.IsValid())
		{
			return false;
		}
		Ride.Simulate(8.0f);
		const int32 Takeoffs = Ride.Board->GetTakeoffCount();
		const int32 Apexes = Ride.Board->GetApexCount();
		Ride.Pawn->SteerKite(-1.0f);
		bool bLifted = false;
		float FrameStart = 0.0f;
		float FrameEnd = 0.0f;
		for (float Elapsed = 0.0f; Elapsed < 10.0f && !bLifted; Elapsed += FeedDeltaTime)
		{
			FrameStart = Ride.Board->GetSimTimeSeconds();
			Ride.Simulate(FeedDeltaTime);
			FrameEnd = Ride.Board->GetSimTimeSeconds();
			bLifted = Ride.IsAirborne();
			if (Ride.Kite->GetClockDeg() < 0.0f)
			{
				Ride.Pawn->SteerKite(0.0f);
			}
		}
		TestTrue(TEXT("Lift-off: the kite took the rider off the water"), bLifted);
		TestEqual(TEXT("Lift-off: one take-off"), Ride.Board->GetTakeoffCount(), Takeoffs + 1);
		TestFalse(TEXT("Lift-off: not popped"), Ride.Board->WasLastTakeoffPopped());
		TestTrue(FString::Printf(TEXT("Lift-off: the take-off time is in the frame it happened (%.3f in %.3f..%.3f s)"), Ride.Board->GetLastTakeoffTimeSeconds(), FrameStart, FrameEnd),
			Ride.Board->GetLastTakeoffTimeSeconds() > FrameStart && Ride.Board->GetLastTakeoffTimeSeconds() <= FrameEnd + 1e-5f);

		Ride.Pawn->SteerKite(0.0f);
		bool bDown = false;
		for (float Elapsed = 0.0f; Elapsed < 30.0f && !bDown; Elapsed += FeedDeltaTime)
		{
			Ride.Simulate(FeedDeltaTime);
			if (Ride.IsAirborne() && Ride.Board->Velocity.Z < 0.0f)
			{
				Ride.Pawn->SetLoadHeld(true); // crouch for the landing
			}
			bDown = !Ride.IsAirborne();
		}
		Ride.Pawn->SetLoadHeld(false);
		Ride.Simulate(1.0f);
		FJumpRecord Record;
		const bool bHasRecord = Ride.Tracker->GetLastJumpRecord(Record);
		UE_LOG(LogKiteSurf, Log, TEXT("TakeoffEventsCountAndPop: lift-off at %.3f s, apex %.0f cm, apexes %d, jumps %d, record %d popped %d"),
			Ride.Board->GetLastTakeoffTimeSeconds(), Ride.Board->GetCurrentJumpApexHeight(), Ride.Board->GetApexCount() - Apexes, Ride.Board->GetJumpCount(), bHasRecord, Record.bPopped);
		TestTrue(TEXT("Lift-off: came down"), bDown);
		TestTrue(TEXT("Lift-off: an apex in the air"), Ride.Board->GetApexCount() >= Apexes + 1);
		TestTrue(TEXT("Lift-off: the jump was recorded"), bHasRecord);
		if (bHasRecord)
		{
			TestFalse(TEXT("Lift-off: the record is not popped"), Record.bPopped);
			TestNearlyEqual(TEXT("Lift-off: the record's take-off time is the board's (s)"), Record.TakeoffTimeSeconds, Ride.Board->GetLastTakeoffTimeSeconds(), 1e-5f);
		}
	}
	return true;
}

#endif
