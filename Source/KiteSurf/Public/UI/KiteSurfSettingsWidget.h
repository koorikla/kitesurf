#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GenericPlatform/GenericWindow.h"
#include "KiteSurfSettingsWidget.generated.h"

class USlider;
class UTextBlock;
class UButton;
class UComboBoxString;
class SSlider;
class STextBlock;
class SButton;
template <typename OptionType> class SComboBox;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSettingsBackClicked);

UCLASS()
class KITESURF_API UKiteSurfSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UKiteSurfSettingsWidget(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintAssignable, Category = "Settings")
	FOnSettingsBackClicked OnBackClickedDelegate;

	// --- Gameplay / Audio Controls ---
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<USlider> WindSlider;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<UTextBlock> WindValueText;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<USlider> VolumeSlider;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<UTextBlock> VolumeValueText;

	// --- Settings v2: Display & Graphics Controls (Optional UMG) ---
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<UButton> FullscreenToggleButton;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<UTextBlock> FullscreenValueText;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<UComboBoxString> ResolutionComboBox;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<UButton> VSyncToggleButton;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<UTextBlock> VSyncValueText;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<UComboBoxString> QualityPresetComboBox;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Settings")
	TObjectPtr<UButton> BackButton;

	// --- Callbacks & Methods ---
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void OnWindSliderChanged(float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void OnVolumeSliderChanged(float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void OnBackClicked();

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void FocusFirst();

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ToggleFullscreen();

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetFullscreenMode(EWindowMode::Type InMode);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetResolution(FIntPoint InResolution);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetResolutionByIndex(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ToggleVSync();

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetVSyncEnabled(bool bInVSync);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetQualityPreset(int32 InPresetIndex);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ApplyVideoSettings();

	// State
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	float CurrentWindKnots;

	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	float CurrentVolume;

	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	TEnumAsByte<EWindowMode::Type> CurrentWindowMode;

	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	FIntPoint CurrentResolution;

	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	bool bCurrentVSync;

	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	int32 CurrentQualityPreset; // 0=Low, 1=Medium, 2=High, 3=Epic

	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	bool bSkipOnboarding;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ToggleSkipOnboarding();

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetSkipOnboarding(bool bInSkip);

	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	TArray<FIntPoint> SupportedResolutions;

protected:
	virtual void NativeConstruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void InitializeSettings();
	void UpdateTextDisplays();

	UFUNCTION()
	void OnResolutionComboSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnQualityComboSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	// Slate widgets
	TSharedPtr<SSlider> SlateWindSlider;
	TSharedPtr<STextBlock> SlateWindText;
	TSharedPtr<SSlider> SlateVolumeSlider;
	TSharedPtr<STextBlock> SlateVolumeText;

	TSharedPtr<SButton> SlateFullscreenButton;
	TSharedPtr<STextBlock> SlateFullscreenText;

	TSharedPtr<SComboBox<TSharedPtr<FString>>> SlateResolutionCombo;
	TArray<TSharedPtr<FString>> ResolutionOptions;
	TSharedPtr<STextBlock> SlateResolutionText;

	TSharedPtr<SButton> SlateVSyncButton;
	TSharedPtr<STextBlock> SlateVSyncText;

	TSharedPtr<SComboBox<TSharedPtr<FString>>> SlateQualityCombo;
	TArray<TSharedPtr<FString>> QualityOptions;
	TSharedPtr<STextBlock> SlateQualityText;

	TSharedPtr<SButton> SlateBackButton;
};
