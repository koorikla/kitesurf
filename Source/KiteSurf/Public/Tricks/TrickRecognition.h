#pragma once

#include "CoreMinimal.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/TrickScoring.h"
#include "Tricks/TrickSignature.h"
#include "TrickRecognition.generated.h"

/** Thresholds that tell the kite loops apart (docs/tricks.md 6.6, docs/tricks/T2.md T2.4). All estimates, to be tuned once the loops fly true. */
USTRUCT(BlueprintType)
struct FLoopClassifySettings
{
	GENERATED_BODY()

	/** A megaloop starts with the rider at least this high above the take-off (cm)... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float MegaloopMinRiderHeightCm = 800.0f;

	/** ...takes the kite down to this elevation or lower (deg)... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float MegaloopMaxElevationDeg = 20.0f;

	/** ...and pulls at least this many body weights. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float MegaloopMinTensionBodyWeights = 3.0f;

	/** Rider and board mass for the body weight (kg); the board's default MassKg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float RiderMassKg = 85.0f;

	/**
	 * A loop started after the apex that keeps the kite at or above this elevation (deg)...
	 * Relative, not tricks.md's "55 deg or more", which today's kite cannot fly: a loop starts
	 * about 55 deg up at the window edge and drops some 20 deg (docs/tricks/T2.md section 0 item 1).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float HeliLoopMinElevationDeg = 40.0f;

	/** ...and drops no more than this below where it started (deg) is a heli loop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float HeliLoopMaxDropDeg = 25.0f;

	/** Completed kite or megaloops one way, each starting within this long of the last one ending (s), are one double or triple: they all take the chain's strongest kind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float ChainMaxGapSeconds = 1.0f;

	/** An S-loop half is an unfinished, uncrashed loop record of at least this turn (deg)... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float SLoopHalfMinDeg = 180.0f;

	/** ...starting within this long of the half before it ending (s), the other way. Two halves are an S-loop, three or more a snake loop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float SLoopMaxGapSeconds = 0.6f;

	/** A roll starting more than this before the loop's yank is early, more than this after it late (s); in between it is neither. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float RollTimingMarginSeconds = 0.1f;
};

/**
 * Builds a trick signature from what a jump record knows. Today that is the kite loops and the
 * landing: the rider is taken as hooked in and heelside, with no rotation. T1.6 replaces the rest
 * with the live tracker.
 */
namespace TrickRecognition
{
	/**
	 * The kind of one completed loop: heli loop (started after the apex, kite kept at or above
	 * HeliLoopMinElevationDeg and dropped no more than HeliLoopMaxDropDeg from its start), else
	 * megaloop (rider height, kite elevation and pull thresholds), otherwise kiteloop.
	 */
	KITESURF_API ETrickLoopKind ClassifyLoop(const FJumpLoop& Loop, const FLoopClassifySettings& Settings = FLoopClassifySettings());

	/**
	 * The loop turned against the rider's travel: Direction x RiderTravelSide < 0.
	 *
	 * Sign convention: Direction +1 is the kite turning to the rider's right (clockwise seen from
	 * the rider, KiteComponent's Right = Nose x Dir); RiderTravelSide +1 is riding along Up x
	 * Downwind, to the right looking downwind. Riding right, the right hand is the front hand, and
	 * pulling it turns the kite right, towards the travel: the natural loop. Not yet confirmed
	 * against footage (docs/tricks.md section 9). A rider with no travel side (0) is never contra.
	 */
	KITESURF_API bool IsContraLoop(const FKiteLoopRecord& Loop);

	/**
	 * The loops of one jump, in the order they started (docs/tricks/T2.md T2.4):
	 * - each completed loop gives one entry of its ClassifyLoop kind, contra from IsContraLoop;
	 * - completed kite and megaloops chained one way (ChainMaxGapSeconds) all take the chain's
	 *   strongest kind, so two give a double and three a triple when named; a heli loop never
	 *   chains;
	 * - unfinished halves in alternating directions (SLoopHalfMinDeg, SLoopMaxGapSeconds) give one
	 *   S-loop entry for two halves and one snake loop entry for three or more;
	 * - other unfinished or crashed records give nothing.
	 * Roll timing is left at None: rolls are not in the jump record yet (LoopRollTiming).
	 */
	KITESURF_API TArray<FTrickLoop> ClassifyLoops(const TArray<FJumpLoop>& Loops, const FLoopClassifySettings& Settings = FLoopClassifySettings());

	/**
	 * Early or late roll: when the roll started against the loop's tension peak, the yank
	 * (docs/tricks.md 6.6). Both times on one clock (s). Early when the roll starts at least
	 * RollTimingMarginSeconds before the yank, late when at least that long after, otherwise None.
	 */
	KITESURF_API ELoopRollTiming LoopRollTiming(float RollStartSeconds, float PeakTensionTimeSeconds,
		const FLoopClassifySettings& Settings = FLoopClassifySettings());

	/** The loop's yank on the jump's clock: time from take-off to the loop's peak tension (s). */
	KITESURF_API float PeakTensionSinceTakeoffSeconds(const FJumpLoop& Loop);

	/**
	 * Hooked, heelside, no rotation; the loops from ClassifyLoops; the grade from
	 * TrickScoring::GradeLanding on the record's landing facts.
	 */
	KITESURF_API FTrickSignature SignatureFromJump(const FJumpRecord& Record,
		const FLoopClassifySettings& Settings = FLoopClassifySettings(),
		const FLandingGradeSettings& GradeSettings = FLandingGradeSettings());
}
