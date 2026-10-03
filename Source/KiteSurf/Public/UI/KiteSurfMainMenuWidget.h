#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/KiteSurfMenuNavigator.h"
#include "Styling/SlateBrush.h"
#include "KiteSurfMainMenuWidget.generated.h"

class UButton;
class UKiteSurfSettingsWidget;
class SButton;

UCLASS()
class KITESURF_API UKiteSurfMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UKiteSurfMainMenuWidget(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Menu")
	TObjectPtr<UButton> PlayButton;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Menu")
	TObjectPtr<UButton> SettingsButton;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Menu")
	TObjectPtr<UButton> QuitButton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
	TSubclassOf<UKiteSurfSettingsWidget> SettingsWidgetClass;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Menu")
	TObjectPtr<UKiteSurfSettingsWidget> ActiveSettingsWidget;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Menu")
	TObjectPtr<class UKiteSurfGearWidget> ActiveGearWidget;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Menu")
	TObjectPtr<class UKiteSurfSchoolWidget> ActiveSchoolWidget;

	/** Opens the kite school's lesson menu in place of the main menu. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void OnSchoolClicked();

	/** The lesson menu closed with BACK: the main menu comes back. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void OnSchoolClosed();

	/** A lesson was started from the lesson menu: its map is loading. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void OnSchoolLessonStarted(FName LessonId);

	/**
	 * Opens the gear screen; the ride starts when it is confirmed. On a first run (onboarding neither
	 * completed nor skipped) it starts the kite school's first lesson instead
	 * (USchoolOnboardingSubsystem::StartFirstRunTutorial, docs/tutorials.md S7).
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void OnPlayClicked();

	/** Gear chosen: load the open water level. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void StartRide();

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void OnGearCancelled();

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void OnSettingsClicked();

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void OnQuitClicked();

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void OnSettingsClosed();

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void FocusFirst();

	/** True while the startup intro video covers the menu. */
	UFUNCTION(BlueprintPure, Category = "Menu")
	bool IsIntroPlaying() const { return bIntroPlaying; }

	/** Ends the intro (any key or click does this) and brings in the menu. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void SkipIntro();

	/** How long the intro may take to show its first frame before the menu is shown without it (s). */
	static constexpr double IntroStartTimeoutSeconds = 4.0;

protected:
	virtual void NativeConstruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** Keyboard and gamepad navigation: the selection, and what each item does. Public so tests can drive it. */
public:
	FKiteMenuNavigator& GetNavigator();

private:
	class UKiteSurfMenuVideoSubsystem* GetVideos() const;

	UPROPERTY(Transient)
	TObjectPtr<class UKiteSurfVideoPlayer> IntroPlayer;

	bool bIntroPlaying = false;
	double IntroStartTime = 0.0;
	/** When the intro ended into the menu, for the white flash that fades off it; 0 for none. */
	double FlashStartTime = 0.0;

	void BuildNavigation();
	FKiteMenuNavigator Navigator;

	UPROPERTY(Transient)
	TObjectPtr<class UTexture2D> BackgroundTexture;
	FSlateBrush BackgroundBrush;

	TSharedPtr<SButton> SlatePlayButton;
	TSharedPtr<SButton> SlateSchoolButton;
	TSharedPtr<SButton> SlateSettingsButton;
	TSharedPtr<SButton> SlateQuitButton;
};
