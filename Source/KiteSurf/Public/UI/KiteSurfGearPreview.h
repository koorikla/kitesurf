#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KiteGear.h"
#include "RiderCharacter.h"
#include "KiteSurfGearPreview.generated.h"

class UPointLightComponent;
class USceneCaptureComponent2D;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * The gear screen's preview: the chosen rider on the chosen board, turning slowly on a stand under the
 * chosen kite, filmed by its own camera into a texture the gear screen draws.
 *
 * Spawned by the gear screen in whatever level is loaded, high above it, and drawn only into its
 * own camera (the meshes are visible to scene captures only), so it never shows in the game view.
 * What each choice looks like comes from RiderCharacter::GetStaticMeshPath and
 * KiteGear::GetMeshPath / GetLengthScale, the same as on the water.
 */
UCLASS()
class KITESURF_API AKiteSurfGearPreview : public AActor
{
	GENERATED_BODY()

public:
	AKiteSurfGearPreview();

	virtual void Tick(float DeltaTime) override;

	/** Where the stand is put: far above the water, out of sight of the game camera. */
	static FVector GetStageLocation() { return FVector(0.0f, 0.0f, 100000.0f); }

	/** Size of the picture (px); the gear screen shows it at this aspect. */
	static constexpr int32 ImageWidth = 720;
	static constexpr int32 ImageHeight = 900;

	/** Dresses the stand: rider, kite model and size (m2), board. */
	void ShowGear(ERiderCharacter Rider, EKiteModel KiteModel, float KiteSizeM2, EBoardSize Board);

	/** Turns the stand by hand (deg), e.g. from a mouse drag; the slow turn waits a moment, then carries on. */
	void TurnBy(float DeltaYawDeg);

	/** The picture: the gear screen draws this. */
	UTextureRenderTarget2D* GetRenderTarget();

	float GetTurnYawDeg() const { return TurnYawDeg; }

	UStaticMeshComponent* GetBoardMesh() const { return BoardMesh; }
	UStaticMeshComponent* GetRiderStaticMesh() const { return RiderStaticMesh; }
	UStaticMeshComponent* GetKiteMesh() const { return KiteMesh; }
	USceneCaptureComponent2D* GetCapture() const { return Capture; }

	/** How fast the stand turns by itself (deg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	float AutoTurnDegPerSec = 18.0f;

	/** After a turn by hand, how long before it turns by itself again (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	float AutoTurnResumeSeconds = 2.5f;

protected:
	virtual void PostInitializeComponents() override;

private:
	void ApplyTurn();

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneComponent> Stand;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UStaticMeshComponent> BoardMesh;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UStaticMeshComponent> RiderStaticMesh;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UStaticMeshComponent> KiteMesh;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UStaticMeshComponent> Backdrop;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UPointLightComponent> KeyLight;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UPointLightComponent> RimLight;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	float TurnYawDeg = 0.0f;
	float SecondsSinceManualTurn = 1000.0f;
};
