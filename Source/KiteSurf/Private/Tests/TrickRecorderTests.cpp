#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "KiteSurfUnits.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/JumpRecorder.h"
#include "Tricks/LandingMath.h"
#include "Tricks/TrickScoring.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests on the pure jump recorder (T0.2, docs/tricks/T0.md section 4) and LandingMath: no world,
// no board, no kite. The recorder is fed synthetic per-step snapshots at 240 Hz, shaped like the
// board's counters and Last* fields will be once the wiring PR fills them.

namespace TrickRecorderTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Drives a recorder (or a session) with synthetic snapshots. The kite clock runs KiteOffsetSeconds ahead of the board's. */
	struct FDriver
	{
		FJumpRecorderInput In;
		TArray<FKiteLoopRecord> KiteLoops;
		TArray<FJumpRecord> Finalised;
		TFunction<bool(const FJumpRecorderInput&, FJumpRecord&)> StepFn;
		float Hz = 240.0f;
		int64 Steps = 0;
		float KiteOffsetSeconds = 37.5f;

		explicit FDriver(TFunction<bool(const FJumpRecorderInput&, FJumpRecord&)> InStepFn) : StepFn(MoveTemp(InStepFn))
		{
			In.BoardState = EBoardState::Planing;
			In.TensionN = 900.0f;
			In.KiteElevationDeg = 50.0f;
			In.Velocity = FVector(0.0, 1200.0, 0.0);
			Step(); // primes the counters
		}

		float TimeAt(int64 StepIndex) const { return static_cast<float>(static_cast<double>(StepIndex) / Hz); }
		float Now() const { return TimeAt(Steps); }
		float Next() const { return TimeAt(Steps + 1); }
		float KiteTime(float BoardTime) const { return BoardTime + KiteOffsetSeconds; }

		/** One step: the clocks advance, then the recorder sees the snapshot. True when it finalised a record. */
		bool Step()
		{
			++Steps;
			In.BoardTimeSeconds = Now();
			In.KiteTimeSeconds = KiteTime(Now());
			In.KiteLoops = &KiteLoops;
			FJumpRecord Record;
			const bool bFinalised = StepFn(In, Record);
			if (bFinalised)
			{
				Finalised.Add(Record);
			}
			return bFinalised;
		}

		void StepFor(float Seconds)
		{
			const int32 Count = FMath::RoundToInt(Seconds * Hz);
			for (int32 i = 0; i < Count; ++i)
			{
				Step();
			}
		}

		/** The board leaves the water during the next step. Returns the take-off time. */
		float TakeOff(bool bPopped = true)
		{
			++In.TakeoffCount;
			In.bLastTakeoffPopped = bPopped;
			In.LastTakeoffTimeSeconds = Next();
			In.BoardState = EBoardState::Airborne;
			In.Location.Z = 0.0;
			const float T = In.LastTakeoffTimeSeconds;
			Step();
			return T;
		}

		/** In the air for Seconds; Z(t since take-off) in cm, Tension and Elevation per step when given. */
		void Fly(float TakeoffTime, float Seconds, TFunction<float(float)> Z,
			TFunction<float(float)> Tension = nullptr, TFunction<float(float)> Elevation = nullptr)
		{
			const int32 Count = FMath::RoundToInt(Seconds * Hz);
			for (int32 i = 0; i < Count; ++i)
			{
				const float T = Next() - TakeoffTime;
				In.Location.Z = Z(T);
				if (Tension) { In.TensionN = Tension(T); }
				if (Elevation) { In.KiteElevationDeg = Elevation(T); }
				Step();
			}
		}

		/** The board ends the jump during the next step: JumpCount goes up and the Last* fields are set. True when a record was finalised. */
		bool Land(float TakeoffTime, bool bClean, float SinkCmS, float ApexCm, float ApexTime, float YawDeg = 5.0f, float KiteElevDeg = 60.0f)
		{
			++In.JumpCount;
			In.bLastLandingClean = bClean;
			In.bCrashing = !bClean;
			In.BoardState = EBoardState::Landing;
			In.Location.Z = 0.0;
			In.LastApexCm = ApexCm;
			In.LastApexTimeSeconds = ApexTime;
			In.LastAirtimeSeconds = Next() - TakeoffTime;
			In.LastDistanceCm = 1234.0f;
			In.LastSinkRateCmS = SinkCmS;
			In.LastLandingG = LandingMath::ComputeLandingG(SinkCmS, LandingMath::DefaultLandingAbsorbDistanceCm);
			In.LastLandingAngleDeg = YawDeg;
			In.KiteElevationDeg = KiteElevDeg;
			return Step();
		}

		/** Riding away after a clean landing. */
		void RideAway(float Seconds = 0.5f)
		{
			In.BoardState = EBoardState::Planing;
			In.bCrashing = false;
			StepFor(Seconds);
		}

		/** The board's crash recovery or a respawn: ResetCount goes up and the rider is back on the water. */
		void ResetBoard()
		{
			++In.ResetCount;
			In.BoardState = EBoardState::Planing;
			In.bCrashing = false;
			In.Location.Z = 0.0;
			Step();
		}
	};

	TFunction<bool(const FJumpRecorderInput&, FJumpRecord&)> StepRecorder(FJumpRecorder& Recorder)
	{
		return [&Recorder](const FJumpRecorderInput& In, FJumpRecord& Out) { return Recorder.Step(In, Out); };
	}

	TFunction<bool(const FJumpRecorderInput&, FJumpRecord&)> StepSession(FJumpSession& Session)
	{
		return [&Session](const FJumpRecorderInput& In, FJumpRecord& Out) { return Session.Step(In, &Out); };
	}

	/** A kite loop record on the kite clock. */
	FKiteLoopRecord KiteLoop(int32 Index, int32 Direction, float StartKiteTime, float DurationS, float TurnDeg, bool bCompleted,
		float StartElevationDeg = 70.0f, float MinElevationDeg = 40.0f, float PeakTensionN = 1500.0f, float RiderZCm = 400.0f)
	{
		FKiteLoopRecord Loop;
		Loop.Index = Index;
		Loop.Direction = Direction;
		Loop.StartTimeSeconds = StartKiteTime;
		Loop.DurationSeconds = DurationS;
		Loop.TurnDeg = TurnDeg;
		Loop.bCompleted = bCompleted;
		Loop.StartElevationDeg = StartElevationDeg;
		Loop.MinElevationDeg = MinElevationDeg;
		Loop.PeakTensionN = PeakTensionN;
		Loop.PeakTensionTimeSeconds = StartKiteTime + 0.5f * DurationS;
		Loop.RiderZAtStartCm = RiderZCm;
		Loop.RiderTravelSide = Direction; // natural loops, not contra
		return Loop;
	}

	/** A ballistic arc peaking at PeakS after take-off, PeakCm high (cm). */
	TFunction<float(float)> Arc(float PeakS, float PeakCm)
	{
		return [PeakS, PeakCm](float T) { return FMath::Max(0.0f, PeakCm * (1.0f - FMath::Square((T - PeakS) / PeakS))); };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickJumpRecorderEdgeCases, "KiteSurf.Trick.JumpRecorderEdgeCases", TrickRecorderTest::Flags)

bool FKiteSurfTrickJumpRecorderEdgeCases::RunTest(const FString& Parameters)
{
	using namespace TrickRecorderTest;

	// Priming: counters already past zero produce nothing on the first step.
	{
		FJumpRecorder Recorder;
		FJumpRecorderInput In;
		In.TakeoffCount = 7;
		In.JumpCount = 6;
		In.ResetCount = 2;
		In.BoardState = EBoardState::Airborne;
		FJumpRecord Out;
		TestFalse(TEXT("Priming step: no record"), Recorder.Step(In, Out));
		TestFalse(TEXT("Priming step: nothing open"), Recorder.IsJumpOpen());
		In.BoardTimeSeconds = 1.0f / 240.0f;
		TestFalse(TEXT("Same counters next step: no record"), Recorder.Step(In, Out));
		TestFalse(TEXT("Same counters next step: nothing open"), Recorder.IsJumpOpen());
	}

	FJumpRecorder Recorder;
	FDriver D(StepRecorder(Recorder));

	// Skip: airborne, then back on the water without the jump being counted.
	{
		const float T0 = D.TakeOff(false);
		TestTrue(TEXT("Skip: open after the take-off"), Recorder.IsJumpOpen());
		D.Fly(T0, 0.2f, Arc(0.1f, 30.0f));
		TestTrue(TEXT("Skip: open in the air"), Recorder.IsJumpOpen());
		D.In.BoardState = EBoardState::Planing;
		D.Step();
		TestFalse(TEXT("Skip: not open once back on the water"), Recorder.IsJumpOpen());
		D.StepFor(0.5f);
		TestEqual(TEXT("Skip: no record"), D.Finalised.Num(), 0);
	}

	// Reset mid-air: dropped, and the landing count that follows is not a jump.
	{
		const float T0 = D.TakeOff();
		D.Fly(T0, 0.8f, Arc(1.0f, 400.0f));
		TestTrue(TEXT("Reset: open in the air"), Recorder.IsJumpOpen());
		D.ResetBoard();
		TestFalse(TEXT("Reset: not open after the reset"), Recorder.IsJumpOpen());
		D.StepFor(0.2f);
		++D.In.JumpCount;
		D.Step();
		TestEqual(TEXT("Reset: no record"), D.Finalised.Num(), 0);
	}

	// Landing with no take-off seen: the board was set airborne by hand.
	{
		D.In.BoardState = EBoardState::Airborne;
		D.StepFor(0.5f);
		TestFalse(TEXT("No take-off: nothing open while airborne"), Recorder.IsJumpOpen());
		const bool bFinalised = D.Land(D.Now(), true, 300.0f, 200.0f, D.Now());
		TestFalse(TEXT("No take-off: landing finalises nothing"), bFinalised);
		D.RideAway();
		TestEqual(TEXT("No take-off: no record"), D.Finalised.Num(), 0);
	}

	// Mid-air crash that is not a landing (the spot's TriggerCrash): open until the recovery's reset drops it.
	{
		const float T0 = D.TakeOff();
		D.Fly(T0, 0.5f, Arc(1.0f, 400.0f));
		D.In.bCrashing = true;
		D.In.BoardState = EBoardState::Landing;
		D.StepFor(0.5f);
		TestTrue(TEXT("Spot crash: still open while crashing"), Recorder.IsJumpOpen());
		D.ResetBoard();
		TestFalse(TEXT("Spot crash: dropped by the reset"), Recorder.IsJumpOpen());
		TestEqual(TEXT("Spot crash: no record"), D.Finalised.Num(), 0);
	}

	// Crash landing: Crashed, g filled, a crash grade and no score; the recovery's reset adds nothing.
	{
		const float T0 = D.TakeOff();
		D.Fly(T0, 2.0f, Arc(1.0f, 500.0f));
		const bool bFinalised = D.Land(T0, false, 600.0f, 500.0f, T0 + 1.0f, 40.0f);
		TestTrue(TEXT("Crash: finalised on the landing step"), bFinalised);
		TestFalse(TEXT("Crash: nothing open"), Recorder.IsJumpOpen());
		if (TestEqual(TEXT("Crash: one record"), D.Finalised.Num(), 1))
		{
			const FJumpRecord& R = D.Finalised[0];
			TestEqual(TEXT("Crash: index 0"), R.Index, 0);
			TestEqual(TEXT("Crash: outcome"), R.Outcome, EJumpOutcome::Crashed);
			TestNearlyEqual(TEXT("Crash: sink rate (cm/s)"), R.SinkRateCmS, 600.0f, 1e-4f);
			TestNearlyEqual(TEXT("Crash: landing g"), R.LandingG, LandingMath::ComputeLandingG(600.0f, LandingMath::DefaultLandingAbsorbDistanceCm), 1e-4f);
			TestTrue(TEXT("Crash: g over 5"), R.LandingG > 5.0f);
			TestNearlyEqual(TEXT("Crash: landing yaw (deg)"), R.LandingYawDeg, 40.0f, 1e-4f);
			TestEqual(TEXT("Crash: grade"), R.Grade, ELandingGrade::Crash);
			TestNearlyEqual(TEXT("Crash: scores 0"), R.Score.Total, 0.0f, 1e-6f);
		}
		D.StepFor(1.5f);
		D.ResetBoard();
		D.StepFor(0.5f);
		TestEqual(TEXT("Crash: the recovery adds no record"), D.Finalised.Num(), 1);
	}

	// Apex time from the board's height when the board does not supply one.
	{
		const float T0 = D.TakeOff();
		D.Fly(T0, 2.0f, Arc(0.75f, 300.0f));
		D.Land(T0, true, 300.0f, 300.0f, -1.0f);
		D.RideAway();
		if (TestEqual(TEXT("Apex fallback: two records"), D.Finalised.Num(), 2))
		{
			TestNearlyEqual(TEXT("Apex fallback: apex time at the highest step (s)"), D.Finalised[1].ApexTimeSeconds - T0, 0.75f, 1.0f / 240.0f + 1e-4f);
			TestEqual(TEXT("Apex fallback: index 1"), D.Finalised[1].Index, 1);
		}
	}

	// Loop overlap on the kite clock (37.5 s ahead of the board's): one record ending before the
	// take-off, one inside, one starting after the landing, and an open 200 deg run at the landing.
	{
		D.KiteLoops.Reset();
		D.In.TensionN = 900.0f;
		D.In.KiteElevationDeg = 50.0f;
		D.In.Velocity = FVector(300.0, 1200.0, 0.0);
		const float T0 = D.TakeOff(true);
		const float K0 = D.KiteTime(T0);
		const float ApexT = T0 + 1.5f;
		D.KiteLoops.Add(KiteLoop(10, 1, K0 - 3.0f, 1.5f, 360.0f, true));  // ends 1.5 s before the take-off
		D.KiteLoops.Add(KiteLoop(11, 1, K0 + 0.5f, 1.2f, 360.0f, true));  // inside
		D.KiteLoops.Add(KiteLoop(12, 1, K0 + 3.5f, 1.0f, 360.0f, true));  // starts 0.5 s after the landing
		D.In.bHasOpenLoop = true;
		D.In.OpenLoop = KiteLoop(-1, 1, K0 + 2.0f, 1.0f, 200.0f, false);
		D.Fly(T0, 3.0f - 1.0f / 240.0f, Arc(1.5f, 1100.0f),
			[](float T) { return 900.0f + 1000.0f * FMath::Sin(PI * FMath::Min(T / 3.0f, 1.0f)); }, // peaks at 1900 N at 1.5 s
			[](float T) { return 50.0f - 20.0f * FMath::Sin(PI * FMath::Min(T / 3.0f, 1.0f)); }); // down to 30 deg
		TestTrue(TEXT("Loops: open in the air"), Recorder.IsJumpOpen());
		TestTrue(TEXT("Loops: live peak tension rising"), Recorder.GetLive().PeakTensionN > 1800.0f);
		const bool bFinalised = D.Land(T0, true, 300.0f, 1100.0f, ApexT);
		TestTrue(TEXT("Loops: finalised"), bFinalised);
		D.In.bHasOpenLoop = false;
		D.RideAway();
		if (TestEqual(TEXT("Loops: three records"), D.Finalised.Num(), 3))
		{
			const FJumpRecord& R = D.Finalised[2];
			TestEqual(TEXT("Loops: index 2"), R.Index, 2);
			TestEqual(TEXT("Loops: landed"), R.Outcome, EJumpOutcome::Landed);
			TestTrue(TEXT("Loops: popped"), R.bPopped);
			TestNearlyEqual(TEXT("Loops: take-off time (s)"), R.TakeoffTimeSeconds, T0, 1e-6f);
			TestNearlyEqual(TEXT("Loops: apex time (s)"), R.ApexTimeSeconds, ApexT, 1e-6f);
			TestNearlyEqual(TEXT("Loops: landing - take-off is the airtime (s)"), R.LandingTimeSeconds - R.TakeoffTimeSeconds, R.AirtimeSeconds, 1e-4f);
			TestNearlyEqual(TEXT("Loops: airtime (s)"), R.AirtimeSeconds, 3.0f, 1e-3f);
			TestNearlyEqual(TEXT("Loops: take-off speed (cm/s)"), R.TakeoffSpeedCmS, FMath::Sqrt(300.0f * 300.0f + 1200.0f * 1200.0f), 0.01f);
			TestNearlyEqual(TEXT("Loops: take-off tension (N)"), R.TakeoffTensionN, 900.0f, 1e-3f);
			TestNearlyEqual(TEXT("Loops: peak tension (N)"), R.PeakTensionN, 1900.0f, 0.5f);
			TestNearlyEqual(TEXT("Loops: lowest kite elevation (deg)"), R.MinKiteElevationDeg, 30.0f, 0.05f);
			TestNearlyEqual(TEXT("Loops: kite elevation at landing (deg)"), R.KiteElevationAtLandingDeg, 60.0f, 1e-4f);
			TestNearlyEqual(TEXT("Loops: apex from the board (cm)"), R.ApexHeightCm, 1100.0f, 1e-3f);
			TestNearlyEqual(TEXT("Loops: distance from the board (cm)"), R.DistanceCm, 1234.0f, 1e-3f);
			TestNearlyEqual(TEXT("Loops: landing g"), R.LandingG, LandingMath::ComputeLandingG(300.0f, LandingMath::DefaultLandingAbsorbDistanceCm), 1e-4f);
			if (TestEqual(TEXT("Loops: the inside record and the open run"), R.Loops.Num(), 2))
			{
				TestEqual(TEXT("Loops: first is the inside record"), R.Loops[0].Loop.Index, 11);
				TestNearlyEqual(TEXT("Loops: inside StartSinceTakeoff (s)"), R.Loops[0].StartSinceTakeoffSeconds, 0.5f, 1e-4f);
				TestNearlyEqual(TEXT("Loops: inside StartSinceApex (s)"), R.Loops[0].StartSinceApexSeconds, -1.0f, 1e-4f);
				TestNearlyEqual(TEXT("Loops: inside rider height (cm)"), R.Loops[0].RiderHeightAtStartCm, 400.0f, 1e-3f);
				TestFalse(TEXT("Loops: second is the open run"), R.Loops[1].Loop.bCompleted);
				TestNearlyEqual(TEXT("Loops: open run turn (deg)"), R.Loops[1].Loop.TurnDeg, 200.0f, 1e-4f);
				TestNearlyEqual(TEXT("Loops: open StartSinceTakeoff (s)"), R.Loops[1].StartSinceTakeoffSeconds, 2.0f, 1e-4f);
				TestNearlyEqual(TEXT("Loops: open StartSinceApex (s)"), R.Loops[1].StartSinceApexSeconds, 0.5f, 1e-4f);
			}
			TestEqual(TEXT("Loops: one completed"), R.CountCompletedLoops(), 1);
		}
	}

	// An open run under 180 deg is not attached; a record straddling the take-off is.
	{
		D.KiteLoops.Reset();
		const float T0 = D.TakeOff();
		const float K0 = D.KiteTime(T0);
		D.KiteLoops.Add(KiteLoop(20, -1, K0 - 0.5f, 1.0f, 360.0f, true)); // straddles the take-off
		D.In.bHasOpenLoop = true;
		D.In.OpenLoop = KiteLoop(-1, -1, K0 + 1.0f, 0.6f, 150.0f, false);
		D.Fly(T0, 2.0f, Arc(1.0f, 500.0f));
		D.Land(T0, true, 300.0f, 500.0f, T0 + 1.0f);
		D.In.bHasOpenLoop = false;
		D.RideAway();
		if (TestEqual(TEXT("Short run: four records"), D.Finalised.Num(), 4))
		{
			const FJumpRecord& R = D.Finalised[3];
			if (TestEqual(TEXT("Short run: only the straddling record"), R.Loops.Num(), 1))
			{
				TestEqual(TEXT("Short run: it is record 20"), R.Loops[0].Loop.Index, 20);
				TestNearlyEqual(TEXT("Short run: started before the take-off (s)"), R.Loops[0].StartSinceTakeoffSeconds, -0.5f, 1e-4f);
			}
		}
	}

	// A take-off while a jump is still open drops the old one and opens the new one.
	{
		const float T0 = D.TakeOff();
		D.Fly(T0, 0.5f, Arc(1.0f, 400.0f));
		const float T1 = D.TakeOff();
		TestTrue(TEXT("Second take-off: open"), Recorder.IsJumpOpen());
		TestNearlyEqual(TEXT("Second take-off: the live record is the new jump (s)"), Recorder.GetLive().TakeoffTimeSeconds, T1, 1e-6f);
		D.Fly(T1, 1.0f, Arc(0.5f, 200.0f));
		D.Land(T1, true, 300.0f, 200.0f, T1 + 0.5f);
		D.RideAway();
		if (TestEqual(TEXT("Second take-off: one more record"), D.Finalised.Num(), 5))
		{
			TestNearlyEqual(TEXT("Second take-off: recorded from the second take-off (s)"), D.Finalised[4].TakeoffTimeSeconds, T1, 1e-6f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickJumpRecorderNamesAndScores, "KiteSurf.Trick.JumpRecorderNamesAndScores", TrickRecorderTest::Flags)

bool FKiteSurfTrickJumpRecorderNamesAndScores::RunTest(const FString& Parameters)
{
	using namespace TrickRecorderTest;

	FJumpSession Session;
	FDriver D(StepSession(Session));

	// Two completed right kiteloops, 0.1 s apart, both before the apex at 2 s; a clean 3 m/s landing
	// with the kite at 60 deg and 5 deg of yaw (stomped).
	auto DoubleKiteloop = [&D](bool bClean)
	{
		D.KiteLoops.Reset();
		const float T0 = D.TakeOff();
		const float K0 = D.KiteTime(T0);
		D.KiteLoops.Add(KiteLoop(0, 1, K0 + 0.3f, 1.2f, 360.0f, true));
		D.KiteLoops.Add(KiteLoop(1, 1, K0 + 1.6f, 1.2f, 360.0f, true));
		D.Fly(T0, 4.0f, Arc(2.0f, 1200.0f));
		D.Land(T0, bClean, 300.0f, 1200.0f, T0 + 2.0f, bClean ? 5.0f : 40.0f);
		D.RideAway();
	};

	DoubleKiteloop(true);
	float FirstTotal = 0.0f;
	if (TestEqual(TEXT("First: one record"), D.Finalised.Num(), 1))
	{
		const FJumpRecord& R = D.Finalised[0];
		TestEqual(TEXT("First: name"), R.TrickName, FString(TEXT("Double kiteloop")));
		TestFalse(TEXT("First: family key"), R.FamilyKey.IsEmpty());
		TestEqual(TEXT("First: grade"), R.Grade, ELandingGrade::Stomped);
		TestEqual(TEXT("First: two loops"), R.Loops.Num(), 2);
		TestTrue(TEXT("First: non-zero score"), R.Score.Total > 0.0f);
		TestTrue(TEXT("First: extremity from the loops"), R.Score.Extremity > 0.0f);
		TestNearlyEqual(TEXT("First: repeat factor"), R.RepeatFactor, 1.0f, 1e-6f);
		FirstTotal = R.Score.Total;
	}

	DoubleKiteloop(true);
	if (TestEqual(TEXT("Second: two records"), D.Finalised.Num(), 2))
	{
		const FJumpRecord& R = D.Finalised[1];
		TestEqual(TEXT("Second: same name"), R.TrickName, FString(TEXT("Double kiteloop")));
		TestEqual(TEXT("Second: same family"), R.FamilyKey, D.Finalised[0].FamilyKey);
		TestEqual(TEXT("Second: index 1"), R.Index, 1);
		TestNearlyEqual(TEXT("Second: same raw score"), R.Score.Total, FirstTotal, 1e-3f);
		TestNearlyEqual(TEXT("Second: repeat factor"), R.RepeatFactor, 0.75f, 1e-6f);
	}
	TestNearlyEqual(TEXT("Session points: 1 + 0.75 of the score"), Session.GetSessionPoints(), 1.75f * FirstTotal, 1e-2f);

	// A crash of the same trick is not a landing: no points, and the repeat count does not move.
	DoubleKiteloop(false);
	if (TestEqual(TEXT("Crash: three records"), D.Finalised.Num(), 3))
	{
		const FJumpRecord& R = D.Finalised[2];
		TestEqual(TEXT("Crash: outcome"), R.Outcome, EJumpOutcome::Crashed);
		TestNearlyEqual(TEXT("Crash: scores 0"), R.Score.Total, 0.0f, 1e-6f);
		TestNearlyEqual(TEXT("Crash: would have been paid 0.5"), R.RepeatFactor, 0.5f, 1e-6f);
	}
	DoubleKiteloop(true);
	if (TestEqual(TEXT("Third landing: four records"), D.Finalised.Num(), 4))
	{
		TestNearlyEqual(TEXT("Third landing: repeat factor 0.5"), D.Finalised[3].RepeatFactor, 0.5f, 1e-6f);
	}
	TestEqual(TEXT("Session record count"), Session.GetRecordCount(), 4);
	TestEqual(TEXT("Session keeps all four"), Session.GetRecords().Num(), 4);
	TestEqual(TEXT("Three landings counted for the family"), Session.GetTrickSession().GetCount(D.Finalised[0].FamilyKey), 3);
	{
		FJumpRecord Last;
		TestTrue(TEXT("Last record exists"), Session.GetLastRecord(Last));
		TestEqual(TEXT("Last record is index 3"), Last.Index, 3);
	}

	// A straight air is its own family: full pay.
	{
		D.KiteLoops.Reset();
		const float T0 = D.TakeOff();
		D.Fly(T0, 1.5f, Arc(0.75f, 400.0f));
		D.Land(T0, true, 300.0f, 400.0f, T0 + 0.75f);
		D.RideAway();
		if (TestEqual(TEXT("Straight air: five records"), D.Finalised.Num(), 5))
		{
			TestEqual(TEXT("Straight air: name"), D.Finalised[4].TrickName, FString(TEXT("Straight air")));
			TestNearlyEqual(TEXT("Straight air: repeat factor"), D.Finalised[4].RepeatFactor, 1.0f, 1e-6f);
		}
	}

	// The session keeps the last MaxRecords records and keeps counting.
	{
		FJumpSession Ring;
		Ring.MaxRecords = 3;
		FDriver R(StepSession(Ring));
		for (int32 i = 0; i < 5; ++i)
		{
			const float T0 = R.TakeOff();
			R.Fly(T0, 1.0f, Arc(0.5f, 200.0f));
			R.Land(T0, true, 300.0f, 200.0f, T0 + 0.5f);
			R.RideAway();
		}
		TestEqual(TEXT("Ring: five counted"), Ring.GetRecordCount(), 5);
		if (TestEqual(TEXT("Ring: three kept"), Ring.GetRecords().Num(), 3))
		{
			TestEqual(TEXT("Ring: the oldest kept is index 2"), Ring.GetRecords()[0].Index, 2);
			TestEqual(TEXT("Ring: the newest is index 4"), Ring.GetRecords()[2].Index, 4);
		}
		TestEqual(TEXT("Default MaxRecords"), FJumpSession().MaxRecords, 200);
		Ring.Clear();
		TestEqual(TEXT("Cleared: count"), Ring.GetRecordCount(), 0);
		TestEqual(TEXT("Cleared: records"), Ring.GetRecords().Num(), 0);
		TestNearlyEqual(TEXT("Cleared: points"), Ring.GetSessionPoints(), 0.0f, 1e-6f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfJumpLandingGIsAG, "KiteSurf.Jump.LandingGIsAG", TrickRecorderTest::Flags)

bool FKiteSurfJumpLandingGIsAG::RunTest(const FString& Parameters)
{
	// 1 + v^2 / (2 g s) at s = 30 cm.
	TestNearlyEqual(TEXT("2 m/s sink over 30 cm (g)"), LandingMath::ComputeLandingG(200.0f, 30.0f), 1.68f, 0.01f);
	TestNearlyEqual(TEXT("6 m/s sink over 30 cm (g)"), LandingMath::ComputeLandingG(600.0f, 30.0f), 7.12f, 0.02f);
	TestNearlyEqual(TEXT("No sink is just the weight (g)"), LandingMath::ComputeLandingG(0.0f, 30.0f), 1.0f, 1e-6f);
	TestNearlyEqual(TEXT("Rising at contact counts as no sink (g)"), LandingMath::ComputeLandingG(-100.0f, 30.0f), 1.0f, 1e-6f);
	TestNearlyEqual(TEXT("Default absorb distance (cm)"), LandingMath::DefaultLandingAbsorbDistanceCm, 45.0f, 1e-6f);
	// The ratio of the squares: three times the sink is nine times the extra g.
	const float Extra2 = LandingMath::ComputeLandingG(200.0f, 30.0f) - 1.0f;
	const float Extra6 = LandingMath::ComputeLandingG(600.0f, 30.0f) - 1.0f;
	TestNearlyEqual(TEXT("Extra g scales with the square of the sink"), Extra6 / Extra2, 9.0f, 1e-3f);
	// Twice the absorb distance halves the extra g.
	TestNearlyEqual(TEXT("Extra g halves with twice the absorb distance"),
		LandingMath::ComputeLandingG(600.0f, 60.0f) - 1.0f, 0.5f * Extra6, 1e-3f);
	TestTrue(TEXT("A zero absorb distance stays finite"), FMath::IsFinite(LandingMath::ComputeLandingG(600.0f, 0.0f)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
