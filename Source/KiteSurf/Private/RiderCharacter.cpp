#include "RiderCharacter.h"

const TCHAR* RiderCharacter::GetDisplayName(ERiderCharacter Character)
{
	switch (Character)
	{
	case ERiderCharacter::Wetsuit:
		return TEXT("WETSUIT RIDER");
	case ERiderCharacter::Robot:
		return TEXT("ROBOT");
	default:
		return TEXT("SANTA");
	}
}

const TCHAR* RiderCharacter::GetDescription(ERiderCharacter Character)
{
	switch (Character)
	{
	case ERiderCharacter::Wetsuit:
		return TEXT("Full wetsuit and helmet: dressed for a cold, windy day.");
	case ERiderCharacter::Robot:
		return TEXT("The test pilot. Never tires, never complains.");
	default:
		return TEXT("Board shorts and the hat. Delivers by kite.");
	}
}

const TCHAR* RiderCharacter::GetStaticMeshPath(ERiderCharacter Character)
{
	switch (Character)
	{
	case ERiderCharacter::Wetsuit:
		return TEXT("/Game/Meshes/SM_RiderWetsuit");
	case ERiderCharacter::Robot:
		return nullptr;
	default:
		return TEXT("/Game/Meshes/SM_RiderSanta");
	}
}

ERiderCharacter RiderCharacter::Next(ERiderCharacter Character)
{
	return FromIndex((static_cast<int32>(Character) + 1) % static_cast<int32>(ERiderCharacter::Count));
}

ERiderCharacter RiderCharacter::FromIndex(int32 Index)
{
	return (Index >= 0 && Index < static_cast<int32>(ERiderCharacter::Count)) ? static_cast<ERiderCharacter>(Index) : ERiderCharacter::Santa;
}
