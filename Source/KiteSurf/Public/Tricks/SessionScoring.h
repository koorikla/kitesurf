#pragma once

#include "CoreMinimal.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/TrickScoring.h"
#include "SessionScoring.generated.h"

/** Where a best-three session is (T2.5, backlog F5). */
UENUM(BlueprintType)
enum class EBestThreePhase : uint8
{
	/** Never started. */
	Idle     UMETA(DisplayName = "Idle"),
	/** The clock is running. */
	Running  UMETA(DisplayName = "Running"),
	/** The horn has gone with a jump in the air that took off before it; waiting for its landing. */
	Overtime UMETA(DisplayName = "Overtime"),
	/** Over: the results stand until the next Start. */
	Finished UMETA(DisplayName = "Finished")
};

/** Tunables of FBestThreeSession. Every value is an estimate. */
USTRUCT(BlueprintType)
struct FBestThreeSettings
{
	GENERATED_BODY()

	/** How many jumps count towards the total, each from a different family key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks", meta = (ClampMin = "1"))
	int32 CountingJumps = 3;

	/** Longest wait after the horn for a jump that took off before it (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks", meta = (ClampMin = "0.0"))
	float OvertimeMaxSeconds = 15.0f;

	/**
	 * Jumps lower than this above their take-off are ignored (cm): a hop neither scores nor uses up
	 * a family's first, full-value landing. The HUD's trick card uses the same metre.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks", meta = (ClampMin = "0.0"))
	float MinJumpHeightCm = 100.0f;

	/** Repeat factors (RepeatFactors) for the session's own repeat count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	FTrickScoringSettings Scoring;
};

/**
 * A timed big air session in which the best three jumps count (T2.5, docs/research.md F5,
 * docs/tricks.md 6.8). Pure: no UObject, no world. UTrickSessionSubsystem owns one and feeds it
 * the trick tracker's records.
 *
 * Clock: Tick advances the session by the seconds given. Records carry board simulation times, so
 * Start takes the clock reading at the start (StartClockSeconds) and a record took off at
 * Record.TakeoffTimeSeconds - StartClockSeconds into the session. The caller ticks with the same
 * clock (the subsystem ticks with the board's simulation time), so pausing pauses the session.
 *
 * Rules:
 * - Only jumps that took off inside the session count: at or after the start and before the horn
 *   (Duration). A jump already in the air at Start does not count.
 * - The horn: a jump in the air when time runs out counts if it took off before the horn, as in
 *   real heats. The session goes to Overtime and finishes at that jump's landing (or when it is
 *   dropped, or after OvertimeMaxSeconds). A take-off after the horn never counts.
 * - Repeats: the session counts landings per family key from zero (free-ride repeats before Start
 *   do not carry over) and pays each landing Score.Total x RepeatFactor: 1, 0.75, 0.5, 0.25, then
 *   0.1 (TrickScoring::RepeatFactor). The session's copy of the record carries that factor.
 * - Counting: each family key counts once, with its best paid landing, and the total is the sum
 *   of the best CountingJumps (3) of those. So a repeat of the same quality never raises the total
 *   (it is paid 0.75 of what is already counted). A repeat raises it only by beating the counted
 *   landing by more than 1 / 0.75 on its raw score, which is GKA's "counts once, the best one".
 * - A crash scores 0 and is kept in the jump list, but it does not use up a repeat and never counts.
 * - Jumps under MinJumpHeightCm are ignored altogether.
 */
class KITESURF_API FBestThreeSession
{
public:
	FBestThreeSession() = default;
	explicit FBestThreeSession(const FBestThreeSettings& InSettings) : Settings(InSettings) {}

	/** Starts (or restarts) a session of DurationSeconds; forgets the last one's jumps and repeats. */
	void Start(float DurationSeconds, float StartClockSeconds = 0.0f);

	/**
	 * Advances the clock by Seconds. JumpInAir is the jump in progress (the tracker's live record)
	 * or null when the rider is not in the air: at the horn, a live jump that took off inside the
	 * session holds the session in Overtime until it lands. Feed the step's finished records with
	 * AddJump before ticking, so an overtime landing is counted before the session finishes.
	 */
	void Tick(float Seconds, const FJumpRecord* JumpInAir = nullptr);

	/** Offers a finished jump. True when the session took it (in time, high enough); a crash taken scores 0. */
	bool AddJump(const FJumpRecord& Record);

	/** The best CountingJumps paid scores, one per family key, summed. */
	float GetTotal() const;

	/** The counting jumps, best first: the session's copies, RepeatFactor set to what the session paid. */
	TArray<FJumpRecord> GetCounting() const;

	/** Every jump the session took, in order, with the session's RepeatFactor. */
	const TArray<FJumpRecord>& GetJumps() const { return Jumps; }

	/** Seconds until the horn; 0 from the horn on. */
	float GetTimeLeft() const;

	/** Over: the results stand. */
	bool IsFinished() const { return Phase == EBestThreePhase::Finished; }

	/** Running or in overtime. */
	bool IsActive() const { return Phase == EBestThreePhase::Running || Phase == EBestThreePhase::Overtime; }

	EBestThreePhase GetPhase() const { return Phase; }
	float GetDuration() const { return DurationSeconds; }
	float GetElapsed() const { return ElapsedSeconds; }

	/** Clock seconds since the session finished; 0 before. */
	float GetSecondsSinceFinished() const { return SecondsSinceFinished; }

	/** Seconds into the session at which a record took off (negative before the start). */
	float SessionTimeOf(float ClockSeconds) const { return ClockSeconds - StartClockSeconds; }

	/** The record took off at or after the start and before the horn. */
	bool TookOffInWindow(const FJumpRecord& Record) const;

	/** What a record pays: Score.Total x RepeatFactor, and 0 for a crash. */
	static float Paid(const FJumpRecord& Record);

	FBestThreeSettings Settings;

private:
	void Finish();

	EBestThreePhase Phase = EBestThreePhase::Idle;
	float DurationSeconds = 0.0f;
	float StartClockSeconds = 0.0f;
	float ElapsedSeconds = 0.0f;
	float SecondsSinceFinished = 0.0f;
	TArray<FJumpRecord> Jumps;
	FTrickSession Repeats;
};
