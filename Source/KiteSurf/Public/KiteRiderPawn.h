#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "RiderCharacter.h"
#include "KiteMotionBar.h"
#include "RiderRig.h"
#include "Tricks/GrabState.h"
#include "Tricks/BarState.h"
#include "Tricks/RiderAttitudeComponent.h"
#include "Tricks/LandingEvaluator.h"
#include "KiteRiderPawn.generated.h"

class UStaticMeshComponent;
class USkeletalMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UWindComponent;
class UBoardMovementComponent;
class UBoardWakeComponent;
class UWindStreakComponent;
class UKiteComponent;
class UTrickTrackerComponent;
class URiderAttitudeComponent;
class UInputMappingContext;
class UInputAction;
class UAudioComponent;
class USoundBase;

/** What the rider is doing, as far as it can be heard. */
struct FRideAudioState
{
	/** Wind past the rider's ears: true wind less their own motion (knots). */
	float ApparentWindKnots = 0.0f;
	float BoardSpeedKnots = 0.0f;
	bool bOnWater = true;
	float LineTensionN = 0.0f;
	/** Air over the kite (m/s): about the wind when it is parked, several times that through a loop. */
	float KiteAirspeedMS = 0.0f;
	/** The canopy has no load in it: slack lines, a stall, or the bar right out. 0..1. */
	float KiteLuff = 0.0f;
	/** How hard the edge is driven: carving or a loaded crouch. 0..1. */
	float EdgeEffort = 0.0f;
	bool bAirborne = false;
};

/** Volume and pitch for each sound loop, and how much of the music's second layer to play. */
struct FRideAudioMix
{
	float WindVolume = 0.0f;
	float WindPitch = 1.0f;
	float WaterVolume = 0.0f;
	float WaterPitch = 1.0f;
	float LineVolume = 0.0f;
	float LinePitch = 1.0f;
	/** Spray off a hard edge. */
	float SprayVolume = 0.0f;
	/** The kite moving through the air. */
	float KiteVolume = 0.0f;
	float KitePitch = 1.0f;
	/** A luffing canopy flapping. */
	float FlutterVolume = 0.0f;
	/** The music's in-the-air layer, 0..1 of the music volume. */
	float AirMusic = 0.0f;
};

/** Something that happened to the rider that has its own sound. */
enum class ERideSound : uint8
{
	KiteCrash,
	Relaunch,
	Aground,
	Shark
};

UCLASS()
class KITESURF_API AKiteRiderPawn : public APawn
{
	GENERATED_BODY()

public:
	AKiteRiderPawn();

	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/**
	 * One fixed step of the whole rig, in this order: the kite with the rider where they are, the
	 * line force to the board, the rider's attitude (URiderAttitudeComponent, which hands the board
	 * its air orientation), the board, then the trick tracker. Tick runs as many of these as the
	 * frame holds.
	 */
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void StepSimulation(float StepSeconds);

	/** Time the simulation has advanced (s). */
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	float GetSimTimeSeconds() const { return SimTimeSeconds; }

	/** How many fixed steps the last frame ran. */
	int32 GetLastFrameSimSteps() const { return LastFrameSimSteps; }

	/** The physics debug level this pawn draws at: the kite.Physics.Debug console variable, or at least 1 when the kite's bDrawDebug is set. 0 in Shipping. */
	int32 GetPhysicsDebugLevel() const;

	/** The fixed step the kite, lines and board are simulated with (s). The same ride at any frame rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "0.0001"))
	float SimStepSeconds;

	/** Frame time above this is clamped (s), so a hitch slows the simulation down instead of blowing it up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "0.001"))
	float MaxFrameSeconds;

	/** Most fixed steps one frame will run; the rest of that frame's time is dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "1"))
	int32 MaxSimStepsPerFrame;

	/** Draw the rider and kite between the last two simulation states, so motion is smooth however the frame rate divides the step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	bool bInterpolateRendering;

	/** Step the kite and board from Tick. Off, they stay where they are and Tick only poses the rider, camera and sound: for tests that place the rider by hand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	bool bStepSimulation;

	// Public API
	UFUNCTION(BlueprintCallable, Category = "Kite")
	void SteerKite(float Axis /* -1..1 */);

	/**
	 * Puts the bar at this position, 0 (out) to 1 (in), and holds it there: the scripted path (tests,
	 * the ride's start, kitesurf.Input). It takes the bar back from the player's spring-loaded sheet
	 * input until the player next moves the bar (see bBarReturnsToMiddle).
	 */
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
	UInputAction* GetWeightShiftAction() const { return WeightShiftAction.Get(); }
	UInputAction* GetJumpAction() const { return JumpAction.Get(); }
	UInputAction* GetPauseAction() const { return PauseAction.Get(); }
	UInputAction* GetRecenterMotionAction() const { return RecenterMotionAction.Get(); }
	UInputAction* GetGrabFrontAction() const { return GrabFrontAction.Get(); }
	UInputAction* GetGrabBackAction() const { return GrabBackAction.Get(); }
	UInputAction* GetOneFootAction() const { return OneFootAction.Get(); }
	UInputAction* GetHookAction() const { return HookAction.Get(); }
	UInputAction* GetPassAction() const { return PassAction.Get(); }
	UInputAction* GetRotateAction() const { return RotateAction.Get(); }

	UKiteComponent* GetKite() const { return Kite.Get(); }
	UBoardMovementComponent* GetBoardMovement() const { return BoardMovement.Get(); }
	UBoardWakeComponent* GetWake() const { return Wake.Get(); }
	UWindStreakComponent* GetWindStreaks() const { return WindStreaks.Get(); }
	UWindComponent* GetWind() const { return Wind.Get(); }
	/** Names, grades and scores the rider's jumps; stepped after the board in StepSimulation. */
	UTrickTrackerComponent* GetTrickTracker() const { return TrickTracker.Get(); }
	/** The rider's rotation in the air (T1.2): stepped before the board in StepSimulation, slaved to the riding pose on the water. */
	URiderAttitudeComponent* GetRiderAttitude() const { return RiderAttitude.Get(); }

	/**
	 * Steps the rider attitude and gives the board its air orientation from it. Off, the attitude is
	 * not stepped and the board keeps its old kinematic air orientation (AirSpinRate, auto-align,
	 * weight-shift pitch), as with no attitude component.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Rotation")
	bool bUseRiderAttitude;

	/**
	 * The pre-wind stick, as the rider holds it while loading: X +1 towards a back roll, -1 a front
	 * roll; Y +1 up, -1 down (pulled, a backflip). While the load is held on the water and the stick
	 * is past PreWindStickThreshold, the pre-wind builds to full over PreWindBuildSeconds; the take-off
	 * turns it into the rotation the rider leaves the water with. The player's left stick and WASD
	 * reach it through the input handlers (see OnEdgeTriggered); calling this directly is the scripted
	 * path (tests, kitesurf.PreWind), which takes the controls over from the player's stick.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rider|Rotation")
	void SetPreWind(FVector2D Stick);

	UFUNCTION(BlueprintPure, Category = "Rider|Rotation")
	FVector2D GetPreWindStick() const { return PreWindStick; }

	/** How far the pre-wind has been built, 0..1. Back to 0 once the rider is on the water and not loading. */
	UFUNCTION(BlueprintPure, Category = "Rider|Rotation")
	float GetPreWindAmount() const { return PreWindAmount; }

	/**
	 * The rotation stick in the air, same axes as SetPreWind: past AirRotationDeadzone it drives the
	 * attitude's capped control torque and turns the landing assist off. The player's left stick and
	 * WASD reach it in the air through the input handlers; calling this directly is the scripted path
	 * (tests, kitesurf.Input), which takes the controls over from the player's stick.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rider|Rotation")
	void SetAirRotationInput(FVector2D Stick);

	UFUNCTION(BlueprintPure, Category = "Rider|Rotation")
	FVector2D GetAirRotationInput() const { return AirRotationStick; }

	/** Tuck in the air, 0 (stretched) to 1 (tucked): a tuck spins the rider faster. The player tucks by pressing and holding jump in the air; calling this is the scripted path. */
	UFUNCTION(BlueprintCallable, Category = "Rider|Rotation")
	void SetTuck(float Amount);

	/**
	 * Player rider input (T1.4; batch A, docs/tricks/review.md section 4, amending
	 * docs/tricks/README.md decision 7). The left stick and WASD are IA_Edge (X) and
	 * IA_WeightShift (Y), and are read by state, with no action of their own:
	 * - on the water: the board's carve and weight shift, as always;
	 * - holding jump on the water (loading) with IA_Rotate also held (Shift / LT): the pre-wind,
	 *   while the board keeps the carve and weight shift it had when the load started
	 *   (bPreWindLatchesBoardInput); IA_Rotate up, the stick just keeps carving and shifting weight
	 *   through the load, so a plain jump stays straight;
	 * - in the air with IA_Rotate held: the rotation stick (SetAirRotationInput); without it the
	 *   stick does nothing to the attitude. A jump press held in the air is the tuck either way.
	 * Stick X is turned into the rotation's X by the screen side of the rider's back
	 * (GetScreenBackSign), so X towards the side of the screen the rider's back is on is a back roll.
	 * These are the functions Enhanced Input calls; they are public so tests drive the player path
	 * through them.
	 */
	void OnEdgeTriggered(const FInputActionValue& Value);
	void OnWeightShiftTriggered(const FInputActionValue& Value);
	void OnJumpPressed(const FInputActionValue& Value);
	void OnJumpReleased(const FInputActionValue& Value);

	/**
	 * The rotation modifier (batch A): LeftShift on the keyboard, LT on the gamepad (digital past
	 * half travel). Held, the player's stick reaches the pre-wind while loading and the rotation
	 * stick in the air (see OnEdgeTriggered above); without it the stick is always the board's carve
	 * and weight shift, so a plain jump stays straight. Public so tests drive the player path.
	 */
	void OnRotatePressed(const FInputActionValue& Value);
	void OnRotateReleased(const FInputActionValue& Value);

	/** True while IA_Rotate is held. */
	bool IsRotateHeld() const { return bRotateHeld; }

	/**
	 * Grabs and the one-footer (T2.1, T2.2): IA_GrabFront (LB, Q), IA_GrabBack (RB, E) and IA_OneFoot
	 * (L3, C), Started and Completed. In the air only (FGrabState): a grab button sends that hand to
	 * the board, and while one is held the left stick picks the zone (rotation axes: towards the
	 * chest the toe edge, the back the heel edge, up the nose, down the tail) instead of rotating the
	 * rider, whose rotation keeps its momentum. Both grab buttons together (the chord) take the board
	 * off the feet (T2.3, FBoardOffState): the stick picks the variant as it comes off (up superman,
	 * down tic tac, sideways board pass, centred a plain board-off), and letting go of either button
	 * catches it again, which must be done before the touchdown. The one-footer takes the back foot
	 * out of its strap while held; it must be back in before the landing. Public so tests press them.
	 */
	void OnGrabFrontPressed(const FInputActionValue& Value) { bGrabFrontHeld = true; }
	void OnGrabFrontReleased(const FInputActionValue& Value) { bGrabFrontHeld = false; }
	void OnGrabBackPressed(const FInputActionValue& Value) { bGrabBackHeld = true; }
	void OnGrabBackReleased(const FInputActionValue& Value) { bGrabBackHeld = false; }
	void OnOneFootPressed(const FInputActionValue& Value) { bOneFootHeld = true; }
	void OnOneFootReleased(const FInputActionValue& Value) { bOneFootHeld = false; }

	/**
	 * The scripted path for the trick buttons (tests, kitesurf.Trick): holds the grab and one-footer
	 * buttons and the grab zone stick, in rotation axes (X +1 towards the rider's back, the heel
	 * edge; -1 the toe edge; Y +1 the nose, -1 the tail), with no screen-side mapping. The player's
	 * stick takes the zone back on its next move.
	 */
	void SetTrickInput(bool bGrabFront, bool bGrabBack, bool bOneFoot, FVector2D ZoneStick = FVector2D::ZeroVector);

	/**
	 * Unhooked riding (T3.1, docs/tricks/T3.md sections 1 and 2): IA_Hook (Y, F) hooks in or out on
	 * the water, IA_Pass (X on both keyboard and pad since batch A) is the handle pass, used in the
	 * air while unhooked. A press is
	 * kept until the next fixed step, which hands it to BarStateMachine::Step (StepBar). Public so
	 * tests press them; PressHook and PressPass are the scripted path (kitesurf.Hook, kitesurf.Pass).
	 */
	void OnHookPressed(const FInputActionValue& Value) { PressHook(); }
	void OnPassPressed(const FInputActionValue& Value) { PressPass(); }
	void PressHook() { bHookPressPending = true; }
	void PressPass() { bPassPressPending = true; }

	/** The bar and handle-pass state (Tricks/BarState.h), stepped every fixed step after the kite. */
	const FBarState& GetBarState() const { return Bar; }

	UFUNCTION(BlueprintPure, Category = "Rider|Bar")
	bool IsHooked() const { return Bar.bHooked; }

	/** The bar was pulled from the hands (grip limit, a pass under load, wrapped lines): the kite is on its leash until the next reset. */
	UFUNCTION(BlueprintPure, Category = "Rider|Bar")
	bool IsBarLost() const { return Bar.Place == EBarPlace::Lost; }

	/**
	 * Unhooked, how far the arms are out, 0 (bar at the hips) to 1 (arms straight out along the
	 * lines). Every bar input sets it through SheetKite while unhooked (ArmExtensionForBar), and the
	 * kite's own sheet stays at UnhookedStopperSheet. Hooked it follows the bar too, unused.
	 */
	UFUNCTION(BlueprintPure, Category = "Rider|Bar")
	float GetArmExtension() const { return bFlipArmsIn ? FlipArmExtension : ArmExtension; }

	/** The arm extension the bar's position gives (ArmExtensionForBar), before the flip's arms in (T3.3). */
	float GetBarArmExtension() const { return ArmExtension; }

	/** Unhooked, a flip pre-wind has pulled the arms in to FlipArmExtension for this jump (T3.3), and the player has not moved the bar since. */
	bool AreFlipArmsIn() const { return bFlipArmsIn; }

	/**
	 * The raley's arms (T3.2): unhooked, both hands on the bar in front, and the arms out at
	 * RaleyArmExtension or more. The rider attitude then lets the line swing the body out and turns
	 * the roll input about the lines (the S-bend; FAttitudeInputs::bRaleyArms).
	 */
	bool HasRaleyArms() const;

	/** The tantrum's back hand is off the bar (T3.3): from a backflip pre-wind's take-off until RegrabBeforeContactSeconds before contact. */
	bool IsTantrumHandOff() const { return bTantrumHandOff; }

	/**
	 * The arm extension a bar position gives unhooked: the bar's middle (Neutral) is DefaultExtension,
	 * fully in (1) is 0, the bar at the hips, and fully out (0) is 1, the arms straight; linear either
	 * side of the middle.
	 */
	static float ArmExtensionForBar(float BarPosition, float Neutral, float DefaultExtension);

	/** Where the lines pulled on the body on the last fixed step (cm, body frame, from the centre of mass): LineAttach::AttachPointBody. */
	FVector GetLineAttachBodyCm() const { return LineAttachBodyCm; }

	/** The bar's centre as last drawn (world, cm). */
	FVector GetDrawnBarCentre() const { return DrawnBarCentre; }

	/** How many times the motion bar was recentred for a hook toggle, for tests. */
	int32 GetHookRecentreCount() const { return HookRecentreCount; }

	/**
	 * Unhooked, the chicken loop rides up to the stopper: the kite's sheet is held here (0..1).
	 * docs/tricks/T3.md 1.2 has 0.6; T3.1 PR 3 lowered it to 0.45, the lowest it allows, which makes
	 * the unhooked pop the most ballistic with the low park at 45 deg (KiteSurf.Trick.UnhookedPopIsBallistic).
	 * Estimate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Bar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UnhookedStopperSheet;

	/**
	 * Unhooked, the kite's low park holds it this high (deg above the horizon; UKiteComponent::
	 * SetLowParkAssist). Kept at 45 by T3.1 PR 3: lower is more ballistic (35 gives 8h/t^2 8.3 m/s^2
	 * against 7.3 at 45), but the landing evaluator grades a landing with the kite under
	 * HotLandingKiteElevationDeg (45) sketchy (KiteTooLow), so every unhooked landing would be. Estimate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Bar", meta = (ClampMin = "10.0", ClampMax = "85.0"))
	float LowParkElevationDeg;

	/** Unhooked, the arm extension with the bar in the middle (BarNeutralSheet). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Bar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UnhookedArmExtensionDefault;

	/**
	 * Unhooked, arms out at least this far (GetArmExtension) is the raley's extension (T3.2): the bar
	 * pushed out past the middle's UnhookedArmExtensionDefault. Below it a roll pre-wind is the roll it
	 * is hooked in (the KGB's back roll). Estimate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Bar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RaleyArmExtension;

	/**
	 * Unhooked, a flip pre-wind (stick Y) pulls the bar in to this arm extension for the jump, a short
	 * lever so the line does not fight the flip (T3.3; docs/tricks/T3.md 1.2), unless the player moves
	 * the bar. Estimate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Bar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlipArmExtension;

	/** The tantrum (T3.3): unhooked, a backflip pre-wind takes the back hand off the bar at take-off. On by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Bar")
	bool bTantrumBackHandOff;

	/** The tantrum's back hand is back on the bar when the time to contact is under this (s). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Bar", meta = (ClampMin = "0.0"))
	float RegrabBeforeContactSeconds;

	/** The flick assist: a pass started in the air dips the kite for slack (UKiteComponent::RequestFlick). On by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Bar")
	bool bFlickAssist;

	/** The bar on its leash hangs this far up the lines from the harness (cm). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Bar", meta = (ClampMin = "0.0"))
	float LeashLengthCm;

	/** The grip limit, the pass's slack window and the rest (BarStateMachine). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Bar")
	FBarTunables BarTunables;

	/** Where the lines pull on the body for each bar state (LineAttach). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Bar")
	FLineAttachTunables LineAttachTunables;

	/**
	 * The riding stance on the water (T3.5, docs/tricks.md section 2): Heelside, or after an unhooked
	 * landing Toeside (chest away from the kite, the bar in front, the lines round the front) or Blind
	 * (back to the kite, the bar behind the back). Set at the touchdown from the body's heading against
	 * the kite and the bar's route (BarStateMachine::StanceForWrap of the effective wrap). Toeside and
	 * Blind are held for ToesideHoldSeconds and BlindHoldSeconds with no slide round; then the slide
	 * round brings the rider back to Heelside, as it always has for a rider with the back to the kite.
	 * On the water X (IA_Pass) ends them early: from Toeside the half turn back the way the frontside 180
	 * came; from Blind a surface pass and the backside half turn on to heelside (with the lines already
	 * passed round in the air, the half turn alone). Hooked in, the stance is always Heelside, so hooked
	 * riding slides round as before.
	 */
	UFUNCTION(BlueprintPure, Category = "Rider|Stance")
	ETrickStance GetRidingStance() const { return RidingStance; }

	/** How long the rider has been in the current riding stance on the water (s). */
	UFUNCTION(BlueprintPure, Category = "Rider|Stance")
	float GetStanceSeconds() const { return StanceSeconds; }

	/** The torso twist the rig was last drawn with (deg, + turns the chest to the rider's right; FRiderRigInput::TorsoTwistDeg). */
	float GetDrawnTorsoTwistDeg() const { return DrawnTorsoTwistDeg; }

	/** Toeside is held this long after the landing before the slide round (s). Estimate (docs/tricks/T3.md T3.5). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Stance", meta = (ClampMin = "0.0"))
	float ToesideHoldSeconds;

	/** Blind is held this long after the landing before the slide round (s). Estimate (docs/tricks/T3.md T3.5). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Stance", meta = (ClampMin = "0.0"))
	float BlindHoldSeconds;

	/** Riding toeside the torso twists this far back towards the kite over the hips (deg). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Stance", meta = (ClampMin = "0.0", ClampMax = "120.0"))
	float ToesideTorsoTwistDeg;

	/** How fast the drawn torso twists into and out of the toeside twist (deg/s). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Stance", meta = (ClampMin = "1.0"))
	float TorsoTwistRateDegPerSec;

	/** The grabs and the one-footer, stepped in the fixed step before the rider attitude. */
	const FGrabState& GetGrabState() const { return GrabState; }
	FGrabState& GetGrabState() { return GrabState; }

	/** The grab zone stick FGrabState reads, in rotation axes. */
	FVector2D GetGrabZoneStick() const { return GrabZoneStick; }

	/** How far the drawn board was pulled towards the grabbing hand on the last drawn frame (cm, world). */
	FVector GetGrabBoardPullCm() const { return GrabBoardPullCm; }

	/**
	 * In a grab the body holds still and the drawn board is pulled towards the grabbing hand's
	 * shoulder until its socket is this share of the arm's reach away (the arm a little bent). Estimate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Tricks", meta = (ClampMin = "0.3", ClampMax = "0.99"))
	float GrabReachFraction;

	/** The most the drawn board is pulled towards a grabbing hand (cm). The legs still reach the straps. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Tricks", meta = (ClampMin = "0.0"))
	float GrabMaxBoardPullCm;

	/** Where a back foot out of its strap goes, in the board's stance space (cm: X to the nose, Y to the toe edge, Z up): off the tail, on the heel side, lifted. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Tricks")
	FVector OneFootKickStanceCm;

	/** The left stick / WASD as the player holds it (X from IA_Edge, Y from IA_WeightShift), before it is routed. */
	FVector2D GetPlayerRiderStick() const { return PlayerRiderStick; }

	/** True while the player's stick and jump button own the board input, pre-wind, air stick and tuck; the scripted setters and ApplyScriptedInput take them over. */
	bool IsPlayerRiderInputActive() const { return bPlayerRiderInput; }

	/**
	 * +1 or -1: the player's stick X times this is the rotation's X (+1 back roll). +1 when the
	 * rider's back is on the right of the screen. Latched when the load starts, or at the take-off
	 * when the rider leaves the water without loading, and held for the whole airtime.
	 */
	float GetScreenBackSign() const { return ScreenBackSign; }

	/**
	 * Which way stick X is a back roll: sign((-BodyFront) . CameraRight) on the horizontal, so +1 when
	 * the rider's back is on the right of the screen. Fallback when the rider faces nearly straight
	 * along the view (|dot| < 0.1), where the screen cannot tell the sides apart.
	 */
	static float ComputeScreenBackSign(const FVector& BodyFront, const FVector& CameraRight, float Fallback);

	/**
	 * While loading on the water, the left stick and WASD set the pre-wind and the board keeps the
	 * carve and weight shift it had when the load started, so the stick does not also carve. Off:
	 * the stick carves and shifts the weight while loading as well as setting the pre-wind.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Rotation")
	bool bPreWindLatchesBoardInput;

	UFUNCTION(BlueprintPure, Category = "Rider|Rotation")
	float GetTuck() const { return TuckInput; }

	/** Time for the pre-wind to build to full while loading with the stick held (s). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Rotation", meta = (ClampMin = "0.01"))
	float PreWindBuildSeconds;

	/** The pre-wind stick builds only past this (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Rotation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PreWindStickThreshold;

	/** The air rotation stick acts only past this (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Rotation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirRotationDeadzone;

	/**
	 * IA_Rotate (batch A) can be let go for up to this long while still loading on the water without
	 * losing the pre-wind (s): releasing the modifier and the jump button in the same frame still
	 * gives the trick. Past it, on the player path, the pre-wind clears, so a modifier let go well
	 * before the pop leaves a plain jump. Estimate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Rotation", meta = (ClampMin = "0.0"))
	float RotateReleaseGraceSeconds;

	/** Time constant of the filter on the measured vertical acceleration the attitude's time to contact uses (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Rotation", meta = (ClampMin = "0.001"))
	float VerticalAccelFilterSeconds;

	/**
	 * How long the drawn rider takes to hand over between the riding pose and the attitude's body
	 * (s): from the take-off, and back after a landing, the torso and the pelvis line blend so the
	 * figure does not jump. The drawn board eases back onto the root over the same time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Rotation", meta = (ClampMin = "0.0"))
	float RiderHandoverSeconds;
	UStaticMeshComponent* GetControlBarMesh() const { return ControlBarMesh.Get(); }

	/**
	 * The board that is drawn. The root BoardMesh is the physics body (it sweeps and collides) and
	 * is not rendered; this child shows the board and the rider's feet are in its straps. It follows
	 * the root exactly unless SetBoardVisualWorldRotation has turned it.
	 */
	UStaticMeshComponent* GetBoardVisual() const { return BoardVisual.Get(); }

	/**
	 * Turns the drawn board to this world rotation, whatever the root does, until
	 * ClearBoardVisualOverride. Its location still follows the root. For the rider attitude in the
	 * air, and later a board held in the hand. The physics body is not turned.
	 */
	void SetBoardVisualWorldRotation(const FQuat& WorldRotation);

	/** Puts the drawn board back on the root. */
	void ClearBoardVisualOverride();

	/** True while SetBoardVisualWorldRotation is turning the drawn board. */
	bool HasBoardVisualOverride() const { return bBoardVisualOverride; }

	/** World yaw the rider's body faces (deg). Always square across the board: the feet are in the straps. */
	UFUNCTION(BlueprintCallable, Category = "Rider")
	float GetRiderFacingYawDeg() const { return RiderFacingYawDeg; }

	/** World yaw the rider's body is shown facing: the stance yaw, plus any slide round still in progress. */
	UFUNCTION(BlueprintCallable, Category = "Rider")
	float GetRiderBodyYawDeg() const { return FRotator::NormalizeAxis(RiderFacingYawDeg + RiderTurnOffsetDeg); }

	/** Where the lines pull on the rider: the harness hook at the front of their waist. */
	UFUNCTION(BlueprintCallable, Category = "Rider")
	FVector GetHarnessHookWorldPosition() const { return HarnessHookPosition; }

	/** +1 when the rider faces the board's right rail, -1 when they face its left. */
	UFUNCTION(BlueprintCallable, Category = "Rider")
	float GetRiderStanceSide() const { return RiderStanceSide; }

	/** Which rail (+1 right, -1 left) a rider on a board at BoardYawDeg faces to look closest to PreferredFacingYawDeg. */
	static float ChooseStanceSide(float BoardYawDeg, float PreferredFacingYawDeg);

	/** Holds or lets go of the loaded crouch (the jump button held down). */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void SetLoadHeld(bool bHeld);

	/** The jump button let go: pops with whatever load has built up, then stands the rider up. True if they left the water. */
	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ReleaseLoadAndPop();

	/** Brief controller vibration on the pop, landings, crashes, the kite hitting the water and a hard yank on the lines. */
	UFUNCTION(BlueprintCallable, Category = "Input|Haptics")
	void SetHapticsEnabled(bool bEnabled) { bHapticsEnabled = bEnabled; }

	UFUNCTION(BlueprintPure, Category = "Input|Haptics")
	bool AreHapticsEnabled() const { return bHapticsEnabled; }

	/** One short buzz: strength 0..1, for this long, on the heavy motors (a thump) or the light ones (a tick). Does nothing when haptics are off. */
	UFUNCTION(BlueprintCallable, Category = "Input|Haptics")
	void PlayHaptic(float Intensity, float DurationSeconds, bool bHeavy);

	/** How hard and how long a landing of this many g buzzes. */
	static void GetLandingHaptic(float LandingG, float& OutIntensity, float& OutDurationSeconds);

	/** Watches the line tension for a sudden hard pull (a loop's yank) and buzzes once for it. Tick calls this. */
	void UpdateTensionHaptic(float LineTensionN, float DeltaTime);

	/** How many buzzes have been asked for, and the last one, for tests. */
	int32 GetHapticCount() const { return HapticCount; }
	float GetLastHapticIntensity() const { return LastHapticIntensity; }
	float GetLastHapticDuration() const { return LastHapticDuration; }

	/** Line tension above which the lines' pull is felt as a yank (N). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Haptics")
	float HapticYankTensionN = 2200.0f;

	/** Adds a short camera punch (UpdateCamera eases it back out), clamped to CameraKickMaxDeg. */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void KickCamera(float AmountDeg);

	/** How much of the camera kick is left (deg), for tests. */
	UFUNCTION(BlueprintPure, Category = "Camera")
	float GetCameraKickDeg() const { return CameraKickDeg; }

	/**
	 * Uses the controller's motion sensors as the bar: tilt it like a bar to steer, tip its top
	 * towards you to pull the bar in. While it is on and a controller with sensors is found, the
	 * right stick, triggers and bar keys no longer move the bar; without one they carry on working.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input|Motion")
	void SetMotionBarEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Input|Motion")
	bool IsMotionBarEnabled() const { return bMotionBarEnabled; }

	/** True while the bar is actually following a controller's motion sensors. */
	UFUNCTION(BlueprintPure, Category = "Input|Motion")
	bool IsMotionBarActive() const { return bMotionBarActive; }

	/** Takes the way the controller is held now as "bar level, where it is". Done when the motion bar is switched on and on reset. */
	UFUNCTION(BlueprintCallable, Category = "Input|Motion")
	void RecentreMotionBar();

	/**
	 * The recentre button (IA_RecenterMotion: right stick click, Home): takes the way the controller is
	 * held now as "bar level, in the middle" (BarNeutralSheet) and shows "Controller recentred". Does
	 * nothing unless the motion bar is following a controller, so a stray stick click while the right
	 * stick is the bar changes nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input|Motion")
	void RecentreMotionBarToMiddle();

	/** The handler Enhanced Input calls for IA_RecenterMotion; public so tests press it. */
	void OnRecenterMotionTriggered(const FInputActionValue& Value);

	/** How many times the recentre button has recentred the motion bar, for tests. */
	int32 GetMotionRecentreButtonCount() const { return MotionRecentreButtonCount; }

	/** Tilt (pitch) or Move (travel) for the motion bar's power. Steering is the roll either way. */
	UFUNCTION(BlueprintCallable, Category = "Input|Motion")
	void SetMotionSheetMode(EMotionSheetMode InMode);

	UFUNCTION(BlueprintPure, Category = "Input|Motion")
	EMotionSheetMode GetMotionSheetMode() const { return MotionSheetMode; }

	/** The Move mode's travel, as the pawn feeds it; tests read it. */
	const FMotionBarStroke& GetMotionStroke() const { return MotionStroke; }

	/** The Move mode's tunables (deadband, still detection, gain). Its limits and relaxation are set by the pawn every frame. */
	FMotionBarStroke& GetMutableMotionStroke() { return MotionStroke; }

	/**
	 * With the bar returning to the middle, the Move mode's travel also relaxes towards the middle
	 * with this time constant (s), so a drifting position does not need recentring often. Estimate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Motion", meta = (ClampMin = "0.1"))
	float MoveRelaxSeconds = 5.0f;

	/** The controller being read, for the settings screen; empty if none. */
	UFUNCTION(BlueprintPure, Category = "Input|Motion")
	FString GetMotionDeviceName() const;

	/** How the controller is being held, as the motion bar sees it: roll right and pitch towards the player (deg). */
	FVector2D GetMotionTiltDeg() const { return FVector2D(MotionFilter.GetRollDeg(), MotionFilter.GetPitchDeg()); }

	/** The last reading taken from the controller. */
	const FKiteMotionSample& GetLastMotionSample() const { return LastMotionSample; }

	/** Where motion readings come from. Tests put a scripted controller here; otherwise it is the platform's. */
	void SetMotionSource(TSharedPtr<IKiteMotionSource> InSource);

	/** How tilt becomes steering and bar position. */
	FMotionBarMapping MotionBarMapping;

	/** How loud and at what pitch each loop should play for what the rider is doing. Volumes 0..1, pitch 1 = as recorded. */
	static FRideAudioMix ComputeAudioMix(const FRideAudioState& State);

	/** The same for a rider with a parked kite and no edge: wind, water and lines only. */
	static FRideAudioMix ComputeAudioMix(float ApparentWindKnots, float BoardSpeedKnots, bool bOnWater, float LineTensionN);

	/** Plays the sound for something that happened. Running aground and a shark replace the splash of the crash they cause. */
	void PlayRideSound(ERideSound Sound);

	/** How many ride sounds have been asked for, and the last one, for tests. */
	int32 GetRideSoundCount() const { return RideSoundCount; }
	ERideSound GetLastRideSound() const { return LastRideSound; }

	/** Music volume, 0..1: the ride's music follows it at once. */
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void SetMusicVolume(float Volume);

	UFUNCTION(BlueprintPure, Category = "Audio")
	float GetMusicVolume() const { return MusicVolume; }

	/** Ambient volume, 0..1: scales the wind, water, spray, line, kite and flutter loops. */
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void SetAmbientVolume(float Volume);

	UFUNCTION(BlueprintPure, Category = "Audio")
	float GetAmbientVolume() const { return AmbientVolume; }

	/** Effects volume, 0..1: scales the one-shots (pop, landing, crashes, reset and the rest). */
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void SetEffectsVolume(float Volume);

	UFUNCTION(BlueprintPure, Category = "Audio")
	float GetEffectsVolume() const { return EffectsVolume; }

	/** The volume the last one-shot was played at, after the effects volume, for tests. */
	float GetLastOneShotVolume() const { return LastOneShotVolume; }

	UAudioComponent* GetMusicBase() const { return MusicBaseComponent.Get(); }
	UAudioComponent* GetMusicAir() const { return MusicAirComponent.Get(); }
	UAudioComponent* GetSprayLoop() const { return SprayLoopComponent.Get(); }
	UAudioComponent* GetKiteLoop() const { return KiteLoopComponent.Get(); }
	UAudioComponent* GetFlutterLoop() const { return FlutterLoopComponent.Get(); }

	/** The mix the loops are playing at now. */
	const FRideAudioMix& GetAudioMix() const { return AudioMix; }

	UAudioComponent* GetWindLoop() const { return WindLoopComponent.Get(); }
	UAudioComponent* GetWaterLoop() const { return WaterLoopComponent.Get(); }
	UAudioComponent* GetLineLoop() const { return LineLoopComponent.Get(); }
	USoundBase* GetPopSound() const { return PopSound.Get(); }
	USoundBase* GetLandingSound() const { return LandingSound.Get(); }
	USoundBase* GetCrashSound() const { return CrashSound.Get(); }
	USoundBase* GetResetSound() const { return ResetSound.Get(); }
	USoundBase* GetStompSound() const { return StompSound.Get(); }

	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetCurrentSteerInput() const { return CurrentSteerInput; }

	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetCurrentSheetInput() const { return CurrentSheetInput; }

	/**
	 * Sets every held input at once, as the keys or sticks would. Used by the kitesurf.Input console
	 * command to script smoke runs. AirRotation and Tuck go to SetAirRotationInput and SetTuck.
	 */
	void ApplyScriptedInput(float Steer, float SheetRate, float Carve, float WeightShift, bool bLoop, FVector2D AirRotation = FVector2D::ZeroVector, float Tuck = 0.0f);

	/** Shows the chosen rider on the board. */
	UFUNCTION(BlueprintCallable, Category = "Rider")
	void SetRiderCharacter(ERiderCharacter InCharacter);

	UFUNCTION(BlueprintCallable, Category = "Rider")
	ERiderCharacter GetRiderCharacter() const { return RiderCharacter; }

	/** The jointed rider's torso (pelvis to head). */
	UStaticMeshComponent* GetRiderStaticMesh() const { return RiderTorso.Get(); }

	/** The jointed rider's limb parts: thigh, shin, upper arm and forearm for the left side, then the same for the right. */
	const TArray<TObjectPtr<UStaticMeshComponent>>& GetRiderLimbs() const { return RiderLimbs; }

	/** How the jointed rider is posed now: where the pelvis, knees, feet, elbows and hands are. */
	const FRiderRigPose& GetRiderRigPose() const { return RiderPose; }

	/**
	 * Hold the bar moving in (+) or out (-), -1..1; 0 leaves it where it is. The scripted path
	 * (tests, kitesurf.Input): the bar stays where the rate leaves it whatever bBarReturnsToMiddle
	 * says. The player's keys, stick and triggers go through OnSheetTriggered instead.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void SetSheetRateInput(float Axis);

	/**
	 * The player's sheet input (IA_Sheet: Up / Down, the right stick, the triggers), -1..1. With
	 * bBarReturnsToMiddle the bar is spring-loaded: held input sets how far from BarNeutralSheet it
	 * goes (full input: fully in or out; half a trigger: half way), and letting go returns it to the
	 * middle at BarReturnRatePerSec. Without it, the input moves the bar as SetSheetRateInput does.
	 * Public so tests and kitesurf.Bar drive the player path.
	 */
	void OnSheetTriggered(const FInputActionValue& Value);

	/** True while the player's sheet input owns the bar (and so springs back to the middle); scripted calls and the mouse bar take it back. */
	bool IsPlayerSheetInputActive() const { return bPlayerSheetInput; }

	/** The bar springs back to BarNeutralSheet when the player lets go of the sheet input (the "Bar returns to middle" setting, on by default). */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void SetBarReturnsToMiddle(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Input")
	bool GetBarReturnsToMiddle() const { return bBarReturnsToMiddle; }

	/** The bar's resting place with the spring-loaded sheet input, 0..1. Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BarNeutralSheet;

	/** How fast the bar returns to BarNeutralSheet when the player lets go (throw per second). Estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite", meta = (ClampMin = "0.01"))
	float BarReturnRatePerSec;

	/** Sheet in (+) / out (-) input currently held, -1..1. The bar position itself is GetCurrentSheetInput(). */
	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetSheetRateInput() const { return SheetRateInput; }

	/** Bar steering per unit of mouse travel while the right mouse button is held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite")
	float MouseSteerSensitivity;

	/** Bar sheeting per unit of mouse travel while the right mouse button is held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kite")
	float MouseSheetSensitivity;

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

	/** The highest the camera tilts up for a composed shot (deg). It tilts further, up to CameraMaxFramingPitchDeg, only when the kite and the rider would not both fit otherwise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraMaxLookPitchDeg;

	/** The furthest the camera tilts up to keep a high kite and the rider in frame together (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0", ClampMax = "80"))
	float CameraMaxFramingPitchDeg;

	/** When the kite and the rider do not both fit, the view widens up to this horizontal field of view (deg) before the boom pulls back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "60", ClampMax = "150"))
	float CameraMaxFOVDeg;

	/** How far the boom pulls back when even CameraMaxFOVDeg cannot show the kite and the rider together (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float CameraMaxArmLengthCm;

	/** Room kept between the edge of the frame and the kite or the rider (deg), on top of their own size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float CameraFrameMarginDeg;

	/**
	 * The composed shot keeps the kite and the rider inside this fraction of the half frame to the
	 * sides and the bottom, clear of the edges; it tilts, widens and pulls back to do so. Only a
	 * sudden move (a loop, a pop) that the easing has not caught up with yet uses the frame out to
	 * CameraFrameMarginDeg from its edge.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.3", ClampMax = "1"))
	float CameraComfortFrameFraction;

	/** As CameraComfortFrameFraction, towards the top of the frame, where the HUD's panels sit over the sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.3", ClampMax = "1"))
	float CameraComfortTopFraction;

	/** Half the kite's span kept in frame, for the reference kite size; it scales with the kite (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float CameraKiteFrameRadiusCm;

	/** How fast the view widens or the boom pulls back when the kite and the rider need more room (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float CameraZoomOutSpeed;

	/** How fast the view narrows and the boom comes back in once there is room again (1/s); slower, so it does not pump. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float CameraZoomInSpeed;

	/** The camera never goes lower than this above the water under it (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float CameraMinHeightAboveWaterCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraTurnSpeed;

	/** Height of the camera boom's pivot above the board (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraPivotHeightCm;

	/**
	 * In the air the camera looks along the horizontal velocity, not the board, so spins do not
	 * swing it. Slower than this (cm/s) the direction means little and the last heading is held.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float CameraAirMinSpeedCmS;

	/**
	 * How long the boom pivot takes to go from riding on the board's tilt (on the water) to sitting
	 * straight above the board whatever it does (in the air), and back (s).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float CameraPivotLevelSeconds;

	/**
	 * A short, decaying kick added to the look pitch: a Stomped landing's camera punch (review batch
	 * D). KickCamera adds to it, up to CameraKickMaxDeg; UpdateCamera eases it back to 0 at
	 * CameraKickDecaySpeed, so it never becomes a new resting state. It never touches roll, which the
	 * camera otherwise never uses, so the horizon stays level through the kick.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float CameraKickMaxDeg = 6.0f;

	/** How fast the camera kick eases back to 0 (1/s, FInterpTo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float CameraKickDecaySpeed = 10.0f;

	/** The camera kick a Stomped landing adds (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float StompCameraKickDeg = 3.5f;

	/** Lean away from the kite at full line load (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderMaxLeanDeg;

	/** Extra lean back while floating in the water (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderFloatLeanDeg;

	/** Extra lean away from the kite in a full loaded crouch (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderLoadLeanDeg;

	/** Extra lean back against the harness at full UBoardMovementComponent::GetHarnessLeanAmount, the carve held against the limit of how far the board can point from the pull (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderHarnessLeanDeg;

	/** Most the rider hangs back from the harness in the air, with the kite low and pulling (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderAirHangLeanDeg;

	/** How long the rider rides with their back to the kite before sliding the board round to face it (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderSwitchDelaySeconds;

	/** How fast the rider comes round when they slide the board to face the kite (deg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	float RiderSwitchTurnRateDeg;

	/** The harness hook on the rider's body: forward, right, up from the pelvis (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	FVector HarnessHookOffsetCm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UKiteComponent> Kite;

protected:
	virtual void BeginPlay() override;

	// Components
	/** The root and the physics body: swept by UBoardMovementComponent and collides, but is not rendered (BoardVisual is). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BoardMesh;

	/** The board that is drawn: a child of BoardMesh with no collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BoardVisual;

	/** The jointed riders (Santa, wetsuit): a torso and eight limb parts, posed every frame by RiderRig. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> RiderTorso;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TArray<TObjectPtr<UStaticMeshComponent>> RiderLimbs;

	FRiderRigPose RiderPose;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rider")
	ERiderCharacter RiderCharacter;

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

	/** Wind lines on the water round the rider: how the wind's direction is read off the sea. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWindStreakComponent> WindStreaks;

	/** Follows the jumps by polling the board and the kite: jump records, trick names and scores. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTrickTrackerComponent> TrickTracker;

	/** The rider's body orientation and angular momentum in the air (T1.2). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<URiderAttitudeComponent> RiderAttitude;

	// Sound: loops that play all the time and are faded and pitched by UpdateAudioModulation
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> WindLoopComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> WaterLoopComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> LineLoopComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> SprayLoopComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> KiteLoopComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> FlutterLoopComponent;

	/** The ride's music: two loops of the same length played in step. The second comes in while the rider is in the air. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> MusicBaseComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UAudioComponent> MusicAirComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> KiteCrashSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> RelaunchSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> AgroundSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> SharkSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> PopSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> LandingSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> CrashSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> ResetSound;

	/** A Stomped landing's extra one-shot, on top of the g-scaled splash (review batch D). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> StompSound;

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
	TObjectPtr<UInputAction> WeightShiftAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> PauseAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ResetAction;

	/** Recentres the motion bar with the bar in the middle (right stick click, Home). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> RecenterMotionAction;

	/** Grab with the front hand in the air (LB, Q). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> GrabFrontAction;

	/** Grab with the back hand in the air (RB, E). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> GrabBackAction;

	/** Back foot out of its strap in the air (left stick click, C). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> OneFootAction;

	/** Hook in or out of the harness, on the water (Y, F). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> HookAction;

	/** The handle pass, in the air while unhooked: X on the keyboard matches the pad's X (left face). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> PassAction;

	/** The rotation modifier (batch A): held, the stick reaches the pre-wind and the air rotation stick (LeftShift, LT). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> RotateAction;

public:
	UFUNCTION(BlueprintCallable, Category = "Gameplay")
	void ResetRider();

	UFUNCTION(BlueprintCallable, Category = "UI")
	void TogglePause();

private:
	void OnSteerTriggered(const FInputActionValue& Value);
	void UpdateMouseBar();

	/** Moves the bar for this frame from the sheet input: the spring, the rate, or the motion bar. Tick calls it after UpdateMotionBar. */
	void UpdateBarSheet(float DeltaTime);

	/** Sets the bar's position and hands it to the kite, without changing who owns the bar. */
	void ApplySheet(float Amount);

	/** Hands the bar back from the player's spring to a position that holds: scripted calls and the mouse bar. */
	void ReleasePlayerSheetSpring();

	/** Where the spring-loaded bar heads for this input, -1..1: BarNeutralSheet, moved that fraction of the way to fully in or out. */
	float GetSpringSheetTarget(float Input) const;

	/** The motion bar's own bar position, 0..1, before the keys' trim: the pitch (Tilt) or the travel (Move). */
	float GetMotionSheet() const;
	void OnPauseTriggered(const FInputActionValue& Value);
	void OnResetTriggered(const FInputActionValue& Value);

	UFUNCTION()
	void HandleBoardCrash(float Intensity);

	UFUNCTION()
	void HandleBoardReset();

	UFUNCTION()
	void HandleBoardLanding(float LandingG);

	/**
	 * The grade-specific layer on top of HandleBoardLanding's g-scaled splash and buzz (review batch
	 * D): Stomped gets a heavy haptic, a short camera kick and its own one-shot; Sketchy a light
	 * wobble. Clean adds nothing here, and Crash is HandleBoardCrash's job (bound to OnBoardCrash,
	 * broadcast right after this on a crash landing), unchanged.
	 */
	UFUNCTION()
	void HandleBoardLandingVerdict(const FLandingVerdict& Verdict);

	void UpdateAudioModulation(float DeltaTime);

	/** kite.Physics.Debug 1: forces, winds and numbers at the kite and the rider, drawn for one frame. */
	void DrawPhysicsDebug() const;

	/** kite.Physics.Debug 2: one CSV line per fixed step to LogKiteSurf, so a ride can be plotted. */
	void LogPhysicsTelemetry();
	bool bLoggedTelemetryHeader = false;

	FRideAudioMix AudioMix;
	void PlayOneShot(USoundBase* Sound, float Volume, float Pitch = 1.0f);
	float MusicVolume = 0.6f;
	float AmbientVolume = 1.0f;
	float EffectsVolume = 1.0f;
	float LastOneShotVolume = 0.0f;
	int32 RideSoundCount = 0;
	ERideSound LastRideSound = ERideSound::KiteCrash;
	/** Set by running aground or a shark: the crash that follows keeps quiet, as they have their own sound. */
	bool bSkipNextCrashSplash = false;

	UFUNCTION()
	void HandleKiteRelaunched();

	void UpdateMotionBar(float DeltaTime);
	TSharedPtr<IKiteMotionSource> MotionSource;
	FMotionBarFilter MotionFilter;
	FKiteMotionSample LastMotionSample;
	bool bMotionBarEnabled = false;
	bool bMotionBarActive = false;
	bool bMotionRecentrePending = false;
	/** The pending recentre puts the bar in the middle rather than leaving it where it is. */
	bool bMotionRecentreToMiddle = false;
	int32 MotionRecentreButtonCount = 0;
	EMotionSheetMode MotionSheetMode = EMotionSheetMode::Tilt;
	FMotionBarStroke MotionStroke;

	UFUNCTION()
	void HandleKiteCrashed(FVector Location);
	bool bHapticsEnabled = true;
	int32 HapticCount = 0;
	float LastHapticIntensity = 0.0f;
	float LastHapticDuration = 0.0f;
	float YankCooldownSeconds = 0.0f;
	bool bAboveYankTension = false;
	void UpdateCamera(float DeltaTime);
	void UpdateRiderPose(float DeltaTime);

	/**
	 * The riding lean of the body: away from the pull, back in the water while floating, back over
	 * the tail while loading (Load, 0..1), and back against the hook with the carve held against the
	 * harness's limit (HarnessLean, 0..1, UBoardMovementComponent::GetHarnessLeanAmount). Facing is the
	 * level facing, TowardsKite the level direction to the kite (zero without one). Shared by the drawn
	 * pose (with the smoothed kite) and the attitude's slaved pose in the fixed step (with the kite
	 * where it is).
	 */
	FVector ComputeLevelBodyUp(const FVector& Facing, const FVector& TowardsKite, bool bHasKite, bool bAirborne, float Load, float HarnessLean) const;

	/** The attitude's pose on the water, from the simulation's own state: Up along the riding lean, Front the stance facing. */
	FQuat ComputeSlavedBodyQuat() const;

	/** Steps the grabs and the one-footer for one fixed step and hands the board the back foot. Before the rider attitude. */
	void StepGrabs(float StepSeconds);

	/**
	 * Steps the bar for one fixed step (BarStateMachine::Step), after the kite: the hook toggle, the
	 * grip limit, the wrap and the pass, and what they do to the kite (low park, the stopper, the
	 * flick, the leash) and the board (a bar lost on the water crashes; in the air the landing does).
	 * A board reset rehooks.
	 */
	void StepBar(float StepSeconds);

	/** Back to hooked in with the bar in both hands: the kite off its leash and low park, the bar's sheet back on the kite. */
	void ResetBar();

	/**
	 * T3.3, before the bar: the flip's arms in (a flip pre-wind while unhooked, kept to the touchdown
	 * unless the bar moves) and the tantrum's back hand (off at a backflip take-off, back on before
	 * contact).
	 */
	void StepFreestyleHands();

	/** The pre-wind wound up is a flip (stick Y in the attitude's flip sector); OutBack is set for a backflip. */
	bool IsFlipPreWind(bool* OutBack = nullptr) const;

	/** The body frame the bar and the line attach use: the attitude's body (slaved on the water). */
	FQuat GetBarBodyQuat() const;

	/** +1 when the board's nose is on the body's right (the attitude's strap offset). */
	float GetBarNoseSideSign() const;

	/**
	 * The riding stance for one fixed step (T3.5), after the board: set at a touchdown from the bar's
	 * effective wrap and route, Heelside whenever hooked, without the bar, crashing or floating; a held
	 * stance counts up and ends in the slide round at its hold time, or at a buffered half turn from
	 * Blind once the tension allows it.
	 */
	void StepStance(float StepSeconds);

	/**
	 * Starts the slide round to the other rail on the water (the drawn body turns over
	 * RiderSwitchTurnRateDeg) and sets the stance to Heelside: the way the chest goes through the
	 * board's leading end when bViaTravelNose, through its trailing end otherwise.
	 */
	void StartStanceTurn(bool bViaTravelNose);

	/** The chest goes through the leading end for a half turn that raises the wrap (it is under 0), the trailing end otherwise. */
	void StartUnwindingStanceTurn();

	void SetRidingStance(ETrickStance Stance);

	ETrickStance RidingStance = ETrickStance::Heelside;
	float StanceSeconds = 0.0f;
	/** After a touchdown the stance follows the lines for this long, while the body settles on its rail (s). */
	static constexpr float StanceSettleSeconds = 0.2f;
	float StanceSettleSecondsLeft = 0.0f;
	bool bStanceWasAirborne = false;
	/** Blind with the lines passed round in the air: X waits this long for the tension to allow the half turn (s). */
	float StanceTurnBufferLeft = 0.0f;
	/** The bar's nose side on the last StepBar, and the one latched at the touchdown for a held stance (the straps', as in the air). */
	float LastBarNoseSideSign = 1.0f;
	float StanceNoseSideSign = 1.0f;
	/** The drawn toeside twist (deg) and the side it goes to (+1 the rider's right), latched as it starts. */
	float DrawnTorsoTwistDeg = 0.0f;
	float TorsoTwistSign = 1.0f;

	FBarState Bar;
	bool bHookPressPending = false;
	bool bPassPressPending = false;
	float ArmExtension = 0.7f;
	/** T3.3: the flip's arms in, and the bar position when they went in (moving the bar from there ends it). */
	bool bFlipArmsIn = false;
	bool bFlipArmsOverridden = false;
	float FlipArmsBarAtStart = 0.5f;
	/** T3.3: the tantrum's back hand is off the bar; the board's air state on the last StepFreestyleHands. */
	bool bTantrumHandOff = false;
	bool bFreestyleHandsWasAirborne = false;
	FVector LineAttachBodyCm = FVector::ZeroVector;
	FVector DrawnBarCentre = FVector::ZeroVector;
	int32 SeenBarResetCount = 0;
	int32 HookRecentreCount = 0;
	float GripHapticCooldownSeconds = 0.0f;

	/** Steps the rider attitude for one fixed step and hands the board its air orientation. Called between the line force and the board. */
	void StepRiderAttitude(float StepSeconds);

	/** Puts the attitude back on the riding pose with nothing in hand and the drawn rider and board on the root (crash, reset). */
	void ResetRiderAttitude();

	/** Turns and places the drawn board for the attitude in the air, and eases it back onto the root after a landing. Before the rig. */
	void UpdateBoardVisualFromAttitude(float DeltaTime);

	/**
	 * Sends the player's stick and jump button where the rider's state says (see OnEdgeTriggered):
	 * the board, the pre-wind, or the air stick and the tuck. Does nothing while the scripted path
	 * owns those inputs. The handlers call it, and Tick calls it before the fixed steps so a state
	 * change (load, take-off, landing) re-routes a stick that is held still.
	 */
	void RoutePlayerRiderInput();

	/** Latches GetScreenBackSign when the load starts, or at a take-off without a load. Every Tick and before routing. */
	void UpdateScreenBackSignLatch();

	FVector2D PlayerRiderStick = FVector2D::ZeroVector;
	bool bPlayerRiderInput = false;
	/** IA_Rotate (batch A): held, the player's stick reaches the pre-wind and the air rotation; let go, the stick is always the board. */
	bool bRotateHeld = false;
	/** How long IA_Rotate has been up while still loading on the water, on the player path: past RotateReleaseGraceSeconds the pre-wind clears (StepRiderAttitude). */
	float RotateReleaseElapsedSeconds = 0.0f;

	FGrabState GrabState;
	bool bGrabFrontHeld = false;
	bool bGrabBackHeld = false;
	bool bOneFootHeld = false;
	FVector2D GrabZoneStick = FVector2D::ZeroVector;
	/** SetTrickInput owns the zone stick until the player's stick moves. */
	bool bScriptedGrabZoneStick = false;
	FVector GrabBoardPullCm = FVector::ZeroVector;
	/** The board-off variant being drawn; kept through the last frames of a catch, when the state is back on the feet. */
	ETrickBoardOff DrawnBoardOffVariant = ETrickBoardOff::Plain;
	/** Jump was pressed in the air and is still held: the tuck. A press that began on the water (a kite lift-off with the button still down) does not tuck until pressed again. */
	bool bPlayerTuckHeld = false;
	float ScreenBackSign = 1.0f;
	bool bBackSignWasLoading = false;
	bool bBackSignWasAirborne = false;

	FVector2D PreWindStick = FVector2D::ZeroVector;
	/** The last pre-wind stick direction held past the threshold while it was building: what the take-off uses. */
	FVector2D PreWindDirection = FVector2D::ZeroVector;
	float PreWindAmount = 0.0f;
	FVector2D AirRotationStick = FVector2D::ZeroVector;
	float TuckInput = 0.0f;
	/** The load and edge on the water at the last step before the rider left it: the board's Jump() zeroes the load. */
	float LastGroundLoad = 0.0f;
	float LastGroundEdgeHold = 0.0f;
	/** Sigma, the travel side, latched at take-off (RiderAxes::TravelSide). */
	float TravelSideSigma = 1.0f;
	/** The board's vertical speed at the last step, and the filtered vertical acceleration from it. */
	float LastStepVerticalSpeedCmS = 0.0f;
	float FilteredVerticalAccelCmS2 = 0.0f;
	bool bHasLastStepVerticalSpeed = false;
	/** The render alpha of the last Tick: how far between the last two steps the frame is drawn. */
	float LastRenderAlpha = 1.0f;
	/** 0: the drawn rider is the riding pose; 1: the attitude's body. Moves over RiderHandoverSeconds. */
	float RiderAirBlend = 0.0f;
	/** The attitude's body and board as last drawn in the air, held for the hand-over after the landing. */
	FQuat LastAirBodyQuat = FQuat::Identity;
	FQuat LastAirBoardQuat = FQuat::Identity;
	FVector LastAirBoardOffsetCm = FVector::ZeroVector;
	/** The load the rider is drawn with (0..1): the board's, let go no faster than the board lets it go. */
	float DrawnLoad = 0.0f;
	/** True while the drawn board's override is the attitude's (not someone else's SetBoardVisualWorldRotation). */
	bool bAttitudeOwnsBoardVisual = false;
	/** The board's reset count the drawn board last saw: a reset ends the rotation at once. */
	int32 SeenAttitudeResetCount = 0;

	/** True once the kite simulation has placed the kite at line length from the rider. */
	bool HasKitePosition() const;

	float CurrentSteerInput;
	float CurrentSheetInput;
	float SheetRateInput;
	/** The "Bar returns to middle" setting. */
	bool bBarReturnsToMiddle;
	/** The last sheet input came from the player (OnSheetTriggered), so the spring applies. */
	bool bPlayerSheetInput;
	/** While the motion bar is active: the player's spring-loaded trim on top of the controller's bar position. */
	float PlayerSheetOffset;
	float KeySteerInput;
	float MouseSteerInput;
	/** Set by ApplyScriptedInput: the bar goes straight to the kite wherever it is. */
	bool bScriptedRawSteer;
	FVector SmoothedKiteOffset;
	float CameraYawDeg;
	float CameraLookPitchDeg;
	/** The direction the camera looks along before the kite clamp: the board on the water, the flight in the air (deg). */
	float CameraHeadingYawDeg;
	/** 0: the boom pivot rides on the board's pitch and roll, as on the water. 1: it sits straight above the board, as in the air. */
	float CameraPivotAirBlend;
	/** The field of view, boom length and boom pitch UpdateCamera last set (deg, cm, deg). */
	float CameraCurrentFOVDeg;
	float CameraCurrentArmCm;
	float CameraCurrentBoomPitchDeg;
	/** Where the boom pivot was last frame, to predict the boom's location lag (cm). */
	FVector CameraLastPivotLocation;
	/** The camera kick left to ease out (deg); see CameraKickMaxDeg. */
	float CameraKickDeg = 0.0f;
	bool bBoardVisualOverride;
	float RiderFacingYawDeg;
	float RiderStanceSide;
	/** Yaw still to come off while the rider slides round to face the kite (deg). */
	float RiderTurnOffsetDeg;
	float BackToKiteSeconds;
	FVector HarnessHookPosition;
	/** Time left in which a rider who has just got on the board picks the rail that faces the kite (s). */
	float StanceChoiceSecondsLeft;
	static constexpr float StanceChoiceWindowSeconds = 0.3f;
	/** The board's reset count when the stance was last chosen for a reset. */
	int32 SeenBoardResetCount;
	bool bViewInitialized;
	uint64 LastPauseToggleFrame;
	float KiteAzimuthDeg;
	FVector BoardVelocity;

	// Fixed-step driver state
	float SimAccumulatorSeconds;
	float SimTimeSeconds;
	int32 LastFrameSimSteps;
	bool bHasSimState;
	/** The simulation's own transform, before and after the last step. */
	FVector PrevSimLocation;
	FVector SimLocation;
	FQuat PrevSimRotation;
	FQuat SimRotation;
	/** Where the root was last drawn, so a teleport from outside the step loop can be told apart from our own interpolation. */
	FVector LastRenderLocation;
	FQuat LastRenderRotation;
};
