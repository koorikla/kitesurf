#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KiteSurfSpot.generated.h"

class AKiteRiderPawn;
class UStaticMesh;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class ESpotObstacleType : uint8
{
	Island,
	Sandbar
};

/** A piece of sand in the water. Its shape is the top of a flattened ellipsoid, as built by generate_mesh_objs.py. */
USTRUCT(BlueprintType)
struct FSpotObstacle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Spot")
	ESpotObstacleType Type = ESpotObstacleType::Sandbar;

	/** Centre on the water (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Spot")
	FVector2D Centre = FVector2D::ZeroVector;

	/** Which way its long axis points (deg). */
	UPROPERTY(BlueprintReadOnly, Category = "Spot")
	float YawDeg = 0.0f;

	/** How far the sand stands above the water at this point (cm); zero or less where there is none. */
	float GetSandHeightCm(const FVector2D& WorldXY) const;

	/** Half length, half width and height above the water of the part that shows (cm). */
	FVector GetVisibleExtent() const;
};

/** A shark: patrols a circle, and goes for a rider who is down in the water nearby. */
USTRUCT(BlueprintType)
struct FSpotShark
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Spot")
	FVector2D PatrolCentre = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Spot")
	FVector2D Position = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Spot")
	float HeadingDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Spot")
	bool bHunting = false;

	float PatrolAngleDeg = 0.0f;
	float CalmSeconds = 0.0f;
};

/**
 * What is in the water at the spot: islands and sandbars to ride round or jump over, and sharks.
 * Spawned by the game mode when a ride starts, laid out around the start position relative to
 * the wind, and switched on and off from the gear screen. Riding onto sand or into a shark is a
 * crash; anything the rider is higher than, they clear.
 */
UCLASS()
class KITESURF_API AKiteSurfSpot : public AActor
{
	GENERATED_BODY()

public:
	AKiteSurfSpot();

	virtual void Tick(float DeltaTime) override;

	/** Lays the spot out round Origin for wind blowing along DownwindDir, and watches this rider. */
	UFUNCTION(BlueprintCallable, Category = "Spot")
	void Setup(AKiteRiderPawn* InRider, const FVector& Origin, const FVector& DownwindDir, bool bIslands, bool bSandbars, bool bSharks);

	/** Switches features on and off, keeping the layout where it is. */
	UFUNCTION(BlueprintCallable, Category = "Spot")
	void SetFeatures(bool bIslands, bool bSandbars, bool bSharks);

	/** Advances the sharks and checks the rider against the sand and the sharks. Tick calls this. */
	UFUNCTION(BlueprintCallable, Category = "Spot")
	void StepSpot(float DeltaTime);

	const TArray<FSpotObstacle>& GetObstacles() const { return Obstacles; }
	const TArray<FSpotShark>& GetSharks() const { return Sharks; }
	TArray<FSpotShark>& GetMutableSharks() { return Sharks; }

	/** Height of sand above the water at this point (cm), from whichever obstacle is there; zero or less in open water. */
	UFUNCTION(BlueprintCallable, Category = "Spot")
	float GetSandHeightCm(const FVector& WorldLocation) const;

	/** What last happened to the rider here ("Ran aground", "Shark!"), for the HUD and tests; empty if nothing. */
	UFUNCTION(BlueprintCallable, Category = "Spot")
	FString GetLastEvent() const { return LastEvent; }

	/** No sand closer to the start than this, so a ride always opens on clear water (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spot")
	float ClearStartRadiusCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spot|Sharks")
	int32 SharkCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spot|Sharks")
	float SharkPatrolRadiusCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spot|Sharks")
	float SharkPatrolSpeedCmS;

	/** Speed towards a rider who is down in the water (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spot|Sharks")
	float SharkHuntSpeedCmS;

	/** A shark notices a floating rider inside this distance (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spot|Sharks")
	float SharkNoticeRadiusCm;

	/** Closer than this on the water, the shark has the rider (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spot|Sharks")
	float SharkBiteRadiusCm;

	/** A rider this far above the water is over the shark, not in it (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spot|Sharks")
	float SharkClearHeightCm;

private:
	void RebuildVisuals();
	void UpdateSharkVisuals();
	void CrashRider(const FString& Event, const FVector& PutBackAt);
	FVector FindWaterNear(const FVector& From, const FVector& TowardsDir) const;

	UPROPERTY(Transient)
	TObjectPtr<AKiteRiderPawn> Rider;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ObstacleMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> SharkMeshes;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> IslandMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SandbarMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SharkMesh;

	TArray<FSpotObstacle> Obstacles;
	TArray<FSpotShark> Sharks;
	FVector SpotOrigin = FVector::ZeroVector;
	FVector SpotDownwind = FVector::ForwardVector;
	bool bHasLayout = false;
	bool bIslandsOn = true;
	bool bSandbarsOn = true;
	bool bSharksOn = true;
	FString LastEvent;
};
