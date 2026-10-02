#include "UI/KiteSurfMainMenuGameMode.h"
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
