#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "KiteRiderPawn.h"
#include "KiteComponent.h"
#include "WindComponent.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

// Test 1: zero wind -> zero tension
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteZeroWind, "KiteSurf.Kite.ZeroWind", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteZeroWind::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn && Pawn->Kite)
		{
			UWindComponent* WindComp = Pawn->FindComponentByClass<UWindComponent>();
			TestNotNull(TEXT("WindComponent found"), WindComp);
			if (WindComp)
			{
				WindComp->BaseWind = FVector::ZeroVector;
			}

			Pawn->SheetKite(1.0f);
			Pawn->Kite->UpdateKite(0.1f);

			const float Tension = Pawn->Kite->GetLineTensionN();
			TestNearlyEqual(TEXT("Zero wind results in zero tension"), Tension, 0.0f, 0.001f);
			TestEqual(TEXT("Zero wind results in zero line force"), Pawn->Kite->GetLineForce(), FVector::ZeroVector);
		}

		World->DestroyWorld(false);
	}

	return true;
}

// Test 2: kite at zenith in 15 kn wind, sheeted in -> tension in a plausible 200-900 N range
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteZenithTension, "KiteSurf.Kite.ZenithTensionInRange", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteZenithTension::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn && Pawn->Kite)
		{
			UWindComponent* WindComp = Pawn->FindComponentByClass<UWindComponent>();
			if (WindComp)
			{
				// 15 knots approx 772 cm/s
				WindComp->BaseWind = FVector(772.0f, 0.0f, 0.0f);
			}

			// Set kite at zenith: Azimuth = 0, Elevation = 90
			Pawn->Kite->SetAzimuthDeg(0.0f);
			Pawn->Kite->SetElevationDeg(90.0f);
			Pawn->SheetKite(1.0f); // sheeted in

			Pawn->Kite->UpdateKite(0.1f);

			const float Tension = Pawn->Kite->GetLineTensionN();
			TestTrue(FString::Printf(TEXT("Tension at zenith (%.1f N) in plausible range 200-900 N"), Tension),
				Tension >= 200.0f && Tension <= 900.0f);
		}

		World->DestroyWorld(false);
	}

	return true;
}

// Test 3: sheeting out reduces tension monotonically
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteSheetMonotonic, "KiteSurf.Kite.SheetingOutReducesTensionMonotonically", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteSheetMonotonic::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn && Pawn->Kite)
		{
			UWindComponent* WindComp = Pawn->FindComponentByClass<UWindComponent>();
			if (WindComp)
			{
				WindComp->BaseWind = FVector(772.0f, 0.0f, 0.0f);
			}

			Pawn->Kite->SetAzimuthDeg(0.0f);
			Pawn->Kite->SetElevationDeg(45.0f);

			float PrevTension = 1e9f;
			const float SheetSteps[] = { 1.0f, 0.75f, 0.5f, 0.25f, 0.0f };

			for (float SheetVal : SheetSteps)
			{
				Pawn->SheetKite(SheetVal);
				Pawn->Kite->UpdateKite(0.0f);
				const float CurrentTension = Pawn->Kite->GetLineTensionN();

				TestTrue(FString::Printf(TEXT("Tension at sheet %.2f (%.1f N) <= previous sheet (%.1f N)"),
					SheetVal, CurrentTension, PrevTension), CurrentTension <= PrevTension + 0.001f);

				PrevTension = CurrentTension;
			}

			// Verify sheeted in has strictly greater tension than sheeted out
			Pawn->SheetKite(1.0f);
			Pawn->Kite->UpdateKite(0.0f);
			const float MaxTension = Pawn->Kite->GetLineTensionN();

			Pawn->SheetKite(0.0f);
			Pawn->Kite->UpdateKite(0.0f);
			const float MinTension = Pawn->Kite->GetLineTensionN();

			TestTrue(TEXT("Sheeted in tension is strictly greater than sheeted out tension"), MaxTension > MinTension);
		}

		World->DestroyWorld(false);
	}

	return true;
}

// Test 4: steering +1 for 1 s increases azimuth
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteSteeringIncreasesAzimuth, "KiteSurf.Kite.SteeringIncreasesAzimuth", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteSteeringIncreasesAzimuth::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn && Pawn->Kite)
		{
			UWindComponent* WindComp = Pawn->FindComponentByClass<UWindComponent>();
			if (WindComp)
			{
				WindComp->BaseWind = FVector(772.0f, 0.0f, 0.0f);
			}

			Pawn->Kite->SetAzimuthDeg(0.0f);
			Pawn->Kite->SetElevationDeg(45.0f);
			Pawn->SheetKite(0.5f);

			const float InitialAzimuth = Pawn->GetKiteAzimuthDeg();

			// Steer +1 for 1 s over 20 steps
			Pawn->SteerKite(1.0f);
			const int32 NumSteps = 20;
			const float Dt = 1.0f / NumSteps;
			for (int32 i = 0; i < NumSteps; ++i)
			{
				Pawn->Kite->UpdateKite(Dt);
			}

			const float FinalAzimuth = Pawn->GetKiteAzimuthDeg();
			TestTrue(FString::Printf(TEXT("Steering +1 for 1s increased azimuth from %.1f to %.1f"),
				InitialAzimuth, FinalAzimuth), FinalAzimuth > InitialAzimuth);
		}

		World->DestroyWorld(false);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
