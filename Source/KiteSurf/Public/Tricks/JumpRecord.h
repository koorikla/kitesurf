#pragma once

#include "CoreMinimal.h"
#include "Tricks/KiteLoopRecord.h"
#include "Tricks/TrickTypes.h"
#include "JumpRecord.generated.h"

/** How a jump ended. */
UENUM(BlueprintType)
enum class EJumpOutcome : uint8
{
	Landed  UMETA(DisplayName = "Landed"),
	Crashed UMETA(DisplayName = "Crashed")
};

/** A kite loop flown during a jump, placed in the jump's time line. */
USTRUCT(BlueprintType)
struct FJumpLoop
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	FKiteLoopRecord Loop;

	/** Time from take-off to the loop's start (s). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float StartSinceTakeoffSeconds = 0.0f;

	/** Time from the apex to the loop's start (s); negative when the loop started on the way up. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float StartSinceApexSeconds = 0.0f;

	/** Rider height above the take-off point when the loop started (cm): Loop.RiderZAtStartCm - TakeoffLocation.Z. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float RiderHeightAtStartCm = 0.0f;
};

/**
 * Everything kept about one jump, from take-off to landing or crash. Times are board simulation
 * time. The recorder (T0.2) fills the facts and the rider's rotation (T1.6, FRotationRecognizer);
 * the trick fields at the end of the jump come from TrickRecognition, TrickNaming and TrickScoring.
 */
USTRUCT(BlueprintType)
struct FJumpRecord
{
	GENERATED_BODY()

	/** Goes up by one per jump in a session; -1 until recorded. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	int32 Index = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	EJumpOutcome Outcome = EJumpOutcome::Landed;

	/** Popped off the water, rather than lifted off by the kite. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	bool bPopped = false;

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float TakeoffTimeSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float ApexTimeSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float LandingTimeSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float AirtimeSeconds = 0.0f;

	/** World position at take-off (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	FVector TakeoffLocation = FVector::ZeroVector;

	/** World position at landing (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	FVector LandingLocation = FVector::ZeroVector;

	/** Horizontal speed at take-off (cm/s). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float TakeoffSpeedCmS = 0.0f;

	/** Highest point above the take-off point (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float ApexHeightCm = 0.0f;

	/** Distance over the water from take-off to landing (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float DistanceCm = 0.0f;

	/** Downward speed at contact (cm/s, positive). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float SinkRateCmS = 0.0f;

	/** Deceleration of the landing (g). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float LandingG = 1.0f;

	/** Angle between the board and its velocity at contact, either end (deg). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float LandingYawDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float KiteElevationAtLandingDeg = 0.0f;

	/** Line tension at take-off (N). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float TakeoffTensionN = 0.0f;

	/** Highest line tension in the air (N). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float PeakTensionN = 0.0f;

	/** Lowest kite elevation in the air (deg). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float MinKiteElevationDeg = 90.0f;

	/** Kite loops that overlap the jump, in the order they started. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	TArray<FJumpLoop> Loops;

	// --- Rider rotation (T1.6): FRotationRecognizer over the rider attitude. All defaults when the attitude was not live. ---

	/** The rider attitude was simulated in the air, so the rotation fields below were measured. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	bool bRotationTracked = false;

	/** Inversions in the order they were counted. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	TArray<ETrickInversion> Inversions;

	/** Body spin credited, in half turns (2 is a 360). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	int32 SpinHalfTurns = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	ETrickSense SpinSense = ETrickSense::None;

	/** Rotation about world up the spin is credited from, less the flight's own turn (deg, signed). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float SpinDeg = 0.0f;

	/** Heelside, or Blind or Toeside when the rider landed facing the other way. Heelside in the air. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	ETrickStance LandingStance = ETrickStance::Heelside;

	/** Net heading at touchdown against the take-off's, less the flight's turn (deg, -180..180). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float NetHeadingDeg = 0.0f;

	/** When the first inversion's rotation started, from take-off (s); negative when nothing inverted. Feeds TrickRecognition::LoopRollTiming. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float RollStartSinceTakeoffSeconds = -1.0f;

	/** Why the board's landing verdict graded the landing down (UBoardMovementComponent::GetLastLandingVerdict); None for a good landing. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	ELandingCause LandingCause = ELandingCause::None;

	/** TrickNaming::Name of the jump's signature. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	FString TrickName;

	/** TrickNaming::FamilyKey of the jump's signature: what counts as a repeat. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	FString FamilyKey;

	/**
	 * The board's landing verdict's grade (TrickScoring::GradeFromVerdict; docs/tricks/README.md
	 * decision 6), Crash for a crashed jump. Records built without a board use TrickScoring::GradeLanding.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	ELandingGrade Grade = ELandingGrade::Clean;

	/** TrickScoring::ScoreJump, before the repeat factor. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	FTrickScore Score;

	/** Share of Score.Total the session paid for this jump: 1 the first time a family is landed, less for repeats. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float RepeatFactor = 1.0f;

	/** Loops in this jump that reached 360. */
	int32 CountCompletedLoops() const
	{
		int32 Count = 0;
		for (const FJumpLoop& JumpLoop : Loops)
		{
			Count += JumpLoop.Loop.bCompleted ? 1 : 0;
		}
		return Count;
	}
};
