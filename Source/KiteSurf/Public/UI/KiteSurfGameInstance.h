#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
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

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void LoadSettingsFromDisk();

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SaveSettingsToDisk();

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetPendingWindKnots(float InKnots);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetMasterVolume(float InVolume);
};
