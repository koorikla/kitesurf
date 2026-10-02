#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
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

private:
	TSharedPtr<SButton> SlateResumeButton;
	TSharedPtr<SButton> SlateRestartButton;
	TSharedPtr<SButton> SlateGearButton;
	TSharedPtr<SButton> SlateSettingsButton;
	TSharedPtr<SButton> SlateMainMenuButton;
	TSharedPtr<SButton> SlateQuitButton;
};
