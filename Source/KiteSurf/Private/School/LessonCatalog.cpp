#include "School/LessonCatalog.h"

#include "School/LessonEvaluator.h"

#define LOCTEXT_NAMESPACE "LessonCatalog"

// ---------------------------------------------------------------------------------------------
// Builders and the section 3.4 rules.
// ---------------------------------------------------------------------------------------------

FLessonMeasure LessonRules::Jump(ELessonMetric Metric)
{
	FLessonMeasure M;
	M.Metric = Metric;
	return M;
}

FLessonMeasure LessonRules::Channel(ELessonChannel InChannel, ELessonAnchor Anchor, ELessonReduce Reduce, float FromSeconds, float ToSeconds)
{
	FLessonMeasure M;
	M.Metric = ELessonMetric::Channel;
	M.Channel = InChannel;
	M.Anchor = Anchor;
	M.Reduce = Reduce;
	M.FromSeconds = FromSeconds;
	M.ToSeconds = ToSeconds;
	return M;
}

FLessonMeasure LessonRules::Transition(ELessonMetric Metric)
{
	FLessonMeasure M;
	M.Metric = Metric;
	M.Anchor = ELessonAnchor::Event;
	return M;
}

FLessonCondition LessonRules::Condition(const FLessonMeasure& Measure, float Min, float Max)
{
	FLessonCondition C;
	C.Measure = Measure;
	C.Min = Min;
	C.Max = Max;
	return C;
}

FLessonCondition LessonRules::GradeAtLeast(ELandingGrade Grade)
{
	return Condition(Jump(ELessonMetric::LandingGrade), 0.0f, static_cast<float>(static_cast<int32>(Grade)));
}

FLessonFault LessonRules::Fault(FName Id, const FLessonMeasure& Measure, ELessonCompare Compare, float Threshold, int32 Priority, const FText& Feedback)
{
	FLessonFault F;
	F.Id = Id;
	F.Measure = Measure;
	F.Compare = Compare;
	F.Threshold = Threshold;
	F.Priority = Priority;
	F.Feedback = Feedback;
	return F;
}

FLessonFault LessonRules::DivedEarly(int32 Priority, const FText& Feedback)
{
	return Fault(TEXT("DivedEarly"), Jump(ELessonMetric::KiteElevationAtTouchdown), ELessonCompare::Less, DivedEarlyKiteDeg, Priority, Feedback);
}

FLessonFault LessonRules::FrontStall(int32 Priority, const FText& Feedback)
{
	return Fault(TEXT("FrontStall"), Channel(ELessonChannel::KiteDownwind, ELessonAnchor::Touchdown, ELessonReduce::At),
		ELessonCompare::Less, 0.0f, Priority, Feedback);
}

FLessonFault LessonRules::EdgeLostBeforeTakeoff(int32 Priority, const FText& Feedback)
{
	return Fault(TEXT("EdgeLost"), Channel(ELessonChannel::EdgeAbs, ELessonAnchor::Takeoff, ELessonReduce::Min, EdgeLostFromSeconds, EdgeLostToSeconds),
		ELessonCompare::Less, EdgeLostAbs, Priority, Feedback);
}

FLessonFault LessonRules::KiteRoseDuringPop(int32 Priority, const FText& Feedback)
{
	return Fault(TEXT("KiteRose"), Channel(ELessonChannel::KiteElevation, ELessonAnchor::Takeoff, ELessonReduce::Rise, PopFromSeconds, PopToSeconds),
		ELessonCompare::Greater, PopKiteRiseDeg, Priority, Feedback);
}

FLessonFault LessonRules::SheetedInWhileClimbing(int32 Priority, const FText& Feedback)
{
	return Fault(TEXT("SheetedInClimbing"), Channel(ELessonChannel::BarWhileClimbing, ELessonAnchor::Takeoff, ELessonReduce::Max, SendFromSeconds, SendToSeconds),
		ELessonCompare::Greater, SheetedInBar, Priority, Feedback);
}

FLessonFault LessonRules::LandingCause(ELandingCause Cause, int32 Priority, const FText& Feedback)
{
	const FString Name = StaticEnum<ELandingCause>()->GetNameStringByValue(static_cast<int64>(Cause));
	return Fault(FName(*(TEXT("Cause") + Name)), Jump(ELessonMetric::LandingCause), ELessonCompare::Equal,
		static_cast<float>(static_cast<int32>(Cause)), Priority, Feedback);
}

TArray<FLessonFault> LessonRules::LandingCauseFaults(int32 Priority)
{
	return {
		LandingCause(ELandingCause::TooHard, Priority, LOCTEXT("Cause.TooHard", "Landed too hard: dive the kite before touchdown")),
		LandingCause(ELandingCause::KiteTooLow, Priority, LOCTEXT("Cause.KiteTooLow", "The kite was too low to carry you out")),
		LandingCause(ELandingCause::Sideways, Priority, LOCTEXT("Cause.Sideways", "Land with the board pointing where you go")),
		LandingCause(ELandingCause::UnderRotated, Priority, LOCTEXT("Cause.UnderRotated", "Level the board before touchdown")),
		LandingCause(ELandingCause::OverRotated, Priority, LOCTEXT("Cause.OverRotated", "Too much turn: level out earlier")),
		LandingCause(ELandingCause::Inverted, Priority, LOCTEXT("Cause.Inverted", "Upside down: bar straight, kite at 12")),
		LandingCause(ELandingCause::BarLost, Priority, LOCTEXT("Cause.BarLost", "Keep both hands on the bar")),
	};
}

// ---------------------------------------------------------------------------------------------
// Chapters A and B (docs/tutorials.md section 2). Every threshold is an estimate.
// ---------------------------------------------------------------------------------------------

namespace LessonCatalogPrivate
{
	using namespace LessonRules;
	using EM = ELessonMetric;
	using EC = ELessonChannel;
	using EA = ELessonAnchor;
	using ER = ELessonReduce;
	using ECmp = ELessonCompare;

	/** Speeds in the lessons are set in knots and stored in m/s. */
	constexpr float MSPerKnot = 0.5144f;

	/** A3's target speed (m/s) and band (share either side). */
	constexpr float A3TargetSpeedMS = 15.0f * MSPerKnot;
	constexpr float A3Band = 0.15f;

	/** Taking off faster than this is "too fast" for a pop or a jump transition (m/s, about 19 kn). */
	constexpr float TooFastTakeoffMS = 10.0f;

	FLessonObjective Objective(EM Metric, float Min, float Max = UE_BIG_NUMBER, int32 Count = 1)
	{
		FLessonObjective O;
		O.Metric = Metric;
		O.Min = Min;
		O.Max = Max;
		O.Count = Count;
		return O;
	}

	FLessonObjective Held(EM Metric, float Min, float Max, float Seconds)
	{
		FLessonObjective O = Objective(Metric, Min, Max);
		O.WindowSeconds = Seconds;
		return O;
	}

	FLessonStep Step(const FText& Prompt, FName Glyph, ELessonCue Cue, const FLessonObjective& O)
	{
		FLessonStep S;
		S.Prompt = Prompt;
		S.InputGlyph = Glyph;
		S.Cue = Cue;
		S.Objective = O;
		return S;
	}

	FLessonDef Lesson(const TCHAR* Id, const TCHAR* Chapter, const FText& Title, const FText& Summary, TArray<FName> Requires)
	{
		FLessonDef L;
		L.Id = Id;
		L.Chapter = Chapter;
		L.Title = Title;
		L.Summary = Summary;
		L.Requires = MoveTemp(Requires);
		return L;
	}

	/** Chapter A: 10 to 14 kn, the big board, auto-edge and auto-park as each lesson sets them. */
	FLessonSetup RidingSetup(ELessonStart Start, float StartSpeedKnots = 0.0f)
	{
		FLessonSetup S;
		S.WindKnots = 12.0f;
		S.Kite = EKiteModel::Boost;
		S.Board = EBoardSize::Large;
		S.Start = Start;
		S.StartSpeedKnots = StartSpeedKnots;
		return S;
	}

	/** Chapter B: 12 to 16 kn, the medium board for more pop, the landing assist on. */
	FLessonSetup JumpSetup(float WindKnots = 14.0f)
	{
		FLessonSetup S;
		S.WindKnots = WindKnots;
		S.Kite = EKiteModel::Boost;
		S.Board = EBoardSize::Medium;
		S.Start = ELessonStart::Riding;
		S.StartSpeedKnots = 14.0f;
		S.Assists.bLandingAssist = true;
		return S;
	}

	TArray<FLessonDef> ChapterA()
	{
		TArray<FLessonDef> Out;

		{
			FLessonDef L = Lesson(TEXT("A1"), TEXT("A"), LOCTEXT("A1.Title", "Kite power dive"),
				LOCTEXT("A1.Summary", "Dive the kite for power, then fly it straight back up to 12."), {});
			// Standing in the shallows does not exist yet; docs/tutorials.md section 4 allows a floating start.
			L.Setup = RidingSetup(ELessonStart::Floating);
			L.Setup.Assists.bAutoPark = true;
			FLessonObjective Dives = Objective(EM::KiteDives, 30.0f, 70.0f, 6);
			Dives.WindowSeconds = 2.0f;
			FLessonObjective FirstDive = Dives;
			FirstDive.Count = 1;
			L.Steps.Add(Step(LOCTEXT("A1.Step1", "Dive the kite to 45° and back up"), TEXT("IA_Steer"), ELessonCue::GhostKite, FirstDive));
			L.Pass = Dives;
			const FText BackUp = LOCTEXT("A1.Fault.BackUp", "Turn it back up before 45°");
			L.Faults.Add(Fault(TEXT("TooDeep"), Channel(EC::KiteElevation, EA::Latest, ER::Min, -4.0f, 0.0f), ECmp::Less, 30.0f, 2, BackUp));
			L.Faults.Add(Fault(TEXT("TooSlowBack"), Channel(EC::KiteElevation, EA::Latest, ER::Max, -2.0f, 0.0f), ECmp::Less, 70.0f, 1, BackUp));
			L.Stars.HigherBar.Add(Condition(Channel(EC::KiteElevation, EA::Event, ER::Max, 0.0f, 1.5f), 70.0f));
			L.Stars.HigherBarText = LOCTEXT("A1.Higher", "Back above 70° within 1.5 s");
			Out.Add(L);
		}

		{
			FLessonDef L = Lesson(TEXT("A2"), TEXT("A"), LOCTEXT("A2.Title", "Water start"),
				LOCTEXT("A2.Summary", "One dive of the kite pulls you up and onto the plane."), { TEXT("A1") });
			L.Setup = RidingSetup(ELessonStart::Floating);
			L.Setup.Assists.bAutoPark = true;
			L.Setup.Assists.bAutoEdge = true;
			L.Steps.Add(Step(LOCTEXT("A2.Step1", "Dive the kite to get up"), TEXT("IA_Steer"), ELessonCue::GhostKite,
				Objective(EM::TimeToPlaning, 0.0f, 4.0f)));
			FLessonObjective Ride = Objective(EM::DistanceRidden, 50.0f, UE_BIG_NUMBER, 2);
			Ride.bEachTack = true;
			L.Steps.Add(Step(LOCTEXT("A2.Step2", "Ride 50 m, then the same on the other tack"), TEXT("IA_Edge"), ELessonCue::Meter, Ride));
			L.Pass = Ride;
			L.Faults.Add(Fault(TEXT("BoardSquare"), Channel(EC::KiteBearing, EA::Latest, ER::Mean, -3.0f, 0.0f), ECmp::Greater, 60.0f, 2,
				LOCTEXT("A2.Fault.Square", "Point the nose at the kite")));
			// No nose-dive flag exists: a fall right after the start stands in for it.
			L.Faults.Add(Fault(TEXT("Nosedive"), Channel(EC::Fallen, EA::Latest, ER::Max, -3.0f, 0.0f), ECmp::GreaterOrEqual, 1.0f, 1,
				LOCTEXT("A2.Fault.Nosedive", "More weight on the back foot")));
			L.Stars.HigherBar.Add(Condition(Channel(EC::KiteElevation, EA::Latest, ER::At), 30.0f, 60.0f));
			L.Stars.HigherBarText = LOCTEXT("A2.Higher", "Kite between 30° and 60° as you ride");
			Out.Add(L);
		}

		{
			FLessonDef L = Lesson(TEXT("A3"), TEXT("A"), LOCTEXT("A3.Title", "Speed control"),
				LOCTEXT("A3.Summary", "Hold a steady speed with the bar alone, the kite at 45°."), { TEXT("A2") });
			L.Setup = RidingSetup(ELessonStart::Riding, 12.0f);
			L.Setup.Assists.bAutoPark = true;
			L.Setup.Assists.bAutoEdge = true;
			L.Steps.Add(Step(LOCTEXT("A3.Step1", "Fly the kite at 45°"), TEXT("IA_Steer"), ELessonCue::WindowArc,
				Held(EM::KiteElevationHeld, 35.0f, 55.0f, 5.0f)));
			FLessonObjective Hold = Held(EM::SpeedHeld, A3TargetSpeedMS * (1.0f - A3Band), A3TargetSpeedMS * (1.0f + A3Band), 20.0f);
			Hold.Conditions.Add(Condition(Channel(EC::KiteElevation, EA::Latest, ER::At), 35.0f, 55.0f));
			L.Steps.Add(Step(LOCTEXT("A3.Step2", "Bar in for speed, out to slow down"), TEXT("IA_Sheet"), ELessonCue::SpeedBand, Hold));
			L.Pass = Hold;
			L.Faults.Add(Fault(TEXT("KiteParkedHigh"), Channel(EC::KiteElevation, EA::Latest, ER::Mean, -3.0f, 0.0f), ECmp::Greater, 55.0f, 1,
				LOCTEXT("A3.Fault.High", "Kite too high: fly it at 45°")));
			L.Stars.HigherBar.Add(Condition(Channel(EC::Speed, EA::Latest, ER::At), A3TargetSpeedMS * 0.9f, A3TargetSpeedMS * 1.1f));
			L.Stars.HigherBarText = LOCTEXT("A3.Higher", "Within 10% of the target");
			Out.Add(L);
		}

		{
			FLessonDef L = Lesson(TEXT("A4"), TEXT("A"), LOCTEXT("A4.Title", "Upwind"),
				LOCTEXT("A4.Summary", "Edge against the kite to gain ground upwind."), { TEXT("A3") });
			L.Setup = RidingSetup(ELessonStart::Riding, 12.0f);
			L.Setup.Assists.bAutoEdge = true;
			L.Steps.Add(Step(LOCTEXT("A4.Step1", "Plane, then edge and look upwind"), TEXT("IA_Edge"), ELessonCue::Gate,
				Objective(EM::UpwindGain, 15.0f)));
			FLessonObjective Upwind = Objective(EM::UpwindGain, 50.0f, UE_BIG_NUMBER, 2);
			Upwind.bEachTack = true;
			L.Steps.Add(Step(LOCTEXT("A4.Step2", "Reach the buoy on each tack"), TEXT("IA_Edge"), ELessonCue::Gate, Upwind));
			L.Pass = Upwind;
			L.Faults.Add(Fault(TEXT("CarvedTooSoon"), Channel(EC::Planing, EA::Latest, ER::Mean, -5.0f, 0.0f), ECmp::Less, 0.5f, 1,
				LOCTEXT("A4.Fault.PlaneFirst", "Plane first, then carve upwind")));
			L.Faults.Add(Fault(TEXT("SpeedCollapsed"), Channel(EC::Speed, EA::Latest, ER::Drop, -4.0f, 0.0f), ECmp::Greater, 2.5f, 2,
				LOCTEXT("A4.Fault.EaseEdge", "Ease the edge")));
			L.Stars.HigherBar.Add(Condition(Channel(EC::Planing, EA::Latest, ER::Min, -5.0f, 0.0f), 1.0f));
			L.Stars.HigherBarText = LOCTEXT("A4.Higher", "Planing all the way");
			Out.Add(L);
		}

		{
			FLessonDef L = Lesson(TEXT("A5"), TEXT("A"), LOCTEXT("A5.Title", "Transition"),
				LOCTEXT("A5.Summary", "Slow down, switch, and ride away the other way."), { TEXT("A4") });
			L.Setup = RidingSetup(ELessonStart::Riding, 12.0f);
			L.Setup.Assists.bAutoPark = true;
			L.Steps.Add(Step(LOCTEXT("A5.Step1", "Slow down: bar out"), TEXT("IA_Sheet"), ELessonCue::SpeedBand,
				Held(EM::SpeedHeld, 0.0f, 5.0f, 1.0f)));
			L.Steps.Add(Step(LOCTEXT("A5.Step2", "Steer the kite across 12"), TEXT("IA_Steer"), ELessonCue::GhostKite,
				[] { FLessonObjective O = Objective(EM::Channel, 0.0f, 10.0f); O.Channel = EC::KiteClockAbs; return O; }()));
			FLessonObjective Turns = Objective(EM::TransitionSpeedKept, 0.7f, UE_BIG_NUMBER, 3);
			Turns.Conditions.Add(Condition(Channel(EC::Fallen, EA::Event, ER::Max, -LessonEval::TransitionSettleSeconds, LessonEval::TransitionSettleSeconds), 0.0f, 0.0f));
			L.Steps.Add(Step(LOCTEXT("A5.Step3", "Switch your feet and ride away"), TEXT("IA_Edge"), ELessonCue::None, Turns));
			L.Pass = Turns;
			L.Faults.Add(Fault(TEXT("SankAfter"), Channel(EC::Planing, EA::Event, ER::Mean, 0.0f, LessonEval::TransitionSettleSeconds), ECmp::Less, 0.5f, 1,
				LOCTEXT("A5.Fault.Sank", "Steer the kite faster through the turn")));
			L.Stars.HigherBar.Add(Condition(Transition(EM::TransitionSpeedKept), 0.85f));
			L.Stars.HigherBarText = LOCTEXT("A5.Higher", "Keep 85% of your speed");
			Out.Add(L);
		}

		{
			FLessonDef L = Lesson(TEXT("A6"), TEXT("A"), LOCTEXT("A6.Title", "Toeside"),
				LOCTEXT("A6.Summary", "Ride on your toe edge, facing away from the kite."), { TEXT("A5") });
			L.RequiredFeature = ELessonFeature::ToesideRiding;
			L.Setup = RidingSetup(ELessonStart::Riding, 12.0f);
			FLessonObjective Toeside = Objective(EM::DistanceRidden, 100.0f);
			Toeside.Conditions.Add(Condition(Channel(EC::Toeside, EA::Latest, ER::At), 1.0f, 1.0f));
			L.Steps.Add(Step(LOCTEXT("A6.Step1", "Turn toeside and ride"), TEXT("IA_Edge"), ELessonCue::Meter, Toeside));
			L.Pass = Toeside;
			// Which hand steers is not measured: a kite dropping low while toeside stands in for front-hand steering.
			L.Faults.Add(Fault(TEXT("FrontHandSteering"), Channel(EC::KiteElevation, EA::Latest, ER::Min, -3.0f, 0.0f), ECmp::Less, 25.0f, 1,
				LOCTEXT("A6.Fault.BackHand", "Back hand flies the kite")));
			L.Stars.HigherBar.Add(Condition(Channel(EC::KiteElevation, EA::Latest, ER::At), 35.0f, 60.0f));
			L.Stars.HigherBarText = LOCTEXT("A6.Higher", "Kite steady between 35° and 60°");
			Out.Add(L);
		}

		{
			FLessonDef L = Lesson(TEXT("A7"), TEXT("A"), LOCTEXT("A7.Title", "Carving transition"),
				LOCTEXT("A7.Summary", "Carve round while the kite crosses 12, and never stop planing."), { TEXT("A5") });
			L.Setup = RidingSetup(ELessonStart::Riding, 14.0f);
			L.Steps.Add(Step(LOCTEXT("A7.Step1", "Kite across 12 as you carve"), TEXT("IA_Steer"), ELessonCue::TimingRing,
				Objective(EM::Transition, 1.0f)));
			const FLessonObjective Carve = Objective(EM::TransitionNotPlaningSeconds, 0.0f, 0.1f);
			L.Steps.Add(Step(LOCTEXT("A7.Step2", "Carve through without stopping"), TEXT("IA_Edge"), ELessonCue::TimingRing, Carve));
			L.Pass = Carve;
			L.Faults.Add(Fault(TEXT("KiteEarly"), Transition(EM::KiteLeadAtTransition), ECmp::Greater, 1.5f, 2,
				LOCTEXT("A7.Fault.Early", "The kite left the power too soon")));
			L.Faults.Add(Fault(TEXT("KiteLate"), Transition(EM::KiteLeadAtTransition), ECmp::Less, 0.0f, 1,
				LOCTEXT("A7.Fault.Late", "You sank waiting for pull")));
			L.Stars.HigherBar.Add(Condition(Transition(EM::KiteLeadAtTransition), 0.0f, 1.0f));
			L.Stars.HigherBarText = LOCTEXT("A7.Higher", "Kite and board in time");
			Out.Add(L);
		}

		return Out;
	}

	TArray<FLessonDef> ChapterB()
	{
		TArray<FLessonDef> Out;
		const FLessonCondition Clean = GradeAtLeast(ELandingGrade::Clean);
		const FLessonCondition Stomped = GradeAtLeast(ELandingGrade::Stomped);
		const FText StompedText = LOCTEXT("B.Higher.Stomped", "Stomped landings");

		{
			FLessonDef L = Lesson(TEXT("B1"), TEXT("B"), LOCTEXT("B1.Title", "Pop"),
				LOCTEXT("B1.Summary", "Jump off the water with the board, not the kite."), { TEXT("A4") });
			L.Setup = JumpSetup();
			FLessonObjective Carve = Held(EM::ChannelHeld, 0.7f, 1.0f, 1.0f);
			Carve.Channel = EC::EdgeAbs;
			Carve.Conditions.Add(Condition(Channel(EC::Planing, EA::Latest, ER::At), 1.0f, 1.0f));
			L.Steps.Add(Step(LOCTEXT("B1.Step1", "Carve: edge hard upwind"), TEXT("IA_Edge"), ELessonCue::Meter, Carve));
			FLessonObjective Stomp = Objective(EM::JumpHeight, 0.2f);
			Stomp.Conditions.Add(Condition(Jump(EM::Popped), 1.0f, 1.0f));
			L.Steps.Add(Step(LOCTEXT("B1.Step2", "Stomp: load, then let go"), TEXT("IA_Jump"), ELessonCue::TimingRing, Stomp));
			FLessonObjective Pop = Objective(EM::JumpHeight, 0.5f);
			Pop.Conditions.Add(Condition(Jump(EM::Popped), 1.0f, 1.0f));
			Pop.Conditions.Add(Condition(Channel(EC::KiteElevation, EA::Takeoff, ER::Range, PopFromSeconds, PopToSeconds), 0.0f, PopKiteRiseDeg));
			L.Steps.Add(Step(LOCTEXT("B1.Step3", "Carve and stomp: pop"), TEXT("IA_Jump"), ELessonCue::None, Pop));
			L.Pass = Pop;
			L.Faults.Add(KiteRoseDuringPop(2, LOCTEXT("B1.Fault.KiteRose", "You lifted the kite: pop with the board")));
			L.Faults.Add(Fault(TEXT("Skipped"), Jump(EM::TakeoffSpeed), ECmp::Greater, TooFastTakeoffMS, 1,
				LOCTEXT("B1.Fault.Skipped", "Too fast: edge earlier")));
			L.Stars.HigherBar.Add(Condition(Jump(EM::JumpHeight), 1.0f));
			L.Stars.HigherBarText = LOCTEXT("B1.Higher", "Pop 1 m");
			Out.Add(L);
		}

		{
			FLessonDef L = Lesson(TEXT("B2"), TEXT("B"), LOCTEXT("B2.Title", "Small jump"),
				LOCTEXT("B2.Summary", "Send the kite slowly, sheet in at 12, land with control."), { TEXT("B1") });
			L.Setup = JumpSetup();
			L.Steps.Add(Step(LOCTEXT("B2.Step1", "Send the kite slowly to 12"), TEXT("IA_Steer"), ELessonCue::GhostKite,
				[] { FLessonObjective O = Objective(EM::Channel, 80.0f); O.Channel = EC::KiteElevation; return O; }()));
			FLessonObjective Jump1 = Objective(EM::JumpHeight, 1.0f, 2.0f, 5);
			Jump1.bInARow = true;
			Jump1.Conditions.Add(Clean);
			L.Steps.Add(Step(LOCTEXT("B2.Step2", "Bar in at 12"), TEXT("IA_Sheet"), ELessonCue::TimingRing, Jump1));
			L.Pass = Jump1;
			L.Faults.Add(SheetedInWhileClimbing(3, LOCTEXT("B2.Fault.Sheeted", "Bar out while it climbs, in at 12")));
			L.Faults.Add(EdgeLostBeforeTakeoff(2, LOCTEXT("B2.Fault.Edge", "Hold the edge until take-off")));
			L.Faults.Append(LandingCauseFaults(0));
			L.Stars.HigherBar.Add(Stomped);
			L.Stars.HigherBarText = StompedText;
			Out.Add(L);
		}

		{
			FLessonDef L = Lesson(TEXT("B3"), TEXT("B"), LOCTEXT("B3.Title", "Landing dive"),
				LOCTEXT("B3.Summary", "Dive the kite in the last second so it catches you."), { TEXT("B2") });
			L.Setup = JumpSetup();
			L.Setup.Assists.bSlowMotion = true;
			FLessonObjective Dive = Objective(EM::DiveBeforeTouchdown, 0.15f, 1.0f);
			Dive.Conditions.Add(Condition(Jump(EM::JumpHeight), 1.0f));
			L.Steps.Add(Step(LOCTEXT("B3.Step1", "Dive the kite now"), TEXT("IA_Steer"), ELessonCue::TimingRing, Dive));
			FLessonObjective Land = Objective(EM::JumpHeight, 3.0f, 5.0f);
			Land.Conditions.Add(Clean);
			Land.Conditions.Add(Condition(Jump(EM::DiveBeforeTouchdown), 0.15f, 1.0f));
			L.Steps.Add(Step(LOCTEXT("B3.Step2", "Jump 3 m and dive to land"), TEXT("IA_Jump"), ELessonCue::None, Land));
			L.Pass = Land;
			const FText Early = LOCTEXT("B3.Fault.Early", "Dived early: the kite was too low to catch you");
			L.Faults.Add(FrontStall(4, LOCTEXT("B3.Fault.FrontStall", "The kite overflew")));
			L.Faults.Add(DivedEarly(3, Early));
			L.Faults.Add(Fault(TEXT("DiveTooSoon"), Jump(EM::DiveBeforeTouchdown), ECmp::Greater, 1.0f, 3, Early));
			L.Faults.Add(Fault(TEXT("DivedLate"), Jump(EM::DiveBeforeTouchdown), ECmp::Less, 0.15f, 2,
				LOCTEXT("B3.Fault.Late", "Dived late: no pull, you sank")));
			L.Faults.Append(LandingCauseFaults(0));
			L.Stars.HigherBar.Add(Stomped);
			L.Stars.HigherBarText = StompedText;
			Out.Add(L);
		}

		{
			FLessonDef L = Lesson(TEXT("B4"), TEXT("B"), LOCTEXT("B4.Title", "Jump and grab"),
				LOCTEXT("B4.Summary", "Grab the board while the kite holds at 12."), { TEXT("B3") });
			L.RequiredFeature = ELessonFeature::Grabs;
			L.Setup = JumpSetup();
			FLessonObjective Grab = Objective(EM::GrabHoldSeconds, 0.5f);
			Grab.Conditions.Add(Condition(Channel(EC::KiteClockAbs, EA::Apex, ER::At), 0.0f, 15.0f));
			Grab.Conditions.Add(Clean);
			// No grab input exists yet (tricks T2.1), so no glyph.
			L.Steps.Add(Step(LOCTEXT("B4.Step1", "Grab in the air"), NAME_None, ELessonCue::None, Grab));
			L.Pass = Grab;
			L.Faults.Add(Fault(TEXT("KiteDrifted"), Channel(EC::KiteClockAbs, EA::Apex, ER::Max, -0.5f, 0.5f), ECmp::Greater, 15.0f, 1,
				LOCTEXT("B4.Fault.Drift", "Hold the kite at 12 while you grab")));
			L.Faults.Append(LandingCauseFaults(0));
			L.Stars.HigherBar.Add(Condition(Jump(EM::GrabHoldSeconds), 1.0f));
			L.Stars.HigherBarText = LOCTEXT("B4.Higher", "Hold the grab 1 s");
			Out.Add(L);
		}

		{
			FLessonDef L = Lesson(TEXT("B5"), TEXT("B"), LOCTEXT("B5.Title", "Jump transition"),
				LOCTEXT("B5.Summary", "Turn in the air and land riding the other way."), { TEXT("B3"), TEXT("A5") });
			L.Setup = JumpSetup();
			FLessonObjective Turn = Objective(EM::HeadingChange, 120.0f);
			Turn.Conditions.Add(Clean);
			L.Steps.Add(Step(LOCTEXT("B5.Step1", "Jump, steer across, land the other way"), TEXT("IA_Jump"), ELessonCue::GhostKite, Turn));
			L.Pass = Turn;
			L.Faults.Add(Fault(TEXT("KitePast12"), Channel(EC::KiteClockAbs, EA::Touchdown, ER::At), ECmp::Greater, 40.0f, 2,
				LOCTEXT("B5.Fault.Past12", "No room to dive")));
			L.Faults.Add(Fault(TEXT("TooFast"), Jump(EM::TakeoffSpeed), ECmp::Greater, TooFastTakeoffMS, 1,
				LOCTEXT("B5.Fault.TooFast", "Pop harder to kill your speed")));
			L.Faults.Append(LandingCauseFaults(0));
			L.Stars.HigherBar.Add(Stomped);
			L.Stars.HigherBarText = StompedText;
			Out.Add(L);
		}

		{
			FLessonDef L = Lesson(TEXT("B6"), TEXT("B"), LOCTEXT("B6.Title", "Downloop transition"),
				LOCTEXT("B6.Summary", "Loop the kite through the bottom of the window to turn."), { TEXT("B5") });
			L.Setup = JumpSetup(10.0f); // very light wind
			L.Setup.StartSpeedKnots = 10.0f;
			FLessonObjective Loop = Objective(EM::Transition, 1.0f);
			Loop.Conditions.Add(Condition(Channel(EC::CompletedLoops, EA::Event, ER::Change, -4.0f, LessonEval::TransitionSettleSeconds), 1.0f));
			Loop.Conditions.Add(Condition(Channel(EC::Fallen, EA::Event, ER::Max, -4.0f, LessonEval::TransitionSettleSeconds), 0.0f, 0.0f));
			L.Steps.Add(Step(LOCTEXT("B6.Step1", "Ride downwind, then loop the kite"), TEXT("IA_Steer"), ELessonCue::GhostKite, Loop));
			L.Pass = Loop;
			L.Faults.Add(Fault(TEXT("LetGoMidLoop"), Channel(EC::KiteElevation, EA::Event, ER::Min, -4.0f, LessonEval::TransitionSettleSeconds),
				ECmp::Less, 20.0f, 1, LOCTEXT("B6.Fault.LetGo", "Keep steering until the kite climbs")));
			L.Stars.HigherBar.Add(Condition(Transition(EM::TransitionSpeedKept), 0.7f));
			L.Stars.HigherBarText = LOCTEXT("B6.Higher", "Keep 70% of your speed");
			Out.Add(L);
		}

		return Out;
	}

	/** True when a measure can be read by LessonEval::ReadMeasure. */
	bool IsReadableMeasure(const FLessonMeasure& M)
	{
		switch (LessonEval::GetMetricSource(M.Metric))
		{
		case ELessonMetricSource::Jump:
		case ELessonMetricSource::Channel:
			return true;
		case ELessonMetricSource::Ride:
			return M.Metric == EM::Transition || M.Metric == EM::TransitionSpeedKept
				|| M.Metric == EM::TransitionNotPlaningSeconds || M.Metric == EM::KiteLeadAtTransition;
		default:
			return false;
		}
	}

	/** Problems with one measure's window. */
	void CheckWindow(const FString& Where, const FLessonMeasure& M, TArray<FString>& Problems)
	{
		const float MaxSpan = (LessonTelemetry::DefaultCapacity - 1) / 60.0f - 1.0f;
		if (M.Metric != EM::Channel)
		{
			return;
		}
		if (M.ToSeconds < M.FromSeconds)
		{
			Problems.Add(Where + TEXT(": window ends before it starts"));
		}
		if (M.FromSeconds < -MaxSpan || M.ToSeconds > MaxSpan)
		{
			Problems.Add(Where + TEXT(": window does not fit in the telemetry"));
		}
		if (M.Anchor == EA::Latest && M.ToSeconds > 0.0f)
		{
			Problems.Add(Where + TEXT(": window reaches past the newest sample"));
		}
		if (M.Anchor == EA::Event && M.ToSeconds > LessonEval::TransitionSettleSeconds)
		{
			Problems.Add(Where + TEXT(": window reaches past the transition settle time"));
		}
	}

	void CheckObjective(const FString& Where, const FLessonObjective& O, TArray<FString>& Problems)
	{
		if (!O.IsSet())
		{
			Problems.Add(Where + TEXT(": no objective"));
			return;
		}
		if (O.Count < 1)
		{
			Problems.Add(Where + TEXT(": count under 1"));
		}
		if (O.Min > O.Max)
		{
			Problems.Add(Where + TEXT(": min above max"));
		}
		const ELessonMetricSource Source = LessonEval::GetMetricSource(O.Metric);
		const float MaxSpan = (LessonTelemetry::DefaultCapacity - 1) / 60.0f - 1.0f;
		if (Source == ELessonMetricSource::Held && O.WindowSeconds > MaxSpan)
		{
			Problems.Add(Where + TEXT(": hold longer than the telemetry"));
		}
		for (int32 I = 0; I < O.Conditions.Num(); ++I)
		{
			const FLessonCondition& C = O.Conditions[I];
			const FString CWhere = FString::Printf(TEXT("%s condition %d"), *Where, I);
			if (!IsReadableMeasure(C.Measure))
			{
				Problems.Add(CWhere + TEXT(": measure cannot be read"));
			}
			if ((Source == ELessonMetricSource::Held || O.Metric == EM::DistanceRidden) && C.Measure.Metric != EM::Channel)
			{
				Problems.Add(CWhere + TEXT(": held and distance objectives only read channel conditions"));
			}
			if (C.Min > C.Max)
			{
				Problems.Add(CWhere + TEXT(": min above max"));
			}
			CheckWindow(CWhere, C.Measure, Problems);
		}
	}
}

const TArray<FLessonDef>& LessonCatalog::GetAll()
{
	static const TArray<FLessonDef> All = []
	{
		TArray<FLessonDef> Lessons = LessonCatalogPrivate::ChapterA();
		Lessons.Append(LessonCatalogPrivate::ChapterB());
		return Lessons;
	}();
	return All;
}

const FLessonDef* LessonCatalog::Find(FName Id)
{
	return GetAll().FindByPredicate([Id](const FLessonDef& L) { return L.Id == Id; });
}

bool LessonCatalog::IsFeatureBuilt(ELessonFeature Feature)
{
	return Feature == ELessonFeature::None;
}

bool LessonCatalog::IsAvailable(const FLessonDef& Lesson)
{
	return IsFeatureBuilt(Lesson.RequiredFeature);
}

TArray<FString> LessonCatalog::Validate(const TArray<FLessonDef>& Lessons)
{
	using namespace LessonCatalogPrivate;
	TArray<FString> Problems;
	TMap<FName, int32> IndexOf;

	for (int32 I = 0; I < Lessons.Num(); ++I)
	{
		const FLessonDef& L = Lessons[I];
		const FString Id = L.Id.ToString();
		if (L.Id.IsNone())
		{
			Problems.Add(FString::Printf(TEXT("lesson %d: no id"), I));
			continue;
		}
		if (IndexOf.Contains(L.Id))
		{
			Problems.Add(Id + TEXT(": duplicate id"));
		}
		IndexOf.Add(L.Id, I);
		if (L.Chapter.IsNone() || !Id.StartsWith(L.Chapter.ToString(), ESearchCase::CaseSensitive))
		{
			Problems.Add(Id + TEXT(": id is not in its chapter"));
		}
		if (L.Title.IsEmpty())
		{
			Problems.Add(Id + TEXT(": no title"));
		}
		if (L.Setup.Map != FName(TEXT("L_FlatWater")))
		{
			Problems.Add(Id + TEXT(": lessons use L_FlatWater"));
		}
		if (L.Steps.Num() < 1 || L.Steps.Num() > 3)
		{
			Problems.Add(Id + TEXT(": one to three steps"));
		}
		for (int32 S = 0; S < L.Steps.Num(); ++S)
		{
			const FString Where = FString::Printf(TEXT("%s step %d"), *Id, S + 1);
			if (L.Steps[S].Prompt.IsEmpty())
			{
				Problems.Add(Where + TEXT(": no prompt"));
			}
			CheckObjective(Where, L.Steps[S].Objective, Problems);
		}
		CheckObjective(Id + TEXT(" pass"), L.Pass, Problems);
		if (L.Faults.Num() == 0)
		{
			Problems.Add(Id + TEXT(": no fault line"));
		}
		for (const FLessonFault& F : L.Faults)
		{
			const FString Where = Id + TEXT(" fault ") + F.Id.ToString();
			if (F.Feedback.IsEmpty())
			{
				Problems.Add(Where + TEXT(": no feedback"));
			}
			if (!IsReadableMeasure(F.Measure))
			{
				Problems.Add(Where + TEXT(": measure cannot be read"));
			}
			CheckWindow(Where, F.Measure, Problems);
		}
		if (L.Stars.HigherBar.Num() == 0 && L.Setup.Assists.CountOn() == 0)
		{
			Problems.Add(Id + TEXT(": two and three stars cannot be reached (no assists and no higher bar)"));
		}
		if (L.Stars.HigherBar.Num() > 0 && L.Stars.HigherBarText.IsEmpty())
		{
			Problems.Add(Id + TEXT(": higher bar without text"));
		}
		FLessonObjective Higher = L.Pass;
		Higher.Conditions.Append(L.Stars.HigherBar);
		CheckObjective(Id + TEXT(" higher bar"), Higher, Problems);
	}

	for (const FLessonDef& L : Lessons)
	{
		for (const FName& Req : L.Requires)
		{
			if (Req == L.Id)
			{
				Problems.Add(L.Id.ToString() + TEXT(": requires itself"));
			}
			else if (!IndexOf.Contains(Req))
			{
				Problems.Add(L.Id.ToString() + TEXT(": requires missing lesson ") + Req.ToString());
			}
		}
	}

	// Cycles: depth-first search, 0 unvisited, 1 on the stack, 2 done.
	TArray<uint8> State;
	State.SetNumZeroed(Lessons.Num());
	TFunction<bool(int32)> Visit = [&](int32 I) -> bool
	{
		if (State[I] == 1)
		{
			return false;
		}
		if (State[I] == 2)
		{
			return true;
		}
		State[I] = 1;
		for (const FName& Req : Lessons[I].Requires)
		{
			const int32* J = IndexOf.Find(Req);
			if (J && *J != I && !Visit(*J))
			{
				return false;
			}
		}
		State[I] = 2;
		return true;
	};
	for (int32 I = 0; I < Lessons.Num(); ++I)
	{
		if (!Visit(I))
		{
			Problems.Add(Lessons[I].Id.ToString() + TEXT(": prerequisites form a cycle"));
			break;
		}
	}
	return Problems;
}

#undef LOCTEXT_NAMESPACE
