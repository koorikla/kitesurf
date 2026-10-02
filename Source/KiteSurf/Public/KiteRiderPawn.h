#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "KiteRiderPawn.generated.h"

class UStaticMeshComponent;
class USkeletalMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UWindComponent;
class UBoardMovementComponent;
class UBoardWakeComponent;
class UKiteComponent;
class UInputMappingContext;
class UInputAction;
class UAudioComponent;
class USoundBase;

UCLASS()
class KITESURF_API AKiteRiderPawn : public APawn
{
	GENERATED_BODY()

public:
	AKiteRiderPawn();

	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// Public API
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SteerKite(float Axis /* -1..1 */);

	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SheetKite(float Amount /* 0..1 */);

	UFUNCTION(BlueprintCallable, Category = "Board")
	void EdgeBoard(float Axis /* -1..1 */);

	UFUNCTION(BlueprintCallable, Category = "Board")
	bool Jump();

	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetBoardVelocity() const;

	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetKiteAzimuthDeg() const;

	UInputMappingContext* GetDefaultMappingContext() const { return DefaultMappingContext.Get(); }
	UInputAction* GetSteerAction() const { return SteerAction.Get(); }
	UInputAction* GetSheetAction() const { return SheetAction.Get(); }
	UInputAction* GetEdgeAction() const { return EdgeAction.Get(); }
	UInputAction* GetJumpAction() const { return JumpAction.Get(); }
	UInputAction* GetPauseAction() const { return PauseAction.Get(); }

	UKiteComponent* GetKite() const { return Kite.Get(); }
	UBoardMovementComponent* GetBoardMovement() const { return BoardMovement.Get(); }
	UBoardWakeComponent* GetWake() const { return Wake.Get(); }
	UWindComponent* GetWind() const { return Wind.Get(); }
	USkeletalMeshComponent* GetRiderMesh() const { return RiderMesh.Get(); }
	UStaticMeshComponent* GetControlBarMesh() const { return ControlBarMesh.Get(); }

	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetCurrentSteerInput() const { return CurrentSteerInput; }

	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetCurrentSheetInput() const { return CurrentSheetInput; }

	/** Hold the bar moving in (+) or out (-), -1..1; 0 leaves it where it is. */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void SetSheetRateInput(float Axis);

	/** Sheet in (+) / out (-) input currently held, -1..1. The bar position itself is GetCurrentSheetInput(). */
	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetSheetRateInput() const { return SheetRateInput; }

	/** Bar travel per second at full sheet input (0..1 range). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite")
	float SheetRatePerSec;

	// Camera framing: the view sits behind the rider and turns far enough towards the kite to keep it on screen.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraArmLengthCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraBoomPitchDeg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraFOVDeg;

	/** Largest horizontal angle between the view direction and the kite (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraMaxKiteYawOffsetDeg;

	/** How far above the centre of the view the kite is allowed to sit before the camera tilts up (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraKiteHeadroomDeg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraMinLookPitchDeg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraMaxLookPitchDeg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraTurnSpeed;

	/** Lean away from the kite at full line load (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderMaxLeanDeg;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UKiteComponent> Kite;

protected:
	virtual void BeginPlay() override;

	// Components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BoardMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> RiderMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ControlBarMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWindComponent> Wind;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoardMovementComponent> BoardMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoardWakeComponent> Wake;

	// Procedural Audio Components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> AudioBedComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> PopSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> LandingSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> CrashSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> ResetSound;

	// Enhanced Input
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SteerAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SheetAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> EdgeAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> PauseAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ResetAction;

public:
	UFUNCTION(BlueprintCallable, Category = "Gameplay")
	void ResetRider();

	UFUNCTION(BlueprintCallable, Category = "UI")
	void TogglePause();

private:
	void OnSteerTriggered(const FInputActionValue& Value);
	void OnSheetTriggered(const FInputActionValue& Value);
	void OnEdgeTriggered(const FInputActionValue& Value);
	void OnJumpTriggered(const FInputActionValue& Value);
	void OnPauseTriggered(const FInputActionValue& Value);
	void OnResetTriggered(const FInputActionValue& Value);

	UFUNCTION()
	void HandleBoardCrash(float Intensity);

	UFUNCTION()
	void HandleBoardReset();

	UFUNCTION()
	void HandleBoardLanding(float LandingG);

	void UpdateAudioModulation(float DeltaTime);
	void UpdateCamera(float DeltaTime);
	void UpdateRiderPose(float DeltaTime);

	/** True once the kite simulation has placed the kite at line length from the rider. */
	bool HasKitePosition() const;

	float CurrentSteerInput;
	float CurrentSheetInput;
	float SheetRateInput;
	float CameraYawDeg;
	float CameraLookPitchDeg;
	float RiderFacingYawDeg;
	bool bViewInitialized;
	float KiteAzimuthDeg;
	FVector BoardVelocity;
};
