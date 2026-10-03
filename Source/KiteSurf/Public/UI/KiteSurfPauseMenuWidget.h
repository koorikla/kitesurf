#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/KiteSurfMenuNavigator.h"
#include "KiteSurfPauseMenuWidget.generated.h"

class UButton;
class UKiteSurfSettingsWidget;
class SButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPauseMenuResumeClicked);

UCLASS()
class KITESURF_API UKiteSurfPauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UKiteSurfPauseMenuWidget(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintAssignable, Category = "Pause")
	FOnPauseMenuResumeClicked OnResumeClickedDelegate;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Pause")
	TObjectPtr<UButton> ResumeButton;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Pause")
	TObjectPtr<UButton> RestartButton;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Pause")
	TObjectPtr<UButton> SettingsButton;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Pause")
	TObjectPtr<UButton> MainMenuButton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pause")
	TSubclassOf<UKiteSurfSettingsWidget> SettingsWidgetClass;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Pause")
	TObjectPtr<UKiteSurfSettingsWidget> ActiveSettingsWidget;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Pause")
	TObjectPtr<UButton> QuitButton;

	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnResumeClicked();

	/** Starts a 90 s best-three session (UTrickSessionSubsystem) for the rider and resumes the ride. */
	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnSessionClicked();

	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnRestartClicked();

	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnSettingsClicked();

	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnSettingsClosed();

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Pause")
	TObjectPtr<class UKiteSurfGearWidget> ActiveGearWidget;

	/** Opens the gear screen over the paused ride; applying re-rigs the rider on the spot. */
	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnGearClicked();

	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnGearClosed();

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Pause")
	TObjectPtr<class UKiteSurfSchoolWidget> ActiveSchoolWidget;

	/** SCHOOL (free ride) and LESSON MENU (in a lesson): the lesson menu over the paused ride. */
	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnSchoolClicked();

	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnSchoolClosed();

	/** A lesson was started from the lesson menu: it runs in this ride, so the ride resumes. */
	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnSchoolLessonStarted(FName LessonId);

	/** RETRY LESSON: the running lesson starts again from its set-up (ALessonDirector::Retry), and the ride resumes. */
	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnRetryLessonClicked();

	/**
	 * FREE RIDE: the running lesson ends (ALessonDirector::ExitToFreeRide), and the ride resumes. In the
	 * first-run tutorial (lessons A1 to A3, docs/tutorials.md S7) it reads SKIP TUTORIAL and also sets
	 * bSkipOnboarding (USchoolOnboardingSubsystem::SkipTutorial).
	 */
	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnFreeRideClicked();

	/** Whether the FREE RIDE item reads SKIP TUTORIAL: a first-run tutorial lesson is running. */
	bool ShowsSkipTutorial() const;

	/** The FREE RIDE item's label: SKIP TUTORIAL in the first-run tutorial, FREE RIDE otherwise. */
	FText GetFreeRideLabel() const;

	/** The lesson running in this ride, or null in free ride. */
	class ALessonDirector* GetRunningLesson() const;

	/** Whether the menu shows the lesson items (RETRY LESSON, LESSON MENU, FREE RIDE) instead of SCHOOL; set when the navigation is built. */
	bool ShowsLessonItems() const { return bLessonItems; }

	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnMainMenuClicked();

	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnQuitClicked();

	UFUNCTION(BlueprintCallable, Category = "Pause")
	void FocusFirst();

protected:
	virtual void NativeConstruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** Keyboard and gamepad navigation: the selection, and what each item does. Public so tests can drive it. */
public:
	FKiteMenuNavigator& GetNavigator();

private:
	void BuildNavigation();
	/** The game instance's first-run tutorial, or null (a world without a game instance). */
	class USchoolOnboardingSubsystem* GetOnboarding() const;
	FKiteMenuNavigator Navigator;

	TSharedPtr<SButton> SlateResumeButton;
	TSharedPtr<SButton> SlateSessionButton;
	TSharedPtr<SButton> SlateRestartButton;
	TSharedPtr<SButton> SlateGearButton;
	TSharedPtr<SButton> SlateSchoolButton;
	TSharedPtr<SButton> SlateRetryLessonButton;
	TSharedPtr<SButton> SlateLessonMenuButton;
	TSharedPtr<SButton> SlateFreeRideButton;
	bool bLessonItems = false;
	TSharedPtr<SButton> SlateSettingsButton;
	TSharedPtr<SButton> SlateMainMenuButton;
	TSharedPtr<SButton> SlateQuitButton;
};
