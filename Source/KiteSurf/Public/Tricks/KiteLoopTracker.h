#pragma once

#include "CoreMinimal.h"
#include "Tricks/KiteLoopRecord.h"
#include "KiteLoopTracker.generated.h"

/**
 * One kite step as the loop tracker sees it. TurnDeg is the kite's own heading turn during the
 * step that ends at TimeSeconds (the steering turn, positive to the rider's right, the sign of
 * UKiteComponent::GetTurnDeg), not a difference of headings, so the sphere projection and the
 * slack-line re-orientation do not count as turning.
 */
struct FKiteLoopSample
{
	/** Kite simulation time at the end of the step (s). */
	float TimeSeconds = 0.0f;

	/** Heading turn during the step (deg, signed: + right, - left). */
	float TurnDeg = 0.0f;

	/** Kite elevation above the horizon (deg). */
	float ElevationDeg = 0.0f;

	/** Line tension (N). */
	float TensionN = 0.0f;

	/** Rider height, world Z (cm). */
	float RiderZCm = 0.0f;

	/** Rider velocity (cm/s), for the side the rider was travelling to when a loop started. */
	FVector RiderVelocity = FVector::ZeroVector;

	/** Horizontal downwind direction (unit). */
	FVector DownwindDir = FVector::ForwardVector;

	/** The lines are taut. Carried for the kite hookup and for consumers; the tracker does not use it (a slack kite that stops turning ends a run by the stall rule). */
	bool bFlying = true;

	/** The kite is on the water. Ends a run and marks its record bKiteCrashed. */
	bool bCrashed = false;
};

/** Tunables of FKiteLoopTracker (docs/tricks/T0.md section 3). All estimates, to be tuned once loops fly in the air. */
USTRUCT(BlueprintType)
struct FKiteLoopTrackerSettings
{
	GENERATED_BODY()

	/** Same-sign turn (deg) before a run counts as started. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float StartTurnDeg = 30.0f;

	/** Counter-turn back from the run's furthest point (deg) that ends it; the counter-turn starts the opposite run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float ReverseToleranceDeg = 45.0f;

	/** A heading turn rate under this (deg/s)... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float StallRateDegPerS = 20.0f;

	/** ...held this long (s) ends a run: the kite is parked or travelling, not looping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float StallSeconds = 0.6f;

	/** An unfinished part of a run at least this long (deg) is kept as an incomplete record: S-loop halves, a loop cut short. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float MinPartialDeg = 180.0f;

	/** The rider's horizontal direction of travel must be at least this far along crosswind (cosine) for RiderTravelSide to be +1 or -1; otherwise 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float TravelSideMinDot = 0.25f;

	/** Slower than this (cm/s, horizontal) the rider has no travel side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	float TravelSideMinSpeedCmS = 50.0f;

	/** Records kept; the oldest go first. GetTotalRecorded keeps counting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks")
	int32 MaxRecords = 64;
};

/**
 * Turns a kite's per-step heading turn into loop records, independent of the kite's looping
 * state (T0.3). Pure: no UObject, no world. UKiteComponent owns one and feeds it a sample per
 * fixed step (UKiteComponent::GetLoopRecords).
 *
 * - Pending: turn adds up while it keeps one sign; a step slower than StallRateDegPerS, or a turn
 *   of the other sign, starts it again. Once it reaches StartTurnDeg a run opens, starting where
 *   the pending turn started.
 * - Running: every multiple of 360 the run passes emits a completed record, and the next loop
 *   starts at that crossing.
 * - A run ends on a counter-turn of more than ReverseToleranceDeg from its furthest point (the
 *   counter-turn starts the opposite run's pending turn), on StallSeconds of stall, or on a kite
 *   crash (the record is marked bKiteCrashed). The unfinished loop is kept as an incomplete record
 *   when it reached MinPartialDeg. Records end at the run's furthest point (a crash: at the crash).
 * - Each record keeps its own start elevation, lowest elevation, peak tension and its time, rider
 *   height and travel side at the start.
 */
class KITESURF_API FKiteLoopTracker
{
public:
	FKiteLoopTracker() = default;
	explicit FKiteLoopTracker(const FKiteLoopTrackerSettings& InSettings) : Settings(InSettings) {}

	/** Feeds one kite step. Samples must come in time order. */
	void Step(const FKiteLoopSample& Sample);

	/** Drops the open run and any pending turn without a record: the kite was teleported or placed. Records are kept. */
	void CancelRun();

	/** The kept records, oldest first (at most Settings.MaxRecords). */
	const TArray<FKiteLoopRecord>& GetRecords() const { return Records; }

	/** Records emitted since construction, including any no longer kept. */
	int32 GetTotalRecorded() const { return TotalRecorded; }

	/** The loop being flown now as a provisional, incomplete record (turn since the last whole loop); false when no run is open. */
	bool GetOpenRun(FKiteLoopRecord& Out) const;

	/** +1 or -1 while a run is open, 0 otherwise. */
	int32 GetRunDirection() const { return bRunning ? RunDirection : 0; }

	/** Total turn of the open run including its whole loops (deg, a magnitude); 0 when no run is open. */
	float GetRunTurnDeg() const { return bRunning ? RunTurnDeg : 0.0f; }

	FKiteLoopTrackerSettings Settings;

private:
	/** Extremes of the samples since some start time. */
	struct FExtremes
	{
		float StartTimeSeconds = 0.0f;
		float StartElevationDeg = 0.0f;
		float MinElevationDeg = 90.0f;
		float PeakTensionN = 0.0f;
		float PeakTensionTimeSeconds = 0.0f;
		float RiderZAtStartCm = 0.0f;
		int32 RiderTravelSide = 0;

		void Begin(float TimeSeconds, const FKiteLoopSample& Sample, int32 TravelSide);
		void Add(const FKiteLoopSample& Sample);
		void Merge(const FExtremes& Later);
	};

	int32 TravelSideOf(const FKiteLoopSample& Sample) const;
	void StepPending(const FKiteLoopSample& Sample, float StepStartSeconds, float RateDegPerS);
	void OpenRun(int32 Direction, float TurnDeg, const FExtremes& From);
	void EndRun(bool bCrashed, float CrashTimeSeconds);
	void Emit(FKiteLoopRecord Record);
	FKiteLoopRecord MakeRecord(const FExtremes& Loop, float EndTimeSeconds, float TurnDeg, bool bCompleted) const;

	TArray<FKiteLoopRecord> Records;
	int32 TotalRecorded = 0;

	bool bHasLastTime = false;
	float LastTimeSeconds = 0.0f;

	// Pending phase.
	float PendingTurnDeg = 0.0f;
	FExtremes Pending;

	// Open run.
	bool bRunning = false;
	int32 RunDirection = 0;
	/** Turn along RunDirection since the run opened (deg); dips by up to ReverseToleranceDeg on a counter-turn. */
	float RunTurnDeg = 0.0f;
	/** Furthest RunTurnDeg reached, and when. */
	float RunPeakDeg = 0.0f;
	float RunPeakTimeSeconds = 0.0f;
	/** Whole loops already emitted in this run. */
	int32 CompletedLoops = 0;
	/** Extremes of the current loop up to the run's furthest point... */
	FExtremes LoopAtPeak;
	/** ...and of the samples after it (the counter-turn), which open the next run if the run reverses. */
	FExtremes SincePeak;
	bool bHasSincePeak = false;
	/** Time spent under the stall rate (s). */
	float StallTimeSeconds = 0.0f;
};
