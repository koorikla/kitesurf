#include "KiteWaterSurface.h"
#include "WaterBodyComponent.h"
#include "WaterBodyTypes.h"

FKiteWaterBodySurface::FKiteWaterBodySurface(const UWaterBodyComponent* InWaterBodyComponent)
	: WaterBodyComponent(InWaterBodyComponent)
{
}

void FKiteWaterBodySurface::SetWaterBodyComponent(const UWaterBodyComponent* InWaterBodyComponent)
{
	WaterBodyComponent = InWaterBodyComponent;
}

const UWaterBodyComponent* FKiteWaterBodySurface::GetWaterBodyComponent() const
{
	return WaterBodyComponent.Get();
}

void FKiteWaterBodySurface::SampleWaterSurface(const FVector2D& PositionXY, float& OutHeight, FVector& OutNormal) const
{
	OutHeight = 0.0f;
	OutNormal = FVector::UpVector;

	if (WaterBodyComponent.IsValid())
	{
		const EWaterBodyQueryFlags Flags =
			EWaterBodyQueryFlags::ComputeLocation |
			EWaterBodyQueryFlags::ComputeNormal |
			EWaterBodyQueryFlags::ComputeVelocity |
			EWaterBodyQueryFlags::IncludeWaves;

		const FVector QueryLocation(PositionXY.X, PositionXY.Y, 0.0f);
		const auto QueryResult = WaterBodyComponent->TryQueryWaterInfoClosestToWorldLocation(QueryLocation, Flags);
		if (QueryResult.HasValue() && !QueryResult.GetValue().IsInExclusionVolume())
		{
			const FWaterBodyQueryResult& Info = QueryResult.GetValue();
			OutHeight = Info.GetWaterSurfaceLocation().Z;
			OutNormal = Info.GetWaterSurfaceNormal();
			return;
		}
	}
}

float FKiteWaterBodySurface::GetWaterHeight(const FVector2D& PositionXY) const
{
	float Height = 0.0f;
	FVector Normal = FVector::UpVector;
	SampleWaterSurface(PositionXY, Height, Normal);
	return Height;
}

FVector FKiteWaterBodySurface::GetWaterNormal(const FVector2D& PositionXY) const
{
	float Height = 0.0f;
	FVector Normal = FVector::UpVector;
	SampleWaterSurface(PositionXY, Height, Normal);
	return Normal;
}
