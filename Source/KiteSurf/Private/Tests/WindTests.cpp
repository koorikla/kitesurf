#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "WindComponent.h"
#include "KiteWindMath.h"
#include "KiteSurf.h"

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

// The mean wind follows the power-law profile over water: about 20% less at chest height than at
// 10 m, about 10% more where the kite flies.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWindShear, "KiteSurf.Wind.Shear", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWindShear::RunTest(const FString& Parameters)
{
	UWindComponent* WindComp = NewObject<UWindComponent>();
	TestNotNull(TEXT("WindComponent created"), WindComp);
	if (!WindComp)
	{
		return false;
	}

	// Disable gust and direction drift to isolate the profile
	WindComp->GustStrength = 0.0f;
	WindComp->DirectionDriftDeg = 0.0f;
	WindComp->BaseWind = FVector(1000.0, 0.0, 0.0);
	TestNearlyEqual(TEXT("The base wind is quoted at 10 m"), WindComp->ReferenceHeightCm, 1000.0f, 0.001f);

	// u(z) / u(10 m) from the research table (docs/physics/research.md, section 4.1).
	struct FProfilePoint { float HeightCm; float Expected; };
	const FProfilePoint Table[] = { { 150.0f, 0.81f }, { 1000.0f, 1.00f }, { 2500.0f, 1.11f } };
	for (const FProfilePoint& Point : Table)
	{
		const float Ratio = static_cast<float>(WindComp->GetWindAt(FVector(0.0, 0.0, Point.HeightCm)).Size()) / 1000.0f;
		TestNearlyEqual(FString::Printf(TEXT("Wind at %.1f m is %.3f of the 10 m wind (research %.2f, within 1%%)"), Point.HeightCm / 100.0f, Ratio, Point.Expected), Ratio, Point.Expected, 0.01f * Point.Expected);
		TestNearlyEqual(TEXT("and GetProfileFactor says the same"), WindComp->GetProfileFactor(Point.HeightCm), Ratio, 0.0001f);
	}

	// Below MinSampleHeightCm the profile is not sampled: the surface reads the wind at 1 m.
	const float AtFloor = static_cast<float>(WindComp->GetWindAt(FVector(0.0, 0.0, WindComp->MinSampleHeightCm)).Size());
	TestNearlyEqual(TEXT("The wind at 1 m is 0.78 of the 10 m wind (research 0.78, within 1%)"), AtFloor / 1000.0f, 0.78f, 0.0078f);
	TestNearlyEqual(TEXT("At the water the wind is the wind at MinSampleHeightCm"), static_cast<float>(WindComp->GetWindAt(FVector(0.0, 0.0, 0.0)).Size()), AtFloor, 0.001f);
	TestNearlyEqual(TEXT("and below it too"), static_cast<float>(WindComp->GetWindAt(FVector(0.0, 0.0, -85.0)).Size()), AtFloor, 0.001f);
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
	const FVector BaseDir(1.0, 0.0, 0.0);

	// Ten minutes at one place: the direction wanders about the mean by DirectionDriftDeg (one
	// standard deviation) and never by more than twice that.
	const int32 Samples = 2400;
	const float SampleSeconds = 0.25f;
	double SumSquares = 0.0;
	double LargestDeg = 0.0;
	for (int32 Sample = 0; Sample < Samples; ++Sample)
	{
		WindComp->TimeOverride = Sample * SampleSeconds;
		const FVector Dir = WindComp->GetWindAt(FVector(0.0, 0.0, 1000.0)).GetSafeNormal();
		const double AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
		SumSquares += AngleDeg * AngleDeg;
		LargestDeg = FMath::Max(LargestDeg, FMath::Abs(AngleDeg));
	}
	const double StdDevDeg = FMath::Sqrt(SumSquares / Samples);
	UE_LOG(LogKiteSurf, Log, TEXT("DirectionDrift: over 600 s the direction wanders %.2f deg (standard deviation) and at most %.2f deg; DirectionDriftDeg %.1f"), StdDevDeg, LargestDeg, WindComp->DirectionDriftDeg);
	TestTrue(FString::Printf(TEXT("Direction never strays more than twice DirectionDriftDeg (%.2f deg)"), LargestDeg), LargestDeg <= 2.0 * WindComp->DirectionDriftDeg + 0.01);
	TestNearlyEqual(TEXT("Its standard deviation is about DirectionDriftDeg (within 30%)"), static_cast<float>(StdDevDeg), WindComp->DirectionDriftDeg, 0.3f * WindComp->DirectionDriftDeg);

	WindComp->DirectionDriftDeg = 0.0f;
	TestTrue(TEXT("With no drift the wind blows along BaseWind"), WindComp->GetWindAt(FVector(1234.0, 567.0, 1000.0)).GetSafeNormal().Equals(BaseDir, 1e-5));
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

	// Ten minutes of wind at five places 30 m apart across the wind (a gust cell's width), at the
	// reference height where the profile is 1. One place alone sees about a hundred gust cells go
	// by in that time, so its own mean wanders by about 1%.
	const int32 NumPlaces = 5;
	const float PlaceSpacingCm = 3000.0f;
	const int32 NumSamples = 2400;
	const float SampleSeconds = 0.25f;
	double SumFactor = 0.0;
	double SumSpeed = 0.0;
	FString PlaceMeans;
	for (int32 Place = 0; Place < NumPlaces; ++Place)
	{
		const FVector Location(0.0, Place * PlaceSpacingCm, 0.0);
		double PlaceSum = 0.0;
		for (int32 i = 0; i < NumSamples; ++i)
		{
			WindComp->TimeOverride = i * SampleSeconds;
			const float Factor = WindComp->GetGustFactorAt(Location);
			PlaceSum += Factor;
			SumSpeed += WindComp->GetWindAt(FVector(Location.X, Location.Y, WindComp->ReferenceHeightCm)).Size();
		}
		SumFactor += PlaceSum;
		PlaceMeans += FString::Printf(TEXT(" %.4f"), PlaceSum / NumSamples);
	}

	const double MeanFactor = SumFactor / (NumPlaces * NumSamples);
	const double MeanSpeed = SumSpeed / (NumPlaces * NumSamples);
	UE_LOG(LogKiteSurf, Log, TEXT("GustMean: over 600 s the gust factor averages %.4f (at each place:%s) and the speed %.1f cm/s against a base of %.1f"), MeanFactor, *PlaceMeans, MeanSpeed, WindComp->BaseWind.Size());
	TestNearlyEqual(TEXT("The gust factor averages 1 over ten minutes (within 2%)"), static_cast<float>(MeanFactor), 1.0f, 0.02f);
	TestNearlyEqual(TEXT("and the wind at the reference height averages BaseWind (within 2%)"), static_cast<float>(MeanSpeed), 1000.0f, 20.0f);
	return true;
}

// Gusts and lulls must be big enough for the rider to notice and react to, and no bigger than GustStrength says.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWindGustsAndLulls, "KiteSurf.Wind.GustsAndLulls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWindGustsAndLulls::RunTest(const FString& Parameters)
{
	UWindComponent* WindComp = NewObject<UWindComponent>();
	TestNotNull(TEXT("WindComponent created"), WindComp);
	if (!WindComp)
	{
		return false;
	}

	WindComp->BaseWind = FVector(1000.0, 0.0, 0.0);
	const FVector AtWater(0.0, 0.0, 0.0);
	const float Strength = WindComp->GustStrength;

	// Ten minutes of wind with the default gust settings
	float MinFactor = 10.0f;
	float MaxFactor = 0.0f;
	float LargestStepPerSecond = 0.0f;
	float PreviousFactor = 1.0f;
	const float SampleSeconds = 0.25f;
	for (int32 Sample = 0; Sample < 2400; ++Sample)
	{
		WindComp->TimeOverride = Sample * SampleSeconds;
		const float Factor = WindComp->GetGustFactorAt(AtWater);
		MinFactor = FMath::Min(MinFactor, Factor);
		MaxFactor = FMath::Max(MaxFactor, Factor);
		if (Sample > 0)
		{
			LargestStepPerSecond = FMath::Max(LargestStepPerSecond, FMath::Abs(Factor - PreviousFactor) / SampleSeconds);
		}
		PreviousFactor = Factor;
	}
	UE_LOG(LogKiteSurf, Log, TEXT("GustsAndLulls: over 600 s in %.1f m/s the gust factor ranges %.3f to %.3f (GustStrength %.2f), largest change %.2f per second"),
		WindComp->BaseWind.Size() / 100.0f, MinFactor, MaxFactor, Strength, LargestStepPerSecond);

	TestTrue(FString::Printf(TEXT("Gusts reach at least 80%% of GustStrength over the base wind (peak %.3f)"), MaxFactor), MaxFactor >= 1.0f + 0.8f * Strength);
	TestTrue(FString::Printf(TEXT("Lulls reach at least 80%% of GustStrength below it (trough %.3f)"), MinFactor), MinFactor <= 1.0f - 0.8f * Strength);
	TestTrue(TEXT("Gusts and lulls never go past GustStrength"), MaxFactor <= 1.0f + Strength + 0.0001f && MinFactor >= 1.0f - Strength - 0.0001f);
	TestTrue(FString::Printf(TEXT("The wind builds and fades rather than jumping (largest change %.0f%% per second)"), LargestStepPerSecond * 100.0f), LargestStepPerSecond < 0.5f);

	// The gust factor is measured at the reference height, so wind shear does not read as a lull.
	WindComp->GustStrength = 0.0f;
	TestNearlyEqual(TEXT("With gusts off the factor is 1 even at the water"), WindComp->GetGustFactorAt(AtWater), 1.0f, 0.001f);
	return true;
}

// Gusts are carried downwind at the mean wind speed (frozen turbulence): a gust seen upwind arrives
// distance / U later. Another seed is another field.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWindGustsTravel, "KiteSurf.Wind.GustsTravel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWindGustsTravel::RunTest(const FString& Parameters)
{
	UWindComponent* WindComp = NewObject<UWindComponent>();
	TestNotNull(TEXT("WindComponent created"), WindComp);
	if (!WindComp)
	{
		return false;
	}

	const float SpeedCmS = 1000.0f;
	WindComp->BaseWind = FVector(SpeedCmS, 0.0, 0.0);
	TestTrue(TEXT("Gusts are on"), WindComp->GustStrength > 0.0f);

	FRandomStream Random(20261002);
	float LargestDifference = 0.0f;
	for (const float DtSeconds : { 0.5f, 1.0f, 2.0f, 3.0f, 5.0f })
	{
		for (int32 Sample = 0; Sample < 50; ++Sample)
		{
			const FVector Place(Random.FRandRange(-50000.0f, 50000.0f), Random.FRandRange(-50000.0f, 50000.0f), 0.0f);
			const float Time = Random.FRandRange(0.0f, 600.0f);
			const float Here = WindComp->GetGustFactorAtTime(Place, Time);
			const float Downwind = WindComp->GetGustFactorAtTime(Place + FVector(SpeedCmS * DtSeconds, 0.0f, 0.0f), Time + DtSeconds);
			LargestDifference = FMath::Max(LargestDifference, FMath::Abs(Downwind - Here));
		}
	}
	UE_LOG(LogKiteSurf, Log, TEXT("GustsTravel: a gust U dt downwind dt later differs by at most %.4f in the gust factor (dt up to 5 s)"), LargestDifference);
	TestTrue(FString::Printf(TEXT("The gust a place sees arrives U dt downwind dt later, within 3%% (%.4f)"), LargestDifference), LargestDifference <= 0.03f);

	// Another seed: another field. Correlation of the gust factor over 100 places and times.
	UWindComponent* Other = NewObject<UWindComponent>();
	Other->BaseWind = WindComp->BaseWind;
	Other->Seed = WindComp->Seed + 1;
	const int32 Count = 100;
	double SumA = 0.0, SumB = 0.0, SumAA = 0.0, SumBB = 0.0, SumAB = 0.0;
	for (int32 Sample = 0; Sample < Count; ++Sample)
	{
		const FVector Place(Random.FRandRange(-50000.0f, 50000.0f), Random.FRandRange(-50000.0f, 50000.0f), 0.0f);
		const float Time = Random.FRandRange(0.0f, 600.0f);
		const double A = WindComp->GetGustFactorAtTime(Place, Time);
		const double B = Other->GetGustFactorAtTime(Place, Time);
		SumA += A; SumB += B; SumAA += A * A; SumBB += B * B; SumAB += A * B;
	}
	const double CovAB = SumAB / Count - (SumA / Count) * (SumB / Count);
	const double VarA = SumAA / Count - FMath::Square(SumA / Count);
	const double VarB = SumBB / Count - FMath::Square(SumB / Count);
	const double Correlation = CovAB / FMath::Sqrt(FMath::Max(VarA * VarB, 1e-12));
	UE_LOG(LogKiteSurf, Log, TEXT("GustsTravel: seeds %d and %d correlate %.3f over %d samples"), WindComp->Seed, Other->Seed, Correlation, Count);
	TestTrue(FString::Printf(TEXT("A different seed gives a different field (correlation %.3f under 0.5)"), Correlation), FMath::Abs(Correlation) < 0.5);
	return true;
}

// The wind is a pure function of place, time and seed: the same parameters give the same wind.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWindSameSeedSameWind, "KiteSurf.Wind.SameSeedSameWind", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWindSameSeedSameWind::RunTest(const FString& Parameters)
{
	UWindComponent* First = NewObject<UWindComponent>();
	UWindComponent* Second = NewObject<UWindComponent>();
	TestTrue(TEXT("WindComponents created"), First && Second);
	if (!First || !Second)
	{
		return false;
	}
	for (UWindComponent* Wind : { First, Second })
	{
		Wind->BaseWind = FVector(900.0, 300.0, 0.0);
		Wind->Seed = 7;
	}

	FRandomStream Random(42);
	int32 Agreed = 0;
	const int32 Count = 100;
	for (int32 Sample = 0; Sample < Count; ++Sample)
	{
		const FVector Place(Random.FRandRange(-100000.0f, 100000.0f), Random.FRandRange(-100000.0f, 100000.0f), Random.FRandRange(-100.0f, 3000.0f));
		const float Time = Random.FRandRange(0.0f, 3600.0f);
		Agreed += (First->GetWindAtTime(Place, Time) == Second->GetWindAtTime(Place, Time)) ? 1 : 0;
	}
	TestEqual(TEXT("Two winds with the same parameters and seed agree exactly at 100 places and times"), Agreed, Count);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
