#pragma once

#include "CoreMinimal.h"
#include "RiderCharacter.generated.h"

/** Who is on the board. Chosen in settings, stored in the save game, applied by AKiteRiderPawn. */
UENUM(BlueprintType)
enum class ERiderCharacter : uint8
{
	Santa   UMETA(DisplayName = "Santa"),
	Wetsuit UMETA(DisplayName = "Wetsuit"),
	Robot   UMETA(DisplayName = "Robot"),
	Count   UMETA(Hidden)
};

namespace RiderCharacter
{
	/** Name shown in the settings screen. */
	KITESURF_API const TCHAR* GetDisplayName(ERiderCharacter Character);

	/** The next character round the list, for a button that cycles through them. */
	KITESURF_API ERiderCharacter Next(ERiderCharacter Character);

	/** A stored index back to a valid character; anything out of range is Santa. */
	KITESURF_API ERiderCharacter FromIndex(int32 Index);
}
