#pragma once

#include "CoreMinimal.h"
#include "School/LessonTypes.h"
#include "LessonProgress.generated.h"

/**
 * The player's kite school progress (docs/tutorials.md 3.1 and S2): one FLessonRecord per lesson
 * played, held in an FLessonProgressBook, plus the unlock graph and the recommended next lesson
 * (LessonUnlock). Pure: ULessonSubsystem owns the book and saves it in UKiteSurfSaveGame. The trick
 * book is separate and nothing here touches it.
 */

/** The player's results on one lesson. Saved with the settings (UKiteSurfSaveGame::LessonProgress). */
USTRUCT(BlueprintType)
struct KITESURF_API FLessonRecord
{
	GENERATED_BODY()

	/** FLessonDef::Id ("B3"). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "School")
	FName LessonId;

	/** Best stars on a pass, 0 (never passed) to 3. Only goes up. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "School")
	int32 BestStars = 0;

	/** Whether BestValue holds anything: false until an attempt reports a value. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "School")
	bool bHasBestValue = false;

	/**
	 * The best value of BestValueMetric reported on any attempt, in that metric's unit (the units
	 * of ELessonMetric: m, s, m/s, deg, a count, or an enum index such as the landing grade).
	 * "Best" is the highest, or the lowest for LessonProgress::IsLowerBetter metrics.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "School")
	float BestValue = 0.0f;

	/** What BestValue measures: the lesson's pass objective metric unless the caller said otherwise. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "School")
	ELessonMetric BestValueMetric = ELessonMetric::None;

	/** Every finished attempt, passed or not. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "School")
	int32 Attempts = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "School")
	int32 Passes = 0;

	/** Wall clock (UTC) of the first pass; the default (ticks 0) while Passes is 0. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "School")
	FDateTime FirstPassedUtc;

	/** Wall clock (UTC) of the newest attempt. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "School")
	FDateTime LastPlayedUtc;

	/** Passed at least once with every assist off. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "School")
	bool bPassedNoAssists = false;

	bool HasPassed() const { return Passes > 0; }
};

/**
 * Per-lesson progress. Records are kept in the order the lessons were first played. A lesson the
 * catalogue no longer has keeps its record (so a later build that brings it back finds it) but
 * counts nowhere except TotalStars.
 */
USTRUCT(BlueprintType)
struct KITESURF_API FLessonProgressBook
{
	GENERATED_BODY()

	static constexpr int32 MaxStars = 3;

	/**
	 * Counts a finished attempt on a lesson. Attempts goes up by one and LastPlayedUtc becomes
	 * NowUtc. A pass also counts in Passes, sets FirstPassedUtc the first time, raises BestStars
	 * (clamped to 1..3: a pass is always worth at least one star) and sets bPassedNoAssists when
	 * bNoAssists. A failed attempt earns no stars whatever Stars says. Value (in ValueMetric's
	 * unit; NaN or infinite for "no value") replaces BestValue when it is better, on passes and
	 * failures alike; ValueMetric None means the lesson's pass objective metric from
	 * LessonCatalog (higher is better when the lesson is not in the catalogue). An id of None
	 * changes nothing.
	 * @return true when the attempt raised BestStars (the first pass always does).
	 */
	bool RecordAttempt(FName LessonId, bool bPassed, int32 Stars, float Value, bool bNoAssists, const FDateTime& NowUtc,
		ELessonMetric ValueMetric = ELessonMetric::None);

	/** The record for a lesson, or null when it has never been played. */
	const FLessonRecord* Find(FName LessonId) const;

	/** Best stars on a lesson, 0 when it has never been passed. */
	int32 GetStars(FName LessonId) const;

	/** Sum of the best stars of every record. */
	int32 TotalStars() const;

	/**
	 * Share of a chapter's lessons passed (at least one star), 0 to 1, over the lessons given
	 * (the catalogue by default). Lessons that need a feature not yet built count too, so a
	 * chapter with one reaches 1 only once that feature exists. 0 for a chapter with no lessons.
	 */
	float ChapterCompletion(FName Chapter) const;
	float ChapterCompletion(FName Chapter, const TArray<FLessonDef>& Lessons) const;

	/** Forgets every lesson. The trick book is not part of this book and is not touched. */
	void Reset() { Records.Reset(); }

	int32 Num() const { return Records.Num(); }

	/** Every record, first played first. */
	const TArray<FLessonRecord>& GetEntries() const { return Records; }

	/**
	 * Replaces the book, for loading. Records with no id or an id already seen are dropped; stars
	 * are clamped to 0..3, counts to at least 0, attempts to at least the passes, and a record with
	 * no passes has no stars and no no-assists pass.
	 */
	void SetEntries(const TArray<FLessonRecord>& InEntries);

private:
	int32 IndexOf(FName LessonId) const;

	UPROPERTY(SaveGame)
	TArray<FLessonRecord> Records;
};

namespace LessonProgress
{
	/** True for metrics where a smaller value is the better result: landing grade (Stomped is 0), sink, landing g, time to planing, time not planing, loop duration. */
	KITESURF_API bool IsLowerBetter(ELessonMetric Metric);
}

/**
 * The unlock graph and the Continue rule of docs/tutorials.md 3.1, over the lesson catalogue.
 */
namespace LessonUnlock
{
	/**
	 * A lesson can be started when every lesson in FLessonDef::Requires has at least one star in
	 * the book and its required feature is built (LessonCatalog::IsAvailable). A prerequisite
	 * the book has never seen counts as zero stars.
	 */
	KITESURF_API bool IsUnlocked(const FLessonDef& Lesson, const FLessonProgressBook& Book);

	/**
	 * The lesson Continue goes to: the first unlocked lesson without a star, in the order of the
	 * lessons given (the catalogue's chapter and number order: the lowest-numbered); when every
	 * unlocked lesson has a star, the unlocked lesson with the fewest stars, the earliest one on a
	 * tie (so with everything at three stars it is the first unlocked lesson). None when no lesson
	 * is unlocked.
	 */
	KITESURF_API FName RecommendedNext(const FLessonProgressBook& Book);
	KITESURF_API FName RecommendedNext(const FLessonProgressBook& Book, const TArray<FLessonDef>& Lessons);
}
