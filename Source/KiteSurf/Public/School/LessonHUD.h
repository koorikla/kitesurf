#pragma once

#include "CoreMinimal.h"
#include "School/LessonDirector.h"
#include "School/LessonTiming.h"
#include "School/LessonTypes.h"

class AHUD;

/**
 * The HUD lesson layer (docs/tutorials.md 3.4 and S4): what AKiteSurfHUD draws while an
 * ALessonDirector runs. Text only (docs/tutorials.md 6: no voice, no demonstrations yet).
 *
 * - FLessonHUDInput is everything the layer shows, read from the director's getters once a frame
 *   (FromDirector), or built by hand in tests.
 * - LessonHUD:: holds pure formatters and the cue geometry, and BuildView turns an input and the
 *   layer's timers into FLessonHUDView: the strings that are drawn. Tests read the view with no Canvas.
 * - FLessonHUDLayer keeps the timers (the fault line, the held-objective hint, the timing flash),
 *   and draws the view on a Canvas through the owning AHUD.
 */

/** What the result card and the drop-back offer answer to (existing inputs only, no new input assets). */
enum class ELessonHUDAction : uint8
{
	/** The jump button: Next lesson on a pass. */
	Confirm,
	/**
	 * The reset button: Retry on the result card. On the drop-back offer a tap only resets the rider
	 * (the rider's own handler); holding it for LessonHUD::DropBackHoldSeconds takes the offer
	 * (FLessonHUDLayer::SetResetHeld).
	 */
	Retry,
	/** The pause button on the result card: the lesson menu (ULessonSubsystem::RequestLessonMenu). */
	Menu
};

/** What a target zone on the wind-window arc marks. */
enum class ELessonArcZoneKind : uint8
{
	/** The step's own kite elevation band. */
	Target,
	/** The 45 deg cruise band, either side. */
	Cruise,
	/** The 12 o'clock sheet-in band. */
	SheetIn,
	/** The landing sweet spot, just in front of 12 on the side the rider travels. */
	Landing
};

/** A stretch of the wind-window arc, by kite clock (deg, 0 at 12, + to the right looking downwind), From < To. */
struct FLessonArcZone
{
	float FromClockDeg = 0.0f;
	float ToClockDeg = 0.0f;
	ELessonArcZoneKind Kind = ELessonArcZoneKind::Target;
};

/** Everything the lesson layer shows, read once a frame. */
struct KITESURF_API FLessonHUDInput
{
	ELessonPhase Phase = ELessonPhase::Idle;
	ELessonOutcome Outcome = ELessonOutcome::None;
	FName LessonId;
	FText Title;
	FText Summary;
	/** 0-based. */
	int32 StepIndex = 0;
	int32 StepCount = 0;
	/** The step's own prompt (not the fault line that replaces it in the director's GetPrompt). */
	FText StepPrompt;
	FName Glyph;
	ELessonCue Cue = ELessonCue::None;
	bool bSheetInStep = false;
	bool bHasObjective = false;
	FLessonObjective Objective;
	float Progress = 0.0f;
	float Value = 0.0f;
	bool bInBand = false;
	/** The hint for a held objective that is out of band: the lesson's matching fault feedback, or a generic line. */
	FString HeldHint;
	FText FaultLine;
	int32 Stars = 0;
	bool bPassedHigherBar = false;
	FText HigherBarText;
	/** Assists the run rides with, and the lesson's defaults. */
	int32 AssistsOn = 0;
	bool bDropBackOffered = false;
	FName DropBackLessonId;
	FName NextLessonId;
	float PhaseSeconds = 0.0f;
	float IntroSeconds = 3.0f;
	ELessonTimingGrade TimingGrade = ELessonTimingGrade::None;
	int32 TimingSerial = 0;
	/** A slow motion runs at the step's decision point (S8), with its one prompt. */
	bool bSlowMotion = false;
	FText SlowMotionPrompt;

	/** The progress book's record of this lesson. */
	bool bHasRecord = false;
	int32 BestStars = 0;
	bool bHasBestValue = false;
	float BestValue = 0.0f;
	ELessonMetric BestValueMetric = ELessonMetric::None;
	int32 Passes = 0;

	/** The ride now, for the cues. */
	int32 Tack = 1;
	float KiteClockDeg = 0.0f;
	float KiteDepthDeg = 0.0f;
	float KiteElevationDeg = 90.0f;
	float SpeedMS = 0.0f;

	bool IsRunning() const { return Phase != ELessonPhase::Idle; }
	/** A pass or a Failed result: the result card is up and waits for Retry, Next or the menu. */
	bool IsResultCardUp() const { return Phase == ELessonPhase::Result && (Outcome == ELessonOutcome::Passed || Outcome == ELessonOutcome::Failed); }

	static FLessonHUDInput FromDirector(const ALessonDirector& Director);
};

/** Which timed lines are up this frame. */
struct FLessonHUDTimers
{
	bool bShowFault = false;
	bool bShowHint = false;
	bool bShowTiming = false;
	/** Seconds since the timing grade came in (for its pop). */
	float TimingAge = 0.0f;
	/** 0..1: how far the reset button has been held towards taking the drop-back offer. */
	float DropBackHold = 0.0f;
};

/** The text the layer draws. Empty strings are not drawn. */
struct KITESURF_API FLessonHUDView
{
	bool bVisible = false;

	// The lesson panel (intro and steps).
	bool bPanel = false;
	/** "LESSON B2  Small jump" in the intro; "B2  Small jump   STEP 2/2" in a step. */
	FString Header;
	FString Prompt;
	FString Glyph;
	/** "2 / 5   1.4 m   target 1.0 - 2.0 m", or "Get ready" in the intro. */
	FString Progress;
	float ProgressFraction = 0.0f;
	FString Fault;
	FString Hint;
	/** "Too hard? Hold [R | B] to drop back to A2  [####------]": the offer with its hold fill. */
	FString DropBack;
	/** 0..1, the hold fill of the drop-back line (drawn as a bar behind it too). */
	float DropBackFill = 0.0f;
	/** A slow motion runs: Prompt is its one prompt (drawn larger). */
	bool bSlowMotion = false;
	FString Timing;
	ELessonTimingGrade TimingGrade = ELessonTimingGrade::None;
	float TimingAge = 0.0f;

	// The result card.
	bool bResultCard = false;
	bool bPassed = false;
	FString CardTitle;
	FString CardLesson;
	int32 CardStars = 0;
	/** "STARS 2/3": the stars are drawn as shapes; this is their text. */
	FString CardStarsText;
	FString CardResult;
	FString CardBest;
	FString CardNextStar;
	FString CardActions;

	/** The card's lines in order, the empty ones left out (for tests and logs). */
	TArray<FString> ResultCardLines() const;
	/** The panel's lines in order, the empty ones left out (for tests and logs). */
	TArray<FString> PanelLines() const;
};

/** Counts how long a held objective has been out of its band; the hint shows after LessonHUD::HintAfterSeconds. */
struct KITESURF_API FLessonHintTimer
{
	float OutOfBandSeconds = 0.0f;

	/** One frame. True when the hint should show: a held objective out of band for longer than the delay. */
	bool Update(bool bHeldObjective, bool bInBand, float DeltaSeconds);
	void Reset() { OutOfBandSeconds = 0.0f; }
};

namespace LessonHUD
{
	/** How long the fault line shows after a missed attempt (s); the director's AttemptResultSeconds by default. */
	inline constexpr float FaultLineSeconds = 2.5f;
	/** A held objective out of band this long brings up the hint (s). */
	inline constexpr float HintAfterSeconds = 2.0f;
	/** How long the reset button is held to take the drop-back offer (real s); a tap only resets the rider. */
	inline constexpr float DropBackHoldSeconds = 1.0f;
	/** Cells of the text fill on the drop-back line. */
	inline constexpr int32 DropBackFillCells = 10;
	/** How long a timing grade flashes (s). */
	inline constexpr float TimingFlashSeconds = 1.2f;
	/** The landing sweet spot: this far in front of 12 on the travel side (deg of clock). */
	inline constexpr float LandingFromClockDeg = 10.0f;
	inline constexpr float LandingToClockDeg = 25.0f;
	/** The 12 o'clock sheet-in band (deg of clock either side). */
	inline constexpr float SheetInHalfClockDeg = 10.0f;
	/** The cruise band (deg of elevation, so of clock from 12 on the window edge). */
	inline constexpr float CruiseMinDeg = 35.0f;
	inline constexpr float CruiseMaxDeg = 55.0f;

	/** The input's key and stick in brackets: IA_Sheet "[Up/Down | R stick]"; empty for None or an unknown action. */
	KITESURF_API FString GlyphText(FName InputAction);

	/** A metric's value in its unit: m, s, kn (speeds are shown in knots), deg, g, a grade name, a count. */
	KITESURF_API FString FormatMetricValue(ELessonMetric Metric, ELessonChannel Channel, float Value);

	/** The objective's band: "1.0 - 2.0 m", "at least 80°", "at most 4.0 s"; empty when it has neither bound. */
	KITESURF_API FString FormatTarget(const FLessonObjective& Objective);

	/** How far along: held "3.2 / 5.0 s", counted "2 / 5", otherwise a percentage. */
	KITESURF_API FString FormatProgress(const FLessonObjective& Objective, float Progress);

	/** "LESSON B2  Small jump" (intro), "B2  Small jump   STEP 2/3" (step), "B2  Small jump" otherwise. */
	KITESURF_API FString FormatHeader(const FLessonHUDInput& In);

	/**
	 * The drop-back offer with its key and a fill for the hold, HoldFraction 0..1:
	 * "Too hard? Hold [R | B] to drop back to step 1  [##########]" or "... to drop back to A2  [----------]".
	 * Empty when nothing is offered or there is nowhere to go.
	 */
	KITESURF_API FString FormatDropBack(const FLessonHUDInput& In, float HoldFraction = 0.0f);

	/** A text fill: "[####------]" for 0.4 over ten cells. */
	KITESURF_API FString FillText(float Fraction, int32 Cells = DropBackFillCells);

	/** What the next star asks for; empty at three stars or before a pass. */
	KITESURF_API FString FormatNextStar(int32 Stars, bool bPassedHigherBar, const FText& HigherBarText, int32 AssistsOn);

	/** "STARS 2/3". */
	KITESURF_API FString FormatStars(int32 Stars);

	/** The result card's actions with their keys: Next (when there is one, after a pass), Retry, Lesson menu. */
	KITESURF_API FString FormatCardActions(bool bPassed, FName NextLessonId);

	/**
	 * The hint for a held objective out of band: the feedback of the lesson's highest-priority fault
	 * rule that reads the telemetry alone and matches now; otherwise a line from the objective and its
	 * value ("Too slow: bar in").
	 */
	KITESURF_API FString HeldHintLine(const TArray<FLessonFault>& Faults, const FLessonObjective& Objective, const FLessonTelemetry& Telemetry, float Value);

	/** The view for an input and the timers. */
	KITESURF_API FLessonHUDView BuildView(const FLessonHUDInput& In, const FLessonHUDTimers& Timers);

	// --- Cue geometry, in screen space (y down). ---

	/** A point of the wind-window arc: the kite at this clock and depth into the window, as AKiteSurfHUD's arc draws it. */
	KITESURF_API FVector2D ArcPoint(const FVector2D& Centre, float Radius, float ClockDeg, float DepthDeg = 0.0f);

	/** The clock of a kite at this elevation on the window edge, on Side (+1 right, -1 left). */
	KITESURF_API float ElevationToClock(float ElevationDeg, int32 Side);

	/** The objective's kite elevation band, from its metric, its channel or a condition; false when it has none. */
	KITESURF_API bool ElevationBand(const FLessonObjective& Objective, float& OutMinDeg, float& OutMaxDeg);

	/**
	 * The target zones a cue marks on the arc. WindowArc: the objective's elevation band either side
	 * (one zone across 12 when the band reaches the top), or with no band the cruise band either side,
	 * the sheet-in band and the landing sweet spot on the Tack side. TimingRing on a sheet-in step or a
	 * transition: the sheet-in band. Nothing for the other cues.
	 */
	KITESURF_API TArray<FLessonArcZone> ArcZones(ELessonCue Cue, const FLessonObjective* Objective, bool bSheetInStep, int32 Tack);

	/**
	 * Where the ghost kite sits (clock and elevation, on the window edge), by the phase of the move
	 * rather than the clock: a target at the top (80 deg or more, or the clock near 12) is 12; dives
	 * (power dives, the water start) go to 45 deg on the kite's side while the kite is high and back
	 * up to 80 deg once it is low; an elevation band is its middle on the kite's side; anything else 12.
	 */
	KITESURF_API void GhostTarget(const FLessonObjective& Objective, float KiteElevationDeg, float KiteClockDeg, int32 Tack, float& OutClockDeg, float& OutElevationDeg);

	/** The timing ring's radius around the kite marker: closes from MarkerRadius * 4 at 60 deg of clock or more to MarkerRadius at 12. */
	KITESURF_API float TimingRingRadius(float KiteClockDeg, float MarkerRadius);

	/** The arc zones fade as stars are earned (docs/tutorials.md 3.4): 1 with none, down to 0.4 at three. */
	KITESURF_API float ZoneAlpha(int32 BestStars);
}

/** The lesson layer of AKiteSurfHUD: timers and Canvas drawing. */
class KITESURF_API FLessonHUDLayer
{
public:
	/**
	 * Reads the director (null or idle hides the layer) and moves the timers on by DeltaSeconds (game
	 * time). The drop-back hold runs on RealDeltaSeconds (real time, so a slow motion does not stretch
	 * it); below 0 it is DeltaSeconds.
	 */
	void Update(const ALessonDirector* Director, float DeltaSeconds, float RealDeltaSeconds = -1.0f);

	/** The same with an input built by hand (tests). */
	void UpdateFromInput(const FLessonHUDInput& In, float DeltaSeconds, float RealDeltaSeconds = -1.0f);

	/**
	 * The reset button went down (true) or up (false). A press that starts while the drop-back offer
	 * shows, held for LessonHUD::DropBackHoldSeconds, takes it (ConsumeDropBackHold); a tap does
	 * nothing here (the rider's own handler resets the rider).
	 */
	void SetResetHeld(bool bHeld);

	/** True once, when the hold has filled: the caller takes the offer. The press must be let go and pressed again for another. */
	bool ConsumeDropBackHold();

	/** 0..1 towards taking the offer. */
	float GetDropBackHoldFraction() const;

	const FLessonHUDInput& GetInput() const { return Input; }
	const FLessonHUDView& GetView() const { return View; }
	bool IsVisible() const { return View.bVisible; }

	/** The panel at the top centre, from Top; returns its bottom (Top when nothing is drawn). */
	float DrawPanel(AHUD& HUD, float ScreenW, float ScreenH, float Top) const;

	/** The result card, centred. */
	void DrawResultCard(AHUD& HUD, float ScreenW, float ScreenH) const;

	/** The cue on the wind-window arc: target zones, the ghost kite, the timing ring around the kite marker. */
	void DrawArcCue(AHUD& HUD, const FVector2D& Centre, float Radius) const;

	/** The speed band gauge (SpeedBand cue), at X, Y and Width; nothing for another cue. */
	void DrawSpeedBand(AHUD& HUD, float X, float Y, float Width) const;

private:
	FLessonHUDInput Input;
	FLessonHUDView View;
	FLessonHintTimer HintTimer;
	float FaultRemaining = 0.0f;
	float TimingAge = UE_BIG_NUMBER;
	int32 SeenTimingSerial = 0;
	/** The reset button is down, the press started while the offer showed, and how long it has been held (real s). */
	bool bResetHeld = false;
	bool bResetHoldEligible = false;
	float ResetHoldSeconds = 0.0f;
	bool bDropBackHoldDone = false;
	ELessonPhase LastPhase = ELessonPhase::Idle;
	ELessonOutcome LastOutcome = ELessonOutcome::None;
	FName LastLessonId;
	int32 LastStepIndex = -1;
};
