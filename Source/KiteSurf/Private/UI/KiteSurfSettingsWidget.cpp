#include "UI/KiteSurfSettingsWidget.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfSaveGame.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Blueprint/WidgetTree.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Reply.h"

UKiteSurfSettingsWidget::UKiteSurfSettingsWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, CurrentWindKnots(15.0f)
	, CurrentVolume(1.0f)
{
	bIsFocusable = true;
}

void UKiteSurfSettingsWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UWorld* World = GetWorld())
	{
		if (UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
		{
			CurrentWindKnots = GI->PendingWindKnots;
			CurrentVolume = GI->MasterVolume;
		}
		else
		{
			UKiteSurfSaveGame* SaveGame = UKiteSurfSaveGame::LoadOrCreateSettings();
			if (SaveGame)
			{
				CurrentWindKnots = SaveGame->WindStrengthKnots;
				CurrentVolume = SaveGame->MasterVolume;
			}
		}
	}

	CurrentWindKnots = FMath::Clamp(CurrentWindKnots, 8.0f, 30.0f);
	CurrentVolume = FMath::Clamp(CurrentVolume, 0.0f, 1.0f);

	if (WindSlider)
	{
		WindSlider->SetMinValue(8.0f);
		WindSlider->SetMaxValue(30.0f);
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

	// Fallback Slate UI
	return SNew(SBorder)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.BorderBackgroundColor(FLinearColor(0.02f, 0.05f, 0.1f, 0.92f))
		[
			SNew(SBox)
			.WidthOverride(500.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 20.0f, 20.0f, 10.0f)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("SETTINGS")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 24))
					.ColorAndOpacity(FLinearColor(1.0f, 0.85f, 0.2f))
				]
				// Wind row
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 10.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("WIND STRENGTH:")))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(10.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(SlateWindSlider, SSlider)
						.Value((CurrentWindKnots - 8.0f) / 22.0f)
						.OnValueChanged_Lambda([this](float NewNormValue)
						{
							OnWindSliderChanged(8.0f + NewNormValue * 22.0f);
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SAssignNew(SlateWindText, STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("%.0f kn"), CurrentWindKnots)))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						.ColorAndOpacity(FLinearColor(0.3f, 0.8f, 1.0f))
					]
				]
				// Volume row
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 10.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("MASTER VOLUME:")))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(10.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(SlateVolumeSlider, SSlider)
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
						SAssignNew(SlateVolumeText, STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("%.0f%%"), CurrentVolume * 100.0f)))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						.ColorAndOpacity(FLinearColor::White)
					]
				]
				// Back button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 25.0f, 20.0f, 20.0f)
				.HAlign(HAlign_Center)
				[
					SAssignNew(SlateBackButton, SButton)
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
	CurrentWindKnots = FMath::Clamp(Value, 8.0f, 30.0f);
	UpdateTextDisplays();
}

void UKiteSurfSettingsWidget::OnVolumeSliderChanged(float Value)
{
	CurrentVolume = FMath::Clamp(Value, 0.0f, 1.0f);
	UpdateTextDisplays();
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
}

void UKiteSurfSettingsWidget::OnBackClicked()
{
	if (UWorld* World = GetWorld())
	{
		if (UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
		{
			GI->SetPendingWindKnots(CurrentWindKnots);
			GI->SetMasterVolume(CurrentVolume);
			GI->SaveSettingsToDisk();
		}
		else
		{
			UKiteSurfSaveGame* SaveGame = UKiteSurfSaveGame::LoadOrCreateSettings();
			if (SaveGame)
			{
				SaveGame->WindStrengthKnots = CurrentWindKnots;
				SaveGame->MasterVolume = CurrentVolume;
				SaveGame->SaveSettings();
			}
		}
	}

	OnBackClickedDelegate.Broadcast();
	RemoveFromParent();
}

void UKiteSurfSettingsWidget::FocusFirst()
{
	if (BackButton)
	{
		BackButton->SetKeyboardFocus();
	}
	else if (SlateBackButton.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(SlateBackButton);
	}
}

FReply UKiteSurfSettingsWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_FaceButton_Right)
	{
		OnBackClicked();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
