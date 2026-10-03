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
 * time. The recorder (T0.2) fills the facts; the tracker fills the trick fields at the end of the
 * jump from TrickRecognition, TrickNaming and TrickScoring.
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

	/** TrickNaming::Name of the jump's signature. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	FString TrickName;

	/** TrickNaming::FamilyKey of the jump's signature: what counts as a repeat. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	FString FamilyKey;

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
