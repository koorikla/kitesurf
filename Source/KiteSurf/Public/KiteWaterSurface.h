#pragma once

#include "CoreMinimal.h"

class UWaterBodyComponent;

/**
 * Interface for querying water surface height and normal.
 * Decouples gameplay and physics from the Water plugin so tests can run without a water body actor.
 */
class KITESURF_API IKiteWaterSurface
{
public:
	virtual ~IKiteWaterSurface() = default;

	/** Returns the water surface height (Z in cm) at the given XY position */
	virtual float GetWaterHeight(const FVector2D& PositionXY) const = 0;

	/** Returns the water surface normal at the given XY position */
	virtual FVector GetWaterNormal(const FVector2D& PositionXY) const = 0;

	/** Sample both height and normal at the given XY position */
	virtual void SampleWaterSurface(const FVector2D& PositionXY, float& OutHeight, FVector& OutNormal) const
	{
		OutHeight = GetWaterHeight(PositionXY);
		OutNormal = GetWaterNormal(PositionXY);
	}

	/** Convenience overloads taking FVector */
	float GetWaterHeight(const FVector& Location) const
	{
		return GetWaterHeight(FVector2D(Location.X, Location.Y));
	}

	FVector GetWaterNormal(const FVector& Location) const
	{
		return GetWaterNormal(FVector2D(Location.X, Location.Y));
	}

	void SampleWaterSurface(const FVector& Location, float& OutHeight, FVector& OutNormal) const
	{
		SampleWaterSurface(FVector2D(Location.X, Location.Y), OutHeight, OutNormal);
	}
};

/**
 * Analytic flat water surface implementation (defaults to Z=0 and Normal=UpVector).
 * Used for tests and as a fallback when no water body is present in the world.
 */
class KITESURF_API FKiteFlatWaterSurface : public IKiteWaterSurface
{
public:
	FKiteFlatWaterSurface(float InRestZ = 0.0f)
		: RestZ(InRestZ)
	{
	}

	virtual float GetWaterHeight(const FVector2D& PositionXY) const override
	{
		return RestZ;
	}

	virtual FVector GetWaterNormal(const FVector2D& PositionXY) const override
	{
		return FVector::UpVector;
	}

	virtual void SampleWaterSurface(const FVector2D& PositionXY, float& OutHeight, FVector& OutNormal) const override
	{
		OutHeight = RestZ;
		OutNormal = FVector::UpVector;
	}

private:
	float RestZ;
};

/**
 * Analytic sinusoidal wave water surface for deterministic physics testing and simulated waves.
 */
class KITESURF_API FKiteWaveWaterSurface : public IKiteWaterSurface
{
public:
	FKiteWaveWaterSurface(float InRestZ = 0.0f, float InAmplitude = 50.0f, float InWavelength = 1000.0f, FVector2D InDirection = FVector2D(1.0f, 0.0f))
		: RestZ(InRestZ), Amplitude(InAmplitude), Wavelength(InWavelength), Direction(InDirection.GetSafeNormal())
	{
	}

	virtual void SampleWaterSurface(const FVector2D& PositionXY, float& OutHeight, FVector& OutNormal) const override
	{
		const float K = (Wavelength > 0.0f) ? (2.0f * PI / Wavelength) : 0.0f;
		const float Phase = K * FVector2D::DotProduct(PositionXY, Direction);
		OutHeight = RestZ + Amplitude * FMath::Sin(Phase);
		const float Slope = Amplitude * K * FMath::Cos(Phase);
		OutNormal = FVector(-Slope * Direction.X, -Slope * Direction.Y, 1.0f).GetSafeNormal();
	}

	virtual float GetWaterHeight(const FVector2D& PositionXY) const override
	{
		float H = 0.0f;
		FVector N = FVector::UpVector;
		SampleWaterSurface(PositionXY, H, N);
		return H;
	}

	virtual FVector GetWaterNormal(const FVector2D& PositionXY) const override
	{
		float H = 0.0f;
		FVector N = FVector::UpVector;
		SampleWaterSurface(PositionXY, H, N);
		return N;
	}

private:
	float RestZ;
	float Amplitude;
	float Wavelength;
	FVector2D Direction;
};

/**
 * Water plugin implementation querying UWaterBodyComponent with IncludeWaves.
 */
class KITESURF_API FKiteWaterBodySurface : public IKiteWaterSurface
{
public:
	FKiteWaterBodySurface(const UWaterBodyComponent* InWaterBodyComponent = nullptr);

	void SetWaterBodyComponent(const UWaterBodyComponent* InWaterBodyComponent);
	const UWaterBodyComponent* GetWaterBodyComponent() const;

	virtual float GetWaterHeight(const FVector2D& PositionXY) const override;
	virtual FVector GetWaterNormal(const FVector2D& PositionXY) const override;
	virtual void SampleWaterSurface(const FVector2D& PositionXY, float& OutHeight, FVector& OutNormal) const override;

private:
	TWeakObjectPtr<const UWaterBodyComponent> WaterBodyComponent;
};
