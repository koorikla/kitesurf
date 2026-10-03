#pragma once

#include "CoreMinimal.h"
#include "Tricks/TrickTypes.h"
#include "LandingEvaluator.generated.h"

/**
 * The landing evaluator (docs/tricks.md 6.7, docs/tricks/T1.md T1.5): grades a touchdown as
 * stomped, clean, sketchy or crash and names the cause. Pure functions over plain structs.
 *
 * Not yet called by the board: UBoardMovementComponent still uses its single angle test. The
 * wiring PR builds FLandingInputs with LandingEvaluator::ComputeGeometry plus the board's landing
 * g, sink and hot-landing flag (physics phase 2 item 4) and the kite's elevation.
 *
 * TrickScoring::GradeLanding is the record-only shortcut over this evaluator: it has no tilt or
 * rider state, and it leaves the crash decision to the board. Evaluate is the full verdict.
 */

/** What the evaluator knows about one touchdown. Angles in degrees, speeds in m/s. */
USTRUCT(BlueprintType)
struct FLandingInputs
{
	GENERATED_BODY()

	/** Board up against the water normal (deg, 0 flat, 90 on its rail, 180 upside down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks")
	float TiltDeg = 0.0f;

	/**
	 * Board heading against the velocity along the water (deg). Either end counts, so a switch
	 * landing is valid: any value is folded to 0..90 by LandingEvaluator::FoldYawDeg.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks")
	float YawOffVelocityDeg = 0.0f;

	/** Speed into the water at contact, positive downwards (m/s); phase 2's relative sink. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks")
	float SinkMS = 0.0f;

	/** The board's landing g (phase 2 item 4: 1 + v_rel^2 / (2 g s)). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks")
	float LandingG = 1.0f;

	/** The kite's elevation above the horizon at contact (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks")
	float KiteElevationDeg = 90.0f;

	/** The board's own hot-landing flag (phase 2 WasLastLandingHot): at best sketchy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks")
	bool bHotLanding = false;

	/** dot(rider Up, world Up) at contact: 1 upright, below FLandingThresholds::InvertedBodyUpDot inverted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks")
	float BodyUpDot = 1.0f;

	/**
	 * The remaining rotation along the spin: dot(error axis, spin axis), both unit. Positive when
	 * the rotation stopped short (under-rotated), negative when it went past (over-rotated), 0
	 * when there was no rotation (counted as under-rotated). Only the sign is used. Same
	 * convention as FAttitudeDebug::ErrorAlongSpin, which the wiring may pass instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks")
	float ErrorAlongSpin = 0.0f;

	/** False when the board is off the feet at contact (board-off not caught). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks")
	bool bBoardAttached = true;

	/** False when the rider has let go of the bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks")
	bool bBarInHands = true;

	/** True when a handle pass is still between the hands at contact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks")
	bool bPassInProgress = false;
};

/** The worst tilt and yaw a grade allows (deg, inclusive). */
USTRUCT(BlueprintType)
struct FLandingGradeLimits
{
	GENERATED_BODY()

	FLandingGradeLimits() = default;
	FLandingGradeLimits(float InMaxTiltDeg, float InMaxYawDeg) : MaxTiltDeg(InMaxTiltDeg), MaxYawDeg(InMaxYawDeg) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	float MaxTiltDeg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	float MaxYawDeg = 0.0f;
};

/** Thresholds for LandingEvaluator::Evaluate (docs/tricks.md 6.7). Every value is an estimate. */
USTRUCT(BlueprintType)
struct FLandingThresholds
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	FLandingGradeLimits Stomped = FLandingGradeLimits(15.0f, 20.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	FLandingGradeLimits Clean = FLandingGradeLimits(30.0f, 45.0f);

	/** Beyond these the landing is a crash: over- or under-rotated past the tilt, sideways past the yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	FLandingGradeLimits Sketchy = FLandingGradeLimits(50.0f, 75.0f);

	/** Stomped needs the kite at least this high (deg)... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	float StompedMinKiteElevationDeg = 45.0f;

	/** ...and a landing no harder than this (g). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	float StompedMaxLandingG = 4.0f;

	/** A kite under this is a hot landing: at best sketchy, cause KiteTooLow (deg; phase 2 HotLandingKiteElevationDeg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	float HotLandingKiteElevationDeg = 45.0f;

	/** Sinking faster than this is a hot landing: at best sketchy, cause TooHard (m/s; phase 2 HotLandingSinkMS). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	float HotLandingSinkMS = 6.0f;

	/** Harder than this is at best sketchy, cause TooHard (g; T0's SketchyMinG). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	float SketchyMinLandingG = 8.0f;

	/** Harder than this is a crash, cause TooHard (g). Phase 2 sets the board's CrashLandingG to 8 once its g is real. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	float CrashLandingG = 10.0f;

	/** BodyUpDot under this is an inverted crash (0: rider Up below the horizon). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	float InvertedBodyUpDot = 0.0f;

	/** Share of the horizontal speed kept, per grade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	float SpeedRetentionStomped = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	float SpeedRetentionClean = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	float SpeedRetentionSketchy = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Landing")
	float SpeedRetentionCrash = 0.0f;

	/** The speed retention for a grade. */
	float SpeedRetentionFor(ELandingGrade Grade) const;
};

/** The grade, why it was not better, and how much horizontal speed the rider keeps. */
USTRUCT(BlueprintType)
struct FLandingVerdict
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	ELandingGrade Grade = ELandingGrade::Clean;

	/** None for stomped and clean, and for a sketchy landing graded down by tilt or yaw alone. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	ELandingCause Cause = ELandingCause::None;

	/** Share of the horizontal speed kept (0 for a crash). */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float SpeedRetention = 0.8f;
};

/** The geometric inputs of a landing, from LandingEvaluator::ComputeGeometry. */
USTRUCT(BlueprintType)
struct FLandingGeometry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float TiltDeg = 0.0f;

	/** Already folded to 0..90. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float YawOffVelocityDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float BodyUpDot = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float ErrorAlongSpin = 0.0f;
};

namespace LandingEvaluator
{
	/**
	 * Grades a landing. In order, the first that applies:
	 * - board off, bar lost, pass unfinished, inverted (BodyUpDot under InvertedBodyUpDot) and
	 *   landing g over CrashLandingG are crashes with those causes;
	 * - tilt over Sketchy.MaxTiltDeg is a crash, UnderRotated when ErrorAlongSpin >= 0, otherwise
	 *   OverRotated; yaw (folded) over Sketchy.MaxYawDeg is a crash, Sideways;
	 * - otherwise the grade is the best whose tilt and yaw limits both hold (inclusive). Stomped
	 *   also needs the kite at StompedMinKiteElevationDeg or higher and g at StompedMaxLandingG
	 *   or lower, else it is clean. A hot landing (kite under HotLandingKiteElevationDeg, sink over
	 *   HotLandingSinkMS or bHotLanding) or g over SketchyMinLandingG is at best sketchy, with
	 *   cause KiteTooLow (kite low or the board's hot flag alone) or TooHard (sink or g).
	 * A non-finite tilt, yaw or g fails its limit.
	 */
	KITESURF_API FLandingVerdict Evaluate(const FLandingInputs& Inputs, const FLandingThresholds& Thresholds = FLandingThresholds());

	/** Folds any yaw (deg) to 0..90, either end of the board: 175, 185, -5 and 355 all give 5. */
	KITESURF_API float FoldYawDeg(float YawDeg);

	/**
	 * The geometric inputs from world-space state at contact:
	 * - TiltDeg: angle between the board's up (Z) and WaterNormal;
	 * - YawOffVelocityDeg: angle between the board's forward (X) and Velocity, both projected on
	 *   the water plane, either end (0..90); 0 when either projection is nearly zero (no travel
	 *   along the water, or the board on its nose, which the tilt already crashes);
	 * - BodyUpDot: dot(BodyQuat's up, world up);
	 * - ErrorAlongSpin: dot(unit (board up x water normal), AngularVelocity direction): the
	 *   board's tilt error only, 0 without spin or tilt. The same sign as
	 *   FAttitudeDebug::ErrorAlongSpin, whose error axis also includes the yaw.
	 * WaterNormal need not be unit length; a zero normal counts as world up. Velocity and
	 * AngularVelocity may be in any units (cm/s and rad/s in the board).
	 */
	KITESURF_API FLandingGeometry ComputeGeometry(const FQuat& BoardQuat, const FVector& WaterNormal, const FVector& Velocity,
		const FQuat& BodyQuat, const FVector& AngularVelocity = FVector::ZeroVector);

	/** Copies the geometry into the inputs, leaving the other fields as they are. */
	KITESURF_API void ApplyGeometry(const FLandingGeometry& Geometry, FLandingInputs& Inputs);
}
