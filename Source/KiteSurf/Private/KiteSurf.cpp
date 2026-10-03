#include "KiteSurf.h"
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "Misc/CoreDelegates.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "KiteRiderPawn.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "Tricks/TrickTrackerComponent.h"
#include "WindComponent.h"
#include "AudioMixerBlueprintLibrary.h"
#include "UI/KiteSurfMainMenuWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/KiteSurfSettingsWidget.h"
#include "UI/KiteSurfGearWidget.h"
#include "UI/KiteSurfPauseMenuWidget.h"
#include "UObject/UObjectIterator.h"
#include "KiteSurfUnits.h"
#include "Capture/KiteSurfCinematicCamera.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogKiteSurf);

class FKiteSurfGameModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.SmokeFrames"),
			TEXT("Runs the game for N frames, saves Saved/Screenshots/<Platform>/smoke.png near the end, then exits. Usage: kitesurf.SmokeFrames <NumFrames>"),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FKiteSurfGameModule::HandleSmokeFrames),
			ECVF_Default
		);

		// Scripting aids for headless smoke runs: they let a screenshot show a menu or a paused game.
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.After"),
			TEXT("Runs a console command after N frames. Usage: kitesurf.After <NumFrames> <Command...>"),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FKiteSurfGameModule::HandleAfter),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.TogglePause"),
			TEXT("Opens or closes the pause menu, as the Escape key does."),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
				{
					if (It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
					{
						It->TogglePause();
					}
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.Input"),
			TEXT("Holds inputs on the player's rider. Usage: kitesurf.Input <Steer -1..1> <SheetRate -1..1> <Turn -1..1> <WeightShift -1..1> <RawSteer 0|1>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				auto Arg = [&Args](int32 Index) { return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : 0.0f; };
				for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
				{
					if (It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
					{
						It->ApplyScriptedInput(Arg(0), Arg(1), Arg(2), Arg(3), Arg(4) > 0.5f);
					}
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.Wind"),
			TEXT("Sets the base wind speed for the player's rider, keeping its direction. Usage: kitesurf.Wind <knots>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				const float Knots = Args.IsValidIndex(0) ? FMath::Max(FCString::Atof(*Args[0]), 0.0f) : 15.0f;
				for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
				{
					UWindComponent* Wind = It->GetWind();
					if (Wind && It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
					{
						const FVector Direction = Wind->BaseWind.IsNearlyZero() ? FVector::ForwardVector : Wind->BaseWind.GetSafeNormal();
						Wind->BaseWind = Direction * KiteUnits::KnotsToCmS(Knots);
					}
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.AudioRecordStart"),
			TEXT("Starts recording everything the game plays. Finish with kitesurf.AudioRecordStop."),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				// Any game world will do: the menus have no rider.
				if (const UWorld* World = FindGameWorld())
				{
					UAudioMixerBlueprintLibrary::StartRecordingOutput(World, 0.0f);
					UE_LOG(LogKiteSurf, Log, TEXT("Audio recording started"));
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.AudioRecordStop"),
			TEXT("Writes the recording to Saved/BouncedWavFiles/<name>.wav. Usage: kitesurf.AudioRecordStop <name>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				const FString Name = Args.IsValidIndex(0) ? Args[0] : TEXT("kitesurf");
				if (const UWorld* World = FindGameWorld())
				{
					UAudioMixerBlueprintLibrary::StopRecordingOutput(World, EAudioRecordingExportType::WavFile, Name, FString());
					UE_LOG(LogKiteSurf, Log, TEXT("Audio recording written as %s.wav"), *Name);
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.Kite"),
			TEXT("Rigs the player's kite at this size (m2), or the size recommended for the current wind with 0. Usage: kitesurf.Kite <m2>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				AKiteRiderPawn* Rider = FindPlayerRider();
				UKiteComponent* Kite = Rider ? Rider->GetKite() : nullptr;
				const UWindComponent* Wind = Rider ? Rider->GetWind() : nullptr;
				if (!Kite)
				{
					return;
				}
				float SizeM2 = Args.IsValidIndex(0) ? FCString::Atof(*Args[0]) : 0.0f;
				if (SizeM2 <= 0.0f && Wind)
				{
					SizeM2 = UKiteComponent::RecommendKiteSizeM2(KiteUnits::CmSToKnots(Wind->BaseWind.Size()));
				}
				Kite->SetKiteSize(SizeM2);
				UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.Kite: %.0f m2"), SizeM2);
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.HoldKite"),
			TEXT("Flies the player's kite at a clock position (deg, + on the right) by working the steering, as a rider holding it low to keep riding; 'off' lets go of the bar. For scripted rides. Usage: kitesurf.HoldKite <ClockDeg|off>"),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FKiteSurfGameModule::HandleHoldKite),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.State"),
			TEXT("Logs the player's ride in one line: board state, speed, height, kite position, line tension and the bar. For scripting rides."),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				const AKiteRiderPawn* Rider = FindPlayerRider();
				const UBoardMovementComponent* Board = Rider ? Rider->GetBoardMovement() : nullptr;
				const UKiteComponent* Kite = Rider ? Rider->GetKite() : nullptr;
				if (!Board || !Kite)
				{
					return;
				}
				UE_LOG(LogKiteSurf, Display, TEXT("State: %s%s%s, %.1f kn, height %.0f cm, kite clock %.0f deg elevation %.0f deg, tension %.0f N, steer %.2f, bar %.2f"),
					*UEnum::GetDisplayValueAsText(Board->GetBoardState()).ToString(), Board->IsFloating() ? TEXT(" floating") : TEXT(""), Board->IsCrashing() ? TEXT(" crashing") : TEXT(""),
					KiteUnits::CmSToKnots(Board->Velocity.Size2D()), Board->GetCurrentJumpHeight(), Kite->GetClockDeg(), Kite->GetElevationDeg(), Kite->GetLineTensionN(),
					Rider->GetCurrentSteerInput(), Rider->GetCurrentSheetInput());
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.Shot"),
			TEXT("Films the player's rider with a cinematic camera, cutting to the shot. Usage: kitesurf.Shot <Chase|Side|Low|Orbit|Wide|KiteView>"),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FKiteSurfGameModule::HandleShot),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.CaptureFrames"),
			TEXT("Saves every frame without UI to Saved/MenuVideo/<Name>/frame_00000.png and so on, then exits. Run with -benchmark -fps=30 for a fixed timestep. Usage: kitesurf.CaptureFrames <NumFrames> <Name>"),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FKiteSurfGameModule::HandleCaptureFrames),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.HideUI"),
			TEXT("Hides the HUD and any on-screen widgets, for filming."),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				UWorld* World = GEngine ? GEngine->GetCurrentPlayWorld() : nullptr;
				APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
				if (PC && PC->GetHUD())
				{
					PC->GetHUD()->bShowHUD = false;
				}
				if (World)
				{
					UWidgetLayoutLibrary::RemoveAllWidgets(World);
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.MotionBar"),
			TEXT("Switches the motion bar on or off for the player's rider and logs what it reads. Usage: kitesurf.MotionBar <0|1>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				const bool bOn = !Args.IsValidIndex(0) || FCString::Atoi(*Args[0]) != 0;
				for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
				{
					if (It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
					{
						// Switching on re-centres, so an already running bar is reported as it was.
						if (It->IsMotionBarEnabled() != bOn)
						{
							It->SetMotionBarEnabled(bOn);
						}
						const FKiteMotionSample& Sample = It->GetLastMotionSample();
						UE_LOG(LogKiteSurf, Log, TEXT("Motion bar %s (active %d, device '%s', steer %.2f, sheet %.2f, roll %.1f deg, pitch %.1f deg, accel (%.2f, %.2f, %.2f) g, gyro (%.3f, %.3f, %.3f) rad/s)"),
							bOn ? TEXT("on") : TEXT("off"), It->IsMotionBarActive(), *It->GetMotionDeviceName(), It->GetCurrentSteerInput(), It->GetCurrentSheetInput(),
							It->GetMotionTiltDeg().X, It->GetMotionTiltDeg().Y, Sample.AccelG.X, Sample.AccelG.Y, Sample.AccelG.Z, Sample.GyroRadS.X, Sample.GyroRadS.Y, Sample.GyroRadS.Z);
					}
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.Load"),
			TEXT("Holds (1) or lets go of (0) the jump button on the player's rider: held is the loaded crouch, letting go pops. Usage: kitesurf.Load <0|1>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				const bool bHold = !Args.IsValidIndex(0) || FCString::Atoi(*Args[0]) != 0;
				for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
				{
					if (It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
					{
						if (bHold)
						{
							It->SetLoadHeld(true);
						}
						else
						{
							It->ReleaseLoadAndPop();
						}
					}
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.Jump"),
			TEXT("Pops the player's rider off the water, as the jump key does."),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
				{
					if (It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
					{
						It->Jump();
					}
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.Jumps"),
			TEXT("Logs the player's jump records this session as CSV: index, outcome, name, height m, airtime s, distance m, landing g, peak line tension N, completed loops, points."),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				AKiteRiderPawn* Rider = FindPlayerRider();
				const UTrickTrackerComponent* Tracker = Rider ? Rider->GetTrickTracker() : nullptr;
				if (!Tracker)
				{
					UE_LOG(LogKiteSurf, Warning, TEXT("kitesurf.Jumps: no rider with a trick tracker"));
					return;
				}
				UE_LOG(LogKiteSurf, Display, TEXT("jumpcsv,index,outcome,name,height_m,airtime_s,distance_m,landing_g,peak_n,loops,score"));
				for (const FJumpRecord& Record : Tracker->GetJumpRecords())
				{
					UE_LOG(LogKiteSurf, Display, TEXT("jumpcsv,%d,%s,\"%s\",%.2f,%.2f,%.1f,%.2f,%.0f,%d,%.1f"),
						Record.Index, Record.Outcome == EJumpOutcome::Landed ? TEXT("Landed") : TEXT("Crashed"), *Record.TrickName,
						KiteUnits::CmToM(Record.ApexHeightCm), Record.AirtimeSeconds, KiteUnits::CmToM(Record.DistanceCm), Record.LandingG,
						Record.PeakTensionN, Record.CountCompletedLoops(), Record.Score.Total * Record.RepeatFactor);
				}
				UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.Jumps: %d jumps recorded, %d kept, %.1f points this session"),
					Tracker->GetJumpRecordCount(), Tracker->GetJumpRecords().Num(), Tracker->GetJumpSession().GetSessionPoints());
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.MenuKey"),
			TEXT("Sends a key press through the UI, as the keyboard or gamepad would. Usage: kitesurf.MenuKey <Up|Down|Left|Right|Enter|Gamepad_DPad_Down|...>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				const FKey Key(Args.IsValidIndex(0) ? FName(*Args[0]) : NAME_None);
				if (!Key.IsValid())
				{
					return;
				}
				// Through Slate, as a real key press goes: it reaches the menu only if the menu has
				// keyboard focus, which is the thing worth checking in a scripted run.
				FSlateApplication& Slate = FSlateApplication::Get();
				const FKeyEvent Event(Key, FModifierKeysState(), Slate.GetUserIndexForKeyboard(), false, 0, 0);
				const bool bHandled = Slate.ProcessKeyDownEvent(Event);
				Slate.ProcessKeyUpEvent(Event);
				UE_LOG(LogKiteSurf, Log, TEXT("Menu key %s: %s"), *Key.ToString(), bHandled ? TEXT("handled") : TEXT("not handled"));
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.OpenGear"),
			TEXT("Opens the gear screen from whichever menu is on screen (PLAY on the main menu, GEAR on the pause menu)."),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				for (TObjectIterator<UKiteSurfPauseMenuWidget> It; It; ++It)
				{
					if (It->IsInViewport())
					{
						It->OnGearClicked();
						return;
					}
				}
				for (TObjectIterator<UKiteSurfMainMenuWidget> It; It; ++It)
				{
					if (It->IsInViewport())
					{
						It->OnPlayClicked();
						return;
					}
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.OpenSettings"),
			TEXT("Opens the settings screen from whichever menu is on screen."),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				for (TObjectIterator<UKiteSurfPauseMenuWidget> It; It; ++It)
				{
					if (It->IsInViewport())
					{
						It->OnSettingsClicked();
						return;
					}
				}
				for (TObjectIterator<UKiteSurfMainMenuWidget> It; It; ++It)
				{
					if (It->IsInViewport())
					{
						It->OnSettingsClicked();
						return;
					}
				}
			}),
			ECVF_Default
		);
	}

	virtual void ShutdownModule() override
	{
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.After"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.TogglePause"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.OpenSettings"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.OpenGear"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.MenuKey"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Input"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Jump"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Jumps"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Load"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.State"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.HoldKite"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Kite"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Shot"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.CaptureFrames"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.HideUI"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.MotionBar"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.AudioRecordStart"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.AudioRecordStop"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Wind"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.SmokeFrames"));
		FDefaultGameModuleImpl::ShutdownModule();
	}

private:
	FTSTicker::FDelegateHandle TickerHandle;
	int32 RemainingFrames = 0;

	FTSTicker::FDelegateHandle CaptureHandle;
	FString CaptureDir;
	int32 CaptureFrameIndex = 0;
	int32 CaptureFrameCount = 0;
	TWeakObjectPtr<AKiteSurfCinematicCamera> CinematicCamera;

	static AKiteRiderPawn* FindPlayerRider()
	{
		for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
		{
			if (It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
			{
				return *It;
			}
		}
		return nullptr;
	}

	FTSTicker::FDelegateHandle HoldKiteHandle;
	float HoldKiteClockDeg = 0.0f;

	void HandleHoldKite(const TArray<FString>& Args)
	{
		if (HoldKiteHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(HoldKiteHandle);
			HoldKiteHandle.Reset();
		}
		if (!Args.IsValidIndex(0) || Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			if (AKiteRiderPawn* Rider = FindPlayerRider())
			{
				Rider->SteerKite(0.0f);
			}
			return;
		}
		HoldKiteClockDeg = FCString::Atof(*Args[0]);
		HoldKiteHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
		{
			AKiteRiderPawn* Rider = FindPlayerRider();
			const UKiteComponent* Kite = Rider ? Rider->GetKite() : nullptr;
			if (Kite)
			{
				// A gentle hand: bar over in proportion to how far the kite is from where it should be, at most
				// half bar, so the steering dead time does not swing it round.
				Rider->SteerKite(FMath::Clamp((HoldKiteClockDeg - Kite->GetClockDeg()) / 40.0f, -0.5f, 0.5f));
			}
			return true;
		}));
	}

	void HandleShot(const TArray<FString>& Args)
	{
		EKiteSurfShot Shot = EKiteSurfShot::Chase;
		if (!Args.IsValidIndex(0) || !AKiteSurfCinematicCamera::ParseShot(Args[0], Shot))
		{
			UE_LOG(LogKiteSurf, Warning, TEXT("Usage: kitesurf.Shot <Chase|Side|Low|Orbit|Wide|KiteView>"));
			return;
		}
		AKiteRiderPawn* Rider = FindPlayerRider();
		if (!Rider)
		{
			UE_LOG(LogKiteSurf, Warning, TEXT("kitesurf.Shot: no rider to film"));
			return;
		}
		if (!CinematicCamera.IsValid() || CinematicCamera->GetWorld() != Rider->GetWorld())
		{
			CinematicCamera = Rider->GetWorld()->SpawnActor<AKiteSurfCinematicCamera>();
		}
		if (CinematicCamera.IsValid())
		{
			CinematicCamera->StartShot(Shot, Rider);
			UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.Shot %s"), *Args[0]);
		}
	}

	void HandleCaptureFrames(const TArray<FString>& Args)
	{
		CaptureFrameCount = Args.IsValidIndex(0) ? FMath::Max(FCString::Atoi(*Args[0]), 1) : 300;
		const FString Name = Args.IsValidIndex(1) ? Args[1] : TEXT("capture");
		CaptureDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MenuVideo") / Name);
		IFileManager::Get().DeleteDirectory(*CaptureDir, false, true);
		IFileManager::Get().MakeDirectory(*CaptureDir, true);
		CaptureFrameIndex = 0;
		UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.CaptureFrames: %d frames to %s"), CaptureFrameCount, *CaptureDir);

		if (CaptureHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(CaptureHandle);
		}
		CaptureHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
		{
			// One request a frame; the viewport writes it when it next draws.
			if (CaptureFrameIndex < CaptureFrameCount)
			{
				FScreenshotRequest::RequestScreenshot(CaptureDir / FString::Printf(TEXT("frame_%05d.png"), CaptureFrameIndex), false, false);
				++CaptureFrameIndex;
				return true;
			}
			// A few more frames so the last request is written before exiting.
			if (++CaptureFrameIndex > CaptureFrameCount + 5)
			{
				UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.CaptureFrames complete. Requesting exit."));
				FPlatformMisc::RequestExit(false);
				return false;
			}
			return true;
		}));
	}

	// Frames before exit at which the screenshot is requested, leaving time for it to be written.
	static constexpr int32 ScreenshotLeadFrames = 30;

	/** The world the game is being played in, menu or ride. */
	static const UWorld* FindGameWorld()
	{
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					return Context.World();
				}
			}
		}
		return nullptr;
	}

	void HandleSmokeFrames(const TArray<FString>& Args)
	{
		int32 FramesToRun = 60;
		if (Args.Num() > 0)
		{
			FramesToRun = FCString::Atoi(*Args[0]);
			if (FramesToRun <= 0)
			{
				FramesToRun = 60;
			}
		}

		UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.SmokeFrames starting: will run for %d frames then exit"), FramesToRun);
		RemainingFrames = FramesToRun;

		if (TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		}

		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FKiteSurfGameModule::TickSmokeFrames)
		);
	}

	void HandleAfter(const TArray<FString>& Args)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogKiteSurf, Warning, TEXT("Usage: kitesurf.After <NumFrames> <Command...>"));
			return;
		}
		const int32 Frames = FMath::Max(FCString::Atoi(*Args[0]), 1);
		TArray<FString> CommandParts(Args);
		CommandParts.RemoveAt(0);
		const FString Command = FString::Join(CommandParts, TEXT(" "));

		TSharedRef<int32> Remaining = MakeShared<int32>(Frames);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Remaining, Command](float)
		{
			if (--(*Remaining) > 0)
			{
				return true;
			}
			if (GEngine)
			{
				UWorld* World = GEngine->GetCurrentPlayWorld();
				GEngine->Exec(World, *Command);
			}
			return false;
		}));
	}

	bool TickSmokeFrames(float DeltaTime)
	{
		RemainingFrames--;
		if (RemainingFrames == ScreenshotLeadFrames)
		{
			// Captured late so the frame shows gameplay with shaders compiled, not the first frame.
			FScreenshotRequest::RequestScreenshot(TEXT("smoke"), true, false);
		}
		if (RemainingFrames <= 0)
		{
			UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.SmokeFrames complete! Requesting exit."));
			FPlatformMisc::RequestExit(false);
			return false; // remove ticker
		}
		return true; // continue ticking
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FKiteSurfGameModule, KiteSurf, "KiteSurf");
