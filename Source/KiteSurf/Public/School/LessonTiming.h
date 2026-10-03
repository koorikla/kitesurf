#pragma once

#include "CoreMinimal.h"
#include "School/LessonEvaluator.h"
#include "School/LessonTelemetry.h"
#include "School/LessonTypes.h"
#include "LessonTiming.generated.h"

struct FJumpRecord;

/** The grade of the moment of an action on a timing-ring step (docs/tutorials.md 3.4, OlliOlli style). */
UENUM(BlueprintType)
enum class ELessonTimingGrade : uint8
{
	None,
	Early,
	Good,
	Perfect,
	Late
};

/**
 * Timing grades for the timing-ring steps (docs/tutorials.md 3.4, S4). Pure functions over the
 * lesson telemetry and the jump record, called by ALessonDirector; the HUD lesson layer flashes the
 * grade. Every threshold here is an estimate.
 *
 * - Sheet-in steps (glyph IA_Sheet): graded live, the moment the bar comes in through SheetInBar,
 *   against the kite reaching 80 to 90 deg: under SheetGoodMinDeg Early, under SheetPerfectMinDeg
 *   Good, at the top Perfect unless the kite has waited there (Good after SheetPerfectWaitSeconds,
 *   Late after SheetLateWaitSeconds).
 * - The others are graded when their attempt is judged:
 *   - the landing dive (DiveBeforeTouchdown) and the kite's lead at a transition: the value against
 *     the objective's band (A7's: 0 to 1.5 s), a high value (dived or crossed too soon) Early;
 *   - a loop start against the apex: offset from the apex;
 *   - a stomp (a jump step with glyph IA_Jump): take-off against the peak line tension in the second
 *     before it.
 */
namespace LessonTiming
{
	/** The bar counts as "in" once it passes this (0 out, 1 in). */
	inline constexpr float SheetInBar = 0.75f;
	/** Sheet-in graded only with the kite at least this high: lower down it is riding, not a send (deg). */
	inline constexpr float SheetMinKiteDeg = 45.0f;
	/** The kite at the top: sheet in now (deg). */
	inline constexpr float SheetPerfectMinDeg = 80.0f;
	/** Below the top but close: Good (deg). */
	inline constexpr float SheetGoodMinDeg = 65.0f;
	/** At the top for longer than this before the bar came in: Good, not Perfect (s). */
	inline constexpr float SheetPerfectWaitSeconds = 0.35f;
	/** At the top for longer than this: Late (s). */
	inline constexpr float SheetLateWaitSeconds = 0.7f;
	/** A second sheet-in within this long is not graded again (s). */
	inline constexpr float SheetRegradeSeconds = 1.5f;
	/** The kite's lead at a transition: A7's band, crossing 12 up to 1.5 s before the board (s). */
	inline constexpr float KiteLeadMaxSeconds = 1.5f;
	/** Stomp: take-off within this long after the peak load is Perfect, within StompGoodSeconds Good, later Late (s). */
	inline constexpr float StompPerfectSeconds = 0.1f;
	inline constexpr float StompGoodSeconds = 0.3f;
	/** Loop start against the apex: Perfect within this long, Good within LoopGoodSeconds (s). */
	inline constexpr float LoopPerfectSeconds = 0.15f;
	inline constexpr float LoopGoodSeconds = 0.4f;

	// --- Slow motion at a step's decision point (docs/tutorials.md 3.4, S8). Estimates. ---
	// The schedule runs in real seconds, not game seconds: the player feels it in real time, and a
	// game-time schedule would stretch itself by the slow motion it makes.

	/** Game time against real time at the bottom of the slow motion. */
	inline constexpr float SlowMoDilation = 0.6f;
	/** Real seconds easing from 1 down to SlowMoDilation. */
	inline constexpr float SlowMoRampInSeconds = 0.1f;
	/** Real seconds held at SlowMoDilation. */
	inline constexpr float SlowMoHoldSeconds = 0.5f;
	/** Real seconds easing back up to 1. */
	inline constexpr float SlowMoRampOutSeconds = 0.2f;
	/** Clean (or better) attempts on one step after which that step's slow motion stops. */
	inline constexpr int32 SlowMoOffAfterCleanAttempts = 3;
	/** KiteAtTop arms again once the kite is this far below its threshold (deg). */
	inline constexpr float SlowMoRearmDeg = 15.0f;
	/**
	 * KiteAtTop also fires within this much under its threshold once the kite stops climbing (deg): a
	 * loaded send tops out lower than an unloaded one (about 74 deg on B2's 14 kn set-up), and the top
	 * of the climb is the moment to sheet in.
	 */
	inline constexpr float SlowMoTopBandDeg = 10.0f;
	/** "Stopped climbing": the elevation rises slower than this between samples (deg/s). */
	inline constexpr float SlowMoTopRateDegS = 2.0f;
	/** RiderDescending fires only on a jump that climbed at least this high (m above the water). */
	inline constexpr float SlowMoMinJumpM = 0.5f;

	/** The whole slow motion, ramps included (real s). */
	KITESURF_API float SlowMoTotalSeconds();

	/** Real seconds into a slow motion where its ramp out starts. */
	KITESURF_API float SlowMoReleaseSeconds();

	/** The time dilation RealSeconds into a slow motion: 1 before and after, eased (smoothstep) down to SlowMoDilation, held, and eased back. */
	KITESURF_API float SlowMoDilationAt(float RealSeconds);

	/** Whether a slow-motion cue may fire again, and the height of the jump so far (RiderDescending). */
	struct FSlowMoArm
	{
		bool bArmed = true;
		float JumpPeakM = 0.0f;
		/** The previous sample's kite elevation and time (KiteAtTop's climb rate). */
		bool bHasLast = false;
		float LastKiteDeg = 0.0f;
		float LastTime = 0.0f;
	};

	/**
	 * One telemetry sample against a slow-motion cue (ELessonSlowMoTrigger): true when the decision
	 * point is reached with this sample. Firing disarms the cue until it arms again (the kite drops
	 * back, the rider is back on the water). Nothing fires with the rider fallen.
	 */
	KITESURF_API bool DetectSlowMoCue(const FLessonSlowMoCue& Cue, const FLessonSample& Sample, FSlowMoArm& Arm);

	/** "EARLY", "GOOD", "PERFECT", "LATE"; empty for None. */
	KITESURF_API FString GradeText(ELessonTimingGrade Grade);

	/** Sheet-in at this kite elevation, after the kite had been at the top (over SheetPerfectMinDeg) for SecondsAtTop. */
	KITESURF_API ELessonTimingGrade GradeSheetIn(float KiteElevationDeg, float SecondsAtTop);

	/**
	 * A value against a band [Min, Max]: outside it Early or Late (bHighIsEarly says which side is
	 * which), the middle half Perfect, the rest of the band Good.
	 */
	KITESURF_API ELessonTimingGrade GradeInBand(float Value, float Min, float Max, bool bHighIsEarly);

	/** An offset from the ideal moment (s, negative before it): Perfect within PerfectSeconds, Good within GoodSeconds, else Early or Late. */
	KITESURF_API ELessonTimingGrade GradeOffset(float OffsetSeconds, float PerfectSeconds, float GoodSeconds);

	/**
	 * True when the bar came in through SheetInBar between the two newest samples, with the rider on
	 * the water and the kite at least SheetMinKiteDeg high; OutGrade is that sheet-in's grade.
	 */
	KITESURF_API bool DetectSheetIn(const FLessonTelemetry& Telemetry, ELessonTimingGrade& OutGrade);

	/**
	 * The grade of a judged attempt on a timing-ring step (not a sheet-in step). None when the step's
	 * objective has no moment to grade or the value cannot be read.
	 */
	KITESURF_API ELessonTimingGrade GradeAttempt(const FLessonStep& Step, const FLessonObjective& Objective, const FLessonTelemetry& Telemetry,
		const FJumpRecord* Jump, const FLessonJumpExtras& Extras);

	/** A sheet-in step: graded live by DetectSheetIn rather than when its attempt is judged. */
	KITESURF_API bool IsSheetInStep(const FLessonStep& Step);
}
