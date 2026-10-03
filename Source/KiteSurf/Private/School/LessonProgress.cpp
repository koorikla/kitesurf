#include "School/LessonProgress.h"
#include "School/LessonCatalog.h"

bool LessonProgress::IsLowerBetter(ELessonMetric Metric)
{
	switch (Metric)
	{
	case ELessonMetric::LandingGrade:
	case ELessonMetric::SinkAtTouchdown:
	case ELessonMetric::LandingG:
	case ELessonMetric::TimeToPlaning:
	case ELessonMetric::TransitionNotPlaningSeconds:
	case ELessonMetric::LoopDuration:
		return true;
	default:
		return false;
	}
}

int32 FLessonProgressBook::IndexOf(FName LessonId) const
{
	return Records.IndexOfByPredicate([LessonId](const FLessonRecord& R) { return R.LessonId == LessonId; });
}

bool FLessonProgressBook::RecordAttempt(FName LessonId, bool bPassed, int32 Stars, float Value, bool bNoAssists, const FDateTime& NowUtc,
	ELessonMetric ValueMetric)
{
	if (LessonId.IsNone())
	{
		return false;
	}

	int32 Index = IndexOf(LessonId);
	if (Index == INDEX_NONE)
	{
		FLessonRecord NewRecord;
		NewRecord.LessonId = LessonId;
		Index = Records.Add(NewRecord);
	}
	FLessonRecord& Record = Records[Index];

	++Record.Attempts;
	Record.LastPlayedUtc = NowUtc;

	bool bRaisedStars = false;
	if (bPassed)
	{
		if (Record.Passes == 0)
		{
			Record.FirstPassedUtc = NowUtc;
		}
		++Record.Passes;
		const int32 Earned = FMath::Clamp(Stars, 1, MaxStars);
		if (Earned > Record.BestStars)
		{
			Record.BestStars = Earned;
			bRaisedStars = true;
		}
		Record.bPassedNoAssists |= bNoAssists;
	}

	if (FMath::IsFinite(Value))
	{
		if (ValueMetric == ELessonMetric::None)
		{
			if (const FLessonDef* Def = LessonCatalog::Find(LessonId))
			{
				ValueMetric = Def->Pass.Metric;
			}
		}
		// A value of another metric (the lesson's objective changed) starts the best again.
		const bool bSameMetric = Record.bHasBestValue && Record.BestValueMetric == ValueMetric;
		const bool bBetter = !bSameMetric
			|| (LessonProgress::IsLowerBetter(ValueMetric) ? Value < Record.BestValue : Value > Record.BestValue);
		if (bBetter)
		{
			Record.bHasBestValue = true;
			Record.BestValue = Value;
			Record.BestValueMetric = ValueMetric;
		}
	}

	return bRaisedStars;
}

const FLessonRecord* FLessonProgressBook::Find(FName LessonId) const
{
	const int32 Index = IndexOf(LessonId);
	return Index == INDEX_NONE ? nullptr : &Records[Index];
}

int32 FLessonProgressBook::GetStars(FName LessonId) const
{
	const FLessonRecord* Record = Find(LessonId);
	return Record ? Record->BestStars : 0;
}

int32 FLessonProgressBook::TotalStars() const
{
	int32 Total = 0;
	for (const FLessonRecord& Record : Records)
	{
		Total += Record.BestStars;
	}
	return Total;
}

float FLessonProgressBook::ChapterCompletion(FName Chapter) const
{
	return ChapterCompletion(Chapter, LessonCatalog::GetAll());
}

float FLessonProgressBook::ChapterCompletion(FName Chapter, const TArray<FLessonDef>& Lessons) const
{
	int32 InChapter = 0;
	int32 Passed = 0;
	for (const FLessonDef& Lesson : Lessons)
	{
		if (Lesson.Chapter == Chapter)
		{
			++InChapter;
			Passed += GetStars(Lesson.Id) > 0 ? 1 : 0;
		}
	}
	return InChapter > 0 ? float(Passed) / float(InChapter) : 0.0f;
}

void FLessonProgressBook::SetEntries(const TArray<FLessonRecord>& InEntries)
{
	Records.Reset();
	for (const FLessonRecord& In : InEntries)
	{
		if (In.LessonId.IsNone() || IndexOf(In.LessonId) != INDEX_NONE)
		{
			continue;
		}
		FLessonRecord Record = In;
		Record.Passes = FMath::Max(Record.Passes, 0);
		Record.Attempts = FMath::Max(Record.Attempts, Record.Passes);
		Record.BestStars = Record.Passes > 0 ? FMath::Clamp(Record.BestStars, 0, MaxStars) : 0;
		Record.bPassedNoAssists = Record.bPassedNoAssists && Record.Passes > 0;
		Records.Add(Record);
	}
}

bool LessonUnlock::IsUnlocked(const FLessonDef& Lesson, const FLessonProgressBook& Book)
{
	if (!LessonCatalog::IsAvailable(Lesson))
	{
		return false;
	}
	for (const FName& Required : Lesson.Requires)
	{
		if (Book.GetStars(Required) < 1)
		{
			return false;
		}
	}
	return true;
}

FName LessonUnlock::RecommendedNext(const FLessonProgressBook& Book)
{
	return RecommendedNext(Book, LessonCatalog::GetAll());
}

FName LessonUnlock::RecommendedNext(const FLessonProgressBook& Book, const TArray<FLessonDef>& Lessons)
{
	FName Fewest = NAME_None;
	int32 FewestStars = TNumericLimits<int32>::Max();
	for (const FLessonDef& Lesson : Lessons)
	{
		if (!IsUnlocked(Lesson, Book))
		{
			continue;
		}
		const int32 Stars = Book.GetStars(Lesson.Id);
		if (Stars == 0)
		{
			return Lesson.Id; // the first unlocked lesson without a star
		}
		if (Stars < FewestStars) // strict: the earliest lesson wins a tie
		{
			FewestStars = Stars;
			Fewest = Lesson.Id;
		}
	}
	return Fewest;
}
