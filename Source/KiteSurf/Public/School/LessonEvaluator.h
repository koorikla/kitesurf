#pragma once

#include "CoreMinimal.h"
#include "School/LessonTypes.h"
#include "School/LessonTelemetry.h"
#include "Tricks/TrickTypes.h"
#include "LessonEvaluator.generated.h"

struct FJumpRecord;
struct FTrickSignature;

/**
 * The kite school's evaluators (docs/tutorials.md S1): pure functions over a lesson objective or
 * fault list, the progress so far, the telemetry ring buffer and the last jump record. The lesson
 * director (ALessonDirector, S3) calls EvaluateObjective on each new telemetry sample, then
 * ApplyResult, and DiagnoseFault when an attempt fails.
 */

/** Facts about a jump that FJumpRecord does not hold yet. Unknown facts make their metrics unavailable. */
struct FLessonJumpExtras
{
	/** Why the landing was graded down (LandingEvaluator::Evaluate's cause). */
	ELandingCause LandingCause = ELandingCause::None;

	bool bHasRotation = false;
	/** Body rotation in the jump (deg). */
	float RotationDeg = 0.0f;

	bool bHasGrab = false;
	/** Longest grab held (s). */
	float GrabHoldSeconds = 0.0f;
};

/** Where a lesson step has got to. The director owns it: BeginStep, then ApplyResult after every evaluation. */
USTRUCT(BlueprintType)
struct FLessonProgress
{
	GENERATED_BODY()

	/** Qualifying events counted (the current streak when the objective is bInARow). */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	int32 Count = 0;

	/** Events judged, qualifying or not. */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	int32 Attempts = 0;

	/** Events that missed. */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	int32 Failures = 0;

	/** When the step started (s, sample clock). Ride events before it are ignored. */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	float StepStartTimeSeconds = 0.0f;

	/** The newest jump already judged (FJumpRecord::Index). */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	int32 LastJumpIndex = -1;

	/** The newest ride event already judged (its time, s). */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	float LastEventTimeSeconds = -UE_BIG_NUMBER;

	/** Tack of the last counted event, for bEachTack (0 none yet). */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	int32 LastCountedTack = 0;

	/** Upwind gain and distance are measured from here: a time... */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	float ReferenceTimeSeconds = 0.0f;

	/** ...and the upwind position then (m), used when that time has left the telemetry. */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	float ReferenceUpwindM = 0.0f;
};

/** One evaluation of an objective. */
USTRUCT(BlueprintType)
struct FObjectiveResult
{
	GENERATED_BODY()

	/** 0..1 towards the pass. */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	float Progress = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "School")
	bool bPassed = false;

	/** An attempt missed in this evaluation: the moment to diagnose a fault. */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	bool bFailed = false;

	/** A qualifying event was counted in this evaluation. */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	bool bCounted = false;

	/** Held objectives: the newest sample is inside the band. Events: the newest judged event qualified. */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	bool bQualifies = false;

	/** The primary metric's newest value (0 when it could not be read). */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	float Value = 0.0f;

	/** The progress to store (ApplyResult copies it). */
	UPROPERTY(BlueprintReadOnly, Category = "School")
	FLessonProgress NewProgress;
};

/** Where a metric's value comes from. */
enum class ELessonMetricSource : uint8
{
	None,
	Jump,
	Channel,
	Held,
	Ride
};

namespace LessonEval
{
	/** A dive must drop at least this far below the return elevation to count (deg). */
	inline constexpr float DiveMinDropDeg = 15.0f;
	/** DiveBeforeTouchdown: the dive starts when the kite leaves this band under its highest elevation in the air (deg). */
	inline constexpr float DiveStartDropDeg = 5.0f;
	/** Transition metrics compare this long before and after the tack change (s); the event is judged once this long has passed. */
	inline constexpr float TransitionSettleSeconds = 2.0f;
	/** HeadingChange reads the heading this long before take-off (s). */
	inline constexpr float HeadingBeforeTakeoffSeconds = 0.3f;
	/** KiteLeadAtTransition looks for the kite crossing 12 within this long either side of the change (s). */
	inline constexpr float KiteCrossingSearchSeconds = 4.0f;

	KITESURF_API ELessonMetricSource GetMetricSource(ELessonMetric Metric);

	/** The channel a Held metric holds: SpeedHeld Speed, KiteElevationHeld KiteElevation, ChannelHeld the objective's own. */
	KITESURF_API ELessonChannel HeldChannel(const FLessonObjective& Objective);

	/**
	 * Reads one measure. Jump metrics need Jump; Takeoff, Apex and Touchdown anchors need Jump;
	 * the Event anchor uses EventTimeSeconds, or the last tack change when it is not finite.
	 * Ride metrics other than the transition ones cannot be read as a measure. False when the value
	 * is not available.
	 */
	KITESURF_API bool ReadMeasure(const FLessonMeasure& Measure, const FLessonTelemetry& Telemetry, const FJumpRecord* Jump,
		const FLessonJumpExtras& Extras, float EventTimeSeconds, float& OutValue);

	/** Compare's verdict on Value against Threshold. */
	KITESURF_API bool Compare(ELessonCompare Compare, float Value, float Threshold);

	/** Every condition reads and lands inside its range. */
	KITESURF_API bool ConditionsHold(const TArray<FLessonCondition>& Conditions, const FLessonTelemetry& Telemetry, const FJumpRecord* Jump,
		const FLessonJumpExtras& Extras, float EventTimeSeconds);

	/** Starts a step at the newest sample: clears the count, places the reference there and ignores earlier events and jumps. */
	KITESURF_API FLessonProgress BeginStep(const FLessonTelemetry& Telemetry, int32 LastJumpIndex = -1);

	/**
	 * Evaluates an objective against the progress so far. Pure: the returned NewProgress holds the
	 * counts with this evaluation's events added, and ApplyResult stores it.
	 * - Jump: LastJump counts as an event when its Index is newer than Progress.LastJumpIndex.
	 * - Held: progress is the unbroken time in band since the step start over WindowSeconds.
	 * - Ride: the events found in the telemetry since the last one judged.
	 */
	KITESURF_API FObjectiveResult EvaluateObjective(const FLessonObjective& Objective, const FLessonProgress& Progress,
		const FLessonTelemetry& Telemetry, const FJumpRecord* LastJump, const FLessonJumpExtras& Extras = FLessonJumpExtras());

	/** Stores an evaluation's progress. */
	KITESURF_API void ApplyResult(FLessonProgress& Progress, const FObjectiveResult& Result);

	/** The highest-priority fault whose measure compares true, the earlier on a tie; null when none matches. */
	KITESURF_API const FLessonFault* DiagnoseFault(const TArray<FLessonFault>& Faults, const FLessonTelemetry& Telemetry,
		const FJumpRecord& Jump, const FLessonJumpExtras& Extras = FLessonJumpExtras());

	/** The pass objective with the star rules' higher-bar conditions added. */
	KITESURF_API FLessonObjective HigherBarObjective(const FLessonDef& Lesson);

	/** 0 to 3 stars (FStarRules). Default is the lesson's set-up assists, Used what the attempt rode with. */
	KITESURF_API int32 ComputeStars(const FStarRules& Rules, const FLessonAssists& Default, const FLessonAssists& Used,
		bool bPassed, bool bPassedHigherBar);

	/** Rotation (360 per inversion plus 180 per spin half turn) and the longest grab from a signature, with a landing cause. */
	KITESURF_API FLessonJumpExtras ExtrasFromSignature(const FTrickSignature& Signature, ELandingCause Cause = ELandingCause::None);
}
