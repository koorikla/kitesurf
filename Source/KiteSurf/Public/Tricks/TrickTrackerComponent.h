#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Tricks/JumpRecorder.h"
#include "Tricks/KiteLoopRecord.h"
#include "TrickTrackerComponent.generated.h"

class UBoardMovementComponent;
class UKiteComponent;
class URiderAttitudeComponent;
class FGrabState;

/**
 * Follows the rider's jumps and names, grades and scores each one (docs/tricks/T0.md sections 3
 * and 4). It does not tick: AKiteRiderPawn::StepSimulation calls StepTracker once per fixed step,
 * after the kite and the board have stepped.
 *
 * It polls the public getters of the board and the kite, so it works in tests where no delegate is
 * bound, and feeds an FJumpSession (FJumpRecorder, naming, scoring, repeats). Every fact comes from
 * the component that owns it; the tracker derives nothing itself:
 * - Take-offs: UBoardMovementComponent::GetTakeoffCount, WasLastTakeoffPopped and
 *   GetLastTakeoffTimeSeconds, counted in the board's BeginAirborne (the pop and the kite lift-off).
 * - Apex time: GetCurrentJumpApexTimeSeconds, the board time at which the jump was highest.
 * - Sink rate, landing g and landing angle (the record's LandingYawDeg): GetLastLandingSinkMS,
 *   GetLastLandingG (physics phase 2 item 4: the sink relative to the water's surface, taken out
 *   over the absorb distance the crouch lengthens) and GetLastLandingAngleDeg, read when the board
 *   counts the jump.
 * - Kite loops: UKiteComponent::GetLoopRecords and GetOpenLoop, which the kite builds from its own
 *   per-step heading turn (T0.3).
 * - Rider rotation (T1.6): URiderAttitudeComponent's body orientation and angular velocity (found
 *   on the owner), counted by the recorder's FRotationRecognizer on every step the attitude is
 *   simulated: inversions, spin half turns and the landing stance.
 * - Landing cause: GetLastLandingVerdict().Cause, the evaluator's reason for grading it down.
 * - Grabs and the one-footer (T2.1, T2.2): the owner's FGrabState (SetGrabSource), stepped before
 *   the board, so on the touchdown step a grab still held is counted to the landing.
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

	/** The rider's grabs and one-footer (AKiteRiderPawn's FGrabState); null for none. The pawn sets it in its constructor. */
	void SetGrabSource(const FGrabState* InGrabs) { GrabSource = InGrabs; }

	/** One fixed step, after the kite's and the board's: reads them and steps the jump session. */
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

private:
	void BuildLiveJump(const FJumpRecorderInput& In);

	UPROPERTY(Transient)
	TObjectPtr<UBoardMovementComponent> Board;

	UPROPERTY(Transient)
	TObjectPtr<UKiteComponent> Kite;

	/** The rider attitude, found on the owner at the first step; null for a pawn without one (no rotation is counted). */
	UPROPERTY(Transient)
	TObjectPtr<URiderAttitudeComponent> Attitude;

	/** Owned by the pawn, which outlives this component. */
	const FGrabState* GrabSource = nullptr;

	FJumpSession Session;
	FJumpRecord LiveJump;
};
