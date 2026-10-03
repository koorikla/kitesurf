#pragma once

#include "CoreMinimal.h"
#include "Tricks/TrickTypes.h"
#include "TrickSignature.generated.h"

/** One kite loop as the trick sees it. */
USTRUCT(BlueprintType)
struct FTrickLoop
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	ETrickLoopKind Kind = ETrickLoopKind::Kiteloop;

	/** Looped against the natural direction (the kite dives away from the rider's travel). Direction to be checked against footage. */
	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	bool bContra = false;

	/** When a roll in this loop started relative to the loop's pull. */
	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	ELoopRollTiming RollTiming = ELoopRollTiming::None;
};

/** One grab: a hand and a zone of the board. */
USTRUCT(BlueprintType)
struct FTrickGrab
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	ETrickHand Hand = ETrickHand::Front;

	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	ETrickGrabZone Zone = ETrickGrabZone::Nose;

	/** How long it was held (s). Scored, but not part of the name or the family key. */
	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	float HoldSeconds = 0.0f;
};

/** One handle pass: the bar goes behind the back from one hand to the other. */
USTRUCT(BlueprintType)
struct FTrickPass
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	ETrickSense Sense = ETrickSense::Backside;

	/** Body rotation that carries the pass (deg); the number in the name (360 is "3"). Credited in multiples of 180. */
	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	int32 Degrees = 360;

	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	ETrickPassKind Kind = ETrickPassKind::Air;
};

/**
 * What a rider did in one jump, with the timings taken out: the input to TrickNaming and
 * TrickScoring. The recogniser builds it from the jump record (TrickRecognition), tests build it
 * by hand.
 *
 * Conventions:
 * - Body spin without a handle pass is SpinHalfTurns and SpinSense. A pass carries its own
 *   rotation in FTrickPass::Degrees, so a KGB 5 is one back roll plus a backside 540 pass, with
 *   SpinHalfTurns left at 0.
 * - "To blind" with the bar held behind the back is a spin plus a Blind landing, with no pass:
 *   back to blind is a back roll, a backside half turn and LandingStance Blind.
 */
USTRUCT(BlueprintType)
struct FTrickSignature
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	ETrickStance TakeoffStance = ETrickStance::Heelside;

	/** The board was ridden the other way round at take-off. */
	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	bool bSwitchTakeoff = false;

	/** Hooked into the harness for the whole jump. Unhooked, or any pass, names the jump as freestyle. */
	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	bool bHooked = true;

	/** Inversions in the order they happened. */
	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	TArray<ETrickInversion> Inversions;

	/** Body spin without a handle pass, in half turns (2 is a 360). */
	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	int32 SpinHalfTurns = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	ETrickSense SpinSense = ETrickSense::None;

	/** The line pull swung the body out horizontal and back under. */
	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	bool bRaley = false;

	/** A raley with an off-axis overhead rotation. The overhead turn is recorded as body spin. */
	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	bool bSBend = false;

	/** Kite loops in the order they were flown. */
	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	TArray<FTrickLoop> Loops;

	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	TArray<FTrickGrab> Grabs;

	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	bool bOneFooter = false;

	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	ETrickBoardOff BoardOff = ETrickBoardOff::None;

	/** How long the board was off the feet (s). Not part of the name or the family key. */
	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	float BoardOffSeconds = 0.0f;

	/** Hand-to-hand handle passes only, in order. */
	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	TArray<FTrickPass> Passes;

	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	ETrickStance LandingStance = ETrickStance::Heelside;

	/** Not part of the name or the family key. */
	UPROPERTY(BlueprintReadWrite, Category = "Tricks")
	ELandingGrade Grade = ELandingGrade::Clean;
};
