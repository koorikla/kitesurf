#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SWidget.h"

class UTexture2D;

/** Shared look of the full-screen menus: the key art behind a dark panel that holds the content. */
namespace KiteSurfMenuStyle
{
	/** The key art drawn by scripts/editor/make_splash.sh, or null if it has not been imported. */
	KITESURF_API UTexture2D* LoadBackgroundTexture();

	/** Points a brush at the background texture. The caller keeps both alive. */
	KITESURF_API void SetupBackgroundBrush(FSlateBrush& Brush, UTexture2D* Texture);

	/**
	 * The background filling the screen with Content laid over it. Without a texture the
	 * background is a plain dark blue.
	 */
	KITESURF_API TSharedRef<SWidget> BuildBackdrop(const FSlateBrush* BackgroundBrush, bool bHasTexture, const TSharedRef<SWidget>& Content);

	/** A dark, slightly see-through panel for menu content to sit on over the background. */
	KITESURF_API TSharedRef<SWidget> BuildPanel(const TSharedRef<SWidget>& Content);
}
