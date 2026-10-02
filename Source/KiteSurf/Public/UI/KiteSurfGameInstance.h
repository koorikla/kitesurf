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

	/** Chosen wind strength in knots passed between menus and gameplay levels (default 20 kn, which suits the default 9 m kite) */
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

	/** Use the controller's motion sensors as the bar instead of the right stick (off by default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bMotionBar;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetMotionBar(bool bEnabled);

	/** Controller vibration on pops, landings, crashes and hard pulls (on by default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bHaptics;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetHaptics(bool bEnabled);

	/** What is in the water at the spot */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bSpotIslands;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bSpotSandbars;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bSpotSharks;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetSpotFeatures(bool bIslands, bool bSandbars, bool bSharks);

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

	/** Chosen kite size in m^2 (default 9, the middle of the range); 0 means the size recommended for the wind. */
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
