#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/KiteSurfMenuNavigator.h"
#include "KiteGear.h"
#include "RiderCharacter.h"
#include "Styling/SlateBrush.h"
#include "KiteSurfGearWidget.generated.h"

class SButton;
class SSlider;
class STextBlock;
class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGearConfirmed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGearCancelled);

/**
 * The gear screen: who rides, which kite and how big, which board, how hard it blows, and what
 * is in the water at the spot.
 * Opened from the main menu before a ride (RIDE / BACK) and from the pause menu during one
 * (APPLY). Confirming stores the choices, saves them, and re-rigs a ride that is under way.
 */
UCLASS()
class KITESURF_API UKiteSurfGearWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UKiteSurfGearWidget(const FObjectInitializer& ObjectInitializer);

	/** RIDE or APPLY was pressed and the choices have been stored. */
	UPROPERTY(BlueprintAssignable, Category = "Gear")
	FOnGearConfirmed OnConfirmedDelegate;

	/** BACK or Escape: the choices were left as they were. */
	UPROPERTY(BlueprintAssignable, Category = "Gear")
	FOnGearCancelled OnCancelledDelegate;

	/** True when opened from the pause menu: one APPLY button, drawn over the paused game. */
	UPROPERTY(BlueprintReadWrite, Category = "Gear")
	bool bDuringRide = false;

	UPROPERTY(BlueprintReadOnly, Category = "Gear")
	ERiderCharacter CurrentRider = ERiderCharacter::Santa;

	UPROPERTY(BlueprintReadOnly, Category = "Gear")
	EKiteModel CurrentKiteModel = EKiteModel::Loop;

	/** 9 m unless chosen otherwise; 0 means the size recommended for the chosen wind. */
	UPROPERTY(BlueprintReadOnly, Category = "Gear")
	float CurrentKiteSizeM2 = 9.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Gear")
	EBoardSize CurrentBoardSize = EBoardSize::Medium;

	UPROPERTY(BlueprintReadOnly, Category = "Gear")
	float CurrentWindKnots = 20.0f;

	/** What is in the water at the spot. */
	UPROPERTY(BlueprintReadOnly, Category = "Gear")
	bool bIslands = true;

	UPROPERTY(BlueprintReadOnly, Category = "Gear")
	bool bSandbars = true;

	UPROPERTY(BlueprintReadOnly, Category = "Gear")
	bool bSharks = true;

	UFUNCTION(BlueprintCallable, Category = "Gear")
	void ToggleIslands();

	UFUNCTION(BlueprintCallable, Category = "Gear")
	void ToggleSandbars();

	UFUNCTION(BlueprintCallable, Category = "Gear")
	void ToggleSharks();

	UFUNCTION(BlueprintCallable, Category = "Gear")
	void CycleRider();

	UFUNCTION(BlueprintCallable, Category = "Gear")
	void CycleKiteModel();

	/** Steps through: recommended for the wind, then each kite size. */
	UFUNCTION(BlueprintCallable, Category = "Gear")
	void CycleKiteSize();

	/** A size from UKiteComponent::GetKiteSizesM2, or 0 (or anything else) for the recommended one. */
	UFUNCTION(BlueprintCallable, Category = "Gear")
	void SetKiteSizeM2(float SizeM2);

	UFUNCTION(BlueprintCallable, Category = "Gear")
	void CycleBoardSize();

	UFUNCTION(BlueprintCallable, Category = "Gear")
	void SetWindKnots(float Knots);

	/** The kite that will be rigged: the chosen size, or the recommended one for the chosen wind. */
	UFUNCTION(BlueprintPure, Category = "Gear")
	float GetEffectiveKiteSizeM2() const;

	/** What the kite size row shows, e.g. "AUTO: 6 m" or "9 m (6 m recommended)". */
	UFUNCTION(BlueprintPure, Category = "Gear")
	FString GetKiteSizeText() const;

	/** One line on how the chosen kite suits the chosen wind: underpowered, well powered, overpowered. */
	UFUNCTION(BlueprintPure, Category = "Gear")
	FString GetPowerText() const;

	/** Stores the choices in the game instance, saves them, re-rigs a ride under way, and broadcasts OnConfirmed. */
	UFUNCTION(BlueprintCallable, Category = "Gear")
	void Confirm();

	/** Leaves the stored choices alone and broadcasts OnCancelled. */
	UFUNCTION(BlueprintCallable, Category = "Gear")
	void Cancel();

	UFUNCTION(BlueprintCallable, Category = "Gear")
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
	FKiteMenuNavigator::FItem MakeKiteSizeItem();
	FKiteMenuNavigator Navigator;

	void LoadChoices();
	void UpdateTexts();
	TSharedRef<SWidget> BuildChoiceRow(const TCHAR* Label, TSharedPtr<SButton>& OutButton, TSharedPtr<STextBlock>& OutValueText, TSharedPtr<STextBlock>& OutDescriptionText, TFunction<void()> OnClicked);

	bool bChoicesLoaded = false;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BackgroundTexture;
	FSlateBrush BackgroundBrush;

	TSharedPtr<SSlider> WindSlider;
	TSharedPtr<STextBlock> WindText;
	TSharedPtr<SButton> RiderButton;
	TSharedPtr<STextBlock> RiderText;
	TSharedPtr<STextBlock> RiderDescription;
	TSharedPtr<SButton> KiteModelButton;
	TSharedPtr<STextBlock> KiteModelText;
	TSharedPtr<STextBlock> KiteModelDescription;
	TSharedPtr<SButton> KiteSizeButton;
	TSharedPtr<STextBlock> KiteSizeText;
	TSharedPtr<STextBlock> KiteSizeDescription;
	TSharedPtr<SButton> BoardButton;
	TSharedPtr<STextBlock> BoardText;
	TSharedPtr<STextBlock> BoardDescription;
	TSharedPtr<SButton> IslandsButton;
	TSharedPtr<STextBlock> IslandsText;
	TSharedPtr<STextBlock> IslandsDescription;
	TSharedPtr<SButton> SandbarsButton;
	TSharedPtr<STextBlock> SandbarsText;
	TSharedPtr<STextBlock> SandbarsDescription;
	TSharedPtr<SButton> SharksButton;
	TSharedPtr<STextBlock> SharksText;
	TSharedPtr<STextBlock> SharksDescription;
	TSharedPtr<SButton> ConfirmButton;
	TSharedPtr<SButton> BackButton;
};
