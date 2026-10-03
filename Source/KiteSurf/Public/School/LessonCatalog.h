#pragma once

#include "CoreMinimal.h"
#include "School/LessonTypes.h"

/**
 * Builders for lesson data, and the fault rules of docs/tutorials.md 3.4 as named functions, so
 * the catalogue reads like the tables in section 2 and later chapters reuse the same rules.
 * Every threshold is an estimate.
 */
namespace LessonRules
{
	/** Dived early: the kite under this at touchdown (deg). */
	inline constexpr float DivedEarlyKiteDeg = 45.0f;
	/** Edge lost: |edge| fell under this in the last moments before take-off. */
	inline constexpr float EdgeLostAbs = 0.3f;
	/** ...over this window before take-off (s). */
	inline constexpr float EdgeLostFromSeconds = -0.5f;
	inline constexpr float EdgeLostToSeconds = -0.05f;
	/** Lifted the kite on a pop: it rose more than this (deg)... */
	inline constexpr float PopKiteRiseDeg = 10.0f;
	/** ...from this long before take-off to this long after (s). */
	inline constexpr float PopFromSeconds = -0.5f;
	inline constexpr float PopToSeconds = 0.3f;
	/** Sheeted in while the kite climbed: bar past this while climbing (LessonTelemetry::ClimbRateThresholdDegS)... */
	inline constexpr float SheetedInBar = 0.7f;
	/** ...in this window around take-off (s). */
	inline constexpr float SendFromSeconds = -2.0f;
	inline constexpr float SendToSeconds = 0.5f;

	/** A jump-record metric. */
	KITESURF_API FLessonMeasure Jump(ELessonMetric Metric);

	/** A channel reduced over [anchor + From, anchor + To]. */
	KITESURF_API FLessonMeasure Channel(ELessonChannel Channel, ELessonAnchor Anchor, ELessonReduce Reduce, float FromSeconds = 0.0f, float ToSeconds = 0.0f);

	/** A transition metric read at the Event anchor. */
	KITESURF_API FLessonMeasure Transition(ELessonMetric Metric);

	KITESURF_API FLessonCondition Condition(const FLessonMeasure& Measure, float Min, float Max = UE_BIG_NUMBER);

	/** Landing grade at least Grade (Stomped is best). */
	KITESURF_API FLessonCondition GradeAtLeast(ELandingGrade Grade);

	KITESURF_API FLessonFault Fault(FName Id, const FLessonMeasure& Measure, ELessonCompare Compare, float Threshold, int32 Priority, const FText& Feedback);

	/** Kite under DivedEarlyKiteDeg at touchdown. */
	KITESURF_API FLessonFault DivedEarly(int32 Priority, const FText& Feedback);
	/** Kite upwind of the rider at touchdown (KiteDownwind under 0). */
	KITESURF_API FLessonFault FrontStall(int32 Priority, const FText& Feedback);
	/** |edge| under EdgeLostAbs just before take-off. */
	KITESURF_API FLessonFault EdgeLostBeforeTakeoff(int32 Priority, const FText& Feedback);
	/** Kite elevation rose more than PopKiteRiseDeg around take-off. */
	KITESURF_API FLessonFault KiteRoseDuringPop(int32 Priority, const FText& Feedback);
	/** Bar past SheetedInBar while the kite climbed, around take-off. */
	KITESURF_API FLessonFault SheetedInWhileClimbing(int32 Priority, const FText& Feedback);
	/** The landing evaluator named this cause. */
	KITESURF_API FLessonFault LandingCause(ELandingCause Cause, int32 Priority, const FText& Feedback);
	/** One rule per landing evaluator cause a hooked jump can have, each with its line. */
	KITESURF_API TArray<FLessonFault> LandingCauseFaults(int32 Priority = 0);
}

/**
 * The kite school's lessons (docs/tutorials.md section 2) as C++ data, like the trick naming
 * table. Chapters A (riding) and B (jumps) so far; C to F are added as their trick features land
 * (S9), with the prerequisites of section 2 (chapter F keeps its gate behind chapters C and E).
 * Not yet run by the game.
 */
namespace LessonCatalog
{
	/** Every lesson, in chapter and number order. Built once. */
	KITESURF_API const TArray<FLessonDef>& GetAll();

	/** A lesson by id ("B3"); null when there is none. */
	KITESURF_API const FLessonDef* Find(FName Id);

	/** Whether the game has a feature yet. Toeside riding, grabs, rotation, unhooked and kickers are not built. */
	KITESURF_API bool IsFeatureBuilt(ELessonFeature Feature);

	/** The lesson's required feature is built (it can be started once its prerequisites are passed). */
	KITESURF_API bool IsAvailable(const FLessonDef& Lesson);

	/**
	 * Problems with a set of lessons, empty when it is sound: ids unique and in their chapter,
	 * prerequisites present and acyclic, a pass objective, at least one fault with a line, steps
	 * with prompts and objectives, windows that fit in the telemetry, the flat-water map, stars
	 * that can reach two and three.
	 */
	KITESURF_API TArray<FString> Validate(const TArray<FLessonDef>& Lessons);
}
