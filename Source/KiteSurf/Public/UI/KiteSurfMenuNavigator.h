#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class SButton;
class SSlider;
class STextBlock;

/**
 * Keyboard and gamepad navigation for a menu: a list of items in screen order, one of them
 * selected and highlighted. Up and down (arrows, D-pad, left stick) move the selection, left and
 * right change the selected item's value, and accept (Enter, Space, the bottom face button)
 * presses it. The menu keeps keyboard focus on itself and passes its key presses to HandleKey,
 * so the controls themselves are built not to take focus.
 */
class KITESURF_API FKiteMenuNavigator
{
public:
	/** What the selected item does; any of these may be empty. */
	struct FItem
	{
		/** Accept was pressed. */
		TFunction<void()> Activate;
		/** Left (-1) or right (+1) was pressed. */
		TFunction<void(int32)> Adjust;
		/** Show or hide the selection highlight. */
		TFunction<void(bool)> Highlight;
	};

	void Reset();

	/** A button: accept presses it. With no Adjust, left and right press it too if bCycles (a row that steps through choices). */
	int32 AddButton(const TSharedPtr<SButton>& Button, TFunction<void()> Activate, bool bCycles = false);

	/** A slider: left and right step it. */
	int32 AddSlider(const TSharedPtr<SSlider>& Slider, TFunction<void(int32)> Adjust);

	/** A row whose value is shown as text (a drop-down): left and right step through its choices. */
	int32 AddText(const TSharedPtr<STextBlock>& Text, TFunction<void(int32)> Adjust);

	int32 AddItem(FItem Item);

	int32 Num() const { return Items.Num(); }
	int32 GetSelected() const { return Selected; }

	/** Selects an item (clamped) and moves the highlight. */
	void Select(int32 Index);

	/** Moves the selection up (-1) or down (+1), wrapping round the ends. */
	void Move(int32 Delta);

	/** Presses the selected item. False if it has nothing to press. */
	bool Activate();

	/** Changes the selected item's value. False if it has none. */
	bool Adjust(int32 Direction);

	/** Routes a key press. True if it was a navigation key and was used. */
	bool HandleKey(const FKey& Key);

	/** What a key press did, for the menu to make a sound for. */
	enum class EAction : uint8 { Moved, Adjusted, Activated };

	/** Called by HandleKey when a key did something. */
	TFunction<void(EAction)> OnAction;

	/** The index of the item selected when the menu opens. */
	int32 DefaultIndex = 0;

private:
	TArray<FItem> Items;
	int32 Selected = INDEX_NONE;
};
