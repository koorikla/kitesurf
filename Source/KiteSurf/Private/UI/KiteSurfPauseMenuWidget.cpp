#include "UI/KiteSurfPauseMenuWidget.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfMenuStyle.h"
#include "UI/KiteSurfControlsLegend.h"
#include "UI/KiteSurfSettingsWidget.h"
#include "UI/KiteSurfGearWidget.h"
#include "Tricks/TrickSessionSubsystem.h"
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
	if (SettingsButton)
	{
		SettingsButton->OnClicked.AddDynamic(this, &UKiteSurfPauseMenuWidget::OnSettingsClicked);
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
	// A solid brush, tinted mostly opaque, so the paused game is dimmed behind the menu.
	return SNew(SBorder)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.01f, 0.03f, 0.08f, 0.85f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
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
					.IsFocusable(false)
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
					.IsFocusable(false)
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
				// Gear Button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(25.0f, 6.0f)
				[
					SAssignNew(SlateGearButton, SButton)
					.IsFocusable(false)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.OnClicked_Lambda([this]()
					{
						OnGearClicked();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("GEAR")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						.Margin(FMargin(10.0f, 8.0f))
					]
				]
				// Best-three session button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(25.0f, 6.0f)
				[
					SAssignNew(SlateSessionButton, SButton)
					.IsFocusable(false)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.OnClicked_Lambda([this]()
					{
						OnSessionClicked();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("BEST-THREE SESSION (90 s)")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						.Margin(FMargin(10.0f, 8.0f))
					]
				]
				// Settings Button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(25.0f, 6.0f)
				[
					SAssignNew(SlateSettingsButton, SButton)
					.IsFocusable(false)
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
					.IsFocusable(false)
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
					.IsFocusable(false)
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
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(40.0f, 0.0f, 0.0f, 0.0f)
			[
				KiteSurfControlsLegend::Build()
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

void UKiteSurfPauseMenuWidget::OnSessionClicked()
{
	if (UWorld* World = GetWorld())
	{
		if (UTrickSessionSubsystem* Sessions = World->GetSubsystem<UTrickSessionSubsystem>())
		{
			Sessions->StartSession(UTrickSessionSubsystem::DefaultSessionSeconds);
		}
	}
	OnResumeClicked();
}

void UKiteSurfPauseMenuWidget::OnRestartClicked()
{
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGamePaused(World, false);
		FString LevelName = World->GetName();
		if (LevelName.IsEmpty() || LevelName.StartsWith(TEXT("UEDPIE")))
		{
			FName MapToLoad = FName(TEXT("L_OpenWater"));
			if (UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
			{
				MapToLoad = FName(*GI->PendingMapName);
			}
			UGameplayStatics::OpenLevel(World, MapToLoad);
		}
		else
		{
			UGameplayStatics::OpenLevel(World, FName(*LevelName));
		}
	}
}

void UKiteSurfPauseMenuWidget::OnSettingsClicked()
{
	UWorld* World = GetWorld();
	if (!World || ActiveSettingsWidget)
	{
		return;
	}

	TSubclassOf<UKiteSurfSettingsWidget> ClassToSpawn = SettingsWidgetClass ? SettingsWidgetClass : TSubclassOf<UKiteSurfSettingsWidget>(UKiteSurfSettingsWidget::StaticClass());
	ActiveSettingsWidget = CreateWidget<UKiteSurfSettingsWidget>(World, ClassToSpawn);
	if (ActiveSettingsWidget)
	{
		ActiveSettingsWidget->OnBackClickedDelegate.AddDynamic(this, &UKiteSurfPauseMenuWidget::OnSettingsClosed);
		if (World->GetGameViewport() != nullptr)
		{
			ActiveSettingsWidget->AddToViewport(110); // above the pause menu
		}
		ActiveSettingsWidget->FocusFirst();

		// One screen at a time: the pause menu comes back when settings close.
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UKiteSurfPauseMenuWidget::OnSettingsClosed()
{
	ActiveSettingsWidget = nullptr;
	SetVisibility(ESlateVisibility::Visible);
	FocusFirst();
}

void UKiteSurfPauseMenuWidget::OnGearClicked()
{
	UWorld* World = GetWorld();
	if (!World || ActiveGearWidget)
	{
		return;
	}

	ActiveGearWidget = CreateWidget<UKiteSurfGearWidget>(World, UKiteSurfGearWidget::StaticClass());
	if (ActiveGearWidget)
	{
		ActiveGearWidget->bDuringRide = true;
		ActiveGearWidget->OnConfirmedDelegate.AddDynamic(this, &UKiteSurfPauseMenuWidget::OnGearClosed);
		ActiveGearWidget->OnCancelledDelegate.AddDynamic(this, &UKiteSurfPauseMenuWidget::OnGearClosed);
		if (World->GetGameViewport() != nullptr)
		{
			ActiveGearWidget->AddToViewport(110); // above the pause menu
		}
		ActiveGearWidget->FocusFirst();

		// One screen at a time: the pause menu comes back when the gear screen closes.
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UKiteSurfPauseMenuWidget::OnGearClosed()
{
	ActiveGearWidget = nullptr;
	SetVisibility(ESlateVisibility::Visible);
	FocusFirst();
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
	// The menu itself holds keyboard focus and routes keys to its navigator; the controls are
	// built not to take focus, so a mouse click does not leave the keys on one of them.
	BuildNavigation();
	Navigator.Select(Navigator.DefaultIndex);
	if (const TSharedPtr<SWidget> Widget = GetCachedWidget())
	{
		FSlateApplication::Get().SetKeyboardFocus(Widget);
	}
}

FKiteMenuNavigator& UKiteSurfPauseMenuWidget::GetNavigator()
{
	if (Navigator.Num() == 0)
	{
		BuildNavigation();
		Navigator.Select(Navigator.DefaultIndex);
	}
	return Navigator;
}

void UKiteSurfPauseMenuWidget::BuildNavigation()
{
	Navigator.Reset();
	Navigator.OnAction = [this](FKiteMenuNavigator::EAction Action)
	{
		KiteSurfMenuStyle::PlayMenuSound(this, Action == FKiteMenuNavigator::EAction::Activated ? EKiteMenuSound::Select : EKiteMenuSound::Move);
	};
	Navigator.AddButton(SlateResumeButton, [this]() { OnResumeClicked(); });
	Navigator.AddButton(SlateRestartButton, [this]() { OnRestartClicked(); });
	Navigator.AddButton(SlateGearButton, [this]() { OnGearClicked(); });
	Navigator.AddButton(SlateSessionButton, [this]() { OnSessionClicked(); });
	Navigator.AddButton(SlateSettingsButton, [this]() { OnSettingsClicked(); });
	Navigator.AddButton(SlateMainMenuButton, [this]() { OnMainMenuClicked(); });
	Navigator.AddButton(SlateQuitButton, [this]() { OnQuitClicked(); });
}

FReply UKiteSurfPauseMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (GetNavigator().HandleKey(Key))
	{
		return FReply::Handled();
	}
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right)
	{
		OnResumeClicked();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
