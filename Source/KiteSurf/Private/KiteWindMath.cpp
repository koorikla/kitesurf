#include "KiteWindMath.h"
#include "KiteSurfUnits.h"

float UKiteWindMath::KnotsToCmPerSec(float Knots)
{
	return KiteUnits::KnotsToCmS(Knots);
}

FVector UKiteWindMath::ApparentWind(const FVector& TrueWind, const FVector& RiderVelocity)
{
	return TrueWind - RiderVelocity;
}

float UKiteWindMath::WindWindowAzimuthDeg(const FVector& DownwindDir, const FVector& KiteDir)
{
	FVector2D Downwind2D(DownwindDir.X, DownwindDir.Y);
	FVector2D Kite2D(KiteDir.X, KiteDir.Y);

	if (Downwind2D.IsNearlyZero() || Kite2D.IsNearlyZero())
	{
		return 0.0f;
	}

	Downwind2D.Normalize();
	Kite2D.Normalize();

	const float Dot = FMath::Clamp(FVector2D::DotProduct(Downwind2D, Kite2D), -1.0f, 1.0f);
	// 2D cross product Z component: Downwind2D.X * Kite2D.Y - Downwind2D.Y * Kite2D.X
	// Positive when Kite is to the right of Downwind (+Yaw in Unreal coordinates)
	const float CrossZ = Downwind2D.X * Kite2D.Y - Downwind2D.Y * Kite2D.X;
	const float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(CrossZ, Dot));

	return FMath::Clamp(AngleDeg, -90.0f, 90.0f);
}

FVector UKiteWindMath::KitePositionInWindow(const FVector& RiderPos, const FVector& DownwindDir, float AzimuthDeg, float ElevationDeg, float LineLengthCm)
{
	FVector2D Downwind2D(DownwindDir.X, DownwindDir.Y);
	if (Downwind2D.IsNearlyZero())
	{
		Downwind2D = FVector2D(1.0f, 0.0f);
	}
	else
	{
		Downwind2D.Normalize();
	}

	const float ClampedAzimuthDeg = FMath::Clamp(AzimuthDeg, -90.0f, 90.0f);
	const float ClampedElevationDeg = FMath::Clamp(ElevationDeg, 0.0f, 90.0f);

	// Rotate horizontal downwind vector around Z-axis by AzimuthDeg
	const FVector HorizontalDir = FVector(Downwind2D.X, Downwind2D.Y, 0.0f).RotateAngleAxis(ClampedAzimuthDeg, FVector::UpVector);

	const float ElevationRad = FMath::DegreesToRadians(ClampedElevationDeg);
	const float CosElev = FMath::Cos(ElevationRad);
	const float SinElev = FMath::Sin(ElevationRad);

	const FVector KiteUnitDir = HorizontalDir * CosElev + FVector::UpVector * SinElev;
	return RiderPos + KiteUnitDir * LineLengthCm;
}
