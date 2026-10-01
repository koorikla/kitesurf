#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "KiteSurfPauseMenuWidget.generated.h"

class UButton;
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
	TObjectPtr<UButton> MainMenuButton;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "Pause")
	TObjectPtr<UButton> QuitButton;

	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnResumeClicked();

	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OnRestartClicked();

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
	TSharedPtr<SButton> SlateMainMenuButton;
	TSharedPtr<SButton> SlateQuitButton;
};
