#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if PLATFORM_LINUX
#include <SDL3/SDL.h>
#endif

#if WITH_DEV_AUTOMATION_TESTS

// The module turns on SDL's background gamepad events at start-up (KiteSurf.cpp), so a controller
// keeps driving the game when the window loses focus. This reads the hint back from the SDL the
// engine runs; it cannot press a real controller, so whether the pad works unfocused is a check for
// a person holding one.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputGamepadWithoutFocus, "KiteSurf.Input.GamepadWorksWithoutFocus", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfInputGamepadWithoutFocus::RunTest(const FString& Parameters)
{
#if PLATFORM_LINUX
	const char* EnvValue = SDL_getenv("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS");
	if (EnvValue != nullptr)
	{
		AddInfo(FString::Printf(TEXT("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=%s is set in the environment and takes precedence."), UTF8_TO_TCHAR(EnvValue)));
		return true;
	}
	TestTrue(TEXT("SDL delivers gamepad events while no game window has focus"), SDL_GetHintBoolean(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, false));
#else
	AddInfo(TEXT("Only Linux reads gamepads through SDL."));
#endif
	return true;
}

#endif
