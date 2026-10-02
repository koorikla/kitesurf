#include "UI/KiteSurfMenuNavigator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FLinearColor SelectedColor(1.0f, 0.8f, 0.15f);

	bool IsUp(const FKey& Key)
	{
		return Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up;
	}

	bool IsDown(const FKey& Key)
	{
		return Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down;
	}

	bool IsLeft(const FKey& Key)
	{
		return Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left;
	}

	bool IsRight(const FKey& Key)
	{
		return Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right;
	}

	bool IsAccept(const FKey& Key)
	{
		return Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom;
	}
}

void FKiteMenuNavigator::Reset()
{
	Items.Reset();
	Selected = INDEX_NONE;
}

int32 FKiteMenuNavigator::AddItem(FItem Item)
{
	return Items.Add(MoveTemp(Item));
}

int32 FKiteMenuNavigator::AddButton(const TSharedPtr<SButton>& Button, TFunction<void()> Activate, bool bCycles)
{
	FItem Item;
	if (bCycles)
	{
		Item.Adjust = [Activate](int32) { Activate(); };
	}
	Item.Activate = MoveTemp(Activate);
	const TWeakPtr<SButton> WeakButton = Button;
	Item.Highlight = [WeakButton](bool bSelected)
	{
		if (const TSharedPtr<SButton> Pinned = WeakButton.Pin())
		{
			Pinned->SetBorderBackgroundColor(bSelected ? SelectedColor : FLinearColor::White);
		}
	};
	return AddItem(MoveTemp(Item));
}

int32 FKiteMenuNavigator::AddSlider(const TSharedPtr<SSlider>& Slider, TFunction<void(int32)> Adjust)
{
	FItem Item;
	Item.Adjust = MoveTemp(Adjust);
	const TWeakPtr<SSlider> WeakSlider = Slider;
	Item.Highlight = [WeakSlider](bool bSelected)
	{
		if (const TSharedPtr<SSlider> Pinned = WeakSlider.Pin())
		{
			Pinned->SetSliderHandleColor(bSelected ? SelectedColor : FLinearColor::White);
			Pinned->SetSliderBarColor(bSelected ? SelectedColor : FLinearColor::White);
		}
	};
	return AddItem(MoveTemp(Item));
}

int32 FKiteMenuNavigator::AddText(const TSharedPtr<STextBlock>& Text, TFunction<void(int32)> Adjust)
{
	FItem Item;
	Item.Adjust = MoveTemp(Adjust);
	const TWeakPtr<STextBlock> WeakText = Text;
	Item.Highlight = [WeakText](bool bSelected)
	{
		if (const TSharedPtr<STextBlock> Pinned = WeakText.Pin())
		{
			Pinned->SetColorAndOpacity(bSelected ? SelectedColor : FLinearColor::White);
		}
	};
	return AddItem(MoveTemp(Item));
}

void FKiteMenuNavigator::Select(int32 Index)
{
	if (Items.Num() == 0)
	{
		Selected = INDEX_NONE;
		return;
	}
	const int32 NewSelected = FMath::Clamp(Index, 0, Items.Num() - 1);
	if (Items.IsValidIndex(Selected) && Items[Selected].Highlight)
	{
		Items[Selected].Highlight(false);
	}
	Selected = NewSelected;
	if (Items[Selected].Highlight)
	{
		Items[Selected].Highlight(true);
	}
}

void FKiteMenuNavigator::Move(int32 Delta)
{
	if (Items.Num() == 0)
	{
		return;
	}
	if (Selected == INDEX_NONE)
	{
		Select(DefaultIndex);
		return;
	}
	Select(((Selected + Delta) % Items.Num() + Items.Num()) % Items.Num());
}

bool FKiteMenuNavigator::Activate()
{
	if (!Items.IsValidIndex(Selected) || !Items[Selected].Activate)
	{
		return false;
	}
	// Copied first: pressing it may close the menu and take this navigator with it.
	const TFunction<void()> Press = Items[Selected].Activate;
	Press();
	return true;
}

bool FKiteMenuNavigator::Adjust(int32 Direction)
{
	if (!Items.IsValidIndex(Selected) || !Items[Selected].Adjust)
	{
		return false;
	}
	const TFunction<void(int32)> Change = Items[Selected].Adjust;
	Change(Direction < 0 ? -1 : 1);
	return true;
}

bool FKiteMenuNavigator::HandleKey(const FKey& Key)
{
	// The callback is copied first: pressing an item may close the menu and take this navigator with it.
	const TFunction<void(EAction)> Notify = OnAction;
	auto Did = [&Notify](EAction Action)
	{
		if (Notify)
		{
			Notify(Action);
		}
	};
	if (IsUp(Key) || IsDown(Key))
	{
		Did(EAction::Moved);
		Move(IsUp(Key) ? -1 : 1);
		return true;
	}
	if (IsLeft(Key) || IsRight(Key))
	{
		if (Items.IsValidIndex(Selected) && Items[Selected].Adjust)
		{
			Did(EAction::Adjusted);
		}
		Adjust(IsLeft(Key) ? -1 : 1);
		return true;
	}
	if (IsAccept(Key))
	{
		if (Items.IsValidIndex(Selected) && Items[Selected].Activate)
		{
			Did(EAction::Activated);
		}
		Activate();
		return true;
	}
	return false;
}
