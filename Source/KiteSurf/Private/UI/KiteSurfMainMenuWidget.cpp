#include "UI/KiteSurfMainMenuWidget.h"
#include "UI/KiteSurfSettingsWidget.h"
#include "UI/KiteSurfGearWidget.h"
#include "UI/KiteSurfMenuStyle.h"
#include "Engine/Texture2D.h"
#include "UI/KiteSurfControlsLegend.h"
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
	SetIsFocusable(true);
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

	// Fallback Slate UI: the key art fills the screen, with the menu and the controls on dark
	// panels on its emptier left side.
	BackgroundTexture = KiteSurfMenuStyle::LoadBackgroundTexture();
	KiteSurfMenuStyle::SetupBackgroundBrush(BackgroundBrush, BackgroundTexture);

	const TSharedRef<SWidget> MenuContent = SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(FMargin(70.0f, 0.0f, 0.0f, 0.0f))
		[
			// Stacked down the left, clear of the kite and rider in the art.
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			[
			KiteSurfMenuStyle::BuildPanel(
			SNew(SBox)
			.WidthOverride(360.0f)
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
					.Text(FText::FromString(TEXT("koorikla big air")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
					.ColorAndOpacity(FLinearColor(0.4f, 0.75f, 1.0f))
				]
				// Play Button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(30.0f, 8.0f)
				[
					SAssignNew(SlatePlayButton, SButton)
					.IsFocusable(false)
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
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
						.Margin(FMargin(10.0f, 8.0f))
					]
				]
			])
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			.Padding(0.0f, 18.0f, 0.0f, 0.0f)
			[
				KiteSurfMenuStyle::BuildPanel(KiteSurfControlsLegend::Build())
			]
		];

	return KiteSurfMenuStyle::BuildBackdrop(&BackgroundBrush, BackgroundTexture != nullptr, MenuContent);
}

void UKiteSurfMainMenuWidget::OnPlayClicked()
{
	UWorld* World = GetWorld();
	if (!World || ActiveGearWidget)
	{
		return;
	}

	// Gear first: wind, kite, board and rider are chosen before the ride starts.
	ActiveGearWidget = CreateWidget<UKiteSurfGearWidget>(World, UKiteSurfGearWidget::StaticClass());
	if (ActiveGearWidget)
	{
		ActiveGearWidget->OnConfirmedDelegate.AddDynamic(this, &UKiteSurfMainMenuWidget::StartRide);
		ActiveGearWidget->OnCancelledDelegate.AddDynamic(this, &UKiteSurfMainMenuWidget::OnGearCancelled);
		if (World->GetGameViewport() != nullptr)
		{
			ActiveGearWidget->AddToViewport(20);
		}
		ActiveGearWidget->FocusFirst();
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UKiteSurfMainMenuWidget::StartRide()
{
	ActiveGearWidget = nullptr;
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::OpenLevel(World, FName(TEXT("L_OpenWater")));
	}
}

void UKiteSurfMainMenuWidget::OnGearCancelled()
{
	ActiveGearWidget = nullptr;
	SetVisibility(ESlateVisibility::Visible);
	FocusFirst();
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
		if (World->GetGameViewport() != nullptr)
		{
			ActiveSettingsWidget->AddToViewport(20);
		}
		ActiveSettingsWidget->FocusFirst();

		// One screen at a time: the menu comes back when settings close.
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UKiteSurfMainMenuWidget::OnSettingsClosed()
{
	ActiveSettingsWidget = nullptr;
	SetVisibility(ESlateVisibility::Visible);
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
	// The menu itself holds keyboard focus and routes keys to its navigator; the controls are
	// built not to take focus, so a mouse click does not leave the keys on one of them.
	BuildNavigation();
	Navigator.Select(Navigator.DefaultIndex);
	if (const TSharedPtr<SWidget> Widget = GetCachedWidget())
	{
		FSlateApplication::Get().SetKeyboardFocus(Widget);
	}
}

FKiteMenuNavigator& UKiteSurfMainMenuWidget::GetNavigator()
{
	if (Navigator.Num() == 0)
	{
		BuildNavigation();
		Navigator.Select(Navigator.DefaultIndex);
	}
	return Navigator;
}

void UKiteSurfMainMenuWidget::BuildNavigation()
{
	Navigator.Reset();
	Navigator.AddButton(SlatePlayButton, [this]() { OnPlayClicked(); });
	Navigator.AddButton(SlateSettingsButton, [this]() { OnSettingsClicked(); });
	Navigator.AddButton(SlateQuitButton, [this]() { OnQuitClicked(); });
}

FReply UKiteSurfMainMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (GetNavigator().HandleKey(Key))
	{
		return FReply::Handled();
	}
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right)
	{
		OnQuitClicked();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
