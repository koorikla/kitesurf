#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WindComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class KITESURF_API UWindComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWindComponent();

	UFUNCTION(BlueprintCallable, Category = "Wind")
	FVector GetWindAt(const FVector& WorldLocation) const;

	UFUNCTION(BlueprintCallable, Category = "Wind")
	FVector GetBaseWind() const { return BaseWind; }

	/** Current wind speed over the base wind speed at this place: above 1 in a gust, below 1 in a lull. */
	UFUNCTION(BlueprintCallable, Category = "Wind")
	float GetGustFactorAt(const FVector& WorldLocation) const;

	/** Reference base wind speed at 10 m elevation in knots (un-sheared baseline, 1 kn = 51.44 cm/s) */
	UFUNCTION(BlueprintCallable, Category = "Wind")
	float GetReferenceWindSpeedKnots() const { return BaseWind.Size() / 51.44f; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	FVector BaseWind;

	/** Gust variation strength as a fraction of base speed (0..1, default 0.3) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GustStrength;

	/** Quick puffs run this many times faster than the gust period. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "1.0"))
	float GustPuffRate;

	/** Share of the gust variation that comes from the quick puffs (0..1); the rest is the slow swell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GustPuffShare;

	/** Perlin noise rarely gets near +-1; this stretches it so gusts and lulls reach most of GustStrength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0.0"))
	float GustNoiseGain;

	/** Gust period in seconds (default 8) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0.1"))
	float GustPeriodSeconds;

	/** Maximum wind direction drift in degrees (default 10) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float DirectionDriftDeg;

	/** Height in cm where wind reaches full speed (default 1000, wind at Z=0 is 70% of wind at ShearHeightCm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "1.0"))
	float ShearHeightCm;

	/** Injectable time override for automated tests when World is null */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	float TimeOverride;

protected:
	virtual void BeginPlay() override;
};
