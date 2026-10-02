#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Styling/SlateBrush.h"
#include "KiteSurfMenuVideo.generated.h"

class UFileMediaSource;
class UMediaPlayer;
class UMediaTexture;
class UTexture2D;

/** One video playing into a texture that Slate can draw. */
UCLASS()
class KITESURF_API UKiteSurfVideoPlayer : public UObject
{
	GENERATED_BODY()

public:
	/** Starts playing a file under Content/ (e.g. "Movies/MenuLoop.webm"); false if it cannot be opened. */
	bool Open(const FString& ContentRelativePath, bool bLoop);

	void Close();

	/** True once the video is drawing frames; until then the brush shows nothing. */
	bool HasFrames() const;

	/** True when the file was opened and has not reached its end (looping videos never do). */
	bool IsActive() const { return bActive; }

	/** The video as a brush, transparent until the first frame arrives. */
	const FSlateBrush* GetBrush() const { return &Brush; }

	UMediaPlayer* GetPlayer() const { return Player; }
	UFileMediaSource* GetSource() const { return Source; }

	/** Broadcast when a video that does not loop reaches its end, or fails to open. */
	FSimpleMulticastDelegate OnFinished;

private:
	UFUNCTION()
	void HandleEndReached();

	UFUNCTION()
	void HandleOpenFailed(FString FailedUrl);

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> Player;

	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> Texture;

	UPROPERTY(Transient)
	TObjectPtr<UFileMediaSource> Source;

	FSlateBrush Brush;
	bool bActive = false;
};

/**
 * The pre-rendered menu videos (filmed by scripts/render-menu-video.sh): the intro played once at
 * startup and the loop behind the menus. Lives as long as the game, so moving between menu screens
 * does not restart the loop. Also owns the loading screen shown while a ride loads.
 */
UCLASS()
class KITESURF_API UKiteSurfMenuVideoSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* IntroPath = TEXT("Movies/Intro.webm");
	static constexpr const TCHAR* LoopPath = TEXT("Movies/MenuLoop.webm");

	/** Full path of a video under Content/. */
	static FString GetVideoFilePath(const TCHAR* ContentRelativePath);

	/** Videos need a renderer and the file; under -nullrhi or without the file the menus show the still art. */
	static bool CanPlayVideo(const TCHAR* ContentRelativePath);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The intro plays once, the first time the main menu opens. */
	bool ShouldPlayIntro() const;

	/** Starts the intro; null if it cannot play, in which case it counts as played. */
	UKiteSurfVideoPlayer* StartIntro();

	/** The intro has finished or been skipped: stop it and start the loop. */
	void FinishIntro();

	bool HasPlayedIntro() const { return bIntroPlayed; }

	/** The menu background loop, started on first use; null if it cannot play. */
	UKiteSurfVideoPlayer* GetLoop();

	/** Stops the loop, e.g. when a ride starts. */
	void StopLoop();

	/**
	 * Stops the loop and sets up the loading screen (the still art, the name and LOADING) for the next
	 * map load. Call just before opening a level: the movie player shows it when the load starts.
	 */
	void PrepareLoadingScreen();

private:
	UPROPERTY(Transient)
	TObjectPtr<UKiteSurfVideoPlayer> Intro;

	UPROPERTY(Transient)
	TObjectPtr<UKiteSurfVideoPlayer> Loop;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LoadingTexture;
	FSlateBrush LoadingBrush;

	bool bIntroPlayed = false;
};
