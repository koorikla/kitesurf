#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SchoolOnboarding.generated.h"

class ALessonDirector;
class ULessonSubsystem;
class UKiteSurfGameInstance;

/**
 * The first-run tutorial (docs/tutorials.md 3.1, S7): kite school lessons A1 to A3 in place of the
 * old four-step HUD onboarding.
 *
 * - PLAY on a first run (UKiteSurfGameInstance::bOnboardingCompleted and bSkipOnboarding both off)
 *   calls StartFirstRunTutorial, which starts A1 through ULessonSubsystem::StartLesson: the lesson
 *   is pending, L_FlatWater opens, and AKiteSurfGameMode starts it once the ride is set up. A player
 *   who left the tutorial part-way resumes at the first of A1 to A3 without a pass.
 * - The result card's Next goes on through A2 and A3 (ALessonDirector::Next, unchanged).
 * - Passing A3, from the tutorial or the School menu, sets bOnboardingCompleted
 *   (ULessonSubsystem::OnLessonResultRecorded, written with the lesson result).
 * - SkipTutorial (the pause menu's SKIP TUTORIAL while a tutorial lesson runs) sets bSkipOnboarding,
 *   writes the save and ends the lesson in free ride.
 * - GetHintLines is the HUD's tutorial line: the welcome and how to skip during A1 to A3, then the
 *   "tutorial complete" choices on A3's result card (continue the school, the first jump B2 when it
 *   is unlocked, free ride). The School menu stays available throughout.
 *
 * Exists only on a UKiteSurfGameInstance. Tests make one with NewObject on a game instance, give it
 * a lesson subsystem with SetLessonSubsystem, and turn disk writes off.
 */
UCLASS()
class KITESURF_API USchoolOnboardingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The tutorial's lessons, in order, and the first jump it points to afterwards. */
	static const FName FirstLessonId;   // A1
	static const FName LastLessonId;    // A3
	static const FName FirstJumpLessonId; // B2

	/** A1, A2 or A3. */
	static bool IsTutorialLessonId(FName LessonId);

	/** 1 to 3 for A1 to A3, 0 for any other lesson. */
	static int32 TutorialLessonNumber(FName LessonId);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Uses this lesson subsystem instead of the game instance's, and listens to its results (tests). */
	void SetLessonSubsystem(ULessonSubsystem* InLessons);

	/** Whether SkipTutorial writes the Settings slot (on by default). Tests turn it off. */
	void SetWriteToDisk(bool bEnabled) { bWriteToDisk = bEnabled; }

	/** Neither completed nor skipped: PLAY starts the tutorial. */
	UFUNCTION(BlueprintPure, Category = "School|Onboarding")
	bool NeedsFirstRunTutorial() const;

	/**
	 * The lesson the tutorial starts with: the first of A1 to A3 without a pass that is unlocked (A1 on
	 * a first run); None when all three are passed.
	 */
	UFUNCTION(BlueprintPure, Category = "School|Onboarding")
	FName GetTutorialStartLessonId() const;

	/**
	 * PLAY's first-run path: when NeedsFirstRunTutorial, starts GetTutorialStartLessonId through
	 * ULessonSubsystem::StartLesson (pending, then L_FlatWater) and marks the tutorial running. When
	 * A1 to A3 are all passed already it sets bOnboardingCompleted instead. False when nothing started,
	 * so PLAY goes on to the gear screen and free ride.
	 */
	UFUNCTION(BlueprintCallable, Category = "School|Onboarding")
	bool StartFirstRunTutorial();

	/**
	 * "Skip tutorial": bSkipOnboarding on and written, the tutorial no longer running, and Director
	 * (the running lesson, may be null) ended in free ride.
	 */
	void SkipTutorial(ALessonDirector* Director);

	/** Started this session by StartFirstRunTutorial, and neither skipped nor completed since. */
	UFUNCTION(BlueprintPure, Category = "School|Onboarding")
	bool IsTutorialRunning() const { return bTutorialRunning; }

	/** The tutorial is running and Director runs one of its lessons: the pause menu's FREE RIDE reads SKIP TUTORIAL. */
	bool IsTutorialLesson(const ALessonDirector* Director) const;

	/** The tutorial was completed by passing A3 this session (the completion lines show on A3's result card). */
	bool WasCompletedThisSession() const { return bCompletedThisSession; }

	/**
	 * The HUD's tutorial line for the running lesson (Director may be null), empty when there is
	 * nothing to say:
	 * - during A1 to A3 (not on a result card) while the tutorial runs: "Welcome to kite school:
	 *   lesson 1 of the basics." (or "Kite school: lesson 2 of the basics.") and how to skip;
	 * - on A3's result card after the pass that completed the tutorial: "Tutorial complete", the next
	 *   lesson on the card's Next, the first jump (B2 in the lesson menu when it is unlocked, otherwise
	 *   where jumps start) and free ride.
	 */
	TArray<FString> GetHintLines(const ALessonDirector* Director) const;

	/** ULessonSubsystem::OnLessonResultRecorded: a pass of A3 completes the tutorial. */
	void HandleLessonResult(FName LessonId, bool bPassed);

private:
	UKiteSurfGameInstance* GetKiteGameInstance() const;
	ULessonSubsystem* GetLessons() const;
	void BindLessons(ULessonSubsystem* InLessons);

	UPROPERTY(Transient)
	TObjectPtr<ULessonSubsystem> LessonsOverride;

	TWeakObjectPtr<ULessonSubsystem> BoundLessons;
	FDelegateHandle ResultHandle;
	bool bWriteToDisk = true;
	bool bTutorialRunning = false;
	bool bCompletedThisSession = false;
};
