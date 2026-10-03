#include "Tricks/FreestyleHeat.h"
#include "KiteSurfUnits.h"
#include "Tricks/LandingEvaluator.h"
#include "Tricks/TrickNaming.h"

void FFreestyleHeat::Start(int32 InAttempts, float InStartClockSeconds)
{
	Phase = EFreestyleHeatPhase::Running;
	AttemptLimit = FMath::Clamp(InAttempts, 1, MaxAttempts);
	StartClockSeconds = InStartClockSeconds;
	CountdownSeconds = FMath::Max(Settings.TrickCountdownSeconds, 0.0f);
	CountdownLeft = CountdownSeconds;
	bCountdownHeld = false;
	SecondsSinceFinished = 0.0f;
	Attempts.Reset();
	Rescore();
}

void FFreestyleHeat::Cancel()
{
	Phase = EFreestyleHeatPhase::Idle;
	bCountdownHeld = false;
	SecondsSinceFinished = 0.0f;
	Attempts.Reset();
	Rescore();
}

EHeatRecordVerdict FFreestyleHeat::Classify(const FJumpRecord& Record, float MinAirtimeSeconds)
{
	// Any crash is an attempt, hooked or not: losing the bar or the board, or a crash landing.
	if (Record.Outcome == EJumpOutcome::Crashed || Record.Grade == ELandingGrade::Crash || Record.LandingCause == ELandingCause::BarLost)
	{
		return EHeatRecordVerdict::Crash;
	}
	if (Record.bHooked)
	{
		return EHeatRecordVerdict::Hooked;
	}
	return Record.AirtimeSeconds > MinAirtimeSeconds ? EHeatRecordVerdict::Attempt : EHeatRecordVerdict::TooShort;
}

FScoredTrick FFreestyleHeat::ScoreRecord(const FJumpRecord& Record, const FTrickScoringSettings& Scoring,
	const FLoopClassifySettings& InLoopClassify, const FLandingGradeSettings& InLandingGrade)
{
	FTrickSignature Signature = TrickRecognition::SignatureFromJump(Record, InLoopClassify, InLandingGrade, Scoring.GrabMinHoldSeconds);
	// The record's grade is the board's landing verdict (decision 6); the signature's is the record-only shortcut.
	const bool bCrash = Classify(Record) == EHeatRecordVerdict::Crash;
	Signature.Grade = bCrash ? ELandingGrade::Crash : Record.Grade;
	FLandingVerdict Verdict;
	Verdict.Grade = Signature.Grade;
	Verdict.Cause = Record.LandingCause;
	FScoredTrick Trick = FreestyleScoring::MakeScoredTrick(Signature, Verdict, KiteUnits::CmToM(Record.ApexHeightCm), Scoring);
	if (bCrash)
	{
		Trick.Score = 0.0f;
	}
	if (!Record.TrickName.IsEmpty())
	{
		Trick.Name = Record.TrickName;
	}
	return Trick;
}

EHeatRecordVerdict FFreestyleHeat::OfferRecord(const FJumpRecord& Record)
{
	if (Phase != EFreestyleHeatPhase::Running || !TookOffInHeat(Record))
	{
		return EHeatRecordVerdict::OutsideHeat;
	}
	const EHeatRecordVerdict Verdict = Classify(Record, Settings.MinAirtimeSeconds);
	if (Verdict != EHeatRecordVerdict::Attempt && Verdict != EHeatRecordVerdict::Crash)
	{
		return Verdict;
	}
	FHeatAttempt Attempt;
	Attempt.Trick = ScoreRecord(Record, Settings.Scoring, LoopClassify, LandingGrade);
	Attempt.Kind = Verdict == EHeatRecordVerdict::Crash ? EHeatAttemptKind::Crash : EHeatAttemptKind::Trick;
	Attempt.ApexM = KiteUnits::CmToM(Record.ApexHeightCm);
	Attempt.Grade = Verdict == EHeatRecordVerdict::Crash ? ELandingGrade::Crash : Record.Grade;
	Attempt.RecordIndex = Record.Index;
	AddAttempt(Attempt);
	return Verdict;
}

void FFreestyleHeat::Tick(float Seconds, const FJumpRecord* JumpInAir)
{
	const float Step = FMath::Max(Seconds, 0.0f);
	if (Phase == EFreestyleHeatPhase::Finished)
	{
		SecondsSinceFinished += Step;
		return;
	}
	if (Phase != EFreestyleHeatPhase::Running || !IsCountdownOn())
	{
		return;
	}
	CountdownLeft = FMath::Max(CountdownLeft - Step, 0.0f);
	bCountdownHeld = false;
	if (CountdownLeft > 0.0f)
	{
		return;
	}
	if (JumpInAir && TookOffInHeat(*JumpInAir))
	{
		// Wait for the jump's record: it may still be the attempt.
		bCountdownHeld = true;
		return;
	}
	// Nothing tried in time: the attempt is lost.
	FHeatAttempt Lost;
	Lost.Kind = EHeatAttemptKind::TimedOut;
	Lost.Trick.Name = TEXT("No trick");
	Lost.Trick.Family = EGkaFamily::None;
	Lost.Trick.Score = 0.0f;
	AddAttempt(Lost);
}

void FFreestyleHeat::AddAttempt(const FHeatAttempt& Attempt)
{
	Attempts.Add(Attempt);
	Rescore();
	CountdownLeft = CountdownSeconds;
	bCountdownHeld = false;
	if (Attempts.Num() >= AttemptLimit)
	{
		Phase = EFreestyleHeatPhase::Finished;
		SecondsSinceFinished = 0.0f;
	}
}

void FFreestyleHeat::Rescore()
{
	TArray<FScoredTrick> Tricks;
	Tricks.Reserve(Attempts.Num());
	for (const FHeatAttempt& Attempt : Attempts)
	{
		Tricks.Add(Attempt.Trick);
	}
	FFreestyleHeatRules Rules = Settings.Rules;
	Rules.Attempts = AttemptLimit;
	Result = FreestyleScoring::ScoreFreestyleHeat(Tricks, Rules);
}

TArray<FScoredTrick> FFreestyleHeat::GetCounting() const
{
	TArray<FScoredTrick> Counting;
	for (const int32 Index : Result.CountingIdx)
	{
		if (Attempts.IsValidIndex(Index))
		{
			Counting.Add(Attempts[Index].Trick);
		}
	}
	return Counting;
}

TArray<EGkaFamily> FFreestyleHeat::GetFamiliesUsed() const
{
	TArray<EGkaFamily> Families;
	for (const FScoredTrick& Trick : GetCounting())
	{
		Families.AddUnique(Trick.Family);
	}
	return Families;
}

FString FreestyleHeat::FamilyLabel(EGkaFamily Family)
{
	switch (Family)
	{
	case EGkaFamily::RaleyBased:      return TEXT("Raley");
	case EGkaFamily::KgbSlim:         return TEXT("KGB/Slim");
	case EGkaFamily::HinterHeart:     return TEXT("Hinter/Heart");
	case EGkaFamily::Mobes:           return TEXT("Mobes");
	case EGkaFamily::Rewinds:         return TEXT("Rewinds");
	case EGkaFamily::ToesideBlind:    return TEXT("Toeside/Blind");
	case EGkaFamily::Combos:          return TEXT("Combos");
	case EGkaFamily::InvertedDoubles: return TEXT("Inv. doubles");
	case EGkaFamily::KiteLoopPasses:  return TEXT("Loop passes");
	default:                          return TEXT("--");
	}
}

FString FreestyleHeat::FormatBonus(float Points)
{
	const float Rounded = FMath::RoundToFloat(Points);
	if (FMath::IsNearlyEqual(Points, Rounded, 1e-3f))
	{
		return FString::Printf(TEXT("%d"), FMath::RoundToInt(Points));
	}
	return FString::Printf(TEXT("%.1f"), Points);
}
