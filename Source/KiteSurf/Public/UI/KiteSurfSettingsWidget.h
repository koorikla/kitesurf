#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "KiteSurfSettingsWidget.generated.h"

class USlider;
class UTextBlock;
class UButton;
class SSlider;
class STextBlock;
class SButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSettingsBackClicked);

UCLASS()
class KITESURF_API UKiteSurfSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UKiteSurfSettingsWidget(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintAssignable, Category = "Settings")
	FOnSettingsBackClicked OnBackClickedDelegate;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<USlider> WindSlider;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<UTextBlock> WindValueText;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<USlider> VolumeSlider;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<UTextBlock> VolumeValueText;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<UButton> BackButton;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void OnWindSliderChanged(float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void OnVolumeSliderChanged(float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void OnBackClicked();

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void FocusFirst();

	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	float CurrentWindKnots;

	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	float CurrentVolume;

protected:
	virtual void NativeConstruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void UpdateTextDisplays();

	TSharedPtr<SSlider> SlateWindSlider;
	TSharedPtr<STextBlock> SlateWindText;
	TSharedPtr<SSlider> SlateVolumeSlider;
	TSharedPtr<STextBlock> SlateVolumeText;
	TSharedPtr<SButton> SlateBackButton;
};
