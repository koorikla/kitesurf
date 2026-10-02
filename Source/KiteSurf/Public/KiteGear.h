#pragma once

#include "CoreMinimal.h"
#include "KiteGear.generated.h"

/** The kind of kite. Chosen on the gear screen, stored in the save game, applied by UKiteComponent::SetKiteModel. */
UENUM(BlueprintType)
enum class EKiteModel : uint8
{
	/** Three struts: light, pivots fast, climbs back quickly after a loop. */
	Loop  UMETA(DisplayName = "Loop (3 strut)"),
	/** Five struts: more lift and glide for height and hangtime, slower to turn. */
	Boost UMETA(DisplayName = "Boost (5 strut)"),
	Count UMETA(Hidden)
};

/** The size of the twin-tip. Chosen on the gear screen, applied by UBoardMovementComponent::SetBoardSize. */
UENUM(BlueprintType)
enum class EBoardSize : uint8
{
	/** Poppy and quick to turn, but needs more speed to plane and sinks sooner in light wind. */
	Small  UMETA(DisplayName = "132 cm"),
	Medium UMETA(DisplayName = "138 cm"),
	/** Planes early and holds an edge, with less pop and slower turns. */
	Large  UMETA(DisplayName = "145 cm"),
	Count  UMETA(Hidden)
};

/** How a kite model differs from the reference kite the simulation is tuned for (the loop kite: all 1). */
struct FKiteModelTraits
{
	float TurnRadiusScale = 1.0f;
	float LiftScale = 1.0f;
	float InducedDragScale = 1.0f;
	float MassScale = 1.0f;
};

/** How a board size differs from the reference board (the 138: all 1). */
struct FBoardSizeTraits
{
	float PopScale = 1.0f;
	float PlaningSpeedScale = 1.0f;
	float PlaningDragScale = 1.0f;
	float GripScale = 1.0f;
	float TurnRateScale = 1.0f;
};

namespace KiteGear
{
	KITESURF_API const TCHAR* GetDisplayName(EKiteModel Model);
	KITESURF_API const TCHAR* GetDescription(EKiteModel Model);
	KITESURF_API EKiteModel Next(EKiteModel Model);
	/** A stored index back to a valid model; anything out of range is the loop kite. */
	KITESURF_API EKiteModel KiteModelFromIndex(int32 Index);
	KITESURF_API FKiteModelTraits GetTraits(EKiteModel Model);

	KITESURF_API const TCHAR* GetDisplayName(EBoardSize Size);
	KITESURF_API const TCHAR* GetDescription(EBoardSize Size);
	KITESURF_API EBoardSize Next(EBoardSize Size);
	/** A stored index back to a valid size; anything out of range is the 138. */
	KITESURF_API EBoardSize BoardSizeFromIndex(int32 Index);
	KITESURF_API FBoardSizeTraits GetTraits(EBoardSize Size);
}
