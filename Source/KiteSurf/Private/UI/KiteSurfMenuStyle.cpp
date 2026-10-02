#include "UI/KiteSurfMenuStyle.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"

UTexture2D* KiteSurfMenuStyle::LoadBackgroundTexture()
{
	return LoadObject<UTexture2D>(nullptr, TEXT("/Game/Textures/T_MenuBackground"));
}

void KiteSurfMenuStyle::SetupBackgroundBrush(FSlateBrush& Brush, UTexture2D* Texture)
{
	Brush = FSlateBrush();
	if (Texture)
	{
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		Brush.DrawAs = ESlateBrushDrawType::Image;
	}
}

TSharedRef<SWidget> KiteSurfMenuStyle::BuildBackdrop(const FSlateBrush* BackgroundBrush, bool bHasTexture, const TSharedRef<SWidget>& Content)
{
	TSharedRef<SOverlay> Overlay = SNew(SOverlay);

	// A solid colour underneath in any case, so nothing behind the menu shows through.
	Overlay->AddSlot()
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.01f, 0.03f, 0.08f, 1.0f))
	];
	if (bHasTexture && BackgroundBrush)
	{
		// Stretched to the screen. The art is 16:9, so on a 16:9 screen that is its own shape.
		Overlay->AddSlot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SImage).Image(BackgroundBrush)
		];
	}
	Overlay->AddSlot()
	[
		Content
	];
	return Overlay;
}

TSharedRef<SWidget> KiteSurfMenuStyle::BuildPanel(const TSharedRef<SWidget>& Content)
{
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.01f, 0.04f, 0.09f, 0.82f))
		.Padding(FMargin(8.0f, 10.0f))
		[
			Content
		];
}
