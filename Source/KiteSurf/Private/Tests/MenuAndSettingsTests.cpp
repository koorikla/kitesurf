#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UI/KiteSurfSaveGame.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfSettingsWidget.h"
#include "UI/KiteSurfMainMenuWidget.h"
#include "UI/KiteSurfPauseMenuWidget.h"
#include "UI/KiteSurfMainMenuGameMode.h"
#include "WindComponent.h"
#include "KiteSurfHUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKiteSurfSaveGameDefaultsAndClampingTest,
    "KiteSurf.UI.SaveGameDefaultsAndClamping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FKiteSurfSaveGameDefaultsAndClampingTest::RunTest(const FString& Parameters)
{
    UKiteSurfSaveGame* SaveGame = NewObject<UKiteSurfSaveGame>();
    TestNotNull(TEXT("SaveGame object created"), SaveGame);

    // Check default values
    TestEqual(TEXT("Default WindStrengthKnots is 15.0 kn"), SaveGame->WindStrengthKnots, 15.0f);
    TestEqual(TEXT("Default MasterVolume is 1.0 (100%)"), SaveGame->MasterVolume, 1.0f);

    // Test underflow clamping (min wind = 8.0, min volume = 0.0)
    SaveGame->WindStrengthKnots = 2.0f;
    SaveGame->MasterVolume = -0.5f;
    SaveGame->SaveSettings();
    TestEqual(TEXT("Wind clamped to minimum 8.0 kn"), SaveGame->WindStrengthKnots, 8.0f);
    TestEqual(TEXT("Volume clamped to minimum 0.0"), SaveGame->MasterVolume, 0.0f);

    // Test overflow clamping (max wind = 30.0, max volume = 1.0)
    SaveGame->WindStrengthKnots = 55.0f;
    SaveGame->MasterVolume = 2.5f;
    SaveGame->SaveSettings();
    TestEqual(TEXT("Wind clamped to maximum 30.0 kn"), SaveGame->WindStrengthKnots, 30.0f);
    TestEqual(TEXT("Volume clamped to maximum 1.0"), SaveGame->MasterVolume, 1.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKiteSurfGameInstanceSettingsTest,
    "KiteSurf.UI.GameInstanceSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FKiteSurfGameInstanceSettingsTest::RunTest(const FString& Parameters)
{
    UKiteSurfGameInstance* GI = NewObject<UKiteSurfGameInstance>();
    TestNotNull(TEXT("GameInstance created"), GI);

    TestEqual(TEXT("Default PendingWindKnots is 15.0 kn"), GI->PendingWindKnots, 15.0f);
    TestEqual(TEXT("Default MasterVolume is 1.0"), GI->MasterVolume, 1.0f);

    // Test setters with out-of-range values
    GI->SetPendingWindKnots(4.0f);
    TestEqual(TEXT("PendingWindKnots clamped at 8.0 kn"), GI->PendingWindKnots, 8.0f);

    GI->SetPendingWindKnots(45.0f);
    TestEqual(TEXT("PendingWindKnots clamped at 30.0 kn"), GI->PendingWindKnots, 30.0f);

    GI->SetMasterVolume(-0.2f);
    TestEqual(TEXT("MasterVolume clamped at 0.0"), GI->MasterVolume, 0.0f);

    GI->SetMasterVolume(1.8f);
    TestEqual(TEXT("MasterVolume clamped at 1.0"), GI->MasterVolume, 1.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKiteSurfUIWidgetsInstantiationTest,
    "KiteSurf.UI.WidgetsInstantiation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FKiteSurfUIWidgetsInstantiationTest::RunTest(const FString& Parameters)
{
    UKiteSurfSettingsWidget* SettingsWidget = NewObject<UKiteSurfSettingsWidget>();
    TestNotNull(TEXT("UKiteSurfSettingsWidget can be instantiated"), SettingsWidget);
    TestEqual(TEXT("SettingsWidget default wind is 15 kn"), SettingsWidget->CurrentWindKnots, 15.0f);
    TestEqual(TEXT("SettingsWidget default volume is 1.0"), SettingsWidget->CurrentVolume, 1.0f);

    UKiteSurfMainMenuWidget* MainMenuWidget = NewObject<UKiteSurfMainMenuWidget>();
    TestNotNull(TEXT("UKiteSurfMainMenuWidget can be instantiated"), MainMenuWidget);

    UKiteSurfPauseMenuWidget* PauseMenuWidget = NewObject<UKiteSurfPauseMenuWidget>();
    TestNotNull(TEXT("UKiteSurfPauseMenuWidget can be instantiated"), PauseMenuWidget);

    AKiteSurfMainMenuGameMode* GameMode = NewObject<AKiteSurfMainMenuGameMode>();
    TestNotNull(TEXT("AKiteSurfMainMenuGameMode can be instantiated"), GameMode);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKiteSurfPauseMenuTogglesTest,
    "KiteSurf.UI.PauseMenuToggles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FKiteSurfPauseMenuTogglesTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    APlayerController* PC = World->SpawnActor<APlayerController>();
    TestNotNull(TEXT("PlayerController spawned"), PC);
    if (PC)
    {
        APlayerState* PS = World->SpawnActor<APlayerState>();
        PC->SetPlayerState(PS);
    }

    AKiteSurfHUD* HUD = World->SpawnActor<AKiteSurfHUD>();
    TestNotNull(TEXT("HUD spawned"), HUD);

    if (HUD && PC)
    {
        HUD->PlayerOwner = PC;

        TestFalse(TEXT("Initially game is not paused"), UGameplayStatics::IsGamePaused(World));
        TestNull(TEXT("Initially ActivePauseMenuWidget is null"), HUD->GetActivePauseMenuWidget());

        // First toggle: open pause menu
        HUD->TogglePauseMenu();
        TestTrue(TEXT("After 1st toggle: game is paused"), UGameplayStatics::IsGamePaused(World));
        TestNotNull(TEXT("After 1st toggle: ActivePauseMenuWidget is valid"), HUD->GetActivePauseMenuWidget());

        // Second toggle: close pause menu
        HUD->TogglePauseMenu();
        TestFalse(TEXT("After 2nd toggle: game is unpaused"), UGameplayStatics::IsGamePaused(World));
        TestNull(TEXT("After 2nd toggle: ActivePauseMenuWidget is cleared"), HUD->GetActivePauseMenuWidget());
    }

    World->DestroyWorld(false);
    return true;
}
