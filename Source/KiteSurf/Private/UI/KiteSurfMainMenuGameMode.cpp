#include "UI/KiteSurfMainMenuGameMode.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfMainMenuWidget.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"

AKiteSurfMainMenuGameMode::AKiteSurfMainMenuGameMode()
{
	DefaultPawnClass = nullptr;
	HUDClass = nullptr;
}

void AKiteSurfMainMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Music for the menus, at the saved music volume.
	if (USoundBase* Track = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/MU_Menu")))
	{
		const UKiteSurfGameInstance* GI = Cast<UKiteSurfGameInstance>(World->GetGameInstance());
		MenuMusic = UGameplayStatics::SpawnSound2D(this, Track, 1.0f, 1.0f, 0.0f, nullptr, false, false);
		SetMusicVolume(GI ? GI->MusicVolume : 0.6f);
	}

	TSubclassOf<UUserWidget> ClassToSpawn = MainMenuWidgetClass ? MainMenuWidgetClass : TSubclassOf<UUserWidget>(UKiteSurfMainMenuWidget::StaticClass());
	ActiveMainMenuWidget = CreateWidget<UUserWidget>(World, ClassToSpawn);
	if (ActiveMainMenuWidget)
	{
		ActiveMainMenuWidget->AddToViewport(0);
	}

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		PC->bShowMouseCursor = true;
		if (ActiveMainMenuWidget)
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetWidgetToFocus(ActiveMainMenuWidget->TakeWidget());
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PC->SetInputMode(InputMode);
			if (UKiteSurfMainMenuWidget* MainMenu = Cast<UKiteSurfMainMenuWidget>(ActiveMainMenuWidget))
			{
				MainMenu->FocusFirst();
			}
		}
	}
}

void AKiteSurfMainMenuGameMode::SetMusicVolume(float Volume)
{
	if (MenuMusic)
	{
		MenuMusic->SetVolumeMultiplier(0.9f * FMath::Clamp(Volume, 0.0f, 1.0f));
	}
}
