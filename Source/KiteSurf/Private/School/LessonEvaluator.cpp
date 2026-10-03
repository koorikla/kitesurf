#include "School/LessonEvaluator.h"

#include "Tricks/JumpRecord.h"
#include "Tricks/TrickRecognition.h"
#include "Tricks/TrickSignature.h"

#include <limits>

// Named, not anonymous, so a unity build cannot merge these helpers with another file's.
namespace LessonEvalPrivate
{
	constexpr float NoEvent = std::numeric_limits<float>::quiet_NaN();

	const FJumpLoop* FirstCompletedLoop(const FJumpRecord& Jump)
	{
		for (const FJumpLoop& Loop : Jump.Loops)
		{
			if (Loop.Loop.bCompleted)
			{
				return &Loop;
			}
		}
		return nullptr;
	}

	/** The newest tack change already TransitionSettleSeconds old, else the newest at all. */
	bool LastTackChange(const FLessonTelemetry& Telemetry, float& OutTime)
	{
		const TArray<float> Changes = Telemetry.FindTackChanges();
		for (int32 I = Changes.Num() - 1; I >= 0; --I)
		{
			if (Changes[I] + LessonEval::TransitionSettleSeconds <= Telemetry.LatestTime())
			{
				OutTime = Changes[I];
				return true;
			}
		}
		if (Changes.Num() > 0)
		{
			OutTime = Changes.Last();
			return true;
		}
		return false;
	}

	bool AnchorTime(ELessonAnchor Anchor, const FLessonTelemetry& Telemetry, const FJumpRecord* Jump, float EventTime, float& OutTime)
	{
		switch (Anchor)
		{
		case ELessonAnchor::Latest:
			if (Telemetry.IsEmpty())
			{
				return false;
			}
			OutTime = Telemetry.LatestTime();
			return true;
		case ELessonAnchor::Takeoff:
			if (!Jump) { return false; }
			OutTime = Jump->TakeoffTimeSeconds;
			return true;
		case ELessonAnchor::Apex:
			if (!Jump) { return false; }
			OutTime = Jump->ApexTimeSeconds;
			return true;
		case ELessonAnchor::Touchdown:
			if (!Jump) { return false; }
			OutTime = Jump->LandingTimeSeconds;
			return true;
		case ELessonAnchor::Event:
			if (FMath::IsFinite(EventTime))
			{
				OutTime = EventTime;
				return true;
			}
			return LastTackChange(Telemetry, OutTime);
		default:
			return false;
		}
	}

	bool ReadChannel(const FLessonMeasure& M, const FLessonTelemetry& Telemetry, float Anchor, float& Out)
	{
		const float From = Anchor + M.FromSeconds;
		const float To = Anchor + M.ToSeconds;
		float AtFrom = 0.0f;
		float AtTo = 0.0f;
		switch (M.Reduce)
		{
		case ELessonReduce::At:
			return Telemetry.ValueAt(M.Channel, To, Out);
		case ELessonReduce::Change:
			if (!Telemetry.ValueAt(M.Channel, From, AtFrom) || !Telemetry.ValueAt(M.Channel, To, AtTo))
			{
				return false;
			}
			Out = AtTo - AtFrom;
			return true;
		default:
			break;
		}

		const FLessonWindowStats Stats = Telemetry.Window(M.Channel, From, To);
		if (!Stats.IsValid())
		{
			return false;
		}
		switch (M.Reduce)
		{
		case ELessonReduce::Min:   Out = Stats.Min; return true;
		case ELessonReduce::Max:   Out = Stats.Max; return true;
		case ELessonReduce::Mean:  Out = Stats.Mean; return true;
		case ELessonReduce::Range: Out = Stats.Max - Stats.Min; return true;
		case ELessonReduce::Rise:
			if (!Telemetry.ValueAt(M.Channel, From, AtFrom)) { return false; }
			Out = Stats.Max - AtFrom;
			return true;
		case ELessonReduce::Drop:
			if (!Telemetry.ValueAt(M.Channel, From, AtFrom)) { return false; }
			Out = AtFrom - Stats.Min;
			return true;
		default:
			return false;
		}
	}

	/** The kite crossing 12 (clock sign change) nearest to Time within the search window. */
	bool NearestKiteCrossing(const FLessonTelemetry& Telemetry, float Time, float& OutCrossing)
	{
		const float From = Time - LessonEval::KiteCrossingSearchSeconds;
		const float To = Time + LessonEval::KiteCrossingSearchSeconds;
		bool bFound = false;
		for (int32 I = 1; I < Telemetry.Num(); ++I)
		{
			const FLessonSample& A = Telemetry.Get(I - 1);
			const FLessonSample& B = Telemetry.Get(I);
			if (B.TimeSeconds < From || A.TimeSeconds > To)
			{
				continue;
			}
			const bool bCrosses = (A.KiteClockDeg < 0.0f && B.KiteClockDeg >= 0.0f) || (A.KiteClockDeg > 0.0f && B.KiteClockDeg <= 0.0f);
			if (!bCrosses)
			{
				continue;
			}
			const float Span = B.KiteClockDeg - A.KiteClockDeg;
			const float Alpha = FMath::Abs(Span) > UE_SMALL_NUMBER ? -A.KiteClockDeg / Span : 0.0f;
			const float Crossing = FMath::Lerp(A.TimeSeconds, B.TimeSeconds, FMath::Clamp(Alpha, 0.0f, 1.0f));
			if (!bFound || FMath::Abs(Crossing - Time) < FMath::Abs(OutCrossing - Time))
			{
				OutCrossing = Crossing;
				bFound = true;
			}
		}
		return bFound;
	}

	bool ReadTransition(ELessonMetric Metric, const FLessonTelemetry& Telemetry, float ChangeTime, float& Out)
	{
		const float Settle = LessonEval::TransitionSettleSeconds;
		switch (Metric)
		{
		case ELessonMetric::Transition:
			Out = 1.0f;
			return true;
		case ELessonMetric::TransitionSpeedKept:
		{
			float Before = 0.0f;
			float After = 0.0f;
			if (!Telemetry.ValueAt(ELessonChannel::Speed, ChangeTime + Settle, After))
			{
				return false;
			}
			if (!Telemetry.ValueAt(ELessonChannel::Speed, ChangeTime - Settle, Before))
			{
				// The change came soon after the buffer starts: the oldest speed is the best "before".
				if (Telemetry.IsEmpty() || Telemetry.OldestTime() > ChangeTime)
				{
					return false;
				}
				Before = Telemetry.Get(0).SpeedMS;
			}
			Out = After / FMath::Max(Before, 0.1f);
			return true;
		}
		case ELessonMetric::TransitionNotPlaningSeconds:
		{
			if (Telemetry.IsEmpty() || ChangeTime + Settle > Telemetry.LatestTime())
			{
				return false;
			}
			const float From = FMath::Max(ChangeTime - Settle, Telemetry.OldestTime());
			const float To = ChangeTime + Settle;
			const float Planing = Telemetry.TimeInBand(ELessonChannel::Planing, 0.5f, 1.5f, From, To);
			Out = FMath::Max((To - From) - Planing, 0.0f);
			return true;
		}
		case ELessonMetric::KiteLeadAtTransition:
		{
			float Crossing = 0.0f;
			if (!NearestKiteCrossing(Telemetry, ChangeTime, Crossing))
			{
				return false;
			}
			Out = ChangeTime - Crossing;
			return true;
		}
		default:
			return false;
		}
	}

	bool ReadDiveBeforeTouchdown(const FLessonTelemetry& Telemetry, const FJumpRecord& Jump, float& Out)
	{
		const float Takeoff = Jump.TakeoffTimeSeconds;
		const float Landing = Jump.LandingTimeSeconds;
		const FLessonWindowStats Stats = Telemetry.Window(ELessonChannel::KiteElevation, Takeoff, Landing);
		if (!Stats.IsValid() || Landing <= Takeoff)
		{
			return false;
		}
		const float Top = Stats.Max - LessonEval::DiveStartDropDeg;
		float Value = 0.0f;
		if (Telemetry.ValueAt(ELessonChannel::KiteElevation, Landing, Value) && Value >= Top)
		{
			Out = 0.0f;
			return true;
		}
		for (int32 I = Telemetry.FindIndexAtOrBefore(Landing); I >= 0 && Telemetry.Get(I).TimeSeconds >= Takeoff; --I)
		{
			if (Telemetry.Get(I).KiteElevationDeg >= Top)
			{
				Out = Landing - Telemetry.Get(I).TimeSeconds;
				return true;
			}
		}
		Out = Landing - Takeoff;
		return true;
	}

	bool ReadJump(const FLessonMeasure& M, const FLessonTelemetry& Telemetry, const FJumpRecord& J, const FLessonJumpExtras& X, float& Out)
	{
		switch (M.Metric)
		{
		case ELessonMetric::JumpHeight:      Out = J.ApexHeightCm / 100.0f; return true;
		case ELessonMetric::Airtime:         Out = J.AirtimeSeconds; return true;
		case ELessonMetric::JumpDistance:    Out = J.DistanceCm / 100.0f; return true;
		case ELessonMetric::TakeoffSpeed:    Out = J.TakeoffSpeedCmS / 100.0f; return true;
		case ELessonMetric::LandingGrade:
			Out = static_cast<float>(static_cast<int32>(J.Outcome == EJumpOutcome::Crashed ? ELandingGrade::Crash : J.Grade));
			return true;
		case ELessonMetric::LandingCause:    Out = static_cast<float>(static_cast<int32>(X.LandingCause)); return true;
		case ELessonMetric::SinkAtTouchdown: Out = J.SinkRateCmS / 100.0f; return true;
		case ELessonMetric::LandingG:        Out = J.LandingG; return true;
		case ELessonMetric::KiteElevationAtTouchdown: Out = J.KiteElevationAtLandingDeg; return true;
		case ELessonMetric::MinKiteElevationInAir:    Out = J.MinKiteElevationDeg; return true;
		case ELessonMetric::Popped:          Out = J.bPopped ? 1.0f : 0.0f; return true;
		case ELessonMetric::CompletedLoops:  Out = static_cast<float>(J.CountCompletedLoops()); return true;
		case ELessonMetric::LoopKind:
		{
			Out = 0.0f;
			for (const FTrickLoop& Loop : TrickRecognition::ClassifyLoops(J.Loops))
			{
				if (Loop.Kind == M.LoopKind)
				{
					Out = 1.0f;
				}
			}
			return true;
		}
		case ELessonMetric::LoopStartSinceApex:
		{
			const FJumpLoop* Loop = FirstCompletedLoop(J);
			if (!Loop) { return false; }
			Out = Loop->StartSinceApexSeconds;
			return true;
		}
		case ELessonMetric::LoopDuration:
		{
			const FJumpLoop* Loop = FirstCompletedLoop(J);
			if (!Loop) { return false; }
			Out = Loop->Loop.DurationSeconds;
			return true;
		}
		case ELessonMetric::TrickNameContains:
			Out = (!M.Text.IsEmpty() && J.TrickName.Contains(M.Text, ESearchCase::IgnoreCase)) ? 1.0f : 0.0f;
			return true;
		case ELessonMetric::RotationDeg:
			if (!X.bHasRotation) { return false; }
			Out = X.RotationDeg;
			return true;
		case ELessonMetric::GrabHoldSeconds:
			if (!X.bHasGrab) { return false; }
			Out = X.GrabHoldSeconds;
			return true;
		case ELessonMetric::DiveBeforeTouchdown:
			return ReadDiveBeforeTouchdown(Telemetry, J, Out);
		case ELessonMetric::HeadingChange:
		{
			float Before = 0.0f;
			float After = 0.0f;
			if (!Telemetry.ValueAt(ELessonChannel::Heading, J.TakeoffTimeSeconds - LessonEval::HeadingBeforeTakeoffSeconds, Before)
				&& !Telemetry.ValueAt(ELessonChannel::Heading, J.TakeoffTimeSeconds, Before))
			{
				return false;
			}
			if (!Telemetry.ValueAt(ELessonChannel::Heading, J.LandingTimeSeconds, After))
			{
				return false;
			}
			Out = FMath::Abs(FMath::FindDeltaAngleDegrees(Before, After));
			return true;
		}
		default:
			return false;
		}
	}

	int32 TackAt(const FLessonTelemetry& Telemetry, float Time)
	{
		const int32 I = Telemetry.FindIndexAtOrBefore(Time);
		return I == INDEX_NONE ? 0 : FMath::Sign(Telemetry.Get(I).Tack);
	}

	bool InRange(float Value, float Min, float Max)
	{
		return FMath::IsFinite(Value) && Value >= Min && Value <= Max;
	}

	/** Counts one judged event into the result's progress. */
	void CountEvent(const FLessonObjective& Objective, FObjectiveResult& R, bool bQualifies, int32 Tack, float Value)
	{
		FLessonProgress& P = R.NewProgress;
		++P.Attempts;
		R.bQualifies = bQualifies;
		R.Value = Value;
		if (bQualifies)
		{
			const bool bSameTack = Objective.bEachTack && P.LastCountedTack != 0 && Tack == P.LastCountedTack;
			if (!bSameTack)
			{
				++P.Count;
				P.LastCountedTack = Tack;
				R.bCounted = true;
			}
		}
		else
		{
			++P.Failures;
			R.bFailed = true;
			if (Objective.bInARow)
			{
				P.Count = 0;
			}
		}
	}

	/** Channel conditions only, read on one sample (Held and DistanceRidden). */
	bool SampleConditionsHold(const TArray<FLessonCondition>& Conditions, const FLessonTelemetry& Telemetry, int32 Index)
	{
		for (const FLessonCondition& C : Conditions)
		{
			if (C.Measure.Metric == ELessonMetric::Channel && !InRange(Telemetry.ChannelAt(Index, C.Measure.Channel), C.Min, C.Max))
			{
				return false;
			}
		}
		return true;
	}

	/** Where upwind gain and distance start: the reference, the step start, or with bEachTack the last tack change after them. */
	float RideBaseTime(const FLessonObjective& Objective, const FLessonProgress& P, const FLessonTelemetry& Telemetry)
	{
		float Base = FMath::Max(P.ReferenceTimeSeconds, P.StepStartTimeSeconds);
		if (Objective.bEachTack)
		{
			const TArray<float> Changes = Telemetry.FindTackChanges();
			if (Changes.Num() > 0 && Changes.Last() > Base)
			{
				Base = Changes.Last();
			}
		}
		return Base;
	}

	void EvaluateRide(const FLessonObjective& Objective, const FLessonTelemetry& Telemetry, const FJumpRecord* Jump,
		const FLessonJumpExtras& Extras, FObjectiveResult& R)
	{
		FLessonProgress& P = R.NewProgress;
		if (Telemetry.IsEmpty())
		{
			return;
		}
		const FLessonSample& Latest = Telemetry.Latest();
		const float Start = P.StepStartTimeSeconds;

		switch (Objective.Metric)
		{
		case ELessonMetric::TimeToPlaning:
		{
			int32 First = INDEX_NONE;
			for (int32 I = 0; I < Telemetry.Num(); ++I)
			{
				const FLessonSample& S = Telemetry.Get(I);
				if (S.TimeSeconds >= Start && S.BoardState == EBoardState::Planing)
				{
					First = I;
					break;
				}
			}
			const float Deadline = Start + Objective.Max;
			const bool bMissRecorded = P.LastEventTimeSeconds >= Deadline;
			if (First != INDEX_NONE)
			{
				const float T = Telemetry.Get(First).TimeSeconds;
				const float Value = T - Start;
				R.Value = Value;
				if (T > P.LastEventTimeSeconds && !(Value > Objective.Max && bMissRecorded))
				{
					CountEvent(Objective, R, InRange(Value, Objective.Min, Objective.Max), Telemetry.Get(First).Tack, Value);
					P.LastEventTimeSeconds = T;
				}
			}
			else
			{
				R.Value = Latest.TimeSeconds - Start;
				if (Latest.TimeSeconds > Deadline && !bMissRecorded)
				{
					CountEvent(Objective, R, false, Latest.Tack, R.Value);
					P.LastEventTimeSeconds = Deadline;
				}
			}
			break;
		}

		case ELessonMetric::UpwindGain:
		case ELessonMetric::DistanceRidden:
		{
			const float Base = RideBaseTime(Objective, P, Telemetry);
			float Value = 0.0f;
			if (Objective.Metric == ELessonMetric::UpwindGain)
			{
				float BaseUpwind = 0.0f;
				if (!Telemetry.ValueAt(ELessonChannel::Upwind, Base, BaseUpwind))
				{
					BaseUpwind = Base > P.ReferenceTimeSeconds ? Telemetry.Get(0).UpwindM : P.ReferenceUpwindM;
				}
				Value = Latest.UpwindM - BaseUpwind;
			}
			else
			{
				for (int32 I = 0; I + 1 < Telemetry.Num(); ++I)
				{
					const FLessonSample& S = Telemetry.Get(I);
					if (S.TimeSeconds >= Base && S.BoardState == EBoardState::Planing && SampleConditionsHold(Objective.Conditions, Telemetry, I))
					{
						Value += S.SpeedMS * (Telemetry.Get(I + 1).TimeSeconds - S.TimeSeconds);
					}
				}
			}
			R.Value = Value;
			const bool bReady = Value >= Objective.Min && Latest.BoardState == EBoardState::Planing
				&& (Objective.Metric == ELessonMetric::DistanceRidden
					|| LessonEval::ConditionsHold(Objective.Conditions, Telemetry, Jump, Extras, Latest.TimeSeconds));
			if (bReady && Latest.TimeSeconds > P.LastEventTimeSeconds)
			{
				CountEvent(Objective, R, true, Latest.Tack, Value);
				P.LastEventTimeSeconds = Latest.TimeSeconds;
				P.ReferenceTimeSeconds = Latest.TimeSeconds;
				P.ReferenceUpwindM = Latest.UpwindM;
			}
			break;
		}

		case ELessonMetric::KiteDives:
		{
			const float ReturnDeg = Objective.Max;
			const float FloorDeg = Objective.Min;
			const float DeepEnough = ReturnDeg - LessonEval::DiveMinDropDeg;
			const float Limit = Objective.WindowSeconds;
			bool bInDive = false;
			float DiveStart = 0.0f;
			float Depth = 0.0f;
			auto Judge = [&](float DiveEnd, bool bComplete)
			{
				if (DiveStart <= P.LastEventTimeSeconds || Depth > DeepEnough)
				{
					return;
				}
				const float Duration = DiveEnd - DiveStart;
				const bool bTooDeep = Depth < FloorDeg;
				const bool bTooSlow = Limit > 0.0f && Duration > Limit;
				if (bComplete || bTooDeep || bTooSlow)
				{
					// Conditions read with the dive's start as the Event anchor.
					const bool bQualifies = !bTooDeep && !bTooSlow
						&& LessonEval::ConditionsHold(Objective.Conditions, Telemetry, Jump, Extras, DiveStart);
					CountEvent(Objective, R, bQualifies, TackAt(Telemetry, DiveStart), Depth);
					P.LastEventTimeSeconds = DiveStart;
				}
			};
			for (int32 I = 0; I < Telemetry.Num(); ++I)
			{
				const FLessonSample& S = Telemetry.Get(I);
				if (S.TimeSeconds < Start)
				{
					continue;
				}
				const float E = S.KiteElevationDeg;
				if (!bInDive)
				{
					if (E < ReturnDeg)
					{
						bInDive = true;
						DiveStart = S.TimeSeconds;
						Depth = E;
					}
				}
				else
				{
					Depth = FMath::Min(Depth, E);
					if (E >= ReturnDeg)
					{
						Judge(S.TimeSeconds, true);
						bInDive = false;
					}
				}
			}
			if (bInDive)
			{
				Judge(Latest.TimeSeconds, false);
			}
			R.Value = Latest.KiteElevationDeg;
			break;
		}

		case ELessonMetric::Transition:
		case ELessonMetric::TransitionSpeedKept:
		case ELessonMetric::TransitionNotPlaningSeconds:
		case ELessonMetric::KiteLeadAtTransition:
		{
			for (const float Change : Telemetry.FindTackChanges())
			{
				if (Change < Start || Change <= P.LastEventTimeSeconds || Change + LessonEval::TransitionSettleSeconds > Latest.TimeSeconds)
				{
					continue;
				}
				float Value = 0.0f;
				const bool bRead = ReadTransition(Objective.Metric, Telemetry, Change, Value);
				const bool bQualifies = bRead && InRange(Value, Objective.Min, Objective.Max)
					&& LessonEval::ConditionsHold(Objective.Conditions, Telemetry, Jump, Extras, Change);
				CountEvent(Objective, R, bQualifies, TackAt(Telemetry, Change), bRead ? Value : 0.0f);
				P.LastEventTimeSeconds = Change;
			}
			break;
		}

		default:
			break;
		}
	}
}

ELessonMetricSource LessonEval::GetMetricSource(ELessonMetric Metric)
{
	switch (Metric)
	{
	case ELessonMetric::None:
		return ELessonMetricSource::None;
	case ELessonMetric::JumpHeight:
	case ELessonMetric::Airtime:
	case ELessonMetric::JumpDistance:
	case ELessonMetric::TakeoffSpeed:
	case ELessonMetric::LandingGrade:
	case ELessonMetric::LandingCause:
	case ELessonMetric::SinkAtTouchdown:
	case ELessonMetric::LandingG:
	case ELessonMetric::KiteElevationAtTouchdown:
	case ELessonMetric::MinKiteElevationInAir:
	case ELessonMetric::Popped:
	case ELessonMetric::CompletedLoops:
	case ELessonMetric::LoopKind:
	case ELessonMetric::LoopStartSinceApex:
	case ELessonMetric::LoopDuration:
	case ELessonMetric::TrickNameContains:
	case ELessonMetric::RotationDeg:
	case ELessonMetric::GrabHoldSeconds:
	case ELessonMetric::DiveBeforeTouchdown:
	case ELessonMetric::HeadingChange:
		return ELessonMetricSource::Jump;
	case ELessonMetric::Channel:
		return ELessonMetricSource::Channel;
	case ELessonMetric::SpeedHeld:
	case ELessonMetric::KiteElevationHeld:
	case ELessonMetric::ChannelHeld:
		return ELessonMetricSource::Held;
	case ELessonMetric::TimeToPlaning:
	case ELessonMetric::UpwindGain:
	case ELessonMetric::DistanceRidden:
	case ELessonMetric::KiteDives:
	case ELessonMetric::Transition:
	case ELessonMetric::TransitionSpeedKept:
	case ELessonMetric::TransitionNotPlaningSeconds:
	case ELessonMetric::KiteLeadAtTransition:
		return ELessonMetricSource::Ride;
	default:
		return ELessonMetricSource::None;
	}
}

ELessonChannel LessonEval::HeldChannel(const FLessonObjective& Objective)
{
	switch (Objective.Metric)
	{
	case ELessonMetric::SpeedHeld:         return ELessonChannel::Speed;
	case ELessonMetric::KiteElevationHeld: return ELessonChannel::KiteElevation;
	default:                               return Objective.Channel;
	}
}

bool LessonEval::ReadMeasure(const FLessonMeasure& Measure, const FLessonTelemetry& Telemetry, const FJumpRecord* Jump,
	const FLessonJumpExtras& Extras, float EventTimeSeconds, float& OutValue)
{
	using namespace LessonEvalPrivate;
	float Value = 0.0f;
	bool bRead = false;
	switch (GetMetricSource(Measure.Metric))
	{
	case ELessonMetricSource::Jump:
		bRead = Jump && ReadJump(Measure, Telemetry, *Jump, Extras, Value);
		break;
	case ELessonMetricSource::Channel:
	{
		float Anchor = 0.0f;
		bRead = AnchorTime(Measure.Anchor, Telemetry, Jump, EventTimeSeconds, Anchor) && ReadChannel(Measure, Telemetry, Anchor, Value);
		break;
	}
	case ELessonMetricSource::Ride:
	{
		float Change = 0.0f;
		bRead = AnchorTime(ELessonAnchor::Event, Telemetry, Jump, EventTimeSeconds, Change)
			&& ReadTransition(Measure.Metric, Telemetry, Change, Value);
		break;
	}
	default:
		break;
	}
	if (!bRead || !FMath::IsFinite(Value))
	{
		return false;
	}
	OutValue = Value;
	return true;
}

bool LessonEval::Compare(ELessonCompare Compare, float Value, float Threshold)
{
	switch (Compare)
	{
	case ELessonCompare::Less:           return Value < Threshold;
	case ELessonCompare::LessOrEqual:    return Value <= Threshold;
	case ELessonCompare::Greater:        return Value > Threshold;
	case ELessonCompare::GreaterOrEqual: return Value >= Threshold;
	case ELessonCompare::Equal:          return FMath::Abs(Value - Threshold) <= 0.001f;
	case ELessonCompare::NotEqual:       return FMath::Abs(Value - Threshold) > 0.001f;
	default:                             return false;
	}
}

bool LessonEval::ConditionsHold(const TArray<FLessonCondition>& Conditions, const FLessonTelemetry& Telemetry, const FJumpRecord* Jump,
	const FLessonJumpExtras& Extras, float EventTimeSeconds)
{
	for (const FLessonCondition& C : Conditions)
	{
		float Value = 0.0f;
		if (!ReadMeasure(C.Measure, Telemetry, Jump, Extras, EventTimeSeconds, Value) || !LessonEvalPrivate::InRange(Value, C.Min, C.Max))
		{
			return false;
		}
	}
	return true;
}

FLessonProgress LessonEval::BeginStep(const FLessonTelemetry& Telemetry, int32 LastJumpIndex)
{
	FLessonProgress P;
	P.LastJumpIndex = LastJumpIndex;
	if (!Telemetry.IsEmpty())
	{
		// Ride events are filtered by the step start, so LastEventTimeSeconds stays unset: a rider
		// already planing at the start plans in 0 s.
		P.StepStartTimeSeconds = Telemetry.LatestTime();
		P.ReferenceTimeSeconds = Telemetry.LatestTime();
		P.ReferenceUpwindM = Telemetry.Latest().UpwindM;
	}
	return P;
}

FObjectiveResult LessonEval::EvaluateObjective(const FLessonObjective& Objective, const FLessonProgress& Progress,
	const FLessonTelemetry& Telemetry, const FJumpRecord* LastJump, const FLessonJumpExtras& Extras)
{
	using namespace LessonEvalPrivate;
	FObjectiveResult R;
	R.NewProgress = Progress;
	FLessonProgress& P = R.NewProgress;
	const int32 Needed = FMath::Max(Objective.Count, 1);

	switch (GetMetricSource(Objective.Metric))
	{
	case ELessonMetricSource::Held:
	{
		if (Telemetry.IsEmpty())
		{
			return R;
		}
		const ELessonChannel HeldCh = HeldChannel(Objective);
		auto Test = [&](int32 I)
		{
			return InRange(Telemetry.ChannelAt(I, HeldCh), Objective.Min, Objective.Max)
				&& SampleConditionsHold(Objective.Conditions, Telemetry, I);
		};
		const float Held = Telemetry.TrailingTimeWhere(Test, P.StepStartTimeSeconds);
		R.Value = Telemetry.ChannelAt(Telemetry.Num() - 1, HeldCh);
		R.bQualifies = Test(Telemetry.Num() - 1);
		if (Objective.WindowSeconds > 0.0f)
		{
			R.Progress = FMath::Clamp(Held / Objective.WindowSeconds, 0.0f, 1.0f);
			R.bPassed = Held >= Objective.WindowSeconds;
		}
		else
		{
			R.Progress = R.bQualifies ? 1.0f : 0.0f;
			R.bPassed = R.bQualifies;
		}
		return R;
	}

	case ELessonMetricSource::Channel:
	{
		float Value = 0.0f;
		const bool bRead = ReadMeasure(Objective.PrimaryMeasure(), Telemetry, LastJump, Extras, NoEvent, Value);
		R.Value = bRead ? Value : 0.0f;
		R.bQualifies = bRead && InRange(Value, Objective.Min, Objective.Max)
			&& ConditionsHold(Objective.Conditions, Telemetry, LastJump, Extras, NoEvent);
		R.bPassed = R.bQualifies;
		R.Progress = R.bPassed ? 1.0f : 0.0f;
		return R;
	}

	case ELessonMetricSource::Jump:
	{
		if (LastJump)
		{
			float Value = 0.0f;
			const bool bRead = ReadMeasure(Objective.PrimaryMeasure(), Telemetry, LastJump, Extras, NoEvent, Value);
			R.Value = bRead ? Value : 0.0f;
			if (LastJump->Index > P.LastJumpIndex)
			{
				const bool bQualifies = bRead && InRange(Value, Objective.Min, Objective.Max)
					&& ConditionsHold(Objective.Conditions, Telemetry, LastJump, Extras, LastJump->LandingTimeSeconds);
				CountEvent(Objective, R, bQualifies, TackAt(Telemetry, LastJump->TakeoffTimeSeconds), R.Value);
				P.LastJumpIndex = LastJump->Index;
			}
		}
		break;
	}

	case ELessonMetricSource::Ride:
		EvaluateRide(Objective, Telemetry, LastJump, Extras, R);
		break;

	default:
		return R;
	}

	R.bPassed = P.Count >= Needed;
	float Partial = 0.0f;
	if (!R.bPassed && (Objective.Metric == ELessonMetric::UpwindGain || Objective.Metric == ELessonMetric::DistanceRidden)
		&& Objective.Min > 0.0f)
	{
		Partial = FMath::Clamp(R.Value / Objective.Min, 0.0f, 1.0f);
		if (R.bCounted)
		{
			Partial = 0.0f; // the reference just moved: the next gain starts from zero
		}
	}
	R.Progress = FMath::Clamp((static_cast<float>(P.Count) + Partial) / static_cast<float>(Needed), 0.0f, 1.0f);
	return R;
}

void LessonEval::ApplyResult(FLessonProgress& Progress, const FObjectiveResult& Result)
{
	Progress = Result.NewProgress;
}

const FLessonFault* LessonEval::DiagnoseFault(const TArray<FLessonFault>& Faults, const FLessonTelemetry& Telemetry,
	const FJumpRecord& Jump, const FLessonJumpExtras& Extras)
{
	const FLessonFault* Best = nullptr;
	for (const FLessonFault& Fault : Faults)
	{
		float Value = 0.0f;
		if (!ReadMeasure(Fault.Measure, Telemetry, &Jump, Extras, LessonEvalPrivate::NoEvent, Value)
			|| !Compare(Fault.Compare, Value, Fault.Threshold))
		{
			continue;
		}
		if (!Best || Fault.Priority > Best->Priority)
		{
			Best = &Fault;
		}
	}
	return Best;
}

FLessonObjective LessonEval::HigherBarObjective(const FLessonDef& Lesson)
{
	FLessonObjective Objective = Lesson.Pass;
	Objective.Conditions.Append(Lesson.Stars.HigherBar);
	return Objective;
}

int32 LessonEval::ComputeStars(const FStarRules& Rules, const FLessonAssists& Default, const FLessonAssists& Used,
	bool bPassed, bool bPassedHigherBar)
{
	if (!bPassed)
	{
		return 0;
	}
	const bool bHigherBar = bPassedHigherBar && Rules.HigherBar.Num() > 0;
	const int32 DefaultOn = Default.CountOn();
	const int32 UsedOn = Used.CountOn();
	if (UsedOn == 0 && (DefaultOn > 0 || bHigherBar))
	{
		return 3;
	}
	if (UsedOn < DefaultOn || bHigherBar)
	{
		return 2;
	}
	return 1;
}

FLessonJumpExtras LessonEval::ExtrasFromSignature(const FTrickSignature& Signature, ELandingCause Cause)
{
	FLessonJumpExtras X;
	X.LandingCause = Cause;
	X.bHasRotation = true;
	X.RotationDeg = 360.0f * Signature.Inversions.Num() + 180.0f * Signature.SpinHalfTurns;
	X.bHasGrab = true;
	for (const FTrickGrab& Grab : Signature.Grabs)
	{
		X.GrabHoldSeconds = FMath::Max(X.GrabHoldSeconds, Grab.HoldSeconds);
	}
	return X;
}
