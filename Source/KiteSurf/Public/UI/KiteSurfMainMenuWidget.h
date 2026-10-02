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

	/** Opens the gear screen; the ride starts when it is confirmed. */
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

	UPROPERTY(Transient)
	TObjectPtr<class UTexture2D> BackgroundTexture;
	FSlateBrush BackgroundBrush;

	TSharedPtr<SButton> SlatePlayButton;
	TSharedPtr<SButton> SlateSettingsButton;
	TSharedPtr<SButton> SlateQuitButton;
};
