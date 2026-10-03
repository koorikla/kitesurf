#include "UI/KiteSurfGameInstance.h"
#include "KiteGear.h"
#include "KiteComponent.h"
#include "UI/KiteSurfSaveGame.h"
#include "Tricks/JumpRecord.h"
#include "Misc/App.h"

UKiteSurfGameInstance::UKiteSurfGameInstance()
	: PendingWindKnots(20.0f)
	, MasterVolume(1.0f)
	, MusicVolume(0.6f)
	, AmbientVolume(1.0f)
	, EffectsVolume(1.0f)
	, bSkipOnboarding(false)
	, bOnboardingCompleted(false)
	, RiderCharacter(ERiderCharacter::Santa)
	, bMotionBar(false)
	, bHaptics(true)
	, bSpotIslands(true)
	, bSpotSandbars(true)
	, bSpotSharks(true)
	, KiteModel(EKiteModel::Loop)
	, BoardSize(EBoardSize::Medium)
	, KiteSizeM2(9.0f)
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
		ApplySaveGame(*SaveGame);
	}
}

void UKiteSurfGameInstance::ApplySaveGame(const UKiteSurfSaveGame& SaveGame)
{
	PendingWindKnots = FMath::Clamp(SaveGame.WindStrengthKnots, KiteGear::MinWindKnots, KiteGear::MaxWindKnots);
	MasterVolume = FMath::Clamp(SaveGame.MasterVolume, 0.0f, 1.0f);
	MusicVolume = FMath::Clamp(SaveGame.MusicVolume, 0.0f, 1.0f);
	AmbientVolume = FMath::Clamp(SaveGame.AmbientVolume, 0.0f, 1.0f);
	EffectsVolume = FMath::Clamp(SaveGame.EffectsVolume, 0.0f, 1.0f);
	bSkipOnboarding = SaveGame.bSkipOnboarding;
	bOnboardingCompleted = SaveGame.bOnboardingCompleted;
	RiderCharacter = RiderCharacter::FromIndex(SaveGame.RiderCharacterIndex);
	SetKiteSizeM2(SaveGame.KiteSizeM2);
	KiteModel = KiteGear::KiteModelFromIndex(SaveGame.KiteModelIndex);
	bMotionBar = SaveGame.bMotionBar;
	bHaptics = SaveGame.bHaptics;
	SetSpotFeatures(SaveGame.bSpotIslands, SaveGame.bSpotSandbars, SaveGame.bSpotSharks);
	BoardSize = KiteGear::BoardSizeFromIndex(SaveGame.BoardSizeIndex);
	TrickBook.SetEntries(SaveGame.TrickBook.GetEntries());
	BestSessionTotalBySeconds = SaveGame.BestSessionTotalBySeconds;
	FApp::SetVolumeMultiplier(MasterVolume);
}

void UKiteSurfGameInstance::SaveSettingsToDisk()
{
	UKiteSurfSaveGame* SaveGame = UKiteSurfSaveGame::LoadOrCreateSettings();
	if (SaveGame)
	{
		WriteToSaveGame(*SaveGame);
		SaveGame->SaveSettings();
	}
}

void UKiteSurfGameInstance::WriteToSaveGame(UKiteSurfSaveGame& SaveGame) const
{
	SaveGame.WindStrengthKnots = FMath::Clamp(PendingWindKnots, KiteGear::MinWindKnots, KiteGear::MaxWindKnots);
	SaveGame.MasterVolume = FMath::Clamp(MasterVolume, 0.0f, 1.0f);
	SaveGame.MusicVolume = FMath::Clamp(MusicVolume, 0.0f, 1.0f);
	SaveGame.AmbientVolume = FMath::Clamp(AmbientVolume, 0.0f, 1.0f);
	SaveGame.EffectsVolume = FMath::Clamp(EffectsVolume, 0.0f, 1.0f);
	SaveGame.bSkipOnboarding = bSkipOnboarding;
	SaveGame.bOnboardingCompleted = bOnboardingCompleted;
	SaveGame.RiderCharacterIndex = static_cast<int32>(RiderCharacter);
	SaveGame.KiteSizeM2 = KiteSizeM2;
	SaveGame.KiteModelIndex = static_cast<int32>(KiteModel);
	SaveGame.bMotionBar = bMotionBar;
	SaveGame.bHaptics = bHaptics;
	SaveGame.bSpotIslands = bSpotIslands;
	SaveGame.bSpotSandbars = bSpotSandbars;
	SaveGame.bSpotSharks = bSpotSharks;
	SaveGame.BoardSizeIndex = static_cast<int32>(BoardSize);
	SaveGame.TrickBook = TrickBook;
	SaveGame.BestSessionTotalBySeconds = BestSessionTotalBySeconds;
}

bool UKiteSurfGameInstance::RecordTrickLanding(const FJumpRecord& Record)
{
	return TrickBook.RecordLanding(Record);
}

float UKiteSurfGameInstance::GetBestSessionTotal(int32 DurationSeconds) const
{
	const float* Best = BestSessionTotalBySeconds.Find(DurationSeconds);
	return Best ? *Best : 0.0f;
}

bool UKiteSurfGameInstance::RecordSessionTotal(int32 DurationSeconds, float Total)
{
	const float* Best = BestSessionTotalBySeconds.Find(DurationSeconds);
	if (Total <= 0.0f || (Best && Total <= *Best))
	{
		return false;
	}
	BestSessionTotalBySeconds.Add(DurationSeconds, Total);
	return true;
}

void UKiteSurfGameInstance::SetPendingWindKnots(float InKnots)
{
	PendingWindKnots = FMath::Clamp(InKnots, KiteGear::MinWindKnots, KiteGear::MaxWindKnots);
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

void UKiteSurfGameInstance::SetKiteModel(EKiteModel InModel)
{
	KiteModel = KiteGear::KiteModelFromIndex(static_cast<int32>(InModel));
}

void UKiteSurfGameInstance::SetBoardSize(EBoardSize InSize)
{
	BoardSize = KiteGear::BoardSizeFromIndex(static_cast<int32>(InSize));
}

void UKiteSurfGameInstance::SetSpotFeatures(bool bIslands, bool bSandbars, bool bSharks)
{
	bSpotIslands = bIslands;
	bSpotSandbars = bSandbars;
	bSpotSharks = bSharks;
}

void UKiteSurfGameInstance::SetMotionBar(bool bEnabled)
{
	bMotionBar = bEnabled;
}

void UKiteSurfGameInstance::SetHaptics(bool bEnabled)
{
	bHaptics = bEnabled;
}

void UKiteSurfGameInstance::SetMusicVolume(float InVolume)
{
	MusicVolume = FMath::Clamp(InVolume, 0.0f, 1.0f);
}

void UKiteSurfGameInstance::SetAmbientVolume(float InVolume)
{
	AmbientVolume = FMath::Clamp(InVolume, 0.0f, 1.0f);
}

void UKiteSurfGameInstance::SetEffectsVolume(float InVolume)
{
	EffectsVolume = FMath::Clamp(InVolume, 0.0f, 1.0f);
}
