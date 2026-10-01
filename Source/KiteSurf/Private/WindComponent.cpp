#include "WindComponent.h"

UWindComponent::UWindComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Default base wind: 15 knots ≈ 772 cm/s in +X direction (1 knot = 51.44 cm/s)
	BaseWind = FVector(772.0f, 0.0f, 0.0f);
}

FVector UWindComponent::GetWindAt(const FVector& WorldLocation) const
{
	return BaseWind;
}
