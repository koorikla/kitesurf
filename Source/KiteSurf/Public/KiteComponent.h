#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "KiteComponent.generated.h"

class UWindComponent;
class UStaticMeshComponent;
class UCableComponent;

/**
 * The kite, flown on the sphere of its lines.
 *
 * The kite has a position on the sphere and a heading along it. It flies forward along its
 * heading at an airspeed of (glide ratio x the wind blowing along the lines), and drifts with
 * the wind blowing across them. With its nose pointing out of the window the two cancel and the
 * kite parks at the window edge.
 *
 * Steering asks for a direction of travel round the window; the kite stops where the bar is
 * centred. With the loop input held, steering turns the kite directly at a rate proportional to
 * its airspeed, so holding the bar over flies a loop.
 *
 * Line tension follows the kite's airspeed squared, so a kite diving through the middle of the
 * window pulls several times harder than a parked one.
 */
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

	/** While held, steering turns the kite directly instead of choosing a direction of travel, so holding the bar over flies a loop. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetLoopHeld(bool bHeld);

	UFUNCTION(BlueprintCallable, Category = "Kite")
	bool IsLoopHeld() const { return bLoopHeld; }

	// Outputs
	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetLineForce() const; // Force on rider in kg*cm/s^2 (1 N = 100 kg*cm/s^2)

	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetLineTensionN() const; // Line tension in Newtons

	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetKiteWorldPosition() const;

	/** Angle of the kite to the right of straight downwind (true wind), -90..90. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetAzimuthDeg() const;

	/** Angle of the kite above the horizon, 0..90. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetElevationDeg() const;

	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetKiteVelocity() const;

	UFUNCTION(BlueprintCallable, Category = "Kite")
	FRotator GetKiteRotation() const;

	/** Unit vector along the sphere in the direction the kite's nose points. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetKiteHeading() const { return KiteHeading; }

	/** Speed of the kite through the air along its heading (cm/s). */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetAirspeedCmS() const { return AirspeedCmS; }

	/** Degrees the kite has turned under the current steering input; 360 is one loop. Positive to the right. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetTurnDeg() const { return TurnDeg; }

	/** Place the kite parked at this azimuth (keeps elevation). */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetAzimuthDeg(float InAzimuthDeg);

	/** Place the kite parked at this elevation (keeps azimuth). */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetElevationDeg(float InElevationDeg);

	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetWindComponent(UWindComponent* InWindComponent);

	/**
	 * Place the kite by clock position and window depth, measured from the true wind.
	 * ClockDeg: angle from the zenith around the wind axis, positive to the right looking downwind (90 = right horizon).
	 * DepthDeg: angle from the window edge towards straight downwind.
	 * The kite then settles to the depth its glide ratio gives it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetWindowPosition(float ClockDeg, float DepthDeg);

	/** Clock position of the kite around the window axis, positive to the right looking downwind. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetClockDeg() const;

	/** Angle of the kite from the window edge towards the middle of the window. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetWindowDepthDeg() const;

	/** Horizontal unit vector the true wind blows towards, sampled at the rider. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetDownwindDir() const;

	/** Horizontal unit vector the apparent wind (true wind minus rider velocity) blows towards: the axis of the window the rider feels. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetWindowAxis() const;

	/** Where each line meets the bar, in front of the rider on the kite's side. */
	FVector GetBarEndWorldPosition(bool bLeft) const;

	// Visual Components
	UStaticMeshComponent* GetKiteMesh() const { return KiteMesh; }
	UCableComponent* GetLeftLine() const { return LeftLine; }
	UCableComponent* GetRightLine() const { return RightLine; }

	/** Derived from the kite's position each update; use the setters to place the kite. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Kite|State")
	float AzimuthDeg;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Kite|State")
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

	/** Lift to drag with the bar out. Sets how fast the kite flies and how far forward it parks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float GlideRatioSheetedOut;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float GlideRatioSheetedIn;

	/** Radius of the tightest loop, at full steering (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float MinTurnRadiusCm;

	/** Cap on the kite's airspeed, standing in for line drag (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float MaxAirspeedCmS;

	/** How quickly the kite reaches its airspeed (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float AirspeedResponse;

	/** Heading at full steering, measured from nose-out: just past along-the-edge, so the kite also dips into the window and gains power as it travels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float TravelHeadingDeg;

	/** The kite turns towards the heading the bar asks for at this gain (1/s)... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float SteerAssistGain;

	/** ...up to this rate (deg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float SteerAssistMaxRateDegPerSec;

	/** The kite is kept at least this far above the water. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float MinElevationDeg;

	/** Cap on line tension, standing in for line stretch and the rider letting go (N). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float MaxLineTensionN;

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
	FVector GetRiderPosition() const;
	FVector GetRiderVelocity() const;

	/** Direction from the rider for an azimuth and elevation measured from the true wind. */
	FVector DirectionFromAngles(float InAzimuthDeg, float InElevationDeg) const;

	/** Rebuilds the flight state from AzimuthDeg / ElevationDeg: parked there, nose out of the window. */
	void PlaceParked();

	/** Updates AzimuthDeg / ElevationDeg from KiteDir. */
	void UpdateAngles();

	/** Unit vector from the rider to the kite. */
	FVector KiteDir;

	/** Unit vector along the sphere in the direction of the kite's nose. */
	FVector KiteHeading;

	float AirspeedCmS;
	float TurnDeg;
	float CentredBarSeconds;
	bool bPlacementPending;
	bool bLoopHeld;

	FVector KiteWorldPosition;
	FRotator KiteWorldRotation;
	FVector KiteVelocity;

	float LineTensionN;
	FVector LineForce;
};
