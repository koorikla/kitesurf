#pragma once

#include "CoreMinimal.h"
#include "Tricks/TrickTypes.h"
#include "TrickBook.generated.h"

struct FJumpRecord;

/**
 * Which kind of board a trick was landed on. Twin-tip and foil records are kept apart
 * (docs/research.md G4), so the category is part of a trick book entry's key. There is no foil
 * in the game yet: everything lands as TwinTip until one is added.
 */
UENUM(BlueprintType)
enum class ETrickBoardCategory : uint8
{
	TwinTip UMETA(DisplayName = "Twin-tip"),
	Foil    UMETA(DisplayName = "Foil")
};

/** One trick the player has landed, with their bests on it. Saved with the settings (UKiteSurfSaveGame::TrickBook). */
USTRUCT(BlueprintType)
struct KITESURF_API FTrickBookEntry
{
	GENERATED_BODY()

	/** TrickNaming::FamilyKey of the trick: what counts as the same trick. With BoardCategory, the entry's key. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Tricks")
	FString FamilyKey;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Tricks")
	ETrickBoardCategory BoardCategory = ETrickBoardCategory::TwinTip;

	/** TrickNaming::Name as it was the first time the trick was landed. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Tricks")
	FString DisplayName;

	/** Wall clock (UTC) of the first landing. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Tricks")
	FDateTime FirstLandedUtc;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Tricks")
	int32 TimesLanded = 0;

	/** Highest FJumpRecord::Score.Total on a landing of this trick. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Tricks")
	float BestScore = 0.0f;

	/** Highest apex above take-off on a landing of this trick (m). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Tricks")
	float BestHeightM = 0.0f;

	/** Best landing grade (Stomped is best). Crash only while TimesLanded is 0. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Tricks")
	ELandingGrade BestGrade = ELandingGrade::Crash;
};

/**
 * The tricks the player has landed (T2.7, docs/tricks/T2.md). A trick unlocks on its first
 * landing; a crash never unlocks or improves anything. Entries are kept in the order they were
 * first landed. Pure: the game instance owns one and saves it with the settings. Not yet fed by
 * the game: the trick tracker calls UKiteSurfGameInstance::RecordTrickLanding once it is wired.
 */
USTRUCT(BlueprintType)
struct KITESURF_API FTrickBook
{
	GENERATED_BODY()

	/**
	 * Counts a finished jump. Crashes (Outcome Crashed or Grade Crash) and records without a
	 * FamilyKey change nothing. Returns true when this was the first landing of the trick on that
	 * board, which adds an entry; a repeat adds one to TimesLanded and raises the bests.
	 */
	bool RecordLanding(const FJumpRecord& Record, ETrickBoardCategory Board = ETrickBoardCategory::TwinTip);

	/** RecordLanding with the time of the landing given, for tests and replays. */
	bool RecordLanding(const FJumpRecord& Record, ETrickBoardCategory Board, const FDateTime& NowUtc);

	/** The entry for a trick on a board, or null when it has not been landed. */
	const FTrickBookEntry* Find(const FString& FamilyKey, ETrickBoardCategory Board = ETrickBoardCategory::TwinTip) const;

	int32 Num() const { return Entries.Num(); }

	/** Every entry, first landed first. */
	const TArray<FTrickBookEntry>& GetEntries() const { return Entries; }

	/**
	 * Replaces the book, for loading. Entries with an empty key, no landings or a key already seen
	 * are dropped; the rest are put in first-landed order (stable, so equal times keep their order).
	 */
	void SetEntries(const TArray<FTrickBookEntry>& InEntries);

	void Reset() { Entries.Reset(); }

	/** True when a grade is better than another: Stomped beats Clean beats Sketchy beats Crash. */
	static bool IsBetterGrade(ELandingGrade Grade, ELandingGrade Than) { return static_cast<uint8>(Grade) < static_cast<uint8>(Than); }

private:
	int32 IndexOf(const FString& FamilyKey, ETrickBoardCategory Board) const;

	UPROPERTY(SaveGame)
	TArray<FTrickBookEntry> Entries;
};
