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

	/** One line about the rider, shown under the gear preview. */
	KITESURF_API const TCHAR* GetDescription(ERiderCharacter Character);

	/**
	 * The posed static mesh this rider is drawn with, or null for a rider drawn with the animated
	 * mannequin (the robot). Used by the pawn and the gear preview alike, so a new rider is a new
	 * enum value and a mesh here.
	 */
	KITESURF_API const TCHAR* GetStaticMeshPath(ERiderCharacter Character);

	/** The animated mannequin and its idle, for riders without a static mesh. */
	static constexpr const TCHAR* MannequinMeshPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple");
	static constexpr const TCHAR* MannequinIdlePath = TEXT("/Game/Characters/Mannequins/Anims/MM_Idle");

	/** The next character round the list, for a button that cycles through them. */
	KITESURF_API ERiderCharacter Next(ERiderCharacter Character);

	/** A stored index back to a valid character; anything out of range is Santa. */
	KITESURF_API ERiderCharacter FromIndex(int32 Index);
}
