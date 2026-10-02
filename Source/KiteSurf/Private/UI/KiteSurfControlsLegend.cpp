#include "UI/KiteSurfControlsLegend.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FKiteSurfControlBinding Bindings[] =
	{
		{ TEXT("Steer the kite round the window"), TEXT("Left / Right"),              TEXT("Right stick left / right") },
		{ TEXT("Sheet in / out (power)"),          TEXT("Up / Down"),                 TEXT("Right stick up / down, triggers") },
		{ TEXT("Loop the kite"),                   TEXT("Hold Shift while steering"), TEXT("Hold right bumper while steering") },
		{ TEXT("Bar on the mouse"),                TEXT("Hold right button: move to steer and sheet, left button loops"), TEXT("") },
		{ TEXT("Turn the board; spin in the air"), TEXT("A / D"),                     TEXT("Left stick left / right") },
		{ TEXT("Weight on the nose / the tail"),   TEXT("W / S"),                     TEXT("Left stick up / down") },
		{ TEXT("Pop (hold S + send the kite first)"), TEXT("Space"),                     TEXT("Bottom face button") },
		{ TEXT("Reset the rider"),                 TEXT("R"),                         TEXT("Right face button") },
		{ TEXT("Pause menu"),                      TEXT("Esc or P"),                  TEXT("Start") },
	};

	const float ActionColumnWidth = 290.0f;
	const float KeyboardColumnWidth = 390.0f;
	const float GamepadColumnWidth = 220.0f;

	TSharedRef<SWidget> MakeCell(const TCHAR* Text, float Width, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		return SNew(SBox)
			.WidthOverride(Width)
			.Padding(FMargin(0.0f, 3.0f, 12.0f, 3.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Text))
				.Font(Font)
				.ColorAndOpacity(Color)
				.AutoWrapText(true)
			];
	}

	TSharedRef<SWidget> MakeRow(const TCHAR* Action, const TCHAR* Keyboard, const TCHAR* Gamepad, const FSlateFontInfo& Font, const FLinearColor& ActionColor, const FLinearColor& KeyColor)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[MakeCell(Action, ActionColumnWidth, Font, ActionColor)]
			+ SHorizontalBox::Slot().AutoWidth()[MakeCell(Keyboard, KeyboardColumnWidth, Font, KeyColor)]
			+ SHorizontalBox::Slot().AutoWidth()[MakeCell(Gamepad, GamepadColumnWidth, Font, KeyColor)];
	}
}

TConstArrayView<FKiteSurfControlBinding> KiteSurfControlsLegend::GetBindings()
{
	return Bindings;
}

TSharedRef<SWidget> KiteSurfControlsLegend::Build()
{
	const FLinearColor HeadingColor(1.0f, 0.85f, 0.2f);
	const FLinearColor ColumnColor(0.4f, 0.75f, 1.0f);
	const FLinearColor ActionColor(0.9f, 0.95f, 1.0f);
	const FLinearColor KeyColor(1.0f, 1.0f, 1.0f);

	TSharedRef<SVerticalBox> Table = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("CONTROLS")))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
			.ColorAndOpacity(HeadingColor)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeRow(TEXT(""), TEXT("KEYBOARD / MOUSE"), TEXT("GAMEPAD"), FCoreStyle::GetDefaultFontStyle("Bold", 11), ColumnColor, ColumnColor)
		];

	for (const FKiteSurfControlBinding& Binding : Bindings)
	{
		Table->AddSlot()
		.AutoHeight()
		[
			MakeRow(Binding.Action, Binding.Keyboard, Binding.Gamepad, FCoreStyle::GetDefaultFontStyle("Regular", 13), ActionColor, KeyColor)
		];
	}

	return Table;
}
