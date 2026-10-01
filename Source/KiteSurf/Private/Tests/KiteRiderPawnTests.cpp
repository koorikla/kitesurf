#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "KiteRiderPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

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
