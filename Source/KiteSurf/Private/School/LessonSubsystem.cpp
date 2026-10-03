#include "School/LessonSubsystem.h"
#include "School/LessonCatalog.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfSaveGame.h"

DEFINE_LOG_CATEGORY_STATIC(LogKiteSchool, Log, All);

bool ULessonSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Outer && Outer->IsA<UKiteSurfGameInstance>();
}

bool ULessonSubsystem::RecordLessonResult(FName LessonId, bool bPassed, int32 Stars, float Value, bool bNoAssists)
{
	if (!LessonCatalog::Find(LessonId))
	{
		UE_LOG(LogKiteSchool, Warning, TEXT("RecordLessonResult: no lesson %s in the catalogue; ignored"), *LessonId.ToString());
		return false;
	}
	const bool bRaised = Progress.RecordAttempt(LessonId, bPassed, Stars, Value, bNoAssists, FDateTime::UtcNow());
	SaveProgress();
	return bRaised;
}

bool ULessonSubsystem::IsUnlocked(FName LessonId) const
{
	const FLessonDef* Lesson = LessonCatalog::Find(LessonId);
	return Lesson && LessonUnlock::IsUnlocked(*Lesson, Progress);
}

FName ULessonSubsystem::GetRecommendedNext() const
{
	return LessonUnlock::RecommendedNext(Progress);
}

float ULessonSubsystem::GetChapterCompletion(FName Chapter) const
{
	return Progress.ChapterCompletion(Chapter);
}

int32 ULessonSubsystem::GetTotalStars() const
{
	return Progress.TotalStars();
}

TArray<FLessonListItem> ULessonSubsystem::GetLessonList() const
{
	TArray<FLessonListItem> Items;
	for (const FLessonDef& Lesson : LessonCatalog::GetAll())
	{
		FLessonListItem& Item = Items.AddDefaulted_GetRef();
		Item.LessonId = Lesson.Id;
		Item.Title = Lesson.Title;
		Item.Chapter = Lesson.Chapter;
		Item.Stars = Progress.GetStars(Lesson.Id);
		Item.bAvailable = LessonCatalog::IsAvailable(Lesson);
		Item.bLocked = !LessonUnlock::IsUnlocked(Lesson, Progress);
		const FLessonRecord* Record = Progress.Find(Lesson.Id);
		Item.bNew = !Item.bLocked && (!Record || Record->Attempts == 0);
	}
	return Items;
}

void ULessonSubsystem::ResetProgress()
{
	Progress.Reset();
	SaveProgress();
}

bool ULessonSubsystem::StartLesson(FName LessonId)
{
	if (!IsUnlocked(LessonId))
	{
		UE_LOG(LogKiteSchool, Log, TEXT("StartLesson: %s is locked or unknown"), *LessonId.ToString());
		return false;
	}
	UE_LOG(LogKiteSchool, Log, TEXT("StartLesson: %s is unlocked, but lessons cannot run yet (S3)"), *LessonId.ToString());
	return false;
}

void ULessonSubsystem::LoadFromSaveGame(const UKiteSurfSaveGame& SaveGame)
{
	Progress.SetEntries(SaveGame.LessonProgress.GetEntries());
}

void ULessonSubsystem::WriteToSaveGame(UKiteSurfSaveGame& SaveGame) const
{
	SaveGame.LessonProgress = Progress;
}

void ULessonSubsystem::SaveProgress()
{
	if (!bWriteToDisk)
	{
		return;
	}
	if (UKiteSurfGameInstance* GameInstance = Cast<UKiteSurfGameInstance>(GetGameInstance()))
	{
		GameInstance->SaveSettingsToDisk();
	}
}
