#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Tricks/TrickBook.h"
#include "School/LessonProgress.h"
#include "KiteSurfSaveGame.generated.h"

UCLASS()
class KITESURF_API UKiteSurfSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UKiteSurfSaveGame();

	/** Wind strength in knots (8.0 to 30.0 kn, default 15.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	float WindStrengthKnots;

	/** Master volume (0.0 to 1.0, default 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	float MasterVolume;

	/** Music volume (0.0 to 1.0, default 0.6) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	float MusicVolume;

	/** Ambient volume (0.0 to 1.0, default 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	float AmbientVolume;

	/** Effects volume (0.0 to 1.0, default 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	float EffectsVolume;

	/** Whether onboarding prompts should be skipped */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bSkipOnboarding;

	/** Whether the player has completed the first-run onboarding prompt sequence */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bOnboardingCompleted;

	/** Chosen rider, as an ERiderCharacter index (default 0, Santa) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	int32 RiderCharacterIndex;

	/** Chosen kite size in m^2 (default 9); 0 means the size recommended for the wind */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	float KiteSizeM2;

	/** Chosen kite model, as an EKiteModel index (default 0, the loop kite) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	int32 KiteModelIndex;

	/** Chosen board, as an EBoardSize index (default 1, the 138) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	int32 BoardSizeIndex;

	/** Use the controller's motion sensors as the bar (off by default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bMotionBar;

	/**
	 * The bar springs back to the middle when the power input is let go (on by default). Saves made
	 * before this setting have no such property and load with it on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bBarReturnsToMiddle;

	/** How the motion bar reads power, as an EMotionSheetMode index (default 0, Tilt) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	int32 MotionSheetModeIndex;

	/** Controller vibration (on by default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bHaptics;

	/** What is in the water at the spot (all on by default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bSpotIslands;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bSpotSandbars;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bSpotSharks;

	/**
	 * Tricks landed and the bests on each (T2.7). Saves made before the trick book have no such
	 * property and load with it empty: tagged property serialisation leaves the default.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Progress")
	FTrickBook TrickBook;

	/**
	 * Best best-three session total per session length in whole seconds (T2.5): 90 -> the best
	 * 90 s session. Saves made before sessions have no such property and load with no best.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Progress")
	TMap<int32, float> BestSessionTotalBySeconds;

	/**
	 * Best freestyle heat total per attempt count (T3.6): 7 -> the best 7-trick heat. Saves made
	 * before heats have no such property and load with no best.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Progress")
	TMap<int32, float> BestFreestyleHeatTotalByAttempts;

	/**
	 * Kite school results per lesson (S2), owned at run time by ULessonSubsystem. Saves made before
	 * the kite school have no such property and load with it empty. Separate from the trick book:
	 * resetting lesson progress never touches TrickBook.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Progress")
	FLessonProgressBook LessonProgress;

	static const FString DefaultSaveSlot;
	static const int32 DefaultUserIndex;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	static UKiteSurfSaveGame* LoadOrCreateSettings(const FString& SlotName = TEXT("Settings"), int32 UserIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	bool SaveSettings(const FString& SlotName = TEXT("Settings"), int32 UserIndex = 0);
};
