#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Capture/KiteSurfCinematicCamera.h"
#include "Engine/GameInstance.h"
#include "Interfaces/IPluginManager.h"
#include "MediaPlayer.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/KiteSurfMenuVideo.h"

// The menu videos are in Content/Movies as WebM files, and a packaged build copies them as loose files.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfMenuVideosArePackaged, "KiteSurf.Menu.VideosArePackaged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfMenuVideosArePackaged::RunTest(const FString& Parameters)
{
	for (const TCHAR* Path : { UKiteSurfMenuVideoSubsystem::IntroPath, UKiteSurfMenuVideoSubsystem::LoopPath })
	{
		const FString File = UKiteSurfMenuVideoSubsystem::GetVideoFilePath(Path);
		TArray<uint8> Bytes;
		if (!TestTrue(FString::Printf(TEXT("%s can be read"), Path), FFileHelper::LoadFileToArray(Bytes, *File)))
		{
			continue;
		}
		// A real encode, not a Git LFS pointer left by a checkout without LFS.
		TestTrue(FString::Printf(TEXT("%s is a real video (%d KB)"), Path, Bytes.Num() / 1024), Bytes.Num() > 100 * 1024);
		const bool bEbml = Bytes.Num() > 4 && Bytes[0] == 0x1A && Bytes[1] == 0x45 && Bytes[2] == 0xDF && Bytes[3] == 0xA3;
		TestTrue(FString::Printf(TEXT("%s starts with the Matroska/WebM header"), Path), bEbml);
	}

	// Electra plays files from disk; in a pak file it could not find them.
	TArray<FString> NonUFS;
	GConfig->GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("DirectoriesToAlwaysStageAsNonUFS"), NonUFS, GGameIni);
	const bool bMoviesStaged = NonUFS.ContainsByPredicate([](const FString& Entry) { return Entry.Contains(TEXT("\"Movies\"")); });
	TestTrue(TEXT("Content/Movies is staged as loose files (DefaultGame.ini)"), bMoviesStaged);
	return true;
}

// The player and the decoder for VP9 are switched on: without them the menus would only show the still art.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfMenuVideoPluginsEnabled, "KiteSurf.Menu.VideoPluginsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfMenuVideoPluginsEnabled::RunTest(const FString& Parameters)
{
	for (const TCHAR* Name : { TEXT("ElectraPlayer"), TEXT("VPxDecoderElectra") })
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Name);
		TestTrue(FString::Printf(TEXT("%s plugin is enabled"), Name), Plugin.IsValid() && Plugin->IsEnabled());
	}
	return true;
}

// With no renderer (or no file) there is no video: the intro counts as played straight away and the
// menus keep the still art, so a missing video never holds the menu back.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfMenuVideoFallsBackToStill, "KiteSurf.Menu.VideoFallsBackToStill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfMenuVideoFallsBackToStill::RunTest(const FString& Parameters)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UKiteSurfMenuVideoSubsystem* Videos = NewObject<UKiteSurfMenuVideoSubsystem>(GameInstance);

	TestFalse(TEXT("A video that does not exist cannot play"), UKiteSurfMenuVideoSubsystem::CanPlayVideo(TEXT("Movies/NoSuchVideo.webm")));
	TestFalse(TEXT("The intro has not played yet"), Videos->HasPlayedIntro());

	if (!FApp::CanEverRender())
	{
		TestFalse(TEXT("Without a renderer the intro does not play"), Videos->ShouldPlayIntro());
		TestNull(TEXT("so starting it gives no player"), Videos->StartIntro());
		TestTrue(TEXT("and it counts as played"), Videos->HasPlayedIntro());
		TestNull(TEXT("There is no loop either: the menus show the still art"), Videos->GetLoop());
	}
	else
	{
		// With a renderer: the intro plays once, then the loop.
		TestTrue(TEXT("The intro plays the first time"), Videos->ShouldPlayIntro());
		TestNotNull(TEXT("and starts"), Videos->StartIntro());
		Videos->FinishIntro();
		TestFalse(TEXT("It does not play a second time"), Videos->ShouldPlayIntro());
		const UKiteSurfVideoPlayer* Loop = Videos->GetLoop();
		TestTrue(TEXT("The loop is playing"), Loop && Loop->IsActive());
		TestTrue(TEXT("and loops"), Loop && Loop->GetPlayer() && Loop->GetPlayer()->IsLooping());
	}
	Videos->StopLoop();
	return true;
}

namespace
{
	/** Whether a point is inside the picture of a 16:9 camera at Location looking at LookAt. */
	bool IsInFrame(const FKiteSurfShotFrame& Frame, const FVector& Point)
	{
		const FRotator View = (Frame.LookAt - Frame.Location).Rotation();
		const FVector Local = View.UnrotateVector(Point - Frame.Location);
		if (Local.X <= 0.0f)
		{
			return false;
		}
		const float TanHalfH = FMath::Tan(FMath::DegreesToRadians(Frame.FieldOfViewDeg * 0.5f));
		const float TanHalfV = TanHalfH * 9.0f / 16.0f;
		return FMath::Abs(Local.Y / Local.X) < TanHalfH && FMath::Abs(Local.Z / Local.X) < TanHalfV;
	}

	float DistanceToSegment(const FVector& Point, const FVector& A, const FVector& B)
	{
		return FMath::PointDistToSegment(Point, A, B);
	}
}

// Every cinematic shot has the rider and the kite in the picture and the camera clear of the water and
// the lines, whichever way the rider is going and wherever the kite is.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfCaptureShotsFrameTheRider, "KiteSurf.Capture.ShotsFrameTheRider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfCaptureShotsFrameTheRider::RunTest(const FString& Parameters)
{
	// Chase is the rider's own camera, not one of these.
	const EKiteSurfShot Shots[] = { EKiteSurfShot::Side, EKiteSurfShot::Low, EKiteSurfShot::Orbit, EKiteSurfShot::Wide, EKiteSurfShot::KiteView };
	const float WaterZ = 0.0f;
	// Wind along +X: the kite parked at 45 degrees on its 24 m lines.
	const FVector Kite(1700.0f, 0.0f, 1700.0f);

	struct FCase { const TCHAR* Name; FVector Velocity; };
	const FCase Cases[] = {
		{ TEXT("riding across the wind at 16 kn"), FVector(0.0f, 850.0f, 0.0f) },
		{ TEXT("running downwind"), FVector(800.0f, 250.0f, 0.0f) },
		{ TEXT("stopped"), FVector::ZeroVector },
	};

	for (const FCase& Case : Cases)
	{
		for (EKiteSurfShot Shot : Shots)
		{
			const FString What = FString::Printf(TEXT("%s, %s"), *UEnum::GetValueAsString(Shot), Case.Name);
			const FVector Rider(0.0f, 0.0f, 0.0f);
			const FVector Anchor = AKiteSurfCinematicCamera::ComputeShotAnchor(Shot, Rider, Case.Velocity, Kite, WaterZ);
			for (float Seconds : { 0.0f, 2.0f, 4.0f })
			{
				// A planted shot sees the rider ride on from where it was placed.
				const FVector RiderNow = Shot == EKiteSurfShot::Wide ? Rider + Case.Velocity * Seconds : Rider;
				const FVector KiteNow = Kite + (RiderNow - Rider);
				const FKiteSurfShotFrame Frame = AKiteSurfCinematicCamera::ComputeShotFrame(Shot, RiderNow, Case.Velocity, KiteNow, Seconds, Anchor, WaterZ);

				TestTrue(FString::Printf(TEXT("%s at %.0f s: rider in the picture"), *What, Seconds), IsInFrame(Frame, RiderNow));
				TestTrue(FString::Printf(TEXT("%s at %.0f s: camera above the water (%.0f cm)"), *What, Seconds, Frame.Location.Z - WaterZ), Frame.Location.Z - WaterZ >= 100.0f);
				const float FromLines = DistanceToSegment(Frame.Location, RiderNow, KiteNow);
				TestTrue(FString::Printf(TEXT("%s at %.0f s: camera clear of the lines (%.0f cm)"), *What, Seconds, FromLines), FromLines > 500.0f);
				TestTrue(FString::Printf(TEXT("%s at %.0f s: kite in the picture"), *What, Seconds), IsInFrame(Frame, KiteNow));
			}
		}
	}

	// With the kite overhead, as it flies through a jump and just after, every shot still has the rider.
	const FVector Overhead(250.0f, 0.0f, 2390.0f); // 84 deg up on 24 m lines
	for (EKiteSurfShot Shot : Shots)
	{
		for (const FVector& Rider : { FVector::ZeroVector, FVector(0.0f, 0.0f, 600.0f) })
		{
			const FVector Velocity(0.0f, 1500.0f, 0.0f);
			const FVector Anchor = AKiteSurfCinematicCamera::ComputeShotAnchor(Shot, Rider, Velocity, Rider + Overhead, WaterZ);
			const FKiteSurfShotFrame Frame = AKiteSurfCinematicCamera::ComputeShotFrame(Shot, Rider, Velocity, Rider + Overhead, 1.0f, Anchor, WaterZ);
			TestTrue(FString::Printf(TEXT("%s, kite overhead, rider %.0f m up: rider in the picture"), *UEnum::GetValueAsString(Shot), Rider.Z / 100.0f), IsInFrame(Frame, Rider));
			TestTrue(FString::Printf(TEXT("%s, kite overhead, rider %.0f m up: and the kite (FOV %.0f deg)"), *UEnum::GetValueAsString(Shot), Rider.Z / 100.0f, Frame.FieldOfViewDeg), IsInFrame(Frame, Rider + Overhead));
		}
	}

	// In the air the following cameras stay at their height over the water, so the jump rises
	// through the picture instead of the sea dropping away.
	const FVector Velocity(0.0f, 1000.0f, 0.0f);
	const FKiteSurfShotFrame OnWater = AKiteSurfCinematicCamera::ComputeShotFrame(EKiteSurfShot::Side, FVector::ZeroVector, Velocity, Kite, 0.0f, FVector::ZeroVector, WaterZ);
	const FVector Airborne(0.0f, 0.0f, 700.0f);
	const FKiteSurfShotFrame InAir = AKiteSurfCinematicCamera::ComputeShotFrame(EKiteSurfShot::Side, Airborne, Velocity, Kite + Airborne, 0.0f, FVector::ZeroVector, WaterZ);
	TestNearlyEqual(TEXT("Side shot camera height with the rider 7 m up (cm)"), static_cast<float>(InAir.Location.Z), static_cast<float>(OnWater.Location.Z), 1.0f);
	TestTrue(TEXT("and the rider is still in the picture"), IsInFrame(InAir, Airborne));

	EKiteSurfShot Parsed = EKiteSurfShot::Chase;
	TestTrue(TEXT("Shot names parse"), AKiteSurfCinematicCamera::ParseShot(TEXT("kiteview"), Parsed) && Parsed == EKiteSurfShot::KiteView);
	TestFalse(TEXT("Unknown shot names do not"), AKiteSurfCinematicCamera::ParseShot(TEXT("Drone"), Parsed));
	return true;
}

#endif
