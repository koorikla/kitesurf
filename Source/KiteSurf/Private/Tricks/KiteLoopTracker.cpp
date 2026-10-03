#include "Tricks/KiteLoopTracker.h"

void FKiteLoopTracker::FExtremes::Begin(float TimeSeconds, const FKiteLoopSample& Sample, int32 TravelSide)
{
	StartTimeSeconds = TimeSeconds;
	StartElevationDeg = Sample.ElevationDeg;
	MinElevationDeg = Sample.ElevationDeg;
	PeakTensionN = Sample.TensionN;
	PeakTensionTimeSeconds = Sample.TimeSeconds;
	RiderZAtStartCm = Sample.RiderZCm;
	RiderTravelSide = TravelSide;
}

void FKiteLoopTracker::FExtremes::Add(const FKiteLoopSample& Sample)
{
	MinElevationDeg = FMath::Min(MinElevationDeg, Sample.ElevationDeg);
	if (Sample.TensionN > PeakTensionN)
	{
		PeakTensionN = Sample.TensionN;
		PeakTensionTimeSeconds = Sample.TimeSeconds;
	}
}

void FKiteLoopTracker::FExtremes::Merge(const FExtremes& Later)
{
	MinElevationDeg = FMath::Min(MinElevationDeg, Later.MinElevationDeg);
	if (Later.PeakTensionN > PeakTensionN)
	{
		PeakTensionN = Later.PeakTensionN;
		PeakTensionTimeSeconds = Later.PeakTensionTimeSeconds;
	}
}

int32 FKiteLoopTracker::TravelSideOf(const FKiteLoopSample& Sample) const
{
	const FVector Travel(Sample.RiderVelocity.X, Sample.RiderVelocity.Y, 0.0f);
	const float Speed = Travel.Size();
	if (Speed < FMath::Max(Settings.TravelSideMinSpeedCmS, UE_KINDA_SMALL_NUMBER))
	{
		return 0;
	}
	// Crosswind-right: Up x Downwind, to the right looking downwind (as KiteSurfGameMode's tack).
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Sample.DownwindDir).GetSafeNormal2D();
	if (Right.IsNearlyZero())
	{
		return 0;
	}
	const float Along = FVector::DotProduct(Travel / Speed, Right);
	if (FMath::Abs(Along) < Settings.TravelSideMinDot)
	{
		return 0;
	}
	return Along > 0.0f ? 1 : -1;
}

void FKiteLoopTracker::Step(const FKiteLoopSample& Sample)
{
	const float StepStartSeconds = bHasLastTime ? LastTimeSeconds : Sample.TimeSeconds;
	const float StepSeconds = FMath::Max(Sample.TimeSeconds - StepStartSeconds, 0.0f);
	LastTimeSeconds = Sample.TimeSeconds;
	bHasLastTime = true;

	if (Sample.bCrashed)
	{
		if (bRunning)
		{
			EndRun(true, Sample.TimeSeconds);
		}
		PendingTurnDeg = 0.0f;
		Pending.Begin(Sample.TimeSeconds, Sample, 0);
		return;
	}

	// A step with no time (the first sample) has no rate: it is slow only if it did not turn.
	const float RateDegPerS = StepSeconds > 0.0f
		? FMath::Abs(Sample.TurnDeg) / StepSeconds
		: (Sample.TurnDeg != 0.0f ? TNumericLimits<float>::Max() : 0.0f);

	if (!bRunning)
	{
		StepPending(Sample, StepStartSeconds, RateDegPerS);
		return;
	}

	const float AlongRunDeg = Sample.TurnDeg * RunDirection;
	const float BeforeDeg = RunTurnDeg;
	RunTurnDeg += AlongRunDeg;
	StallTimeSeconds = RateDegPerS < Settings.StallRateDegPerS ? StallTimeSeconds + StepSeconds : 0.0f;

	if (RunTurnDeg > RunPeakDeg)
	{
		// New ground: a counter-turn that came back belongs to this loop after all.
		if (bHasSincePeak)
		{
			LoopAtPeak.Merge(SincePeak);
			bHasSincePeak = false;
		}
		// Each whole loop passed in this step ends a record at the crossing, and the next loop starts there.
		while (RunTurnDeg >= 360.0f * (CompletedLoops + 1))
		{
			const float CrossingDeg = 360.0f * (CompletedLoops + 1);
			const float Fraction = AlongRunDeg > 0.0f ? FMath::Clamp((CrossingDeg - BeforeDeg) / AlongRunDeg, 0.0f, 1.0f) : 1.0f;
			const float CrossingSeconds = StepStartSeconds + Fraction * StepSeconds;
			LoopAtPeak.Add(Sample);
			Emit(MakeRecord(LoopAtPeak, CrossingSeconds, 360.0f, true));
			++CompletedLoops;
			LoopAtPeak.Begin(CrossingSeconds, Sample, TravelSideOf(Sample));
		}
		LoopAtPeak.Add(Sample);
		RunPeakDeg = RunTurnDeg;
		RunPeakTimeSeconds = Sample.TimeSeconds;
	}
	else
	{
		if (!bHasSincePeak)
		{
			SincePeak.Begin(RunPeakTimeSeconds, Sample, TravelSideOf(Sample));
			bHasSincePeak = true;
		}
		SincePeak.Add(Sample);
	}

	if (RunPeakDeg - RunTurnDeg > Settings.ReverseToleranceDeg)
	{
		// The counter-turn since the furthest point is the start of a run the other way.
		const float CounterTurnDeg = RunPeakDeg - RunTurnDeg;
		const int32 Opposite = -RunDirection;
		const FExtremes CounterTurn = SincePeak;
		EndRun(false, 0.0f);
		PendingTurnDeg = Opposite * CounterTurnDeg;
		Pending = CounterTurn;
		if (CounterTurnDeg >= Settings.StartTurnDeg)
		{
			OpenRun(Opposite, CounterTurnDeg, Pending);
		}
		return;
	}

	if (StallTimeSeconds >= Settings.StallSeconds)
	{
		EndRun(false, 0.0f);
		PendingTurnDeg = 0.0f;
		Pending.Begin(Sample.TimeSeconds, Sample, TravelSideOf(Sample));
	}
}

void FKiteLoopTracker::StepPending(const FKiteLoopSample& Sample, float StepStartSeconds, float RateDegPerS)
{
	if (RateDegPerS < Settings.StallRateDegPerS)
	{
		PendingTurnDeg = 0.0f;
		Pending.Begin(Sample.TimeSeconds, Sample, TravelSideOf(Sample));
		return;
	}

	if (PendingTurnDeg == 0.0f || FMath::Sign(Sample.TurnDeg) != FMath::Sign(PendingTurnDeg))
	{
		PendingTurnDeg = 0.0f;
		Pending.Begin(StepStartSeconds, Sample, TravelSideOf(Sample));
	}
	else
	{
		Pending.Add(Sample);
	}
	PendingTurnDeg += Sample.TurnDeg;

	if (FMath::Abs(PendingTurnDeg) >= Settings.StartTurnDeg)
	{
		OpenRun(PendingTurnDeg > 0.0f ? 1 : -1, FMath::Abs(PendingTurnDeg), Pending);
	}
}

void FKiteLoopTracker::OpenRun(int32 Direction, float TurnDeg, const FExtremes& From)
{
	bRunning = true;
	RunDirection = Direction;
	RunTurnDeg = TurnDeg;
	RunPeakDeg = TurnDeg;
	RunPeakTimeSeconds = LastTimeSeconds;
	CompletedLoops = 0;
	LoopAtPeak = From;
	bHasSincePeak = false;
	StallTimeSeconds = 0.0f;
	PendingTurnDeg = 0.0f;
}

void FKiteLoopTracker::EndRun(bool bCrashed, float CrashTimeSeconds)
{
	const float RemainderDeg = RunPeakDeg - 360.0f * CompletedLoops;
	if (RemainderDeg >= Settings.MinPartialDeg)
	{
		if (bCrashed)
		{
			// A crash cuts the loop where it is: keep everything up to it.
			FExtremes Loop = LoopAtPeak;
			if (bHasSincePeak)
			{
				Loop.Merge(SincePeak);
			}
			FKiteLoopRecord Record = MakeRecord(Loop, CrashTimeSeconds, RemainderDeg, false);
			Record.bKiteCrashed = true;
			Emit(Record);
		}
		else
		{
			Emit(MakeRecord(LoopAtPeak, RunPeakTimeSeconds, RemainderDeg, false));
		}
	}

	bRunning = false;
	RunDirection = 0;
	RunTurnDeg = 0.0f;
	RunPeakDeg = 0.0f;
	CompletedLoops = 0;
	bHasSincePeak = false;
	StallTimeSeconds = 0.0f;
}

void FKiteLoopTracker::CancelRun()
{
	bRunning = false;
	RunDirection = 0;
	RunTurnDeg = 0.0f;
	RunPeakDeg = 0.0f;
	CompletedLoops = 0;
	bHasSincePeak = false;
	StallTimeSeconds = 0.0f;
	PendingTurnDeg = 0.0f;
}

FKiteLoopRecord FKiteLoopTracker::MakeRecord(const FExtremes& Loop, float EndTimeSeconds, float TurnDeg, bool bCompleted) const
{
	FKiteLoopRecord Record;
	Record.Direction = RunDirection;
	Record.StartTimeSeconds = Loop.StartTimeSeconds;
	Record.DurationSeconds = FMath::Max(EndTimeSeconds - Loop.StartTimeSeconds, 0.0f);
	Record.TurnDeg = TurnDeg;
	Record.bCompleted = bCompleted;
	Record.StartElevationDeg = Loop.StartElevationDeg;
	Record.MinElevationDeg = Loop.MinElevationDeg;
	Record.PeakTensionN = Loop.PeakTensionN;
	Record.PeakTensionTimeSeconds = Loop.PeakTensionTimeSeconds;
	Record.RiderZAtStartCm = Loop.RiderZAtStartCm;
	Record.RiderTravelSide = Loop.RiderTravelSide;
	return Record;
}

void FKiteLoopTracker::Emit(FKiteLoopRecord Record)
{
	Record.Index = TotalRecorded++;
	Records.Add(Record);
	const int32 Keep = FMath::Max(Settings.MaxRecords, 1);
	if (Records.Num() > Keep)
	{
		Records.RemoveAt(0, Records.Num() - Keep);
	}
}

bool FKiteLoopTracker::GetOpenRun(FKiteLoopRecord& Out) const
{
	if (!bRunning)
	{
		return false;
	}
	FExtremes Loop = LoopAtPeak;
	if (bHasSincePeak)
	{
		Loop.Merge(SincePeak);
	}
	Out = MakeRecord(Loop, LastTimeSeconds, FMath::Max(RunPeakDeg - 360.0f * CompletedLoops, 0.0f), false);
	return true;
}
