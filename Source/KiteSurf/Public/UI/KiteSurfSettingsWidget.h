#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/KiteSurfMenuNavigator.h"
#include "GenericPlatform/GenericWindow.h"
#include "RiderCharacter.h"
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

	/** Music volume, 0..1. Whatever music is playing follows it at once. */
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	float CurrentMusicVolume = 0.6f;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void OnMusicSliderChanged(float Value);

	/** Ambient volume, 0..1: wind, water, spray, lines and the kite. */
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	float CurrentAmbientVolume = 1.0f;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void OnAmbientSliderChanged(float Value);

	/** Effects volume, 0..1: the pop, landings, crashes and the menus' sounds. */
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	float CurrentEffectsVolume = 1.0f;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void OnEffectsSliderChanged(float Value);

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

	/** Controller motion sensors as the bar, instead of the right stick. */
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	bool bMotionBar = false;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ToggleMotionBar();

	/** Controller vibration. */
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	bool bHaptics = true;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ToggleHaptics();

	/** What the motion bar row's note says: what it does, or which controller it found. */
	UFUNCTION(BlueprintPure, Category = "Settings")
	FString GetMotionBarNote() const;

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

	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	ERiderCharacter CurrentRiderCharacter;

	/** Chosen kite size in m^2; 0 means the size recommended for the chosen wind. */
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
	float CurrentKiteSizeM2;

	/** Steps through: recommended for the wind, then each kite size. */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void CycleKiteSize();

	/** What the kite row shows, e.g. "AUTO: 6 m for 30 kn" or "9 m (6 m recommended)". */
	UFUNCTION(BlueprintPure, Category = "Settings")
	FString GetKiteSizeText() const;

	/** Steps to the next rider in the list. */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void CycleRiderCharacter();

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

	/** Keyboard and gamepad navigation: the selection, and what each item does. Public so tests can drive it. */
public:
	FKiteMenuNavigator& GetNavigator();

private:
	void BuildNavigation();
	FKiteMenuNavigator Navigator;

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

	TSharedPtr<SButton> SlateKiteButton;
	TSharedPtr<STextBlock> SlateKiteText;

	TSharedPtr<SButton> SlateRiderButton;
	TSharedPtr<STextBlock> SlateRiderText;

	TSharedPtr<SSlider> SlateMusicSlider;
	TSharedPtr<STextBlock> SlateMusicText;
	TSharedPtr<SSlider> SlateAmbientSlider;
	TSharedPtr<STextBlock> SlateAmbientText;
	TSharedPtr<SSlider> SlateEffectsSlider;
	TSharedPtr<STextBlock> SlateEffectsText;

	/** Shows a volume on its slider and percentage, when the keys moved it rather than the mouse. */
	static void ShowVolume(const TSharedPtr<SSlider>& Slider, const TSharedPtr<STextBlock>& Text, float Volume);

	TSharedPtr<SButton> SlateHapticsButton;
	TSharedPtr<STextBlock> SlateHapticsText;

	TSharedPtr<SButton> SlateMotionBarButton;
	TSharedPtr<STextBlock> SlateMotionBarText;
	TSharedPtr<STextBlock> SlateMotionBarNote;

	TSharedPtr<SButton> SlateVSyncButton;
	TSharedPtr<STextBlock> SlateVSyncText;

	TSharedPtr<SComboBox<TSharedPtr<FString>>> SlateQualityCombo;
	TArray<TSharedPtr<FString>> QualityOptions;
	TSharedPtr<STextBlock> SlateQualityText;

	TSharedPtr<SButton> SlateBackButton;
};
