#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BoardWakeComponent.generated.h"

class UBoardMovementComponent;
class UInstancedStaticMeshComponent;

/**
 * Foam trail and spray thrown up by the board. Both are small CPU-simulated pools drawn as
 * instanced meshes, so the effect needs no Niagara assets and can be checked in headless tests.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class KITESURF_API UBoardWakeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBoardWakeComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Advance the foam and spray pools; called from TickComponent, public so tests can step it. */
	void Simulate(float DeltaTime);

	int32 GetNumFoamPatches() const { return FoamPatches.Num(); }
	int32 GetNumSprayDrops() const { return SprayDrops.Num(); }

	/** Throw a ring of spray, used for landings and crashes. Intensity 1 is a normal landing. */
	UFUNCTION(BlueprintCallable, Category = "Wake")
	void EmitSplash(float Intensity);

	/** Board speed below which the board leaves no wake (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake")
	float MinWakeSpeedCmS;

	/** Distance travelled between foam patches (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|Foam")
	float FoamSpacingCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|Foam")
	float FoamLifetime;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|Foam")
	float FoamStartSizeCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|Foam")
	float FoamEndSizeCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|Foam")
	float FoamOpacity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|Foam")
	int32 MaxFoamPatches;

	/** Spray drops per second at 20 kn with the board fully loaded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|Spray")
	float SprayRatePerSec;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|Spray")
	int32 MaxSprayDrops;

protected:
	virtual void BeginPlay() override;

private:
	struct FFoamPatch
	{
		FVector2D Position;
		float YawDeg;
		float Age;
		float Strength;
	};

	struct FSprayDrop
	{
		FVector Position;
		FVector Velocity;
		float Age;
		float Lifetime;
		float SizeCm;
	};

	UFUNCTION()
	void HandleBoardLanding(float LandingG);

	UFUNCTION()
	void HandleBoardCrash(float Intensity);

	UFUNCTION()
	void HandleKiteCrashed(FVector WaterLocation);

	void EmitSplashAt(const FVector& Location, float Intensity);

	UInstancedStaticMeshComponent* CreateInstances(const TCHAR* Name, const TCHAR* MeshPath, const TCHAR* MaterialPath, int32 Count);
	void UpdateInstances();
	void AddSprayDrop(const FVector& Position, const FVector& Velocity);

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> FoamInstances;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> SprayInstances;

	UPROPERTY(Transient)
	TObjectPtr<UBoardMovementComponent> BoardMovement;

	TArray<FFoamPatch> FoamPatches;
	TArray<FSprayDrop> SprayDrops;
	float DistanceSinceFoamCm;
	float SprayDebt;
	FRandomStream Random;
};
