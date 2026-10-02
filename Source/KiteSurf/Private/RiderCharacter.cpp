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

ERiderCharacter RiderCharacter::Next(ERiderCharacter Character)
{
	return FromIndex((static_cast<int32>(Character) + 1) % static_cast<int32>(ERiderCharacter::Count));
}

ERiderCharacter RiderCharacter::FromIndex(int32 Index)
{
	return (Index >= 0 && Index < static_cast<int32>(ERiderCharacter::Count)) ? static_cast<ERiderCharacter>(Index) : ERiderCharacter::Santa;
}
