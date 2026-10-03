#include "Tricks/JumpRecorder.h"
#include "Tricks/BarState.h"
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

	// The rotation is measured from the riding pose at the take-off; the attitude takes over on the
	// next step.
	Rotation = FRotationRecognizer();
	Raley = FRaleyRecognizer();
	bRotationStepped = false;
	LastStepTimeSeconds = In.BoardTimeSeconds;
	if (In.bHasAttitude)
	{
		const FRotationTakeoffFrame Frame = FRotationTakeoffFrame::Make(In.BodyQuat, In.Velocity, In.BoardForward);
		Rotation.Begin(Frame, In.BodyQuat);
		Raley.Begin(Frame.Sigma);
	}
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

	// The grabs and the one-footer as the grab state has them; on the landing step the grab still
	// held is counted to the touchdown (the grab state stepped before the board landed).
	if (In.Grabs)
	{
		Live.Grabs = *In.Grabs;
	}
	Live.bOneFooter = In.bOneFooter;
	Live.OneFootSeconds = In.OneFootSeconds;
	Live.BoardOff = In.BoardOff;
	Live.BoardOffSeconds = In.BoardOffSeconds;

	// The bar as it stands: hooked or not, the passes finished so far and the stance the wrap gives.
	// On the landing step it is the bar as it met the water (stepped before the board landed); a pass
	// still between the hands then is not counted.
	if (In.Bar)
	{
		const FBarJumpSummary Bar = BarStateMachine::SummariseJump(*In.Bar);
		Live.bHooked = In.Bar->bHooked;
		Live.Passes = In.Bar->bHooked ? TArray<FTrickPass>() : Bar.Passes;
		Live.BarLandingStance = In.Bar->bHooked ? ETrickStance::Heelside : Bar.LandingStance;
	}

	const float StepSeconds = In.BoardTimeSeconds - LastStepTimeSeconds;
	LastStepTimeSeconds = In.BoardTimeSeconds;
	if (Rotation.HasBegun() && In.bHasAttitude && In.bAttitudeActive && StepSeconds > 0.0f)
	{
		Rotation.Step(In.BodyQuat, In.AngularVelocityRadS, StepSeconds, In.Velocity);
		Raley.Step(In.BodyQuat, In.AngularVelocityRadS, In.LineDirWorld, In.bRaleyArms, StepSeconds);
		bRotationStepped = true;
	}
	if (bRotationStepped)
	{
		// The live view: what has been credited so far, for the ticker.
		ApplyRotation(Rotation.GetCurrent(), Live);
		ApplyRaley(Live);
	}
}

ETrickMove FJumpRecorder::TakeoffMoveOf(const FJumpRecord& Record)
{
	if (Record.bSBend)
	{
		return ETrickMove::SBend;
	}
	if (Record.bRaley)
	{
		return ETrickMove::Raley;
	}
	if (Record.Inversions.Num() > 0)
	{
		switch (Record.Inversions[0])
		{
		case ETrickInversion::BackRoll:  return ETrickMove::BackRoll;
		case ETrickInversion::FrontRoll: return ETrickMove::FrontRoll;
		case ETrickInversion::FrontFlip: return ETrickMove::FrontFlip;
		case ETrickInversion::BackFlip:  return ETrickMove::BackFlip;
		default: break;
		}
	}
	return ETrickMove::Pop;
}

void FJumpRecorder::ApplyRaley(FJumpRecord& Record) const
{
	if (Raley.HasBegun())
	{
		const FRaleyResult Result = Raley.Get(!Record.bHooked, Record.Inversions.Num(), Record.Passes.Num());
		Record.bRaley = Result.bRaley;
		Record.bSBend = Result.bSBend;
		Record.SBendSense = Result.SBendSense;
		Record.MaxTiltDeg = Result.MaxTiltDeg;
		Record.LineSpinDeg = Result.LineSpinDeg;
		if (Result.bSBend)
		{
			// The S-bend is the overhead rotation: its turn about the lines is the body spin the naming
			// table reads (S-bend backside, hinterberger frontside), and any inversion it made is its own.
			Record.Inversions.Reset();
			Record.RollStartSinceTakeoffSeconds = -1.0f;
			Record.SpinHalfTurns = FMath::Max(2, FMath::RoundToInt(FMath::Abs(Result.LineSpinDeg) / 180.0f));
			Record.SpinSense = Result.SBendSense;
		}
	}
	Record.TakeoffMove = TakeoffMoveOf(Record);
}

void FJumpRecorder::ApplyRotation(const FRotationResult& Result, FJumpRecord& Record)
{
	Record.bRotationTracked = true;
	Record.Inversions = Result.InversionKinds();
	Record.SpinHalfTurns = Result.SpinHalfTurns;
	Record.SpinSense = Result.SpinSense;
	Record.SpinDeg = Result.SpinDeg;
	Record.LandingStance = Result.LandingStance;
	Record.NetHeadingDeg = Result.NetHeadingDeg;
	Record.RollStartSinceTakeoffSeconds = Result.RollStartSeconds;
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
	Record.LandingCause = In.LastLandingCause;
	CollectLoops(In, Record);
	if (bRotationStepped)
	{
		// The touchdown step's body: the attitude stepped before the board landed.
		ApplyRotation(Rotation.Finish(In.BodyQuat), Record);
		ApplyRaley(Record);
	}

	FTrickSignature Signature = TrickRecognition::SignatureFromJump(Record, Settings.LoopClassify, Settings.LandingGrade,
		Settings.Scoring.GrabMinHoldSeconds);
	if (In.bHasLandingVerdict)
	{
		// Decision 6: the board's landing verdict owns the grade. The signature's GradeLanding is the
		// record-only shortcut, kept for snapshots without a board.
		Signature.Grade = TrickScoring::GradeFromVerdict(In.LastLandingGrade, Record.Outcome == EJumpOutcome::Crashed);
	}
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
