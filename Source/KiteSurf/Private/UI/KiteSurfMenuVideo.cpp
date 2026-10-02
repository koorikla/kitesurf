#include "UI/KiteSurfMenuVideo.h"
#include "FileMediaSource.h"
#include "HAL/FileManager.h"
#include "KiteSurf.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Misc/Paths.h"
#include "MoviePlayer.h"
#include "RHI.h"
#include "UI/KiteSurfMenuStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

bool UKiteSurfVideoPlayer::Open(const FString& ContentRelativePath, bool bLoop)
{
	Close();

	Player = NewObject<UMediaPlayer>(this, NAME_None, RF_Transient);
	Player->PlayOnOpen = true;
	Player->SetLooping(bLoop);
	Player->OnEndReached.AddDynamic(this, &UKiteSurfVideoPlayer::HandleEndReached);
	Player->OnMediaOpenFailed.AddDynamic(this, &UKiteSurfVideoPlayer::HandleOpenFailed);

	// Cleared to transparent, so whatever is behind the video shows until its first frame.
	Texture = NewObject<UMediaTexture>(this, NAME_None, RF_Transient);
	Texture->AutoClear = true;
	Texture->ClearColor = FLinearColor::Transparent;
	Texture->SetMediaPlayer(Player);
	Texture->UpdateResource();

	Brush = FSlateBrush();
	Brush.SetResourceObject(Texture);
	Brush.ImageSize = FVector2D(1920.0f, 1080.0f);
	Brush.DrawAs = ESlateBrushDrawType::Image;

	// "./" is the project's Content directory, wherever the game is installed.
	Source = NewObject<UFileMediaSource>(this, NAME_None, RF_Transient);
	Source->SetFilePath(TEXT("./") + ContentRelativePath);

	bActive = Player->OpenSource(Source);
	if (!bActive)
	{
		UE_LOG(LogKiteSurf, Warning, TEXT("Menu video %s could not be opened"), *ContentRelativePath);
	}
	return bActive;
}

void UKiteSurfVideoPlayer::Close()
{
	bActive = false;
	if (Player)
	{
		Player->OnEndReached.RemoveAll(this);
		Player->OnMediaOpenFailed.RemoveAll(this);
		Player->Close();
	}
}

bool UKiteSurfVideoPlayer::HasFrames() const
{
	return bActive && Player && Player->IsPlaying() && Player->GetTime() > FTimespan::Zero();
}

void UKiteSurfVideoPlayer::HandleEndReached()
{
	if (Player && !Player->IsLooping())
	{
		bActive = false;
		OnFinished.Broadcast();
	}
}

void UKiteSurfVideoPlayer::HandleOpenFailed(FString FailedUrl)
{
	UE_LOG(LogKiteSurf, Warning, TEXT("Menu video failed to open: %s"), *FailedUrl);
	bActive = false;
	OnFinished.Broadcast();
}

FString UKiteSurfMenuVideoSubsystem::GetVideoFilePath(const TCHAR* ContentRelativePath)
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / ContentRelativePath);
}

bool UKiteSurfMenuVideoSubsystem::CanPlayVideo(const TCHAR* ContentRelativePath)
{
	return FApp::CanEverRender() && GDynamicRHI != nullptr && IFileManager::Get().FileExists(*GetVideoFilePath(ContentRelativePath));
}

void UKiteSurfMenuVideoSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bIntroPlayed = false;
}

void UKiteSurfMenuVideoSubsystem::Deinitialize()
{
	if (Intro)
	{
		Intro->Close();
	}
	StopLoop();
	Super::Deinitialize();
}

bool UKiteSurfMenuVideoSubsystem::ShouldPlayIntro() const
{
	return !bIntroPlayed && CanPlayVideo(IntroPath);
}

UKiteSurfVideoPlayer* UKiteSurfMenuVideoSubsystem::StartIntro()
{
	if (!ShouldPlayIntro())
	{
		bIntroPlayed = true;
		return nullptr;
	}
	if (!Intro)
	{
		Intro = NewObject<UKiteSurfVideoPlayer>(this);
	}
	if (!Intro->Open(IntroPath, false))
	{
		bIntroPlayed = true;
		return nullptr;
	}
	return Intro;
}

void UKiteSurfMenuVideoSubsystem::FinishIntro()
{
	bIntroPlayed = true;
	if (Intro)
	{
		Intro->OnFinished.Clear();
		Intro->Close();
	}
	GetLoop();
}

UKiteSurfVideoPlayer* UKiteSurfMenuVideoSubsystem::GetLoop()
{
	if (Loop && Loop->IsActive())
	{
		return Loop;
	}
	if (!CanPlayVideo(LoopPath))
	{
		return nullptr;
	}
	if (!Loop)
	{
		Loop = NewObject<UKiteSurfVideoPlayer>(this);
	}
	return Loop->Open(LoopPath, true) ? Loop.Get() : nullptr;
}

void UKiteSurfMenuVideoSubsystem::StopLoop()
{
	if (Loop)
	{
		Loop->Close();
	}
}

void UKiteSurfMenuVideoSubsystem::PrepareLoadingScreen()
{
	StopLoop();
	if (!IsMoviePlayerEnabled())
	{
		return;
	}

	if (!LoadingTexture)
	{
		LoadingTexture = KiteSurfMenuStyle::LoadBackgroundTexture();
		KiteSurfMenuStyle::SetupBackgroundBrush(LoadingBrush, LoadingTexture);
	}

	const TSharedRef<SWidget> Caption = SNew(SBox)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.0f, 0.0f, 70.0f, 60.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("KITESURF")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 40))
				.ColorAndOpacity(FLinearColor(0.96f, 0.96f, 0.94f))
				.ShadowOffset(FVector2D(2.0f, 2.0f))
				.ShadowColorAndOpacity(FLinearColor(0.0f, 0.05f, 0.12f, 0.6f))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("RIGGING UP...")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
				.ColorAndOpacity(FLinearColor(0.73f, 0.89f, 0.11f))
				.ShadowOffset(FVector2D(1.0f, 1.0f))
				.ShadowColorAndOpacity(FLinearColor(0.0f, 0.05f, 0.12f, 0.6f))
			]
		];

	FLoadingScreenAttributes Attributes;
	Attributes.bAutoCompleteWhenLoadingCompletes = true;
	Attributes.MinimumLoadingScreenDisplayTime = 1.0f;
	Attributes.WidgetLoadingScreen = KiteSurfMenuStyle::BuildBackdrop(&LoadingBrush, LoadingTexture != nullptr, Caption);
	GetMoviePlayer()->SetupLoadingScreen(Attributes);
}
