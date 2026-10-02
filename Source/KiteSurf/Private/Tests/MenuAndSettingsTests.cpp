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
#include "Misc/App.h"
#include "GameMapsSettings.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "KiteRiderPawn.h"

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
    TestFalse(TEXT("Default bSkipOnboarding is false"), SaveGame->bSkipOnboarding);
    TestFalse(TEXT("Default bOnboardingCompleted is false"), SaveGame->bOnboardingCompleted);

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
    TestFalse(TEXT("Default bSkipOnboarding is false"), GI->bSkipOnboarding);
    TestFalse(TEXT("Default bOnboardingCompleted is false"), GI->bOnboardingCompleted);
    GI->SetSkipOnboarding(true);
    TestTrue(TEXT("SetSkipOnboarding sets bSkipOnboarding"), GI->bSkipOnboarding);
    GI->SetOnboardingCompleted(true);
    TestTrue(TEXT("SetOnboardingCompleted sets bOnboardingCompleted"), GI->bOnboardingCompleted);

    // Test setters with out-of-range values
    GI->SetPendingWindKnots(4.0f);
    TestEqual(TEXT("PendingWindKnots clamped at 8.0 kn"), GI->PendingWindKnots, 8.0f);

    GI->SetPendingWindKnots(45.0f);
    TestEqual(TEXT("PendingWindKnots clamped at 30.0 kn"), GI->PendingWindKnots, 30.0f);

    GI->SetMasterVolume(-0.2f);
    TestEqual(TEXT("MasterVolume clamped at 0.0"), GI->MasterVolume, 0.0f);
    TestEqual(TEXT("FApp::GetVolumeMultiplier matches MasterVolume (0.0)"), FApp::GetVolumeMultiplier(), 0.0f);

    GI->SetMasterVolume(1.8f);
    TestEqual(TEXT("MasterVolume clamped at 1.0"), GI->MasterVolume, 1.0f);
    TestEqual(TEXT("FApp::GetVolumeMultiplier matches MasterVolume (1.0)"), FApp::GetVolumeMultiplier(), 1.0f);

    // Test that GameMapsSettings has L_MainMenu as GameDefaultMap and KiteSurfGameInstance registered
    const UGameMapsSettings* MapsSettings = GetDefault<UGameMapsSettings>();
    TestNotNull(TEXT("UGameMapsSettings default object exists"), MapsSettings);
    if (MapsSettings)
    {
        TestTrue(TEXT("GameDefaultMap points to L_MainMenu"), MapsSettings->GetGameDefaultMap().Contains(TEXT("L_MainMenu")));
        TestTrue(TEXT("EditorStartupMap points to L_OpenWater"), MapsSettings->EditorStartupMap.ToString().Contains(TEXT("L_OpenWater")));
        TestTrue(TEXT("GameInstanceClass is KiteSurfGameInstance"), MapsSettings->GameInstanceClass.ToString().Contains(TEXT("KiteSurfGameInstance")));
    }

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
    TestFalse(TEXT("SettingsWidget default bSkipOnboarding is false"), SettingsWidget->bSkipOnboarding);
    SettingsWidget->ToggleSkipOnboarding();
    TestTrue(TEXT("ToggleSkipOnboarding toggles to true"), SettingsWidget->bSkipOnboarding);
    SettingsWidget->ToggleSkipOnboarding();
    TestFalse(TEXT("ToggleSkipOnboarding toggles back to false"), SettingsWidget->bSkipOnboarding);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKiteSurfSettingsV2ControlsTest,
    "KiteSurf.UI.SettingsV2Controls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FKiteSurfSettingsV2ControlsTest::RunTest(const FString& Parameters)
{
    UKiteSurfSettingsWidget* SettingsWidget = NewObject<UKiteSurfSettingsWidget>();
    TestNotNull(TEXT("UKiteSurfSettingsWidget can be instantiated"), SettingsWidget);
    if (!SettingsWidget)
    {
        return false;
    }
    SettingsWidget->Initialize();

    // 1. Initial State verification
    TestTrue(TEXT("SupportedResolutions is populated"), SettingsWidget->SupportedResolutions.Num() > 0);
    TestTrue(TEXT("CurrentQualityPreset is in valid range [0, 3]"), SettingsWidget->CurrentQualityPreset >= 0 && SettingsWidget->CurrentQualityPreset <= 3);

    // 2. Fullscreen mode toggle and set
    const EWindowMode::Type InitialMode = SettingsWidget->CurrentWindowMode;
    SettingsWidget->ToggleFullscreen();
    TestNotEqual(TEXT("ToggleFullscreen changes window mode"), SettingsWidget->CurrentWindowMode.GetValue(), InitialMode);
    SettingsWidget->SetFullscreenMode(EWindowMode::WindowedFullscreen);
    TestEqual(TEXT("SetFullscreenMode sets WindowedFullscreen"), SettingsWidget->CurrentWindowMode.GetValue(), EWindowMode::WindowedFullscreen);
    SettingsWidget->SetFullscreenMode(EWindowMode::Windowed);
    TestEqual(TEXT("SetFullscreenMode sets Windowed"), SettingsWidget->CurrentWindowMode.GetValue(), EWindowMode::Windowed);

    // 3. Resolution setting
    const FIntPoint TestRes(1920, 1080);
    SettingsWidget->SetResolution(TestRes);
    TestEqual(TEXT("SetResolution sets 1920x1080"), SettingsWidget->CurrentResolution, TestRes);

    if (SettingsWidget->SupportedResolutions.Num() > 0)
    {
        SettingsWidget->SetResolutionByIndex(0);
        TestEqual(TEXT("SetResolutionByIndex sets first supported resolution"), SettingsWidget->CurrentResolution, SettingsWidget->SupportedResolutions[0]);
    }

    // 4. VSync toggle and set
    const bool bInitialVSync = SettingsWidget->bCurrentVSync;
    SettingsWidget->ToggleVSync();
    TestEqual(TEXT("ToggleVSync inverts vsync flag"), SettingsWidget->bCurrentVSync, !bInitialVSync);
    SettingsWidget->SetVSyncEnabled(true);
    TestTrue(TEXT("SetVSyncEnabled(true) enables VSync"), SettingsWidget->bCurrentVSync);
    SettingsWidget->SetVSyncEnabled(false);
    TestFalse(TEXT("SetVSyncEnabled(false) disables VSync"), SettingsWidget->bCurrentVSync);

    // 5. Scalability Quality Presets (0=Low, 1=Medium, 2=High, 3=Epic)
    SettingsWidget->SetQualityPreset(0);
    TestEqual(TEXT("SetQualityPreset(0) sets Low"), SettingsWidget->CurrentQualityPreset, 0);
    SettingsWidget->SetQualityPreset(3);
    TestEqual(TEXT("SetQualityPreset(3) sets Epic"), SettingsWidget->CurrentQualityPreset, 3);
    SettingsWidget->SetQualityPreset(2);
    TestEqual(TEXT("SetQualityPreset(2) sets High"), SettingsWidget->CurrentQualityPreset, 2);
    SettingsWidget->SetQualityPreset(1);
    TestEqual(TEXT("SetQualityPreset(1) sets Medium"), SettingsWidget->CurrentQualityPreset, 1);

    // Clamping test
    SettingsWidget->SetQualityPreset(-1);
    TestEqual(TEXT("SetQualityPreset(-1) clamped to 0"), SettingsWidget->CurrentQualityPreset, 0);
    SettingsWidget->SetQualityPreset(5);
    TestEqual(TEXT("SetQualityPreset(5) clamped to 3"), SettingsWidget->CurrentQualityPreset, 3);

    // 6. Test Slate fallback widget hierarchy creation
    TSharedRef<SWidget> SlateWidget = SettingsWidget->TakeWidget();
    TestTrue(TEXT("TakeWidget creates valid Slate widget"), SlateWidget != SNullWidget::NullWidget);

    // 7. Verify ApplyVideoSettings updates UGameUserSettings
    SettingsWidget->SetQualityPreset(2);
    SettingsWidget->SetFullscreenMode(EWindowMode::Windowed);
    SettingsWidget->SetVSyncEnabled(false);
    SettingsWidget->ApplyVideoSettings();

    UGameUserSettings* UserSettings = UGameUserSettings::GetGameUserSettings();
    TestNotNull(TEXT("UGameUserSettings is valid"), UserSettings);
    if (UserSettings)
    {
        TestEqual(TEXT("UserSettings Scalability matches CurrentQualityPreset"), UserSettings->GetOverallScalabilityLevel(), 2);
        TestEqual(TEXT("UserSettings WindowMode matches CurrentWindowMode"), UserSettings->GetFullscreenMode(), EWindowMode::Windowed);
        TestEqual(TEXT("UserSettings VSync matches bCurrentVSync"), UserSettings->IsVSyncEnabled(), false);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKiteSurfRiderMaterialsTest,
    "KiteSurf.Graphics.RiderMaterials",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FKiteSurfRiderMaterialsTest::RunTest(const FString& Parameters)
{
    // 1. Verify SKM_Manny_Simple skeletal mesh exists and loads
    USkeletalMesh* MannyMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
    TestNotNull(TEXT("SKM_Manny_Simple skeletal mesh loads successfully"), MannyMesh);

    // 2. Verify M_Mannequin base material exists and loads
    UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Characters/Mannequins/Materials/M_Mannequin.M_Mannequin"));
    TestNotNull(TEXT("M_Mannequin material loads successfully"), BaseMaterial);

    // 3. Verify MI_Manny_01_New and MI_Manny_02_New material instances exist and load
    UMaterialInterface* MannyMat01 = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Characters/Mannequins/Materials/Manny/MI_Manny_01_New.MI_Manny_01_New"));
    TestNotNull(TEXT("MI_Manny_01_New material instance loads successfully"), MannyMat01);

    UMaterialInterface* MannyMat02 = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Characters/Mannequins/Materials/Manny/MI_Manny_02_New.MI_Manny_02_New"));
    TestNotNull(TEXT("MI_Manny_02_New material instance loads successfully"), MannyMat02);

    if (MannyMesh)
    {
        const TArray<FSkeletalMaterial>& MeshMaterials = MannyMesh->GetMaterials();
        TestTrue(TEXT("SKM_Manny_Simple has material slots"), MeshMaterials.Num() > 0);
        for (int32 i = 0; i < MeshMaterials.Num(); ++i)
        {
            TestNotNull(FString::Printf(TEXT("SKM_Manny_Simple material slot %d is assigned"), i), MeshMaterials[i].MaterialInterface.Get());
        }
    }

    return true;
}

// Escape reaches the pawn twice on one press (the Enhanced Input action and the fallback key
// binding). That must open the pause menu once, not open it and close it again.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKiteSurfPauseOneTogglePerPressTest,
    "KiteSurf.UI.PauseOneTogglePerPress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FKiteSurfPauseOneTogglePerPressTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    APlayerController* PC = World->SpawnActor<APlayerController>();
    AKiteSurfHUD* HUD = World->SpawnActor<AKiteSurfHUD>();
    AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
    TestTrue(TEXT("Controller, HUD and pawn spawned"), PC && HUD && Pawn);
    if (PC && HUD && Pawn)
    {
        PC->SetPlayerState(World->SpawnActor<APlayerState>());
        HUD->PlayerOwner = PC;
        PC->MyHUD = HUD;
        PC->Possess(Pawn);

        Pawn->TogglePause();
        Pawn->TogglePause(); // the duplicate from the same key press, same frame
        TestTrue(TEXT("One Escape press pauses the game"), UGameplayStatics::IsGamePaused(World));
        TestNotNull(TEXT("and leaves the pause menu open"), HUD->GetActivePauseMenuWidget());

        if (UKiteSurfPauseMenuWidget* PauseMenu = HUD->GetActivePauseMenuWidget())
        {
            // Settings opened from the pause menu replace it on screen and hand back to it.
            PauseMenu->OnSettingsClicked();
            TestNotNull(TEXT("Settings open from the pause menu"), PauseMenu->ActiveSettingsWidget.Get());
            TestEqual(TEXT("The pause menu is hidden behind settings"), PauseMenu->GetVisibility(), ESlateVisibility::Collapsed);
            PauseMenu->OnSettingsClosed();
            TestNull(TEXT("Closing settings clears it"), PauseMenu->ActiveSettingsWidget.Get());
            TestEqual(TEXT("and shows the pause menu again"), PauseMenu->GetVisibility(), ESlateVisibility::Visible);
            TestTrue(TEXT("The game is still paused"), UGameplayStatics::IsGamePaused(World));
        }
    }

    World->DestroyWorld(false);
    return true;
}

// The settings screen replaces the main menu rather than drawing over it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKiteSurfMainMenuHidesBehindSettingsTest,
    "KiteSurf.UI.MainMenuHidesBehindSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FKiteSurfMainMenuHidesBehindSettingsTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    UKiteSurfMainMenuWidget* MainMenu = CreateWidget<UKiteSurfMainMenuWidget>(World, UKiteSurfMainMenuWidget::StaticClass());
    TestNotNull(TEXT("Main menu created"), MainMenu);
    if (MainMenu)
    {
        MainMenu->OnSettingsClicked();
        TestNotNull(TEXT("Settings open from the main menu"), MainMenu->ActiveSettingsWidget.Get());
        TestEqual(TEXT("The main menu is hidden behind settings"), MainMenu->GetVisibility(), ESlateVisibility::Collapsed);

        MainMenu->OnSettingsClosed();
        TestEqual(TEXT("Closing settings shows the main menu again"), MainMenu->GetVisibility(), ESlateVisibility::Visible);
    }

    World->DestroyWorld(false);
    return true;
}
