#include "UI/KiteSurfGameInstance.h"
#include "KiteComponent.h"
#include "UI/KiteSurfSaveGame.h"
#include "Misc/App.h"

UKiteSurfGameInstance::UKiteSurfGameInstance()
	: PendingWindKnots(15.0f)
	, MasterVolume(1.0f)
	, bSkipOnboarding(false)
	, bOnboardingCompleted(false)
	, RiderCharacter(ERiderCharacter::Santa)
	, KiteSizeM2(0.0f)
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
		PendingWindKnots = FMath::Clamp(SaveGame->WindStrengthKnots, 8.0f, 40.0f);
		MasterVolume = FMath::Clamp(SaveGame->MasterVolume, 0.0f, 1.0f);
		bSkipOnboarding = SaveGame->bSkipOnboarding;
		bOnboardingCompleted = SaveGame->bOnboardingCompleted;
		RiderCharacter = RiderCharacter::FromIndex(SaveGame->RiderCharacterIndex);
		SetKiteSizeM2(SaveGame->KiteSizeM2);
		FApp::SetVolumeMultiplier(MasterVolume);
	}
}

void UKiteSurfGameInstance::SaveSettingsToDisk()
{
	UKiteSurfSaveGame* SaveGame = UKiteSurfSaveGame::LoadOrCreateSettings();
	if (SaveGame)
	{
		SaveGame->WindStrengthKnots = FMath::Clamp(PendingWindKnots, 8.0f, 40.0f);
		SaveGame->MasterVolume = FMath::Clamp(MasterVolume, 0.0f, 1.0f);
		SaveGame->bSkipOnboarding = bSkipOnboarding;
		SaveGame->bOnboardingCompleted = bOnboardingCompleted;
		SaveGame->RiderCharacterIndex = static_cast<int32>(RiderCharacter);
		SaveGame->KiteSizeM2 = KiteSizeM2;
		SaveGame->SaveSettings();
	}
}

void UKiteSurfGameInstance::SetPendingWindKnots(float InKnots)
{
	PendingWindKnots = FMath::Clamp(InKnots, 8.0f, 40.0f);
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

void UKiteSurfGameInstance::SetKiteSizeM2(float InSizeM2)
{
	// Anything that is not one of the sizes on offer means "recommend one".
	KiteSizeM2 = UKiteComponent::GetKiteSizesM2().Contains(InSizeM2) ? InSizeM2 : 0.0f;
}

float UKiteSurfGameInstance::GetEffectiveKiteSizeM2() const
{
	return KiteSizeM2 > 0.0f ? KiteSizeM2 : UKiteComponent::RecommendKiteSizeM2(PendingWindKnots);
}
