#include "UI/KiteSurfMainMenuWidget.h"
#include "UI/KiteSurfSettingsWidget.h"
#include "UI/KiteSurfGameInstance.h"
#include "Components/Button.h"
#include "Blueprint/WidgetTree.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/PlayerController.h"
#include "Input/Reply.h"

UKiteSurfMainMenuWidget::UKiteSurfMainMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsFocusable = true;
}

void UKiteSurfMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (PlayButton)
	{
		PlayButton->OnClicked.AddDynamic(this, &UKiteSurfMainMenuWidget::OnPlayClicked);
	}
	if (SettingsButton)
	{
		SettingsButton->OnClicked.AddDynamic(this, &UKiteSurfMainMenuWidget::OnSettingsClicked);
	}
	if (QuitButton)
	{
		QuitButton->OnClicked.AddDynamic(this, &UKiteSurfMainMenuWidget::OnQuitClicked);
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}

	FocusFirst();
}

TSharedRef<SWidget> UKiteSurfMainMenuWidget::RebuildWidget()
{
	if (WidgetTree && WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	// Fallback Slate UI
	return SNew(SBorder)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.BorderBackgroundColor(FLinearColor(0.01f, 0.03f, 0.08f, 0.90f))
		[
			SNew(SBox)
			.WidthOverride(420.0f)
			[
				SNew(SVerticalBox)
				// Title
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 30.0f, 20.0f, 20.0f)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("KITESURF")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 32))
					.ColorAndOpacity(FLinearColor(1.0f, 0.85f, 0.2f))
				]
				// Subtitle
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 0.0f, 20.0f, 25.0f)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("HYDRODYNAMICS & AERODYNAMICS SIMULATION")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
					.ColorAndOpacity(FLinearColor(0.4f, 0.75f, 1.0f))
				]
				// Play Button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(30.0f, 8.0f)
				[
					SAssignNew(SlatePlayButton, SButton)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.OnClicked_Lambda([this]()
					{
						OnPlayClicked();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("PLAY")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
						.Margin(FMargin(10.0f, 8.0f))
					]
				]
				// Settings Button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(30.0f, 8.0f)
				[
					SAssignNew(SlateSettingsButton, SButton)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.OnClicked_Lambda([this]()
					{
						OnSettingsClicked();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("SETTINGS")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
						.Margin(FMargin(10.0f, 8.0f))
					]
				]
				// Quit Button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(30.0f, 8.0f, 30.0f, 30.0f)
				[
					SAssignNew(SlateQuitButton, SButton)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.OnClicked_Lambda([this]()
					{
						OnQuitClicked();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("QUIT")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
						.Margin(FMargin(10.0f, 8.0f))
					]
				]
			]
		];
}

void UKiteSurfMainMenuWidget::OnPlayClicked()
{
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::OpenLevel(World, FName(TEXT("L_OpenWater")));
	}
}

void UKiteSurfMainMenuWidget::OnSettingsClicked()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TSubclassOf<UKiteSurfSettingsWidget> ClassToSpawn = SettingsWidgetClass ? SettingsWidgetClass : TSubclassOf<UKiteSurfSettingsWidget>(UKiteSurfSettingsWidget::StaticClass());
	ActiveSettingsWidget = CreateWidget<UKiteSurfSettingsWidget>(World, ClassToSpawn);
	if (ActiveSettingsWidget)
	{
		ActiveSettingsWidget->OnBackClickedDelegate.AddDynamic(this, &UKiteSurfMainMenuWidget::OnSettingsClosed);
		ActiveSettingsWidget->AddToViewport(20);
		ActiveSettingsWidget->FocusFirst();
	}
}

void UKiteSurfMainMenuWidget::OnSettingsClosed()
{
	ActiveSettingsWidget = nullptr;
	FocusFirst();
}

void UKiteSurfMainMenuWidget::OnQuitClicked()
{
	if (UWorld* World = GetWorld())
	{
		UKismetSystemLibrary::QuitGame(World, GetOwningPlayer(), EQuitPreference::Quit, false);
	}
}

void UKiteSurfMainMenuWidget::FocusFirst()
{
	if (PlayButton)
	{
		PlayButton->SetKeyboardFocus();
	}
	else if (SlatePlayButton.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(SlatePlayButton);
	}
}

FReply UKiteSurfMainMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right)
	{
		OnQuitClicked();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
