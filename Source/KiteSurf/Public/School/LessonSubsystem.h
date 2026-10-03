#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "School/LessonProgress.h"
#include "LessonSubsystem.generated.h"

class UKiteSurfSaveGame;

/** One lesson as the lesson menu (S5) shows it: a tile on the chapter map. */
USTRUCT(BlueprintType)
struct KITESURF_API FLessonListItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "School")
	FName LessonId;

	UPROPERTY(BlueprintReadOnly, Category = "School")
	FText Title;

	UPROPERTY(BlueprintReadOnly, Category = "School")
	FName Chapter;

	/** Best stars, 0 to 3. */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	int32 Stars = 0;

	/** Not startable: a prerequisite has no star yet, or the lesson's feature is not built (LessonUnlock::IsUnlocked). */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	bool bLocked = true;

	/** The lesson's required feature is built (LessonCatalog::IsAvailable); false shows "coming later" rather than a prerequisite. */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	bool bAvailable = false;

	/** The "new" badge: unlocked and never attempted. */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	bool bNew = false;
};

/**
 * The kite school's progress (docs/tutorials.md 3.3, S2): owns the player's FLessonProgressBook,
 * answers which lessons are unlocked and which one Continue goes to, and records lesson results.
 *
 * Persistence goes through the game instance's existing save path: UKiteSurfGameInstance's
 * ApplySaveGame hands the loaded UKiteSurfSaveGame to LoadFromSaveGame, WriteToSaveGame asks
 * WriteToSaveGame, and RecordLessonResult and ResetProgress call SaveSettingsToDisk so a result is
 * on disk as soon as it is known. Exists only on a UKiteSurfGameInstance.
 *
 * Starting a lesson (opening the map with its set-up and running it) is S3's ALessonDirector;
 * StartLesson is a stub until then.
 */
UCLASS()
class KITESURF_API ULessonSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	const FLessonProgressBook& GetProgress() const { return Progress; }

	/**
	 * Counts a finished attempt (FLessonProgressBook::RecordAttempt, now, the lesson's pass metric)
	 * and writes the save. Stars from LessonEval::ComputeStars; Value in the pass metric's unit, NaN
	 * for none; bNoAssists when every assist was off. Lessons not in the catalogue are ignored.
	 * @return true when the attempt raised the lesson's best stars.
	 */
	UFUNCTION(BlueprintCallable, Category = "School")
	bool RecordLessonResult(FName LessonId, bool bPassed, int32 Stars, float Value, bool bNoAssists);

	/** LessonUnlock::IsUnlocked for a catalogue lesson; false for an id the catalogue does not have. */
	UFUNCTION(BlueprintPure, Category = "School")
	bool IsUnlocked(FName LessonId) const;

	/** Where Continue goes (LessonUnlock::RecommendedNext over the catalogue). */
	UFUNCTION(BlueprintPure, Category = "School")
	FName GetRecommendedNext() const;

	/** Share of a chapter's lessons passed, 0 to 1 (FLessonProgressBook::ChapterCompletion). */
	UFUNCTION(BlueprintPure, Category = "School")
	float GetChapterCompletion(FName Chapter) const;

	UFUNCTION(BlueprintPure, Category = "School")
	int32 GetTotalStars() const;

	/** Every catalogue lesson in chapter and number order, with its stars, lock state and badge. */
	UFUNCTION(BlueprintPure, Category = "School")
	TArray<FLessonListItem> GetLessonList() const;

	/** Forgets every lesson result and writes the save. The trick book is left as it is. */
	UFUNCTION(BlueprintCallable, Category = "School")
	void ResetProgress();

	/**
	 * S3 stub: starting a lesson is the lesson director's job (S3). Until then this only checks
	 * that the lesson exists and is unlocked, logs, and always returns false.
	 */
	UFUNCTION(BlueprintCallable, Category = "School")
	bool StartLesson(FName LessonId);

	/** Takes the progress from a loaded save (called by UKiteSurfGameInstance::ApplySaveGame). */
	void LoadFromSaveGame(const UKiteSurfSaveGame& SaveGame);

	/** Writes the progress into a save (called by UKiteSurfGameInstance::WriteToSaveGame). */
	void WriteToSaveGame(UKiteSurfSaveGame& SaveGame) const;

	/** Whether RecordLessonResult and ResetProgress write the Settings slot (on by default). Tests turn it off so the player's save is never written. */
	void SetWriteToDisk(bool bEnabled) { bWriteToDisk = bEnabled; }
	bool GetWriteToDisk() const { return bWriteToDisk; }

private:
	void SaveProgress();

	FLessonProgressBook Progress;
	bool bWriteToDisk = true;
};
