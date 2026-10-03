#pragma once

#include "CoreMinimal.h"
#include "BoardMovementComponent.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/KiteLoopRecord.h"
#include "Tricks/RotationRecognizer.h"
#include "Tricks/TrickRecognition.h"
#include "Tricks/TrickScoring.h"
#include "JumpRecorder.generated.h"

struct FBarState;

/**
 * One simulation step as the jump recorder sees it (docs/tricks/T0.md section 4). The tracker
 * fills one per fixed step, after the board has stepped, by polling the board and the kite:
 * delegates are not bound in tests, so the recorder works from counters.
 *
 * Every field has a board, kite or rider attitude getter (T0.2 board events, T0.3 kite hookup,
 * T1.2 attitude); each comment names it.
 *
 * The bar (T3.1) comes from the pawn's FBarState (Bar), summarised by BarStateMachine::SummariseJump.
 */
struct FJumpRecorderInput
{
	// --- Board ---

	/** Board simulation time at the end of this step (s): UBoardMovementComponent::GetSimTimeSeconds. */
	float BoardTimeSeconds = 0.0f;

	/** UBoardMovementComponent::GetBoardState. */
	EBoardState BoardState = EBoardState::Planing;

	/** UBoardMovementComponent::IsCrashing. */
	bool bCrashing = false;

	/** Take-offs so far, popped or lifted off by the kite: UBoardMovementComponent::GetTakeoffCount, counted in BeginAirborne. */
	int32 TakeoffCount = 0;

	/** Jumps ended so far, landed or crashed (not skips): UBoardMovementComponent::GetJumpCount. */
	int32 JumpCount = 0;

	/** UBoardMovementComponent::GetResetCount. */
	int32 ResetCount = 0;

	/** The last take-off was a pop rather than a kite lift-off: UBoardMovementComponent::WasLastTakeoffPopped. */
	bool bLastTakeoffPopped = false;

	/** Board time of the last take-off (s): UBoardMovementComponent::GetLastTakeoffTimeSeconds. */
	float LastTakeoffTimeSeconds = 0.0f;

	/** Board position (cm, world): the updated component's location. */
	FVector Location = FVector::ZeroVector;

	/** Board velocity (cm/s, world): UBoardMovementComponent::Velocity. */
	FVector Velocity = FVector::ZeroVector;

	/** Apex of the last finished jump above its take-off (cm): GetLastJumpApexHeight. */
	float LastApexCm = 0.0f;

	/**
	 * Board time of the last finished jump's apex (s): UBoardMovementComponent::GetCurrentJumpApexTimeSeconds,
	 * which holds the last jump's apex time until the next take-off. Negative when not supplied: the
	 * recorder then uses the step at which it saw the board highest.
	 */
	float LastApexTimeSeconds = -1.0f;

	/** GetLastJumpAirtime (s). */
	float LastAirtimeSeconds = 0.0f;

	/** GetLastJumpDistance (cm). */
	float LastDistanceCm = 0.0f;

	/** Downward speed into the water at the last contact, relative to its surface (cm/s, positive): UBoardMovementComponent::GetLastLandingSinkMS. */
	float LastSinkRateCmS = 0.0f;

	/** Deceleration of the last landing (g): UBoardMovementComponent::GetLastLandingG, LandingMath::ComputeLandingG over the board's absorb distance. */
	float LastLandingG = 1.0f;

	/** Angle between the board and its velocity at the last contact (deg): UBoardMovementComponent::GetLastLandingAngleDeg. */
	float LastLandingAngleDeg = 0.0f;

	/** UBoardMovementComponent::WasLastLandingClean: false after a crash landing. */
	bool bLastLandingClean = true;

	/** Why the last landing was graded down: UBoardMovementComponent::GetLastLandingVerdict().Cause. */
	ELandingCause LastLandingCause = ELandingCause::None;

	/**
	 * The board graded the last landing (LandingEvaluator::Evaluate), so LastLandingGrade is its verdict.
	 * The tracker always sets it: the board sets the verdict on the same landing that counts the jump.
	 * False in pure tests that build snapshots without a board; the record is then graded by
	 * TrickScoring::GradeLanding.
	 */
	bool bHasLandingVerdict = false;

	/** The last landing's grade: UBoardMovementComponent::GetLastLandingVerdict().Grade. Used only with bHasLandingVerdict. */
	ELandingGrade LastLandingGrade = ELandingGrade::Clean;

	/** The board's nose (world, unit): UBoardMovementComponent::GetBoardWorldQuat().GetAxisX(). Stands in for the travel direction at a take-off with no speed along the water. */
	FVector BoardForward = FVector::ForwardVector;

	// --- Rider attitude (T1.2; read by the rotation recogniser, T1.6) ---

	/** The pawn has a rider attitude to read: the fields below mean something. */
	bool bHasAttitude = false;

	/** The attitude is simulated (in the air): URiderAttitudeComponent::IsSimulating. Only such steps are counted. */
	bool bAttitudeActive = false;

	/** Body orientation at the end of this step (world): URiderAttitudeComponent::GetBodyQuat. On the take-off step it is the riding pose. */
	FQuat BodyQuat = FQuat::Identity;

	/** Body angular velocity (rad/s, world): URiderAttitudeComponent::GetAngularVelocity. */
	FVector AngularVelocityRadS = FVector::ZeroVector;

	// --- Grabs and the one-footer (T2.1, T2.2): the pawn's FGrabState ---

	/** This flight's grabs so far: FGrabState::GetGrabs. Null when the pawn has no grab state. Read during Step only. */
	const TArray<FTrickGrab>* Grabs = nullptr;

	/** FGrabState::IsOneFooter: the back foot has been out long enough to count. */
	bool bOneFooter = false;

	/** FGrabState::GetOneFootSeconds (s). */
	float OneFootSeconds = 0.0f;

	/** The flight's board-off (T2.3): FBoardOffState::GetFlightBoardOff, None until a board has been held long enough. */
	ETrickBoardOff BoardOff = ETrickBoardOff::None;

	/** FBoardOffState::GetFlightBoardOffSeconds (s). */
	float BoardOffSeconds = 0.0f;

	// --- The bar (T3.1, T3.4): the pawn's FBarState ---

	/** The bar's state, stepped before the board: hooked or not, the wrap and this flight's passes. Null when the pawn has none (a hooked jump). Read during Step only. */
	const FBarState* Bar = nullptr;

	// --- Kite ---

	/** Line tension (N). */
	float TensionN = 0.0f;

	/** Kite elevation above the horizon (deg). */
	float KiteElevationDeg = 0.0f;

	/**
	 * Kite simulation time at the end of this step (s): UKiteComponent::GetSimTimeSeconds. Loop
	 * records are on this clock. It advances by the same fixed step as the board's.
	 */
	float KiteTimeSeconds = 0.0f;

	// --- Kite loops (the kite's FKiteLoopTracker, T0.3 hookup) ---

	/** The kite's kept loop records, oldest first: UKiteComponent::GetLoopRecords. Null when there is no kite. Read during Step only. */
	const TArray<FKiteLoopRecord>* KiteLoops = nullptr;

	/** A loop run is open on the kite: UKiteComponent::GetOpenLoop returned true. */
	bool bHasOpenLoop = false;

	/** The open run as a provisional record (turn since its last whole loop). Used only with bHasOpenLoop. */
	FKiteLoopRecord OpenLoop;
};

/** Tunables of FJumpRecorder. */
USTRUCT(BlueprintType)
struct FJumpRecorderSettings
{
	GENERATED_BODY()

	/** An open kite loop run still flying at landing is attached when it has turned at least this far (deg). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float MinOpenLoopDeg = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	FLoopClassifySettings LoopClassify;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	FLandingGradeSettings LandingGrade;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	FTrickScoringSettings Scoring;
};

/**
 * Turns per-step board and kite snapshots into one FJumpRecord per jump (T0.2, the pure part).
 * Pure: no UObject, no world. UTrickTrackerComponent owns one (through FJumpSession) and steps it
 * after the board.
 *
 * The first step only reads the counters. After that, in this order:
 * - Reset: ResetCount changed while a jump is open: the jump is dropped.
 * - Landing or crash: JumpCount changed while a jump is open: the record is finalised from the
 *   board's Last* fields. Landed when bLastLandingClean, otherwise Crashed. Loops: every kite loop
 *   record whose span overlaps [take-off, landing] on the kite clock, plus the open run when it
 *   has turned at least MinOpenLoopDeg, in the order they started. A JumpCount change with no
 *   jump open (a landing with no take-off seen, as in tests that set the board airborne by hand)
 *   is ignored.
 * - Take-off: TakeoffCount changed: a live record opens with the take-off time, position,
 *   horizontal speed and tension. A jump still open is dropped first.
 * - Skip: a jump is open, the board is no longer airborne, not crashing, and JumpCount did not
 *   change: the board treated it as a skip off the surface, and it is dropped.
 * - While open: the peak tension, the lowest kite elevation and the highest board position, the
 *   grabs, the one-footer and the board-off as the snapshot has them (Grabs, bOneFooter,
 *   OneFootSeconds, BoardOff, BoardOffSeconds), and
 *   the rider's rotation: an FRotationRecognizer begun at the take-off (frame from the velocity,
 *   the board's nose and the body) and stepped on every step with bAttitudeActive. The live
 *   record carries the rotation credited so far (GetCurrent); the finalised record the result at
 *   touchdown (Finish with that step's body), when at least one step was counted.
 * A crash in the air that is not a landing (TriggerCrash from the spot) keeps the jump open until
 * the crash recovery's reset drops it.
 *
 * A finalised record also gets the landing verdict's cause (LastLandingCause) and its trick fields:
 * the signature from TrickRecognition::SignatureFromJump (which reads the rotation fields),
 * TrickNaming::Name and FamilyKey, the grade, and the score from TrickScoring::ScoreJump.
 * The grade is the board's verdict (docs/tricks/README.md decision 6) through
 * TrickScoring::GradeFromVerdict when the snapshot has one (bHasLandingVerdict), otherwise the
 * signature's TrickScoring::GradeLanding over the record; a crash outcome is Crash either way. The
 * score's execution factor follows that grade.
 * RepeatFactor is left at 1; FJumpSession applies it.
 */
class KITESURF_API FJumpRecorder
{
public:
	FJumpRecorder() = default;
	explicit FJumpRecorder(const FJumpRecorderSettings& InSettings) : Settings(InSettings) {}

	/** Feeds one step. True when a record was finalised this step; OutRecord is filled only then. */
	bool Step(const FJumpRecorderInput& In, FJumpRecord& OutRecord);

	/** A take-off was seen and the jump has not ended. */
	bool IsJumpOpen() const { return bOpen; }

	/** The jump in progress: take-off facts, peak tension and lowest kite elevation so far. Meaningful while IsJumpOpen. */
	const FJumpRecord& GetLive() const { return Live; }

	/** Records finalised so far; the next record's Index. */
	int32 GetRecordedCount() const { return RecordedCount; }

	/** The rotation recogniser of the jump in progress (or of the last jump). */
	const FRotationRecognizer& GetRotation() const { return Rotation; }

	/** Drops any open jump, forgets the board counters (the next step only reads them again) and restarts Index at 0. */
	void Reset();

	FJumpRecorderSettings Settings;

private:
	void Open(const FJumpRecorderInput& In);
	void Accumulate(const FJumpRecorderInput& In);
	void Finalise(const FJumpRecorderInput& In, FJumpRecord& OutRecord);
	void CollectLoops(const FJumpRecorderInput& In, FJumpRecord& Record) const;
	/** Copies a rotation result into a record's rotation fields. */
	static void ApplyRotation(const FRotationResult& Rotation, FJumpRecord& Record);
	FJumpLoop MakeJumpLoop(const FKiteLoopRecord& Loop, const FJumpRecord& Record) const;

	bool bPrimed = false;
	int32 LastTakeoffCount = 0;
	int32 LastJumpCount = 0;
	int32 LastResetCount = 0;

	bool bOpen = false;
	FJumpRecord Live;
	/** Kite time minus board time, read at take-off (s): the two clocks advance together. */
	float KiteMinusBoardSeconds = 0.0f;
	/** Highest board Z seen in the air and the board time of that step, for the apex time when the board does not give it. */
	float HighestZCm = 0.0f;
	float HighestZTimeSeconds = 0.0f;

	/** The rider's rotation in this jump, and whether any attitude step was counted. */
	FRotationRecognizer Rotation;
	bool bRotationStepped = false;
	/** Board time of the last step seen while open (s), for the recogniser's step length. */
	float LastStepTimeSeconds = 0.0f;

	int32 RecordedCount = 0;
};

/**
 * A session of jumps: an FJumpRecorder, the repeat counts per family key, and the last
 * MaxRecords records. Pure; the tracker component owns one.
 *
 * A landed jump is counted in the session's FTrickSession and its RepeatFactor set to the share
 * paid (1, 0.75, 0.5, ...). A crash scores 0 and is not a landing, so it is not counted; its
 * RepeatFactor shows what a landing would have been paid.
 */
class KITESURF_API FJumpSession
{
public:
	FJumpSession() = default;
	explicit FJumpSession(const FJumpRecorderSettings& InSettings) : Recorder(InSettings) {}

	/** Steps the recorder. True when a record was finalised and added; OutRecord (when given) gets it. */
	bool Step(const FJumpRecorderInput& In, FJumpRecord* OutRecord = nullptr);

	/** The kept records, oldest first (at most MaxRecords). */
	const TArray<FJumpRecord>& GetRecords() const { return Records; }

	/** Records finalised since construction or the last Clear, including any no longer kept. */
	int32 GetRecordCount() const { return Recorder.GetRecordedCount(); }

	/** The newest record; false when there is none. */
	bool GetLastRecord(FJumpRecord& Out) const;

	/** Points paid this session: the sum of Score.Total x RepeatFactor over every record. */
	float GetSessionPoints() const { return SessionPoints; }

	const FTrickSession& GetTrickSession() const { return Tricks; }

	const FJumpRecorder& GetRecorder() const { return Recorder; }
	FJumpRecorder& GetRecorder() { return Recorder; }

	/** Forgets the records, the repeat counts and the points, and resets the recorder. */
	void Clear();

	/** Records kept; the oldest go first. GetRecordCount keeps counting. */
	int32 MaxRecords = 200;

private:
	FJumpRecorder Recorder;
	FTrickSession Tricks;
	TArray<FJumpRecord> Records;
	float SessionPoints = 0.0f;
};
