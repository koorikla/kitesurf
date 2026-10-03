#include "UI/KiteSurfControlsLegend.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FKiteSurfControlBinding ControlBindings[] =
	{
		{ TEXT("Steer the kite round the window"), TEXT("Left / Right"),              TEXT("Right stick left / right") },
		{ TEXT("Bar in / out (power; unhooked: arms in / out); let go: back to the middle"), TEXT("Down / Up (hold)"), TEXT("Right stick, RT") },
		{ TEXT("Loop the kite"),                   TEXT("Keep steering towards the kite's own side"), TEXT("Keep the stick towards the kite's own side") },
		{ TEXT("Bar on the mouse"),                TEXT("Hold right button: move to steer and sheet"), TEXT("") },
		{ TEXT("Motion bar: recentre, bar in the middle"), TEXT("Home"),                  TEXT("Right stick click") },
		{ TEXT("Turn the board"),                  TEXT("A / D"),                     TEXT("Left stick left / right") },
		{ TEXT("Weight on the nose / the tail"),   TEXT("W / S"),                     TEXT("Left stick up / down") },
		{ TEXT("Hold to load the edge, let go to pop"), TEXT("Space"),                    TEXT("Bottom face button") },
		{ TEXT("Pre-wind: hold Shift / LT and a direction while loading"), TEXT("WASD + hold Space + Shift"), TEXT("Left stick + hold bottom face button + LT") },
		{ TEXT("Air, with Shift / LT: towards your back: back roll; away: front roll; up-diagonal: spin; W / S: flip"), TEXT("A / D / W / S + Shift"), TEXT("Left stick + LT") },
		{ TEXT("Hold jump in the air: tuck"),      TEXT("Space"),                     TEXT("Bottom face button") },
		{ TEXT("Air: grab, front / back hand (hold)"), TEXT("Q / E"),                 TEXT("LB / RB") },
		{ TEXT("Grab zone: nose, toe edge, heel edge, tail"), TEXT("W / S: nose / tail; A / D towards your chest: toe edge, your back: heel edge (while grabbing)"), TEXT("Left stick, same directions") },
		{ TEXT("Air: board-off (hold both; let go to catch before landing)"), TEXT("Q + E; W: superman, S: tic tac, A / D: board pass"), TEXT("LB + RB, left stick") },
		{ TEXT("Air: back foot out (hold; back in before landing)"), TEXT("C"),          TEXT("Left stick click") },
		{ TEXT("Hook in / unhook (on the water)"), TEXT("F"),                         TEXT("Top face button") },
		{ TEXT("Unhooked air: handle pass (lines slack, back to the kite)"), TEXT("X"), TEXT("Left face button") },
		{ TEXT("Reset the rider"),                 TEXT("R"),                         TEXT("Right face button") },
		{ TEXT("Pause menu"),                      TEXT("Esc or P"),                  TEXT("Start") },
		{ TEXT("In menus: move, change, select"),  TEXT("Arrows, Enter; Esc goes back"), TEXT("D-pad or left stick, bottom face button") },
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
	return ControlBindings;
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

	for (const FKiteSurfControlBinding& Binding : ControlBindings)
	{
		Table->AddSlot()
		.AutoHeight()
		[
			MakeRow(Binding.Action, Binding.Keyboard, Binding.Gamepad, FCoreStyle::GetDefaultFontStyle("Regular", 13), ActionColor, KeyColor)
		];
	}

	return Table;
}
