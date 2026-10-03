#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KiteGear.h"
#include "School/LessonEvaluator.h"
#include "School/LessonTelemetry.h"
#include "School/LessonTiming.h"
#include "School/LessonTypes.h"
#include "Tricks/JumpRecord.h"
#include "LessonDirector.generated.h"

class AKiteRiderPawn;
class ULessonSubsystem;

/** Where a lesson run has got to (docs/tutorials.md 3.3). */
UENUM(BlueprintType)
enum class ELessonPhase : uint8
{
	/** No lesson: before BeginLesson, or after ExitToFreeRide. */
	Idle,
	/** The lesson's title and summary; the set-up is applied, telemetry runs, nothing is judged yet. */
	Intro,
	/** A drill step: its objective is judged every update (the last step is judged by the lesson's pass test). */
	Step,
	/** A result: see ELessonOutcome. AttemptFailed returns to the step by itself; Passed and Failed wait for Retry, Next or exit. */
	Result
};

/** What the result is. */
UENUM(BlueprintType)
enum class ELessonOutcome : uint8
{
	None,
	/** One attempt (a jump, a ride event) missed: one fault line is shown, then the step goes on. */
	AttemptFailed,
	/** The lesson ended without a pass: the time limit ran out. Recorded as a failed attempt. */
	Failed,
	/** The pass test was met. Stars were computed and the result recorded. */
	Passed
};

/**
 * Overrides for one lesson run, for reruns from the lesson menu (S5: "wind and assists"). The
 * defaults run the lesson as the catalogue sets it.
 */
USTRUCT(BlueprintType)
struct FLessonRunOptions
{
	GENERATED_BODY()

	/** Wind for the run (kn); 0 or below the lesson's own wind uses the lesson's (the player can only raise it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float WindKnots = 0.0f;

	/** Ride with Assists instead of the lesson's default assists. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	bool bOverrideAssists = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	FLessonAssists Assists;
};

/**
 * Runs one kite school lesson in the ride level (docs/tutorials.md 3.3, S3). Spawned by
 * AKiteSurfGameMode when ULessonSubsystem has a pending lesson, or by ULessonSubsystem::StartLesson
 * straight into a ride already running.
 *
 * - BeginLesson applies the set-up through the pawn's existing APIs: the board reset and
 *   AKiteSurfGameMode::InitializeRide for the start, then the lesson's wind, kite, board and assists
 *   on the pawn's own components (the player's gear choice on the game instance is not touched).
 *   What it changed is put back by ExitToFreeRide.
 * - Each update (Tick, after the pawn's tick) it polls the board's and the kite's public getters
 *   into an FLessonTelemetry sample whenever the board clock has moved on by four fixed steps
 *   (1/60 s at the pawn's 240 Hz), then evaluates the current objective (LessonEval). A finished
 *   jump is seen as a change of the trick tracker's record count.
 * - A missed attempt shows exactly one fault line (LessonEval::DiagnoseFault); three on one step
 *   raise the drop-back offer. A pass computes the stars (LessonEval::ComputeStars, with the higher
 *   bar judged alongside the pass test) and records the result through ULessonSubsystem.
 *
 * Draws nothing: the HUD lesson layer (S4) and the menus (S5) read the getters. Runs without a
 * renderer, so tests drive it under -nullrhi by calling UpdateLesson after each pawn tick.
 */
UCLASS()
class KITESURF_API ALessonDirector : public AActor
{
	GENERATED_BODY()

public:
	ALessonDirector();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Seconds the intro shows before the first step is judged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float IntroSeconds = 3.0f;

	/** Seconds an AttemptFailed result shows before the step goes on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float AttemptResultSeconds = 2.5f;

	/** Failed attempts on one step that raise the drop-back offer (the channel's own advice). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	int32 DropBackAfterFailures = 3;

	/**
	 * Seconds of board time from the first step to a Failed result (lessons are meant to take one to
	 * three minutes). 0 never fails on time. Estimate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "School")
	float LessonTimeLimitSeconds = 300.0f;

	/**
	 * Starts a lesson on a rider: applies the set-up and goes to the intro. A lesson already running
	 * on this director is ended first (counted as a failed attempt if anything was judged). False
	 * when Pawn is not a kite rider.
	 */
	bool BeginLesson(const FLessonDef& Lesson, APawn* Pawn);
	bool BeginLesson(const FLessonDef& Lesson, APawn* Pawn, const FLessonRunOptions& Options);

	/** BeginLesson for a catalogue lesson by id. */
	UFUNCTION(BlueprintCallable, Category = "School")
	bool BeginLessonById(FName LessonId, APawn* Pawn);

	/** One update: sample the telemetry, judge the objective, move the state machine. Tick calls it; tests call it after each pawn tick. */
	void UpdateLesson(float DeltaSeconds);

	/** Starts the lesson again from its set-up (the pause menu's Retry, the result card's "Retry for more stars"). */
	UFUNCTION(BlueprintCallable, Category = "School")
	bool Retry();

	/**
	 * After a pass: starts the next lesson, the first unlocked one after this in catalogue order.
	 * False (and nothing changes) before a pass or when there is none.
	 */
	UFUNCTION(BlueprintCallable, Category = "School")
	bool Next();

	/** The lesson Next would start; None when there is none. */
	UFUNCTION(BlueprintPure, Category = "School")
	FName GetNextLessonId() const;

	/**
	 * Takes the drop-back offer: back to the previous step, or on the first step to the lesson's
	 * first prerequisite. False when nothing was offered or there is nowhere to go.
	 */
	UFUNCTION(BlueprintCallable, Category = "School")
	bool AcceptDropBack();

	/** Ends the lesson (a failed attempt if anything was judged and it was not passed), puts back what the set-up changed and removes the director. */
	UFUNCTION(BlueprintCallable, Category = "School")
	void ExitToFreeRide();

	/** Records results through this subsystem instead of the game instance's (tests: a subsystem with SetWriteToDisk(false)). */
	void SetLessonSubsystem(ULessonSubsystem* InLessons) { LessonsOverride = InLessons; }

	/**
	 * Spawns a director in World, ending any other there, and begins the lesson on Pawn (the player's
	 * rider when null) with the run's options (the lesson menu's wind and assists). Null when there is
	 * no rider or the lesson cannot begin.
	 */
	static ALessonDirector* StartInWorld(UWorld* World, const FLessonDef& Lesson, APawn* Pawn = nullptr,
		const FLessonRunOptions& Options = FLessonRunOptions());

	/** The run's options: the lesson menu's wind and assists, kept by Retry. */
	const FLessonRunOptions& GetRunOptions() const { return RunOptions; }

	// --- State for the HUD lesson layer (S4) and the menus (S5). ---

	UFUNCTION(BlueprintPure, Category = "School")
	ELessonPhase GetPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "School")
	ELessonOutcome GetOutcome() const { return Outcome; }

	UFUNCTION(BlueprintPure, Category = "School")
	bool IsRunning() const { return Phase != ELessonPhase::Idle; }

	/** The lesson being run; null when idle. */
	const FLessonDef* GetLesson() const { return Phase != ELessonPhase::Idle ? &Lesson : nullptr; }

	UFUNCTION(BlueprintPure, Category = "School")
	FName GetLessonId() const { return Phase != ELessonPhase::Idle ? Lesson.Id : NAME_None; }

	/** 0-based. */
	UFUNCTION(BlueprintPure, Category = "School")
	int32 GetStepIndex() const { return StepIndex; }

	UFUNCTION(BlueprintPure, Category = "School")
	int32 GetStepCount() const { return Lesson.Steps.Num(); }

	/** What the HUD shows as the prompt: the lesson's summary in the intro, the step's prompt, the fault line while an AttemptFailed shows; empty otherwise. */
	UFUNCTION(BlueprintPure, Category = "School")
	FText GetPrompt() const;

	/** The input whose glyph goes with the prompt (an Input Action name); None outside a step. */
	UFUNCTION(BlueprintPure, Category = "School")
	FName GetInputGlyph() const;

	UFUNCTION(BlueprintPure, Category = "School")
	ELessonCue GetCue() const;

	/** The objective being judged: the step's, or on the last step the lesson's pass test. */
	const FLessonObjective* GetCurrentObjective() const;

	/** 0..1 towards the current objective. */
	UFUNCTION(BlueprintPure, Category = "School")
	float GetObjectiveProgress() const { return LastResult.Progress; }

	/** The objective's primary metric, newest value (its unit). */
	UFUNCTION(BlueprintPure, Category = "School")
	float GetObjectiveValue() const { return LastResult.Value; }

	/**
	 * Held objectives: the newest sample is inside the band (with every condition). Events: the newest
	 * judged event qualified. The HUD's hint line (S4) shows when a held objective stays out of band.
	 */
	UFUNCTION(BlueprintPure, Category = "School")
	bool IsObjectiveInBand() const { return LastResult.bQualifies; }

	/**
	 * The newest timing grade on a timing-ring step (LessonTiming): a sheet-in step is graded the moment
	 * the bar comes in, the others when their attempt is judged. None until the first grade of a run.
	 */
	UFUNCTION(BlueprintPure, Category = "School")
	ELessonTimingGrade GetTimingGrade() const { return TimingGrade; }

	/** Goes up by one with every new timing grade, so the HUD can flash each one once. */
	UFUNCTION(BlueprintPure, Category = "School")
	int32 GetTimingSerial() const { return TimingSerial; }

	/** The one fault line of the last missed attempt; empty when none matched or nothing missed yet. */
	UFUNCTION(BlueprintPure, Category = "School")
	FText GetLastFaultLine() const { return LastFaultLine; }

	/** FLessonFault::Id of the last fault line. */
	UFUNCTION(BlueprintPure, Category = "School")
	FName GetLastFaultId() const { return LastFaultId; }

	/** Stars of the pass, 0 until Passed. */
	UFUNCTION(BlueprintPure, Category = "School")
	int32 GetStars() const { return Stars; }

	/** The pass met the higher bar too. */
	UFUNCTION(BlueprintPure, Category = "School")
	bool PassedHigherBar() const { return bPassedHigherBar; }

	/** Attempts judged on the current step, and the ones that missed. */
	UFUNCTION(BlueprintPure, Category = "School")
	int32 GetStepAttempts() const { return StepProgress.Attempts; }

	UFUNCTION(BlueprintPure, Category = "School")
	int32 GetStepFailures() const { return StepFailures; }

	/** Attempts judged in this run of the lesson, every step. */
	UFUNCTION(BlueprintPure, Category = "School")
	int32 GetLessonAttempts() const { return LessonAttempts; }

	/** Three failed attempts on this step: offer to drop back (AcceptDropBack). */
	UFUNCTION(BlueprintPure, Category = "School")
	bool IsDropBackOffered() const { return bOfferDropBack; }

	/** Where AcceptDropBack goes on the first step: the lesson's first prerequisite (None if it has none). */
	UFUNCTION(BlueprintPure, Category = "School")
	FName GetDropBackLessonId() const;

	/** What the run rides with: the lesson's assists or the run's override. */
	const FLessonAssists& GetUsedAssists() const { return UsedAssists; }

	/** Wind the run was set up with (kn). */
	UFUNCTION(BlueprintPure, Category = "School")
	float GetWindKnots() const { return WindKnots; }

	/** Where the set-up placed the rider: the lesson's start, or the nearest one the game supports (Standing floats, Airborne rides). */
	UFUNCTION(BlueprintPure, Category = "School")
	ELessonStart GetAppliedStart() const { return AppliedStart; }

	/** Where results are recorded: the override, or the game instance's subsystem; null when neither exists. The HUD's result card reads the best result here. */
	ULessonSubsystem* GetLessonSubsystem() const { return GetLessons(); }

	AKiteRiderPawn* GetRider() const { return Rider.Get(); }

	const FLessonTelemetry& GetTelemetry() const { return Telemetry; }

	/** Board seconds the current phase has lasted. */
	UFUNCTION(BlueprintPure, Category = "School")
	float GetPhaseSeconds() const { return PhaseSeconds; }

	/** The start the game can actually give for a requested one. */
	static ELessonStart SupportedStart(ELessonStart Requested);

private:
	/** What the set-up changes on the rider, to put back on exit. */
	struct FFreeRideSnapshot
	{
		bool bValid = false;
		FVector BaseWind = FVector::ZeroVector;
		EKiteModel KiteModel = EKiteModel::Boost;
		float KiteSizeM2 = 9.0f;
		EBoardSize BoardSize = EBoardSize::Medium;
		bool bParkHoldAssist = false;
		bool bAutoEdge = true;
		float LandingAssistStrength = 1.0f;
	};

	void ApplySetup();
	void ApplyAssists(const FLessonAssists& Assists);
	void TakeSnapshot();
	void RestoreSnapshot();

	/** A sample from the rider's components now. */
	FLessonSample MakeSample() const;
	/** Adds a sample when the board clock has moved on by a sample interval; true when one was added. */
	bool SampleTelemetry();

	void EnterIntro();
	void EnterStep(int32 Index);
	void EnterResult(ELessonOutcome InOutcome);
	void SetPhase(ELessonPhase InPhase);
	void JudgeStep();
	void OnAttemptFailed(const FJumpRecord* Jump, const FLessonJumpExtras& Extras);
	void OnPassed(float Value);
	/** Counts the lesson as a failed attempt in the progress book if anything was judged and nothing recorded yet. */
	void RecordAbandonedAttempt();
	bool IsLastStep() const { return StepIndex >= Lesson.Steps.Num() - 1; }

	/** The newest jump record, or null; Extras get its landing cause. */
	const FJumpRecord* NewestJump(FLessonJumpExtras& OutExtras);

	ULessonSubsystem* GetLessons() const;
	float BoardTime() const;
	static const TCHAR* PhaseName(ELessonPhase InPhase);
	static const TCHAR* OutcomeName(ELessonOutcome InOutcome);

	UPROPERTY(Transient)
	TWeakObjectPtr<AKiteRiderPawn> Rider;

	UPROPERTY(Transient)
	TObjectPtr<ULessonSubsystem> LessonsOverride;

	FLessonDef Lesson;
	FLessonRunOptions RunOptions;
	FLessonAssists UsedAssists;
	float WindKnots = 0.0f;
	ELessonStart AppliedStart = ELessonStart::Riding;
	FFreeRideSnapshot Snapshot;

	ELessonPhase Phase = ELessonPhase::Idle;
	ELessonOutcome Outcome = ELessonOutcome::None;
	int32 StepIndex = 0;
	float PhaseSeconds = 0.0f;
	/** Board time the first step started; the time limit runs from here. */
	float LessonStartTime = 0.0f;

	FLessonTelemetry Telemetry;
	/** Board time the next telemetry sample is due. */
	float NextSampleTime = -UE_BIG_NUMBER;
	FLessonProgress StepProgress;
	/** The higher bar (pass test plus the star rules' conditions), judged alongside the pass test on the last step. */
	FLessonProgress HigherBarProgress;
	bool bHigherBarMet = false;
	FObjectiveResult LastResult;
	/** The tracker's newest record, copied out for the evaluators. */
	FJumpRecord NewestJumpRecord;
	int32 LastSeenRecordCount = 0;
	int32 SeenLoopRecords = 0;
	int32 CompletedLoops = 0;

	int32 StepFailures = 0;
	int32 LessonAttempts = 0;
	bool bOfferDropBack = false;
	FText LastFaultLine;
	FName LastFaultId;
	int32 Stars = 0;
	bool bPassedHigherBar = false;
	ELessonTimingGrade TimingGrade = ELessonTimingGrade::None;
	int32 TimingSerial = 0;
	/** Board time of the last sheet-in grade, so one pull of the bar is graded once. */
	float LastSheetGradeTime = -UE_BIG_NUMBER;
	void SetTimingGrade(ELessonTimingGrade Grade);
	/** On a sheet-in timing step: grades the bar coming in, if it just did. */
	void GradeSheetIn();
	/** The run's result is in the progress book. */
	bool bRecorded = false;
};
