#pragma once

#include "CoreMinimal.h"

class SWidget;

/** One row of the controls legend shown in the main menu and the pause menu. */
struct FKiteSurfControlBinding
{
	const TCHAR* Action;
	const TCHAR* Keyboard;
	const TCHAR* Gamepad;
};

namespace KiteSurfControlsLegend
{
	/** The bindings as the player sees them. Keep in step with scripts/editor/make_input_assets.py. */
	KITESURF_API TConstArrayView<FKiteSurfControlBinding> GetBindings();

	/** A three-column table (action, keyboard and mouse, gamepad) with a heading. */
	KITESURF_API TSharedRef<SWidget> Build();
}
