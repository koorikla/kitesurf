#include "UI/KiteSurfMainMenuWidget.h"
#include "UI/KiteSurfSettingsWidget.h"
#include "UI/KiteSurfGearWidget.h"
#include "UI/KiteSurfMenuStyle.h"
#include "UI/KiteSurfMenuVideo.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
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

	// The intro plays once, the first time the menu opens; then the loop runs behind the menus.
	if (UKiteSurfMenuVideoSubsystem* Videos = GetVideos())
	{
		IntroPlayer = Videos->ShouldPlayIntro() ? Videos->StartIntro() : nullptr;
		if (IntroPlayer)
		{
			bIntroPlaying = true;
			IntroStartTime = FPlatformTime::Seconds();
			IntroPlayer->OnFinished.AddUObject(this, &UKiteSurfMainMenuWidget::SkipIntro);
		}
		else
		{
			Videos->FinishIntro();
		}
	}

	FocusFirst();
}

void UKiteSurfMainMenuWidget::NativeDestruct()
{
	if (IntroPlayer)
	{
		IntroPlayer->OnFinished.RemoveAll(this);
	}
	Super::NativeDestruct();
}

UKiteSurfMenuVideoSubsystem* UKiteSurfMainMenuWidget::GetVideos() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UKiteSurfMenuVideoSubsystem>() : nullptr;
}

void UKiteSurfMainMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// A video that never starts (no decoder, a broken file) must not hold the menu back.
	if (bIntroPlaying && IntroPlayer && !IntroPlayer->HasFrames() && FPlatformTime::Seconds() - IntroStartTime > IntroStartTimeoutSeconds)
	{
		UE_LOG(LogTemp, Warning, TEXT("The intro video did not start; showing the menu"));
		SkipIntro();
	}
}

void UKiteSurfMainMenuWidget::SkipIntro()
{
	if (!bIntroPlaying)
	{
		return;
	}
	bIntroPlaying = false;
	// The intro ends on white: fade that off the menu, but only if the intro was seen at all.
	FlashStartTime = IntroPlayer && IntroPlayer->HasFrames() ? FPlatformTime::Seconds() : 0.0;
	if (IntroPlayer)
	{
		IntroPlayer->OnFinished.RemoveAll(this);
	}
	if (UKiteSurfMenuVideoSubsystem* Videos = GetVideos())
	{
		Videos->FinishIntro();
	}
	FocusFirst();
}

FReply UKiteSurfMainMenuWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIntroPlaying)
	{
		SkipIntro();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
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

	const TSharedRef<SWidget> Backdrop = KiteSurfMenuStyle::BuildBackdrop(&BackgroundBrush, BackgroundTexture != nullptr,
		KiteSurfMenuStyle::MenuLoopBrush(GetGameInstance()), MenuContent);

	static constexpr double FlashSeconds = 0.7;
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			Backdrop
		]
		// The intro, over everything until it ends or is skipped.
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor::Black)
			.Padding(0.0f)
			.Visibility_Lambda([this]() { return bIntroPlaying ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SImage).Image_Lambda([this]() -> const FSlateBrush* { return IntroPlayer ? IntroPlayer->GetBrush() : nullptr; })
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Bottom)
				.Padding(FMargin(0.0f, 0.0f, 40.0f, 30.0f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Press any key")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
					.ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.6f))
					.ShadowOffset(FVector2D(1.0f, 1.0f))
					.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.5f))
				]
			]
		]
		// The white the intro ends on, fading off the menu.
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.Visibility(EVisibility::HitTestInvisible)
			.BorderBackgroundColor_Lambda([this]()
			{
				const double Elapsed = FlashStartTime > 0.0 ? FPlatformTime::Seconds() - FlashStartTime : FlashSeconds;
				const float Alpha = FMath::Clamp(1.0f - static_cast<float>(Elapsed / FlashSeconds), 0.0f, 1.0f);
				return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, Alpha * Alpha));
			})
		];
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
	if (UKiteSurfMenuVideoSubsystem* Videos = GetVideos())
	{
		Videos->PrepareLoadingScreen();
	}
	if (UWorld* World = GetWorld())
	{
		FName MapToLoad = FName(TEXT("L_OpenWater"));
		if (UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
		{
			MapToLoad = FName(*GI->PendingMapName);
		}
		UGameplayStatics::OpenLevel(World, MapToLoad);
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
	Navigator.OnAction = [this](FKiteMenuNavigator::EAction Action)
	{
		KiteSurfMenuStyle::PlayMenuSound(this, Action == FKiteMenuNavigator::EAction::Activated ? EKiteMenuSound::Select : EKiteMenuSound::Move);
	};
	Navigator.AddButton(SlatePlayButton, [this]() { OnPlayClicked(); });
	Navigator.AddButton(SlateSettingsButton, [this]() { OnSettingsClicked(); });
	Navigator.AddButton(SlateQuitButton, [this]() { OnQuitClicked(); });
}

FReply UKiteSurfMainMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bIntroPlaying)
	{
		SkipIntro();
		return FReply::Handled();
	}

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
