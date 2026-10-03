#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "BoardMovementComponent.h"
#include "UI/KiteSurfMenuStyle.h"
#include "KiteMotionBar.h"
#include "KiteComponent.h"
#include "UI/KiteSurfGameInstance.h"
#include "WindStreakComponent.h"
#include "KiteSurfHUD.h"
#include "KiteSurfUnits.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "WindComponent.h"
#include "KiteRiderPawn.h"
#include "KiteComponent.h"
#include "BoardMovementComponent.h"

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

	if (Pawn && Pawn->GetKite() && Pawn->GetBoardMovement())
	{
		// The bar and the board take what they are given only within their travel.
		Pawn->SteerKite(2.0f);
		TestEqual(TEXT("SteerKite(2) clamps the bar to 1"), Pawn->GetCurrentSteerInput(), 1.0f);
		TestEqual(TEXT("and the kite gets 1"), Pawn->GetKite()->Steer, 1.0f);
		Pawn->SteerKite(-2.0f);
		TestEqual(TEXT("SteerKite(-2) clamps the bar to -1"), Pawn->GetCurrentSteerInput(), -1.0f);
		Pawn->SheetKite(1.5f);
		TestEqual(TEXT("SheetKite(1.5) clamps the bar to 1"), Pawn->GetCurrentSheetInput(), 1.0f);
		Pawn->SheetKite(-1.0f);
		TestEqual(TEXT("SheetKite(-1) clamps the bar to 0"), Pawn->GetCurrentSheetInput(), 0.0f);
		TestEqual(TEXT("and the kite gets 0"), Pawn->GetKite()->Sheet, 0.0f);
		Pawn->EdgeBoard(5.0f);
		TestEqual(TEXT("EdgeBoard(5) clamps the edge to 1"), Pawn->GetBoardMovement()->GetEdgeInput(), 1.0f);
		Pawn->EdgeBoard(-5.0f);
		TestEqual(TEXT("EdgeBoard(-5) clamps the edge to -1"), Pawn->GetBoardMovement()->GetEdgeInput(), -1.0f);
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
		{ TEXT("/Game/Audio/SW_Stomp"), false, 0.3f },
		{ TEXT("/Game/Audio/SW_ResetCue"), false, 0.2f },
		{ TEXT("/Game/Audio/SW_SprayLoop"), true, 2.0f },
		{ TEXT("/Game/Audio/SW_KiteLoop"), true, 2.0f },
		{ TEXT("/Game/Audio/SW_FlutterLoop"), true, 1.5f },
		{ TEXT("/Game/Audio/SW_RotationWhoosh"), true, 1.0f },
		{ TEXT("/Game/Audio/SW_KiteCrash"), false, 0.5f },
		{ TEXT("/Game/Audio/SW_Relaunch"), false, 0.5f },
		{ TEXT("/Game/Audio/SW_Aground"), false, 0.5f },
		{ TEXT("/Game/Audio/SW_Shark"), false, 0.8f },
		{ TEXT("/Game/Audio/SW_UIMove"), false, 0.05f },
		{ TEXT("/Game/Audio/SW_UISelect"), false, 0.2f },
		{ TEXT("/Game/Audio/SW_UIBack"), false, 0.2f },
		{ TEXT("/Game/Audio/MU_Menu"), true, 15.0f },
		{ TEXT("/Game/Audio/MU_RideBase"), true, 30.0f },
		{ TEXT("/Game/Audio/MU_RideAir"), true, 30.0f },
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

	// The ride's two music loops are played in step, so they must be exactly as long as each other, and stereo.
	const USoundWave* RideBase = LoadObject<USoundWave>(nullptr, TEXT("/Game/Audio/MU_RideBase"));
	const USoundWave* RideAir = LoadObject<USoundWave>(nullptr, TEXT("/Game/Audio/MU_RideAir"));
	if (RideBase && RideAir)
	{
		TestEqual(TEXT("The two ride music loops are the same length"), RideBase->Duration, RideAir->Duration);
		TestTrue(TEXT("and stereo"), RideBase->NumChannels == 2 && RideAir->NumChannels == 2);
	}
	TestNotNull(TEXT("The menus have their sounds"), KiteSurfMenuStyle::GetMenuSound(EKiteMenuSound::Move));
	TestTrue(TEXT("three different ones"), KiteSurfMenuStyle::GetMenuSound(EKiteMenuSound::Move) != KiteSurfMenuStyle::GetMenuSound(EKiteMenuSound::Select)
		&& KiteSurfMenuStyle::GetMenuSound(EKiteMenuSound::Select) != KiteSurfMenuStyle::GetMenuSound(EKiteMenuSound::Back));

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
		TestNotNull(TEXT("Stomp sound"), Pawn->GetStompSound());
		TestTrue(TEXT("The spray, kite and flutter loops have their sounds"), Pawn->GetSprayLoop() && Pawn->GetSprayLoop()->GetSound() && Pawn->GetKiteLoop() && Pawn->GetKiteLoop()->GetSound() && Pawn->GetFlutterLoop() && Pawn->GetFlutterLoop()->GetSound());
		TestTrue(TEXT("The rotation loop has its sound"), Pawn->GetRotationLoop() && Pawn->GetRotationLoop()->GetSound());
		TestTrue(TEXT("The ride has its two music loops"), Pawn->GetMusicBase() && Pawn->GetMusicBase()->GetSound() && Pawn->GetMusicAir() && Pawn->GetMusicAir()->GetSound());
		TestTrue(TEXT("and the music plays through a pause"), Pawn->GetMusicBase() && Pawn->GetMusicBase()->bIsUISound && Pawn->GetMusicAir()->bIsUISound);
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

	// A parked kite and no edge: none of the extra sounds.
	TestTrue(TEXT("Riding quietly there is no spray, kite roar, flutter or air music"), Riding.SprayVolume == 0.0f && Riding.KiteVolume == 0.0f && Riding.FlutterVolume == 0.0f && Riding.AirMusic == 0.0f);

	// The kite roars when it is flown fast, as through a loop.
	FRideAudioState Looping;
	Looping.ApparentWindKnots = 22.0f;
	Looping.BoardSpeedKnots = 16.0f;
	Looping.LineTensionN = 2500.0f;
	Looping.KiteAirspeedMS = 14.0f;
	TestEqual(TEXT("A parked kite is not heard"), AKiteRiderPawn::ComputeAudioMix(Looping).KiteVolume, 0.0f);
	Looping.KiteAirspeedMS = 28.0f;
	const FRideAudioMix Fast = AKiteRiderPawn::ComputeAudioMix(Looping);
	Looping.KiteAirspeedMS = 42.0f;
	const FRideAudioMix Faster = AKiteRiderPawn::ComputeAudioMix(Looping);
	TestTrue(FString::Printf(TEXT("A kite flown fast roars, more and higher the faster it goes (%.2f, %.2f)"), Fast.KiteVolume, Faster.KiteVolume), Fast.KiteVolume > 0.2f && Faster.KiteVolume > Fast.KiteVolume && Faster.KitePitch > Fast.KitePitch && Faster.KiteVolume <= 1.0f);

	// Spray comes off a driven edge at speed, and only on the water.
	FRideAudioState Carving;
	Carving.ApparentWindKnots = 22.0f;
	Carving.BoardSpeedKnots = 18.0f;
	Carving.EdgeEffort = 1.0f;
	const FRideAudioMix Sprayed = AKiteRiderPawn::ComputeAudioMix(Carving);
	TestTrue(FString::Printf(TEXT("A hard edge at speed throws spray (%.2f)"), Sprayed.SprayVolume), Sprayed.SprayVolume > 0.4f);
	Carving.EdgeEffort = 0.4f;
	TestTrue(TEXT("less of it for less edge"), AKiteRiderPawn::ComputeAudioMix(Carving).SprayVolume < Sprayed.SprayVolume);
	Carving.EdgeEffort = 1.0f;
	Carving.BoardSpeedKnots = 0.0f;
	TestEqual(TEXT("none standing still"), AKiteRiderPawn::ComputeAudioMix(Carving).SprayVolume, 0.0f);
	Carving.BoardSpeedKnots = 18.0f;
	Carving.bOnWater = false;
	Carving.bAirborne = true;
	const FRideAudioMix InTheAir = AKiteRiderPawn::ComputeAudioMix(Carving);
	TestEqual(TEXT("and none in the air"), InTheAir.SprayVolume, 0.0f);
	TestEqual(TEXT("In the air the music's second layer comes in"), InTheAir.AirMusic, 1.0f);

	// A canopy with no load in it flaps, if there is wind to flap it.
	FRideAudioState Luffing;
	Luffing.ApparentWindKnots = 20.0f;
	Luffing.KiteLuff = 1.0f;
	TestTrue(TEXT("A slack kite flaps"), AKiteRiderPawn::ComputeAudioMix(Luffing).FlutterVolume > 0.3f);
	Luffing.ApparentWindKnots = 0.0f;
	TestEqual(TEXT("but not in a calm"), AKiteRiderPawn::ComputeAudioMix(Luffing).FlutterVolume, 0.0f);

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
		TestTrue(FString::Printf(TEXT("After two seconds in 20 kn the wind loop is up (%.2f)"), Pawn->GetAudioMix().WindVolume), Pawn->GetAudioMix().WindVolume > 0.15f);
		TestNearlyEqual(TEXT("and the component is playing at that volume"), Pawn->GetWindLoop()->VolumeMultiplier, Pawn->GetAudioMix().WindVolume, 0.001f);

		// Music: the base layer follows the music volume, and the air layer comes in when the rider leaves the water.
		Pawn->SetMusicVolume(0.8f);
		Pawn->Tick(1.0f / 60.0f);
		TestTrue(FString::Printf(TEXT("The base layer plays at a level set by the music volume (%.2f)"), Pawn->GetMusicBase()->VolumeMultiplier), Pawn->GetMusicBase()->VolumeMultiplier > 0.2f && Pawn->GetMusicBase()->VolumeMultiplier < 0.8f);
		TestEqual(TEXT("On the water the air layer is silent"), Pawn->GetMusicAir()->VolumeMultiplier, 0.0f);
		TestEqual(TEXT("Neither layer is pitched, so they stay in step"), Pawn->GetMusicBase()->PitchMultiplier, Pawn->GetMusicAir()->PitchMultiplier);
		Pawn->GetBoardMovement()->SetBoardState(EBoardState::Airborne);
		for (int32 Step = 0; Step < 60; ++Step)
		{
			Pawn->Tick(1.0f / 60.0f);
		}
		TestTrue(FString::Printf(TEXT("A second in the air and the air layer is up (%.2f)"), Pawn->GetMusicAir()->VolumeMultiplier), Pawn->GetMusicAir()->VolumeMultiplier > 0.3f);
		Pawn->GetBoardMovement()->SetBoardState(EBoardState::Planing);
		Pawn->Tick(1.0f / 60.0f);
		TestTrue(TEXT("It lingers after landing rather than cutting off"), Pawn->GetMusicAir()->VolumeMultiplier > 0.25f);
		for (int32 Step = 0; Step < 240; ++Step)
		{
			Pawn->Tick(1.0f / 60.0f);
		}
		TestEqual(TEXT("and has gone four seconds later"), Pawn->GetMusicAir()->VolumeMultiplier, 0.0f);
		Pawn->SetMusicVolume(0.0f);
		Pawn->Tick(1.0f / 60.0f);
		TestEqual(TEXT("Music volume at nothing silences the base layer"), Pawn->GetMusicBase()->VolumeMultiplier, 0.0f);

		// The ambient volume scales what the loops play, not what the ride calls for.
		Pawn->SetAmbientVolume(0.5f);
		Pawn->Tick(1.0f / 60.0f);
		TestTrue(TEXT("There is wind to hear"), Pawn->GetAudioMix().WindVolume > 0.05f);
		TestNearlyEqual(TEXT("Ambient volume at half halves the wind loop"), Pawn->GetWindLoop()->VolumeMultiplier, 0.5f * Pawn->GetAudioMix().WindVolume, 0.001f);
		Pawn->SetAmbientVolume(0.0f);
		Pawn->Tick(1.0f / 60.0f);
		TestEqual(TEXT("and at nothing silences it"), Pawn->GetWindLoop()->VolumeMultiplier, 0.0f);
		Pawn->SetAmbientVolume(1.0f);

		// The effects volume scales the one-shots.
		Pawn->PlayRideSound(ERideSound::KiteCrash);
		const float FullVolume = Pawn->GetLastOneShotVolume();
		Pawn->SetEffectsVolume(0.5f);
		Pawn->PlayRideSound(ERideSound::KiteCrash);
		TestTrue(TEXT("A one-shot is played"), FullVolume > 0.0f);
		TestNearlyEqual(TEXT("Effects volume at half halves a one-shot"), Pawn->GetLastOneShotVolume(), 0.5f * FullVolume, 0.001f);
		Pawn->SetEffectsVolume(1.0f);

		// The kite hitting the water and coming back up have their own sounds.
		const int32 Before = Pawn->GetRideSoundCount();
		Pawn->PlayRideSound(ERideSound::KiteCrash);
		Pawn->PlayRideSound(ERideSound::Relaunch);
		TestEqual(TEXT("Ride sounds are played when asked for"), Pawn->GetRideSoundCount(), Before + 2);
		TestTrue(TEXT("the last being the relaunch"), Pawn->GetLastRideSound() == ERideSound::Relaunch);
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
	Wind->BaseWind = FVector(0.0f, KiteUnits::KnotsToCmS(20.0f), 0.0f);
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
	Wind->BaseWind = FVector(-KiteUnits::KnotsToCmS(15.0f), 0.0f, 0.0f);
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

namespace
{
	/** A controller held at a given roll and pitch, reporting what its sensors would. */
	class FScriptedMotionSource : public IKiteMotionSource
	{
	public:
		virtual bool Poll(FKiteMotionSample& OutSample) override
		{
			if (!bConnected)
			{
				return false;
			}
			OutSample = Sample;
			return true;
		}

		virtual FString GetDeviceName() const override { return bConnected ? TEXT("Scripted Controller") : FString(); }

		/** Holds the pad still at this attitude: roll right and pitch towards the player (deg). */
		void Hold(float RollDeg, float PitchDeg)
		{
			Sample.AccelG = UpFor(RollDeg, PitchDeg);
			Sample.GyroRadS = FVector::ZeroVector;
		}

		/** "Up" in the controller's axes for a pad rolled right and pulled in like a bar. */
		static FVector UpFor(float RollDeg, float PitchDeg)
		{
			const float Roll = FMath::DegreesToRadians(RollDeg);
			const float Pitch = FMath::DegreesToRadians(PitchDeg);
			return FVector(-FMath::Sin(Roll) * FMath::Cos(Pitch), FMath::Cos(Roll) * FMath::Cos(Pitch), FMath::Sin(Pitch)).GetSafeNormal();
		}

		FKiteMotionSample Sample;
		bool bConnected = true;
	};
}

// The filter that works out how the controller is held.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfMotionBarFilter, "KiteSurf.MotionBar.Filter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfMotionBarFilter::RunTest(const FString& Parameters)
{
	const float DeltaTime = 1.0f / 60.0f;
	FKiteMotionSample Sample;

	// Lying face up and still: level.
	FMotionBarFilter Filter;
	TestFalse(TEXT("No reading yet"), Filter.IsInitialized());
	Sample.AccelG = FVector(0.0f, 1.0f, 0.0f);
	Filter.Update(Sample, DeltaTime);
	TestTrue(TEXT("One reading is enough to start"), Filter.IsInitialized());
	TestNearlyEqual(TEXT("Face up is no roll"), Filter.GetRollDeg(), 0.0f, 0.01f);
	TestNearlyEqual(TEXT("and no pitch"), Filter.GetPitchDeg(), 0.0f, 0.01f);

	// Held still at an attitude, the accelerometer alone gets it there within a couple of seconds.
	Sample.AccelG = FScriptedMotionSource::UpFor(25.0f, 0.0f);
	for (int32 Step = 0; Step < 240; ++Step)
	{
		Filter.Update(Sample, DeltaTime);
	}
	TestNearlyEqual(TEXT("Right side down 25 deg reads as 25 deg of roll"), Filter.GetRollDeg(), 25.0f, 0.5f);
	TestNearlyEqual(TEXT("with no pitch"), Filter.GetPitchDeg(), 0.0f, 0.5f);

	Sample.AccelG = FScriptedMotionSource::UpFor(0.0f, 20.0f);
	for (int32 Step = 0; Step < 240; ++Step)
	{
		Filter.Update(Sample, DeltaTime);
	}
	TestNearlyEqual(TEXT("Pulled in 20 deg reads as 20 deg of pitch"), Filter.GetPitchDeg(), 20.0f, 0.5f);
	TestNearlyEqual(TEXT("with no roll"), Filter.GetRollDeg(), 0.0f, 0.5f);

	// The direction of "pulled in", pinned to the controller's own axes rather than to the test's
	// helper: checked on a DualSense, pulling the bar in raises the edge nearest the player, which
	// shows as "up" gaining a part along +Z. The other sign let the bar out when it was pulled.
	FMotionBarFilter Pulled;
	Sample.GyroRadS = FVector::ZeroVector;
	Sample.AccelG = FVector(0.0f, FMath::Cos(FMath::DegreesToRadians(20.0f)), FMath::Sin(FMath::DegreesToRadians(20.0f)));
	Pulled.Update(Sample, DeltaTime);
	TestNearlyEqual(TEXT("Up tilted towards +Z is the bar pulled in"), Pulled.GetPitchDeg(), 20.0f, 0.01f);
	FMotionBarMapping PullMapping;
	PullMapping.Calibrate(0.0f, 0.0f, 0.5f);
	TestTrue(TEXT("and pulling in adds power"), PullMapping.GetSheet(Pulled.GetPitchDeg()) > 0.5f);

	// A quick turn of the wrist: the gyro follows it at once, before the accelerometer has caught up.
	// Rolling right is turning about the axis that points at the player.
	FMotionBarFilter Quick;
	Sample.AccelG = FVector(0.0f, 1.0f, 0.0f);
	Sample.GyroRadS = FVector::ZeroVector;
	Quick.Update(Sample, DeltaTime);
	const float RollRateDegS = 200.0f;
	for (int32 Step = 0; Step < 6; ++Step) // a tenth of a second: 20 degrees
	{
		Sample.AccelG = FScriptedMotionSource::UpFor(RollRateDegS * DeltaTime * (Step + 1), 0.0f);
		Sample.GyroRadS = FVector(0.0f, 0.0f, -FMath::DegreesToRadians(RollRateDegS));
		Quick.Update(Sample, DeltaTime);
	}
	TestNearlyEqual(TEXT("A 20 deg flick of the wrist is followed without lag"), Quick.GetRollDeg(), 20.0f, 1.5f);

	// Shaken: the accelerometer reads far from 1 g and is ignored, so the attitude holds.
	const float Before = Quick.GetRollDeg();
	Sample.GyroRadS = FVector::ZeroVector;
	Sample.AccelG = FVector(2.5f, 0.3f, 0.0f);
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Quick.Update(Sample, DeltaTime);
	}
	TestNearlyEqual(TEXT("A shake does not move the estimate"), Quick.GetRollDeg(), Before, 0.01f);

	// Gyro drift with the pad still: the accelerometer holds the estimate to within a couple of degrees.
	FMotionBarFilter Drift;
	Sample.AccelG = FVector(0.0f, 1.0f, 0.0f);
	Sample.GyroRadS = FVector(0.0f, 0.0f, FMath::DegreesToRadians(2.0f)); // 2 deg/s of bias
	for (int32 Step = 0; Step < 60 * 30; ++Step)
	{
		Drift.Update(Sample, DeltaTime);
	}
	TestTrue(FString::Printf(TEXT("Half a minute of 2 deg/s gyro bias leaves under 2 deg of error (%.2f deg)"), Drift.GetRollDeg()), FMath::Abs(Drift.GetRollDeg()) < 2.0f);
	return true;
}

// Tilt becomes steering and bar position.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfMotionBarMapping, "KiteSurf.MotionBar.Mapping", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfMotionBarMapping::RunTest(const FString& Parameters)
{
	FMotionBarMapping Mapping;
	Mapping.Calibrate(0.0f, 0.0f, 0.5f);

	TestEqual(TEXT("Level is no steering"), Mapping.GetSteer(0.0f), 0.0f);
	TestEqual(TEXT("A degree or two of wobble is still no steering"), Mapping.GetSteer(Mapping.SteerDeadzoneDeg - 0.5f), 0.0f);
	TestTrue(TEXT("Right side down steers right"), Mapping.GetSteer(15.0f) > 0.2f && Mapping.GetSteer(15.0f) < 0.8f);
	TestNearlyEqual(TEXT("and left side down steers left by the same amount"), Mapping.GetSteer(-15.0f), -Mapping.GetSteer(15.0f), 0.0001f);
	TestEqual(TEXT("Full steering at SteerFullDeg"), Mapping.GetSteer(Mapping.SteerFullDeg), 1.0f);
	TestEqual(TEXT("and no more beyond it"), Mapping.GetSteer(80.0f), 1.0f);

	TestEqual(TEXT("Level leaves the bar where it was"), Mapping.GetSheet(0.0f), 0.5f);
	TestNearlyEqual(TEXT("Tipping towards you pulls the bar in"), Mapping.GetSheet(10.0f), 0.5f + 10.0f / Mapping.SheetRangeDeg, 0.0001f);
	TestNearlyEqual(TEXT("Tipping away lets it out"), Mapping.GetSheet(-10.0f), 0.5f - 10.0f / Mapping.SheetRangeDeg, 0.0001f);
	TestEqual(TEXT("The bar stops at fully in"), Mapping.GetSheet(60.0f), 1.0f);
	TestEqual(TEXT("and at fully out"), Mapping.GetSheet(-60.0f), 0.0f);

	// Centred with the pad held some other way and the bar somewhere else: that is the new level.
	Mapping.Calibrate(12.0f, 35.0f, 0.8f);
	TestEqual(TEXT("After centring, the way it is held is no steering"), Mapping.GetSteer(12.0f), 0.0f);
	TestNearlyEqual(TEXT("and the bar is where it was"), Mapping.GetSheet(35.0f), 0.8f, 0.0001f);
	TestTrue(TEXT("Steering is measured from there"), Mapping.GetSteer(12.0f + 20.0f) > 0.0f && Mapping.GetSteer(12.0f - 20.0f) < 0.0f);
	return true;
}

// On the rider: the motion bar takes over the bar while a controller is there, and hands it back.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfMotionBarOnThePawn, "KiteSurf.MotionBar.OnThePawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfMotionBarOnThePawn::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteRiderPawn* Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
	TestNotNull(TEXT("Pawn spawned"), Pawn);
	if (!Pawn)
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}
	const float DeltaTime = 1.0f / 60.0f;
	auto Tick = [Pawn, DeltaTime](int32 Steps)
	{
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Pawn->Tick(DeltaTime);
		}
	};

	const TSharedPtr<FScriptedMotionSource> Controller = MakeShared<FScriptedMotionSource>();
	Controller->Hold(8.0f, 30.0f); // however it happens to be held
	Pawn->SheetKite(0.7f);
	Pawn->SetMotionSource(Controller);

	// Off by default: the controller's tilt does nothing.
	TestFalse(TEXT("The motion bar is off by default"), Pawn->IsMotionBarEnabled());
	Controller->Hold(40.0f, 60.0f);
	Tick(30);
	TestFalse(TEXT("Off, it is not active"), Pawn->IsMotionBarActive());
	TestEqual(TEXT("and tilt does not steer"), Pawn->GetCurrentSteerInput(), 0.0f);
	TestEqual(TEXT("or move the bar"), Pawn->GetCurrentSheetInput(), 0.7f);

	// Switched on: however the pad is held at that moment is level, with the bar where it was.
	Controller->Hold(8.0f, 30.0f);
	Pawn->SetMotionBarEnabled(true);
	Tick(5);
	TestTrue(TEXT("Switched on with a controller, it is active"), Pawn->IsMotionBarActive());
	TestEqual(TEXT("It names the controller"), Pawn->GetMotionDeviceName(), FString(TEXT("Scripted Controller")));
	TestNearlyEqual(TEXT("Switching on does not steer"), Pawn->GetCurrentSteerInput(), 0.0f, 0.01f);
	TestNearlyEqual(TEXT("or move the bar"), Pawn->GetCurrentSheetInput(), 0.7f, 0.01f);

	// Tilt right and tip towards you: steer right, bar in.
	Controller->Hold(8.0f + 25.0f, 30.0f + 10.0f);
	Tick(240);
	TestTrue(FString::Printf(TEXT("Right side down steers the kite right (%.2f)"), Pawn->GetCurrentSteerInput()), Pawn->GetCurrentSteerInput() > 0.5f);
	TestNearlyEqual(TEXT("The kite gets that steering"), Pawn->GetKite()->Steer, Pawn->GetCurrentSteerInput(), 0.001f);
	TestNearlyEqual(TEXT("Pulling it in 10 deg pulls the bar in by a fifth"), Pawn->GetCurrentSheetInput(), 0.9f, 0.02f);

	// Tilt left and tip away.
	Controller->Hold(8.0f - 35.0f, 30.0f - 40.0f);
	Tick(240);
	TestNearlyEqual(TEXT("Left side well down is full left"), Pawn->GetCurrentSteerInput(), -1.0f, 0.05f);
	TestNearlyEqual(TEXT("Tipped well away the bar is right out"), Pawn->GetCurrentSheetInput(), 0.0f, 0.02f);

	// The stick's and keys' sheet input adjusts the bar even while the motion bar has it.
	Controller->Hold(8.0f, 30.0f);
	Tick(240);
	const float SheetHeld = Pawn->GetCurrentSheetInput();
	Pawn->SetSheetRateInput(1.0f);
	Tick(30);
	TestTrue(TEXT("Sheet input from the stick or keys moves the bar"), Pawn->GetCurrentSheetInput() > SheetHeld + 0.1f);
	Pawn->SetSheetRateInput(0.0f);

	// Re-centring takes the current hold as level.
	Controller->Hold(8.0f + 20.0f, 30.0f);
	Tick(240);
	TestTrue(TEXT("Tilted, it steers"), Pawn->GetCurrentSteerInput() > 0.3f);
	Pawn->RecentreMotionBar();
	Tick(2);
	TestNearlyEqual(TEXT("After re-centring the same hold is level"), Pawn->GetCurrentSteerInput(), 0.0f, 0.02f);

	// The controller goes away: the stick and keys have the bar back, level.
	Controller->Hold(8.0f + 20.0f + 30.0f, 30.0f);
	Tick(240);
	TestTrue(TEXT("Steering again before the controller goes"), Pawn->GetCurrentSteerInput() > 0.5f);
	Controller->bConnected = false;
	Tick(2);
	TestFalse(TEXT("With the controller gone the motion bar is not active"), Pawn->IsMotionBarActive());
	TestTrue(TEXT("though still switched on, waiting for it"), Pawn->IsMotionBarEnabled());
	TestEqual(TEXT("The bar goes level"), Pawn->GetCurrentSteerInput(), 0.0f);
	const float SheetBefore = Pawn->GetCurrentSheetInput();
	Pawn->SetSheetRateInput(-1.0f);
	Tick(6);
	TestTrue(TEXT("and the stick or keys move it again"), Pawn->GetCurrentSheetInput() < SheetBefore - 0.1f);
	Pawn->SetSheetRateInput(0.0f);

	// It comes back: active again, level where it is now held.
	Controller->bConnected = true;
	Tick(3);
	TestTrue(TEXT("When the controller comes back the motion bar takes over again"), Pawn->IsMotionBarActive());
	TestNearlyEqual(TEXT("level as it is now held"), Pawn->GetCurrentSteerInput(), 0.0f, 0.02f);

	// Switched off: tilt does nothing again.
	Pawn->SetMotionBarEnabled(false);
	Controller->Hold(-30.0f, 0.0f);
	Tick(60);
	TestFalse(TEXT("Switched off, it is not active"), Pawn->IsMotionBarActive());
	TestEqual(TEXT("and tilt does not steer"), Pawn->GetCurrentSteerInput(), 0.0f);

	// The setting is kept by the game instance.
	UKiteSurfGameInstance* GI = NewObject<UKiteSurfGameInstance>();
	TestFalse(TEXT("The motion bar is off in a new game"), GI->bMotionBar);
	GI->SetMotionBar(true);
	TestTrue(TEXT("and can be switched on"), GI->bMotionBar);

	World->DestroyWorld(false);
	return true;
}

// Brief controller vibration for the things that would be felt.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHaptics, "KiteSurf.Input.Haptics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfHaptics::RunTest(const FString& Parameters)
{
	// A landing buzzes harder and longer the harder it is, within limits.
	float SoftIntensity = 0.0f, SoftDuration = 0.0f, HardIntensity = 0.0f, HardDuration = 0.0f, HugeIntensity = 0.0f, HugeDuration = 0.0f;
	AKiteRiderPawn::GetLandingHaptic(1.0f, SoftIntensity, SoftDuration);
	AKiteRiderPawn::GetLandingHaptic(3.0f, HardIntensity, HardDuration);
	AKiteRiderPawn::GetLandingHaptic(12.0f, HugeIntensity, HugeDuration);
	TestTrue(TEXT("A soft landing is a light, short tap"), SoftIntensity > 0.0f && SoftIntensity < 0.4f && SoftDuration < 0.15f);
	TestTrue(TEXT("A harder one is stronger and longer"), HardIntensity > SoftIntensity && HardDuration > SoftDuration);
	TestTrue(TEXT("The hardest is full strength and still brief"), HugeIntensity == 1.0f && HugeDuration <= 0.35f);

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteRiderPawn* Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
	TestNotNull(TEXT("Pawn spawned"), Pawn);
	if (Pawn)
	{
		TestTrue(TEXT("Haptics are on by default"), Pawn->AreHapticsEnabled());
		Pawn->PlayHaptic(0.5f, 0.1f, false);
		TestEqual(TEXT("A buzz is asked for"), Pawn->GetHapticCount(), 1);
		TestEqual(TEXT("at the strength given"), Pawn->GetLastHapticIntensity(), 0.5f);
		Pawn->PlayHaptic(3.0f, 0.1f, true);
		TestEqual(TEXT("Strength is limited to full"), Pawn->GetLastHapticIntensity(), 1.0f);

		// The yank of a loop: once as the pull comes on, not for as long as it lasts, and not again at once.
		const float DeltaTime = 1.0f / 60.0f;
		const int32 Before = Pawn->GetHapticCount();
		Pawn->UpdateTensionHaptic(800.0f, DeltaTime);
		TestEqual(TEXT("Ordinary riding tension does not buzz"), Pawn->GetHapticCount(), Before);
		for (int32 Step = 0; Step < 30; ++Step)
		{
			Pawn->UpdateTensionHaptic(4000.0f, DeltaTime);
		}
		TestEqual(TEXT("A hard pull buzzes once, however long it lasts"), Pawn->GetHapticCount(), Before + 1);
		TestTrue(TEXT("and briefly"), Pawn->GetLastHapticDuration() <= 0.2f);
		Pawn->UpdateTensionHaptic(500.0f, DeltaTime);
		Pawn->UpdateTensionHaptic(4000.0f, DeltaTime);
		TestEqual(TEXT("A second pull straight after does not buzz again"), Pawn->GetHapticCount(), Before + 1);
		for (int32 Step = 0; Step < 90; ++Step)
		{
			Pawn->UpdateTensionHaptic(500.0f, DeltaTime);
		}
		Pawn->UpdateTensionHaptic(4000.0f, DeltaTime);
		TestEqual(TEXT("but one a second and a half later does"), Pawn->GetHapticCount(), Before + 2);

		// Switched off: nothing.
		Pawn->SetHapticsEnabled(false);
		const int32 WhenOff = Pawn->GetHapticCount();
		Pawn->PlayHaptic(1.0f, 0.3f, true);
		for (int32 Step = 0; Step < 120; ++Step)
		{
			Pawn->UpdateTensionHaptic(Step < 60 ? 500.0f : 4000.0f, DeltaTime);
		}
		TestEqual(TEXT("Switched off, nothing buzzes"), Pawn->GetHapticCount(), WhenOff);
	}

	UKiteSurfGameInstance* GI = NewObject<UKiteSurfGameInstance>();
	TestTrue(TEXT("Vibration is on in a new game"), GI->bHaptics);
	GI->SetHaptics(false);
	TestFalse(TEXT("and can be switched off"), GI->bHaptics);

	if (World)
	{
		World->DestroyWorld(false);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
