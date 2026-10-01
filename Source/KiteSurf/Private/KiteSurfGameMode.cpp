#include "KiteSurfGameMode.h"
#include "KiteRiderPawn.h"
#include "KiteSurfHUD.h"

AKiteSurfGameMode::AKiteSurfGameMode()
{
	DefaultPawnClass = AKiteRiderPawn::StaticClass();
	HUDClass = AKiteSurfHUD::StaticClass();
}
