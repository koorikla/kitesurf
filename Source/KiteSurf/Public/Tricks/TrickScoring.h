#pragma once

#include "CoreMinimal.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/TrickSignature.h"
#include "Tricks/TrickTypes.h"
#include "TrickScoring.generated.h"

/** Weights for TrickScoring::ScoreJump (docs/tricks.md 6.8). Every value is an estimate. */
USTRUCT(BlueprintType)
struct FTrickScoringSettings
{
	GENERATED_BODY()

	/** Height score is the apex in metres to this power. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float HeightExponent = 1.15f;

	/** Extremity per completed kite loop, before lowness and lateness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float LoopExtremity = 0.5f;

	/** Kite elevation at which a loop counts as not low at all (deg): lowness is 1 - min elevation / this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float LownessRefDeg = 60.0f;

	/** Extra extremity, once, for any contra loop, S-loop or snake loop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float ContraOrSLoopBonus = 0.3f;

	/** Technicality per inversion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float Inversion = 0.4f;

	/** Technicality per half turn of spin, body spin and pass rotation together. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float SpinPer180 = 0.15f;

	/** Technicality for a grab held at least GrabMinHoldSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float GrabBase = 0.2f;

	/** More for a grab held to GrabFullHoldSeconds, growing linearly from GrabMinHoldSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float GrabHoldBonusMax = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float GrabMinHoldSeconds = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float GrabFullHoldSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float OneFooter = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float BoardOff = 0.6f;

	/** Technicality per handle pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float Pass = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float Unhooked = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float BlindOrToesideLanding = 0.2f;

	/** Execution factor per landing grade. A crash scores nothing, as in real judging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float ExecutionStomped = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float ExecutionClean = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float ExecutionSketchy = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float ExecutionCrash = 0.0f;

	/** Share paid for the 1st, 2nd, ... landing of the same family key; the last entry holds from then on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	TArray<float> RepeatFactors = { 1.0f, 0.75f, 0.5f, 0.25f, 0.1f };
};

/**
 * Thresholds for TrickScoring::GradeLanding. Estimates. GradeLanding maps them onto
 * FLandingThresholds (Tricks/LandingEvaluator.h) and leaves the evaluator's other limits at their
 * defaults.
 */
USTRUCT(BlueprintType)
struct FLandingGradeSettings
{
	GENERATED_BODY()

	/** Stomped: board within this of its velocity (deg)... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float StompedMaxYawDeg = 20.0f;

	/** ...landing no harder than this (g)... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float StompedMaxG = 4.0f;

	/** ...and the kite at or above this; under it the landing is hot, at best sketchy (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float HotKiteElevationDeg = 45.0f;

	/** Harder than this is sketchy (g). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float SketchyMinG = 8.0f;
};

/**
 * Scores a jump per docs/tricks.md 6.8: Height x (1 + Extremity) x (1 + Technicality) x
 * Execution, with repeats in a session paid less. Pure functions over FJumpRecord and
 * FTrickSignature.
 */
namespace TrickScoring
{
	/**
	 * A jump record's grade from the board's landing verdict (LandingEvaluator::Evaluate through
	 * UBoardMovementComponent::GetLastLandingVerdict), which owns the grade (docs/tricks/README.md
	 * decision 6): Crash when the board crashed; otherwise the verdict's grade, with a Crash verdict
	 * the board rode away from (it never does: the board crashes on every Crash verdict) reported as
	 * Sketchy, as GradeLanding does.
	 */
	KITESURF_API ELandingGrade GradeFromVerdict(ELandingGrade VerdictGrade, bool bCrashed);

	/**
	 * The record-only shortcut over LandingEvaluator::Evaluate, for a jump record that has the
	 * yaw, g and kite elevation but no tilt or rider state; Evaluate is the full verdict. The jump
	 * recorder uses it only when it has no board verdict (pure tests that build records by hand);
	 * on a ride the record's grade is GradeFromVerdict. It does not read the sink, so it can call a
	 * hot landing (sink over 6 m/s) clean where the verdict says sketchy.
	 * Crash when the board crashed. Otherwise Evaluate's grade with tilt 0 and Settings mapped
	 * onto FLandingThresholds, with a crash there reported as Sketchy (the board decides crashes):
	 * Stomped when the yaw (folded, so switch counts), g and kite elevation are all inside the
	 * stomped limits (inclusive); Sketchy when the kite is under HotKiteElevationDeg, the landing
	 * is harder than SketchyMinG or the yaw is over the clean limit (45 deg); Clean otherwise.
	 */
	KITESURF_API ELandingGrade GradeLanding(float YawDeg, float LandingG, float KiteElevationDeg, bool bCrashed,
		const FLandingGradeSettings& Settings = FLandingGradeSettings());

	/** Execution factor for a grade. */
	KITESURF_API float ExecutionFactor(ELandingGrade Grade, const FTrickScoringSettings& Settings = FTrickScoringSettings());

	/**
	 * Height from Record.ApexHeightCm; extremity from Record.Loops (completed loops only) and the
	 * signature's contra or S-loops; technicality from the signature's elements; execution from
	 * Signature.Grade, and 0 when Record.Outcome is Crashed. A crash totals exactly 0.
	 */
	KITESURF_API FTrickScore ScoreJump(const FJumpRecord& Record, const FTrickSignature& Signature,
		const FTrickScoringSettings& Settings = FTrickScoringSettings());

	/** Share paid for a trick already landed PriorCount times this session: 1, 0.75, 0.5, 0.25, then 0.1. */
	KITESURF_API float RepeatFactor(int32 PriorCount, const FTrickScoringSettings& Settings = FTrickScoringSettings());
}

/** Counts landings per family key over a session, so repeats are paid less. */
USTRUCT(BlueprintType)
struct KITESURF_API FTrickSession
{
	GENERATED_BODY()

	/** Landings so far per TrickNaming::FamilyKey. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	TMap<FString, int32> Counts;

	/** Counts one more of this family and returns RawTotal times its repeat factor. */
	float Add(const FString& FamilyKey, float RawTotal, const FTrickScoringSettings& Settings = FTrickScoringSettings());

	/** The factor the next landing of this family would get. */
	float NextRepeatFactor(const FString& FamilyKey, const FTrickScoringSettings& Settings = FTrickScoringSettings()) const;

	int32 GetCount(const FString& FamilyKey) const;

	void Reset() { Counts.Reset(); }
};
