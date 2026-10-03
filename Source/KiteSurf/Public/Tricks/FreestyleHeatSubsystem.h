#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tricks/FreestyleHeat.h"
#include "FreestyleHeatSubsystem.generated.h"

class UBoardMovementComponent;
class UTrickTrackerComponent;

/** Why StartHeat did or did not start a heat. */
UENUM(BlueprintType)
enum class EHeatStartResult : uint8
{
	Started         UMETA(DisplayName = "Started"),
	/** No rider with a trick tracker in the world. */
	NoRider         UMETA(DisplayName = "No rider"),
	/** A best-three session is running: one mode at a time. */
	SessionActive   UMETA(DisplayName = "Session active"),
	/** A kite school lesson is running. */
	LessonActive    UMETA(DisplayName = "Lesson active")
};

/**
 * Runs the freestyle heat (T3.6, docs/tricks.md 3.6 and 6.8): one FFreestyleHeat per world, fed
 * from the rider's UTrickTrackerComponent, on the pattern of UTrickSessionSubsystem (T2.5): a world
 * subsystem, so test worlds without a game mode have it and the HUD, the console and the pause menu
 * reach it through the world; it polls the tracker's record count each step and offers every new
 * record to the heat, then ticks the heat's countdown with the board's simulation time. In the game
 * it ticks with the world (not while paused); tests call StepHeat after each pawn tick.
 *
 * One mode at a time: StartHeat refuses while a best-three session or a lesson runs, and a running
 * heat is cancelled when either starts. Refusals, the cancel, "Unhook for freestyle" for a landed
 * hooked jump and a lost attempt are posted as a notice (GetNotice, GetNoticeSerial), which the HUD
 * shows on its notice line.
 *
 * At the end the total goes to UKiteSurfGameInstance::RecordHeatTotal as the local best for the
 * heat's attempt count, and a new best is saved through SaveSettingsToDisk. Test worlds have no game
 * instance, so they never write a save.
 */
UCLASS()
class KITESURF_API UFreestyleHeatSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Starts a heat of Attempts for Tracker's rider, or for the player's rider when Tracker is null.
	 * CountdownSeconds is the trick countdown (negative: the settings' 90 s; 0: off). Restarts a
	 * running heat. Refuses while a session or a lesson runs, with a notice.
	 */
	EHeatStartResult StartHeat(int32 Attempts = FFreestyleHeat::DefaultAttempts, UTrickTrackerComponent* Tracker = nullptr,
		float CountdownSeconds = -1.0f);

	/** Ends a running heat with nothing recorded. */
	void CancelHeat(const FString& Notice = FString());

	/**
	 * One step: cancels the heat if a session or a lesson has started, offers the tracker's new
	 * records, then ticks the heat by the board's simulation time since the last step
	 * (FallbackDeltaSeconds when there is no board). Finishes the results when the last attempt is in.
	 */
	void StepHeat(float FallbackDeltaSeconds = 0.0f);

	const FFreestyleHeat& GetHeat() const { return Heat; }

	/** The heat's settings, for tuning before StartHeat (the countdown, the minimum airtime). */
	FFreestyleHeatSettings& GetSettings() { return Heat.Settings; }

	bool IsHeatActive() const { return Heat.IsActive(); }

	/** The heat finished less than ResultsShowSeconds ago: the HUD shows the results card. */
	bool IsShowingResults() const { return Heat.IsFinished() && Heat.GetSecondsSinceFinished() < ResultsShowSeconds; }

	/** The local best for this attempt count before the heat finished (0 with none), and whether there was one. */
	float GetPreviousBest() const { return PreviousBest; }
	bool HadPreviousBest() const { return bHadPreviousBest; }

	/** The finished heat beat the local best (or was the first of its attempt count to score). */
	bool IsNewBest() const { return bNewBest; }

	/** The newest notice for the HUD and how many have been posted (the HUD shows a notice when this goes up). */
	const FString& GetNotice() const { return Notice; }
	int32 GetNoticeSerial() const { return NoticeSerial; }

	/** How long the results card stays up after the heat ends (s of ride time). An estimate. */
	float ResultsShowSeconds = 12.0f;

	/** The notice for a landed hooked jump during a heat. */
	static const TCHAR* UnhookNotice;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

protected:
	/** Game and PIE worlds only: the editor's own world has no rider. */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Why a heat cannot run now: SessionActive, LessonActive, or Started when nothing is in the way. */
	EHeatStartResult FindBlockingMode() const;
	void PostNotice(const FString& Text);
	void ReadLocalBest();
	void OnFinished();

	TWeakObjectPtr<UTrickTrackerComponent> Tracker;
	TWeakObjectPtr<UBoardMovementComponent> Board;
	FFreestyleHeat Heat;
	int32 SeenRecordCount = 0;
	float LastClockSeconds = 0.0f;
	float PreviousBest = 0.0f;
	bool bHadPreviousBest = false;
	bool bNewBest = false;
	FString Notice;
	int32 NoticeSerial = 0;
};
