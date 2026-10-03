#include "School/LessonTiming.h"

#include "Tricks/JumpRecord.h"

#include <limits>

FString LessonTiming::GradeText(ELessonTimingGrade Grade)
{
	switch (Grade)
	{
	case ELessonTimingGrade::Early:   return TEXT("EARLY");
	case ELessonTimingGrade::Good:    return TEXT("GOOD");
	case ELessonTimingGrade::Perfect: return TEXT("PERFECT");
	case ELessonTimingGrade::Late:    return TEXT("LATE");
	default:                          return FString();
	}
}

bool LessonTiming::IsKiteAtTop(float KiteElevationDeg, float ClimbRateDegS, float TopDeg)
{
	return KiteElevationDeg >= TopDeg || (KiteElevationDeg >= TopDeg - SlowMoTopBandDeg && ClimbRateDegS <= SlowMoTopRateDegS);
}

ELessonTimingGrade LessonTiming::GradeSheetIn(float KiteElevationDeg, float SecondsAtTop, float ClimbRateDegS)
{
	if (KiteElevationDeg < SheetGoodMinDeg)
	{
		return ELessonTimingGrade::Early;
	}
	if (!IsKiteAtTop(KiteElevationDeg, ClimbRateDegS))
	{
		return ELessonTimingGrade::Good;
	}
	if (SecondsAtTop > SheetLateWaitSeconds)
	{
		return ELessonTimingGrade::Late;
	}
	return SecondsAtTop > SheetPerfectWaitSeconds ? ELessonTimingGrade::Good : ELessonTimingGrade::Perfect;
}

ELessonTimingGrade LessonTiming::GradeInBand(float Value, float Min, float Max, bool bHighIsEarly)
{
	if (!FMath::IsFinite(Value))
	{
		return ELessonTimingGrade::None;
	}
	if (Value < Min)
	{
		return bHighIsEarly ? ELessonTimingGrade::Late : ELessonTimingGrade::Early;
	}
	if (Value > Max)
	{
		return bHighIsEarly ? ELessonTimingGrade::Early : ELessonTimingGrade::Late;
	}
	const float Quarter = 0.25f * (Max - Min);
	return Value >= Min + Quarter && Value <= Max - Quarter ? ELessonTimingGrade::Perfect : ELessonTimingGrade::Good;
}

ELessonTimingGrade LessonTiming::GradeOffset(float OffsetSeconds, float PerfectSeconds, float GoodSeconds)
{
	if (!FMath::IsFinite(OffsetSeconds))
	{
		return ELessonTimingGrade::None;
	}
	const float Abs = FMath::Abs(OffsetSeconds);
	if (Abs <= PerfectSeconds)
	{
		return ELessonTimingGrade::Perfect;
	}
	if (Abs <= GoodSeconds)
	{
		return ELessonTimingGrade::Good;
	}
	return OffsetSeconds < 0.0f ? ELessonTimingGrade::Early : ELessonTimingGrade::Late;
}

bool LessonTiming::DetectSheetIn(const FLessonTelemetry& Telemetry, ELessonTimingGrade& OutGrade)
{
	OutGrade = ELessonTimingGrade::None;
	const int32 N = Telemetry.Num();
	if (N < 2)
	{
		return false;
	}
	const FLessonSample& Prev = Telemetry.Get(N - 2);
	const FLessonSample& Now = Telemetry.Get(N - 1);
	if (!(Prev.BarPosition < SheetInBar && Now.BarPosition >= SheetInBar) || Now.BoardState == EBoardState::Airborne
		|| Now.KiteElevationDeg < SheetMinKiteDeg)
	{
		return false;
	}
	// How long the kite has been at the top without a break, up to now.
	const float AtTop = Telemetry.TrailingTimeWhere([&Telemetry](int32 I)
	{
		return IsKiteAtTop(Telemetry.Get(I).KiteElevationDeg, Telemetry.ChannelAt(I, ELessonChannel::KiteClimbRate));
	});
	OutGrade = GradeSheetIn(Now.KiteElevationDeg, AtTop, Telemetry.ChannelAt(N - 1, ELessonChannel::KiteClimbRate));
	return true;
}

bool LessonTiming::IsSheetInStep(const FLessonStep& Step)
{
	return Step.InputGlyph == FName(TEXT("IA_Sheet"));
}

ELessonTimingGrade LessonTiming::GradeAttempt(const FLessonStep& Step, const FLessonObjective& Objective, const FLessonTelemetry& Telemetry,
	const FJumpRecord* Jump, const FLessonJumpExtras& Extras)
{
	const float NoEvent = std::numeric_limits<float>::quiet_NaN();
	switch (Objective.Metric)
	{
	case ELessonMetric::DiveBeforeTouchdown:
	{
		// The landing dive's lead before touchdown against the objective's band: a long lead dived early.
		FLessonMeasure M;
		M.Metric = ELessonMetric::DiveBeforeTouchdown;
		float Lead = 0.0f;
		if (!Jump || !LessonEval::ReadMeasure(M, Telemetry, Jump, Extras, NoEvent, Lead))
		{
			return ELessonTimingGrade::None;
		}
		const float Min = Objective.Min > -UE_BIG_NUMBER * 0.5f ? Objective.Min : 0.0f;
		const float Max = Objective.Max < UE_BIG_NUMBER * 0.5f ? Objective.Max : 1.0f;
		return GradeInBand(Lead, Min, Max, true);
	}
	case ELessonMetric::Transition:
	case ELessonMetric::TransitionSpeedKept:
	case ELessonMetric::TransitionNotPlaningSeconds:
	case ELessonMetric::KiteLeadAtTransition:
	{
		// The kite crossing 12 against the board's change of tack (the newest one).
		FLessonMeasure M;
		M.Metric = ELessonMetric::KiteLeadAtTransition;
		float Lead = 0.0f;
		if (!LessonEval::ReadMeasure(M, Telemetry, Jump, Extras, NoEvent, Lead))
		{
			return ELessonTimingGrade::None;
		}
		return GradeInBand(Lead, 0.0f, KiteLeadMaxSeconds, true);
	}
	case ELessonMetric::LoopStartSinceApex:
	{
		FLessonMeasure M;
		M.Metric = ELessonMetric::LoopStartSinceApex;
		float Since = 0.0f;
		if (!Jump || !LessonEval::ReadMeasure(M, Telemetry, Jump, Extras, NoEvent, Since))
		{
			return ELessonTimingGrade::None;
		}
		return GradeOffset(Since, LoopPerfectSeconds, LoopGoodSeconds);
	}
	default:
		break;
	}

	// A stomp: a jump step on the jump button. Take-off against the peak line load in the second before it.
	if (Jump && Step.InputGlyph == FName(TEXT("IA_Jump")) && LessonEval::GetMetricSource(Objective.Metric) == ELessonMetricSource::Jump)
	{
		const float Takeoff = Jump->TakeoffTimeSeconds;
		float PeakTime = 0.0f;
		float PeakTension = -1.0f;
		for (int32 I = 0; I < Telemetry.Num(); ++I)
		{
			const FLessonSample& S = Telemetry.Get(I);
			if (S.TimeSeconds >= Takeoff - 1.0f && S.TimeSeconds <= Takeoff && S.TensionN > PeakTension)
			{
				PeakTension = S.TensionN;
				PeakTime = S.TimeSeconds;
			}
		}
		if (PeakTension < 0.0f)
		{
			return ELessonTimingGrade::None;
		}
		return GradeOffset(Takeoff - PeakTime, StompPerfectSeconds, StompGoodSeconds);
	}
	return ELessonTimingGrade::None;
}

float LessonTiming::SlowMoTotalSeconds()
{
	return SlowMoRampInSeconds + SlowMoHoldSeconds + SlowMoRampOutSeconds;
}

float LessonTiming::SlowMoReleaseSeconds()
{
	return SlowMoRampInSeconds + SlowMoHoldSeconds;
}

float LessonTiming::SlowMoDilationAt(float RealSeconds)
{
	if (!(RealSeconds > 0.0f) || RealSeconds >= SlowMoTotalSeconds())
	{
		return 1.0f;
	}
	auto Smooth = [](float X) { X = FMath::Clamp(X, 0.0f, 1.0f); return X * X * (3.0f - 2.0f * X); };
	float Depth = 1.0f; // 0 at real time, 1 at SlowMoDilation
	if (RealSeconds < SlowMoRampInSeconds)
	{
		Depth = Smooth(RealSeconds / FMath::Max(SlowMoRampInSeconds, KINDA_SMALL_NUMBER));
	}
	else if (RealSeconds > SlowMoReleaseSeconds())
	{
		Depth = 1.0f - Smooth((RealSeconds - SlowMoReleaseSeconds()) / FMath::Max(SlowMoRampOutSeconds, KINDA_SMALL_NUMBER));
	}
	return FMath::Lerp(1.0f, SlowMoDilation, Depth);
}

bool LessonTiming::DetectSlowMoCue(const FLessonSlowMoCue& Cue, const FLessonSample& Sample, FSlowMoArm& Arm)
{
	switch (Cue.Trigger)
	{
	case ELessonSlowMoTrigger::KiteAtTop:
	{
		const float Elevation = Sample.KiteElevationDeg;
		const float Dt = Sample.TimeSeconds - Arm.LastTime;
		const bool bStopped = Arm.bHasLast && Dt > KINDA_SMALL_NUMBER && (Elevation - Arm.LastKiteDeg) / Dt <= SlowMoTopRateDegS;
		Arm.bHasLast = true;
		Arm.LastKiteDeg = Elevation;
		Arm.LastTime = Sample.TimeSeconds;
		if (Elevation < Cue.Threshold - SlowMoRearmDeg)
		{
			Arm.bArmed = true;
		}
		const bool bOnWater = Sample.BoardState != EBoardState::Airborne;
		const bool bAtTop = IsKiteAtTop(Elevation, bStopped ? 0.0f : UE_BIG_NUMBER, Cue.Threshold);
		if (Arm.bArmed && bOnWater && !Sample.bFallen && bAtTop && Sample.BarPosition < SheetInBar)
		{
			Arm.bArmed = false;
			return true;
		}
		return false;
	}
	case ELessonSlowMoTrigger::RiderDescending:
	{
		if (Sample.BoardState != EBoardState::Airborne)
		{
			// Back on the water: the next jump may fire it again.
			Arm.bArmed = true;
			Arm.JumpPeakM = 0.0f;
			return false;
		}
		Arm.JumpPeakM = FMath::Max(Arm.JumpPeakM, Sample.HeightM);
		if (Arm.bArmed && !Sample.bFallen && Sample.VerticalSpeedMS < 0.0f && Sample.HeightM <= Cue.Threshold && Arm.JumpPeakM >= SlowMoMinJumpM)
		{
			Arm.bArmed = false;
			return true;
		}
		return false;
	}
	default:
		return false;
	}
}
