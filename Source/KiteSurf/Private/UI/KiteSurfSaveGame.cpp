#include "UI/KiteSurfSaveGame.h"
#include "Kismet/GameplayStatics.h"

const FString UKiteSurfSaveGame::DefaultSaveSlot = TEXT("Settings");
const int32 UKiteSurfSaveGame::DefaultUserIndex = 0;

UKiteSurfSaveGame::UKiteSurfSaveGame()
	: WindStrengthKnots(20.0f)
	, MasterVolume(1.0f)
	, MusicVolume(0.6f)
	, AmbientVolume(1.0f)
	, EffectsVolume(1.0f)
	, bSkipOnboarding(false)
	, bOnboardingCompleted(false)
	, RiderCharacterIndex(0)
	, KiteSizeM2(9.0f)
	, KiteModelIndex(0)
	, BoardSizeIndex(1)
	, bMotionBar(false)
	, bHaptics(true)
	, bSpotIslands(true)
	, bSpotSandbars(true)
	, bSpotSharks(true)
{
}

UKiteSurfSaveGame* UKiteSurfSaveGame::LoadOrCreateSettings(const FString& SlotName, int32 UserIndex)
{
	if (UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
	{
		if (USaveGame* Loaded = UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex))
		{
			if (UKiteSurfSaveGame* SaveGame = Cast<UKiteSurfSaveGame>(Loaded))
			{
				SaveGame->WindStrengthKnots = FMath::Clamp(SaveGame->WindStrengthKnots, 8.0f, 40.0f);
				SaveGame->MasterVolume = FMath::Clamp(SaveGame->MasterVolume, 0.0f, 1.0f);
				return SaveGame;
			}
		}
	}

	UKiteSurfSaveGame* NewSave = NewObject<UKiteSurfSaveGame>();
	NewSave->WindStrengthKnots = 20.0f;
	NewSave->MasterVolume = 1.0f;
	NewSave->bSkipOnboarding = false;
	NewSave->bOnboardingCompleted = false;
	return NewSave;
}

bool UKiteSurfSaveGame::SaveSettings(const FString& SlotName, int32 UserIndex)
{
	WindStrengthKnots = FMath::Clamp(WindStrengthKnots, 8.0f, 40.0f);
	MasterVolume = FMath::Clamp(MasterVolume, 0.0f, 1.0f);
	return UGameplayStatics::SaveGameToSlot(this, SlotName, UserIndex);
}
