#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "School/LessonCatalog.h"
#include "School/LessonEvaluator.h"
#include "School/LessonTelemetry.h"
#include "Tests/SchoolTestScenes.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/TrickSignature.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named, not anonymous, so a unity build cannot merge these with another file's helpers.
namespace SchoolLessonTest
{
	using namespace SchoolTestScenes;
	using EM = ELessonMetric;
	using EC = ELessonChannel;
	using EA = ELessonAnchor;
	using ER = ELessonReduce;

	FLessonObjective Objective(EM Metric, float Min, float Max = UE_BIG_NUMBER, int32 Count = 1)
	{
		FLessonObjective O;
		O.Metric = Metric;
		O.Min = Min;
		O.Max = Max;
		O.Count = Count;
		return O;
	}

	FString MetricName(EM Metric)
	{
		return StaticEnum<ELessonMetric>()->GetNameStringByValue(static_cast<int64>(Metric));
	}

	/** The metrics the both-sides test has checked. */
	struct FCoverage
	{
		TSet<int64> Metrics;
	};

	/** A result that meets the objective: an event counted, or a hold or channel test passed. */
	bool Meets(const FObjectiveResult& R)
	{
		return R.bCounted || R.bPassed;
	}

	/**
	 * The objective is met on Good and not on Bad. With bBadIsAttempt the bad side must also be a
	 * judged, failed attempt (not just nothing happening).
	 */
	void BothSides(FAutomationTestBase& Test, FCoverage& Coverage, const FString& What, const FLessonObjective& O,
		const FSchoolScene& Good, const FSchoolScene& Bad, bool bBadIsAttempt)
	{
		Coverage.Metrics.Add(static_cast<int64>(O.Metric));
		const FString Name = MetricName(O.Metric) + TEXT(" ") + What;
		const FObjectiveResult G = EvaluateOnce(O, Good);
		const FObjectiveResult B = EvaluateOnce(O, Bad);
		Test.TestTrue(Name + TEXT(": met on the passing side (value ") + FString::SanitizeFloat(G.Value) + TEXT(")"), Meets(G));
		Test.TestFalse(Name + TEXT(": no failed attempt on the passing side"), G.bFailed);
		Test.TestFalse(Name + TEXT(": not met on the failing side (value ") + FString::SanitizeFloat(B.Value) + TEXT(")"), Meets(B));
		if (bBadIsAttempt)
		{
			Test.TestTrue(Name + TEXT(": the failing side is a failed attempt"), B.bFailed);
		}
	}

	/** A copy of a scene with a change made to it. */
	template <typename FChange>
	FSchoolScene With(const FSchoolScene& Base, FChange&& Change)
	{
		FSchoolScene Copy = Base;
		Change(Copy);
		return Copy;
	}

	FJumpLoop MakeLoop(bool bCompleted, float StartSinceApex, float StartElevation, float MinElevation, float RiderHeightCm, float PeakTensionN, float Duration = 1.2f)
	{
		FJumpLoop L;
		L.Loop.bCompleted = bCompleted;
		L.Loop.TurnDeg = bCompleted ? 360.0f : 200.0f;
		L.Loop.Direction = 1;
		L.Loop.DurationSeconds = Duration;
		L.Loop.StartElevationDeg = StartElevation;
		L.Loop.MinElevationDeg = MinElevation;
		L.Loop.PeakTensionN = PeakTensionN;
		L.StartSinceApexSeconds = StartSinceApex;
		L.StartSinceTakeoffSeconds = 0.6f + StartSinceApex;
		L.RiderHeightAtStartCm = RiderHeightCm;
		return L;
	}

	/** A ride with a tack change at ChangeT; Make adjusts each sample. */
	template <typename FMake>
	FSchoolScene TackChange(float ChangeT, float EndT, FMake&& Make)
	{
		return BuildRide(0.0f, EndT, [ChangeT, &Make](float T)
		{
			FLessonSample S = Riding(T);
			S.Tack = T < ChangeT ? 1 : -1;
			Make(S, T);
			return S;
		});
	}

	/** Kite elevation: at 85, one dive starting at DiveT down to Depth and back over Duration. */
	FSchoolScene OneDive(float DiveT, float Depth, float Duration, float EndT)
	{
		return BuildRide(0.0f, EndT, [=](float T)
		{
			FLessonSample S = Riding(T);
			const float Phase = (T - DiveT) / Duration;
			S.KiteElevationDeg = (Phase > 0.0f && Phase < 1.0f) ? 85.0f - (85.0f - Depth) * FMath::Sin(PI * Phase) : 85.0f;
			return S;
		});
	}
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolTelemetryRingBuffer, "KiteSurf.School.TelemetryRingBuffer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfSchoolTelemetryRingBuffer::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Default capacity holds more than 20 s at 60 Hz"), (LessonTelemetry::DefaultCapacity - 1) / 60.0f > 20.0f);

	FLessonTelemetry T(10);
	TestTrue(TEXT("Starts empty"), T.IsEmpty());
	TestFalse(TEXT("ValueAt on empty"), [&T] { float V; return T.ValueAt(ELessonChannel::Speed, 0.0f, V); }());
	for (int32 I = 0; I < 25; ++I)
	{
		FLessonSample S;
		S.TimeSeconds = static_cast<float>(I);
		S.SpeedMS = 2.0f * I;
		TestTrue(FString::Printf(TEXT("Add sample %d"), I), T.Add(S));
	}
	TestEqual(TEXT("Full at capacity"), T.Num(), 10);
	TestEqual(TEXT("Capacity"), T.GetCapacity(), 10);
	TestNearlyEqual(TEXT("Oldest kept is t=15 s after the wrap"), T.OldestTime(), 15.0f, 1e-6f);
	TestNearlyEqual(TEXT("Newest is t=24 s"), T.LatestTime(), 24.0f, 1e-6f);
	TestNearlyEqual(TEXT("Get(3) is t=18 s"), T.Get(3).TimeSeconds, 18.0f, 1e-6f);
	TestNearlyEqual(TEXT("Span (s)"), T.SpanSeconds(), 9.0f, 1e-6f);

	FLessonSample Old;
	Old.TimeSeconds = 24.0f;
	TestFalse(TEXT("A sample at the newest time is refused"), T.Add(Old));
	Old.TimeSeconds = 20.0f;
	TestFalse(TEXT("An older sample is refused"), T.Add(Old));
	TestEqual(TEXT("Refused samples change nothing"), T.Num(), 10);

	TestEqual(TEXT("Index at or before 17.5 s"), T.FindIndexAtOrBefore(17.5f), 2);
	TestEqual(TEXT("Index before the oldest"), T.FindIndexAtOrBefore(14.0f), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Index after the newest"), T.FindIndexAtOrBefore(100.0f), 9);

	float V = 0.0f;
	TestTrue(TEXT("ValueAt inside"), T.ValueAt(ELessonChannel::Speed, 17.5f, V));
	TestNearlyEqual(TEXT("ValueAt interpolates (m/s)"), V, 35.0f, 1e-4f);
	TestTrue(TEXT("ValueAt on the newest sample"), T.ValueAt(ELessonChannel::Speed, 24.0f, V));
	TestNearlyEqual(TEXT("ValueAt newest (m/s)"), V, 48.0f, 1e-4f);
	TestFalse(TEXT("ValueAt before the buffer"), T.ValueAt(ELessonChannel::Speed, 14.5f, V));
	TestFalse(TEXT("ValueAt after the buffer"), T.ValueAt(ELessonChannel::Speed, 24.5f, V));

	const FLessonWindowStats W = T.Window(ELessonChannel::Speed, 16.5f, 18.5f);
	TestEqual(TEXT("Window values: two samples and two interpolated ends"), W.Count, 4);
	TestNearlyEqual(TEXT("Window min (m/s)"), W.Min, 33.0f, 1e-4f);
	TestNearlyEqual(TEXT("Window max (m/s)"), W.Max, 37.0f, 1e-4f);
	TestNearlyEqual(TEXT("Window mean (m/s)"), W.Mean, 35.0f, 1e-4f);
	TestTrue(TEXT("A short window between two samples still has a value"), T.Window(ELessonChannel::Speed, 17.2f, 17.4f).IsValid());
	TestFalse(TEXT("A window outside the buffer is invalid"), T.Window(ELessonChannel::Speed, 30.0f, 31.0f).IsValid());

	TestNearlyEqual(TEXT("Time in band [34, 36] m/s (s)"), T.TimeInBand(ELessonChannel::Speed, 34.0f, 36.0f, 0.0f, 100.0f), 2.0f, 1e-4f);
	TestNearlyEqual(TEXT("Time in band clipped to the window (s)"), T.TimeInBand(ELessonChannel::Speed, 34.0f, 36.0f, 17.5f, 18.25f), 0.75f, 1e-4f);
	TestNearlyEqual(TEXT("Trailing time at 40 m/s or more (s)"), T.TrailingTimeInBand(ELessonChannel::Speed, 40.0f, 100.0f), 4.0f, 1e-4f);
	TestNearlyEqual(TEXT("Trailing time cut by NotBefore (s)"), T.TrailingTimeInBand(ELessonChannel::Speed, 40.0f, 100.0f, 22.0f), 2.0f, 1e-4f);
	TestNearlyEqual(TEXT("Trailing time when the newest fails (s)"), T.TrailingTimeInBand(ELessonChannel::Speed, 0.0f, 10.0f), 0.0f, 1e-6f);

	// Heading interpolates the short way round; tack changes skip unknown tacks; derived channels.
	FLessonTelemetry H(16);
	const float Headings[] = { 350.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f };
	const int32 Tacks[] = { 1, 1, 0, -1, -1, 1 };
	const float Elevations[] = { 40.0f, 50.0f, 82.0f, 86.0f, 70.0f, 70.0f };
	for (int32 I = 0; I < 6; ++I)
	{
		FLessonSample S;
		S.TimeSeconds = 0.5f * I;
		S.HeadingDeg = Headings[I];
		S.Tack = Tacks[I];
		S.KiteElevationDeg = Elevations[I];
		S.BarPosition = 0.8f;
		H.Add(S);
	}
	TestTrue(TEXT("Heading at 0.25 s"), H.ValueAt(ELessonChannel::Heading, 0.25f, V));
	TestNearlyEqual(TEXT("Heading 350 to 10 passes through 0, not 180 (deg)"), FMath::Abs(FMath::FindDeltaAngleDegrees(V, 0.0f)), 0.0f, 1e-3f);
	const TArray<float> Changes = H.FindTackChanges();
	TestEqual(TEXT("Two tack changes (0 skipped)"), Changes.Num(), 2);
	if (Changes.Num() == 2)
	{
		TestNearlyEqual(TEXT("First change at the first -1 sample (s)"), Changes[0], 1.5f, 1e-6f);
		TestNearlyEqual(TEXT("Second change at the +1 sample (s)"), Changes[1], 2.5f, 1e-6f);
	}
	TestNearlyEqual(TEXT("Climb rate of the oldest sample is 0"), H.ChannelAt(0, ELessonChannel::KiteClimbRate), 0.0f, 1e-6f);
	TestNearlyEqual(TEXT("Climb rate 40 to 50 deg in 0.5 s (deg/s)"), H.ChannelAt(1, ELessonChannel::KiteClimbRate), 20.0f, 1e-4f);
	TestNearlyEqual(TEXT("Bar while climbing below 80 deg"), H.ChannelAt(1, ELessonChannel::BarWhileClimbing), 0.8f, 1e-6f);
	TestNearlyEqual(TEXT("No bar while climbing above 80 deg"), H.ChannelAt(3, ELessonChannel::BarWhileClimbing), 0.0f, 1e-6f);
	TestNearlyEqual(TEXT("No bar while climbing when the kite falls"), H.ChannelAt(4, ELessonChannel::BarWhileClimbing), 0.0f, 1e-6f);
	TestNearlyEqual(TEXT("Absolute edge channel"), [] { FLessonSample S; S.EdgeInput = -0.7f; return S.Get(ELessonChannel::EdgeAbs); }(), 0.7f, 1e-6f);

	T.Reset();
	TestTrue(TEXT("Reset empties"), T.IsEmpty());
	FLessonSample Again;
	Again.TimeSeconds = 1.0f;
	TestTrue(TEXT("Accepts an earlier time after a reset"), T.Add(Again));
	TestEqual(TEXT("One sample after the reset"), T.Num(), 1);
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolMetricsBothSides, "KiteSurf.School.MetricsBothSides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfSchoolMetricsBothSides::RunTest(const FString& Parameters)
{
	using namespace SchoolLessonTest;
	FCoverage Cov;
	const FSchoolScene Jump = BuildJump(FJumpScene());

	// Jump record metrics.
	BothSides(*this, Cov, TEXT("1 to 2 m, too high"), Objective(EM::JumpHeight, 1.0f, 2.0f), Jump,
		With(Jump, [](FSchoolScene& S) { S.Jump.ApexHeightCm = 250.0f; }), true);
	BothSides(*this, Cov, TEXT("1 to 2 m, too low"), Objective(EM::JumpHeight, 1.0f, 2.0f), Jump,
		With(Jump, [](FSchoolScene& S) { S.Jump.ApexHeightCm = 50.0f; }), true);
	BothSides(*this, Cov, TEXT("1 s or more"), Objective(EM::Airtime, 1.0f), Jump,
		With(Jump, [](FSchoolScene& S) { S.Jump.AirtimeSeconds = 0.8f; }), true);
	BothSides(*this, Cov, TEXT("8 m or more"), Objective(EM::JumpDistance, 8.0f), Jump,
		With(Jump, [](FSchoolScene& S) { S.Jump.DistanceCm = 700.0f; }), true);
	BothSides(*this, Cov, TEXT("10 m/s or less"), Objective(EM::TakeoffSpeed, 0.0f, 10.0f), Jump,
		With(Jump, [](FSchoolScene& S) { S.Jump.TakeoffSpeedCmS = 1100.0f; }), true);
	const float CleanIndex = static_cast<float>(static_cast<int32>(ELandingGrade::Clean));
	BothSides(*this, Cov, TEXT("at least Clean, sketchy"), Objective(EM::LandingGrade, 0.0f, CleanIndex), Jump,
		With(Jump, [](FSchoolScene& S) { S.Jump.Grade = ELandingGrade::Sketchy; }), true);
	BothSides(*this, Cov, TEXT("at least Clean, crashed outcome"), Objective(EM::LandingGrade, 0.0f, CleanIndex),
		With(Jump, [](FSchoolScene& S) { S.Jump.Grade = ELandingGrade::Stomped; }),
		With(Jump, [](FSchoolScene& S) { S.Jump.Outcome = EJumpOutcome::Crashed; }), true);
	const float TooHard = static_cast<float>(static_cast<int32>(ELandingCause::TooHard));
	BothSides(*this, Cov, TEXT("is TooHard"), Objective(EM::LandingCause, TooHard, TooHard),
		With(Jump, [](FSchoolScene& S) { S.Extras.LandingCause = ELandingCause::TooHard; }), Jump, true);
	BothSides(*this, Cov, TEXT("3 m/s or less"), Objective(EM::SinkAtTouchdown, 0.0f, 3.0f), Jump,
		With(Jump, [](FSchoolScene& S) { S.Jump.SinkRateCmS = 400.0f; }), true);
	BothSides(*this, Cov, TEXT("4 g or less"), Objective(EM::LandingG, 0.0f, 4.0f), Jump,
		With(Jump, [](FSchoolScene& S) { S.Jump.LandingG = 5.0f; }), true);
	BothSides(*this, Cov, TEXT("45 deg or more"), Objective(EM::KiteElevationAtTouchdown, 45.0f), Jump,
		With(Jump, [](FSchoolScene& S) { S.Jump.KiteElevationAtLandingDeg = 40.0f; }), true);
	BothSides(*this, Cov, TEXT("30 deg or more"), Objective(EM::MinKiteElevationInAir, 30.0f),
		With(Jump, [](FSchoolScene& S) { S.Jump.MinKiteElevationDeg = 50.0f; }),
		With(Jump, [](FSchoolScene& S) { S.Jump.MinKiteElevationDeg = 20.0f; }), true);
	BothSides(*this, Cov, TEXT("popped"), Objective(EM::Popped, 1.0f, 1.0f),
		With(Jump, [](FSchoolScene& S) { S.Jump.bPopped = true; }), Jump, true);

	const FSchoolScene CompletedLoop = With(Jump, [](FSchoolScene& S) { S.Jump.Loops.Add(MakeLoop(true, -0.2f, 60.0f, 30.0f, 100.0f, 1500.0f)); });
	const FSchoolScene UnfinishedLoop = With(Jump, [](FSchoolScene& S) { S.Jump.Loops.Add(MakeLoop(false, -0.2f, 60.0f, 30.0f, 100.0f, 1500.0f)); });
	BothSides(*this, Cov, TEXT("one or more"), Objective(EM::CompletedLoops, 1.0f), CompletedLoop, UnfinishedLoop, true);

	FLessonObjective Heli = Objective(EM::LoopKind, 1.0f);
	Heli.LoopKind = ETrickLoopKind::HeliLoop;
	BothSides(*this, Cov, TEXT("heli loop"), Heli,
		With(Jump, [](FSchoolScene& S) { S.Jump.Loops.Add(MakeLoop(true, 0.2f, 60.0f, 50.0f, 100.0f, 1500.0f)); }), CompletedLoop, true);
	FLessonObjective Mega = Objective(EM::LoopKind, 1.0f);
	Mega.LoopKind = ETrickLoopKind::Megaloop;
	BothSides(*this, Cov, TEXT("megaloop"), Mega,
		With(Jump, [](FSchoolScene& S) { S.Jump.Loops.Add(MakeLoop(true, -0.2f, 60.0f, 15.0f, 900.0f, 3000.0f)); }),
		With(Jump, [](FSchoolScene& S) { S.Jump.Loops.Add(MakeLoop(true, -0.2f, 60.0f, 15.0f, 900.0f, 1000.0f)); }), true);

	BothSides(*this, Cov, TEXT("before the apex"), Objective(EM::LoopStartSinceApex, -UE_BIG_NUMBER, 0.0f), CompletedLoop,
		With(Jump, [](FSchoolScene& S) { S.Jump.Loops.Add(MakeLoop(true, 0.3f, 60.0f, 30.0f, 100.0f, 1500.0f)); }), true);
	BothSides(*this, Cov, TEXT("no loop is unavailable"), Objective(EM::LoopStartSinceApex, -UE_BIG_NUMBER, 0.0f), CompletedLoop, Jump, true);
	BothSides(*this, Cov, TEXT("1.5 s or less"), Objective(EM::LoopDuration, 0.0f, 1.5f), CompletedLoop,
		With(Jump, [](FSchoolScene& S) { S.Jump.Loops.Add(MakeLoop(true, -0.2f, 60.0f, 30.0f, 100.0f, 1500.0f, 2.0f)); }), true);

	FLessonObjective Named = Objective(EM::TrickNameContains, 1.0f);
	Named.Text = TEXT("kiteloop");
	BothSides(*this, Cov, TEXT("contains kiteloop"), Named,
		With(Jump, [](FSchoolScene& S) { S.Jump.TrickName = TEXT("Double Kiteloop"); }),
		With(Jump, [](FSchoolScene& S) { S.Jump.TrickName = TEXT("Jump"); }), true);

	const FSchoolScene Rotated = With(Jump, [](FSchoolScene& S) { S.Extras.bHasRotation = true; S.Extras.RotationDeg = 360.0f; });
	BothSides(*this, Cov, TEXT("330 to 390, half turn"), Objective(EM::RotationDeg, 330.0f, 390.0f), Rotated,
		With(Jump, [](FSchoolScene& S) { S.Extras.bHasRotation = true; S.Extras.RotationDeg = 180.0f; }), true);
	BothSides(*this, Cov, TEXT("330 to 390, unknown"), Objective(EM::RotationDeg, 330.0f, 390.0f), Rotated, Jump, true);
	BothSides(*this, Cov, TEXT("0.5 s or more"), Objective(EM::GrabHoldSeconds, 0.5f),
		With(Jump, [](FSchoolScene& S) { S.Extras.bHasGrab = true; S.Extras.GrabHoldSeconds = 0.7f; }),
		With(Jump, [](FSchoolScene& S) { S.Extras.bHasGrab = true; S.Extras.GrabHoldSeconds = 0.3f; }), true);
	BothSides(*this, Cov, TEXT("0.5 s or more, unknown"), Objective(EM::GrabHoldSeconds, 0.5f),
		With(Jump, [](FSchoolScene& S) { S.Extras.bHasGrab = true; S.Extras.GrabHoldSeconds = 0.7f; }), Jump, true);

	{
		FJumpScene Good;
		Good.HeightM = 4.0f;
		Good.Airtime = 1.8f;
		FJumpScene Late = Good;
		Late.DiveLeadS = 0.05f;
		FJumpScene Early = Good;
		Early.DiveLeadS = 1.4f;
		Early.DiveToDeg = 50.0f;
		const FSchoolScene GoodScene = BuildJump(Good);
		BothSides(*this, Cov, TEXT("in the last second, late"), Objective(EM::DiveBeforeTouchdown, 0.15f, 1.0f), GoodScene, BuildJump(Late), true);
		BothSides(*this, Cov, TEXT("in the last second, early"), Objective(EM::DiveBeforeTouchdown, 0.15f, 1.0f), GoodScene, BuildJump(Early), true);
		float Lead = 0.0f;
		LessonEval::ReadMeasure(LessonRules::Jump(EM::DiveBeforeTouchdown), GoodScene.Telemetry, &GoodScene.Jump, GoodScene.Extras, 0.0f, Lead);
		// Linear dive 85 -> 60 deg over 0.6 s leaves the top 5 deg after 0.12 s: about 0.48 s before touchdown.
		TestNearlyEqual(TEXT("Dive started about 0.48 s before touchdown (s)"), Lead, 0.48f, 0.02f);
	}
	{
		FJumpScene Turn;
		Turn.HeadingAfter = 270.0f;
		FJumpScene Straight;
		Straight.HeadingAfter = 120.0f;
		BothSides(*this, Cov, TEXT("120 deg or more"), Objective(EM::HeadingChange, 120.0f), BuildJump(Turn), BuildJump(Straight), true);
	}

	// Channel: the newest sample.
	{
		FLessonObjective High = Objective(EM::Channel, 80.0f);
		High.Channel = EC::KiteElevation;
		BothSides(*this, Cov, TEXT("kite at 80 deg or more now"), High,
			BuildRide(0.0f, 2.0f, [](float T) { FLessonSample S = Riding(T); S.KiteElevationDeg = 85.0f; return S; }),
			BuildRide(0.0f, 2.0f, [](float T) { return Riding(T); }), false);
	}

	// Held.
	{
		FLessonObjective Speed = Objective(EM::SpeedHeld, 7.0f, 9.0f);
		Speed.WindowSeconds = 5.0f;
		Speed.Conditions.Add(LessonRules::Condition(LessonRules::Channel(EC::KiteElevation, EA::Latest, ER::At), 35.0f, 55.0f));
		const FSchoolScene Broken = BuildRide(0.0f, 6.0f, [](float T) { FLessonSample S = Riding(T); S.SpeedMS = T < 3.0f ? 8.0f : (T < 3.1f ? 5.0f : 8.0f); return S; });
		BothSides(*this, Cov, TEXT("5 s, broken at 3 s"), Speed, BuildRide(0.0f, 6.0f, [](float T) { return Riding(T); }), Broken, false);
		BothSides(*this, Cov, TEXT("5 s, kite outside its band"), Speed, BuildRide(0.0f, 6.0f, [](float T) { return Riding(T); }),
			BuildRide(0.0f, 6.0f, [](float T) { FLessonSample S = Riding(T); S.KiteElevationDeg = 60.0f; return S; }), false);
		const FObjectiveResult Part = EvaluateOnce(Speed, Broken);
		TestNearlyEqual(TEXT("Held progress after the break: 2.9 of 5 s"), Part.Progress, 2.9f / 5.0f, 0.02f);

		FLessonObjective Kite = Objective(EM::KiteElevationHeld, 35.0f, 55.0f);
		Kite.WindowSeconds = 5.0f;
		BothSides(*this, Cov, TEXT("35 to 55 deg for 5 s"), Kite, BuildRide(0.0f, 6.0f, [](float T) { return Riding(T); }),
			BuildRide(0.0f, 6.0f, [](float T) { FLessonSample S = Riding(T); S.KiteElevationDeg = 60.0f; return S; }), false);

		FLessonObjective Edge = Objective(EM::ChannelHeld, 0.7f, 1.0f);
		Edge.Channel = EC::EdgeAbs;
		Edge.WindowSeconds = 1.0f;
		BothSides(*this, Cov, TEXT("edge 0.7 or more for 1 s"), Edge,
			BuildRide(0.0f, 2.0f, [](float T) { FLessonSample S = Riding(T); S.EdgeInput = -0.8f; return S; }),
			BuildRide(0.0f, 2.0f, [](float T) { FLessonSample S = Riding(T); S.EdgeInput = 0.5f; return S; }), false);
	}

	// Ride events.
	{
		auto PlaningFrom = [](float PlaneT, float EndT)
		{
			return BuildRide(0.0f, EndT, [PlaneT](float T)
			{
				FLessonSample S = Riding(T);
				S.BoardState = T < PlaneT ? EBoardState::Displacement : EBoardState::Planing;
				return S;
			});
		};
		BothSides(*this, Cov, TEXT("within 4 s, never"), Objective(EM::TimeToPlaning, 0.0f, 4.0f), PlaningFrom(3.0f, 6.0f), PlaningFrom(10.0f, 6.0f), true);
		BothSides(*this, Cov, TEXT("within 4 s, at 5 s"), Objective(EM::TimeToPlaning, 0.0f, 4.0f), PlaningFrom(3.0f, 6.0f), PlaningFrom(5.0f, 6.0f), true);
		TestNearlyEqual(TEXT("Time to planing (s)"), EvaluateOnce(Objective(EM::TimeToPlaning, 0.0f, 4.0f), PlaningFrom(3.0f, 6.0f)).Value, 3.0f, 0.02f);

		auto Upwind = [](float RateMS, bool bPlaningAtEnd)
		{
			return BuildRide(0.0f, 20.0f, [RateMS, bPlaningAtEnd](float T)
			{
				FLessonSample S = Riding(T);
				S.UpwindM = RateMS * T;
				S.BoardState = (bPlaningAtEnd || T < 19.0f) ? EBoardState::Planing : EBoardState::Displacement;
				return S;
			});
		};
		BothSides(*this, Cov, TEXT("50 m, short"), Objective(EM::UpwindGain, 50.0f), Upwind(3.0f, true), Upwind(2.0f, true), false);
		BothSides(*this, Cov, TEXT("50 m, not planing"), Objective(EM::UpwindGain, 50.0f), Upwind(3.0f, true), Upwind(3.0f, false), false);

		auto Distance = [](float PlaneT)
		{
			return BuildRide(0.0f, 10.0f, [PlaneT](float T)
			{
				FLessonSample S = Riding(T);
				S.BoardState = T < PlaneT ? EBoardState::Displacement : EBoardState::Planing;
				return S;
			});
		};
		BothSides(*this, Cov, TEXT("50 m"), Objective(EM::DistanceRidden, 50.0f), Distance(0.0f), Distance(5.0f), false);
		FLessonObjective Toeside = Objective(EM::DistanceRidden, 50.0f);
		Toeside.Conditions.Add(LessonRules::Condition(LessonRules::Channel(EC::Toeside, EA::Latest, ER::At), 1.0f, 1.0f));
		BothSides(*this, Cov, TEXT("50 m toeside"), Toeside,
			BuildRide(0.0f, 10.0f, [](float T) { FLessonSample S = Riding(T); S.bToeside = true; return S; }), Distance(0.0f), false);

		FLessonObjective Dive = Objective(EM::KiteDives, 30.0f, 70.0f);
		Dive.WindowSeconds = 2.0f;
		BothSides(*this, Cov, TEXT("too deep"), Dive, OneDive(1.0f, 45.0f, 1.0f, 4.0f), OneDive(1.0f, 25.0f, 1.0f, 4.0f), true);
		BothSides(*this, Cov, TEXT("too slow"), Dive, OneDive(1.0f, 45.0f, 1.0f, 4.0f), OneDive(1.0f, 45.0f, 4.0f, 6.0f), true);
		BothSides(*this, Cov, TEXT("too shallow to be a dive"), Dive, OneDive(1.0f, 45.0f, 1.0f, 4.0f), OneDive(1.0f, 60.0f, 1.0f, 4.0f), false);

		const FSchoolScene Turned = TackChange(4.0f, 8.0f, [](FLessonSample& S, float T) { S.SpeedMS = T < 4.0f ? 8.0f : 7.0f; });
		BothSides(*this, Cov, TEXT("a settled tack change"), Objective(EM::Transition, 1.0f), Turned,
			BuildRide(0.0f, 8.0f, [](float T) { return Riding(T); }), false);
		BothSides(*this, Cov, TEXT("70% kept"), Objective(EM::TransitionSpeedKept, 0.7f), Turned,
			TackChange(4.0f, 8.0f, [](FLessonSample& S, float T) { S.SpeedMS = T < 4.0f ? 8.0f : 4.0f; }), true);
		BothSides(*this, Cov, TEXT("0.1 s or less"), Objective(EM::TransitionNotPlaningSeconds, 0.0f, 0.1f), Turned,
			TackChange(4.0f, 8.0f, [](FLessonSample& S, float T) { if (T >= 4.0f && T < 5.0f) { S.BoardState = EBoardState::Displacement; } }), true);
		TestNearlyEqual(TEXT("Seconds not planing around the turn (s)"),
			EvaluateOnce(Objective(EM::TransitionNotPlaningSeconds, 0.0f, 0.1f),
				TackChange(4.0f, 8.0f, [](FLessonSample& S, float T) { if (T >= 4.0f && T < 5.0f) { S.BoardState = EBoardState::Displacement; } })).Value,
			1.0f, 0.03f);
		auto KiteCross = [](float CrossT)
		{
			return TackChange(5.0f, 8.0f, [CrossT](FLessonSample& S, float T) { S.KiteClockDeg = (T - CrossT) * 20.0f; });
		};
		BothSides(*this, Cov, TEXT("0 to 1.5 s"), Objective(EM::KiteLeadAtTransition, 0.0f, 1.5f), KiteCross(4.5f), KiteCross(3.0f), true);
		TestNearlyEqual(TEXT("Kite led the board by 0.5 s"), EvaluateOnce(Objective(EM::KiteLeadAtTransition, 0.0f, 1.5f), KiteCross(4.5f)).Value, 0.5f, 0.02f);
	}

	// Every metric was checked.
	const UEnum* Enum = StaticEnum<ELessonMetric>();
	for (int32 I = 0; I < Enum->NumEnums() - 1; ++I)
	{
		const int64 Value = Enum->GetValueByIndex(I);
		if (Value == static_cast<int64>(EM::None))
		{
			continue;
		}
		TestTrue(TEXT("Both sides checked for ") + Enum->GetNameStringByIndex(I), Cov.Metrics.Contains(Value));
	}
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolChannelWindows, "KiteSurf.School.ChannelWindows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfSchoolChannelWindows::RunTest(const FString& Parameters)
{
	using namespace SchoolLessonTest;
	// Speed = t, kite elevation = 90 - t, tack +1 until 2.5 s then -1.
	const FSchoolScene Ramp = BuildRide(0.0f, 10.0f, [](float T)
	{
		FLessonSample S = Riding(T);
		S.SpeedMS = T;
		S.KiteElevationDeg = 90.0f - T;
		S.Tack = T < 2.5f ? 1 : -1;
		return S;
	});
	FJumpRecord J;
	J.Index = 0;
	J.TakeoffTimeSeconds = 4.0f;
	J.ApexTimeSeconds = 5.0f;
	J.LandingTimeSeconds = 6.0f;
	const FLessonJumpExtras X;
	const float NoEvent = std::numeric_limits<float>::quiet_NaN();

	auto Read = [&](const FLessonMeasure& M, const FJumpRecord* Jump, float EventT, float& Out)
	{
		return LessonEval::ReadMeasure(M, Ramp.Telemetry, Jump, X, EventT, Out);
	};
	auto Expect = [&](const TCHAR* What, const FLessonMeasure& M, float Expected, const FJumpRecord* Jump = nullptr, float EventT = std::numeric_limits<float>::quiet_NaN())
	{
		float V = 0.0f;
		const bool bRead = Read(M, Jump, EventT, V);
		TestTrue(FString(What) + TEXT(": readable"), bRead);
		TestNearlyEqual(FString(What), V, Expected, 1e-3f);
	};
	using LessonRules::Channel;
	Expect(TEXT("At the newest sample"), Channel(EC::Speed, EA::Latest, ER::At), 10.0f);
	Expect(TEXT("At 1 s ago"), Channel(EC::Speed, EA::Latest, ER::At, -1.0f, -1.0f), 9.0f);
	Expect(TEXT("Min over the last 2 s"), Channel(EC::Speed, EA::Latest, ER::Min, -2.0f, 0.0f), 8.0f);
	Expect(TEXT("Max over the last 2 s"), Channel(EC::Speed, EA::Latest, ER::Max, -2.0f, 0.0f), 10.0f);
	Expect(TEXT("Mean over the last 2 s"), Channel(EC::Speed, EA::Latest, ER::Mean, -2.0f, 0.0f), 9.0f);
	Expect(TEXT("Range over the last 2 s"), Channel(EC::Speed, EA::Latest, ER::Range, -2.0f, 0.0f), 2.0f);
	Expect(TEXT("Change over the last 2 s"), Channel(EC::Speed, EA::Latest, ER::Change, -2.0f, 0.0f), 2.0f);
	Expect(TEXT("Rise over the last 2 s"), Channel(EC::Speed, EA::Latest, ER::Rise, -2.0f, 0.0f), 2.0f);
	Expect(TEXT("Drop of a rising channel"), Channel(EC::Speed, EA::Latest, ER::Drop, -2.0f, 0.0f), 0.0f);
	Expect(TEXT("Drop of a falling channel"), Channel(EC::KiteElevation, EA::Latest, ER::Drop, -2.0f, 0.0f), 2.0f);
	Expect(TEXT("Rise of a falling channel"), Channel(EC::KiteElevation, EA::Latest, ER::Rise, -2.0f, 0.0f), 0.0f);
	Expect(TEXT("At take-off"), Channel(EC::Speed, EA::Takeoff, ER::At), 4.0f, &J);
	Expect(TEXT("Min around take-off"), Channel(EC::Speed, EA::Takeoff, ER::Min, -1.0f, 1.0f), 3.0f, &J);
	Expect(TEXT("At the apex"), Channel(EC::Speed, EA::Apex, ER::At), 5.0f, &J);
	Expect(TEXT("At touchdown"), Channel(EC::Speed, EA::Touchdown, ER::At), 6.0f, &J);
	Expect(TEXT("At a given event"), Channel(EC::Speed, EA::Event, ER::At), 3.0f, nullptr, 3.0f);
	Expect(TEXT("At the last settled tack change when no event is given"), Channel(EC::Speed, EA::Event, ER::At), 2.5f, nullptr, NoEvent);

	float V = 0.0f;
	TestFalse(TEXT("Take-off anchor without a jump"), Read(Channel(EC::Speed, EA::Takeoff, ER::At), nullptr, NoEvent, V));
	TestFalse(TEXT("Window after the newest sample"), Read(Channel(EC::Speed, EA::Latest, ER::Max, 1.0f, 2.0f), nullptr, NoEvent, V));
	TestFalse(TEXT("Window before the oldest sample"), Read(Channel(EC::Speed, EA::Takeoff, ER::Rise, -10.0f, 0.0f), &J, NoEvent, V));
	TestFalse(TEXT("A jump metric without a jump"), Read(LessonRules::Jump(EM::JumpHeight), nullptr, NoEvent, V));
	TestFalse(TEXT("A ride metric that is not a transition cannot be a measure"), Read(LessonRules::Jump(EM::UpwindGain), &J, NoEvent, V));

	const FSchoolScene NoTack = BuildRide(0.0f, 3.0f, [](float T) { return Riding(T); });
	TestFalse(TEXT("Event anchor with no tack change"),
		LessonEval::ReadMeasure(Channel(EC::Speed, EA::Event, ER::At), NoTack.Telemetry, nullptr, X, NoEvent, V));

	// Compare.
	TestTrue(TEXT("Less"), LessonEval::Compare(ELessonCompare::Less, 1.0f, 2.0f));
	TestFalse(TEXT("Less, equal"), LessonEval::Compare(ELessonCompare::Less, 2.0f, 2.0f));
	TestTrue(TEXT("LessOrEqual, equal"), LessonEval::Compare(ELessonCompare::LessOrEqual, 2.0f, 2.0f));
	TestTrue(TEXT("Greater"), LessonEval::Compare(ELessonCompare::Greater, 3.0f, 2.0f));
	TestFalse(TEXT("Greater, equal"), LessonEval::Compare(ELessonCompare::Greater, 2.0f, 2.0f));
	TestTrue(TEXT("GreaterOrEqual, equal"), LessonEval::Compare(ELessonCompare::GreaterOrEqual, 2.0f, 2.0f));
	TestTrue(TEXT("Equal within 0.001"), LessonEval::Compare(ELessonCompare::Equal, 2.0005f, 2.0f));
	TestFalse(TEXT("Equal, apart"), LessonEval::Compare(ELessonCompare::Equal, 2.1f, 2.0f));
	TestTrue(TEXT("NotEqual"), LessonEval::Compare(ELessonCompare::NotEqual, 2.1f, 2.0f));
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolCountingRules, "KiteSurf.School.CountingRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfSchoolCountingRules::RunTest(const FString& Parameters)
{
	using namespace SchoolLessonTest;

	// Jumps in a row: the B2 shape, three needed.
	{
		FLessonObjective Row = Objective(EM::JumpHeight, 1.0f, 2.0f, 3);
		Row.bInARow = true;
		Row.Conditions.Add(LessonRules::GradeAtLeast(ELandingGrade::Clean));
		const FLessonTelemetry Empty;
		FLessonProgress P;
		int32 NextIndex = 0;
		auto Jump = [&](float HeightM, ELandingGrade Grade)
		{
			FJumpRecord J;
			J.Index = NextIndex++;
			J.ApexHeightCm = HeightM * 100.0f;
			J.Grade = Grade;
			const FObjectiveResult R = LessonEval::EvaluateObjective(Row, P, Empty, &J);
			LessonEval::ApplyResult(P, R);
			return R;
		};
		Jump(1.5f, ELandingGrade::Clean);
		const FObjectiveResult Second = Jump(1.2f, ELandingGrade::Stomped);
		TestEqual(TEXT("Two in a row"), P.Count, 2);
		TestNearlyEqual(TEXT("Progress two of three"), Second.Progress, 2.0f / 3.0f, 1e-5f);
		TestFalse(TEXT("Not passed at two"), Second.bPassed);
		const FObjectiveResult Miss = Jump(1.5f, ELandingGrade::Sketchy);
		TestTrue(TEXT("A sketchy landing fails the attempt"), Miss.bFailed);
		TestEqual(TEXT("The miss resets the streak"), P.Count, 0);
		Jump(1.5f, ELandingGrade::Clean);
		Jump(1.5f, ELandingGrade::Clean);
		const FObjectiveResult Third = Jump(1.9f, ELandingGrade::Clean);
		TestTrue(TEXT("Three in a row passes"), Third.bPassed);
		TestNearlyEqual(TEXT("Progress complete"), Third.Progress, 1.0f, 1e-6f);
		TestEqual(TEXT("Attempts counted"), P.Attempts, 6);
		TestEqual(TEXT("Failures counted"), P.Failures, 1);

		// The same jump again counts nothing.
		FJumpRecord Same;
		Same.Index = NextIndex - 1;
		Same.ApexHeightCm = 150.0f;
		const FObjectiveResult Again = LessonEval::EvaluateObjective(Row, P, Empty, &Same);
		TestFalse(TEXT("A jump already judged is not counted again"), Again.bCounted || Again.bFailed);
		TestEqual(TEXT("Count unchanged"), Again.NewProgress.Count, P.Count);

		// Not in a row: a miss keeps the count.
		FLessonObjective Total = Row;
		Total.bInARow = false;
		FLessonProgress Q;
		FJumpRecord Good;
		Good.Index = 0;
		Good.ApexHeightCm = 150.0f;
		LessonEval::ApplyResult(Q, LessonEval::EvaluateObjective(Total, Q, Empty, &Good));
		FJumpRecord Bad = Good;
		Bad.Index = 1;
		Bad.Grade = ELandingGrade::Crash;
		LessonEval::ApplyResult(Q, LessonEval::EvaluateObjective(Total, Q, Empty, &Bad));
		TestEqual(TEXT("Without bInARow a miss keeps the count"), Q.Count, 1);

		// BeginStep ignores the jump that was newest when the step began.
		const FLessonProgress Fresh = LessonEval::BeginStep(Empty, 4);
		FJumpRecord Old = Good;
		Old.Index = 4;
		TestFalse(TEXT("A jump from before the step is not counted"), LessonEval::EvaluateObjective(Total, Fresh, Empty, &Old).bCounted);
		Old.Index = 5;
		TestTrue(TEXT("The next jump is counted"), LessonEval::EvaluateObjective(Total, Fresh, Empty, &Old).bCounted);
		TestEqual(TEXT("No jump: nothing happens"), LessonEval::EvaluateObjective(Total, Fresh, Empty, nullptr).NewProgress.Attempts, 0);
	}

	// One jump on each tack.
	{
		const FSchoolScene Ride = BuildRide(0.0f, 15.0f, [](float T) { FLessonSample S = Riding(T); S.Tack = T < 10.0f ? 1 : -1; return S; });
		FLessonObjective EachTack = Objective(EM::JumpHeight, 1.0f, UE_BIG_NUMBER, 2);
		EachTack.bEachTack = true;
		FLessonProgress P;
		auto Jump = [&](int32 Index, float TakeoffT)
		{
			FJumpRecord J;
			J.Index = Index;
			J.ApexHeightCm = 150.0f;
			J.TakeoffTimeSeconds = TakeoffT;
			const FObjectiveResult R = LessonEval::EvaluateObjective(EachTack, P, Ride.Telemetry, &J);
			LessonEval::ApplyResult(P, R);
			return R;
		};
		TestTrue(TEXT("First jump on tack +1 counts"), Jump(0, 5.0f).bCounted);
		const FObjectiveResult SameTack = Jump(1, 7.0f);
		TestFalse(TEXT("A second jump on the same tack does not count"), SameTack.bCounted);
		TestFalse(TEXT("...and is not a failure"), SameTack.bFailed);
		const FObjectiveResult Other = Jump(2, 12.0f);
		TestTrue(TEXT("A jump on the other tack counts"), Other.bCounted);
		TestTrue(TEXT("One on each tack passes"), Other.bPassed);
	}

	// The director's loop: a sample per step, evaluate, apply. Upwind gain on each tack.
	{
		FLessonObjective Upwind = Objective(EM::UpwindGain, 50.0f, UE_BIG_NUMBER, 2);
		Upwind.bEachTack = true;
		struct FRun
		{
			TArray<float> CountTimes;
			float PassTime = -1.0f;
			FLessonProgress Progress;
		};
		auto Run = [&Upwind](float ChangeT)
		{
			FRun Out;
			FLessonTelemetry T;
			Out.Progress = LessonEval::BeginStep(T);
			for (int32 I = 0; I <= 40 * 60; ++I)
			{
				const float Time = I / Hz;
				FLessonSample S = Riding(Time);
				S.Tack = Time < ChangeT ? 1 : -1;
				S.UpwindM = 3.0f * Time;
				T.Add(S);
				const FObjectiveResult R = LessonEval::EvaluateObjective(Upwind, Out.Progress, T, nullptr);
				LessonEval::ApplyResult(Out.Progress, R);
				if (R.bCounted)
				{
					Out.CountTimes.Add(Time);
				}
				if (R.bPassed && Out.PassTime < 0.0f)
				{
					Out.PassTime = Time;
				}
			}
			return Out;
		};

		const FRun Both = Run(20.0f);
		TestEqual(TEXT("Counted once on each tack"), Both.CountTimes.Num(), 2);
		if (Both.CountTimes.Num() == 2)
		{
			TestNearlyEqual(TEXT("First 50 m at 3 m/s after about 16.7 s"), Both.CountTimes[0], 50.0f / 3.0f, 0.05f);
			TestNearlyEqual(TEXT("Second 50 m counted from the tack change at 20 s"), Both.CountTimes[1], 20.0f + 50.0f / 3.0f, 0.05f);
		}
		TestNearlyEqual(TEXT("Passed with the second gain (s)"), Both.PassTime, 20.0f + 50.0f / 3.0f, 0.05f);

		const FRun OneTack = Run(UE_BIG_NUMBER);
		TestEqual(TEXT("One tack only: counted once"), OneTack.CountTimes.Num(), 1);
		TestTrue(TEXT("One tack only: not passed"), OneTack.PassTime < 0.0f);
		TestEqual(TEXT("One tack only: the second 50 m was judged but not counted"), OneTack.Progress.Attempts, 2);
		TestEqual(TEXT("One tack only: count stays 1"), OneTack.Progress.Count, 1);
	}

	// The director's loop: kite dives, one too deep among them.
	{
		FLessonObjective Dives = Objective(EM::KiteDives, 30.0f, 70.0f, 6);
		Dives.WindowSeconds = 2.0f;
		FLessonTelemetry T;
		FLessonProgress P;
		int32 Failures = 0;
		bool bPassed = false;
		for (int32 I = 0; I <= 30 * 60; ++I)
		{
			const float Time = I / Hz;
			FLessonSample S = Riding(Time);
			// A dive every 3 s from 1 s, 1 s long, to 45 deg; the third to 20 deg.
			const int32 DiveIndex = FMath::FloorToInt((Time - 1.0f) / 3.0f);
			const float Phase = (Time - 1.0f) - 3.0f * DiveIndex;
			const float Depth = DiveIndex == 2 ? 20.0f : 45.0f;
			S.KiteElevationDeg = (Time >= 1.0f && Phase < 1.0f) ? 85.0f - (85.0f - Depth) * FMath::Sin(PI * Phase) : 85.0f;
			T.Add(S);
			const FObjectiveResult R = LessonEval::EvaluateObjective(Dives, P, T, nullptr);
			LessonEval::ApplyResult(P, R);
			Failures += R.bFailed ? 1 : 0;
			bPassed |= R.bPassed;
		}
		TestEqual(TEXT("One dive failed (too deep), reported once"), Failures, 1);
		TestTrue(TEXT("Six good dives pass"), bPassed);
		TestTrue(TEXT("Count reached six"), P.Count >= 6);
	}

	// Transitions in the director's loop are judged once each, after they settle.
	{
		const FLessonObjective Turns = Objective(EM::TransitionSpeedKept, 0.7f, UE_BIG_NUMBER, 2);
		FLessonTelemetry T;
		FLessonProgress P;
		int32 Counted = 0;
		float FirstCountTime = -1.0f;
		for (int32 I = 0; I <= 20 * 60; ++I)
		{
			const float Time = I / Hz;
			FLessonSample S = Riding(Time);
			S.Tack = (Time < 5.0f || Time >= 12.0f) ? 1 : -1;
			T.Add(S);
			const FObjectiveResult R = LessonEval::EvaluateObjective(Turns, P, T, nullptr);
			LessonEval::ApplyResult(P, R);
			if (R.bCounted)
			{
				++Counted;
				if (FirstCountTime < 0.0f)
				{
					FirstCountTime = Time;
				}
			}
		}
		TestEqual(TEXT("Two transitions, each counted once"), Counted, 2);
		TestNearlyEqual(TEXT("Judged once settled (2 s after the change)"), FirstCountTime, 7.0f, 0.02f);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolStars, "KiteSurf.School.Stars",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfSchoolStars::RunTest(const FString& Parameters)
{
	FStarRules Rules;
	Rules.HigherBar.Add(LessonRules::GradeAtLeast(ELandingGrade::Stomped));
	FLessonAssists Default;
	Default.bAutoPark = true;
	Default.bAutoEdge = true;
	FLessonAssists Fewer;
	Fewer.bAutoPark = true;
	const FLessonAssists None;

	TestEqual(TEXT("No pass, no stars"), LessonEval::ComputeStars(Rules, Default, None, false, true), 0);
	TestEqual(TEXT("Pass with the default assists: 1"), LessonEval::ComputeStars(Rules, Default, Default, true, false), 1);
	TestEqual(TEXT("Pass with fewer assists: 2"), LessonEval::ComputeStars(Rules, Default, Fewer, true, false), 2);
	TestEqual(TEXT("Pass the higher bar with the default assists: 2"), LessonEval::ComputeStars(Rules, Default, Default, true, true), 2);
	TestEqual(TEXT("Pass with every assist off: 3"), LessonEval::ComputeStars(Rules, Default, None, true, false), 3);
	TestEqual(TEXT("Every assist off and the higher bar: 3"), LessonEval::ComputeStars(Rules, Default, None, true, true), 3);
	TestEqual(TEXT("More assists than the default: 1"), LessonEval::ComputeStars(Rules, Fewer, Default, true, false), 1);

	// A lesson with no assists to turn off needs the higher bar for 3.
	TestEqual(TEXT("No assists to turn off, no higher bar: 1"), LessonEval::ComputeStars(Rules, None, None, true, false), 1);
	TestEqual(TEXT("No assists to turn off, higher bar: 3"), LessonEval::ComputeStars(Rules, None, None, true, true), 3);
	const FStarRules NoBar;
	TestEqual(TEXT("A higher bar the lesson does not have counts for nothing"), LessonEval::ComputeStars(NoBar, Default, Default, true, true), 1);

	// The higher bar objective is the pass objective with the extra conditions.
	const FLessonDef* B2 = LessonCatalog::Find(TEXT("B2"));
	TestNotNull(TEXT("B2 exists"), B2);
	if (B2)
	{
		const FLessonObjective Higher = LessonEval::HigherBarObjective(*B2);
		TestEqual(TEXT("Higher bar adds its conditions"), Higher.Conditions.Num(), B2->Pass.Conditions.Num() + B2->Stars.HigherBar.Num());
		SchoolTestScenes::FJumpScene Clean;
		SchoolTestScenes::FJumpScene Stomped;
		Stomped.Grade = ELandingGrade::Stomped;
		const SchoolTestScenes::FSchoolScene CleanScene = SchoolTestScenes::BuildJump(Clean);
		const SchoolTestScenes::FSchoolScene StompedScene = SchoolTestScenes::BuildJump(Stomped);
		TestTrue(TEXT("A clean jump counts for the pass"), SchoolTestScenes::EvaluateOnce(B2->Pass, CleanScene).bCounted);
		TestFalse(TEXT("A clean jump does not count for the higher bar"), SchoolTestScenes::EvaluateOnce(Higher, CleanScene).bCounted);
		TestTrue(TEXT("A stomped jump counts for the higher bar"), SchoolTestScenes::EvaluateOnce(Higher, StompedScene).bCounted);
		TestEqual(TEXT("B2 stars: default assists and stomped landings"), LessonEval::ComputeStars(B2->Stars, B2->Setup.Assists, B2->Setup.Assists, true, true), 2);
		TestEqual(TEXT("B2 stars: landing assist off"), LessonEval::ComputeStars(B2->Stars, B2->Setup.Assists, None, true, false), 3);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSchoolExtrasFromSignature, "KiteSurf.School.ExtrasFromSignature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfSchoolExtrasFromSignature::RunTest(const FString& Parameters)
{
	FTrickSignature Sig;
	Sig.Inversions.Add(ETrickInversion::BackRoll);
	Sig.SpinHalfTurns = 1;
	FTrickGrab Short;
	Short.HoldSeconds = 0.3f;
	FTrickGrab Long;
	Long.HoldSeconds = 0.8f;
	Sig.Grabs = { Short, Long };
	const FLessonJumpExtras X = LessonEval::ExtrasFromSignature(Sig, ELandingCause::KiteTooLow);
	TestTrue(TEXT("Rotation known"), X.bHasRotation);
	TestNearlyEqual(TEXT("Back roll plus a half turn (deg)"), X.RotationDeg, 540.0f, 1e-4f);
	TestTrue(TEXT("Grab known"), X.bHasGrab);
	TestNearlyEqual(TEXT("Longest grab (s)"), X.GrabHoldSeconds, 0.8f, 1e-6f);
	TestEqual(TEXT("Cause kept"), static_cast<int32>(X.LandingCause), static_cast<int32>(ELandingCause::KiteTooLow));

	const FLessonJumpExtras Plain = LessonEval::ExtrasFromSignature(FTrickSignature());
	TestNearlyEqual(TEXT("A straight jump has no rotation (deg)"), Plain.RotationDeg, 0.0f, 1e-6f);
	TestNearlyEqual(TEXT("...and no grab (s)"), Plain.GrabHoldSeconds, 0.0f, 1e-6f);
	return true;
}

#endif
