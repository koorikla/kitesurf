#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "KiteSurfGameMode.generated.h"

UCLASS()
class KITESURF_API AKiteSurfGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKiteSurfGameMode();

	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot) override;
	virtual void RestartPlayerAtTransform(AController* NewPlayer, const FTransform& SpawnTransform) override;

	/** Initial forward speed in cm/s given to pawn on spawn (default 12 kn = ~617.28 cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KiteSurf|Spawn")
	float InitialSpawnSpeedCmPerSec;
};
