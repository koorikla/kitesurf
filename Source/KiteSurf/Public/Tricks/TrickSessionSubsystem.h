#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tricks/SessionScoring.h"
#include "TrickSessionSubsystem.generated.h"

class UBoardMovementComponent;
class UTrickTrackerComponent;

/**
 * Runs the best-three session (T2.5, docs/tricks/T2.md): one FBestThreeSession per world, fed
 * from the rider's UTrickTrackerComponent.
 *
 * A world subsystem rather than the game mode or a pawn component (the T2.5 plan): test worlds
 * have no game mode, the HUD, the console and the pause menu reach it through the world, and the
 * pawn and its step stay untouched.
 *
 * It polls, as the HUD's trick card does: each step it reads the tracker's record count and offers
 * every new record to the session, then ticks the session with the board's simulation time and the
 * tracker's live jump. Delegates are not bound in test worlds, so nothing here uses them. In the
 * game it ticks with the world (not while paused); tests call StepSession after each pawn tick.
 *
 * At the end the total goes to UKiteSurfGameInstance::RecordSessionTotal as the local best for
 * the session's length, and a new best is saved through SaveSettingsToDisk. Test worlds have no
 * game instance, so they never write a save.
 */
UCLASS()
class KITESURF_API UTrickSessionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The session length the pause menu and kitesurf.Session start (s). */
	static constexpr float DefaultSessionSeconds = 90.0f;

	/**
	 * Starts a session of DurationSeconds for Tracker's rider, or for the player's rider in this
	 * world when Tracker is null. Restarts a running one. False when there is no rider to follow.
	 */
	bool StartSession(float DurationSeconds = DefaultSessionSeconds, UTrickTrackerComponent* Tracker = nullptr);

	/**
	 * One step: offers the tracker's new records to the session, then ticks it by the board's
	 * simulation time since the last step (FallbackDeltaSeconds when there is no board). Finishes
	 * the session's results when it ends. Tick calls it; tests call it after each pawn tick.
	 */
	void StepSession(float FallbackDeltaSeconds = 0.0f);

	const FBestThreeSession& GetSession() const { return Session; }

	/** Running or in overtime. */
	bool IsSessionActive() const { return Session.IsActive(); }

	/** The session has finished less than ResultsShowSeconds ago: the HUD shows the results card. */
	bool IsShowingResults() const { return Session.IsFinished() && Session.GetSecondsSinceFinished() < ResultsShowSeconds; }

	/** The local best for this session's length before it finished (0 with none), and whether there was one. */
	float GetPreviousBest() const { return PreviousBest; }
	bool HadPreviousBest() const { return bHadPreviousBest; }

	/** The finished session beat the local best (or was the first of its length to score). */
	bool IsNewBest() const { return bNewBest; }

	/** The session's length in whole seconds: the local best's key. */
	int32 GetDurationKey() const { return FMath::RoundToInt(Session.GetDuration()); }

	/** How long the results card stays up after the session ends (s of ride time). An estimate. */
	float ResultsShowSeconds = 12.0f;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

protected:
	/** Game and PIE worlds only: the editor's own world has no rider. */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void ReadLocalBest();
	void OnFinished();
	float ReadClock() const;

	TWeakObjectPtr<UTrickTrackerComponent> Tracker;
	TWeakObjectPtr<UBoardMovementComponent> Board;
	FBestThreeSession Session;
	int32 SeenRecordCount = 0;
	float LastClockSeconds = 0.0f;
	float PreviousBest = 0.0f;
	bool bHadPreviousBest = false;
	bool bNewBest = false;
};
