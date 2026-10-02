#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
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

	/** Whether onboarding prompts should be skipped */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bSkipOnboarding;

	/** Whether the player has completed the first-run onboarding prompt sequence */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bOnboardingCompleted;

	/** Chosen rider, as an ERiderCharacter index (default 0, Santa) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	int32 RiderCharacterIndex;

	static const FString DefaultSaveSlot;
	static const int32 DefaultUserIndex;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	static UKiteSurfSaveGame* LoadOrCreateSettings(const FString& SlotName = TEXT("Settings"), int32 UserIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	bool SaveSettings(const FString& SlotName = TEXT("Settings"), int32 UserIndex = 0);
};
