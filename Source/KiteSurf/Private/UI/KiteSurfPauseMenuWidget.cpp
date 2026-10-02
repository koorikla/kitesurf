#include "UI/KiteSurfPauseMenuWidget.h"
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
#include "GameFramework/WorldSettings.h"
#include "Input/Reply.h"

UKiteSurfPauseMenuWidget::UKiteSurfPauseMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

void UKiteSurfPauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ResumeButton)
	{
		ResumeButton->OnClicked.AddDynamic(this, &UKiteSurfPauseMenuWidget::OnResumeClicked);
	}
	if (RestartButton)
	{
		RestartButton->OnClicked.AddDynamic(this, &UKiteSurfPauseMenuWidget::OnRestartClicked);
	}
	if (MainMenuButton)
	{
		MainMenuButton->OnClicked.AddDynamic(this, &UKiteSurfPauseMenuWidget::OnMainMenuClicked);
	}
	if (QuitButton)
	{
		QuitButton->OnClicked.AddDynamic(this, &UKiteSurfPauseMenuWidget::OnQuitClicked);
	}

	FocusFirst();
}

TSharedRef<SWidget> UKiteSurfPauseMenuWidget::RebuildWidget()
{
	if (WidgetTree && WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	// Fallback Slate UI
	return SNew(SBorder)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.BorderBackgroundColor(FLinearColor(0.01f, 0.03f, 0.08f, 0.85f))
		[
			SNew(SBox)
			.WidthOverride(380.0f)
			[
				SNew(SVerticalBox)
				// Title
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 25.0f, 20.0f, 20.0f)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("PAUSED")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 28))
					.ColorAndOpacity(FLinearColor(1.0f, 0.85f, 0.2f))
				]
				// Resume Button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(25.0f, 6.0f)
				[
					SAssignNew(SlateResumeButton, SButton)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.OnClicked_Lambda([this]()
					{
						OnResumeClicked();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("RESUME")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						.Margin(FMargin(10.0f, 8.0f))
					]
				]
				// Restart Button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(25.0f, 6.0f)
				[
					SAssignNew(SlateRestartButton, SButton)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.OnClicked_Lambda([this]()
					{
						OnRestartClicked();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("RESTART")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						.Margin(FMargin(10.0f, 8.0f))
					]
				]
				// Main Menu Button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(25.0f, 6.0f)
				[
					SAssignNew(SlateMainMenuButton, SButton)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.OnClicked_Lambda([this]()
					{
						OnMainMenuClicked();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("MAIN MENU")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						.Margin(FMargin(10.0f, 8.0f))
					]
				]
				// Quit Button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(25.0f, 6.0f, 25.0f, 25.0f)
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
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						.Margin(FMargin(10.0f, 8.0f))
					]
				]
			]
		];
}

void UKiteSurfPauseMenuWidget::OnResumeClicked()
{
	if (UWorld* World = GetWorld())
	{
		if (!UGameplayStatics::SetGamePaused(World, false))
		{
			if (AWorldSettings* WS = World->GetWorldSettings())
			{
				WS->SetPauserPlayerState(nullptr);
			}
		}
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}

	OnResumeClickedDelegate.Broadcast();
	RemoveFromParent();
}

void UKiteSurfPauseMenuWidget::OnRestartClicked()
{
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGamePaused(World, false);
		FString LevelName = World->GetName();
		if (LevelName.IsEmpty() || LevelName.StartsWith(TEXT("UEDPIE")))
		{
			LevelName = TEXT("L_OpenWater");
		}
		UGameplayStatics::OpenLevel(World, FName(*LevelName));
	}
}

void UKiteSurfPauseMenuWidget::OnMainMenuClicked()
{
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGamePaused(World, false);
		UGameplayStatics::OpenLevel(World, FName(TEXT("L_MainMenu")));
	}
}

void UKiteSurfPauseMenuWidget::OnQuitClicked()
{
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGamePaused(World, false);
		UKismetSystemLibrary::QuitGame(World, GetOwningPlayer(), EQuitPreference::Quit, false);
	}
}

void UKiteSurfPauseMenuWidget::FocusFirst()
{
	if (ResumeButton)
	{
		ResumeButton->SetKeyboardFocus();
	}
	else if (SlateResumeButton.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(SlateResumeButton);
	}
}

FReply UKiteSurfPauseMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right)
	{
		OnResumeClicked();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
