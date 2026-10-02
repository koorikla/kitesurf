#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
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

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void OnPlayClicked();

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void OnSettingsClicked();

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void OnQuitClicked();

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void OnSettingsClosed();

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void FocusFirst();

protected:
	virtual void NativeConstruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	TSharedPtr<SButton> SlatePlayButton;
	TSharedPtr<SButton> SlateSettingsButton;
	TSharedPtr<SButton> SlateQuitButton;
};
