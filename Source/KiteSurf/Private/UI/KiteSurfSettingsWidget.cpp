#include "UI/KiteSurfSettingsWidget.h"
#include "UI/KiteSurfMainMenuGameMode.h"
#include "UI/KiteSurfMenuStyle.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "WindComponent.h"
#include "GameFramework/PlayerController.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfSaveGame.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Blueprint/WidgetTree.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Reply.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"
#include "Kismet/KismetSystemLibrary.h"

UKiteSurfSettingsWidget::UKiteSurfSettingsWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, CurrentWindKnots(20.0f)
	, CurrentVolume(1.0f)
	, CurrentWindowMode(EWindowMode::Windowed)
	, CurrentResolution(1600, 900)
	, bCurrentVSync(false)
	, CurrentQualityPreset(3)
	, CurrentRiderCharacter(ERiderCharacter::Santa)
	, CurrentKiteSizeM2(0.0f)
{
	SetIsFocusable(true);
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		if (WidgetTree == nullptr)
		{
			WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
		}
		InitializeSettings();
	}
}

void UKiteSurfSettingsWidget::InitializeSettings()
{
	if (HasAnyFlags(RF_ClassDefaultObject) || !GEngine)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
		{
			CurrentWindKnots = GI->PendingWindKnots;
			CurrentVolume = GI->MasterVolume;
			CurrentMusicVolume = GI->MusicVolume;
			CurrentAmbientVolume = GI->AmbientVolume;
			CurrentEffectsVolume = GI->EffectsVolume;
			bSkipOnboarding = GI->bSkipOnboarding;
			CurrentRiderCharacter = GI->RiderCharacter;
			CurrentKiteSizeM2 = GI->KiteSizeM2;
			bMotionBar = GI->bMotionBar;
			bHaptics = GI->bHaptics;
		}
		else
		{
			UKiteSurfSaveGame* SaveGame = UKiteSurfSaveGame::LoadOrCreateSettings();
			if (SaveGame)
			{
				CurrentWindKnots = SaveGame->WindStrengthKnots;
				CurrentVolume = SaveGame->MasterVolume;
				CurrentMusicVolume = SaveGame->MusicVolume;
				CurrentAmbientVolume = SaveGame->AmbientVolume;
				CurrentEffectsVolume = SaveGame->EffectsVolume;
				bSkipOnboarding = SaveGame->bSkipOnboarding;
				CurrentRiderCharacter = RiderCharacter::FromIndex(SaveGame->RiderCharacterIndex);
				CurrentKiteSizeM2 = UKiteComponent::GetKiteSizesM2().Contains(SaveGame->KiteSizeM2) ? SaveGame->KiteSizeM2 : 0.0f;
			}
		}
	}

	CurrentWindKnots = FMath::Clamp(CurrentWindKnots, 8.0f, 40.0f);
	CurrentVolume = FMath::Clamp(CurrentVolume, 0.0f, 1.0f);

	if (UGameUserSettings* UserSettings = UGameUserSettings::GetGameUserSettings())
	{
		CurrentWindowMode = UserSettings->GetFullscreenMode();
		if (CurrentWindowMode != EWindowMode::Windowed && CurrentWindowMode != EWindowMode::WindowedFullscreen)
		{
			CurrentWindowMode = EWindowMode::WindowedFullscreen;
		}
		CurrentResolution = UserSettings->GetScreenResolution();
		if (CurrentResolution.X <= 0 || CurrentResolution.Y <= 0)
		{
			CurrentResolution = FIntPoint(1600, 900);
		}
		bCurrentVSync = UserSettings->IsVSyncEnabled();
		const int32 OverallQuality = UserSettings->GetOverallScalabilityLevel();
		CurrentQualityPreset = (OverallQuality >= 0 && OverallQuality <= 3) ? OverallQuality : 3;
	}

	// Populate supported resolutions
	SupportedResolutions.Empty();
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(SupportedResolutions);
	if (SupportedResolutions.Num() == 0)
	{
		SupportedResolutions.Add(FIntPoint(1280, 720));
		SupportedResolutions.Add(FIntPoint(1600, 900));
		SupportedResolutions.Add(FIntPoint(1920, 1080));
		SupportedResolutions.Add(FIntPoint(2560, 1440));
		SupportedResolutions.Add(FIntPoint(3840, 2160));
	}
	if (!SupportedResolutions.Contains(CurrentResolution))
	{
		SupportedResolutions.Add(CurrentResolution);
		SupportedResolutions.Sort([](const FIntPoint& A, const FIntPoint& B)
		{
			return (A.X != B.X) ? (A.X < B.X) : (A.Y < B.Y);
		});
	}
}

void UKiteSurfSettingsWidget::NativeConstruct()
{
	Super::NativeConstruct();

	InitializeSettings();

	if (WindSlider)
	{
		WindSlider->SetMinValue(8.0f);
		WindSlider->SetMaxValue(40.0f);
		WindSlider->SetStepSize(1.0f);
		WindSlider->SetValue(CurrentWindKnots);
		WindSlider->OnValueChanged.AddDynamic(this, &UKiteSurfSettingsWidget::OnWindSliderChanged);
	}

	if (VolumeSlider)
	{
		VolumeSlider->SetMinValue(0.0f);
		VolumeSlider->SetMaxValue(1.0f);
		VolumeSlider->SetStepSize(0.05f);
		VolumeSlider->SetValue(CurrentVolume);
		VolumeSlider->OnValueChanged.AddDynamic(this, &UKiteSurfSettingsWidget::OnVolumeSliderChanged);
	}

	if (FullscreenToggleButton)
	{
		FullscreenToggleButton->OnClicked.AddDynamic(this, &UKiteSurfSettingsWidget::ToggleFullscreen);
	}

	if (ResolutionComboBox)
	{
		ResolutionComboBox->ClearOptions();
		for (const FIntPoint& Res : SupportedResolutions)
		{
			ResolutionComboBox->AddOption(FString::Printf(TEXT("%d x %d"), Res.X, Res.Y));
		}
		ResolutionComboBox->SetSelectedOption(FString::Printf(TEXT("%d x %d"), CurrentResolution.X, CurrentResolution.Y));
		ResolutionComboBox->OnSelectionChanged.AddDynamic(this, &UKiteSurfSettingsWidget::OnResolutionComboSelectionChanged);
	}

	if (VSyncToggleButton)
	{
		VSyncToggleButton->OnClicked.AddDynamic(this, &UKiteSurfSettingsWidget::ToggleVSync);
	}

	if (QualityPresetComboBox)
	{
		QualityPresetComboBox->ClearOptions();
		QualityPresetComboBox->AddOption(TEXT("Low"));
		QualityPresetComboBox->AddOption(TEXT("Medium"));
		QualityPresetComboBox->AddOption(TEXT("High"));
		QualityPresetComboBox->AddOption(TEXT("Epic"));
		QualityPresetComboBox->SetSelectedIndex(CurrentQualityPreset);
		QualityPresetComboBox->OnSelectionChanged.AddDynamic(this, &UKiteSurfSettingsWidget::OnQualityComboSelectionChanged);
	}

	if (BackButton)
	{
		BackButton->OnClicked.AddDynamic(this, &UKiteSurfSettingsWidget::OnBackClicked);
	}

	UpdateTextDisplays();
	FocusFirst();
}

TSharedRef<SWidget> UKiteSurfSettingsWidget::RebuildWidget()
{
	if (WidgetTree && WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	InitializeSettings();

	// A label, a slider and a percentage, as the master volume row is laid out.
	auto MakeVolumeRow = [](const TCHAR* Label, TSharedPtr<SSlider>& Slider, TSharedPtr<STextBlock>& Text, float Value, TFunction<void(float)> OnChanged) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(170.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Label))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(10.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(Slider, SSlider)
				.IsFocusable(false)
				.Value(Value)
				.OnValueChanged_Lambda([OnChanged](float NewVal) { OnChanged(NewVal); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(70.0f).HAlign(HAlign_Right)
				[
					SAssignNew(Text, STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("%.0f%%"), Value * 100.0f)))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
					.ColorAndOpacity(FLinearColor::White)
				]
			];
	};

	ResolutionOptions.Empty();
	TSharedPtr<FString> InitiallySelectedRes;
	for (int32 i = 0; i < SupportedResolutions.Num(); ++i)
	{
		FString ResStr = FString::Printf(TEXT("%d x %d"), SupportedResolutions[i].X, SupportedResolutions[i].Y);
		TSharedPtr<FString> ResItem = MakeShared<FString>(ResStr);
		ResolutionOptions.Add(ResItem);
		if (SupportedResolutions[i] == CurrentResolution)
		{
			InitiallySelectedRes = ResItem;
		}
	}
	if (!InitiallySelectedRes.IsValid() && ResolutionOptions.Num() > 0)
	{
		InitiallySelectedRes = ResolutionOptions[0];
	}

	QualityOptions.Empty();
	QualityOptions.Add(MakeShared<FString>(TEXT("Low")));
	QualityOptions.Add(MakeShared<FString>(TEXT("Medium")));
	QualityOptions.Add(MakeShared<FString>(TEXT("High")));
	QualityOptions.Add(MakeShared<FString>(TEXT("Epic")));
	const int32 SafePreset = FMath::Clamp(CurrentQualityPreset, 0, 3);
	TSharedPtr<FString> InitiallySelectedQuality = QualityOptions[SafePreset];

	// A solid brush: the default border brush is a hollow frame, which lets whatever is behind
	// the settings show through the text.
	return SNew(SBorder)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.02f, 0.05f, 0.1f, 0.97f))
		[
			SNew(SBox)
			.WidthOverride(540.0f)
			[
				SNew(SVerticalBox)
				// Title
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 15.0f, 20.0f, 10.0f)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("SETTINGS")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 24))
					.ColorAndOpacity(FLinearColor(1.0f, 0.85f, 0.2f))
				]
				// Wind, kite, board and rider are chosen on the gear screen.
				// Volume row
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 6.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(170.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("MASTER VOLUME:")))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(10.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(SlateVolumeSlider, SSlider)
						.IsFocusable(false)
						.Value(CurrentVolume)
						.OnValueChanged_Lambda([this](float NewVal)
						{
							OnVolumeSliderChanged(NewVal);
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(70.0f).HAlign(HAlign_Right)
						[
							SAssignNew(SlateVolumeText, STextBlock)
							.Text(FText::FromString(FString::Printf(TEXT("%.0f%%"), CurrentVolume * 100.0f)))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
							.ColorAndOpacity(FLinearColor::White)
						]
					]
				]
				// Music, ambient and effects volume rows
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 6.0f)
				[
					MakeVolumeRow(TEXT("MUSIC VOLUME:"), SlateMusicSlider, SlateMusicText, CurrentMusicVolume, [this](float NewVal) { OnMusicSliderChanged(NewVal); })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 6.0f)
				[
					MakeVolumeRow(TEXT("AMBIENT VOLUME:"), SlateAmbientSlider, SlateAmbientText, CurrentAmbientVolume, [this](float NewVal) { OnAmbientSliderChanged(NewVal); })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 6.0f)
				[
					MakeVolumeRow(TEXT("EFFECTS VOLUME:"), SlateEffectsSlider, SlateEffectsText, CurrentEffectsVolume, [this](float NewVal) { OnEffectsSliderChanged(NewVal); })
				]
				// Fullscreen row
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 6.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(170.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("WINDOW MODE:")))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(10.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(SlateFullscreenButton, SButton)
						.IsFocusable(false)
						.HAlign(HAlign_Center)
						.OnClicked_Lambda([this]()
						{
							ToggleFullscreen();
							return FReply::Handled();
						})
						[
							SAssignNew(SlateFullscreenText, STextBlock)
							.Text(FText::FromString((CurrentWindowMode == EWindowMode::WindowedFullscreen) ? TEXT("BORDERLESS") : TEXT("WINDOWED")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
						]
					]
				]
				// Resolution row
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 6.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(170.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("RESOLUTION:")))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(10.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(SlateResolutionCombo, SComboBox<TSharedPtr<FString>>)
						.IsFocusable(false)
						.OptionsSource(&ResolutionOptions)
						.InitiallySelectedItem(InitiallySelectedRes)
						.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
						{
							return SNew(STextBlock)
								.Text(FText::FromString(*Item))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14));
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewItem, ESelectInfo::Type)
						{
							if (NewItem.IsValid())
							{
								int32 FoundIdx = ResolutionOptions.IndexOfByPredicate([&](const TSharedPtr<FString>& Item) { return Item == NewItem; });
								if (FoundIdx != INDEX_NONE && SupportedResolutions.IsValidIndex(FoundIdx))
								{
									SetResolution(SupportedResolutions[FoundIdx]);
								}
							}
						})
						[
							SAssignNew(SlateResolutionText, STextBlock)
							.Text(FText::FromString(FString::Printf(TEXT("%d x %d"), CurrentResolution.X, CurrentResolution.Y)))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
						]
					]
				]
				// Motion bar row
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 6.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(170.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("MOTION BAR:")))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(10.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(SlateMotionBarButton, SButton)
						.IsFocusable(false)
						.HAlign(HAlign_Center)
						.OnClicked_Lambda([this]()
						{
							ToggleMotionBar();
							return FReply::Handled();
						})
						[
							SAssignNew(SlateMotionBarText, STextBlock)
							.Text(FText::FromString(bMotionBar ? TEXT("ON") : TEXT("OFF")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(200.0f, 0.0f, 20.0f, 6.0f)
				[
					SAssignNew(SlateMotionBarNote, STextBlock)
					.Text(FText::FromString(GetMotionBarNote()))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					.ColorAndOpacity(FLinearColor(0.55f, 0.75f, 0.9f))
					.AutoWrapText(true)
				]
				// Vibration row
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 6.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(170.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("VIBRATION:")))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(10.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(SlateHapticsButton, SButton)
						.IsFocusable(false)
						.HAlign(HAlign_Center)
						.OnClicked_Lambda([this]()
						{
							ToggleHaptics();
							return FReply::Handled();
						})
						[
							SAssignNew(SlateHapticsText, STextBlock)
							.Text(FText::FromString(bHaptics ? TEXT("ON") : TEXT("OFF")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
						]
					]
				]
				// VSync row
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 6.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(170.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("VSYNC:")))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(10.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(SlateVSyncButton, SButton)
						.IsFocusable(false)
						.HAlign(HAlign_Center)
						.OnClicked_Lambda([this]()
						{
							ToggleVSync();
							return FReply::Handled();
						})
						[
							SAssignNew(SlateVSyncText, STextBlock)
							.Text(FText::FromString(bCurrentVSync ? TEXT("ENABLED") : TEXT("DISABLED")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
						]
					]
				]
				// Quality preset row
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 6.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(170.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("QUALITY PRESET:")))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(10.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(SlateQualityCombo, SComboBox<TSharedPtr<FString>>)
						.IsFocusable(false)
						.OptionsSource(&QualityOptions)
						.InitiallySelectedItem(InitiallySelectedQuality)
						.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
						{
							return SNew(STextBlock)
								.Text(FText::FromString(*Item))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14));
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewItem, ESelectInfo::Type)
						{
							if (NewItem.IsValid())
							{
								int32 FoundIdx = QualityOptions.IndexOfByPredicate([&](const TSharedPtr<FString>& Item) { return Item == NewItem; });
								if (FoundIdx != INDEX_NONE)
								{
									SetQualityPreset(FoundIdx);
								}
							}
						})
						[
							SAssignNew(SlateQualityText, STextBlock)
							.Text(FText::FromString(*InitiallySelectedQuality))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
						]
					]
				]
				// Back button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 20.0f, 20.0f, 15.0f)
				.HAlign(HAlign_Center)
				[
					SAssignNew(SlateBackButton, SButton)
					.IsFocusable(false)
					.OnClicked_Lambda([this]()
					{
						OnBackClicked();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("  BACK  ")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
					]
				]
			]
		];
}

void UKiteSurfSettingsWidget::OnWindSliderChanged(float Value)
{
	CurrentWindKnots = FMath::Clamp(Value, 8.0f, 40.0f);
	UpdateTextDisplays();
}

void UKiteSurfSettingsWidget::OnVolumeSliderChanged(float Value)
{
	CurrentVolume = FMath::Clamp(Value, 0.0f, 1.0f);
	if (SlateVolumeSlider.IsValid() && !FMath::IsNearlyEqual(SlateVolumeSlider->GetValue(), CurrentVolume))
	{
		SlateVolumeSlider->SetValue(CurrentVolume); // moved by the keys rather than the mouse
	}
	UpdateTextDisplays();
}

void UKiteSurfSettingsWidget::ShowVolume(const TSharedPtr<SSlider>& Slider, const TSharedPtr<STextBlock>& Text, float Volume)
{
	if (Slider.IsValid() && !FMath::IsNearlyEqual(Slider->GetValue(), Volume))
	{
		Slider->SetValue(Volume);
	}
	if (Text.IsValid())
	{
		Text->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Volume * 100.0f)));
	}
}

// Each of these is heard straight away, in the menu or over a paused ride.
void UKiteSurfSettingsWidget::OnMusicSliderChanged(float Value)
{
	CurrentMusicVolume = FMath::Clamp(Value, 0.0f, 1.0f);
	ShowVolume(SlateMusicSlider, SlateMusicText, CurrentMusicVolume);
	if (UWorld* World = GetWorld())
	{
		if (AKiteSurfMainMenuGameMode* MenuMode = Cast<AKiteSurfMainMenuGameMode>(World->GetAuthGameMode()))
		{
			MenuMode->SetMusicVolume(CurrentMusicVolume);
		}
		const APlayerController* PC = World->GetFirstPlayerController();
		if (AKiteRiderPawn* Rider = PC ? Cast<AKiteRiderPawn>(PC->GetPawn()) : nullptr)
		{
			Rider->SetMusicVolume(CurrentMusicVolume);
		}
	}
}

void UKiteSurfSettingsWidget::OnAmbientSliderChanged(float Value)
{
	CurrentAmbientVolume = FMath::Clamp(Value, 0.0f, 1.0f);
	ShowVolume(SlateAmbientSlider, SlateAmbientText, CurrentAmbientVolume);
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (AKiteRiderPawn* Rider = PC ? Cast<AKiteRiderPawn>(PC->GetPawn()) : nullptr)
	{
		Rider->SetAmbientVolume(CurrentAmbientVolume);
	}
}

void UKiteSurfSettingsWidget::OnEffectsSliderChanged(float Value)
{
	CurrentEffectsVolume = FMath::Clamp(Value, 0.0f, 1.0f);
	ShowVolume(SlateEffectsSlider, SlateEffectsText, CurrentEffectsVolume);
	const UWorld* World = GetWorld();
	// The menus' own sounds read it from the game instance, so the next tick of this slider is at the new level.
	if (UKiteSurfGameInstance* GI = World ? Cast<UKiteSurfGameInstance>(World->GetGameInstance()) : nullptr)
	{
		GI->SetEffectsVolume(CurrentEffectsVolume);
	}
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (AKiteRiderPawn* Rider = PC ? Cast<AKiteRiderPawn>(PC->GetPawn()) : nullptr)
	{
		Rider->SetEffectsVolume(CurrentEffectsVolume);
	}
}

void UKiteSurfSettingsWidget::ToggleFullscreen()
{
	if (CurrentWindowMode == EWindowMode::WindowedFullscreen)
	{
		CurrentWindowMode = EWindowMode::Windowed;
	}
	else
	{
		CurrentWindowMode = EWindowMode::WindowedFullscreen;
	}
	ApplyVideoSettings();
	UpdateTextDisplays();
}

void UKiteSurfSettingsWidget::SetFullscreenMode(EWindowMode::Type InMode)
{
	CurrentWindowMode = InMode;
	ApplyVideoSettings();
	UpdateTextDisplays();
}

void UKiteSurfSettingsWidget::SetResolution(FIntPoint InResolution)
{
	CurrentResolution = InResolution;
	ApplyVideoSettings();
	UpdateTextDisplays();
}

void UKiteSurfSettingsWidget::SetResolutionByIndex(int32 Index)
{
	if (SupportedResolutions.IsValidIndex(Index))
	{
		SetResolution(SupportedResolutions[Index]);
	}
}

void UKiteSurfSettingsWidget::ToggleHaptics()
{
	bHaptics = !bHaptics;
	if (const UWorld* World = GetWorld())
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		if (AKiteRiderPawn* Rider = PC ? Cast<AKiteRiderPawn>(PC->GetPawn()) : nullptr)
		{
			Rider->SetHapticsEnabled(bHaptics);
			// A buzz to say it is on.
			Rider->PlayHaptic(0.5f, 0.15f, true);
		}
	}
	UpdateTextDisplays();
}

void UKiteSurfSettingsWidget::ToggleMotionBar()
{
	bMotionBar = !bMotionBar;

	// Straight away, so the note can say which controller it found.
	if (const UWorld* World = GetWorld())
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		if (AKiteRiderPawn* Rider = PC ? Cast<AKiteRiderPawn>(PC->GetPawn()) : nullptr)
		{
			Rider->SetMotionBarEnabled(bMotionBar);
		}
	}
	UpdateTextDisplays();
}

FString UKiteSurfSettingsWidget::GetMotionBarNote() const
{
	if (!bMotionBar)
	{
		return TEXT("Hold the controller like a bar: tilt to steer, tip it towards you for power. Needs a controller with motion sensors (PlayStation, Switch); Xbox controllers have none.");
	}
	FString Device;
	if (const UWorld* World = GetWorld())
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		if (const AKiteRiderPawn* Rider = PC ? Cast<AKiteRiderPawn>(PC->GetPawn()) : nullptr)
		{
			Device = Rider->IsMotionBarActive() ? Rider->GetMotionDeviceName() : FString();
		}
	}
	if (!Device.IsEmpty())
	{
		return FString::Printf(TEXT("Using %s. However you are holding it when you start is level; reset [R] re-centres."), *Device);
	}
	return TEXT("On. With no motion sensors found the right stick still works. However you hold the controller when the ride starts is level; reset [R] re-centres.");
}

void UKiteSurfSettingsWidget::ToggleVSync()
{
	bCurrentVSync = !bCurrentVSync;
	ApplyVideoSettings();
	UpdateTextDisplays();
}

void UKiteSurfSettingsWidget::SetVSyncEnabled(bool bInVSync)
{
	bCurrentVSync = bInVSync;
	ApplyVideoSettings();
	UpdateTextDisplays();
}

void UKiteSurfSettingsWidget::SetQualityPreset(int32 InPresetIndex)
{
	CurrentQualityPreset = FMath::Clamp(InPresetIndex, 0, 3);
	ApplyVideoSettings();
	UpdateTextDisplays();
}

void UKiteSurfSettingsWidget::CycleKiteSize()
{
	const TConstArrayView<float> Sizes = UKiteComponent::GetKiteSizesM2();
	const int32 Index = Sizes.IndexOfByKey(CurrentKiteSizeM2);
	// Recommended, then smallest to biggest, then back to recommended.
	CurrentKiteSizeM2 = Index == INDEX_NONE ? Sizes[0] : (Index + 1 < Sizes.Num() ? Sizes[Index + 1] : 0.0f);
	UpdateTextDisplays();
}

FString UKiteSurfSettingsWidget::GetKiteSizeText() const
{
	const float Recommended = UKiteComponent::RecommendKiteSizeM2(CurrentWindKnots);
	if (CurrentKiteSizeM2 <= 0.0f)
	{
		return FString::Printf(TEXT("AUTO: %.0f m for %.0f kn"), Recommended, CurrentWindKnots);
	}
	return FString::Printf(TEXT("%.0f m (%.0f m recommended)"), CurrentKiteSizeM2, Recommended);
}

void UKiteSurfSettingsWidget::CycleRiderCharacter()
{
	CurrentRiderCharacter = RiderCharacter::Next(CurrentRiderCharacter);
	UpdateTextDisplays();
}

void UKiteSurfSettingsWidget::ToggleSkipOnboarding()
{
	bSkipOnboarding = !bSkipOnboarding;
	UpdateTextDisplays();
}

void UKiteSurfSettingsWidget::SetSkipOnboarding(bool bInSkip)
{
	bSkipOnboarding = bInSkip;
	UpdateTextDisplays();
}

void UKiteSurfSettingsWidget::ApplyVideoSettings()
{
	if (HasAnyFlags(RF_ClassDefaultObject) || !GEngine)
	{
		return;
	}
	if (UGameUserSettings* UserSettings = UGameUserSettings::GetGameUserSettings())
	{
		UserSettings->SetFullscreenMode(CurrentWindowMode);
		UserSettings->SetScreenResolution(CurrentResolution);
		UserSettings->SetVSyncEnabled(bCurrentVSync);
		UserSettings->SetOverallScalabilityLevel(CurrentQualityPreset);
		UserSettings->ApplySettings(false);
	}
}

void UKiteSurfSettingsWidget::OnResolutionComboSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	for (int32 i = 0; i < SupportedResolutions.Num(); ++i)
	{
		if (SelectedItem == FString::Printf(TEXT("%d x %d"), SupportedResolutions[i].X, SupportedResolutions[i].Y))
		{
			SetResolution(SupportedResolutions[i]);
			break;
		}
	}
}

void UKiteSurfSettingsWidget::OnQualityComboSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectedItem.Equals(TEXT("Low"), ESearchCase::IgnoreCase))
	{
		SetQualityPreset(0);
	}
	else if (SelectedItem.Equals(TEXT("Medium"), ESearchCase::IgnoreCase))
	{
		SetQualityPreset(1);
	}
	else if (SelectedItem.Equals(TEXT("High"), ESearchCase::IgnoreCase))
	{
		SetQualityPreset(2);
	}
	else if (SelectedItem.Equals(TEXT("Epic"), ESearchCase::IgnoreCase))
	{
		SetQualityPreset(3);
	}
}

void UKiteSurfSettingsWidget::UpdateTextDisplays()
{
	const FString WindStr = FString::Printf(TEXT("%.0f kn"), CurrentWindKnots);
	if (WindValueText)
	{
		WindValueText->SetText(FText::FromString(WindStr));
	}
	if (SlateWindText.IsValid())
	{
		SlateWindText->SetText(FText::FromString(WindStr));
	}

	const FString VolStr = FString::Printf(TEXT("%.0f%%"), CurrentVolume * 100.0f);
	if (VolumeValueText)
	{
		VolumeValueText->SetText(FText::FromString(VolStr));
	}
	if (SlateVolumeText.IsValid())
	{
		SlateVolumeText->SetText(FText::FromString(VolStr));
	}

	const FString WindowModeStr = (CurrentWindowMode == EWindowMode::WindowedFullscreen) ? TEXT("BORDERLESS") : TEXT("WINDOWED");
	if (FullscreenValueText)
	{
		FullscreenValueText->SetText(FText::FromString(WindowModeStr));
	}
	if (SlateFullscreenText.IsValid())
	{
		SlateFullscreenText->SetText(FText::FromString(WindowModeStr));
	}

	const FString ResStr = FString::Printf(TEXT("%d x %d"), CurrentResolution.X, CurrentResolution.Y);
	if (SlateResolutionText.IsValid())
	{
		SlateResolutionText->SetText(FText::FromString(ResStr));
	}

	if (SlateKiteText.IsValid())
	{
		SlateKiteText->SetText(FText::FromString(GetKiteSizeText()));
	}

	if (SlateRiderText.IsValid())
	{
		SlateRiderText->SetText(FText::FromString(RiderCharacter::GetDisplayName(CurrentRiderCharacter)));
	}

	if (SlateHapticsText.IsValid())
	{
		SlateHapticsText->SetText(FText::FromString(bHaptics ? TEXT("ON") : TEXT("OFF")));
	}
	if (SlateMotionBarText.IsValid())
	{
		SlateMotionBarText->SetText(FText::FromString(bMotionBar ? TEXT("ON") : TEXT("OFF")));
	}
	if (SlateMotionBarNote.IsValid())
	{
		SlateMotionBarNote->SetText(FText::FromString(GetMotionBarNote()));
	}

	const FString VSyncStr = bCurrentVSync ? TEXT("ENABLED") : TEXT("DISABLED");
	if (VSyncValueText)
	{
		VSyncValueText->SetText(FText::FromString(VSyncStr));
	}
	if (SlateVSyncText.IsValid())
	{
		SlateVSyncText->SetText(FText::FromString(VSyncStr));
	}

	static const TCHAR* QualityNames[] = { TEXT("LOW"), TEXT("MEDIUM"), TEXT("HIGH"), TEXT("EPIC") };
	const int32 SafePreset = FMath::Clamp(CurrentQualityPreset, 0, 3);
	if (SlateQualityText.IsValid())
	{
		SlateQualityText->SetText(FText::FromString(QualityNames[SafePreset]));
	}
}

void UKiteSurfSettingsWidget::OnBackClicked()
{
	if (UWorld* World = GetWorld())
	{
		if (UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
		{
			GI->SetPendingWindKnots(CurrentWindKnots);
			GI->SetMasterVolume(CurrentVolume);
			GI->SetMusicVolume(CurrentMusicVolume);
			GI->SetAmbientVolume(CurrentAmbientVolume);
			GI->SetEffectsVolume(CurrentEffectsVolume);
			GI->SetSkipOnboarding(bSkipOnboarding);
			GI->SetRiderCharacter(CurrentRiderCharacter);
			GI->SetKiteSizeM2(CurrentKiteSizeM2);
			GI->SetMotionBar(bMotionBar);
			GI->SetHaptics(bHaptics);
			GI->SaveSettingsToDisk();

			// A ride that is already under way gets the new wind and kite straight away.
			const APlayerController* PC = World->GetFirstPlayerController();
			if (AKiteRiderPawn* Rider = PC ? Cast<AKiteRiderPawn>(PC->GetPawn()) : nullptr)
			{
				if (UWindComponent* Wind = Rider->GetWind())
				{
					const FVector Direction = Wind->BaseWind.IsNearlyZero() ? FVector::ForwardVector : Wind->BaseWind.GetSafeNormal();
					Wind->BaseWind = Direction * GI->PendingWindKnots * 51.44f;
				}
				if (UKiteComponent* Kite = Rider->GetKite())
				{
					Kite->SetKiteSize(GI->GetEffectiveKiteSizeM2());
				}
			}
		}
		else
		{
			UKiteSurfSaveGame* SaveGame = UKiteSurfSaveGame::LoadOrCreateSettings();
			if (SaveGame)
			{
				SaveGame->WindStrengthKnots = CurrentWindKnots;
				SaveGame->MasterVolume = CurrentVolume;
				SaveGame->MusicVolume = CurrentMusicVolume;
				SaveGame->AmbientVolume = CurrentAmbientVolume;
				SaveGame->EffectsVolume = CurrentEffectsVolume;
				SaveGame->bSkipOnboarding = bSkipOnboarding;
				SaveGame->RiderCharacterIndex = static_cast<int32>(CurrentRiderCharacter);
				SaveGame->KiteSizeM2 = CurrentKiteSizeM2;
				SaveGame->SaveSettings();
			}
		}
	}

	ApplyVideoSettings();

	OnBackClickedDelegate.Broadcast();
	RemoveFromParent();
}

void UKiteSurfSettingsWidget::FocusFirst()
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

FKiteMenuNavigator& UKiteSurfSettingsWidget::GetNavigator()
{
	if (Navigator.Num() == 0)
	{
		BuildNavigation();
		Navigator.Select(Navigator.DefaultIndex);
	}
	return Navigator;
}

void UKiteSurfSettingsWidget::BuildNavigation()
{
	Navigator.Reset();
	Navigator.OnAction = [this](FKiteMenuNavigator::EAction Action)
	{
		KiteSurfMenuStyle::PlayMenuSound(this, Action == FKiteMenuNavigator::EAction::Activated ? EKiteMenuSound::Select : EKiteMenuSound::Move);
	};
	Navigator.AddSlider(SlateVolumeSlider, [this](int32 Direction) { OnVolumeSliderChanged(CurrentVolume + 0.05f * Direction); });
	Navigator.AddSlider(SlateMusicSlider, [this](int32 Direction) { OnMusicSliderChanged(CurrentMusicVolume + 0.05f * Direction); });
	Navigator.AddSlider(SlateAmbientSlider, [this](int32 Direction) { OnAmbientSliderChanged(CurrentAmbientVolume + 0.05f * Direction); });
	Navigator.AddSlider(SlateEffectsSlider, [this](int32 Direction) { OnEffectsSliderChanged(CurrentEffectsVolume + 0.05f * Direction); });
	Navigator.AddButton(SlateFullscreenButton, [this]() { ToggleFullscreen(); }, true);
	Navigator.AddText(SlateResolutionText, [this](int32 Direction)
	{
		// Step to the neighbouring resolution; from one that is not in the list, start at its end.
		const int32 Current = SupportedResolutions.IndexOfByKey(CurrentResolution);
		const int32 Next = Current == INDEX_NONE ? (Direction > 0 ? 0 : SupportedResolutions.Num() - 1) : Current + Direction;
		SetResolutionByIndex(FMath::Clamp(Next, 0, FMath::Max(SupportedResolutions.Num() - 1, 0)));
	});
	Navigator.AddButton(SlateMotionBarButton, [this]() { ToggleMotionBar(); }, true);
	Navigator.AddButton(SlateHapticsButton, [this]() { ToggleHaptics(); }, true);
	Navigator.AddButton(SlateVSyncButton, [this]() { ToggleVSync(); }, true);
	Navigator.AddText(SlateQualityText, [this](int32 Direction) { SetQualityPreset(CurrentQualityPreset + Direction); });
	Navigator.AddButton(SlateBackButton, [this]() { OnBackClicked(); });
}

FReply UKiteSurfSettingsWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (GetNavigator().HandleKey(Key))
	{
		return FReply::Handled();
	}
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_FaceButton_Right)
	{
		KiteSurfMenuStyle::PlayMenuSound(this, EKiteMenuSound::Back);
		OnBackClicked();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
