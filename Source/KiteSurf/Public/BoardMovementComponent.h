#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "BoardMovementComponent.generated.h"

class AWaterBody;
class UWaterBodyComponent;

UENUM(BlueprintType)
enum class EBoardDragRegime : uint8
{
	Displacement UMETA(DisplayName = "Displacement"),
	Planing      UMETA(DisplayName = "Planing")
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class KITESURF_API UBoardMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UBoardMovementComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Accumulate an external force in kg*cm/s^2 (applied during next tick) */
	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	void AddExternalForce(const FVector& Force);

	/** Set heel/toe edge input clamped to [-1.0, 1.0] */
	UFUNCTION(BlueprintCallable, Category = "Board|Input")
	void SetEdgeInput(float Value);

	UFUNCTION(BlueprintCallable, Category = "Board|Input")
	float GetEdgeInput() const { return CurrentEdgeInput; }

	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	bool IsPlaning() const { return CurrentDragRegime == EBoardDragRegime::Planing; }

	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	EBoardDragRegime GetCurrentDragRegime() const { return CurrentDragRegime; }

	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	float GetForwardSpeed() const;

	UFUNCTION(BlueprintCallable, Category = "Board|Physics")
	float GetLateralSpeed() const;

	/** Samples water height and normal at the given world location */
	void SampleWaterSurface(const FVector& Location, float& OutWaterHeight, FVector& OutWaterNormal) const;

public:
	// Tunables (Spec)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Tunables")
	float MassKg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Tunables")
	float BoardLengthCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Tunables")
	float BoardWidthCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Tunables")
	float BuoyancyN;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Tunables")
	float PlaningThresholdCmS;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Tunables")
	float DisplacementDragCoef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Tunables")
	float PlaningDragCoef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Tunables")
	float EdgeGripCoef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Tunables")
	float MaxEdgeAngleDeg;

	// Additional physics tuning
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Physics")
	float BaseLateralDragCoef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Physics")
	float BuoyancySpringStiffness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Physics")
	float BuoyancyDamping;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Physics")
	float PlaningLiftCoef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Physics")
	float CarveTurnRate;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	EBoardDragRegime CurrentDragRegime;

	float CurrentEdgeInput;
	FVector AccumulatedExternalForce;

	mutable TWeakObjectPtr<const UWaterBodyComponent> CachedWaterBodyComponent;
};
