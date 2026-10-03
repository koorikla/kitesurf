#include "Tricks/SessionScoring.h"

void FBestThreeSession::Start(float InDurationSeconds, float InStartClockSeconds)
{
	Phase = EBestThreePhase::Running;
	DurationSeconds = FMath::Max(InDurationSeconds, 0.0f);
	StartClockSeconds = InStartClockSeconds;
	ElapsedSeconds = 0.0f;
	SecondsSinceFinished = 0.0f;
	Jumps.Reset();
	Repeats.Reset();
}

bool FBestThreeSession::TookOffInWindow(const FJumpRecord& Record) const
{
	const float TakeoffSeconds = SessionTimeOf(Record.TakeoffTimeSeconds);
	return TakeoffSeconds >= 0.0f && TakeoffSeconds < DurationSeconds;
}

void FBestThreeSession::Tick(float Seconds, const FJumpRecord* JumpInAir)
{
	const float Step = FMath::Max(Seconds, 0.0f);
	if (Phase == EBestThreePhase::Finished)
	{
		SecondsSinceFinished += Step;
		return;
	}
	if (!IsActive())
	{
		return;
	}

	ElapsedSeconds += Step;
	const bool bCountingJumpInAir = JumpInAir && TookOffInWindow(*JumpInAir);
	if (Phase == EBestThreePhase::Running && ElapsedSeconds >= DurationSeconds)
	{
		// The horn. A jump that took off before it still counts at its landing.
		if (bCountingJumpInAir)
		{
			Phase = EBestThreePhase::Overtime;
		}
		else
		{
			Finish();
			return;
		}
	}
	if (Phase == EBestThreePhase::Overtime
		&& (!bCountingJumpInAir || ElapsedSeconds >= DurationSeconds + Settings.OvertimeMaxSeconds))
	{
		Finish();
	}
}

void FBestThreeSession::Finish()
{
	Phase = EBestThreePhase::Finished;
	SecondsSinceFinished = 0.0f;
}

bool FBestThreeSession::AddJump(const FJumpRecord& Record)
{
	if (!IsActive() || !TookOffInWindow(Record) || Record.ApexHeightCm < Settings.MinJumpHeightCm)
	{
		return false;
	}
	FJumpRecord& Taken = Jumps.Add_GetRef(Record);
	Taken.RepeatFactor = Repeats.NextRepeatFactor(Record.FamilyKey, Settings.Scoring);
	if (Record.Outcome == EJumpOutcome::Landed)
	{
		// Only landings use up a family's full-value landing; a crash is paid nothing either way.
		Repeats.Add(Record.FamilyKey, Record.Score.Total, Settings.Scoring);
	}
	return true;
}

float FBestThreeSession::Paid(const FJumpRecord& Record)
{
	return Record.Outcome == EJumpOutcome::Landed ? FMath::Max(Record.Score.Total * Record.RepeatFactor, 0.0f) : 0.0f;
}

TArray<FJumpRecord> FBestThreeSession::GetCounting() const
{
	// The best paid landing per family key (the earliest on a tie)...
	TMap<FString, int32> BestByFamily;
	for (int32 Index = 0; Index < Jumps.Num(); ++Index)
	{
		const FJumpRecord& Jump = Jumps[Index];
		if (Jump.Outcome != EJumpOutcome::Landed || Paid(Jump) <= 0.0f)
		{
			continue;
		}
		int32* Best = BestByFamily.Find(Jump.FamilyKey);
		if (!Best)
		{
			BestByFamily.Add(Jump.FamilyKey, Index);
		}
		else if (Paid(Jump) > Paid(Jumps[*Best]))
		{
			*Best = Index;
		}
	}

	// ...then the best CountingJumps of those, best first (the earlier jump first on a tie).
	TArray<int32> Candidates;
	BestByFamily.GenerateValueArray(Candidates);
	Candidates.Sort([this](int32 A, int32 B)
	{
		const float PaidA = Paid(Jumps[A]);
		const float PaidB = Paid(Jumps[B]);
		return PaidA != PaidB ? PaidA > PaidB : A < B;
	});

	TArray<FJumpRecord> Counting;
	const int32 Count = FMath::Min(Candidates.Num(), FMath::Max(Settings.CountingJumps, 0));
	for (int32 Rank = 0; Rank < Count; ++Rank)
	{
		Counting.Add(Jumps[Candidates[Rank]]);
	}
	return Counting;
}

float FBestThreeSession::GetTotal() const
{
	float Total = 0.0f;
	for (const FJumpRecord& Jump : GetCounting())
	{
		Total += Paid(Jump);
	}
	return Total;
}

float FBestThreeSession::GetTimeLeft() const
{
	return Phase == EBestThreePhase::Idle ? 0.0f : FMath::Max(DurationSeconds - ElapsedSeconds, 0.0f);
}
