#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Tricks/JumpRecorder.h"
#include "Tricks/KiteLoopTracker.h"
#include "TrickTrackerComponent.generated.h"

class UBoardMovementComponent;
class UKiteComponent;

/**
 * Follows the rider's jumps and names, grades and scores each one (docs/tricks/T0.md section 4,
 * the light wiring). It does not tick: AKiteRiderPawn::StepSimulation calls StepTracker once per
 * fixed step, after the board has stepped.
 *
 * It only polls the public getters of the board and the kite, so it works in tests where no
 * delegate is bound, and it feeds the pure modules: an FKiteLoopTracker for the kite's loops and
 * an FJumpSession (FJumpRecorder, naming, scoring, repeats) for the jumps. What the board and kite
 * do not expose yet is synthesised here, and each piece is replaced by the board's or kite's own
 * value once physics phase 2 adds it:
 * - Take-offs are counted from the board entering Airborne (no GetTakeoffCount yet). The take-off
 *   time is the board time less GetCurrentJumpAirtime.
 * - Popped: the board counts airtime for the step it took off in only when the take-off happened
 *   before its step, which is what a pop does (Jump is called between steps, by the pawn's load
 *   and pop or the jump key); a kite lift-off starts inside the step with no airtime yet. So a
 *   take-off that already has airtime on its first step is taken as popped. An inference from
 *   GetCurrentJumpAirtime, to be replaced by the board's WasLastTakeoffPopped.
 * - Apex time: the step the board was seen highest (the recorder's fallback).
 * - Sink rate and landing g: the board's own, GetLastLandingSinkMS and GetLastLandingG (physics
 *   phase 2 item 4: the sink relative to the water's surface, taken out over the board's absorb
 *   distance, which the crouch lengthens), read when the board counts the jump.
 * - Landing yaw: 0 (the board keeps its landing angle private), so it never grades a landing down.
 * - Kite loops: the heading turn per step is the signed angle between successive GetKiteHeading
 *   values about the line (rider to kite), the axis the kite turns about. That also counts the small
 *   turn of the heading carried along the sphere. T0.3's hookup replaces it with the kite's own
 *   per-step TurnRad once the kite exposes it after phase 2.
 *
 * Finished jumps go to UKiteSurfGameInstance::RecordTrickLanding when the world has that game
 * instance; nothing is written to disk here.
 */
UCLASS(ClassGroup = (KiteSurf), meta = (BlueprintSpawnableComponent))
class KITESURF_API UTrickTrackerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTrickTrackerComponent();

	/** The board and kite to follow. The pawn sets them in its constructor. */
	void SetSources(UBoardMovementComponent* InBoard, UKiteComponent* InKite);

	/** One fixed step, after the board's: reads the board and kite, steps the loop tracker and the jump session. */
	void StepTracker(float StepSeconds);

	/** The session: records, repeat counts and points. Lasts as long as the component (a level load clears it). */
	const FJumpSession& GetJumpSession() const { return Session; }

	/** The kept records, oldest first (at most the session's MaxRecords). */
	const TArray<FJumpRecord>& GetJumpRecords() const { return Session.GetRecords(); }

	/** Records finalised since the start or the last ClearSession; goes up by one per jump. */
	UFUNCTION(BlueprintPure, Category = "Tricks")
	int32 GetJumpRecordCount() const { return Session.GetRecordCount(); }

	/** The newest record; false when there is none. */
	UFUNCTION(BlueprintPure, Category = "Tricks")
	bool GetLastJumpRecord(FJumpRecord& OutRecord) const { return Session.GetLastRecord(OutRecord); }

	/** A take-off was seen and the jump has not ended. */
	UFUNCTION(BlueprintPure, Category = "Tricks")
	bool IsJumpInProgress() const { return Session.GetRecorder().IsJumpOpen(); }

	/**
	 * The jump in progress: the recorder's live record (take-off facts, height and airtime so far)
	 * with the kite loops flown since the take-off, including an open run of at least the
	 * recorder's MinOpenLoopDeg. Meaningful while IsJumpInProgress.
	 */
	const FJumpRecord& GetLiveJump() const { return LiveJump; }

	/** Forgets the session's records, repeats and points. The kite's loop records are kept. */
	UFUNCTION(BlueprintCallable, Category = "Tricks")
	void ClearSession();

	/** The loop tracker fed from the kite's heading. */
	const FKiteLoopTracker& GetLoopTracker() const { return LoopTracker; }

	/** Turn of the kite's heading in the last step (deg, + to the rider's right), as fed to the loop tracker. */
	float GetLastStepTurnDeg() const { return LastStepTurnDeg; }

	/** Take-offs seen so far: the board entering Airborne. */
	int32 GetTakeoffCount() const { return TakeoffCount; }

	/** A heading change larger than this in one step is a teleport (reset, relaunch), not a turn: the open loop run is dropped (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Tricks", meta = (ClampMin = "1"))
	float MaxStepTurnDeg = 45.0f;

	/**
	 * Signed turn from one kite heading to the next about the line direction (deg): positive when
	 * the nose turns towards Nose x LineDir, the kite's right and the sign of UKiteComponent::GetTurnDeg.
	 * Both headings are projected onto the plane across the line first. 0 for degenerate input.
	 */
	static float SignedHeadingTurnDeg(const FVector& PreviousHeading, const FVector& Heading, const FVector& LineDir);

private:
	void StepLoops();
	void BuildLiveJump(const FJumpRecorderInput& In);

	UPROPERTY(Transient)
	TObjectPtr<UBoardMovementComponent> Board;

	UPROPERTY(Transient)
	TObjectPtr<UKiteComponent> Kite;

	FJumpSession Session;
	FKiteLoopTracker LoopTracker;
	FJumpRecord LiveJump;

	bool bHasPrevious = false;
	bool bWasAirborne = false;
	int32 TakeoffCount = 0;
	bool bLastTakeoffPopped = false;
	float LastTakeoffTimeSeconds = 0.0f;
	/** Kite clock at the last take-off (s), to place loops in the live jump. */
	float TakeoffKiteTimeSeconds = 0.0f;
	float LastLandingG = 1.0f;
	float LastSinkCmS = 0.0f;
	int32 LastJumpCount = 0;

	bool bHasHeading = false;
	FVector PreviousHeading = FVector::ZeroVector;
	float LastStepTurnDeg = 0.0f;
	int32 SeenResetCount = 0;
};
