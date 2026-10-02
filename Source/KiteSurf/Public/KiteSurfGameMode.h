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

	/** Clock position of the kite at the start of a ride (deg right of the zenith, looking downwind). */
	static constexpr float StartKiteClockDeg = 65.0f;

	/** Window depth of the kite at the start of a ride (deg from the window edge). */
	static constexpr float StartKiteDepthDeg = 8.0f;

	/** Bar position at the start of a ride (0 = sheeted out, 1 = sheeted in). */
	static constexpr float StartSheet = 0.7f;

	/** Puts the pawn on a beam reach at the given speed with the kite powered up on that side. */
	static void InitializeRide(class AKiteRiderPawn* RiderPawn, float InitialSpeedCmPerSec);

	/** Initial forward speed in cm/s given to pawn on spawn (default 12 kn = ~617.28 cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KiteSurf|Spawn")
	float InitialSpawnSpeedCmPerSec;
};
