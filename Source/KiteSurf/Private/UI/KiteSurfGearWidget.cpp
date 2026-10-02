#include "UI/KiteSurfGearWidget.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfMenuStyle.h"
#include "UI/KiteSurfSaveGame.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "WindComponent.h"
#include "KiteSurfSpot.h"
#include "EngineUtils.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Input/Reply.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const float MinWindKnots = 8.0f;
	const float MaxWindKnots = 40.0f;
	const FLinearColor LabelColor(0.75f, 0.82f, 0.9f);
	const FLinearColor HintColor(0.55f, 0.75f, 0.9f);
	const FLinearColor TitleColor(1.0f, 0.85f, 0.2f);
}

UKiteSurfGearWidget::UKiteSurfGearWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

void UKiteSurfGearWidget::LoadChoices()
{
	if (bChoicesLoaded)
	{
		return;
	}
	bChoicesLoaded = true;

	const UWorld* World = GetWorld();
	if (const UKiteSurfGameInstance* GI = World ? Cast<UKiteSurfGameInstance>(World->GetGameInstance()) : nullptr)
	{
		CurrentRider = GI->RiderCharacter;
		CurrentKiteModel = GI->KiteModel;
		CurrentKiteSizeM2 = GI->KiteSizeM2;
		CurrentBoardSize = GI->BoardSize;
		CurrentWindKnots = GI->PendingWindKnots;
		bIslands = GI->bSpotIslands;
		bSandbars = GI->bSpotSandbars;
		bSharks = GI->bSpotSharks;
	}
	CurrentWindKnots = FMath::Clamp(CurrentWindKnots, MinWindKnots, MaxWindKnots);
}

void UKiteSurfGearWidget::NativeConstruct()
{
	Super::NativeConstruct();
	LoadChoices();
	UpdateTexts();
	FocusFirst();
}

TSharedRef<SWidget> UKiteSurfGearWidget::BuildChoiceRow(const TCHAR* Label, TSharedPtr<SButton>& OutButton, TSharedPtr<STextBlock>& OutValueText, TSharedPtr<STextBlock>& OutDescriptionText, TFunction<void()> OnClicked)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(120.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Label))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
					.ColorAndOpacity(LabelColor)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(OutButton, SButton)
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([OnClicked]()
				{
					OnClicked();
					return FReply::Handled();
				})
				[
					SAssignNew(OutValueText, STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
					.Margin(FMargin(6.0f, 3.0f))
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(120.0f, 3.0f, 0.0f, 0.0f)
		[
			SAssignNew(OutDescriptionText, STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
			.ColorAndOpacity(HintColor)
			.AutoWrapText(true)
		];
}

TSharedRef<SWidget> UKiteSurfGearWidget::RebuildWidget()
{
	if (WidgetTree && WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	LoadChoices();
	BackgroundTexture = KiteSurfMenuStyle::LoadBackgroundTexture();
	KiteSurfMenuStyle::SetupBackgroundBrush(BackgroundBrush, BackgroundTexture);

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(20.0f, 14.0f, 20.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("GEAR")))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
			.ColorAndOpacity(TitleColor)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(20.0f, 0.0f, 20.0f, 12.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Pick the wind, then rig for it.")))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
			.ColorAndOpacity(HintColor)
		]
		// Wind
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(20.0f, 8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(120.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("WIND")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
					.ColorAndOpacity(LabelColor)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(WindSlider, SSlider)
				.Value((CurrentWindKnots - MinWindKnots) / (MaxWindKnots - MinWindKnots))
				.StepSize(1.0f / (MaxWindKnots - MinWindKnots))
				.OnValueChanged_Lambda([this](float Normalised)
				{
					SetWindKnots(MinWindKnots + Normalised * (MaxWindKnots - MinWindKnots));
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(70.0f).HAlign(HAlign_Right)
				[
					SAssignNew(WindText, STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
					.ColorAndOpacity(FLinearColor(0.3f, 0.8f, 1.0f))
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(20.0f, 8.0f)
		[
			BuildChoiceRow(TEXT("KITE SIZE"), KiteSizeButton, KiteSizeText, KiteSizeDescription, [this]() { CycleKiteSize(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(20.0f, 8.0f)
		[
			BuildChoiceRow(TEXT("KITE"), KiteModelButton, KiteModelText, KiteModelDescription, [this]() { CycleKiteModel(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(20.0f, 8.0f)
		[
			BuildChoiceRow(TEXT("BOARD"), BoardButton, BoardText, BoardDescription, [this]() { CycleBoardSize(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(20.0f, 8.0f)
		[
			BuildChoiceRow(TEXT("RIDER"), RiderButton, RiderText, RiderDescription, [this]() { CycleRider(); })
		]
		// Buttons
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(20.0f, 18.0f, 20.0f, 14.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SAssignNew(ConfirmButton, SButton)
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]()
				{
					Confirm();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(bDuringRide ? TEXT("APPLY") : TEXT("RIDE")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
					.Margin(FMargin(10.0f, 6.0f))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]()
				{
					Cancel();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("BACK")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
					.Margin(FMargin(10.0f, 6.0f))
				]
			]
		];

	const TSharedRef<SVerticalBox> SpotRows = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(20.0f, 14.0f, 20.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("SPOT")))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
			.ColorAndOpacity(TitleColor)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(20.0f, 0.0f, 20.0f, 12.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("What is in the water.")))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
			.ColorAndOpacity(HintColor)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(20.0f, 8.0f)
		[
			BuildChoiceRow(TEXT("SANDBARS"), SandbarsButton, SandbarsText, SandbarsDescription, [this]() { ToggleSandbars(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(20.0f, 8.0f)
		[
			BuildChoiceRow(TEXT("ISLANDS"), IslandsButton, IslandsText, IslandsDescription, [this]() { ToggleIslands(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(20.0f, 8.0f, 20.0f, 14.0f)
		[
			BuildChoiceRow(TEXT("SHARKS"), SharksButton, SharksText, SharksDescription, [this]() { ToggleSharks(); })
		];

	const TSharedRef<SWidget> Panel = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			.WidthOverride(520.0f)
			[
				KiteSurfMenuStyle::BuildPanel(Rows)
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		.Padding(18.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(400.0f)
			[
				KiteSurfMenuStyle::BuildPanel(SpotRows)
			]
		];

	TSharedRef<SWidget> Root = SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(FMargin(70.0f, 0.0f, 0.0f, 0.0f))
		[
			Panel
		];
	if (bDuringRide)
	{
		// Over the paused game: dim it rather than replace it with the menu art.
		Root = SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.0f, 0.02f, 0.05f, 0.75f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				Panel
			];
	}
	else
	{
		Root = KiteSurfMenuStyle::BuildBackdrop(&BackgroundBrush, BackgroundTexture != nullptr, KiteSurfMenuStyle::MenuLoopBrush(GetGameInstance()), Root);
	}

	UpdateTexts();
	return Root;
}

void UKiteSurfGearWidget::ToggleIslands()
{
	bIslands = !bIslands;
	UpdateTexts();
}

void UKiteSurfGearWidget::ToggleSandbars()
{
	bSandbars = !bSandbars;
	UpdateTexts();
}

void UKiteSurfGearWidget::ToggleSharks()
{
	bSharks = !bSharks;
	UpdateTexts();
}

void UKiteSurfGearWidget::CycleRider()
{
	CurrentRider = RiderCharacter::Next(CurrentRider);
	UpdateTexts();
}

void UKiteSurfGearWidget::CycleKiteModel()
{
	CurrentKiteModel = KiteGear::Next(CurrentKiteModel);
	UpdateTexts();
}

void UKiteSurfGearWidget::CycleKiteSize()
{
	const TConstArrayView<float> Sizes = UKiteComponent::GetKiteSizesM2();
	const int32 Index = Sizes.IndexOfByKey(CurrentKiteSizeM2);
	// Recommended, then smallest to biggest, then back to recommended.
	CurrentKiteSizeM2 = Index == INDEX_NONE ? Sizes[0] : (Index + 1 < Sizes.Num() ? Sizes[Index + 1] : 0.0f);
	UpdateTexts();
}

void UKiteSurfGearWidget::SetKiteSizeM2(float SizeM2)
{
	CurrentKiteSizeM2 = UKiteComponent::GetKiteSizesM2().Contains(SizeM2) ? SizeM2 : 0.0f;
	UpdateTexts();
}

void UKiteSurfGearWidget::CycleBoardSize()
{
	CurrentBoardSize = KiteGear::Next(CurrentBoardSize);
	UpdateTexts();
}

void UKiteSurfGearWidget::SetWindKnots(float Knots)
{
	CurrentWindKnots = FMath::Clamp(FMath::RoundToFloat(Knots), MinWindKnots, MaxWindKnots);
	UpdateTexts();
}

float UKiteSurfGearWidget::GetEffectiveKiteSizeM2() const
{
	return CurrentKiteSizeM2 > 0.0f ? CurrentKiteSizeM2 : UKiteComponent::RecommendKiteSizeM2(CurrentWindKnots);
}

FString UKiteSurfGearWidget::GetKiteSizeText() const
{
	const float Recommended = UKiteComponent::RecommendKiteSizeM2(CurrentWindKnots);
	if (CurrentKiteSizeM2 <= 0.0f)
	{
		return FString::Printf(TEXT("AUTO: %.0f m"), Recommended);
	}
	return FString::Printf(TEXT("%.0f m"), CurrentKiteSizeM2);
}

FString UKiteSurfGearWidget::GetPowerText() const
{
	const float Recommended = UKiteComponent::RecommendKiteSizeM2(CurrentWindKnots);
	const float Ratio = GetEffectiveKiteSizeM2() / FMath::Max(Recommended, 1.0f);
	if (CurrentKiteSizeM2 <= 0.0f)
	{
		return FString::Printf(TEXT("The size a rider would rig for %.0f kn. Click to choose your own."), CurrentWindKnots);
	}
	if (Ratio < 0.8f)
	{
		return FString::Printf(TEXT("Small for %.0f kn (%.0f m recommended): easy to hold, quick to loop, less lift."), CurrentWindKnots, Recommended);
	}
	if (Ratio > 1.25f)
	{
		return FString::Printf(TEXT("Big for %.0f kn (%.0f m recommended): huge pull and lift, hard to hold down."), CurrentWindKnots, Recommended);
	}
	return FString::Printf(TEXT("Well powered for %.0f kn (%.0f m recommended)."), CurrentWindKnots, Recommended);
}

void UKiteSurfGearWidget::UpdateTexts()
{
	auto Set = [](const TSharedPtr<STextBlock>& Block, const FString& Text)
	{
		if (Block.IsValid())
		{
			Block->SetText(FText::FromString(Text));
		}
	};
	Set(WindText, FString::Printf(TEXT("%.0f kn"), CurrentWindKnots));
	Set(KiteSizeText, GetKiteSizeText());
	Set(KiteSizeDescription, GetPowerText());
	Set(KiteModelText, KiteGear::GetDisplayName(CurrentKiteModel));
	Set(KiteModelDescription, KiteGear::GetDescription(CurrentKiteModel));
	Set(BoardText, KiteGear::GetDisplayName(CurrentBoardSize));
	Set(BoardDescription, KiteGear::GetDescription(CurrentBoardSize));
	Set(RiderText, RiderCharacter::GetDisplayName(CurrentRider));
	Set(RiderDescription, TEXT("Who is on the board."));
	Set(SandbarsText, bSandbars ? TEXT("ON") : TEXT("OFF"));
	Set(SandbarsDescription, TEXT("Strips of sand across your reach. Jump them: riding onto one is a crash."));
	Set(IslandsText, bIslands ? TEXT("ON") : TEXT("OFF"));
	Set(IslandsDescription, TEXT("Sand islands with palms, further out. Something to ride round."));
	Set(SharksText, bSharks ? TEXT("ON") : TEXT("OFF"));
	Set(SharksDescription, TEXT("They patrol in circles, and come for a rider who is down in the water."));
}

void UKiteSurfGearWidget::Confirm()
{
	if (UWorld* World = GetWorld())
	{
		if (UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
		{
			GI->SetPendingWindKnots(CurrentWindKnots);
			GI->SetRiderCharacter(CurrentRider);
			GI->SetKiteModel(CurrentKiteModel);
			GI->SetKiteSizeM2(CurrentKiteSizeM2);
			GI->SetBoardSize(CurrentBoardSize);
			GI->SetSpotFeatures(bIslands, bSandbars, bSharks);
			GI->SaveSettingsToDisk();
		}

		// A ride that is already under way is re-rigged on the spot.
		const APlayerController* PC = World->GetFirstPlayerController();
		if (AKiteRiderPawn* Rider = PC ? Cast<AKiteRiderPawn>(PC->GetPawn()) : nullptr)
		{
			if (UWindComponent* Wind = Rider->GetWind())
			{
				const FVector Direction = Wind->BaseWind.IsNearlyZero() ? FVector::ForwardVector : Wind->BaseWind.GetSafeNormal();
				Wind->BaseWind = Direction * CurrentWindKnots * 51.44f;
			}
			if (UKiteComponent* Kite = Rider->GetKite())
			{
				Kite->SetKiteModel(CurrentKiteModel);
				Kite->SetKiteSize(GetEffectiveKiteSizeM2());
			}
			if (UBoardMovementComponent* Board = Rider->GetBoardMovement())
			{
				Board->SetBoardSize(CurrentBoardSize);
			}
			Rider->SetRiderCharacter(CurrentRider);
		}

		// The spot keeps its layout; features switch on and off where they are.
		if (const TActorIterator<AKiteSurfSpot> Spot(World); Spot)
		{
			Spot->SetFeatures(bIslands, bSandbars, bSharks);
		}
	}

	OnConfirmedDelegate.Broadcast();
	RemoveFromParent();
}

void UKiteSurfGearWidget::Cancel()
{
	OnCancelledDelegate.Broadcast();
	RemoveFromParent();
}

void UKiteSurfGearWidget::FocusFirst()
{
	if (ConfirmButton.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(ConfirmButton);
	}
}

FReply UKiteSurfGearWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)
	{
		Cancel();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
