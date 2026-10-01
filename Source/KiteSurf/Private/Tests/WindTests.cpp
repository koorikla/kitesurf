#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "WindComponent.h"
#include "KiteWindMath.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWindMathKnots, "KiteSurf.Wind.KnotsToCmPerSec", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWindMathKnots::RunTest(const FString& Parameters)
{
	// 1 knot = 51.44 cm/s
	TestNearlyEqual(TEXT("1 knot = 51.44 cm/s"), UKiteWindMath::KnotsToCmPerSec(1.0f), 51.44f, 0.001f);
	TestNearlyEqual(TEXT("15 knots = 771.6 cm/s"), UKiteWindMath::KnotsToCmPerSec(15.0f), 771.6f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWindMathApparent, "KiteSurf.Wind.ApparentWind", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWindMathApparent::RunTest(const FString& Parameters)
{
	// ApparentWind(true=(1000,0,0), rider=(500,0,0)) = (500,0,0)
	const FVector TrueWind(1000.0, 0.0, 0.0);
	const FVector RiderVelocity(500.0, 0.0, 0.0);
	const FVector Apparent = UKiteWindMath::ApparentWind(TrueWind, RiderVelocity);
	TestEqual(TEXT("ApparentWind(true=(1000,0,0), rider=(500,0,0)) = (500,0,0)"), Apparent, FVector(500.0, 0.0, 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWindMathAzimuth, "KiteSurf.Wind.WindowAzimuth", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWindMathAzimuth::RunTest(const FString& Parameters)
{
	const FVector Downwind(1.0, 0.0, 0.0);
	const FVector RightEdge(0.0, 1.0, 0.0);
	const FVector LeftEdge(0.0, -1.0, 0.0);

	TestNearlyEqual(TEXT("Azimuth at center is 0"), UKiteWindMath::WindWindowAzimuthDeg(Downwind, Downwind), 0.0f, 0.1f);
	TestNearlyEqual(TEXT("Azimuth at right edge is 90"), UKiteWindMath::WindWindowAzimuthDeg(Downwind, RightEdge), 90.0f, 0.1f);
	TestNearlyEqual(TEXT("Azimuth at left edge is -90"), UKiteWindMath::WindWindowAzimuthDeg(Downwind, LeftEdge), -90.0f, 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWindMathPosition, "KiteSurf.Wind.WindowPosition", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWindMathPosition::RunTest(const FString& Parameters)
{
	const FVector RiderPos(0.0, 0.0, 0.0);
	const FVector Downwind(1.0, 0.0, 0.0);
	const float LineLength = 2000.0f; // 20m lines

	// Zenith: elevation 90 -> position is (0, 0, LineLength)
	const FVector ZenithPos = UKiteWindMath::KitePositionInWindow(RiderPos, Downwind, 0.0f, 90.0f, LineLength);
	TestNearlyEqual(TEXT("Zenith X is 0"), ZenithPos.X, 0.0, 0.1);
	TestNearlyEqual(TEXT("Zenith Y is 0"), ZenithPos.Y, 0.0, 0.1);
	TestNearlyEqual(TEXT("Zenith Z is LineLength"), ZenithPos.Z, static_cast<double>(LineLength), 0.1);

	// Edge of window on surface: azimuth 90, elevation 0 -> (0, LineLength, 0)
	const FVector RightPos = UKiteWindMath::KitePositionInWindow(RiderPos, Downwind, 90.0f, 0.0f, LineLength);
	TestNearlyEqual(TEXT("Right edge X is 0"), RightPos.X, 0.0, 0.1);
	TestNearlyEqual(TEXT("Right edge Y is LineLength"), RightPos.Y, static_cast<double>(LineLength), 0.1);
	TestNearlyEqual(TEXT("Right edge Z is 0"), RightPos.Z, 0.0, 0.1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWindShear, "KiteSurf.Wind.Shear", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWindShear::RunTest(const FString& Parameters)
{
	UWindComponent* WindComp = NewObject<UWindComponent>();
	TestNotNull(TEXT("WindComponent created"), WindComp);
	if (!WindComp)
	{
		return false;
	}

	// Disable gust and direction drift to isolate shear
	WindComp->GustStrength = 0.0f;
	WindComp->DirectionDriftDeg = 0.0f;
	WindComp->ShearHeightCm = 1000.0f;
	WindComp->BaseWind = FVector(1000.0, 0.0, 0.0);

	const FVector WindAtWater = WindComp->GetWindAt(FVector(0.0, 0.0, 0.0));
	const FVector WindAtShear = WindComp->GetWindAt(FVector(0.0, 0.0, 1000.0));

	TestTrue(TEXT("Wind at Z=0 is lower than at ShearHeightCm"), WindAtWater.Size() < WindAtShear.Size());
	TestNearlyEqual(TEXT("Wind at Z=0 is ~70% of wind at ShearHeightCm"), WindAtWater.Size() / WindAtShear.Size(), 0.70, 0.01);
	TestNearlyEqual(TEXT("Wind at ShearHeightCm is full BaseWind"), WindAtShear.Size(), 1000.0, 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWindDirectionDrift, "KiteSurf.Wind.DirectionDrift", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWindDirectionDrift::RunTest(const FString& Parameters)
{
	UWindComponent* WindComp = NewObject<UWindComponent>();
	TestNotNull(TEXT("WindComponent created"), WindComp);
	if (!WindComp)
	{
		return false;
	}

	WindComp->BaseWind = FVector(1000.0, 0.0, 0.0);
	WindComp->DirectionDriftDeg = 10.0f;
	const FVector BaseDir(1.0, 0.0, 0.0);

	// Test across various times and positions that wind direction stays within DirectionDriftDeg
	for (int32 i = 0; i < 50; ++i)
	{
		WindComp->TimeOverride = static_cast<float>(i) * 0.5f;
		const FVector Pos(static_cast<double>(i) * 100.0, static_cast<double>(i) * 50.0, 500.0);
		const FVector Wind = WindComp->GetWindAt(Pos);
		const FVector Dir = Wind.GetSafeNormal();
		const double AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(BaseDir, Dir), -1.0, 1.0)));
		TestTrue(FString::Printf(TEXT("Direction drift %.2f deg <= %.2f deg"), AngleDeg, WindComp->DirectionDriftDeg + 0.01f), AngleDeg <= static_cast<double>(WindComp->DirectionDriftDeg) + 0.01);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWindGustMean, "KiteSurf.Wind.GustMean", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWindGustMean::RunTest(const FString& Parameters)
{
	UWindComponent* WindComp = NewObject<UWindComponent>();
	TestNotNull(TEXT("WindComponent created"), WindComp);
	if (!WindComp)
	{
		return false;
	}

	WindComp->BaseWind = FVector(1000.0, 0.0, 0.0);
	WindComp->GustStrength = 0.3f;
	WindComp->GustPeriodSeconds = 8.0f;
	WindComp->ShearHeightCm = 1000.0f;
	const FVector SamplePos(0.0, 0.0, 1000.0); // At ShearHeightCm so ShearFactor = 1.0

	// Sample over one gust period
	const int32 NumSamples = 100;
	double SumSpeed = 0.0;
	for (int32 i = 0; i < NumSamples; ++i)
	{
		WindComp->TimeOverride = (static_cast<float>(i) / static_cast<float>(NumSamples)) * WindComp->GustPeriodSeconds;
		const FVector Wind = WindComp->GetWindAt(SamplePos);
		SumSpeed += Wind.Size();
	}

	const double MeanSpeed = SumSpeed / static_cast<double>(NumSamples);
	const double ExpectedSpeed = WindComp->BaseWind.Size();
	const double DeviationFraction = FMath::Abs(MeanSpeed - ExpectedSpeed) / ExpectedSpeed;

	TestTrue(FString::Printf(TEXT("Mean speed %.2f within 5%% of BaseWind %.2f (deviation: %.2f%%)"), MeanSpeed, ExpectedSpeed, DeviationFraction * 100.0), DeviationFraction <= 0.05);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
