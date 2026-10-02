#include "KiteSurf.h"
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "Misc/CoreDelegates.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "KiteRiderPawn.h"
#include "WindComponent.h"
#include "UI/KiteSurfMainMenuWidget.h"
#include "UI/KiteSurfPauseMenuWidget.h"
#include "UObject/UObjectIterator.h"

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
			TEXT("Holds inputs on the player's rider. Usage: kitesurf.Input <Steer -1..1> <SheetRate -1..1> <Turn -1..1> <WeightShift -1..1> <Loop 0|1>"),
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
						Wind->BaseWind = Direction * Knots * 51.44f;
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
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Input"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Jump"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.Wind"));
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("kitesurf.SmokeFrames"));
		FDefaultGameModuleImpl::ShutdownModule();
	}

private:
	FTSTicker::FDelegateHandle TickerHandle;
	int32 RemainingFrames = 0;

	// Frames before exit at which the screenshot is requested, leaving time for it to be written.
	static constexpr int32 ScreenshotLeadFrames = 30;

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
