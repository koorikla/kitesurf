#include "KiteSurfGameMode.h"
#include "KiteRiderPawn.h"

AKiteSurfGameMode::AKiteSurfGameMode()
{
	DefaultPawnClass = AKiteRiderPawn::StaticClass();
}
