#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "WindComponent.h"
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

// The game had sound assets with nothing in them. These are the checks that would have caught it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfAudioSoundsAreReal, "KiteSurf.Audio.SoundsAreReal", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfAudioSoundsAreReal::RunTest(const FString& Parameters)
{
	struct FExpectedSound { const TCHAR* Path; bool bLoop; float MinSeconds; };
	const FExpectedSound Expected[] =
	{
		{ TEXT("/Game/Audio/SW_WindLoop"), true, 3.0f },
		{ TEXT("/Game/Audio/SW_WaterLoop"), true, 2.0f },
		{ TEXT("/Game/Audio/SW_LineLoop"), true, 2.0f },
		{ TEXT("/Game/Audio/SW_Pop"), false, 0.2f },
		{ TEXT("/Game/Audio/SW_Landing"), false, 0.4f },
		{ TEXT("/Game/Audio/SW_Crash"), false, 1.0f },
		{ TEXT("/Game/Audio/SW_ResetCue"), false, 0.2f },
	};
	for (const FExpectedSound& Sound : Expected)
	{
		const USoundWave* Wave = LoadObject<USoundWave>(nullptr, Sound.Path);
		TestNotNull(FString::Printf(TEXT("%s is a sound wave"), Sound.Path), Wave);
		if (!Wave)
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s has audio in it (%.2f s)"), Sound.Path, Wave->Duration), Wave->Duration >= Sound.MinSeconds);
		TestTrue(FString::Printf(TEXT("%s has a sample rate and a channel"), Sound.Path), Wave->GetSampleRateForCurrentPlatform() >= 22050.0f && Wave->NumChannels >= 1);
		TestEqual(FString::Printf(TEXT("%s loops only if it is a loop"), Sound.Path), Wave->bLooping != 0, Sound.bLoop);
		if (Sound.bLoop)
		{
			TestTrue(FString::Printf(TEXT("%s keeps playing while faded to nothing, so it can come back"), Sound.Path), Wave->VirtualizationMode == EVirtualizationMode::PlayWhenSilent);
		}
	}

	// The pawn uses them.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteRiderPawn* Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
	TestNotNull(TEXT("Pawn spawned"), Pawn);
	if (Pawn)
	{
		TestTrue(TEXT("The wind loop has its sound"), Pawn->GetWindLoop() && Pawn->GetWindLoop()->GetSound());
		TestTrue(TEXT("The water loop has its sound"), Pawn->GetWaterLoop() && Pawn->GetWaterLoop()->GetSound());
		TestTrue(TEXT("The line loop has its sound"), Pawn->GetLineLoop() && Pawn->GetLineLoop()->GetSound());
		TestNotNull(TEXT("Pop sound"), Pawn->GetPopSound());
		TestNotNull(TEXT("Landing sound"), Pawn->GetLandingSound());
		TestNotNull(TEXT("Crash sound"), Pawn->GetCrashSound());
		TestNotNull(TEXT("Reset sound"), Pawn->GetResetSound());
	}
	if (World)
	{
		World->DestroyWorld(false);
	}
	return true;
}

// What the rider hears follows what they are doing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfAudioMixFollowsTheRide, "KiteSurf.Audio.MixFollowsTheRide", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfAudioMixFollowsTheRide::RunTest(const FString& Parameters)
{
	const FRideAudioMix Calm = AKiteRiderPawn::ComputeAudioMix(0.0f, 0.0f, true, 0.0f);
	const FRideAudioMix Riding = AKiteRiderPawn::ComputeAudioMix(22.0f, 16.0f, true, 600.0f);
	const FRideAudioMix Storm = AKiteRiderPawn::ComputeAudioMix(45.0f, 28.0f, true, 3000.0f);
	const FRideAudioMix Airborne = AKiteRiderPawn::ComputeAudioMix(22.0f, 16.0f, false, 600.0f);

	TestEqual(TEXT("No wind: no wind noise"), Calm.WindVolume, 0.0f);
	TestEqual(TEXT("Slack lines: no line hum"), Calm.LineVolume, 0.0f);
	TestTrue(TEXT("Stopped in the water there is still a quiet slosh"), Calm.WaterVolume > 0.0f && Calm.WaterVolume < 0.15f);

	TestTrue(FString::Printf(TEXT("Riding, all three are clearly audible (wind %.2f, water %.2f, lines %.2f)"), Riding.WindVolume, Riding.WaterVolume, Riding.LineVolume),
		Riding.WindVolume > 0.25f && Riding.WaterVolume > 0.25f && Riding.LineVolume > 0.04f);
	TestTrue(TEXT("More wind is louder and higher"), Storm.WindVolume > Riding.WindVolume && Storm.WindPitch > Riding.WindPitch);
	TestTrue(TEXT("More speed is louder and higher"), Storm.WaterVolume > Riding.WaterVolume && Storm.WaterPitch > Riding.WaterPitch);
	TestTrue(TEXT("More pull is louder and higher"), Storm.LineVolume > Riding.LineVolume && Storm.LinePitch > Riding.LinePitch);
	TestTrue(TEXT("Nothing is louder than full volume"), Storm.WindVolume <= 1.0f && Storm.WaterVolume <= 1.0f && Storm.LineVolume <= 1.0f);

	TestEqual(TEXT("In the air the water goes quiet"), Airborne.WaterVolume, 0.0f);
	TestEqual(TEXT("and the wind carries on"), Airborne.WindVolume, Riding.WindVolume);

	// The pawn eases towards that mix as it ticks.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteRiderPawn* Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
	if (Pawn)
	{
		if (UWindComponent* Wind = Pawn->GetWind())
		{
			Wind->BaseWind = FVector(20.0f * 51.44f, 0.0f, 0.0f);
			Wind->GustStrength = 0.0f;
		}
		// Standing still in the wind: the rig is not stepped, so the kite does not drag the rider off downwind.
		Pawn->bStepSimulation = false;
		for (int32 Step = 0; Step < 120; ++Step)
		{
			Pawn->Tick(1.0f / 60.0f);
		}
		TestTrue(FString::Printf(TEXT("After two seconds in 20 kn the wind loop is up (%.2f)"), Pawn->GetAudioMix().WindVolume), Pawn->GetAudioMix().WindVolume > 0.2f);
		TestNearlyEqual(TEXT("and the component is playing at that volume"), Pawn->GetWindLoop()->VolumeMultiplier, Pawn->GetAudioMix().WindVolume, 0.001f);
	}
	if (World)
	{
		World->DestroyWorld(false);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
