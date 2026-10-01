#include "WindComponent.h"
#include "UI/KiteSurfGameInstance.h"
#include "Engine/World.h"

UWindComponent::UWindComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Default base wind: 15 knots ≈ 772 cm/s in +X direction (1 knot = 51.44 cm/s)
	BaseWind = FVector(772.0f, 0.0f, 0.0f);
	GustStrength = 0.3f;
	GustPeriodSeconds = 8.0f;
	DirectionDriftDeg = 10.0f;
	ShearHeightCm = 1000.0f;
	TimeOverride = 0.0f;
}

void UWindComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		if (UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance()))
		{
			if (GI->PendingWindKnots > 0.0f)
			{
				const float SpeedCmPerSec = GI->PendingWindKnots * 51.44f;
				FVector Dir = BaseWind.GetSafeNormal();
				if (Dir.IsNearlyZero())
				{
					Dir = FVector(1.0f, 0.0f, 0.0f);
				}
				BaseWind = Dir * SpeedCmPerSec;
			}
		}
	}
}

FVector UWindComponent::GetWindAt(const FVector& WorldLocation) const
{
	const float BaseSpeed = BaseWind.Size();
	if (FMath::IsNearlyZero(BaseSpeed))
	{
		return FVector::ZeroVector;
	}

	const FVector BaseDir = BaseWind / BaseSpeed;

	// Time source: GetWorld()->GetTimeSeconds() when a world exists, else injectable TimeOverride for tests
	const float Time = (GetWorld() != nullptr) ? GetWorld()->GetTimeSeconds() : TimeOverride;
	const float Period = FMath::Max(GustPeriodSeconds, 0.001f);
	const float TimeCoord = Time / Period;

	// Deterministic Perlin noise over (time, location * small scale)
	constexpr float SpatialScale = 0.0001f;
	const FVector SpatialPos = WorldLocation * SpatialScale;

	// Gust noise in range [-1.0, 1.0]
	const float GustNoise = FMath::PerlinNoise3D(FVector(TimeCoord, SpatialPos.X, SpatialPos.Y));

	// Direction drift noise in range [-1.0, 1.0]
	const float DriftNoise = FMath::PerlinNoise3D(FVector(TimeCoord + 31.7f, SpatialPos.X + 17.3f, SpatialPos.Y + 53.1f));
	const float YawOffsetDeg = DriftNoise * DirectionDriftDeg;

	// Direction stays within ±DirectionDriftDeg around UpVector
	const FVector DriftedDir = BaseDir.RotateAngleAxis(YawOffsetDeg, FVector::UpVector);

	// Wind shear: wind at Z=0 is 70% of wind at ShearHeightCm, log profile clamp
	float ShearFactor = 1.0f;
	if (ShearHeightCm > 0.0f)
	{
		const float NormalizedZ = FMath::Clamp(WorldLocation.Z / ShearHeightCm, 0.0f, 1.0f);
		// Log profile from 0.70 at Z=0 to 1.00 at Z=ShearHeightCm
		ShearFactor = 0.7f + 0.3f * (FMath::Loge(1.0f + 9.0f * NormalizedZ) / FMath::Loge(10.0f));
	}

	const float GustMultiplier = FMath::Max(0.0f, 1.0f + GustStrength * GustNoise);
	const float CurrentSpeed = BaseSpeed * GustMultiplier * ShearFactor;

	return DriftedDir * CurrentSpeed;
}
