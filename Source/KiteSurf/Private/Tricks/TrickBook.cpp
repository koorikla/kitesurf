#include "Tricks/TrickBook.h"
#include "Tricks/JumpRecord.h"
#include "KiteSurfUnits.h"
#include "Algo/StableSort.h"

bool FTrickBook::RecordLanding(const FJumpRecord& Record, ETrickBoardCategory Board)
{
	return RecordLanding(Record, Board, FDateTime::UtcNow());
}

bool FTrickBook::RecordLanding(const FJumpRecord& Record, ETrickBoardCategory Board, const FDateTime& NowUtc)
{
	if (Record.Outcome == EJumpOutcome::Crashed || Record.Grade == ELandingGrade::Crash || Record.FamilyKey.IsEmpty())
	{
		return false;
	}

	const float HeightM = KiteUnits::CmToM(FMath::Max(Record.ApexHeightCm, 0.0f));
	const float Score = FMath::IsFinite(Record.Score.Total) ? Record.Score.Total : 0.0f;

	const int32 ExistingIndex = IndexOf(Record.FamilyKey, Board);
	if (ExistingIndex != INDEX_NONE)
	{
		FTrickBookEntry* Existing = &Entries[ExistingIndex];
		++Existing->TimesLanded;
		Existing->BestScore = FMath::Max(Existing->BestScore, Score);
		Existing->BestHeightM = FMath::Max(Existing->BestHeightM, HeightM);
		if (IsBetterGrade(Record.Grade, Existing->BestGrade))
		{
			Existing->BestGrade = Record.Grade;
		}
		if (Existing->DisplayName.IsEmpty())
		{
			Existing->DisplayName = Record.TrickName;
		}
		return false;
	}

	FTrickBookEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.FamilyKey = Record.FamilyKey;
	Entry.BoardCategory = Board;
	Entry.DisplayName = Record.TrickName;
	Entry.FirstLandedUtc = NowUtc;
	Entry.TimesLanded = 1;
	Entry.BestScore = Score;
	Entry.BestHeightM = HeightM;
	Entry.BestGrade = Record.Grade;
	return true;
}

const FTrickBookEntry* FTrickBook::Find(const FString& FamilyKey, ETrickBoardCategory Board) const
{
	const int32 Index = IndexOf(FamilyKey, Board);
	return Index != INDEX_NONE ? &Entries[Index] : nullptr;
}

int32 FTrickBook::IndexOf(const FString& FamilyKey, ETrickBoardCategory Board) const
{
	// Family keys are canonical and case matters in them (TrickNaming::FamilyKey).
	return Entries.IndexOfByPredicate([&FamilyKey, Board](const FTrickBookEntry& Entry)
	{
		return Entry.BoardCategory == Board && Entry.FamilyKey.Equals(FamilyKey, ESearchCase::CaseSensitive);
	});
}

void FTrickBook::SetEntries(const TArray<FTrickBookEntry>& InEntries)
{
	Entries.Reset(InEntries.Num());
	for (const FTrickBookEntry& Entry : InEntries)
	{
		if (Entry.FamilyKey.IsEmpty() || Entry.TimesLanded <= 0 || Find(Entry.FamilyKey, Entry.BoardCategory))
		{
			continue;
		}
		Entries.Add(Entry);
	}
	Algo::StableSortBy(Entries, &FTrickBookEntry::FirstLandedUtc);
}
