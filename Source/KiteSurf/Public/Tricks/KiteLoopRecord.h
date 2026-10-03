#pragma once

#include "CoreMinimal.h"
#include "KiteLoopRecord.generated.h"

/**
 * One kite loop, or a long part of one, as the kite flew it. Kept by the kite's loop tracker
 * (T0.3) from its own heading turn, independent of IsLooping(). The kite knows nothing about
 * jumps: times relative to take-off and apex are in FJumpLoop.
 */
USTRUCT(BlueprintType)
struct FKiteLoopRecord
{
	GENERATED_BODY()

	/** Goes up by one per record on a kite; -1 until recorded. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	int32 Index = -1;

	/** +1 right (clockwise seen from the rider, the same sign as UKiteComponent::GetTurnDeg), -1 left. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	int32 Direction = 0;

	/** Kite simulation time the loop started at (s). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float StartTimeSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float DurationSeconds = 0.0f;

	/** Turn reached in this record (deg, a magnitude); 360 completes a loop. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float TurnDeg = 0.0f;

	/** The turn reached 360. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	bool bCompleted = false;

	/** The kite hit the water during the loop. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	bool bKiteCrashed = false;

	/** Kite elevation when the loop started (deg). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float StartElevationDeg = 0.0f;

	/** Lowest kite elevation during the loop (deg). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float MinElevationDeg = 90.0f;

	/** Highest line tension during the loop (N). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float PeakTensionN = 0.0f;

	/** Kite simulation time of the highest line tension, the loop's yank (s); tells an early roll from a late one. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float PeakTensionTimeSeconds = 0.0f;

	/** Rider height above the water when the loop started (cm, world Z). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float RiderZAtStartCm = 0.0f;

	/**
	 * Which way the rider was travelling when the loop started, for telling a contra loop: +1 along
	 * crosswind-right (Up x Downwind, to the right looking downwind), -1 to the left, 0 when the
	 * rider was going too nearly straight up- or downwind to say (FKiteLoopTrackerSettings::TravelSideMinDot).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	int32 RiderTravelSide = 0;
};
