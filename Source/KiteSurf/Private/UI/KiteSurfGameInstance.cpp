#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfSaveGame.h"
#include "Misc/App.h"

UKiteSurfGameInstance::UKiteSurfGameInstance()
	: PendingWindKnots(15.0f)
	, MasterVolume(1.0f)
	, bSkipOnboarding(false)
	, bOnboardingCompleted(false)
	, RiderCharacter(ERiderCharacter::Santa)
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
		bSkipOnboarding = SaveGame->bSkipOnboarding;
		bOnboardingCompleted = SaveGame->bOnboardingCompleted;
		RiderCharacter = RiderCharacter::FromIndex(SaveGame->RiderCharacterIndex);
		FApp::SetVolumeMultiplier(MasterVolume);
	}
}

void UKiteSurfGameInstance::SaveSettingsToDisk()
{
	UKiteSurfSaveGame* SaveGame = UKiteSurfSaveGame::LoadOrCreateSettings();
	if (SaveGame)
	{
		SaveGame->WindStrengthKnots = FMath::Clamp(PendingWindKnots, 8.0f, 30.0f);
		SaveGame->MasterVolume = FMath::Clamp(MasterVolume, 0.0f, 1.0f);
		SaveGame->bSkipOnboarding = bSkipOnboarding;
		SaveGame->bOnboardingCompleted = bOnboardingCompleted;
		SaveGame->RiderCharacterIndex = static_cast<int32>(RiderCharacter);
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
	FApp::SetVolumeMultiplier(MasterVolume);
}

void UKiteSurfGameInstance::SetSkipOnboarding(bool bInSkip)
{
	bSkipOnboarding = bInSkip;
}

void UKiteSurfGameInstance::SetOnboardingCompleted(bool bInCompleted)
{
	bOnboardingCompleted = bInCompleted;
}

void UKiteSurfGameInstance::SetRiderCharacter(ERiderCharacter InCharacter)
{
	RiderCharacter = RiderCharacter::FromIndex(static_cast<int32>(InCharacter));
}
