#pragma once

#include "CoreMinimal.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/TrickScoring.h"
#include "Tricks/TrickSignature.h"
#include "TrickRecognition.generated.h"

/** Thresholds that tell the kite loops apart (docs/tricks.md 6.6). All estimates, to be tuned once the loops fly true. */
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

	/** A loop started after the apex that keeps the kite at or above this elevation is a heli loop (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float HeliLoopMinElevationDeg = 55.0f;
};

/**
 * Builds a trick signature from what a jump record knows. In T0 that is the kite loops and the
 * landing: the rider is taken as hooked in and heelside, with no rotation. T1.6 and T2.4 replace
 * this with the live tracker and the full loop classifier.
 */
namespace TrickRecognition
{
	/** The kind of one completed loop: heli loop, megaloop, otherwise kiteloop. */
	KITESURF_API ETrickLoopKind ClassifyLoop(const FJumpLoop& Loop, const FLoopClassifySettings& Settings = FLoopClassifySettings());

	/**
	 * Hooked, heelside, no rotation; one FTrickLoop per completed loop in the record (contra when
	 * the loop turned against the rider's travel, an estimate until checked against footage); the
	 * grade from TrickScoring::GradeLanding on the record's landing facts.
	 */
	KITESURF_API FTrickSignature SignatureFromJump(const FJumpRecord& Record,
		const FLoopClassifySettings& Settings = FLoopClassifySettings(),
		const FLandingGradeSettings& GradeSettings = FLandingGradeSettings());
}
