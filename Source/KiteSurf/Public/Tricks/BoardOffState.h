#pragma once

#include "CoreMinimal.h"
#include "Tricks/TrickTypes.h"

/** Tunables of FBoardOffState (docs/tricks/T2.md T2.3). Every value is an estimate. */
struct FBoardOffTuning
{
	/** Time to take the board off the feet and up to the hands (s): the hold starts when it gets there. */
	float RemoveSeconds = 0.25f;
	/** Time from letting go to the board back under the feet, in the straps (s). */
	float RecatchSeconds = 0.30f;
	/**
	 * The last part of the re-catch that still counts as caught at a touchdown, sketchy (s): let go at
	 * least RecatchSeconds before the touchdown to land it clean, at least RecatchSeconds -
	 * RecatchGraceSeconds (0.18 s) to land it at all.
	 */
	float RecatchGraceSeconds = 0.12f;
	/** The variant stick picks a variant only past this; inside it the board-off is Plain. */
	float StickDeadzone = 0.5f;
	/** Time for the tic tac's 360 about the board's long axis (s), from the hold. */
	float TicTacSpinSeconds = 0.5f;
	/** A tic tac is credited only once the board has turned at least this far (deg); short of it the jump is a plain board-off. */
	float TicTacMinDeg = 345.0f;
	/** Time for the board pass round the back (s), from the hold. */
	float BoardPassSeconds = 0.9f;
	/** A board pass is credited only once it has gone at least this far round (0..1); short of it the jump is a plain board-off. */
	float BoardPassMinPhase = 0.95f;
	/** The board must be held off at least this long for the jump to be named and scored a board-off (s). */
	float MinOffSeconds = 0.2f;
	/**
	 * How much a held board tucks the body (0..1), per variant: the attitude does not compose the
	 * board's inertia, so a board held away from the body is approximated as a tuck (the board's
	 * share of the body's inertia comes off). The superman stretches the body, so its tuck is small.
	 */
	float TuckPlain = 0.6f;
	float TuckSuperman = 0.2f;
	float TuckTicTac = 0.6f;
	float TuckBoardPass = 0.5f;
};

/** What FBoardOffState reads each fixed step. */
struct FBoardOffInput
{
	/** Both grab buttons are held (LB+RB, Q+E). */
	bool bChord = false;
	/** One of them was pressed this step with the other held, or both together: the chord was made this step. */
	bool bChordPressed = false;
	/** The board is in the air at the start of the step. On the water nothing starts. */
	bool bAirborne = false;
	/** The variant stick, in the air rotation's axes (FGrabStateInput::ZoneStick): Y +1 up, -1 down; X sideways. */
	FVector2D Stick = FVector2D::ZeroVector;
};

/**
 * The board-off (T2.3): the board taken off the feet in the air, held, and caught again. Pure: no
 * UObject, no world. FGrabState owns one and steps it with the chord of its two grab buttons.
 *
 * In the air, making the chord takes the board off over RemoveSeconds; the strongest push of the
 * stick during the removal picks the variant (neutral Plain, up Superman, down TicTac, sideways
 * BoardPass), latched when the board reaches the hands and the hold starts. Breaking the chord
 * (letting go of either button) starts the re-catch, RecatchSeconds back to the straps from
 * wherever the removal had got to. A touchdown before the re-catch is done is graded by
 * GetCatchAtTouchdown: the last RecatchGraceSeconds of the re-catch are CaughtLate (sketchy), any
 * earlier NotCaught (a crash). On the water nothing starts and a board still off goes back to the
 * feet on its own (the landing has already been graded).
 *
 * The flight's board-off, for the record: the variant credited (a tic tac short of TicTacMinDeg or a
 * pass short of BoardPassMinPhase is Plain) once the board has been held MinOffSeconds, and the
 * seconds it was held. A second board-off in the same flight adds its seconds and its variant
 * replaces the first's. A new flight clears both.
 */
class KITESURF_API FBoardOffState
{
public:
	enum class EPhase : uint8 { Attached, Removing, Held, Catching };

	FBoardOffState() = default;
	explicit FBoardOffState(const FBoardOffTuning& InTuning) : Tuning(InTuning) {}

	/** One fixed step of Dt seconds. */
	void Step(const FBoardOffInput& In, float Dt);

	/** The board on the feet, nothing logged (a reset or a crash). */
	void Reset();

	EPhase GetPhase() const { return Phase; }
	/** The board is not in the straps: coming off, held or being caught. */
	bool IsBoardOff() const { return Phase != EPhase::Attached; }
	bool IsHeld() const { return Phase == EPhase::Held; }
	bool IsCatching() const { return Phase == EPhase::Catching; }
	/** The board went back into the straps on the last step (for a haptic). */
	bool DidCatchThisStep() const { return bCaughtThisStep; }

	/** The variant: provisional while the board comes off (from the stick so far), latched once held. None when attached. */
	ETrickBoardOff GetVariant() const { return Phase == EPhase::Attached ? ETrickBoardOff::None : Variant; }

	/** How far the board is from the feet to the hands, 0..1, linear in time. */
	float GetOffAlpha() const { return Off; }
	/** The same eased (smoothstep), for drawing. */
	float GetOffWeight() const;
	/** The eased weight before the last step, for drawing between steps. */
	float GetPrevOffWeight() const;
	/** How long the board has been held in this board-off (s), from the step it reached the hands. */
	float GetHeldSeconds() const { return HeldSeconds; }
	/** The tic tac's turn about the board's long axis so far (deg, 0..360), eased; 0 for the other variants. */
	float GetTicTacDeg() const { return TicTacDeg; }
	float GetPrevTicTacDeg() const { return PrevTicTacDeg; }
	/** How far round the back the board pass has gone (0..1), eased; 0 for the other variants. */
	float GetPassPhase() const { return PassPhase; }
	float GetPrevPassPhase() const { return PrevPassPhase; }
	/** While catching, how long until the board is back in the straps (s); 0 otherwise. */
	float GetCatchSecondsLeft() const;

	/** What a landing now would find: Attached, CaughtLate (the last RecatchGraceSeconds of the re-catch) or NotCaught. */
	EBoardCatchState GetCatchAtTouchdown() const;

	/** The tuck the held board asks of the body, 0..1: the variant's tuck times the eased off weight. */
	float GetTuckTarget() const;

	/** This flight's board-off for the record: None until a board has been held MinOffSeconds. */
	ETrickBoardOff GetFlightBoardOff() const { return FlightVariant; }
	/** How long the board was held off in this flight (s). */
	float GetFlightBoardOffSeconds() const { return FlightSeconds; }

	/** The variant for a stick (rotation axes): inside Deadzone Plain; otherwise up Superman, down TicTac, sideways BoardPass. */
	static ETrickBoardOff ResolveVariant(const FVector2D& Stick, float Deadzone);

	/** The variant as credited: a tic tac short of TicTacMinDeg or a pass short of BoardPassMinPhase is Plain. */
	static ETrickBoardOff CreditedVariant(ETrickBoardOff Variant, float InTicTacDeg, float InPassPhase, const FBoardOffTuning& InTuning);

	FBoardOffTuning Tuning;

private:
	void StartRemoving(const FVector2D& Stick);

	EPhase Phase = EPhase::Attached;
	ETrickBoardOff Variant = ETrickBoardOff::Plain;
	FVector2D StickPeak = FVector2D::ZeroVector;
	/** 0 on the feet .. 1 in the hands, linear in time. */
	float Off = 0.0f;
	float PrevOff = 0.0f;
	float HeldSeconds = 0.0f;
	float TicTacDeg = 0.0f;
	float PrevTicTacDeg = 0.0f;
	float PassPhase = 0.0f;
	float PrevPassPhase = 0.0f;
	bool bCaughtThisStep = false;
	bool bWasAirborne = false;

	ETrickBoardOff FlightVariant = ETrickBoardOff::None;
	float FlightSeconds = 0.0f;
};

/**
 * Where the held board, the hands and the feet go in a board-off (T2.3), in the rider frame: origin
 * at the pelvis, axes the body's (X Front, Y Right, Z Up, Tricks/RiderAxes.h), cm. Pure, so the
 * tests check the grips are in the arms' reach without a world.
 *
 * - Plain: the board in front of the hips, deck up and towards the chest, both hands on the toe
 *   rail (ToeRailFront, ToeRailBack), the torso folded a little and the knees tucked.
 * - Superman: the board out in front, deck towards the rider, held by the back hand at the tail end
 *   of the toe rail (ToeRailBack, on top), the front hand on the bar, the legs stretched down and
 *   back behind it.
 * - TicTac: the board on the back hand's side, nose forwards, held by the back hand on the toe edge
 *   (on top) and turned TicTacDeg about its long axis through the hand; the front hand on the bar.
 * - BoardPass: the board stood on its tail and carried round the waist at PassPhase (0 in front, 0.25
 *   on the back hand's side, 0.5 behind the back, 0.75 on the front hand's side, 1 in front again)
 *   by its handle: the back hand to 0.5, the front hand from 0.5, both at the hand-over.
 */
struct FBoardOffPose
{
	/** The held board in the rider frame. */
	FTransform BoardInRider = FTransform::Identity;
	/** Per hand (index ETrickHand: 0 front, 1 back): 0 on the bar .. 1 on its grip. */
	float HandOnBoard[2] = { 0.0f, 0.0f };
	/** Per hand: its grip on the board (board-local, cm). */
	FVector GripLocal[2] = { FVector::ZeroVector, FVector::ZeroVector };
	/** Per hand: 0 the elbow out to the side, 1 out and back (the hand behind the back). */
	float HandBehind[2] = { 0.0f, 0.0f };
	/** The fold at the hips (deg, + chest down towards Front), as FRiderRigInput::TorsoPitchDeg. */
	float TorsoPitchDeg = 0.0f;
	/** The ankles, out of the straps, per rig side (0 left, 1 right), in the rider frame. */
	FVector AnkleInRider[2] = { FVector::ZeroVector, FVector::ZeroVector };
};

namespace BoardOffPose
{
	/** The pose for a variant. NoseSideSign +1 when the board's nose is on the rider's right (the front hand's side), as BoardGrabPoints. */
	KITESURF_API FBoardOffPose Evaluate(ETrickBoardOff Variant, float TicTacDeg, float PassPhase, float NoseSideSign);

	/** The rider frame in the world: at the pelvis, turned as the body. */
	KITESURF_API FTransform RiderFrame(const FVector& Pelvis, const FQuat& Body);

	/** The drawn board between the strapped board (Weight 0) and the held one (1): location lerped, rotation slerped. */
	KITESURF_API FTransform BlendBoard(const FTransform& Strapped, const FTransform& Held, float Weight);
}
