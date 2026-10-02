#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "RiderCharacter.h"
#include "KiteGear.h"
#include "KiteSurfGameInstance.generated.h"

class UKiteSurfSaveGame;

UCLASS()
class KITESURF_API UKiteSurfGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UKiteSurfGameInstance();

	virtual void Init() override;

	/** Chosen wind strength in knots passed between menus and gameplay levels (default 15.0 kn) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	float PendingWindKnots;

	/** Master audio volume (0.0 to 1.0, default 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	float MasterVolume;

	/** Whether onboarding prompts should be skipped */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bSkipOnboarding;

	/** Whether onboarding tutorial has been completed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bOnboardingCompleted;

	/** Who rides the board */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	ERiderCharacter RiderCharacter;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetRiderCharacter(ERiderCharacter InCharacter);

	/** The kind of kite to rig */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	EKiteModel KiteModel;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetKiteModel(EKiteModel InModel);

	/** The board to ride */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	EBoardSize BoardSize;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetBoardSize(EBoardSize InSize);

	/** Chosen kite size in m^2; 0 means the size recommended for the wind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	float KiteSizeM2;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetKiteSizeM2(float InSizeM2);

	/** The kite that will be rigged: the chosen size, or the recommended one for PendingWindKnots. */
	UFUNCTION(BlueprintPure, Category = "Settings")
	float GetEffectiveKiteSizeM2() const;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetSkipOnboarding(bool bInSkip);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetOnboardingCompleted(bool bInCompleted);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void LoadSettingsFromDisk();

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SaveSettingsToDisk();

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetPendingWindKnots(float InKnots);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetMasterVolume(float InVolume);
};
