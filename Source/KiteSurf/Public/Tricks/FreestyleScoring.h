#pragma once

#include "CoreMinimal.h"
#include "Tricks/LandingEvaluator.h"
#include "Tricks/TrickScoring.h"
#include "Tricks/TrickSignature.h"
#include "Tricks/TrickTypes.h"
#include "FreestyleScoring.generated.h"

/**
 * GKA-style freestyle scoring (docs/tricks.md 3.6, docs/tricks/T3.md T3.6). Pure functions; not
 * yet used in the game (the heat flow in the game mode comes later).
 */

/** One trick of a heat: its family and its 0.1..10 score (0 for a crash). */
USTRUCT(BlueprintType)
struct FScoredTrick
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks")
	EGkaFamily Family = EGkaFamily::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks")
	float Score = 0.0f;

	/** For the HUD's counting list; not used by the scoring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks")
	FString Name;
};

/** How a freestyle heat counts (GKA 2025, men; docs/tricks.md 3.6). */
USTRUCT(BlueprintType)
struct FFreestyleHeatRules
{
	GENERATED_BODY()

	/** Tricks that count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	int32 Counting = 4;

	/** At most this many of them from the heelside group... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	int32 MaxHeelside = 2;

	/** ...and this many from the variety group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	int32 MaxVariety = 3;

	/** Variety bonus by the number of families counted (index); past the end the last entry holds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	TArray<float> VarietyBonus = { 0.0f, 1.0f, 2.0f, 4.0f, 7.0f };

	/** Only the first this many tricks of the list are attempts; 0 or less counts them all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	int32 Attempts = 7;
};

/** A heat's result. */
USTRUCT(BlueprintType)
struct FHeatResult
{
	GENERATED_BODY()

	/** Counting trick scores plus the variety bonus. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float Total = 0.0f;

	/** The counting trick scores alone. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float TrickTotal = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float VarietyBonus = 0.0f;

	/** Indices into the input list of the counting tricks, best score first. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	TArray<int32> CountingIdx;

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	int32 HeelsideCounted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	int32 VarietyCounted = 0;
};

namespace FreestyleScoring
{
	/** The group a family belongs to: raley-based, KGB and slim, hinter and heart, mobes are heelside; the rest variety. */
	KITESURF_API EGkaGroup GroupOf(EGkaFamily Family);

	/**
	 * One trick's score: clamp(2 x difficulty x execution x clamp(0.8 + 0.1 x ApexM, 0.8, 1.2),
	 * 0.1, 10), and exactly 0 for a crash. Difficulty from TrickNaming::FreestyleFamily, execution
	 * from TrickScoring::ExecutionFactor(Verdict.Grade).
	 */
	KITESURF_API float FreestyleTrickScore(const FTrickSignature& Signature, const FLandingVerdict& Verdict, float ApexM,
		const FTrickScoringSettings& Settings = FTrickScoringSettings());

	/** The trick ready for a heat: family, score and name. */
	KITESURF_API FScoredTrick MakeScoredTrick(const FTrickSignature& Signature, const FLandingVerdict& Verdict, float ApexM,
		const FTrickScoringSettings& Settings = FTrickScoringSettings());

	/**
	 * Scores a heat (T3.6): the best score in each family (crashes, scores of 0 and family None do
	 * not count; ties keep the earlier trick); then, over h = 0..MaxHeelside heelside tricks with
	 * v = min(Counting - h, MaxVariety) variety tricks, the best sum of the top h and top v; then
	 * the variety bonus for the number of families counted (1, 2, 4 or 7 for 1 to 4).
	 */
	KITESURF_API FHeatResult ScoreFreestyleHeat(TConstArrayView<FScoredTrick> Tricks,
		const FFreestyleHeatRules& Rules = FFreestyleHeatRules());
}
