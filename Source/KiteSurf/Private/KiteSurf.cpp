#include "KiteSurf.h"
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "Misc/CoreDelegates.h"
#include "UnrealClient.h"

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
	}

	virtual void ShutdownModule() override
	{
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
