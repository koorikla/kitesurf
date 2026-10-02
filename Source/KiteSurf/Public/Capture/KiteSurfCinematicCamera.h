#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KiteSurfCinematicCamera.generated.h"

class AKiteRiderPawn;
class UCameraComponent;

/** A framing of the rider and kite for the menu video. Chase is the rider's own camera. */
UENUM(BlueprintType)
enum class EKiteSurfShot : uint8
{
	Chase,
	/** Tracks alongside at the rider's speed, side-on, rider and kite both in frame. */
	Side,
	/** Just above the water ahead of the rider, looking back up at rider and kite. */
	Low,
	/** Circles the rider slowly. */
	Orbit,
	/** Planted well ahead and to the side when the shot starts; pans to follow as the rider passes. */
	Wide,
	/** Behind and above the kite, looking down the lines at the rider. */
	KiteView
};

/** Where the camera is and what it looks at for one frame of a shot. */
struct FKiteSurfShotFrame
{
	FVector Location = FVector::ZeroVector;
	FVector LookAt = FVector::ZeroVector;
	float FieldOfViewDeg = 70.0f;
};

/**
 * Films the rider for the pre-rendered menu video (scripts/render-menu-video.sh). Spawned by the
 * kitesurf.Shot console command, which makes it the player's view; each new shot is a cut.
 * Not used in normal play.
 */
UCLASS()
class KITESURF_API AKiteSurfCinematicCamera : public AActor
{
	GENERATED_BODY()

public:
	AKiteSurfCinematicCamera();

	virtual void Tick(float DeltaTime) override;

	/** Starts a shot of this rider. Chase hands the view back to the rider's camera. */
	void StartShot(EKiteSurfShot InShot, AKiteRiderPawn* InRider);

	EKiteSurfShot GetShot() const { return Shot; }

	/** Shot names as typed after kitesurf.Shot; false if the name is not a shot. */
	static bool ParseShot(const FString& Name, EKiteSurfShot& OutShot);

	/**
	 * Where a shot puts the camera, from the rider's position and velocity, the kite's position, how long
	 * the shot has run, where the shot started (for shots that stay planted) and the height of the water
	 * under the rider. All in cm.
	 */
	static FKiteSurfShotFrame ComputeShotFrame(EKiteSurfShot Shot, const FVector& RiderLocation, const FVector& RiderVelocity,
		const FVector& KiteLocation, float ShotSeconds, const FVector& ShotAnchor, float WaterZ);

	/** Where a planted shot (Wide) stands, worked out once when it starts. */
	static FVector ComputeShotAnchor(EKiteSurfShot Shot, const FVector& RiderLocation, const FVector& RiderVelocity, const FVector& KiteLocation, float WaterZ);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

private:
	void UpdateFrame(float DeltaTime, bool bSnap);
	float GetWaterZ() const;

	UPROPERTY(Transient)
	TObjectPtr<AKiteRiderPawn> Rider;

	EKiteSurfShot Shot = EKiteSurfShot::Chase;
	float ShotSeconds = 0.0f;
	FVector ShotAnchor = FVector::ZeroVector;
	FVector SmoothedLookAt = FVector::ZeroVector;
	FVector SmoothedLocation = FVector::ZeroVector;
};
