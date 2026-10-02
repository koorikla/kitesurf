#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfSaveGame.h"

UKiteSurfGameInstance::UKiteSurfGameInstance()
	: PendingWindKnots(15.0f)
	, MasterVolume(1.0f)
{
}

void UKiteSurfGameInstance::Init()
{
	Super::Init();
	LoadSettingsFromDisk();
}

void UKiteSurfGameInstance::LoadSettingsFromDisk()
{
	UKiteSurfSaveGame* SaveGame = UKiteSurfSaveGame::LoadOrCreateSettings();
	if (SaveGame)
	{
		PendingWindKnots = FMath::Clamp(SaveGame->WindStrengthKnots, 8.0f, 30.0f);
		MasterVolume = FMath::Clamp(SaveGame->MasterVolume, 0.0f, 1.0f);
	}
}

void UKiteSurfGameInstance::SaveSettingsToDisk()
{
	UKiteSurfSaveGame* SaveGame = UKiteSurfSaveGame::LoadOrCreateSettings();
	if (SaveGame)
	{
		SaveGame->WindStrengthKnots = FMath::Clamp(PendingWindKnots, 8.0f, 30.0f);
		SaveGame->MasterVolume = FMath::Clamp(MasterVolume, 0.0f, 1.0f);
		SaveGame->SaveSettings();
	}
}

void UKiteSurfGameInstance::SetPendingWindKnots(float InKnots)
{
	PendingWindKnots = FMath::Clamp(InKnots, 8.0f, 30.0f);
}

void UKiteSurfGameInstance::SetMasterVolume(float InVolume)
{
	MasterVolume = FMath::Clamp(InVolume, 0.0f, 1.0f);
}
