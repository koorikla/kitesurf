#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "KiteWindMath.generated.h"

UCLASS()
class KITESURF_API UKiteWindMath : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Convert wind speed from knots to cm/s (1 knot = 51.44 cm/s) */
	UFUNCTION(BlueprintPure, Category = "Kite|Wind")
	static float KnotsToCmPerSec(float Knots);

	/** Calculate apparent wind experienced by rider: TrueWind - RiderVelocity */
	UFUNCTION(BlueprintPure, Category = "Kite|Wind")
	static FVector ApparentWind(const FVector& TrueWind, const FVector& RiderVelocity);

	/** Calculate kite azimuth angle relative to downwind direction in degrees (-90..90) */
	UFUNCTION(BlueprintPure, Category = "Kite|Wind")
	static float WindWindowAzimuthDeg(const FVector& DownwindDir, const FVector& KiteDir);

	/** Calculate 3D kite position in the wind window given rider position and window spherical coordinates */
	UFUNCTION(BlueprintPure, Category = "Kite|Wind")
	static FVector KitePositionInWindow(const FVector& RiderPos, const FVector& DownwindDir, float AzimuthDeg, float ElevationDeg, float LineLengthCm);
};

using KiteWindMath = UKiteWindMath;
