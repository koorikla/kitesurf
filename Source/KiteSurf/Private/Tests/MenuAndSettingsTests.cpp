#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UI/KiteSurfMenuNavigator.h"
#include "UI/KiteSurfGearWidget.h"
#include "UI/KiteSurfMenuStyle.h"
#include "KiteComponent.h"
#include "KiteGear.h"
#include "Blueprint/UserWidget.h"
#include "Misc/Paths.h"
#include "Engine/Texture2D.h"
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
    TestEqual(TEXT("Default WindStrengthKnots is 20 kn"), SaveGame->WindStrengthKnots, 20.0f);
    TestEqual(TEXT("Default kite is the 9 m"), SaveGame->KiteSizeM2, 9.0f);
    TestEqual(TEXT("Default MasterVolume is 1.0 (100%)"), SaveGame->MasterVolume, 1.0f);
    TestFalse(TEXT("Default bSkipOnboarding is false"), SaveGame->bSkipOnboarding);
    TestFalse(TEXT("Default bOnboardingCompleted is false"), SaveGame->bOnboardingCompleted);

    // Saved to a slot of its own: the default slot is the player's real settings file.
    const FString TestSlot = TEXT("SettingsAutomationTest");

    // Test underflow clamping (min wind = 8.0, min volume = 0.0)
    SaveGame->WindStrengthKnots = 2.0f;
    SaveGame->MasterVolume = -0.5f;
    SaveGame->SaveSettings(TestSlot);
    TestEqual(TEXT("Wind clamped to minimum 8.0 kn"), SaveGame->WindStrengthKnots, 8.0f);
    TestEqual(TEXT("Volume clamped to minimum 0.0"), SaveGame->MasterVolume, 0.0f);

    // Test overflow clamping (max wind = 90.0, max volume = 1.0)
    SaveGame->WindStrengthKnots = 120.0f;
    SaveGame->MasterVolume = 2.5f;
    SaveGame->SaveSettings(TestSlot);
    TestEqual(TEXT("Wind clamped to maximum 90.0 kn"), SaveGame->WindStrengthKnots, 90.0f);
    TestEqual(TEXT("Volume clamped to maximum 1.0"), SaveGame->MasterVolume, 1.0f);

    // The kite size travels with the rest of the settings.
    SaveGame->KiteSizeM2 = 9.0f;
    SaveGame->SaveSettings(TestSlot);
    const UKiteSurfSaveGame* Reloaded = UKiteSurfSaveGame::LoadOrCreateSettings(TestSlot);
    TestTrue(TEXT("The chosen kite size is saved and loaded"), Reloaded && Reloaded->KiteSizeM2 == 9.0f);
    UGameplayStatics::DeleteGameInSlot(TestSlot, UKiteSurfSaveGame::DefaultUserIndex);

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

    TestEqual(TEXT("Default PendingWindKnots is 20 kn"), GI->PendingWindKnots, 20.0f);
    TestEqual(TEXT("Default kite is the 9 m, which is the recommended size for that wind"), GI->GetEffectiveKiteSizeM2(), 9.0f);
    TestEqual(TEXT("The recommended kite for the default wind is the default kite"), UKiteComponent::RecommendKiteSizeM2(GI->PendingWindKnots), 9.0f);
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
    TestEqual(TEXT("45 kn is on offer"), GI->PendingWindKnots, 45.0f);
    GI->SetPendingWindKnots(120.0f);
    TestEqual(TEXT("PendingWindKnots clamped at 90.0 kn"), GI->PendingWindKnots, 90.0f);

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
    TestEqual(TEXT("SettingsWidget default wind is 20 kn"), SettingsWidget->CurrentWindKnots, 20.0f);
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

// The gear screen: choices cycle, the recommended kite follows the wind, and it opens from both menus.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfGearScreenTest, "KiteSurf.UI.GearScreen", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfGearScreenTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    UKiteSurfGearWidget* Gear = CreateWidget<UKiteSurfGearWidget>(World, UKiteSurfGearWidget::StaticClass());
    TestNotNull(TEXT("Gear widget created"), Gear);
    if (Gear)
    {
        // It opens on the 9 m in 20 kn, which is the kite that wind calls for.
        TestEqual(TEXT("The default kite is the 9 m"), Gear->CurrentKiteSizeM2, 9.0f);
        TestEqual(TEXT("in 20 kn"), Gear->CurrentWindKnots, 20.0f);
        TestTrue(TEXT("which the gear screen calls well powered"), Gear->GetPowerText().Contains(TEXT("Well powered")));

        // With no size chosen, wind drives the recommended kite.
        Gear->SetKiteSizeM2(0.0f);
        Gear->SetWindKnots(15.0f);
        TestEqual(TEXT("No size chosen: the kite is the recommended one"), Gear->GetEffectiveKiteSizeM2(), UKiteComponent::RecommendKiteSizeM2(15.0f));
        TestTrue(TEXT("and the row says so"), Gear->GetKiteSizeText().StartsWith(TEXT("AUTO")));
        Gear->SetWindKnots(30.0f);
        TestEqual(TEXT("More wind recommends a smaller kite"), Gear->GetEffectiveKiteSizeM2(), 6.0f);
        Gear->SetWindKnots(99.0f);
        TestEqual(TEXT("Wind is limited to 90 kn"), Gear->CurrentWindKnots, 90.0f);
        TestEqual(TEXT("where the recommended kite is the smallest there is"), Gear->GetEffectiveKiteSizeM2(), 2.0f);
        Gear->SetWindKnots(30.0f);

        // The size button steps through every kite and back to the recommendation.
        TArray<float> SizesSeen;
        for (int32 Click = 0; Click < UKiteComponent::GetKiteSizesM2().Num(); ++Click)
        {
            Gear->CycleKiteSize();
            SizesSeen.Add(Gear->CurrentKiteSizeM2);
        }
        TestTrue(TEXT("Clicking the size steps through every kite, smallest first"), SizesSeen == TArray<float>(UKiteComponent::GetKiteSizesM2()));
        TestTrue(TEXT("A 17 m in 30 kn is called out as big"), Gear->GetPowerText().Contains(TEXT("Big")));
        Gear->CycleKiteSize();
        TestEqual(TEXT("and then back to the recommended size"), Gear->CurrentKiteSizeM2, 0.0f);

        // The other rows cycle through their options and come back round.
        const EKiteModel FirstModel = Gear->CurrentKiteModel;
        Gear->CycleKiteModel();
        TestNotEqual(TEXT("The kite button changes the model"), Gear->CurrentKiteModel, FirstModel);
        for (int32 Click = 1; Click < static_cast<int32>(EKiteModel::Count); ++Click)
        {
            Gear->CycleKiteModel();
        }
        TestEqual(TEXT("and comes back round"), Gear->CurrentKiteModel, FirstModel);
        const EBoardSize FirstBoard = Gear->CurrentBoardSize;
        for (int32 Click = 0; Click < static_cast<int32>(EBoardSize::Count); ++Click)
        {
            Gear->CycleBoardSize();
        }
        TestEqual(TEXT("The board button goes round all three boards"), Gear->CurrentBoardSize, FirstBoard);
        const ERiderCharacter FirstRider = Gear->CurrentRider;
        Gear->CycleRider();
        TestNotEqual(TEXT("The rider button changes the rider"), Gear->CurrentRider, FirstRider);
    }

    // PLAY on the main menu goes through the gear screen; backing out returns to the menu.
    UKiteSurfMainMenuWidget* MainMenu = CreateWidget<UKiteSurfMainMenuWidget>(World, UKiteSurfMainMenuWidget::StaticClass());
    TestNotNull(TEXT("Main menu created"), MainMenu);
    if (MainMenu)
    {
        MainMenu->OnPlayClicked();
        TestNotNull(TEXT("PLAY opens the gear screen"), MainMenu->ActiveGearWidget.Get());
        TestEqual(TEXT("The main menu is hidden behind it"), MainMenu->GetVisibility(), ESlateVisibility::Collapsed);
        if (MainMenu->ActiveGearWidget)
        {
            TestFalse(TEXT("From the main menu it is the full-screen version"), MainMenu->ActiveGearWidget->bDuringRide);
            MainMenu->ActiveGearWidget->Cancel();
        }
        TestNull(TEXT("BACK closes the gear screen"), MainMenu->ActiveGearWidget.Get());
        TestEqual(TEXT("and shows the main menu again"), MainMenu->GetVisibility(), ESlateVisibility::Visible);
    }

    // GEAR on the pause menu opens it over the ride.
    UKiteSurfPauseMenuWidget* PauseMenu = CreateWidget<UKiteSurfPauseMenuWidget>(World, UKiteSurfPauseMenuWidget::StaticClass());
    TestNotNull(TEXT("Pause menu created"), PauseMenu);
    if (PauseMenu)
    {
        PauseMenu->OnGearClicked();
        TestNotNull(TEXT("GEAR opens the gear screen"), PauseMenu->ActiveGearWidget.Get());
        if (PauseMenu->ActiveGearWidget)
        {
            TestTrue(TEXT("From the pause menu it is the over-the-ride version"), PauseMenu->ActiveGearWidget->bDuringRide);
            PauseMenu->ActiveGearWidget->Cancel();
        }
        TestNull(TEXT("Closing it returns to the pause menu"), PauseMenu->ActiveGearWidget.Get());
        TestEqual(TEXT("which is visible again"), PauseMenu->GetVisibility(), ESlateVisibility::Visible);
    }

    // The choices reach the game instance and survive a bad stored index.
    UKiteSurfGameInstance* GI = NewObject<UKiteSurfGameInstance>();
    GI->SetKiteModel(EKiteModel::Boost);
    GI->SetBoardSize(EBoardSize::Small);
    TestEqual(TEXT("Game instance keeps the kite model"), GI->KiteModel, EKiteModel::Boost);
    TestEqual(TEXT("Game instance keeps the board"), GI->BoardSize, EBoardSize::Small);
    TestEqual(TEXT("A bad stored kite model falls back to the loop kite"), KiteGear::KiteModelFromIndex(99), EKiteModel::Loop);
    TestEqual(TEXT("A bad stored board falls back to the 138"), KiteGear::BoardSizeFromIndex(-1), EBoardSize::Medium);

    // The menu art exists.
    TestNotNull(TEXT("The menu background texture is imported"), KiteSurfMenuStyle::LoadBackgroundTexture());
    TestTrue(TEXT("The startup splash is in Content/Splash"), FPaths::FileExists(FPaths::ProjectContentDir() / TEXT("Splash/EdSplash.png")) && FPaths::FileExists(FPaths::ProjectContentDir() / TEXT("Splash/Splash.png")));

    World->DestroyWorld(false);
    return true;
}

// Menus are driven from the keyboard and the gamepad: up and down move, left and right change, accept presses.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfMenuNavigationTest, "KiteSurf.UI.MenuNavigation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfMenuNavigationTest::RunTest(const FString& Parameters)
{
    // The navigator on its own.
    {
        FKiteMenuNavigator Navigator;
        int32 Pressed = 0;
        int32 Adjusted = 0;
        TArray<int32> Highlighted;
        Highlighted.Init(0, 3);
        for (int32 Index = 0; Index < 3; ++Index)
        {
            FKiteMenuNavigator::FItem Item;
            Item.Highlight = [&Highlighted, Index](bool bSelected) { Highlighted[Index] = bSelected ? 1 : 0; };
            if (Index != 1)
            {
                Item.Activate = [&Pressed, Index]() { Pressed += 1 + Index; };
            }
            if (Index == 1)
            {
                Item.Adjust = [&Adjusted](int32 Direction) { Adjusted += Direction; };
            }
            Navigator.AddItem(MoveTemp(Item));
        }
        Navigator.Select(0);
        TestTrue(TEXT("The selected item is highlighted and no other"), Highlighted[0] == 1 && Highlighted[1] == 0 && Highlighted[2] == 0);

        TestTrue(TEXT("Down is a navigation key"), Navigator.HandleKey(EKeys::Down));
        TestEqual(TEXT("and moves the selection down"), Navigator.GetSelected(), 1);
        TestTrue(TEXT("The highlight moves with it"), Highlighted[0] == 0 && Highlighted[1] == 1);
        Navigator.HandleKey(EKeys::Gamepad_DPad_Down);
        TestEqual(TEXT("The D-pad moves it too"), Navigator.GetSelected(), 2);
        Navigator.HandleKey(EKeys::Gamepad_LeftStick_Down);
        TestEqual(TEXT("The left stick moves it, and past the bottom it wraps to the top"), Navigator.GetSelected(), 0);
        Navigator.HandleKey(EKeys::Up);
        TestEqual(TEXT("Up from the top wraps to the bottom"), Navigator.GetSelected(), 2);
        Navigator.HandleKey(EKeys::Gamepad_LeftStick_Up);
        Navigator.HandleKey(EKeys::Gamepad_DPad_Up);
        TestEqual(TEXT("Up twice more is back at the top"), Navigator.GetSelected(), 0);

        Navigator.HandleKey(EKeys::Enter);
        TestEqual(TEXT("Enter presses the selected item"), Pressed, 1);
        Navigator.HandleKey(EKeys::Gamepad_FaceButton_Bottom);
        Navigator.HandleKey(EKeys::SpaceBar);
        TestEqual(TEXT("as do the bottom face button and Space"), Pressed, 3);
        Navigator.HandleKey(EKeys::Right);
        TestEqual(TEXT("Left and right do nothing on an item with no value"), Adjusted, 0);

        Navigator.Select(1);
        Navigator.HandleKey(EKeys::Right);
        Navigator.HandleKey(EKeys::Gamepad_DPad_Right);
        Navigator.HandleKey(EKeys::Gamepad_LeftStick_Left);
        TestEqual(TEXT("Left and right change the selected item's value"), Adjusted, 1);
        Navigator.HandleKey(EKeys::Enter);
        TestEqual(TEXT("Accept does nothing on an item with nothing to press"), Pressed, 3);

        TestFalse(TEXT("Other keys are left for the menu to handle"), Navigator.HandleKey(EKeys::Escape));
        TestFalse(TEXT("including letters"), Navigator.HandleKey(EKeys::W));
    }

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("World created"), World);
    if (!World)
    {
        return false;
    }

    // Main menu: PLAY, SCHOOL, SETTINGS, QUIT.
    UKiteSurfMainMenuWidget* MainMenu = CreateWidget<UKiteSurfMainMenuWidget>(World, UKiteSurfMainMenuWidget::StaticClass());
    if (MainMenu)
    {
        FKiteMenuNavigator& Navigator = MainMenu->GetNavigator();
        TestEqual(TEXT("The main menu has four items"), Navigator.Num(), 4);
        TestEqual(TEXT("and opens on PLAY"), Navigator.GetSelected(), 0);
        Navigator.HandleKey(EKeys::Down);
        Navigator.HandleKey(EKeys::Down);
        Navigator.HandleKey(EKeys::Enter);
        TestNotNull(TEXT("Down twice then accept opens settings"), MainMenu->ActiveSettingsWidget.Get());
        MainMenu->OnSettingsClosed();
        TestEqual(TEXT("Back on the menu the selection is PLAY again"), MainMenu->GetNavigator().GetSelected(), 0);
        MainMenu->GetNavigator().HandleKey(EKeys::Gamepad_FaceButton_Bottom);
        TestNotNull(TEXT("Accept on PLAY opens the gear screen"), MainMenu->ActiveGearWidget.Get());
        if (MainMenu->ActiveGearWidget)
        {
            MainMenu->ActiveGearWidget->Cancel();
        }
    }

    // Pause menu in free ride: RESUME, RESTART, GEAR, BEST-THREE SESSION, SCHOOL, FREESTYLE HEAT, SETTINGS, MAIN MENU, QUIT.
    UKiteSurfPauseMenuWidget* PauseMenu = CreateWidget<UKiteSurfPauseMenuWidget>(World, UKiteSurfPauseMenuWidget::StaticClass());
    if (PauseMenu)
    {
        FKiteMenuNavigator& Navigator = PauseMenu->GetNavigator();
        TestEqual(TEXT("The pause menu has nine items"), Navigator.Num(), 9);
        Navigator.HandleKey(EKeys::Gamepad_DPad_Down);
        Navigator.HandleKey(EKeys::Gamepad_DPad_Down);
        Navigator.HandleKey(EKeys::Gamepad_FaceButton_Bottom);
        TestNotNull(TEXT("Down twice then accept opens the gear screen"), PauseMenu->ActiveGearWidget.Get());
        if (PauseMenu->ActiveGearWidget)
        {
            PauseMenu->ActiveGearWidget->Cancel();
        }
    }

    // Settings: four volumes, window mode, resolution, motion bar, motion power, bar to middle, vibration, vsync, quality, back.
    UKiteSurfSettingsWidget* Settings = CreateWidget<UKiteSurfSettingsWidget>(World, UKiteSurfSettingsWidget::StaticClass());
    if (Settings)
    {
        FKiteMenuNavigator& Navigator = Settings->GetNavigator();
        TestEqual(TEXT("Settings has thirteen items"), Navigator.Num(), 13);
        TestEqual(TEXT("and opens on the first"), Navigator.GetSelected(), 0);

        Settings->OnVolumeSliderChanged(1.0f);
        Navigator.HandleKey(EKeys::Left);
        Navigator.HandleKey(EKeys::Left);
        TestNearlyEqual(TEXT("Left on the volume turns it down a step at a time"), Settings->CurrentVolume, 0.9f, 0.001f);
        Navigator.HandleKey(EKeys::Right);
        Navigator.HandleKey(EKeys::Right);
        Navigator.HandleKey(EKeys::Right);
        TestNearlyEqual(TEXT("Right turns it up, stopping at full"), Settings->CurrentVolume, 1.0f, 0.001f);

        // Music volume is the second row.
        Navigator.Select(1);
        Settings->OnMusicSliderChanged(0.5f);
        Navigator.HandleKey(EKeys::Right);
        TestNearlyEqual(TEXT("Right on MUSIC VOLUME turns it up a step"), Settings->CurrentMusicVolume, 0.55f, 0.001f);
        Navigator.HandleKey(EKeys::Left);
        Navigator.HandleKey(EKeys::Left);
        TestNearlyEqual(TEXT("and left turns it down"), Settings->CurrentMusicVolume, 0.45f, 0.001f);

        // Ambient and effects volume follow it.
        Settings->OnAmbientSliderChanged(1.0f);
        Settings->OnEffectsSliderChanged(1.0f);
        Navigator.Select(2);
        Navigator.HandleKey(EKeys::Left);
        TestNearlyEqual(TEXT("Left on AMBIENT VOLUME turns it down a step"), Settings->CurrentAmbientVolume, 0.95f, 0.001f);
        Navigator.Select(3);
        Navigator.HandleKey(EKeys::Left);
        Navigator.HandleKey(EKeys::Left);
        TestNearlyEqual(TEXT("and on EFFECTS VOLUME"), Settings->CurrentEffectsVolume, 0.9f, 0.001f);
        TestNearlyEqual(TEXT("leaving the others alone"), Settings->CurrentAmbientVolume, 0.95f, 0.001f);

        Navigator.Select(6);
        const bool bMotionBefore = Settings->bMotionBar;
        Navigator.HandleKey(EKeys::Enter);
        TestNotEqual(TEXT("Accept on MOTION BAR switches it"), Settings->bMotionBar, bMotionBefore);
        Navigator.HandleKey(EKeys::Right);
        TestEqual(TEXT("and so does right"), Settings->bMotionBar, bMotionBefore);

        Navigator.Select(7);
        const EMotionSheetMode ModeBefore = Settings->MotionSheetMode;
        Navigator.HandleKey(EKeys::Enter);
        TestNotEqual(TEXT("Accept on MOTION POWER switches between tilt and move"), Settings->MotionSheetMode, ModeBefore);
        Navigator.HandleKey(EKeys::Enter);
        TestEqual(TEXT("and back"), Settings->MotionSheetMode, ModeBefore);

        Navigator.Select(8);
        const bool bBarReturnsBefore = Settings->bBarReturnsToMiddle;
        Navigator.HandleKey(EKeys::Enter);
        TestNotEqual(TEXT("Accept on BAR TO MIDDLE switches it"), Settings->bBarReturnsToMiddle, bBarReturnsBefore);

        Navigator.Select(9);
        const bool bHapticsBefore = Settings->bHaptics;
        Navigator.HandleKey(EKeys::Enter);
        TestNotEqual(TEXT("Accept on VIBRATION switches it"), Settings->bHaptics, bHapticsBefore);

        Navigator.Select(11);
        Settings->SetQualityPreset(1);
        Navigator.HandleKey(EKeys::Right);
        TestEqual(TEXT("Right on QUALITY steps it up"), Settings->CurrentQualityPreset, 2);
        Navigator.HandleKey(EKeys::Left);
        Navigator.HandleKey(EKeys::Left);
        Navigator.HandleKey(EKeys::Left);
        TestEqual(TEXT("Left steps it down, stopping at the lowest"), Settings->CurrentQualityPreset, 0);
    }

    // Gear: wind, kite size, kite, board, rider, sandbars, islands, sharks, RIDE, BACK.
    UKiteSurfGearWidget* Gear = CreateWidget<UKiteSurfGearWidget>(World, UKiteSurfGearWidget::StaticClass());
    if (Gear)
    {
        FKiteMenuNavigator& Navigator = Gear->GetNavigator();
        TestEqual(TEXT("The gear screen has eleven items"), Navigator.Num(), 11);
        TestEqual(TEXT("and opens on RIDE, so accept starts the ride"), Navigator.GetSelected(), 9);

        Navigator.Select(0);
        Gear->SetWindKnots(20.0f);
        Navigator.HandleKey(EKeys::Right);
        Navigator.HandleKey(EKeys::Right);
        TestEqual(TEXT("Right on WIND adds a knot each press"), Gear->CurrentWindKnots, 22.0f);
        Navigator.HandleKey(EKeys::Gamepad_DPad_Left);
        TestEqual(TEXT("Left takes one off"), Gear->CurrentWindKnots, 21.0f);

        Navigator.HandleKey(EKeys::Down);
        Gear->SetKiteSizeM2(9.0f);
        Navigator.HandleKey(EKeys::Left);
        TestEqual(TEXT("Left on KITE SIZE steps to the next size down"), Gear->CurrentKiteSizeM2, 8.0f);
        Navigator.HandleKey(EKeys::Right);
        Navigator.HandleKey(EKeys::Right);
        TestEqual(TEXT("Right steps up"), Gear->CurrentKiteSizeM2, 10.0f);
        Gear->SetKiteSizeM2(2.0f);
        Navigator.HandleKey(EKeys::Left);
        TestEqual(TEXT("Below the smallest is the recommended size"), Gear->CurrentKiteSizeM2, 0.0f);
        Navigator.HandleKey(EKeys::Left);
        TestEqual(TEXT("and below that wraps to the biggest"), Gear->CurrentKiteSizeM2, 17.0f);

        Navigator.HandleKey(EKeys::Down);
        const EKiteModel ModelBefore = Gear->CurrentKiteModel;
        Navigator.HandleKey(EKeys::Enter);
        TestNotEqual(TEXT("Accept on KITE changes the model"), Gear->CurrentKiteModel, ModelBefore);

        Navigator.Select(8);
        const bool bSharksBefore = Gear->bSharks;
        Navigator.HandleKey(EKeys::Gamepad_FaceButton_Bottom);
        TestNotEqual(TEXT("Accept on SHARKS switches them"), Gear->bSharks, bSharksBefore);
    }

    World->DestroyWorld(false);
    return true;
}

