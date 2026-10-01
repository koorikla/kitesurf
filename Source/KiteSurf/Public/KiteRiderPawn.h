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
class UKiteComponent;
class UInputMappingContext;
class UInputAction;

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

	UKiteComponent* GetKite() const { return Kite.Get(); }
	UBoardMovementComponent* GetBoardMovement() const { return BoardMovement.Get(); }
	USkeletalMeshComponent* GetRiderMesh() const { return RiderMesh.Get(); }
	UStaticMeshComponent* GetControlBarMesh() const { return ControlBarMesh.Get(); }

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
	TObjectPtr<UInputAction> PauseAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UFUNCTION(BlueprintCallable, Category = "UI")
	void TogglePause();

private:
	void OnSteerTriggered(const FInputActionValue& Value);
	void OnSheetTriggered(const FInputActionValue& Value);
	void OnEdgeTriggered(const FInputActionValue& Value);
	void OnPauseTriggered(const FInputActionValue& Value);
	void OnJumpTriggered(const FInputActionValue& Value);

	float CurrentSteerInput;
	float CurrentSheetInput;
	float KiteAzimuthDeg;
	FVector BoardVelocity;
};
