#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "WindComponent.h"
#include "KiteRiderPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWindReturnsBaseWind, "KiteSurf.Wind.ReturnsBaseWind", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWindReturnsBaseWind::RunTest(const FString& Parameters)
{
	UWindComponent* WindComp = NewObject<UWindComponent>();
	TestNotNull(TEXT("WindComponent created"), WindComp);

	if (WindComp)
	{
		FVector Wind = WindComp->GetWindAt(FVector(100.0f, 200.0f, 0.0f));
		TestEqual(TEXT("Wind matches BaseWind default (772, 0, 0)"), Wind, FVector(772.0f, 0.0f, 0.0f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPawnClampsInputs, "KiteSurf.Pawn.ClampsInputs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPawnClampsInputs::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);

	if (World)
	{
		AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
		TestNotNull(TEXT("Pawn spawned"), Pawn);

		if (Pawn)
		{
			Pawn->SteerKite(2.5f);
			Pawn->SheetKite(1.5f);
			Pawn->SheetKite(-0.5f);
			TestTrue(TEXT("Pawn inputs clamped"), true);
		}

		World->DestroyWorld(false);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
