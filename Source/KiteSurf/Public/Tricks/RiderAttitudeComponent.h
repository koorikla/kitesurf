#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Tricks/RiderAxes.h"
#include "Tricks/BarState.h"
#include "RiderAttitudeComponent.generated.h"

/**
 * Where the lines pull on the rider's body, for LineAttach (docs/tricks/T3.md 1.1). Body frame: X
 * Front, Y Right, Z Up (Tricks/RiderAxes.h), in cm relative to the centre of mass, which is 97 cm
 * above the deck and 10 cm above the rig's pelvis. Every value is an estimate.
 */
USTRUCT(BlueprintType)
struct KITESURF_API FLineAttachTunables
{
	GENERATED_BODY()

	/** Hooked in: the harness hook. The same point as URiderAttitudeComponent::HookOffsetFromComCm by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks|Line attach")
	FVector HookBodyCm = FVector(12.0f, 0.0f, 20.0f);

	/** Unhooked with the bar pulled to the hips (arm extension 0): the hands at the hips. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks|Line attach")
	FVector HipHandsBodyCm = FVector(25.0f, 0.0f, -5.0f);

	/** A shoulder (Y is the right one; the left is mirrored). RiderRig::ShoulderOffsetCm less the 10 cm from the pelvis up to the centre of mass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks|Line attach")
	FVector ShoulderBodyCm = FVector(4.0f, 23.0f, 42.0f);

	/** How far the hands reach from the shoulders with the arms out (cm): 0.97 of RiderRig's arm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks|Line attach", meta = (ClampMin = "0.0"))
	float ArmReachCm = 52.0f;

	/** Half angle of the cone the arms can point the bar in (deg), about Front tilted ArmConeUpTilt up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks|Line attach", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float ArmConeDeg = 100.0f;

	/** The arm cone's axis is normalize(Front + this x Up): 0.36 is 20 deg up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks|Line attach")
	float ArmConeUpTilt = 0.36f;

	/** Blind: the bar held at the lower back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks|Line attach")
	FVector BehindBackBodyCm = FVector(-18.0f, 0.0f, -2.0f);

	/** The pass arc runs from the giving hip, (HipX, +-HipY, Z), round the back through (BackX, 0, Z), to the other hip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks|Line attach")
	float PassHipXCm = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks|Line attach")
	float PassHipYCm = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks|Line attach")
	float PassBackXCm = -22.0f;

	/** Height of the pass arc relative to the centre of mass (cm): the pelvis, 10 cm below it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tricks|Line attach")
	float PassZCm = -10.0f;
};

/**
 * Where the lines pull on the body for each bar state (docs/tricks/T3.md 1.1). Pure functions, used
 * both by the physics (the line torque about the centre of mass, AKiteRiderPawn::StepRiderAttitude)
 * and by the drawn bar (AKiteRiderPawn::UpdateRiderPose), so the picture matches the physics.
 */
namespace LineAttach
{
	/**
	 * The attach point in the body frame (cm from the centre of mass). Hooked or lost: the harness
	 * hook (a lost bar pulls through the leash at the harness). Unhooked with the bar in front: from
	 * the hands at the hips (ArmExtension 0) to the hands at arm's reach along the line, clamped to the
	 * arm cone, from between the shoulders (both hands) or that hand's shoulder (one hand; the front
	 * hand is on the nose's side, NoseSideSign x Right). Behind the back: the lower back. Passing: on
	 * the pass arc at FBarState::PassT, from the hip on the giving side round the back.
	 */
	KITESURF_API FVector AttachPointBody(const FBarState& Bar, const FVector& LineDirBody, float ArmExtension, float NoseSideSign, const FLineAttachTunables& Tunables);

	/** Dir (unit) if it is within HalfAngleDeg of ConeAxis, otherwise the cone's edge on the way to it. */
	KITESURF_API FVector ClampToArmCone(const FVector& DirBody, const FVector& ConeAxisBody, float HalfAngleDeg);

	/** The pass arc at progress P (0..1), body frame: Side +1 starts on the right hip. */
	KITESURF_API FVector PassArcBody(float P, float Side, const FLineAttachTunables& Tunables);

	/** The giving side of a pass (+1 right): the front hand gives for a backside pass. */
	KITESURF_API float PassGivingSide(const FBarState& Bar, float NoseSideSign);
}

/**
 * What one fixed step of the rider's attitude reads. The pawn fills it at the start of each step,
 * after the kite step and before the board step (Kite->StepKite, line force, Attitude->Step,
 * Board->SetAirAttitude, Board->StepBoard). Positions and forces in Unreal units, as the pawn has
 * them; the component converts to SI once.
 */
USTRUCT()
struct KITESURF_API FAttitudeInputs
{
	GENERATED_BODY()

	/** The board is in the air at the start of the step. False: the attitude copies the slaved poses below. */
	bool bAirborne = false;

	/** The board is strapped to the feet. Read from T2 (board-off); the board follows the body for now. */
	bool bStrapped = true;

	/** The kinematic body pose on the water (world). Read on the water and on the take-off step. */
	FQuat SlavedBodyQuat = FQuat::Identity;

	/** The board's world orientation on the water. Read on the water and on the take-off step. */
	FQuat SlavedBoardQuat = FQuat::Identity;

	/** Board (point mass) velocity (cm/s, world). */
	FVector VelocityCmS = FVector::ZeroVector;

	/** This step's line force on the rider (kg*cm/s^2, world): Kite->GetLineForce(). */
	FVector LineForceUU = FVector::ZeroVector;

	/** The lines are taut. Slack (storm bursts, a dropped kite), the line torque is off whatever LineForceUU says. */
	bool bLinesTaut = true;

	/** Height of the board above the water below it (cm), for the time to contact. */
	float HeightAboveWaterCm = 0.0f;

	/** Measured vertical acceleration (cm/s^2), filtered by the pawn over about 0.1 s; the kite makes the fall far from ballistic. */
	float VerticalAccelCmS2 = 0.0f;

	/** Water surface normal under the rider; the landing assist turns Up towards it. */
	FVector WaterNormal = FVector::UpVector;

	/** Air rotation stick after the pawn's side mapping. X: +1 back roll, -1 front roll. Y: +1 up, -1 down (pulled, backflip). */
	FVector2D RotationStick = FVector2D::ZeroVector;

	/** The rotation stick is past its deadzone. While set, the control torque acts and the landing assist does not. */
	bool bRotationInput = false;

	/** Target tuck, 0 (stretched) to 1 (tucked). */
	float Tuck = 0.0f;

	/** Pre-wind stick direction captured by the pawn during the load; consumed on the take-off edge. Same axes as RotationStick. */
	FVector2D PreWindStick = FVector2D::ZeroVector;

	/** How far the pre-wind was built, 0..1. */
	float PreWindAmount = 0.0f;

	/** The board's load (0..1) at the pop, recorded before Jump() zeroes it. */
	float TakeoffLoad = 0.0f;

	/** How hard the edge was held at the pop (0..1). Carried for later tuning; the pre-wind rate does not use it yet. */
	float TakeoffEdgeHold = 0.0f;

	/** Sigma, the travel side latched at load start or take-off (RiderAxes::TravelSide): +1 or -1. */
	float TravelSideSigma = 1.0f;

	/**
	 * Unhooked (T3.1): the lines pull at LineAttachBodyCm, the hands, with HandsLineTorqueScale,
	 * instead of at the hook with LineTorqueScale. False while hooked in and with the bar lost.
	 */
	bool bUseLineAttach = false;

	/** Where the lines pull (cm, body frame, from the centre of mass): LineAttach::AttachPointBody. Read with bUseLineAttach. */
	FVector LineAttachBodyCm = FVector::ZeroVector;
};

/** What the last step did, for debug drawing, telemetry and the landing evaluator. Torques in N*m, world. */
struct FAttitudeDebug
{
	FVector LineTorqueNm = FVector::ZeroVector;
	FVector ControlTorqueNm = FVector::ZeroVector;
	FVector AssistTorqueNm = FVector::ZeroVector;
	/** The travel-align torque about world up (N*m): the rider keeping the board pointed along the flight when no trick is going on. */
	FVector TravelAlignTorqueNm = FVector::ZeroVector;
	/** Posture damping's torque (its angular impulse over the step). */
	FVector PostureTorqueNm = FVector::ZeroVector;
	/** The committed rotation axis (world, unit), zero when none. */
	FVector CommittedAxisWorld = FVector::ZeroVector;
	/** Predicted time to touchdown (s); a large number when the rider is not falling towards the water. */
	float TimeToContactSeconds = TNumericLimits<float>::Max();
	/** Angle to the nearest valid landing attitude (deg): upright, board along the travel either way round. */
	float LandingErrorDeg = 0.0f;
	/** dot(error axis, omega-hat): > 0 under-rotated, < 0 over-rotated, 0 when not rotating. */
	float ErrorAlongSpin = 0.0f;
	bool bAssistActive = false;
	RiderAxes::ERotationFamily Family = RiderAxes::ERotationFamily::None;
};

/**
 * The rider's rotation in the air (T1.2): body orientation and angular momentum on top of the
 * board's point-mass trajectory. Angular momentum is the state, so it is conserved with no torque
 * and a tuck spins the rider faster with no extra rule (omega = I^-1 L).
 *
 * One fixed step: torques at the start of the step (line torque at the hook, capped air control
 * towards a target rate, or the landing-assist PD inside its window), then posture damping and
 * drag as exact exponential decays, then the free rigid-body rotation for the step (a split that
 * is exact for a symmetric top and conserves |L| and, to rounding, the energy).
 *
 * On the water the attitude is slaved to the kinematic pose. The pawn steps it (it does not tick);
 * Step is pure and touches no world, so tests drive a bare NewObject. AKiteRiderPawn::StepSimulation
 * steps it after the line force and before the board, and hands the board GetBoardQuat in the air.
 *
 * SI inside (kg*m^2, N*m, rad/s); Unreal units (cm, kg*cm/s^2) only at the boundary.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class KITESURF_API URiderAttitudeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URiderAttitudeComponent();

	/** One fixed step of Dt seconds. Pure: reads only the inputs and this component's state. */
	void Step(float Dt, const FAttitudeInputs& In);

	/** Back to the slaved pose with no angular momentum, inactive (board reset, crash, teleport). */
	void Reset(const FQuat& Body, const FQuat& Board);

	/**
	 * Puts the rider in the air with this body orientation and angular momentum (kg*m^2/s, world),
	 * for tests and debug. The next airborne Step does not run the take-off (no pre-wind). The
	 * rotation that L gives becomes the committed axis, so posture damping leaves it alone.
	 */
	void SetState(const FQuat& Body, const FVector& AngularMomentumKgM2S);

	/**
	 * True while the attitude is simulated (in the air); false while slaved to the water pose.
	 * (The T1 plan calls it IsActive, which UActorComponent already owns for activation.)
	 */
	UFUNCTION(BlueprintPure, Category = "Rider|Rotation")
	bool IsSimulating() const { return bActive; }

	FQuat GetBodyQuat() const { return Q; }
	FQuat GetRenderBodyQuat(float Alpha) const { return FQuat::Slerp(PrevQ, Q, Alpha); }

	/** The strapped board's world orientation: body times the strap offset (T1.2.5). */
	FQuat GetBoardQuat() const { return Q * StrapOffset; }
	FQuat GetRenderBoardQuat(float Alpha) const { return FQuat::Slerp(PrevQ * PrevStrapOffset, Q * StrapOffset, Alpha); }

	/** The board in body coordinates: settles from the take-off heel and pitch to the canonical offset. */
	FQuat GetStrapOffset() const { return StrapOffset; }

	/**
	 * The board flat under the feet with its nose along NoseSideSign * Right, in body coordinates.
	 * The nose is on the -StanceSide side, so NoseSideSign = -StanceSide.
	 */
	static FQuat MakeCanonicalStrapOffset(float NoseSideSign);

	/** Angular velocity (rad/s, world). */
	UFUNCTION(BlueprintPure, Category = "Rider|Rotation")
	FVector GetAngularVelocity() const { return OmegaW; }

	/** Angular momentum about the centre of mass (kg*m^2/s, world). */
	FVector GetAngularMomentum() const { return L; }

	/** Principal inertia in the body frame (Front, Right, Up) for the current tuck (kg*m^2). */
	FVector GetBodyInertiaKgM2() const;

	/** The current tuck, 0..1, after its smoothing spring. */
	float GetTuckAmount() const { return TuckNow; }

	/** The committed rotation axis in body coordinates (unit), zero when none. */
	FVector GetCommittedAxisBody() const { return CommittedAxisBody; }

	const FAttitudeDebug& GetLastStepDebug() const { return LastDebug; }

	/**
	 * Offset from the physics root (board centre) to the centre of mass (cm, world), between the last
	 * two steps (T1.2.6). The visual board sits at Root + this - Body.Rotate((0, 0, ComAboveBoardCm)),
	 * which is the root at take-off and when upright on landing.
	 */
	FVector GetVisualComOffsetCm(float Alpha) const { return FMath::Lerp(PrevComOffsetWorldCm, ComOffsetWorldCm, Alpha); }

	/**
	 * The landing assist torque (N*m, world) for these inputs at the current state, without
	 * stepping. Zero outside its window or with rotation input. Also fills the time to contact,
	 * landing error, ErrorAlongSpin, bAssistActive and AssistTorqueNm of OutDebug.
	 */
	FVector ComputeAssistTorque(const FAttitudeInputs& In, FAttitudeDebug& OutDebug) const;

	/**
	 * The travel-align torque (N*m, world, about world up) for these inputs at the current state: zero
	 * with rotation input, with a rotation committed, or when the board or the flight has no heading.
	 */
	FVector ComputeTravelAlignTorque(const FAttitudeInputs& In) const;

	/** Time to contact (s): the smallest positive root of h + v t + a t^2 / 2 = 0, else h / max(-v, 1 cm/s). */
	static float ComputeTimeToContact(float HeightCm, float VerticalSpeedCmS, float VerticalAccelCmS2);

	/** The nearest valid landing attitude for this velocity and water normal: Up along the normal, the board along the travel either way round. */
	FQuat ComputeLandingTarget(const FVector& VelocityCmS, const FVector& WaterNormal) const;

	// Tuning. Every default is an estimate (docs/tricks.md section 8, T1 plan T1.2.2).

	/** Principal inertia stretched out, rider plus strapped board (kg*m^2): Front, Right, Up. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation")
	FVector InertiaStretchedKgM2;

	/** Principal inertia fully tucked (kg*m^2): Front, Right, Up. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation")
	FVector InertiaTuckedKgM2;

	/** Smoothing time of the critically damped tuck spring (s): about 60% of the way after this, all but done after three times it. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.01"))
	float TuckSmoothSeconds;

	/** The harness hook relative to the centre of mass (cm, body: Front, Right, Up). Its hang lean is atan(X / Z), 31 deg. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation")
	FVector HookOffsetFromComCm;

	/** Height of the centre of mass above the board centre (cm), the visual pivot. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float ComAboveBoardCm;

	/** Scale on the line torque r x F at the hook. Calibrated so a full pre-wind back roll at 800 N hang tension takes 1.5 to 2.5 s (KiteSurf.Trick.BackRollFromPreWind). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float LineTorqueScale;

	/**
	 * Scale on the line torque r x F with the lines pulling at the hands (unhooked, FAttitudeInputs::
	 * bUseLineAttach), separate from the hooked LineTorqueScale. docs/tricks/T3.md 1.1 has 1.0, at
	 * which a raley falls out of the physics; T3.2 tunes it. Estimate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float HandsLineTorqueScale;

	/** Roll rate from a full pre-wind at full load (deg/s). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float PreWindRollRateDegS;

	/** Flip rate from a full pre-wind at full load (deg/s). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float PreWindFlipRateDegS;

	/** Spin rate from a full pre-wind at full load (deg/s), for an axis tilted less than SpinAxisTiltMaxDeg. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float PreWindSpinRateDegS;

	/** A roll axis tilted less than this from Up counts as a spin for its rate (deg). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float SpinAxisTiltMaxDeg;

	/** Default roll axis tilt from Up towards Front (deg). At least about 54 so a default roll counts as an inversion. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float DefaultRollAxisTiltDeg;

	/**
	 * The air stick's axis tilt before stick Y on a jump that left the water with no rotation (no
	 * pre-wind) (deg). 0: stick X alone spins the rider flat about Up, a backside spin towards the back
	 * roll's side (T1.4: the A/D air spin the board had before the attitude); stick Y down tilts it
	 * towards a roll by up to RollAxisTiltRangeDeg. A jump that took off rotating (a pre-wind, or
	 * SetState with angular momentum) uses DefaultRollAxisTiltDeg, so stick X drives the roll it is
	 * already in instead of turning it into a spin. Set it to DefaultRollAxisTiltDeg for the plan's
	 * "X alone is a roll" everywhere. Estimate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float AirStickTiltWithoutPreWindDeg;

	/** True when this jump left the water rotating (a pre-wind) or SetState gave it a rotation: the air stick then uses DefaultRollAxisTiltDeg. */
	bool TookOffRotating() const { return bTookOffRotating; }

	/** How far stick Y moves the roll axis tilt either way (deg): down towards inverted, up towards a flat spin. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float RollAxisTiltRangeDeg;

	/** A stick within this of vertical is a flip (deg). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float FlipSectorDeg;

	/** Extra angle the stick must move to leave the flip sector once in it (deg). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float FlipSectorHysteresisDeg;

	/** Pre-wind rate scale with no load; full load gives 1: Floor + (1 - Floor) * load. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PreWindLoadFloor;

	/** Air control torque cap as a fraction of a full pre-wind per second (1/s): cap = this * I_axis * full rate. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float AirControlFractionPerS;

	/** Time constant of air control towards its target rate (s), before the cap. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.01"))
	float AirControlResponseSeconds;

	/** Decay rate of rotation off the committed axis, or of all rotation when none is committed (1/s). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float PostureDampingPerS;

	/** Cap on the posture damping torque (N*m). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float PostureMaxTorqueNm;

	/** Air drag on all rotation (1/s). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float AirAngularDragPerS;

	/** Landing assist strength, 0 (off) to 1; the settings set it later. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AssistStrength;

	/** The landing assist acts when the time to contact is under this (s). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float AssistWindowSeconds;

	/** The landing assist acts only within this of a valid landing attitude (deg). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float AssistMaxErrorDeg;

	/** Natural frequency of the landing assist PD (Hz). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float AssistNaturalFreqHz;

	/** Damping ratio of the landing assist PD. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float AssistDampingRatio;

	/** Cap on the landing assist torque (N*m). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float AssistMaxTorqueNm;

	/**
	 * Natural frequency of the travel align (Hz); 0 turns it off. With no rotation input, no rotation
	 * committed (no pre-wind, no stick held this jump) and the landing assist not acting, a rider keeps
	 * the board pointed along the flight, either end first, as the kinematic auto-align did before the
	 * attitude: a PD torque about world up, so the kite dragging the flight round does not leave the
	 * board crossways for the landing. It comes in over StrapSettleSeconds after the take-off. Estimate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float TravelAlignNaturalFreqHz;

	/** Damping ratio of the travel align. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float TravelAlignDampingRatio;

	/** Cap on the travel align torque (N*m). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float TravelAlignMaxTorqueNm;

	/** Slower than this along the water (cm/s) the flight has no direction to align with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.0"))
	float TravelAlignMinSpeedCmS;

	/** Time constant of the board easing from its take-off heel and pitch to flat under the feet (s). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.01"))
	float StrapSettleSeconds;

	/** Time constant of the visual centre-of-mass offset settling to straight above the board (s). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Rotation", meta = (ClampMin = "0.01"))
	float ComOffsetSettleSeconds;

private:
	/** Take-off edge: strap offset, centre-of-mass offset and the pre-wind (T1.2.4). */
	void BeginAir(const FAttitudeInputs& In);

	/** The stick mapping with this component's tunables, from DefaultTiltDeg before stick Y. */
	RiderAxes::FRotationAxisChoice ChooseAxis(const FVector2D& Stick, float Sigma, bool bWasFlip, float DefaultTiltDeg) const;

	/** Full rate of a rotation family (rad/s). */
	float FullRateRadS(RiderAxes::ERotationFamily Family) const;

	/** Inertia about a world axis for body inertia Ib at the current orientation (kg*m^2). */
	double InertiaAbout(const FVector& AxisWorld, const FVector& Ib) const;

	/** Angular velocity (rad/s, world) from L at the current orientation for body inertia Ib. */
	FVector OmegaFrom(const FVector& Ib) const;

	/** The free rigid-body rotation for Dt with L fixed (exact for a symmetric top). */
	void RotateFree(float Dt, const FVector& Ib);

	FQuat Q = FQuat::Identity;
	FQuat PrevQ = FQuat::Identity;
	FQuat StrapOffset = FQuat::Identity;
	FQuat PrevStrapOffset = FQuat::Identity;
	FQuat CanonicalStrapOffset = FQuat::Identity;
	FVector L = FVector::ZeroVector;
	FVector OmegaW = FVector::ZeroVector;
	FVector CommittedAxisBody = FVector::ZeroVector;
	float TuckNow = 0.0f;
	float TuckVel = 0.0f;
	/** Time since the take-off (s); SetState leaves it where it is. */
	float AirSeconds = 0.0f;
	bool bActive = false;
	bool bWasAirborne = false;
	bool bControlWasFlip = false;
	bool bTookOffRotating = false;
	FVector ComOffsetWorldCm = FVector::ZeroVector;
	FVector PrevComOffsetWorldCm = FVector::ZeroVector;
	FAttitudeDebug LastDebug;
};
