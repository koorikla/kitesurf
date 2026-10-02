#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "KiteComponent.generated.h"

class UWindComponent;
class UStaticMeshComponent;
class UCableComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class KITESURF_API UKiteComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKiteComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Core simulation update (callable directly from automation tests or TickComponent)
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void UpdateKite(float DeltaTime);

	// Inputs
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SteerKite(float Axis /* -1..1 */);

	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SheetKite(float Amount /* 0..1 */);

	// Outputs
	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetLineForce() const; // Force on rider in kg*cm/s^2 (1 N = 100 kg*cm/s^2)

	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetLineTensionN() const; // Line tension in Newtons

	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetKiteWorldPosition() const;

	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetAzimuthDeg() const;

	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetElevationDeg() const;

	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetKiteVelocity() const;

	UFUNCTION(BlueprintCallable, Category = "Kite")
	FRotator GetKiteRotation() const;

	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetAzimuthDeg(float InAzimuthDeg);

	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetElevationDeg(float InElevationDeg);

	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetWindComponent(UWindComponent* InWindComponent);

	// Visual Components
	UStaticMeshComponent* GetKiteMesh() const { return KiteMesh; }
	UCableComponent* GetLeftLine() const { return LeftLine; }
	UCableComponent* GetRightLine() const { return RightLine; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Config", meta = (ClampMin = "-90.0", ClampMax = "90.0"))
	float AzimuthDeg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Config", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float ElevationDeg;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Config", meta = (ClampMin = "100.0"))
	float LineLengthCm; // Default 2400 = 24m

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Config", meta = (ClampMin = "1.0"))
	float AreaM2; // Default 12 m^2

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Config", meta = (ClampMin = "0.1"))
	float MassKg; // Default 3 kg

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|State", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Sheet; // 0..1

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|State", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Steer; // -1..1

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float SteerSensitivity; // Steering rate multiplier (deg / (s * (m/s)))

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float DepowerDriftRate; // Elevation drift rate when sheeted out (deg/s)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Debug")
	bool bDrawDebug;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UWindComponent> WindComponent;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> KiteMesh;

	UPROPERTY(Transient)
	TObjectPtr<UCableComponent> LeftLine;

	UPROPERTY(Transient)
	TObjectPtr<UCableComponent> RightLine;

	void SetupVisuals();
	void UpdateVisuals();

	FVector GetWindAt(const FVector& Location) const;
	void ComputeKiteTransform(FVector& OutKitePos, FVector& OutLineDir) const;

	FVector KiteWorldPosition;
	FRotator KiteWorldRotation;
	FVector LastKitePosition;
	FVector KiteVelocity;
	bool bHasLastPosition;

	float LineTensionN;
	FVector LineForce;
};
