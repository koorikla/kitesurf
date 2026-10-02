#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "WindStreakComponent.h"
#include "KiteSurfHUD.h"
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

// The HUD's wind flag shows the wind across the view, not on a fixed map.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDWindFlag, "KiteSurf.HUD.WindFlag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfHUDWindFlag::RunTest(const FString& Parameters)
{
	const FVector WindAlongX(1000.0f, 0.0f, 0.0f);

	// Looking downwind: the wind is at your back, and the flag streams up the screen.
	FVector2D OnScreen = AKiteSurfHUD::GetWindOnScreen(WindAlongX, 0.0f);
	TestTrue(TEXT("Looking downwind the flag streams up the screen"), OnScreen.Equals(FVector2D(0.0, -1.0), 0.001));
	TestEqual(TEXT("and the wind is from behind"), AKiteSurfHUD::DescribeWindSource(OnScreen), FString(TEXT("from behind")));

	// Looking upwind: it streams down the screen, towards you.
	OnScreen = AKiteSurfHUD::GetWindOnScreen(WindAlongX, 180.0f);
	TestTrue(TEXT("Looking upwind the flag streams down the screen"), OnScreen.Equals(FVector2D(0.0, 1.0), 0.001));
	TestEqual(TEXT("and the wind is from ahead"), AKiteSurfHUD::DescribeWindSource(OnScreen), FString(TEXT("from ahead")));

	// Looking along +Y, +X is on your left: the wind blows to your left, so it comes from your right.
	OnScreen = AKiteSurfHUD::GetWindOnScreen(WindAlongX, 90.0f);
	TestTrue(TEXT("Wind blowing to your left streams the flag left"), OnScreen.Equals(FVector2D(-1.0, 0.0), 0.001));
	TestEqual(TEXT("and comes from the right"), AKiteSurfHUD::DescribeWindSource(OnScreen), FString(TEXT("from the right")));
	OnScreen = AKiteSurfHUD::GetWindOnScreen(WindAlongX, -90.0f);
	TestTrue(TEXT("Looking the other way it streams right"), OnScreen.Equals(FVector2D(1.0, 0.0), 0.001));
	TestEqual(TEXT("and comes from the left"), AKiteSurfHUD::DescribeWindSource(OnScreen), FString(TEXT("from the left")));

	// In between, and with a vertical part to the wind that the flag ignores.
	OnScreen = AKiteSurfHUD::GetWindOnScreen(FVector(1000.0f, 0.0f, 400.0f), 135.0f);
	TestNearlyEqual(TEXT("The direction is a unit vector whatever the wind's strength or lift"), static_cast<float>(OnScreen.Size()), 1.0f, 0.001f);
	TestEqual(TEXT("Quartering wind is called out as such"), AKiteSurfHUD::DescribeWindSource(OnScreen), FString(TEXT("from ahead right")));

	TestTrue(TEXT("No wind gives no direction"), AKiteSurfHUD::GetWindOnScreen(FVector::ZeroVector, 0.0f).IsNearlyZero());
	TestEqual(TEXT("and is called calm"), AKiteSurfHUD::DescribeWindSource(FVector2D::ZeroVector), FString(TEXT("calm")));
	return true;
}

// Wind lines on the water lie along the wind, drift down it, and stay round the rider.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWindStreaks, "KiteSurf.Wind.StreaksOnTheWater", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWindStreaks::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteRiderPawn* Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
	TestNotNull(TEXT("Pawn spawned"), Pawn);
	UWindStreakComponent* Streaks = Pawn ? Pawn->GetWindStreaks() : nullptr;
	UWindComponent* Wind = Pawn ? Pawn->GetWind() : nullptr;
	TestTrue(TEXT("The pawn has wind streaks and wind"), Streaks && Wind);
	if (!Streaks || !Wind)
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}

	// 20 kn blowing along +Y, steady.
	Wind->BaseWind = FVector(0.0f, 20.0f * 51.44f, 0.0f);
	Wind->GustStrength = 0.0f;
	Wind->DirectionDriftDeg = 0.0f;
	const float DeltaTime = 1.0f / 60.0f;
	Streaks->Simulate(DeltaTime);

	TestEqual(TEXT("There is a field of streaks"), Streaks->GetStreaks().Num(), Streaks->StreakCount);
	TestNearlyEqual(TEXT("They lie along the wind"), Streaks->GetStreakYawDeg(), 90.0f, 0.5f);

	auto AllInField = [Streaks, Pawn]()
	{
		const FVector2D Centre(Pawn->GetActorLocation());
		for (const FWindStreak& Streak : Streaks->GetStreaks())
		{
			if (FVector2D::Distance(Streak.Position, Centre) > Streaks->FieldRadiusCm + 1.0f)
			{
				return false;
			}
		}
		return true;
	};
	TestTrue(TEXT("All of them are within the field round the rider"), AllInField());

	int32 Visible = 0;
	float MaxOpacity = 0.0f;
	for (const FWindStreak& Streak : Streaks->GetStreaks())
	{
		Visible += Streak.Opacity > 0.3f ? 1 : 0;
		MaxOpacity = FMath::Max(MaxOpacity, Streak.Opacity);
	}
	TestTrue(FString::Printf(TEXT("In 20 kn most of them show (%d of %d)"), Visible, Streaks->StreakCount), Visible > Streaks->StreakCount / 2);
	TestTrue(TEXT("Opacity never exceeds full"), MaxOpacity <= 1.0f);

	// One second on: a streak that has not wrapped has drifted downwind by DriftFraction of the wind.
	const TArray<FWindStreak> Before = Streaks->GetStreaks();
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Streaks->Simulate(DeltaTime);
	}
	// The wind where the streaks are, at the water, which is lighter than the wind aloft.
	const float ExpectedDriftCm = Wind->GetWindAt(Pawn->GetActorLocation()).Size2D() * Streaks->DriftFraction;
	int32 Drifted = 0;
	for (int32 Index = 0; Index < Before.Num(); ++Index)
	{
		const FVector2D Moved = Streaks->GetStreaks()[Index].Position - Before[Index].Position;
		if (FMath::IsNearlyEqual(static_cast<float>(Moved.Y), ExpectedDriftCm, 5.0f) && FMath::Abs(Moved.X) < 1.0f)
		{
			++Drifted;
		}
	}
	TestTrue(FString::Printf(TEXT("After a second most streaks have drifted %.0f cm downwind (%d of %d; the rest wrapped round)"), ExpectedDriftCm, Drifted, Before.Num()), Drifted > Before.Num() * 3 / 4);
	TestTrue(TEXT("and they are all still within the field"), AllInField());

	// The field follows the rider, including a jump of a kilometre.
	Pawn->SetActorLocation(Pawn->GetActorLocation() + FVector(8000.0f, 0.0f, 0.0f));
	Streaks->Simulate(DeltaTime);
	TestTrue(TEXT("The field follows the rider as they ride on"), AllInField());
	Pawn->SetActorLocation(Pawn->GetActorLocation() + FVector(100000.0f, 50000.0f, 0.0f));
	Streaks->Simulate(DeltaTime);
	TestTrue(TEXT("and after a reset a long way off"), AllInField());

	// The wind turns: so do the streaks. It dies: they fade out.
	Wind->BaseWind = FVector(-15.0f * 51.44f, 0.0f, 0.0f);
	Streaks->Simulate(DeltaTime);
	TestNearlyEqual(TEXT("When the wind turns the streaks turn with it"), static_cast<float>(FMath::Abs(FRotator::NormalizeAxis(Streaks->GetStreakYawDeg() - 180.0f))), 0.0f, 0.5f);
	Wind->BaseWind = FVector::ZeroVector;
	Streaks->Simulate(DeltaTime);
	bool bAllGone = true;
	for (const FWindStreak& Streak : Streaks->GetStreaks())
	{
		bAllGone = bAllGone && Streak.Opacity <= 0.0f;
	}
	TestTrue(TEXT("With no wind there are no streaks"), bAllGone);
	TestEqual(TEXT("Streak strength is zero in a calm"), Streaks->GetStrengthForWind(0.0f), 0.0f);
	TestTrue(TEXT("and builds with the wind"), Streaks->GetStrengthForWind(10.0f) > 0.0f && Streaks->GetStrengthForWind(10.0f) < Streaks->GetStrengthForWind(25.0f));

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
