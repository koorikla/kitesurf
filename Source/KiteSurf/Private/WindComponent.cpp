#include "WindComponent.h"
#include "Engine/World.h"
#include "KiteSurfUnits.h"

namespace
{
	// FMath::PerlinNoise3D repeats every 256 units on each axis; seed offsets are drawn from that range.
	constexpr float PerlinPeriod = 256.0f;

	// Standard deviation of FMath::PerlinNoise3D over its whole domain, measured over 10^6 random
	// points (0.263; its range is +-0.96). The gust and direction noises are scaled by it, so their
	// spread is what the parameters say rather than whatever Perlin's range happens to give.
	constexpr float PerlinStdDev = 0.263f;

	// 99th percentile of the two-octave gust noise over its standard deviation, measured the same
	// way: 2.17 for one octave, 2.25 at the default puff share of 0.4, 2.26 at 0.5. Scaled by this,
	// the gust factor's 99th percentile is 1 + GustStrength.
	constexpr float GustP99OverStdDev = 2.25f;

	// Across the wind a gust cell is this fraction of its length along it.
	constexpr float GustCellAcrossFraction = 0.5f;

	// The direction wanders by up to this many standard deviations.
	constexpr float DirectionDriftClampStdDevs = 2.0f;

	/** Murmur3's finaliser: a well-mixed 32-bit hash of a 32-bit value, the same on every platform. */
	uint32 MixBits(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x85ebca6bu;
		Value ^= Value >> 13;
		Value *= 0xc2b2ae35u;
		Value ^= Value >> 16;
		return Value;
	}

	/** A point in Perlin space picked by the seed and a salt, so each noise of each seed reads its own part of the noise. */
	FVector SeedOffset(int32 Seed, uint32 Salt)
	{
		const uint32 Base = MixBits(static_cast<uint32>(Seed) * 0x9e3779b9u + Salt);
		auto Coordinate = [Base](uint32 Axis)
		{
			const uint32 Bits = MixBits(Base + Axis * 0x632be5abu);
			return static_cast<float>(Bits & 0xffffffu) / static_cast<float>(0x1000000u) * PerlinPeriod;
		};
		return FVector(Coordinate(1u), Coordinate(2u), Coordinate(3u));
	}

	float Noise(const FVector& Offset, float X, float Y, float Z)
	{
		return FMath::PerlinNoise3D(FVector(Offset.X + X, Offset.Y + Y, Offset.Z + Z));
	}
}

UWindComponent::UWindComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Default base wind: 15 knots along +X
	BaseWind = FVector(KiteUnits::KnotsToCmS(15.0f), 0.0f, 0.0f);
	ReferenceHeightCm = 1000.0f;
	ShearExponent = 0.11f;
	MinSampleHeightCm = 100.0f;
	GustStrength = 0.3f;
	GustCellLengthCm = 6000.0f;
	GustPuffRate = 3.5f;
	GustPuffShare = 0.4f;
	GustEvolveSeconds = 180.0f;
	DirectionDriftDeg = 5.0f;
	Seed = 1;
	TimeOverride = 0.0f;
}

float UWindComponent::GetCurrentTimeSeconds() const
{
	// World time when there is a world, else the injectable TimeOverride for tests.
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : TimeOverride;
}

FVector UWindComponent::GetWindAt(const FVector& WorldLocation) const
{
	return GetWindAtTime(WorldLocation, GetCurrentTimeSeconds());
}

float UWindComponent::GetProfileFactor(float HeightCm) const
{
	if (ReferenceHeightCm <= 0.0f)
	{
		return 1.0f;
	}
	const float SampleHeightCm = FMath::Max(HeightCm, FMath::Max(MinSampleHeightCm, 1.0f));
	return FMath::Pow(SampleHeightCm / ReferenceHeightCm, ShearExponent);
}

void UWindComponent::SampleGusts(const FVector& WorldLocation, float TimeSeconds, float& OutGustFactor, float& OutYawOffsetDeg) const
{
	OutGustFactor = 1.0f;
	OutYawOffsetDeg = 0.0f;

	const FVector2D MeanWind(BaseWind.X, BaseWind.Y);
	FVector2D Downwind = MeanWind.GetSafeNormal();
	if (Downwind.IsNearlyZero())
	{
		Downwind = FVector2D(1.0f, 0.0f);
	}
	const FVector2D Crosswind(-Downwind.Y, Downwind.X);

	// The pattern is carried downwind at the mean wind speed (Taylor's frozen turbulence): what is at
	// a place now was MeanWind * t upwind of it at time zero. Measured in cells along and across.
	const FVector2D Carried = FVector2D(WorldLocation.X, WorldLocation.Y) - MeanWind * TimeSeconds;
	const float CellLengthCm = FMath::Max(GustCellLengthCm, 1.0f);
	const float Along = FVector2D::DotProduct(Carried, Downwind) / CellLengthCm;
	const float Across = FVector2D::DotProduct(Carried, Crosswind) / (CellLengthCm * GustCellAcrossFraction);
	// A slow third axis, so the pattern changes as it travels instead of repeating.
	const float Evolve = TimeSeconds / FMath::Max(GustEvolveSeconds, 0.1f);

	if (GustStrength > 0.0f)
	{
		const float PuffShare = FMath::Clamp(GustPuffShare, 0.0f, 1.0f);
		const float Puff = FMath::Max(GustPuffRate, 1.0f);
		const float Cells = Noise(SeedOffset(Seed, 1u), Along, Across, Evolve);
		const float Puffs = Noise(SeedOffset(Seed, 2u), Along * Puff, Across * Puff, Evolve);
		const float GustNoise = (1.0f - PuffShare) * Cells + PuffShare * Puffs;

		// Two independent octaves: the spread of the sum is the root of the summed squares.
		const float GustNoiseStdDev = PerlinStdDev * FMath::Sqrt(FMath::Square(1.0f - PuffShare) + FMath::Square(PuffShare));
		const float Normalised = FMath::Clamp(GustNoise / (GustP99OverStdDev * GustNoiseStdDev), -1.0f, 1.0f);
		OutGustFactor = 1.0f + GustStrength * Normalised;
	}

	if (DirectionDriftDeg > 0.0f)
	{
		const float DirectionNoise = Noise(SeedOffset(Seed, 3u), Along, Across, Evolve) / PerlinStdDev;
		OutYawOffsetDeg = FMath::Clamp(DirectionNoise, -DirectionDriftClampStdDevs, DirectionDriftClampStdDevs) * DirectionDriftDeg;
	}
}

FVector UWindComponent::GetWindAtTime(const FVector& WorldLocation, float TimeSeconds) const
{
	const float BaseSpeed = BaseWind.Size();
	if (FMath::IsNearlyZero(BaseSpeed))
	{
		return FVector::ZeroVector;
	}

	float GustFactor = 1.0f;
	float YawOffsetDeg = 0.0f;
	SampleGusts(WorldLocation, TimeSeconds, GustFactor, YawOffsetDeg);

	const FVector Direction = (BaseWind / BaseSpeed).RotateAngleAxis(YawOffsetDeg, FVector::UpVector);
	return Direction * (BaseSpeed * GustFactor * GetProfileFactor(WorldLocation.Z));
}

float UWindComponent::GetGustFactorAtTime(const FVector& WorldLocation, float TimeSeconds) const
{
	if (BaseWind.IsNearlyZero())
	{
		return 1.0f;
	}
	// At the reference height the profile is 1, so this is the wind there over the base wind.
	float GustFactor = 1.0f;
	float YawOffsetDeg = 0.0f;
	SampleGusts(WorldLocation, TimeSeconds, GustFactor, YawOffsetDeg);
	return GustFactor;
}

float UWindComponent::GetGustFactorAt(const FVector& WorldLocation) const
{
	return GetGustFactorAtTime(WorldLocation, GetCurrentTimeSeconds());
}
