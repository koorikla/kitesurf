#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "KiteGear.h"
#include "KiteComponent.generated.h"

class UWindComponent;
class UStaticMeshComponent;
class UCableComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKiteCrashed, FVector, WaterLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnKiteRelaunched);

/**
 * The kite: a point mass on the end of its lines.
 *
 * It has a position, a velocity and a heading (where its nose points). Each step it feels
 * - lift and drag from the air flowing over it (the true wind minus its own velocity), set by
 *   its angle of attack: the angle the flow makes with the canopy, plus the trim from the bar;
 * - a side force that resists sliding sideways, which is what makes it fly where it points;
 * - gravity; and
 * - the pull of the lines, which only ever pull: they hold the kite at line length while the
 *   forces on it point away from the rider, and go slack when they do not.
 *
 * Line tension is whatever the lines need to hold the kite on its arc, so it rises with the
 * square of the kite's airspeed and falls to nothing in a lull. With slack lines nothing holds
 * the canopy to the wind: the kite stops flying, drifts and falls like a sheet until the lines
 * come tight again or it reaches the water. Too much angle of attack stalls it; air on the wrong
 * side of the canopy (overflying the window, or outrunning the wind) luffs it.
 *
 * Steering turns the nose at a rate proportional to airspeed. Normally an assist flies it: bar
 * over means travel round the window that way, bar centred means hold this position at the
 * window edge. With the loop input held the bar turns the kite directly, so holding it flies a loop.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class KITESURF_API UKiteComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKiteComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Advances the kite by DeltaTime in sub-steps no longer than MaxStepSeconds. For tests and for a kite nobody else steps. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void UpdateKite(float DeltaTime);

	/** One fixed step of the kite, with the rider where they are now. The pawn calls this inside its own step loop. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void StepKite(float StepSeconds);

	/** Time the kite's simulation has advanced (s); the wind is sampled at this time. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetSimTimeSeconds() const { return SimTimeSeconds; }

	/** Places the mesh and lines. Alpha blends from the previous step's position to the current one, for rendering between steps. */
	void UpdateVisuals(float Alpha = 1.0f);

	// Inputs
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SteerKite(float Axis /* -1..1 */);

	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SheetKite(float Amount /* 0..1 */);

	/**
	 * Forces the bar straight through to the kite wherever it is in the window. Not bound to a
	 * key: the rider loops the kite by holding the bar towards the kite's own side. This is for
	 * scripted input and tests that need raw steering from any position.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetLoopHeld(bool bHeld);

	UFUNCTION(BlueprintCallable, Category = "Kite")
	bool IsLoopHeld() const { return bLoopHeld; }

	/** True while the rider's bar is going straight to the kite and turning it round: a loop in progress. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	bool IsLooping() const { return bLooping; }

	/** How far round the window on its own side (deg of clock) the kite must be for the bar held that way to loop it rather than fly it there. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Steering", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float LoopClockDeg;

	/** The kite sizes on offer (m^2), smallest first. */
	static TConstArrayView<float> GetKiteSizesM2();

	/** The size from GetKiteSizesM2 a rider of this weight would rig for this wind: about 2.2 x kg / knots. */
	UFUNCTION(BlueprintPure, Category = "Kite")
	static float RecommendKiteSizeM2(float WindKnots, float RiderMassKg = 85.0f);

	/**
	 * Rigs a kite of this area. Its mass, the air it has to push and its turning circle follow the
	 * size: a small kite is light and turns tightly, a big one is slow and powerful.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetKiteSize(float InAreaM2);

	/** Rigs this kind of kite at the current size: its lift, glide, weight and turning follow the model. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetKiteModel(EKiteModel InModel);

	UFUNCTION(BlueprintPure, Category = "Kite")
	EKiteModel GetKiteModel() const { return KiteModel; }

	/** Linear size of this kite against the 12 m^2 the mesh and the tuning are built for. */
	UFUNCTION(BlueprintPure, Category = "Kite")
	float GetSizeScale() const { return FMath::Sqrt(FMath::Max(AreaM2, 1.0f) / ReferenceAreaM2); }

	static constexpr float ReferenceAreaM2 = 12.0f;

	/**
	 * The steering actually reaching the kite, -1..1: the rider's bar while looping, otherwise what
	 * the assist asks for to carry out the bar's intent. 0 while the kite is in the water.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetAppliedSteer() const { return AppliedSteer; }

	/** True while the kite is lying on the water. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	bool IsCrashed() const { return bCrashed; }

	/** Seconds until a crashed kite relaunches by itself; 0 when flying. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetRelaunchSecondsRemaining() const { return bCrashed ? FMath::Max(RelaunchDelaySeconds - CrashedSeconds, 0.0f) : 0.0f; }

	UPROPERTY(BlueprintAssignable, Category = "Kite|Events")
	FOnKiteCrashed OnKiteCrashed;

	UPROPERTY(BlueprintAssignable, Category = "Kite|Events")
	FOnKiteRelaunched OnKiteRelaunched;

	// Outputs
	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetLineForce() const; // Force on rider in kg*cm/s^2 (1 N = 100 kg*cm/s^2)

	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetLineTensionN() const; // Line tension in Newtons

	UFUNCTION(BlueprintCallable, Category = "Kite")
	FVector GetKiteWorldPosition() const;

	/** Angle of the kite to the right of straight downwind (true wind), -180..180. A fast rider's kite can sit past 90. */
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

	/** Speed of the air over the kite (cm/s). */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetAirspeedCmS() const { return AirspeedCmS; }

	/** Angle of the airflow to the canopy, including bar trim (deg). Above StallAngleDeg the kite is stalled; below zero it is luffing. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	float GetAngleOfAttackDeg() const { return AngleOfAttackDeg; }

	/** True while the lines are tight. With slack lines the kite is not flying and the rider feels nothing. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	bool AreLinesTaut() const { return bLinesTaut; }

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
	 * Place the kite by clock position and depth in the window the rider feels (see GetWindowAxis).
	 * ClockDeg: angle from the zenith around the window axis, positive to the right looking down it (90 = right horizon).
	 * DepthDeg: angle from the window edge towards the middle of the window.
	 * The kite then settles to the depth its lift and drag give it.
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

	/** The rider holds the bar: they say where its ends are, and the lines run from there to the kite. */
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SetBarEnds(const FVector& LeftEnd, const FVector& RightEnd);

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

	/** Mass of air the kite has to push around as it accelerates (kg), added to MassKg for inertia. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float AddedMassKg;

	/** Lift coefficient at the stall, the most the canopy can make. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float MaxLiftCoefficient;

	/** Angle of attack at which the kite stalls (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float StallAngleDeg;

	/** How far the force on a stalled canopy leans towards its nose (deg). More recovers from a stall faster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float StalledForwardTiltDeg;

	/** Drag coefficient at zero lift: canopy, tubes and lines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float ParasiteDragCoefficient;

	/** Drag due to lift: Cd rises by this times Cl squared. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float InducedDragFactor;

	/** Extra drag coefficient with the bar hard over: a kite bent into a turn is draggy, which keeps loop speed down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float SteeringDragCoefficient;

	/** Resistance to sliding sideways through the air; makes the kite fly where it points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float SideForceCoefficient;

	/** Drag coefficient of a kite that is not flying (slack lines), on its full area. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float SlackDragCoefficient;

	/** Slack in the lines (cm) at which the canopy has lost its shape completely and only drags. With less it still flies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics", meta = (ClampMin = "1.0"))
	float SlackCollapseCm;

	/** Canopy trim angle with the bar out (deg): depowered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float TrimSheetedOutDeg;

	/** Canopy trim angle with the bar in (deg): powered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Aerodynamics")
	float TrimSheetedInDeg;

	/** Radius of the tightest turn, at full steering (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float MinTurnRadiusCm;

	/** How quickly the kite winds into a turn (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float TurnResponse;

	/** How strongly the nose swings round to face the air it is flying through (rad/s per m/s of sideways flow). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float WeathercockGain;

	/** How strongly the nose drops towards the water when the kite is slow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float GravityTurnGain;

	/** Heading the assist asks for at full steering, measured from nose-out: just past along-the-edge, so the kite also dips into the window and gains power as it travels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float TravelHeadingDeg;

	/** Bar the assist applies per radian of heading error. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float SteerAssistGain;

	/** With the bar centred the assist holds the kite's clock position: degrees of heading correction per degree of drift... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float ParkHoldGain;

	/** ...up to this much (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float ParkHoldMaxDeg;

	/** The assist will not fly the kite lower than this; a loop, a stall or slack lines can take it lower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float MinElevationDeg;

	/** The kite has hit the water when it gets this close to it (cm above world Z = 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float CrashHeightCm;

	/** A kite on the water relaunches by itself after this long, or sooner if steered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float RelaunchDelaySeconds;

	/** Wind needed to get a kite off the water (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float MinRelaunchWindCmS;

	/** Cap on the pull passed to the rider, standing in for line stretch and the rider giving way (N). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Tuning")
	float MaxLineTensionN;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Debug")
	bool bDrawDebug;

	/** The flight is stepped in sub-steps no longer than this (s), whatever UpdateKite is given. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Simulation", meta = (ClampMin = "0.0001"))
	float MaxStepSeconds;

	/** Most sub-steps one UpdateKite call will take; past that the sub-step grows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite|Simulation", meta = (ClampMin = "1"))
	int32 MaxStepsPerUpdate;

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

	FVector GetWindAt(const FVector& Location) const;
	FVector GetRiderPosition() const;
	FVector GetRiderVelocity() const;

	/** Direction from the rider for an azimuth and elevation measured from the true wind. */
	FVector DirectionFromAngles(float InAzimuthDeg, float InElevationDeg) const;

	/** Rebuilds the flight state from AzimuthDeg / ElevationDeg: at line length there, moving with the rider, nose-out. */
	void PlaceParked();

	/** Updates KiteDir, AzimuthDeg and ElevationDeg from the kite's position. */
	void UpdateAngles();

	/** One fixed step of the flight dynamics; returns the line tension during the step (N). */
	float StepFlight(float StepSeconds, float SteerInput, const FVector& RiderPos, const FVector& RiderVelocity, const FVector& Wind);

	/** Lift and drag coefficients for an angle of attack in degrees, valid at any angle. */
	void GetAeroCoefficients(float AlphaDeg, float& OutLift, float& OutDrag) const;

	/** Bar position the kite is flown with: the player's in loop mode, the assist's otherwise. */
	float ComputeSteering(float DeltaTime, const FVector& RiderVelocity, const FVector& Wind);

	void Crash();
	void Relaunch();

	/** Unit vector from the rider to the kite. */
	FVector KiteDir;

	/** Unit vector in the direction of the kite's nose, at right angles to KiteDir. */
	FVector KiteHeading;

	/** World velocity of the kite (cm/s). */
	FVector KiteVelocity;

	FVector KiteWorldPosition;
	FRotator KiteWorldRotation;

	/** Where the kite was before the last step, for drawing it between steps. */
	FVector PrevKiteWorldPosition;

	/** Time the kite's simulation has advanced (s). */
	float SimTimeSeconds;

	float AirspeedCmS;
	float AngleOfAttackDeg;
	float TurnDeg;
	float CentredBarSeconds;
	float ParkClockDeg;
	float TurnRateRadS;
	float AppliedSteer = 0.0f;
	EKiteModel KiteModel = EKiteModel::Loop;
	FVector BarLeftEnd = FVector::ZeroVector;
	FVector BarRightEnd = FVector::ZeroVector;
	bool bHasBarEnds = false;
	bool bHasParkClock;
	bool bPlacementPending;
	bool bLoopHeld;
	bool bLooping;
	float LoopSide;
	bool bCrashed;
	bool bLinesTaut;
	float CrashedSeconds;

	float LineTensionN;
	FVector LineForce;
};
