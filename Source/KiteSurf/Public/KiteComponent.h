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

	/**
	 * Place the kite by clock position and window depth.
	 * ClockDeg: angle from the zenith around the wind axis, positive to the right looking downwind (90 = right horizon).
	 * DepthDeg: angle from the window edge towards straight downwind.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetWindowPosition(float ClockDeg, float DepthDeg);

	/** Clock position of the kite around the wind axis, positive to the right looking downwind. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetClockDeg() const;

	/** Angle of the kite from the window edge towards straight downwind. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetWindowDepthDeg() const;

	/** Horizontal unit vector the true wind blows towards, sampled at the rider. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetDownwindDir() const;

	/**
	 * Axis of the wind window: the direction the apparent wind (true wind minus the rider's
	 * smoothed velocity) blows towards. Azimuth, elevation, clock and depth are measured from it,
	 * so the kite falls back in the window as the rider speeds up, which is what limits board speed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetWindowAxis() const;

	/** Where each line meets the bar, in front of the rider on the kite's side. */
	FVector GetBarEndWorldPosition(bool bLeft) const;

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
	float MaxSteerRateDegPerSec; // Cap on how fast steering moves the kite around the window

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float MinElevationDeg; // Steering cannot fly the kite lower than this

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float EdgeDepthDeg; // Window depth the kite settles at when sheeted out

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float PowerDepthDeg; // Window depth the kite settles at when sheeted in

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float DepthRateDegPerSec; // How fast the kite moves towards its settled depth

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float WindowAxisResponse; // How quickly the window follows changes in rider velocity (1/s)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float MaxWindowSwingDeg; // Largest angle between the window axis and the true wind

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float MaxKiteAirspeedCmS; // Cap on the kite's own speed around the rider when computing apparent wind

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
	FVector GetRiderVelocity2D() const;

	/** Rider velocity as seen by the wind window; lags the real velocity so the kite does not twitch. */
	FVector WindowRiderVelocity;
	bool bSnapWindowRiderVelocity;
	FVector LastKiteOffset;
	void ComputeKiteTransform(FVector& OutKitePos, FVector& OutLineDir) const;

	FVector KiteWorldPosition;
	FRotator KiteWorldRotation;
	FVector LastKitePosition;
	FVector KiteVelocity;
	bool bHasLastPosition;

	float LineTensionN;
	FVector LineForce;
};
