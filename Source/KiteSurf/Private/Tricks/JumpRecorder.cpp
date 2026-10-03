#include "Tricks/JumpRecorder.h"
#include "Tricks/TrickNaming.h"

bool FJumpRecorder::Step(const FJumpRecorderInput& In, FJumpRecord& OutRecord)
{
	if (!bPrimed)
	{
		// Counters may already be past zero (a recorder made mid-session): read them, act on nothing.
		bPrimed = true;
		LastTakeoffCount = In.TakeoffCount;
		LastJumpCount = In.JumpCount;
		LastResetCount = In.ResetCount;
		return false;
	}

	const bool bReset = In.ResetCount != LastResetCount;
	const bool bJumpEnded = In.JumpCount != LastJumpCount;
	const bool bTookOff = In.TakeoffCount != LastTakeoffCount;
	LastResetCount = In.ResetCount;
	LastJumpCount = In.JumpCount;
	LastTakeoffCount = In.TakeoffCount;

	bool bFinalised = false;

	if (bReset && bOpen)
	{
		bOpen = false;
	}

	if (bJumpEnded && bOpen)
	{
		Accumulate(In);
		Finalise(In, OutRecord);
		bOpen = false;
		bFinalised = true;
	}

	if (bTookOff)
	{
		// A jump still open here never ended where the recorder could see it: drop it.
		Open(In);
	}
	else if (bOpen && In.BoardState != EBoardState::Airborne && !In.bCrashing && !bJumpEnded)
	{
		// Back on the water with no jump counted: the board's skip path.
		bOpen = false;
	}

	if (bOpen && !bTookOff)
	{
		Accumulate(In);
	}
	return bFinalised;
}

void FJumpRecorder::Reset()
{
	bPrimed = false;
	bOpen = false;
	Live = FJumpRecord();
	RecordedCount = 0;
}

void FJumpRecorder::Open(const FJumpRecorderInput& In)
{
	bOpen = true;
	Live = FJumpRecord();
	Live.bPopped = In.bLastTakeoffPopped;
	Live.TakeoffTimeSeconds = In.LastTakeoffTimeSeconds;
	Live.TakeoffLocation = In.Location;
	Live.TakeoffSpeedCmS = static_cast<float>(In.Velocity.Size2D());
	Live.TakeoffTensionN = In.TensionN;
	Live.PeakTensionN = In.TensionN;
	Live.MinKiteElevationDeg = In.KiteElevationDeg;
	Live.ApexTimeSeconds = In.BoardTimeSeconds;
	Live.AirtimeSeconds = FMath::Max(In.BoardTimeSeconds - In.LastTakeoffTimeSeconds, 0.0f);
	KiteMinusBoardSeconds = In.KiteTimeSeconds - In.BoardTimeSeconds;
	HighestZCm = static_cast<float>(In.Location.Z);
	HighestZTimeSeconds = In.BoardTimeSeconds;
}

void FJumpRecorder::Accumulate(const FJumpRecorderInput& In)
{
	Live.PeakTensionN = FMath::Max(Live.PeakTensionN, In.TensionN);
	Live.MinKiteElevationDeg = FMath::Min(Live.MinKiteElevationDeg, In.KiteElevationDeg);
	const float Z = static_cast<float>(In.Location.Z);
	if (Z > HighestZCm)
	{
		HighestZCm = Z;
		HighestZTimeSeconds = In.BoardTimeSeconds;
	}
	// Live view only; the board's numbers replace these at the landing.
	Live.ApexHeightCm = FMath::Max(HighestZCm - static_cast<float>(Live.TakeoffLocation.Z), 0.0f);
	Live.ApexTimeSeconds = HighestZTimeSeconds;
	Live.AirtimeSeconds = FMath::Max(In.BoardTimeSeconds - Live.TakeoffTimeSeconds, 0.0f);
}

void FJumpRecorder::Finalise(const FJumpRecorderInput& In, FJumpRecord& OutRecord)
{
	FJumpRecord Record = Live;
	Record.Index = RecordedCount++;
	Record.Outcome = In.bLastLandingClean ? EJumpOutcome::Landed : EJumpOutcome::Crashed;
	Record.ApexTimeSeconds = In.LastApexTimeSeconds >= 0.0f ? In.LastApexTimeSeconds : HighestZTimeSeconds;
	Record.LandingTimeSeconds = In.BoardTimeSeconds;
	Record.AirtimeSeconds = In.LastAirtimeSeconds;
	Record.LandingLocation = In.Location;
	Record.ApexHeightCm = In.LastApexCm;
	Record.DistanceCm = In.LastDistanceCm;
	Record.SinkRateCmS = In.LastSinkRateCmS;
	Record.LandingG = In.LastLandingG;
	Record.LandingYawDeg = In.LastLandingAngleDeg;
	Record.KiteElevationAtLandingDeg = In.KiteElevationDeg;
	CollectLoops(In, Record);

	const FTrickSignature Signature = TrickRecognition::SignatureFromJump(Record, Settings.LoopClassify, Settings.LandingGrade);
	Record.TrickName = TrickNaming::Name(Signature);
	Record.FamilyKey = TrickNaming::FamilyKey(Signature);
	Record.Grade = Signature.Grade;
	Record.Score = TrickScoring::ScoreJump(Record, Signature, Settings.Scoring);
	Record.RepeatFactor = 1.0f;

	OutRecord = MoveTemp(Record);
}

void FJumpRecorder::CollectLoops(const FJumpRecorderInput& In, FJumpRecord& Record) const
{
	const float TakeoffKite = Record.TakeoffTimeSeconds + KiteMinusBoardSeconds;
	const float LandingKite = Record.LandingTimeSeconds + KiteMinusBoardSeconds;

	Record.Loops.Reset();
	if (In.KiteLoops)
	{
		for (const FKiteLoopRecord& Loop : *In.KiteLoops)
		{
			const float Start = Loop.StartTimeSeconds;
			const float End = Loop.StartTimeSeconds + Loop.DurationSeconds;
			if (Start <= LandingKite && End >= TakeoffKite)
			{
				Record.Loops.Add(MakeJumpLoop(Loop, Record));
			}
		}
	}
	if (In.bHasOpenLoop && In.OpenLoop.TurnDeg >= Settings.MinOpenLoopDeg && In.OpenLoop.StartTimeSeconds <= LandingKite)
	{
		Record.Loops.Add(MakeJumpLoop(In.OpenLoop, Record));
	}
	Record.Loops.StableSort([](const FJumpLoop& A, const FJumpLoop& B)
	{
		return A.Loop.StartTimeSeconds < B.Loop.StartTimeSeconds;
	});
}

FJumpLoop FJumpRecorder::MakeJumpLoop(const FKiteLoopRecord& Loop, const FJumpRecord& Record) const
{
	const float TakeoffKite = Record.TakeoffTimeSeconds + KiteMinusBoardSeconds;
	const float ApexKite = Record.ApexTimeSeconds + KiteMinusBoardSeconds;
	FJumpLoop Result;
	Result.Loop = Loop;
	Result.StartSinceTakeoffSeconds = Loop.StartTimeSeconds - TakeoffKite;
	Result.StartSinceApexSeconds = Loop.StartTimeSeconds - ApexKite;
	Result.RiderHeightAtStartCm = Loop.RiderZAtStartCm - static_cast<float>(Record.TakeoffLocation.Z);
	return Result;
}

bool FJumpSession::Step(const FJumpRecorderInput& In, FJumpRecord* OutRecord)
{
	FJumpRecord Record;
	if (!Recorder.Step(In, Record))
	{
		return false;
	}
	const FTrickScoringSettings& Scoring = Recorder.Settings.Scoring;
	Record.RepeatFactor = Tricks.NextRepeatFactor(Record.FamilyKey, Scoring);
	if (Record.Outcome == EJumpOutcome::Landed)
	{
		// Only landings count towards repeats; a crash totals 0 and is paid nothing either way.
		SessionPoints += Tricks.Add(Record.FamilyKey, Record.Score.Total, Scoring);
	}

	if (MaxRecords > 0)
	{
		if (Records.Num() >= MaxRecords)
		{
			Records.RemoveAt(0, Records.Num() - MaxRecords + 1);
		}
		Records.Add(Record);
	}
	if (OutRecord)
	{
		*OutRecord = MoveTemp(Record);
	}
	return true;
}

bool FJumpSession::GetLastRecord(FJumpRecord& Out) const
{
	if (Records.Num() == 0)
	{
		return false;
	}
	Out = Records.Last();
	return true;
}

void FJumpSession::Clear()
{
	Recorder.Reset();
	Tricks.Reset();
	Records.Reset();
	SessionPoints = 0.0f;
}
