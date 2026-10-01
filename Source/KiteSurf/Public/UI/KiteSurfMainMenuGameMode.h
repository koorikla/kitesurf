#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "KiteSurfMainMenuGameMode.generated.h"

class UUserWidget;

UCLASS()
class KITESURF_API AKiteSurfMainMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKiteSurfMainMenuGameMode();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	TSubclassOf<UUserWidget> MainMenuWidgetClass;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UUserWidget> ActiveMainMenuWidget;

protected:
	virtual void BeginPlay() override;
};
