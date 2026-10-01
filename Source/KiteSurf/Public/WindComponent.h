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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	FVector BaseWind;

	/** Gust variation strength as a fraction of base speed (0..1, default 0.3) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GustStrength;

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
};
