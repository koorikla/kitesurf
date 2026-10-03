#pragma once

#include "CoreMinimal.h"
#include "Tricks/FreestyleScoring.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/TrickRecognition.h"
#include "Tricks/TrickScoring.h"
#include "FreestyleHeat.generated.h"

/** Where a freestyle heat is (T3.6). */
UENUM(BlueprintType)
enum class EFreestyleHeatPhase : uint8
{
	/** Never started, or cancelled. */
	Idle     UMETA(DisplayName = "Idle"),
	/** Attempts left to fly. */
	Running  UMETA(DisplayName = "Running"),
	/** Every attempt used: the results stand until the next Start. */
	Finished UMETA(DisplayName = "Finished")
};

/** What one attempt of a heat was. */
UENUM(BlueprintType)
enum class EHeatAttemptKind : uint8
{
	/** An unhooked jump of more than the minimum airtime, scored with FreestyleTrickScore. */
	Trick    UMETA(DisplayName = "Trick"),
	/** A crashed jump (bar lost, board lost, crash landing): 0. */
	Crash    UMETA(DisplayName = "Crash"),
	/** The trick countdown ran out with nothing tried: 0. */
	TimedOut UMETA(DisplayName = "Timed out")
};

/** What the heat made of a finished jump offered to it. */
UENUM(BlueprintType)
enum class EHeatRecordVerdict : uint8
{
	/** An unhooked jump over the minimum airtime: an attempt, scored. */
	Attempt     UMETA(DisplayName = "Attempt"),
	/** A crash (any, hooked or not): an attempt that scores 0. */
	Crash       UMETA(DisplayName = "Crash"),
	/** A hooked jump that was landed: not an attempt ("Unhook for freestyle"). */
	Hooked      UMETA(DisplayName = "Hooked"),
	/** An unhooked hop of the minimum airtime or less that was landed: not an attempt. */
	TooShort    UMETA(DisplayName = "Too short"),
	/** The heat is not running, or the jump took off before it started: not an attempt. */
	OutsideHeat UMETA(DisplayName = "Outside the heat")
};

/** One attempt of a heat. */
USTRUCT(BlueprintType)
struct FHeatAttempt
{
	GENERATED_BODY()

	/** Family, score (0 for a crash or a time-out) and name, as ScoreFreestyleHeat takes it. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	FScoredTrick Trick;

	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	EHeatAttemptKind Kind = EHeatAttemptKind::Trick;

	/** The jump's apex above its take-off (m); 0 for a time-out. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	float ApexM = 0.0f;

	/** The record's landing grade; Crash for a crash or a time-out. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	ELandingGrade Grade = ELandingGrade::Crash;

	/** The record's index in the tracker's session; -1 for a time-out. */
	UPROPERTY(BlueprintReadOnly, Category = "Tricks")
	int32 RecordIndex = -1;
};

/** Tunables of FFreestyleHeat. */
USTRUCT(BlueprintType)
struct FFreestyleHeatSettings
{
	GENERATED_BODY()

	/** Counting, group limits, variety bonus; Attempts is set by Start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	FFreestyleHeatRules Rules;

	/** An unhooked jump must be in the air longer than this to be an attempt (s; docs/tricks/T3.md T3.6). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks", meta = (ClampMin = "0.0"))
	float MinAirtimeSeconds = 0.4f;

	/**
	 * The trick countdown (GKA format): each attempt must be tried within this long of the last one
	 * (or of the start), or it is lost and scores 0 (s of board time). 0 or less turns it off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float TrickCountdownSeconds = 90.0f;

	/** Execution factors and the grab hold the scores use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	FTrickScoringSettings Scoring;
};

/**
 * A GKA-style freestyle heat (T3.6, docs/tricks.md 3.6 and 6.8): a fixed number of attempts, each
 * scored with FreestyleScoring::FreestyleTrickScore from its jump record, and the heat scored with
 * ScoreFreestyleHeat (best per family, four counting within the group limits, variety bonus). Pure:
 * no UObject, no world. UFreestyleHeatSubsystem owns one and feeds it the trick tracker's records,
 * as UTrickSessionSubsystem feeds FBestThreeSession.
 *
 * Rules:
 * - An attempt is an unhooked jump with more than MinAirtimeSeconds (0.4 s) of airtime, or any crash
 *   (bar lost, board lost, a crash landing; hooked or not). A crash scores 0.
 * - A landed hooked jump is not an attempt (the HUD says "Unhook for freestyle"), nor is a landed
 *   unhooked hop of 0.4 s or less, nor a jump that took off before the start.
 * - An unhooked jump that is no freestyle trick (a plain unhooked pop: family None) uses up its
 *   attempt and never counts, as a wasted trick in a real heat.
 * - The trick countdown (TrickCountdownSeconds, 90 s; off at 0): each attempt must be tried within
 *   that long of the last attempt or the start. When it runs out with the rider on the water the
 *   attempt is lost (TimedOut, 0). With a jump in the air it waits for that jump's record: if the
 *   jump is an attempt it counts, otherwise the attempt is lost on the next tick.
 * - The heat finishes when the last attempt is in.
 *
 * Clock: Tick advances the countdown by the seconds given; the caller ticks with the board's
 * simulation time, so pausing pauses the heat. Offer each step's records before ticking.
 */
class KITESURF_API FFreestyleHeat
{
public:
	FFreestyleHeat() = default;
	explicit FFreestyleHeat(const FFreestyleHeatSettings& InSettings) : Settings(InSettings) {}

	/** The attempt count the pause menu and kitesurf.Heat start with (GKA: about seven). */
	static constexpr int32 DefaultAttempts = 7;
	static constexpr int32 MaxAttempts = 20;

	/** Starts (or restarts) a heat of Attempts (1..MaxAttempts) at the clock reading StartClockSeconds; forgets the last one. */
	void Start(int32 Attempts = DefaultAttempts, float StartClockSeconds = 0.0f);

	/** Back to Idle with nothing kept (a cancelled heat). */
	void Cancel();

	/** What a record would be to a running heat, ignoring the take-off window. */
	static EHeatRecordVerdict Classify(const FJumpRecord& Record, float MinAirtimeSeconds = 0.4f);

	/**
	 * The record as a heat trick: the signature rebuilt with TrickRecognition::SignatureFromJump, the
	 * grade the record's (the board's verdict), the family and difficulty from
	 * TrickNaming::FreestyleFamily, the score from FreestyleTrickScore with the record's apex in
	 * metres, and the record's own trick name. A crashed record scores 0.
	 */
	static FScoredTrick ScoreRecord(const FJumpRecord& Record, const FTrickScoringSettings& Scoring = FTrickScoringSettings(),
		const FLoopClassifySettings& LoopClassify = FLoopClassifySettings(), const FLandingGradeSettings& LandingGrade = FLandingGradeSettings());

	/** Offers a finished jump. Attempt or Crash when the heat took it as an attempt. */
	EHeatRecordVerdict OfferRecord(const FJumpRecord& Record);

	/**
	 * Advances the countdown by Seconds. JumpInAir is the tracker's live jump, or null on the water:
	 * a jump in the air that took off in the heat holds a countdown that has run out.
	 */
	void Tick(float Seconds, const FJumpRecord* JumpInAir = nullptr);

	EFreestyleHeatPhase GetPhase() const { return Phase; }
	bool IsActive() const { return Phase == EFreestyleHeatPhase::Running; }
	bool IsFinished() const { return Phase == EFreestyleHeatPhase::Finished; }

	/** Every attempt so far, in order. */
	const TArray<FHeatAttempt>& GetAttempts() const { return Attempts; }
	int32 GetAttemptCount() const { return Attempts.Num(); }
	int32 GetAttemptLimit() const { return AttemptLimit; }

	/** The attempt being flown now, 1-based ("Trick 3/7"); the limit once finished. */
	int32 GetCurrentTrickNumber() const { return FMath::Min(Attempts.Num() + 1, FMath::Max(AttemptLimit, 1)); }

	/** ScoreFreestyleHeat over the attempts so far. */
	const FHeatResult& GetResult() const { return Result; }
	float GetTotal() const { return Result.Total; }

	/** The counting tricks, best first. */
	TArray<FScoredTrick> GetCounting() const;

	/** The families of the counting tricks, best trick first (one each). */
	TArray<EGkaFamily> GetFamiliesUsed() const;

	/** The countdown is on (TrickCountdownSeconds > 0 at the start). */
	bool IsCountdownOn() const { return CountdownSeconds > 0.0f; }

	/** Seconds left to try the current attempt; 0 when the countdown is off or has run out. */
	float GetCountdownLeft() const { return IsCountdownOn() ? CountdownLeft : 0.0f; }

	/** The countdown has run out and a jump in the air holds it. */
	bool IsCountdownHeld() const { return bCountdownHeld; }

	/** Clock seconds since the heat finished; 0 before. */
	float GetSecondsSinceFinished() const { return SecondsSinceFinished; }

	/** The record took off at or after the start. */
	bool TookOffInHeat(const FJumpRecord& Record) const { return Record.TakeoffTimeSeconds >= StartClockSeconds - KINDA_SMALL_NUMBER; }

	FFreestyleHeatSettings Settings;

	/** The recorder's settings the scores rebuild signatures with; the subsystem copies the tracker's. */
	FLoopClassifySettings LoopClassify;
	FLandingGradeSettings LandingGrade;

private:
	void AddAttempt(const FHeatAttempt& Attempt);
	void Rescore();

	EFreestyleHeatPhase Phase = EFreestyleHeatPhase::Idle;
	int32 AttemptLimit = DefaultAttempts;
	float StartClockSeconds = 0.0f;
	float CountdownSeconds = 0.0f;
	float CountdownLeft = 0.0f;
	bool bCountdownHeld = false;
	float SecondsSinceFinished = 0.0f;
	TArray<FHeatAttempt> Attempts;
	FHeatResult Result;
};

namespace FreestyleHeat
{
	/** A short family label for the HUD's counting list: "Raley", "KGB/Slim", "Hinter/Heart", "Mobes", "Rewinds", "Toeside/Blind", "Combos", "Inv. doubles", "Loop passes"; "--" for None. */
	KITESURF_API FString FamilyLabel(EGkaFamily Family);

	/** Points as the HUD shows a bonus: whole numbers without decimals ("7"), otherwise to 1 dp ("2.5"). */
	KITESURF_API FString FormatBonus(float Points);
}
