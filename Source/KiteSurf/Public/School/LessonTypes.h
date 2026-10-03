#pragma once

#include "CoreMinimal.h"
#include "KiteGear.h"
#include "School/LessonTelemetry.h"
#include "Tricks/TrickTypes.h"
#include "LessonTypes.generated.h"

/**
 * Kite school lesson data (docs/tutorials.md 3.2). A lesson is data, not code: a set-up, one to
 * three drill steps, a pass objective, an ordered list of fault rules with their feedback lines,
 * and star rules. LessonEval (LessonEvaluator.h) evaluates objectives and diagnoses faults;
 * LessonCatalog (LessonCatalog.h) holds the lessons; ULessonSubsystem (LessonSubsystem.h, S2)
 * holds the player's progress and the unlocks. Not yet run by the game: the director (S3) comes next.
 *
 * Every threshold in the catalogue is an estimate (docs/tutorials.md section 2).
 */

/** How the rider is placed when a lesson starts. */
UENUM(BlueprintType)
enum class ELessonStart : uint8
{
	/** Standing in the shallows with the kite up. Does not exist in the game yet: the director falls back to Floating. */
	Standing,
	/** In the water, board on the feet, kite at 12. */
	Floating,
	/** Already riding at FLessonSetup::StartSpeedKnots on FLessonSetup::StartTack. */
	Riding,
	/** In the air at FLessonSetup::StartHeightM, for landing drills. */
	Airborne
};

/** What a drill step shows the player (drawn by the HUD lesson layer, S4). */
UENUM(BlueprintType)
enum class ELessonCue : uint8
{
	None,
	/** A translucent kite at the target position, following the expert run by phase. */
	GhostKite,
	/** The wind-window arc with target zones (45 deg cruise, 12 o'clock sheet-in, landing sweet spot). */
	WindowArc,
	/** A ring that grades the moment of an action: Early, Good, Perfect, Late. */
	TimingRing,
	/** A speed band on the HUD. */
	SpeedBand,
	/** A gate or buoy to ride to. */
	Gate,
	/** A meter of the objective's own value (spray arc, edge load, toeside distance). */
	Meter
};

/** A feature of the game a lesson needs that may not be built yet; lessons that need one show as locked. */
UENUM(BlueprintType)
enum class ELessonFeature : uint8
{
	None,
	/** Riding toeside (tricks T3.7): lesson A6. */
	ToesideRiding,
	/** Grabs (tricks T2.1): lesson B4. */
	Grabs,
	/** Live rotation (tricks T1): chapter C. */
	Rotation,
	/** Unhooked riding, raley and passes (tricks T3): chapter F. */
	Unhooked,
	/** Kickers (backlog C9): lesson D5. */
	Kickers
};

/**
 * What a lesson objective, objective condition or fault rule measures. Four families, by where
 * the value comes from (LessonEval::GetMetricSource):
 * - Jump: the last FJumpRecord (and FLessonJumpExtras); one event per new jump.
 * - Channel: a telemetry channel read through a window around an anchor (FLessonMeasure).
 * - Held: a channel kept in a band for FLessonObjective::WindowSeconds.
 * - Ride: events found in the telemetry: time to planing, upwind gain, distance, kite dives,
 *   transitions.
 */
UENUM(BlueprintType)
enum class ELessonMetric : uint8
{
	/** Unset: a lesson without a pass objective is invalid. */
	None,

	// Jump record.
	/** Apex above the take-off point (m). */
	JumpHeight,
	/** Time in the air (s). */
	Airtime,
	/** Distance over the water (m). */
	JumpDistance,
	/** Horizontal speed at take-off (m/s). */
	TakeoffSpeed,
	/** The landing grade as its enum index: Stomped 0, Clean 1, Sketchy 2, Crash 3. "At least Clean" is Max 1. A crashed jump is Crash. */
	LandingGrade,
	/** FLessonJumpExtras::LandingCause as its enum index (ELandingCause). */
	LandingCause,
	/** Sink rate at touchdown (m/s, positive down). */
	SinkAtTouchdown,
	/** Landing deceleration (g). */
	LandingG,
	/** FJumpRecord::KiteElevationAtLandingDeg (deg). */
	KiteElevationAtTouchdown,
	/** Lowest kite elevation in the air (deg). */
	MinKiteElevationInAir,
	/** 1 when the jump was popped off the water, 0 when the kite lifted the rider off. */
	Popped,
	/** Kite loops in the jump that reached 360. */
	CompletedLoops,
	/** 1 when TrickRecognition::ClassifyLoops finds a loop of FLessonMeasure::LoopKind, else 0. */
	LoopKind,
	/** The first completed loop's start against the apex (s, negative before the apex). Unavailable without a completed loop. */
	LoopStartSinceApex,
	/** The first completed loop's duration (s). Unavailable without a completed loop. */
	LoopDuration,
	/** 1 when FJumpRecord::TrickName contains FLessonMeasure::Text (ignoring case), else 0. */
	TrickNameContains,
	/** Body rotation (deg) from FLessonJumpExtras; unavailable until the rotation is known. */
	RotationDeg,
	/** Longest grab held (s) from FLessonJumpExtras; unavailable until grabs are known. */
	GrabHoldSeconds,
	/**
	 * How long before touchdown the landing dive started (s): touchdown time minus the last time in
	 * the air the kite was within LessonEval::DiveStartDropDeg of its highest elevation in the air.
	 * Near 0 when the kite never left its top before touchdown (no dive, or dived late).
	 */
	DiveBeforeTouchdown,
	/** Change of the direction of travel from just before take-off to touchdown (deg, 0..180). */
	HeadingChange,

	// Telemetry window.
	/** FLessonMeasure::Channel reduced over the window around the anchor. */
	Channel,

	// Held in a band.
	/** Speed (m/s) held in [Min, Max]. */
	SpeedHeld,
	/** Kite elevation (deg) held in [Min, Max]. */
	KiteElevationHeld,
	/** FLessonObjective::Channel held in [Min, Max]. */
	ChannelHeld,

	// Ride events.
	/** Seconds from the step start to the first planing sample; a miss once Max has passed without planing. */
	TimeToPlaning,
	/** Ground gained upwind since the reference (m), while planing; with bEachTack, measured from the last tack change. */
	UpwindGain,
	/** Distance ridden while planing since the reference (m), on samples that meet the conditions; with bEachTack, from the last tack change. */
	DistanceRidden,
	/**
	 * Kite dives: the kite leaves Max (the return elevation), drops at least LessonEval::DiveMinDropDeg
	 * below it and climbs back over it. A dive counts when it never went below Min, was back
	 * within WindowSeconds and its conditions hold (read with the dive's start as the Event anchor).
	 */
	KiteDives,
	/** A tack change, value 1. Its conditions are read with the Event anchor at the change. */
	Transition,
	/** A tack change: speed LessonEval::TransitionSettleSeconds after it over speed the same time before. */
	TransitionSpeedKept,
	/** A tack change: seconds not planing within LessonEval::TransitionSettleSeconds either side. */
	TransitionNotPlaningSeconds,
	/** A tack change: change time minus the time the kite crossed 12 nearest to it (s, positive when the kite went first). */
	KiteLeadAtTransition
};

/** The time a channel window is placed against. */
UENUM(BlueprintType)
enum class ELessonAnchor : uint8
{
	/** The newest sample. */
	Latest,
	/** The jump's take-off time. */
	Takeoff,
	/** The jump's apex time. */
	Apex,
	/** The jump's touchdown time. */
	Touchdown,
	/**
	 * The ride event being judged: a tack change, or a kite dive's start. In fault diagnosis and
	 * with no event, the newest tack change at least LessonEval::TransitionSettleSeconds old.
	 */
	Event
};

/** How a channel window becomes one number. */
UENUM(BlueprintType)
enum class ELessonReduce : uint8
{
	/** The value at the window's end (anchor + ToSeconds). */
	At,
	Min,
	Max,
	Mean,
	/** Max minus min. */
	Range,
	/** Value at the end minus value at the start. */
	Change,
	/** Max minus the value at the start: how far it climbed. */
	Rise,
	/** The value at the start minus min: how far it fell. */
	Drop
};

/** How a fault rule compares its measure with its threshold. */
UENUM(BlueprintType)
enum class ELessonCompare : uint8
{
	Less,
	LessOrEqual,
	Greater,
	GreaterOrEqual,
	/** Within 0.001. */
	Equal,
	NotEqual
};

/** One number to measure: a metric, and for channel metrics the window it is read over. */
USTRUCT(BlueprintType)
struct FLessonMeasure
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	ELessonMetric Metric = ELessonMetric::None;

	/** Channel metric: what to read. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	ELessonChannel Channel = ELessonChannel::Speed;

	/** Channel metric: what the window is placed against. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	ELessonAnchor Anchor = ELessonAnchor::Latest;

	/** Channel metric: the window is [anchor + FromSeconds, anchor + ToSeconds]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float FromSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float ToSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	ELessonReduce Reduce = ELessonReduce::At;

	/** LoopKind metric: the kind to look for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	ETrickLoopKind LoopKind = ETrickLoopKind::Kiteloop;

	/** TrickNameContains metric: the text to look for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FString Text;
};

/** A measure that must land inside [Min, Max] (inclusive). */
USTRUCT(BlueprintType)
struct FLessonCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FLessonMeasure Measure;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float Min = -UE_BIG_NUMBER;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float Max = UE_BIG_NUMBER;
};

/**
 * What a step or the pass test asks for. The primary metric, with Min and Max, is the number
 * the HUD shows; every condition must also hold. How it counts depends on the metric's source:
 * - Jump and Ride events: Count events must qualify (in a row when bInARow: a miss resets the
 *   count). With bEachTack an event on the same tack as the last counted one does not count.
 * - Held: the primary channel and every Channel condition (read per sample, ignoring their
 *   window) must hold together for WindowSeconds without a break.
 * Conditions on events are read with the jump (Jump events) or the tack change (the Event
 * anchor) as context.
 */
USTRUCT(BlueprintType)
struct FLessonObjective
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	ELessonMetric Metric = ELessonMetric::None;

	/** ChannelHeld and Channel metrics: the channel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	ELessonChannel Channel = ELessonChannel::Speed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float Min = -UE_BIG_NUMBER;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float Max = UE_BIG_NUMBER;

	/** Qualifying events needed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	int32 Count = 1;

	/** A missed event resets the count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	bool bInARow = false;

	/** Counted events must alternate tacks (one on each tack). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	bool bEachTack = false;

	/** Held: seconds to hold. KiteDives: the longest a dive may stay under Max. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float WindowSeconds = 0.0f;

	/** LoopKind metric: the kind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	ETrickLoopKind LoopKind = ETrickLoopKind::Kiteloop;

	/** TrickNameContains metric: the text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FString Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	TArray<FLessonCondition> Conditions;

	bool IsSet() const { return Metric != ELessonMetric::None; }

	/** The primary metric as a measure (Channel metrics read the newest sample). */
	FLessonMeasure PrimaryMeasure() const
	{
		FLessonMeasure M;
		M.Metric = Metric;
		M.Channel = Channel;
		M.LoopKind = LoopKind;
		M.Text = Text;
		return M;
	}
};

/**
 * A diagnosis rule (docs/tutorials.md 3.4): when the measure compares true against the threshold,
 * the feedback line is a candidate. LessonEval::DiagnoseFault shows exactly one: the highest
 * Priority that matched, the earlier rule on a tie. A measure that cannot be read never matches.
 */
USTRUCT(BlueprintType)
struct FLessonFault
{
	GENERATED_BODY()

	/** Short name for tests and logs, e.g. "DivedEarly". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FLessonMeasure Measure;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	ELessonCompare Compare = ELessonCompare::Greater;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float Threshold = 0.0f;

	/** Higher wins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	int32 Priority = 0;

	/** The one line the player sees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FText Feedback;
};

/** The assist toggles a lesson starts with (docs/tutorials.md 3.5). */
USTRUCT(BlueprintType)
struct FLessonAssists
{
	GENERATED_BODY()

	/** The kite holds where it was parked (UKiteComponent::bParkHoldAssist). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	bool bAutoPark = false;

	/** The board edges by itself (UBoardMovementComponent::bAutoEdge). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	bool bAutoEdge = false;

	/** Help on the landing dive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	bool bLandingAssist = false;

	/** The kite is turned back towards the travel after a jump (physics phase 3; not built yet). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	bool bAutoRedirect = false;

	/** The kite is caught after a loop (chapter E). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	bool bLoopCatch = false;

	/** Slow motion at the lesson's decision point (S8). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	bool bSlowMotion = false;

	int32 CountOn() const
	{
		return int32(bAutoPark) + int32(bAutoEdge) + int32(bLandingAssist) + int32(bAutoRedirect) + int32(bLoopCatch) + int32(bSlowMotion);
	}
};

/** Map, wind, gear, start and assists for a lesson. */
USTRUCT(BlueprintType)
struct FLessonSetup
{
	GENERATED_BODY()

	/** Every lesson uses the flat-water spot for now. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FName Map = TEXT("L_FlatWater");

	/** The lesson's default wind; the player can raise it for reruns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float WindKnots = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	EKiteModel Kite = EKiteModel::Boost;

	/** 0: UKiteComponent::RecommendKiteSizeM2 for the wind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float KiteSizeM2 = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	EBoardSize Board = EBoardSize::Large;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	ELessonStart Start = ELessonStart::Riding;

	/** Riding start: speed (kn). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float StartSpeedKnots = 0.0f;

	/** Riding start: +1 or -1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	int32 StartTack = 1;

	/** Airborne start: height (m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float StartHeightM = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FLessonAssists Assists;
};

/** One drill: a few words plus an input glyph, a cue, and what it asks for. */
USTRUCT(BlueprintType)
struct FLessonStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FText Prompt;

	/** The input whose glyph goes with the prompt (an Input Action name, e.g. "IA_Jump"); None for no glyph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FName InputGlyph;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	ELessonCue Cue = ELessonCue::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FLessonObjective Objective;
};

/**
 * Stars (docs/tutorials.md 3.1), from LessonEval::ComputeStars:
 * - 1: pass;
 * - 2: pass with fewer assists than the lesson's defaults, or pass the higher bar (the pass
 *   objective with HigherBar's conditions added, e.g. Clean becomes Stomped);
 * - 3: pass with every assist off; a lesson with no assists to turn off also needs the higher bar.
 */
USTRUCT(BlueprintType)
struct FStarRules
{
	GENERATED_BODY()

	/** Conditions added to the pass objective for the higher bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	TArray<FLessonCondition> HigherBar;

	/** What the higher bar asks for, in a few words ("Stomped landings"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FText HigherBarText;
};

/** One lesson. */
USTRUCT(BlueprintType)
struct FLessonDef
{
	GENERATED_BODY()

	/** "B3": chapter letter and number, as in docs/tutorials.md section 2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FText Title;

	/** "A" to "F". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FName Chapter;

	/** What the lesson teaches, one line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FText Summary;

	/** Lessons that must be passed first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	TArray<FName> Requires;

	/** A game feature the lesson needs; until it is built the lesson shows as locked (LessonCatalog::IsFeatureBuilt). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	ELessonFeature RequiredFeature = ELessonFeature::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FLessonSetup Setup;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	TArray<FLessonStep> Steps;

	/** The pass test. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FLessonObjective Pass;

	/** Diagnosis rules, each with its feedback line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	TArray<FLessonFault> Faults;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FStarRules Stars;

	/** A demonstration to play for "Watch demo". Reserved: demonstrations come after the text-only first round, so this is None everywhere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FName DemoId;
};
