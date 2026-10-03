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
#include "Tricks/FreestyleHeatSubsystem.h"
#include "Tricks/TrickSessionSubsystem.h"
#include "WindComponent.h"
#include "AudioMixerBlueprintLibrary.h"
#include "UI/KiteSurfMainMenuWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/KiteSurfSettingsWidget.h"
#include "UI/KiteSurfGearWidget.h"
#include "UI/KiteSurfPauseMenuWidget.h"
#include "UObject/UObjectIterator.h"
#include "KiteSurfUnits.h"
#include "InputActionValue.h"
#include "School/LessonTiming.h"
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

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.Screenshot"),
			TEXT("Saves Saved/Screenshots/<Platform>/<Name>.png of the next frame, HUD included (the engine's 'shot' is not reachable from kitesurf.After). Usage: kitesurf.Screenshot <Name>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				const FString Name = Args.IsValidIndex(0) ? Args[0] : FString(TEXT("shot"));
				FScreenshotRequest::RequestScreenshot(Name, true, false);
				UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.Screenshot %s requested"), *Name);
			}),
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
			TEXT("Holds inputs on the player's rider. Usage: kitesurf.Input <Steer -1..1> <SheetRate -1..1> <Turn -1..1> <WeightShift -1..1> <RawSteer 0|1> [<AirRotX -1..1> <AirRotY -1..1> <Tuck 0..1>]. AirRotX +1 is a back roll, AirRotY -1 a backflip; scripted, so with no screen-side mapping (kitesurf.Stick is the player's stick)."),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				auto Arg = [&Args](int32 Index) { return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : 0.0f; };
				for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
				{
					if (It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
					{
						It->ApplyScriptedInput(Arg(0), Arg(1), Arg(2), Arg(3), Arg(4) > 0.5f, FVector2D(Arg(5), Arg(6)), Arg(7));
					}
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.PreWind"),
			TEXT("Holds the pre-wind stick on the player's rider: it winds up while the jump button is held (kitesurf.Load 1) and the take-off turns it into a rotation. Usage: kitesurf.PreWind <X -1..1: +1 back roll> <Y -1..1: -1 backflip>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				auto Arg = [&Args](int32 Index) { return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : 0.0f; };
				for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
				{
					if (It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
					{
						It->SetPreWind(FVector2D(Arg(0), Arg(1)));
					}
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.Stick"),
			TEXT("Holds the player's left stick (A/D, W/S) through the same handlers the keys and the stick use, so it is read by state: the board on the water, the pre-wind while the jump button and IA_Rotate (kitesurf.RotateButton) are both held, the rotation in the air with IA_Rotate held (X towards the side of the screen the rider's back is on is a back roll). Usage: kitesurf.Stick <X -1..1: D +1> <Y -1..1: W +1>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				auto Arg = [&Args](int32 Index) { return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : 0.0f; };
				for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
				{
					if (It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
					{
						It->OnEdgeTriggered(FInputActionValue(Arg(0)));
						It->OnWeightShiftTriggered(FInputActionValue(Arg(1)));
						UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.Stick %.2f %.2f: screen back sign %+.0f; board carve %.2f weight %.2f, pre-wind (%.2f, %.2f), air stick (%.2f, %.2f)"),
							Arg(0), Arg(1), It->GetScreenBackSign(), It->GetBoardMovement() ? It->GetBoardMovement()->GetEdgeInput() : 0.0f,
							It->GetBoardMovement() ? It->GetBoardMovement()->GetWeightShift() : 0.0f, It->GetPreWindStick().X, It->GetPreWindStick().Y,
							It->GetAirRotationInput().X, It->GetAirRotationInput().Y);
					}
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.Trick"),
			TEXT("Holds the trick buttons on the player's rider (T2.1, T2.2), scripted: the grabs (LB/Q front hand, RB/E back hand) and the one-footer (L3/C), with the grab zone stick in rotation axes (X -1 the toe edge, +1 the heel edge; Y +1 the nose, -1 the tail; centred: the toe edge). Both grab buttons are the board-off (T2.3), whose variant the same stick picks (Y +1 superman, -1 tic tac, X board pass, centred plain); setting either back to 0 catches the board. In the air only. Usage: kitesurf.Trick <Front 0|1> <Back 0|1> <OneFoot 0|1> [<ZoneX -1..1> <ZoneY -1..1>]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				auto Arg = [&Args](int32 Index) { return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : 0.0f; };
				for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
				{
					if (It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
					{
						It->SetTrickInput(Arg(0) > 0.5f, Arg(1) > 0.5f, Arg(2) > 0.5f, FVector2D(Arg(3), Arg(4)));
						const FGrabState& Grabs = It->GetGrabState();
						UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.Trick front %d back %d one-foot %d zone (%.2f, %.2f): hand off the bar %d (%s, %s), holding %d for %.2f s, foot out %.2f, %d grab(s) this flight; board-off %s %s (off %.2f, held %.2f s, catch %s)"),
							Arg(0) > 0.5f, Arg(1) > 0.5f, Arg(2) > 0.5f, Arg(3), Arg(4), Grabs.IsHandOffBar(),
							*UEnum::GetValueAsString(Grabs.GetHand()), *UEnum::GetValueAsString(Grabs.GetZone()), Grabs.IsHolding(), Grabs.GetHoldSeconds(),
							Grabs.GetFootOut(), Grabs.GetGrabs().Num(),
							Grabs.GetBoardOff().IsBoardOff() ? TEXT("on") : TEXT("off"), *UEnum::GetValueAsString(Grabs.GetBoardOff().GetVariant()),
							Grabs.GetBoardOff().GetOffAlpha(), Grabs.GetBoardOff().GetHeldSeconds(), *UEnum::GetValueAsString(Grabs.GetBoardOff().GetCatchAtTouchdown()));
					}
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.Hook"),
			TEXT("Presses the hook button (Y / F) on the player's rider (T3.1): on the water, hooks in or out of the harness; ignored in the air. Unhooked, the kite parks low, its sheet is held at the stopper and the bar input moves the arms."),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
				{
					if (It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
					{
						It->PressHook();
						UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.Hook: pressed (was %s)"), It->IsHooked() ? TEXT("hooked in") : TEXT("unhooked"));
					}
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.Pass"),
			TEXT("Presses the handle pass button (X on both keyboard and pad since batch A) on the player's rider (T3.1): unhooked, a pass starts when the lines are slack and the back is to the kite within the press's buffer; the flick assist dips the kite in the air. On the water (T3.5) riding blind it is the surface pass and the half turn to heelside, riding toeside the half turn back."),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
				{
					if (It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
					{
						It->PressPass();
						const FBarState& Bar = It->GetBarState();
						UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.Pass: pressed (%s, %s, wrap %.0f deg, slack %.2f s, stance %s)"), Bar.bHooked ? TEXT("hooked in") : TEXT("unhooked"),
							*UEnum::GetDisplayValueAsText(Bar.Place).ToString(), Bar.WrapDeg, Bar.SlackSeconds, *UEnum::GetDisplayValueAsText(It->GetRidingStance()).ToString());
					}
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.Bar"),
			TEXT("Holds the player's sheet input (Up / Down, right stick, triggers) through the same handler they use: with the bar returning to the middle, the bar goes that fraction of the way to fully in (+) or out (-) and springs back on 0. Usage: kitesurf.Bar <-1..1: +1 power>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				const float Input = Args.IsValidIndex(0) ? FCString::Atof(*Args[0]) : 0.0f;
				for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
				{
					if (It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
					{
						It->OnSheetTriggered(FInputActionValue(Input));
						UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.Bar %.2f: bar at %.2f, returns to middle %d"), Input, It->GetCurrentSheetInput(), It->GetBarReturnsToMiddle());
					}
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.JumpButton"),
			TEXT("Presses (1) or lets go of (0) the player's jump button through the same handlers the key uses: held on the water loads, letting go pops; pressed and held in the air tucks. Usage: kitesurf.JumpButton <0|1>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				const bool bPress = !Args.IsValidIndex(0) || FCString::Atoi(*Args[0]) != 0;
				for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
				{
					if (It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
					{
						if (bPress)
						{
							It->OnJumpPressed(FInputActionValue(true));
						}
						else
						{
							It->OnJumpReleased(FInputActionValue(false));
						}
					}
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.RotateButton"),
			TEXT("Presses (1) or lets go of (0) the player's rotation modifier through the same handlers IA_Rotate uses (batch A: LeftShift / LT): held, the left stick reaches the pre-wind while loading and the rotation stick in the air; without it the stick is always the board. Usage: kitesurf.RotateButton <0|1>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				const bool bPress = !Args.IsValidIndex(0) || FCString::Atoi(*Args[0]) != 0;
				for (TObjectIterator<AKiteRiderPawn> It; It; ++It)
				{
					if (It->GetWorld() && It->GetWorld()->IsGameWorld() && It->IsPlayerControlled())
					{
						if (bPress)
						{
							It->OnRotatePressed(FInputActionValue(true));
						}
						else
						{
							It->OnRotateReleased(FInputActionValue(false));
						}
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
			TEXT("kitesurf.PopAtTop"),
			TEXT("While on (1), pops the player's rider at the top of a send, as a rider timing it by eye: with the load held on the water and the kite at the top (LessonTiming::IsKiteAtTop, the kite school's sheet-in moment), the bar comes in and the load is let go a moment later; on the way down it crouches for the landing. For scripted lesson rides (kite school B2). Usage: kitesurf.PopAtTop <0|1>"),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FKiteSurfGameModule::HandlePopAtTop),
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
				const FBarState& Bar = Rider->GetBarState();
				const FString BarText = Bar.bHooked ? FString()
					: FString::Printf(TEXT(", unhooked: %s, arms %.2f, wrap %.0f deg, kite sheet %.2f, stance %s %.1f s"), *UEnum::GetDisplayValueAsText(Bar.Place).ToString(), Rider->GetArmExtension(), Bar.WrapDeg, Kite->Sheet,
						*UEnum::GetDisplayValueAsText(Rider->GetRidingStance()).ToString(), Rider->GetStanceSeconds());
				UE_LOG(LogKiteSurf, Display, TEXT("State: %s%s%s, %.1f kn heading %.0f deg, height %.0f cm, kite clock %.0f deg elevation %.0f deg turned %.0f deg, tension %.0f N, steer %.2f, bar %.2f%s"),
					*UEnum::GetDisplayValueAsText(Board->GetBoardState()).ToString(), Board->IsFloating() ? TEXT(" floating") : TEXT(""), Board->IsCrashing() ? TEXT(" crashing") : TEXT(""),
					KiteUnits::CmSToKnots(Board->Velocity.Size2D()), Board->Velocity.Rotation().Yaw, Board->GetCurrentJumpHeight(), Kite->GetClockDeg(), Kite->GetElevationDeg(), Kite->GetTurnDeg(), Kite->GetLineTensionN(),
					Rider->GetCurrentSteerInput(), Rider->GetCurrentSheetInput(), *BarText);
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.Shot"),
			TEXT("Films the player's rider with a cinematic camera, cutting to the shot. Usage: kitesurf.Shot <Chase|Side|Low|Orbit|Wide|KiteView|Close>"),
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
						UE_LOG(LogKiteSurf, Log, TEXT("Motion bar %s (active %d, device '%s', power by %s, steer %.2f, sheet %.2f, roll %.1f deg, pitch %.1f deg, travel %.1f cm, still %d, accel (%.2f, %.2f, %.2f) g, gyro (%.3f, %.3f, %.3f) rad/s)"),
							bOn ? TEXT("on") : TEXT("off"), It->IsMotionBarActive(), *It->GetMotionDeviceName(), It->GetMotionSheetMode() == EMotionSheetMode::Move ? TEXT("move") : TEXT("tilt"),
							It->GetCurrentSteerInput(), It->GetCurrentSheetInput(), It->GetMotionTiltDeg().X, It->GetMotionTiltDeg().Y, It->GetMotionStroke().GetDisplacementCm(), It->GetMotionStroke().IsStill(),
							Sample.AccelG.X, Sample.AccelG.Y, Sample.AccelG.Z, Sample.GyroRadS.X, Sample.GyroRadS.Y, Sample.GyroRadS.Z);
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
			TEXT("kitesurf.Session"),
			TEXT("Starts a best-three session for the player's rider: the best three jumps in the time count, repeats are paid less. Usage: kitesurf.Session [seconds, default 90]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				const float Seconds = Args.IsValidIndex(0) ? FMath::Max(FCString::Atof(*Args[0]), 1.0f) : UTrickSessionSubsystem::DefaultSessionSeconds;
				AKiteRiderPawn* Rider = FindPlayerRider();
				UWorld* World = Rider ? Rider->GetWorld() : nullptr;
				UTrickSessionSubsystem* Sessions = World ? World->GetSubsystem<UTrickSessionSubsystem>() : nullptr;
				if (!Sessions || !Sessions->StartSession(Seconds, Rider->GetTrickTracker()))
				{
					UE_LOG(LogKiteSurf, Warning, TEXT("kitesurf.Session: no rider to start a session for"));
				}
			}),
			ECVF_Default
		);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("kitesurf.Heat"),
			TEXT("Starts a freestyle heat for the player's rider (T3.6): unhooked tricks of more than 0.4 s airtime and crashes are attempts, the best per GKA family counts, four count within the group limits, plus the variety bonus. Usage: kitesurf.Heat freestyle [attempts, default 7] [trick countdown s, default 90, 0 off] | kitesurf.Heat stop"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				const FString Mode = Args.IsValidIndex(0) ? Args[0] : FString(TEXT("freestyle"));
				AKiteRiderPawn* Rider = FindPlayerRider();
				UWorld* World = Rider ? Rider->GetWorld() : nullptr;
				UFreestyleHeatSubsystem* Heats = World ? World->GetSubsystem<UFreestyleHeatSubsystem>() : nullptr;
				if (!Heats)
				{
					UE_LOG(LogKiteSurf, Warning, TEXT("kitesurf.Heat: no rider to start a heat for"));
					return;
				}
				if (Mode.Equals(TEXT("stop"), ESearchCase::IgnoreCase))
				{
					Heats->CancelHeat(TEXT("Freestyle heat stopped"));
					return;
				}
				if (!Mode.Equals(TEXT("freestyle"), ESearchCase::IgnoreCase))
				{
					UE_LOG(LogKiteSurf, Warning, TEXT("kitesurf.Heat: unknown heat '%s'; only 'freestyle' (or 'stop')"), *Mode);
					return;
				}
				const int32 Attempts = Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : FFreestyleHeat::DefaultAttempts;
				const float Countdown = Args.IsValidIndex(2) ? FMath::Max(FCString::Atof(*Args[2]), 0.0f) : -1.0f;
				const EHeatStartResult Result = Heats->StartHeat(Attempts > 0 ? Attempts : FFreestyleHeat::DefaultAttempts, Rider->GetTrickTracker(), Countdown);
				UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.Heat: %s"), *UEnum::GetDisplayValueAsText(Result).ToString());
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
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Screenshot"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.TogglePause"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.OpenSettings"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.OpenGear"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.MenuKey"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Input"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Jump"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Jumps"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Session"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Heat"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Load"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.State"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.HoldKite"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.PopAtTop"));
		if (PopAtTopHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(PopAtTopHandle);
			PopAtTopHandle.Reset();
		}
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Kite"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Shot"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.CaptureFrames"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.HideUI"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.MotionBar"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.AudioRecordStart"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.AudioRecordStop"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Wind"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.PreWind"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Stick"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Trick"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.JumpButton"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.RotateButton"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Bar"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Hook"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Pass"));
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

	FTSTicker::FDelegateHandle PopAtTopHandle;
	float PopAtTopLastElevation = -1.0f;
	/** Seconds of game time since the bar came in at the top; below 0 while waiting for the top. */
	float PopAtTopBarInSeconds = -1.0f;
	/** The rider crouched for the landing (let go once back on the water, without a pop). */
	bool bPopAtTopCrouched = false;

	void HandlePopAtTop(const TArray<FString>& Args)
	{
		if (PopAtTopHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(PopAtTopHandle);
			PopAtTopHandle.Reset();
		}
		PopAtTopLastElevation = -1.0f;
		PopAtTopBarInSeconds = -1.0f;
		bPopAtTopCrouched = false;
		if (!Args.IsValidIndex(0) || FCString::Atoi(*Args[0]) == 0)
		{
			return;
		}
		PopAtTopHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
		{
			AKiteRiderPawn* Rider = FindPlayerRider();
			const UKiteComponent* Kite = Rider ? Rider->GetKite() : nullptr;
			const UBoardMovementComponent* Board = Rider ? Rider->GetBoardMovement() : nullptr;
			const UWorld* World = Rider ? Rider->GetWorld() : nullptr;
			if (!Kite || !Board || !World)
			{
				return true;
			}
			const float Dt = World->GetDeltaSeconds();
			const float Elevation = Kite->GetElevationDeg();
			const float ClimbRate = PopAtTopLastElevation >= 0.0f && Dt > KINDA_SMALL_NUMBER ? (Elevation - PopAtTopLastElevation) / Dt : UE_BIG_NUMBER;
			PopAtTopLastElevation = Elevation;
			// In the air: crouch on the way down for the landing, and stand again once back on the water.
			const bool bAirborne = Board->GetBoardState() == EBoardState::Airborne;
			if (bAirborne)
			{
				PopAtTopBarInSeconds = -1.0f;
				if (Board->Velocity.Z < 0.0f && !Board->IsLoadHeld())
				{
					Rider->SetLoadHeld(true);
					bPopAtTopCrouched = true;
				}
				return true;
			}
			if (bPopAtTopCrouched)
			{
				Rider->SetLoadHeld(false);
				bPopAtTopCrouched = false;
				return true;
			}
			const bool bLoadedOnWater = Board->IsLoadHeld() && !Board->IsCrashing();
			if (!bLoadedOnWater)
			{
				PopAtTopBarInSeconds = -1.0f;
				return true;
			}
			if (PopAtTopBarInSeconds < 0.0f)
			{
				if (LessonTiming::IsKiteAtTop(Elevation, ClimbRate))
				{
					// The bar in at the top (0.8 with the bar springing about the middle), then the pop.
					Rider->OnSheetTriggered(FInputActionValue(0.6f));
					PopAtTopBarInSeconds = 0.0f;
					UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.PopAtTop: kite at the top (%.1f deg, climbing %.1f deg/s): bar in"), Elevation, ClimbRate);
				}
				return true;
			}
			PopAtTopBarInSeconds += Dt;
			if (PopAtTopBarInSeconds >= 0.15f)
			{
				Rider->ReleaseLoadAndPop();
				// Off the edge and the tail for the flight: a held edge yaws the board in the air.
				Rider->EdgeBoard(0.0f);
				if (UBoardMovementComponent* RiderBoard = Rider->GetBoardMovement())
				{
					RiderBoard->SetWeightShift(0.0f);
				}
				PopAtTopBarInSeconds = -1.0f;
				UE_LOG(LogKiteSurf, Display, TEXT("kitesurf.PopAtTop: pop"));
			}
			return true;
		}));
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
			UE_LOG(LogKiteSurf, Warning, TEXT("Usage: kitesurf.Shot <Chase|Side|Low|Orbit|Wide|KiteView|Close>"));
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
