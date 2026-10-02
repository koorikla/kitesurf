#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "KiteSurfUnits.h"
#include "WindComponent.generated.h"

/**
 * The wind: a pure function of position, time and a seed, so everything that samples it (kite,
 * rider, HUD, audio, tests) agrees, and the same seed and parameters give the same wind.
 *
 * - The mean wind is BaseWind at ReferenceHeightCm (10 m, where forecasts quote it), scaled with
 *   height by a power law, (z / ReferenceHeightCm) ^ ShearExponent, never sampled below
 *   MinSampleHeightCm: about 0.81 at 1.5 m and 1.11 at 25 m.
 * - Gusts and lulls are two octaves of seeded gradient noise over the water, carried downwind
 *   at the mean speed (a gust seen upwind arrives later) and changing slowly as they go. Cells
 *   are GustCellLengthCm along the wind and half that across. The gust factor is normalised so
 *   the strongest gusts reach 1 + GustStrength and the deepest lulls 1 - GustStrength, and it
 *   never goes past either.
 * - The direction wanders about the mean with a standard deviation of DirectionDriftDeg, and
 *   never by more than twice that.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class KITESURF_API UWindComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWindComponent();

	/** The wind at a place now (world time, or TimeOverride without a world). */
	UFUNCTION(BlueprintCallable, Category = "Wind")
	FVector GetWindAt(const FVector& WorldLocation) const;

	/** The wind at a place and a time (cm/s). The simulation samples this at its own fixed-step time, so a ride is the same whatever the frame rate. */
	UFUNCTION(BlueprintCallable, Category = "Wind")
	FVector GetWindAtTime(const FVector& WorldLocation, float TimeSeconds) const;

	UFUNCTION(BlueprintCallable, Category = "Wind")
	FVector GetBaseWind() const { return BaseWind; }

	/** Current wind speed over the base wind speed at this place, at the reference height: above 1 in a gust, below 1 in a lull. */
	UFUNCTION(BlueprintCallable, Category = "Wind")
	float GetGustFactorAt(const FVector& WorldLocation) const;

	/** The gust factor at the reference height above this place, at this time. */
	UFUNCTION(BlueprintCallable, Category = "Wind")
	float GetGustFactorAtTime(const FVector& WorldLocation, float TimeSeconds) const;

	/** Mean wind at this height over the wind at ReferenceHeightCm: the power-law profile. */
	UFUNCTION(BlueprintPure, Category = "Wind")
	float GetProfileFactor(float HeightCm) const;

	/** Reference base wind speed at the reference height in knots. */
	UFUNCTION(BlueprintCallable, Category = "Wind")
	float GetReferenceWindSpeedKnots() const { return KiteUnits::CmSToKnots(BaseWind.Size()); }

	/** Mean wind at ReferenceHeightCm (cm/s). Its direction is the mean wind direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	FVector BaseWind;

	/** Height BaseWind is quoted at (cm): 10 m, as forecasts and anemometers do. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind|Profile", meta = (ClampMin = "1.0"))
	float ReferenceHeightCm;

	/** Power-law exponent of the wind profile over water (0.11 at sea in near-neutral air): wind at z is (z / ReferenceHeightCm) ^ ShearExponent of BaseWind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind|Profile", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ShearExponent;

	/** The profile is never sampled below this height (cm); the power law means nothing at the surface itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind|Profile", meta = (ClampMin = "1.0"))
	float MinSampleHeightCm;

	/** The strongest gusts reach 1 + GustStrength times the mean wind and the deepest lulls 1 - GustStrength (0..1, default 0.3). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind|Gusts", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GustStrength;

	/** Length of a gust cell along the wind (cm); across the wind it is half that. A cell passes a point in GustCellLengthCm / wind speed seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind|Gusts", meta = (ClampMin = "100.0"))
	float GustCellLengthCm;

	/** The quick puffs (second octave) are this many times smaller than the gust cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind|Gusts", meta = (ClampMin = "1.0"))
	float GustPuffRate;

	/** Share of the gust variation that comes from the quick puffs (0..1); the rest is the gust cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind|Gusts", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GustPuffShare;

	/** The gust pattern changes as it travels: after about this long (s) it is a different pattern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind|Gusts", meta = (ClampMin = "0.1"))
	float GustEvolveSeconds;

	/** Standard deviation of the wind direction about the mean (deg, default 5); it never strays more than twice this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind|Gusts", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float DirectionDriftDeg;

	/** The same seed and parameters give the same wind at every place and time; another seed gives other gusts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	int32 Seed;

	/** Injectable time override for automated tests when World is null */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	float TimeOverride;

private:
	/** World time, or TimeOverride without a world. */
	float GetCurrentTimeSeconds() const;

	/** Gust factor and direction offset (deg) of the field above a place at a time. Independent of height. */
	void SampleGusts(const FVector& WorldLocation, float TimeSeconds, float& OutGustFactor, float& OutYawOffsetDeg) const;
};
