#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "KiteSurfUnits.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/KiteLoopTracker.h"
#include "Tricks/TrickNaming.h"
#include "Tricks/TrickRecognition.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests on the pure kite loop tracker (T0.3) and the loop-family classifier (T2.4): no world, no
// kite. The tracker is fed synthetic heading turns; the classifier synthetic or tracker-made loop
// records. Thresholds are the estimates in FKiteLoopTrackerSettings and FLoopClassifySettings.

namespace TrickLoopTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Drives an FKiteLoopTracker with synthetic kite steps at a fixed rate. The rider rides right (+Y) with the wind along +X. */
	struct FFeeder
	{
		FKiteLoopTracker Tracker;
		float Hz = 240.0f;
		int64 Steps = 0;
		/** Kite elevation (deg) and line tension (N) as functions of time; constant when unset. */
		TFunction<float(float)> Elevation;
		TFunction<float(float)> Tension;
		FVector RiderVelocity = FVector(0.0, 800.0, 0.0);

		explicit FFeeder(float InHz = 240.0f) : Hz(InHz)
		{
			Feed(0.0f, false); // the clock starts at t = 0 with no turn
		}

		float Now() const { return static_cast<float>(static_cast<double>(Steps) / Hz); }

		void Feed(float TurnDeg, bool bCrashed)
		{
			FKiteLoopSample Sample;
			Sample.TimeSeconds = Now();
			Sample.TurnDeg = TurnDeg;
			Sample.ElevationDeg = Elevation ? Elevation(Sample.TimeSeconds) : 60.0f;
			Sample.TensionN = Tension ? Tension(Sample.TimeSeconds) : 1000.0f;
			Sample.RiderZCm = 500.0f;
			Sample.RiderVelocity = RiderVelocity;
			Sample.DownwindDir = FVector::ForwardVector;
			Sample.bCrashed = bCrashed;
			Tracker.Step(Sample);
		}

		/** Turns by exactly Deg (signed) at about RateDegPerS. */
		void TurnBy(float Deg, float RateDegPerS = 180.0f)
		{
			const int32 Count = FMath::Max(1, FMath::CeilToInt(FMath::Abs(Deg) * Hz / RateDegPerS));
			for (int32 i = 0; i < Count; ++i)
			{
				++Steps;
				Feed(Deg / Count, false);
			}
		}

		/** Turns at RateDegPerS (signed) for Seconds. */
		void TurnFor(float RateDegPerS, float Seconds)
		{
			const int32 Count = FMath::RoundToInt(Seconds * Hz);
			for (int32 i = 0; i < Count; ++i)
			{
				++Steps;
				Feed(RateDegPerS / Hz, false);
			}
		}

		/** No turn for Seconds; bCrashed puts the kite on the water. */
		void Hold(float Seconds, bool bCrashed = false)
		{
			const int32 Count = FMath::RoundToInt(Seconds * Hz);
			for (int32 i = 0; i < Count; ++i)
			{
				++Steps;
				Feed(0.0f, bCrashed);
			}
		}

		const TArray<FKiteLoopRecord>& Records() const { return Tracker.GetRecords(); }
	};

	/** A loop record placed in a jump. StartS is from take-off; the apex is at ApexS. */
	FJumpLoop Loop(int32 Direction, float StartS, float DurationS, float TurnDeg, bool bCompleted,
		float StartElevationDeg = 70.0f, float MinElevationDeg = 40.0f, float PeakTensionN = 1500.0f,
		float RiderHeightCm = 1500.0f, float ApexS = 3.0f, int32 TravelSide = 1)
	{
		FJumpLoop Result;
		Result.Loop.Index = 0;
		Result.Loop.Direction = Direction;
		Result.Loop.StartTimeSeconds = 100.0f + StartS;
		Result.Loop.DurationSeconds = DurationS;
		Result.Loop.TurnDeg = TurnDeg;
		Result.Loop.bCompleted = bCompleted;
		Result.Loop.StartElevationDeg = StartElevationDeg;
		Result.Loop.MinElevationDeg = MinElevationDeg;
		Result.Loop.PeakTensionN = PeakTensionN;
		Result.Loop.PeakTensionTimeSeconds = Result.Loop.StartTimeSeconds + 0.5f * DurationS;
		Result.Loop.RiderTravelSide = TravelSide;
		Result.StartSinceTakeoffSeconds = StartS;
		Result.StartSinceApexSeconds = StartS - ApexS;
		Result.RiderHeightAtStartCm = RiderHeightCm;
		return Result;
	}

	/** A completed kiteloop (kite to 40 deg from 70, 15 m up, before the apex). */
	FJumpLoop Kiteloop(int32 Direction, float StartS, float DurationS = 1.5f)
	{
		return Loop(Direction, StartS, DurationS, 360.0f, true);
	}

	/** A completed megaloop: 15 m up, kite to 10 deg, 3000 N (3.6 body weights). */
	FJumpLoop Megaloop(int32 Direction, float StartS, float DurationS = 2.0f)
	{
		return Loop(Direction, StartS, DurationS, 360.0f, true, 70.0f, 10.0f, 3000.0f);
	}

	/** An unfinished half of TurnDeg. */
	FJumpLoop Half(int32 Direction, float StartS, float TurnDeg = 200.0f, float DurationS = 1.0f)
	{
		return Loop(Direction, StartS, DurationS, TurnDeg, false);
	}

	/** Tracker records as jump loops, with take-off at TakeoffSeconds of kite time. */
	TArray<FJumpLoop> AsJumpLoops(const TArray<FKiteLoopRecord>& Records, float TakeoffSeconds)
	{
		TArray<FJumpLoop> Result;
		for (const FKiteLoopRecord& Record : Records)
		{
			FJumpLoop JumpLoop;
			JumpLoop.Loop = Record;
			JumpLoop.StartSinceTakeoffSeconds = Record.StartTimeSeconds - TakeoffSeconds;
			JumpLoop.StartSinceApexSeconds = -1.0f;
			JumpLoop.RiderHeightAtStartCm = 1000.0f;
			Result.Add(JumpLoop);
		}
		return Result;
	}

	FString KindsOf(const TArray<FTrickLoop>& Loops)
	{
		TArray<FString> Parts;
		for (const FTrickLoop& L : Loops)
		{
			FString Part = StaticEnum<ETrickLoopKind>()->GetNameStringByValue(static_cast<int64>(L.Kind));
			Parts.Add(L.bContra ? Part + TEXT("(contra)") : Part);
		}
		return FString::Join(Parts, TEXT(","));
	}

	FString NameOf(const TArray<FJumpLoop>& Loops)
	{
		FJumpRecord Record;
		Record.Index = 0;
		Record.ApexHeightCm = 2000.0f;
		Record.AirtimeSeconds = 7.0f;
		Record.LandingYawDeg = 5.0f;
		Record.LandingG = 3.0f;
		Record.KiteElevationAtLandingDeg = 60.0f;
		Record.Loops = Loops;
		return TrickNaming::Name(TrickRecognition::SignatureFromJump(Record));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickLoopTrackerSynthetic, "KiteSurf.Trick.LoopTrackerSynthetic", TrickLoopTest::Flags)

bool FKiteSurfTrickLoopTrackerSynthetic::RunTest(const FString& Parameters)
{
	using namespace TrickLoopTest;

	// +180 deg/s for 4.2 s: two whole loops of 2 s each; the 36 deg left over is no record.
	for (const float Hz : { 240.0f, 60.0f })
	{
		FFeeder F(Hz);
		F.TurnFor(180.0f, 4.2f);
		TestEqual(FString::Printf(TEXT("%.0f Hz: run open to the right while turning"), Hz), F.Tracker.GetRunDirection(), 1);
		TestNearlyEqual(FString::Printf(TEXT("%.0f Hz: run turn after 4.2 s (deg)"), Hz), F.Tracker.GetRunTurnDeg(), 756.0f, 1.0f);
		FKiteLoopRecord Open;
		TestTrue(TEXT("An open run is reported"), F.Tracker.GetOpenRun(Open));
		TestNearlyEqual(TEXT("The open loop has turned 36 deg since the last whole one"), Open.TurnDeg, 36.0f, 1.0f);
		F.Hold(1.0f);
		TestEqual(TEXT("The stall closed the run"), F.Tracker.GetRunDirection(), 0);
		TestEqual(FString::Printf(TEXT("%.0f Hz: +180 deg/s for 4.2 s gives 2 records"), Hz), F.Records().Num(), 2);
		for (const FKiteLoopRecord& R : F.Records())
		{
			TestTrue(TEXT("Completed"), R.bCompleted);
			TestEqual(TEXT("Direction +1"), R.Direction, 1);
			TestNearlyEqual(FString::Printf(TEXT("%.0f Hz: loop %d lasts 2 s (s)"), Hz, R.Index), R.DurationSeconds, 2.0f, 0.05f);
			TestEqual(TEXT("Riding right: travel side +1"), R.RiderTravelSide, 1);
		}
		if (F.Records().Num() == 2)
		{
			TestEqual(TEXT("Indexes count up"), F.Records()[1].Index, 1);
			TestNearlyEqual(TEXT("The second loop starts where the first ended (s)"), F.Records()[1].StartTimeSeconds,
				F.Records()[0].StartTimeSeconds + F.Records()[0].DurationSeconds, 0.01f);
		}
	}

	// The same at -180 deg/s.
	{
		FFeeder F;
		F.TurnFor(-180.0f, 4.2f);
		F.Hold(1.0f);
		TestEqual(TEXT("-180 deg/s for 4.2 s gives 2 records"), F.Records().Num(), 2);
		for (const FKiteLoopRecord& R : F.Records())
		{
			TestEqual(TEXT("Direction -1"), R.Direction, -1);
			TestTrue(TEXT("Completed"), R.bCompleted);
		}
	}

	// +-5 deg of heading jitter at 2 Hz for 10 s: never a loop.
	{
		FFeeder F;
		float Previous = 0.0f;
		const int32 Count = FMath::RoundToInt(10.0f * F.Hz);
		for (int32 i = 0; i < Count; ++i)
		{
			++F.Steps;
			const float Heading = 5.0f * FMath::Sin(2.0f * PI * 2.0f * F.Now());
			F.Feed(Heading - Previous, false);
			Previous = Heading;
		}
		F.Hold(1.0f);
		TestEqual(TEXT("Jitter gives 0 records"), F.Records().Num(), 0);
	}

	// +100 then -100: flown across, then parked.
	{
		FFeeder F;
		F.TurnBy(100.0f);
		F.TurnBy(-100.0f);
		F.Hold(1.0f);
		TestEqual(TEXT("+100 then -100 gives 0 records"), F.Records().Num(), 0);
	}

	// +200 then -200: two halves, the second starting where the first ended.
	{
		FFeeder F;
		F.TurnBy(200.0f);
		F.TurnBy(-200.0f);
		F.Hold(1.0f);
		TestEqual(TEXT("+200 then -200 gives 2 records"), F.Records().Num(), 2);
		if (F.Records().Num() == 2)
		{
			const FKiteLoopRecord& A = F.Records()[0];
			const FKiteLoopRecord& B = F.Records()[1];
			TestFalse(TEXT("First half incomplete"), A.bCompleted);
			TestFalse(TEXT("Second half incomplete"), B.bCompleted);
			TestEqual(TEXT("First half to the right"), A.Direction, 1);
			TestEqual(TEXT("Second half to the left"), B.Direction, -1);
			TestTrue(FString::Printf(TEXT("First half %.1f deg >= 180"), A.TurnDeg), A.TurnDeg >= 180.0f);
			TestTrue(FString::Printf(TEXT("Second half %.1f deg >= 180"), B.TurnDeg), B.TurnDeg >= 180.0f);
			TestNearlyEqual(TEXT("The second half starts at the reversal (s)"), B.StartTimeSeconds, A.StartTimeSeconds + A.DurationSeconds, 0.01f);
		}
	}

	// +250, stall 1 s, +250: two partial records, not one completed loop.
	{
		FFeeder F;
		F.TurnBy(250.0f);
		F.Hold(1.0f);
		F.TurnBy(250.0f);
		F.Hold(1.0f);
		TestEqual(TEXT("+250, stall, +250 gives 2 records"), F.Records().Num(), 2);
		for (const FKiteLoopRecord& R : F.Records())
		{
			TestFalse(TEXT("Not completed"), R.bCompleted);
			TestEqual(TEXT("Both to the right"), R.Direction, 1);
			TestNearlyEqual(TEXT("Each 250 deg"), R.TurnDeg, 250.0f, 1.0f);
		}
	}

	// A crash at 300 deg: one crashed, incomplete record.
	{
		FFeeder F;
		F.TurnBy(300.0f);
		F.Hold(1.0f, true);
		TestEqual(TEXT("Crash mid-loop gives 1 record"), F.Records().Num(), 1);
		if (F.Records().Num() == 1)
		{
			TestTrue(TEXT("bKiteCrashed"), F.Records()[0].bKiteCrashed);
			TestFalse(TEXT("Not completed"), F.Records()[0].bCompleted);
			TestNearlyEqual(TEXT("Turned 300 deg"), F.Records()[0].TurnDeg, 300.0f, 1.0f);
		}
		TestEqual(TEXT("No run while crashed"), F.Tracker.GetRunDirection(), 0);
	}

	// Elevation dipping 80 -> 30 -> 80 over one loop; the tension peaks at 1.2 s.
	{
		FFeeder F;
		F.Elevation = [](float T) { return T <= 2.0f ? 80.0f - 50.0f * FMath::Sin(PI * T / 2.0f) : 80.0f; };
		F.Tension = [](float T) { return 1000.0f + 2000.0f * FMath::Exp(-FMath::Square((T - 1.2f) / 0.2f)); };
		F.TurnFor(180.0f, 2.0f);
		F.Hold(1.0f);
		TestEqual(TEXT("One loop"), F.Records().Num(), 1);
		if (F.Records().Num() == 1)
		{
			const FKiteLoopRecord& R = F.Records()[0];
			TestTrue(TEXT("Completed"), R.bCompleted);
			TestNearlyEqual(TEXT("MinElevationDeg (deg)"), R.MinElevationDeg, 30.0f, 0.5f);
			TestNearlyEqual(TEXT("StartElevationDeg (deg)"), R.StartElevationDeg, 80.0f, 0.5f);
			TestNearlyEqual(TEXT("PeakTensionN (N)"), R.PeakTensionN, 3000.0f, 5.0f);
			TestNearlyEqual(TEXT("PeakTensionTimeSeconds (s)"), R.PeakTensionTimeSeconds, 1.2f, 0.01f);
			TestNearlyEqual(TEXT("RiderZAtStartCm (cm)"), R.RiderZAtStartCm, 500.0f, 0.01f);
		}
	}

	// Travel side: riding left is -1, straight downwind is 0.
	{
		FFeeder Left;
		Left.RiderVelocity = FVector(300.0, -800.0, 0.0);
		Left.TurnFor(180.0f, 2.1f);
		Left.Hold(1.0f);
		TestTrue(TEXT("Riding left: a record"), Left.Records().Num() == 1 && Left.Records()[0].RiderTravelSide == -1);
		FFeeder Downwind;
		Downwind.RiderVelocity = FVector(800.0, 100.0, 0.0);
		Downwind.TurnFor(180.0f, 2.1f);
		Downwind.Hold(1.0f);
		TestTrue(TEXT("Riding straight downwind: travel side 0"), Downwind.Records().Num() == 1 && Downwind.Records()[0].RiderTravelSide == 0);
	}

	// CancelRun drops a run without a record; MaxRecords keeps the newest.
	{
		FFeeder F;
		F.TurnBy(300.0f);
		F.Tracker.CancelRun();
		F.Hold(1.0f);
		TestEqual(TEXT("A cancelled run leaves no record"), F.Records().Num(), 0);

		FFeeder Ring;
		Ring.Tracker.Settings.MaxRecords = 3;
		Ring.TurnBy(360.0f * 5.0f);
		Ring.Hold(1.0f);
		TestEqual(TEXT("Five loops recorded"), Ring.Tracker.GetTotalRecorded(), 5);
		TestEqual(TEXT("Three kept"), Ring.Records().Num(), 3);
		if (Ring.Records().Num() == 3)
		{
			TestEqual(TEXT("The oldest kept is index 2"), Ring.Records()[0].Index, 2);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickClassifiesLoops, "KiteSurf.Trick.ClassifiesLoops", TrickLoopTest::Flags)

bool FKiteSurfTrickClassifiesLoops::RunTest(const FString& Parameters)
{
	using namespace TrickLoopTest;
	using namespace TrickRecognition;
	const FLoopClassifySettings S;

	// Heli loop: relative to where the loop started. Apex at 3 s.
	TestEqual(TEXT("After the apex, kite 60 -> 45: heli loop (55 deg was never reached)"),
		ClassifyLoop(Loop(1, 3.5f, 1.5f, 360.0f, true, 60.0f, 45.0f), S), ETrickLoopKind::HeliLoop);
	TestEqual(TEXT("After the apex, kite 60 -> 40, a 20 deg drop: heli loop"),
		ClassifyLoop(Loop(1, 3.5f, 1.5f, 360.0f, true, 60.0f, 40.0f), S), ETrickLoopKind::HeliLoop);
	TestEqual(TEXT("Kite down to 39 deg: kiteloop"),
		ClassifyLoop(Loop(1, 3.5f, 1.5f, 360.0f, true, 60.0f, 39.0f), S), ETrickLoopKind::Kiteloop);
	TestEqual(TEXT("A 25 deg drop: heli loop"),
		ClassifyLoop(Loop(1, 3.5f, 1.5f, 360.0f, true, 65.0f, 40.0f), S), ETrickLoopKind::HeliLoop);
	TestEqual(TEXT("A 26 deg drop: kiteloop"),
		ClassifyLoop(Loop(1, 3.5f, 1.5f, 360.0f, true, 66.0f, 40.0f), S), ETrickLoopKind::Kiteloop);
	TestEqual(TEXT("Before the apex: kiteloop"),
		ClassifyLoop(Loop(1, 2.5f, 1.5f, 360.0f, true, 60.0f, 45.0f), S), ETrickLoopKind::Kiteloop);

	// Megaloop: 8 m, 20 deg and 3 body weights (2502 N at 85 kg), each needed.
	const float ThreeBodyWeightsN = 3.0f * S.RiderMassKg * KiteUnits::GravityMS2 + 1.0f;
	TestEqual(TEXT("8.0 m, 20 deg, 3 BW: megaloop"),
		ClassifyLoop(Loop(1, 2.0f, 2.0f, 360.0f, true, 70.0f, 20.0f, ThreeBodyWeightsN, 800.0f), S), ETrickLoopKind::Megaloop);
	TestEqual(TEXT("7.9 m: kiteloop"),
		ClassifyLoop(Loop(1, 2.0f, 2.0f, 360.0f, true, 70.0f, 20.0f, ThreeBodyWeightsN, 790.0f), S), ETrickLoopKind::Kiteloop);
	TestEqual(TEXT("21 deg: kiteloop"),
		ClassifyLoop(Loop(1, 2.0f, 2.0f, 360.0f, true, 70.0f, 21.0f, ThreeBodyWeightsN, 800.0f), S), ETrickLoopKind::Kiteloop);
	TestEqual(TEXT("2.9 BW: kiteloop"),
		ClassifyLoop(Loop(1, 2.0f, 2.0f, 360.0f, true, 70.0f, 20.0f, 2.9f * S.RiderMassKg * KiteUnits::GravityMS2, 800.0f), S), ETrickLoopKind::Kiteloop);

	// Contra: the loop turned against the rider's travel side.
	TestTrue(TEXT("Left loop riding right: contra"), IsContraLoop(Loop(-1, 2.0f, 2.0f, 360.0f, true, 70, 40, 1500, 1500, 3, 1).Loop));
	TestFalse(TEXT("Right loop riding right: natural"), IsContraLoop(Loop(1, 2.0f, 2.0f, 360.0f, true, 70, 40, 1500, 1500, 3, 1).Loop));
	TestTrue(TEXT("Right loop riding left: contra"), IsContraLoop(Loop(1, 2.0f, 2.0f, 360.0f, true, 70, 40, 1500, 1500, 3, -1).Loop));
	TestFalse(TEXT("No travel side: never contra"), IsContraLoop(Loop(-1, 2.0f, 2.0f, 360.0f, true, 70, 40, 1500, 1500, 3, 0).Loop));
	{
		const TArray<FTrickLoop> Contra = ClassifyLoops({ Loop(-1, 2.0f, 2.0f, 360.0f, true) });
		TestTrue(TEXT("One contra kiteloop"), Contra.Num() == 1 && Contra[0].bContra && Contra[0].Kind == ETrickLoopKind::Kiteloop);
		TestEqual(TEXT("Named"), NameOf({ Loop(-1, 2.0f, 2.0f, 360.0f, true) }), FString(TEXT("Contra loop")));
	}

	// Double and triple: completed loops one way, back to back.
	{
		const TArray<FTrickLoop> Double = ClassifyLoops({ Kiteloop(1, 1.0f), Kiteloop(1, 2.5f) });
		TestEqual(TEXT("Two chained kiteloops"), KindsOf(Double), FString(TEXT("Kiteloop,Kiteloop")));
		const TArray<FTrickLoop> Triple = ClassifyLoops({ Kiteloop(1, 1.0f), Kiteloop(1, 2.5f), Kiteloop(1, 4.0f) });
		TestEqual(TEXT("Three chained kiteloops"), KindsOf(Triple), FString(TEXT("Kiteloop,Kiteloop,Kiteloop")));

		// A kiteloop straight into a megaloop is a double megaloop; 1.0 s apart still chains, 1.1 s does not.
		TestEqual(TEXT("Kiteloop into megaloop, no gap"), KindsOf(ClassifyLoops({ Kiteloop(1, 1.0f), Megaloop(1, 2.5f) })),
			FString(TEXT("Megaloop,Megaloop")));
		TestEqual(TEXT("1.0 s gap chains"), KindsOf(ClassifyLoops({ Kiteloop(1, 1.0f), Megaloop(1, 3.5f) })),
			FString(TEXT("Megaloop,Megaloop")));
		TestEqual(TEXT("1.1 s gap does not chain"), KindsOf(ClassifyLoops({ Kiteloop(1, 1.0f), Megaloop(1, 3.6f) })),
			FString(TEXT("Kiteloop,Megaloop")));
		TestEqual(TEXT("Opposite directions do not chain"), KindsOf(ClassifyLoops({ Kiteloop(1, 1.0f), Megaloop(-1, 2.5f) })),
			FString(TEXT("Kiteloop,Megaloop(contra)")));

		// The heli loop landing is not part of the double.
		TestEqual(TEXT("Two kiteloops and a heli loop"),
			KindsOf(ClassifyLoops({ Kiteloop(1, 1.0f), Kiteloop(1, 2.5f), Loop(1, 4.0f, 1.5f, 360.0f, true, 60.0f, 45.0f) })),
			FString(TEXT("Kiteloop,Kiteloop,HeliLoop")));
	}

	// S-loop and snake: unfinished halves alternating in direction.
	{
		TestEqual(TEXT("+200 then -190 within 0.5 s: S-loop"),
			KindsOf(ClassifyLoops({ Half(1, 2.0f, 200.0f), Half(-1, 3.5f, 190.0f) })), FString(TEXT("SLoop")));
		TestEqual(TEXT("0.7 s apart: no S-loop"),
			KindsOf(ClassifyLoops({ Half(1, 2.0f, 200.0f), Half(-1, 3.7f, 190.0f) })), FString());
		TestEqual(TEXT("Both halves the same way: no S-loop"),
			KindsOf(ClassifyLoops({ Half(1, 2.0f), Half(1, 3.0f) })), FString());
		TestEqual(TEXT("A half of 170 deg: no S-loop"),
			KindsOf(ClassifyLoops({ Half(1, 2.0f, 200.0f), Half(-1, 3.0f, 170.0f) })), FString());
		FJumpLoop Crashed = Half(-1, 3.0f);
		Crashed.Loop.bKiteCrashed = true;
		TestEqual(TEXT("A crashed half: no S-loop"), KindsOf(ClassifyLoops({ Half(1, 2.0f), Crashed })), FString());
		TestEqual(TEXT("Three halves: snake loop"),
			KindsOf(ClassifyLoops({ Half(1, 2.0f), Half(-1, 3.0f), Half(1, 4.0f) })), FString(TEXT("SnakeLoop")));
		TestEqual(TEXT("Four halves: snake loop"),
			KindsOf(ClassifyLoops({ Half(1, 2.0f), Half(-1, 3.0f), Half(1, 4.0f), Half(-1, 5.0f) })), FString(TEXT("SnakeLoop")));
		TestEqual(TEXT("A kiteloop, then an S-loop"),
			KindsOf(ClassifyLoops({ Kiteloop(1, 0.5f), Half(1, 2.0f), Half(-1, 3.0f) })), FString(TEXT("Kiteloop,SLoop")));
	}

	// Recorded sets: the tracker's own records classify the same way.
	{
		FFeeder S2;
		S2.TurnBy(200.0f);
		S2.TurnBy(-200.0f);
		S2.Hold(1.0f);
		TestEqual(TEXT("Tracker +200/-200: S-loop"), KindsOf(ClassifyLoops(AsJumpLoops(S2.Records(), 0.0f))), FString(TEXT("SLoop")));

		FFeeder Snake;
		Snake.TurnBy(200.0f);
		Snake.TurnBy(-200.0f);
		Snake.TurnBy(200.0f);
		Snake.Hold(1.0f);
		TestEqual(TEXT("Tracker +200/-200/+200: snake loop"), KindsOf(ClassifyLoops(AsJumpLoops(Snake.Records(), 0.0f))), FString(TEXT("SnakeLoop")));

		FFeeder Double;
		Double.TurnFor(180.0f, 4.2f);
		Double.Hold(1.0f);
		TestEqual(TEXT("Tracker 2 x 360: double"), KindsOf(ClassifyLoops(AsJumpLoops(Double.Records(), 0.0f))), FString(TEXT("Kiteloop,Kiteloop")));

		FFeeder Triple;
		Triple.TurnFor(-180.0f, 6.2f);
		Triple.Hold(1.0f);
		TestEqual(TEXT("Tracker 3 x 360 to the left riding right: triple contra"), KindsOf(ClassifyLoops(AsJumpLoops(Triple.Records(), 0.0f))),
			FString(TEXT("Kiteloop(contra),Kiteloop(contra),Kiteloop(contra)")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickLoopRollTiming, "KiteSurf.Trick.LoopRollTiming", TrickLoopTest::Flags)

bool FKiteSurfTrickLoopRollTiming::RunTest(const FString& Parameters)
{
	using namespace TrickLoopTest;
	using namespace TrickRecognition;

	const float Yank = 3.0f;
	TestEqual(TEXT("Roll 0.2 s before the yank: early"), LoopRollTiming(Yank - 0.2f, Yank), ELoopRollTiming::Early);
	TestEqual(TEXT("Roll 0.11 s before: early"), LoopRollTiming(Yank - 0.11f, Yank), ELoopRollTiming::Early);
	TestEqual(TEXT("Roll 0.09 s before: neither"), LoopRollTiming(Yank - 0.09f, Yank), ELoopRollTiming::None);
	TestEqual(TEXT("Roll at the yank: neither"), LoopRollTiming(Yank, Yank), ELoopRollTiming::None);
	TestEqual(TEXT("Roll 0.05 s after: neither"), LoopRollTiming(Yank + 0.05f, Yank), ELoopRollTiming::None);
	TestEqual(TEXT("Roll 0.11 s after: late"), LoopRollTiming(Yank + 0.11f, Yank), ELoopRollTiming::Late);
	TestEqual(TEXT("Roll 0.2 s after: late"), LoopRollTiming(Yank + 0.2f, Yank), ELoopRollTiming::Late);

	FLoopClassifySettings Wide;
	Wide.RollTimingMarginSeconds = 0.3f;
	TestEqual(TEXT("A 0.3 s margin: 0.2 s before is neither"), LoopRollTiming(Yank - 0.2f, Yank, Wide), ELoopRollTiming::None);
	TestEqual(TEXT("A 0.3 s margin: 0.4 s after is late"), LoopRollTiming(Yank + 0.4f, Yank, Wide), ELoopRollTiming::Late);

	// The yank on the jump's clock: the loop started 2 s after take-off at kite time 102; the pull peaked 1.2 s into it.
	FJumpLoop JumpLoop = Kiteloop(1, 2.0f, 2.0f);
	JumpLoop.Loop.PeakTensionTimeSeconds = JumpLoop.Loop.StartTimeSeconds + 1.2f;
	TestNearlyEqual(TEXT("Yank 3.2 s after take-off (s)"), PeakTensionSinceTakeoffSeconds(JumpLoop), 3.2f, 1e-4f);

	// From the tracker: a loop whose pull peaks 1.2 s in, with rolls started at 0.8 s and 1.6 s.
	FFeeder F;
	F.Tension = [](float T) { return 1000.0f + 2000.0f * FMath::Exp(-FMath::Square((T - 1.2f) / 0.2f)); };
	F.TurnFor(180.0f, 2.0f);
	F.Hold(1.0f);
	TestEqual(TEXT("One recorded loop"), F.Records().Num(), 1);
	if (F.Records().Num() == 1)
	{
		const float RecordedYank = F.Records()[0].PeakTensionTimeSeconds;
		TestEqual(TEXT("Roll at 0.8 s against the recorded yank: early"), LoopRollTiming(0.8f, RecordedYank), ELoopRollTiming::Early);
		TestEqual(TEXT("Roll at 1.6 s against the recorded yank: late"), LoopRollTiming(1.6f, RecordedYank), ELoopRollTiming::Late);
		TestEqual(TEXT("Roll at 1.22 s: neither"), LoopRollTiming(1.22f, RecordedYank), ELoopRollTiming::None);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSignatureNamesLoopFamilies, "KiteSurf.Trick.SignatureNamesLoopFamilies", TrickLoopTest::Flags)

bool FKiteSurfTrickSignatureNamesLoopFamilies::RunTest(const FString& Parameters)
{
	using namespace TrickLoopTest;

	TestEqual(TEXT("Two halves"), NameOf({ Half(1, 2.0f), Half(-1, 3.0f) }), FString(TEXT("S-loop")));
	TestEqual(TEXT("Three halves"), NameOf({ Half(1, 2.0f), Half(-1, 3.0f), Half(1, 4.0f) }), FString(TEXT("Snake loop")));
	TestEqual(TEXT("Two kiteloops back to back"), NameOf({ Kiteloop(1, 1.0f), Kiteloop(1, 2.5f) }), FString(TEXT("Double kiteloop")));
	TestEqual(TEXT("Three kiteloops back to back"), NameOf({ Kiteloop(1, 1.0f), Kiteloop(1, 2.5f), Kiteloop(1, 4.0f) }), FString(TEXT("Triple kiteloop")));
	TestEqual(TEXT("A kiteloop into a megaloop"), NameOf({ Kiteloop(1, 1.0f), Megaloop(1, 2.5f) }), FString(TEXT("Double megaloop")));
	TestEqual(TEXT("A heli loop under the old 55 deg rule's reach"), NameOf({ Loop(1, 3.5f, 1.5f, 360.0f, true, 60.0f, 45.0f) }), FString(TEXT("Heli loop")));
	TestEqual(TEXT("A double and a heli loop landing"),
		NameOf({ Kiteloop(1, 1.0f), Kiteloop(1, 2.5f), Loop(1, 4.0f, 1.5f, 360.0f, true, 60.0f, 45.0f) }),
		FString(TEXT("Double kiteloop + heli loop")));

	// Through the tracker: two halves flown as one S.
	FFeeder F;
	F.TurnBy(-220.0f);
	F.TurnBy(210.0f);
	F.Hold(1.0f);
	TestEqual(TEXT("Tracker -220/+210 named"), NameOf(AsJumpLoops(F.Records(), 0.0f)), FString(TEXT("S-loop")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
