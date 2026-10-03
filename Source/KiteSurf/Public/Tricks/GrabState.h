#pragma once

#include "CoreMinimal.h"
#include "Tricks/TrickSignature.h"
#include "Tricks/TrickTypes.h"

/** Tunables of FGrabState (docs/tricks/T2.md T2.1 and T2.2). Every value is an estimate. */
struct FGrabStateTuning
{
	/** Time for a hand to go from the bar to its board socket (s): the hold starts when it gets there. */
	float ReachSeconds = 0.20f;
	/** Time for a hand to go back from the board to the bar (s). */
	float ReturnSeconds = 0.15f;
	/** A grab is named and scored only when held at least this long after the hand reached the board (s). Mirrors FTrickScoringSettings::GrabMinHoldSeconds. */
	float MinHoldSeconds = 0.30f;
	/** The zone stick picks a zone only past this; inside it the hand takes its default zone (the toe edge). */
	float StickDeadzone = 0.5f;
	/** Time for the back foot to come fully out of its strap (s). */
	float FootOutSeconds = 0.12f;
	/** Time for the back foot to go back into its strap (s): let go at least this long before touchdown. */
	float FootReturnSeconds = 0.15f;
	/** The back foot must be fully out at least this long for the jump to count as a one-footer (s). */
	float MinOneFootSeconds = 0.30f;
	/** Foot out (0 in .. 1 out) at or above this at contact is EFootStrapState::Out (a crash); between 0 and it, Returning (sketchy). */
	float FootOutAtTouchdown = 0.5f;
};

/** What FGrabState reads each fixed step. */
struct FGrabStateInput
{
	/** The front hand's grab button (LB, Q) is held. */
	bool bFront = false;
	/** The back hand's grab button (RB, E) is held. */
	bool bBack = false;
	/** The one-footer button (L3, C) is held. */
	bool bOneFoot = false;
	/** The board is in the air at the start of the step. On the water the buttons do nothing. */
	bool bAirborne = false;
	/**
	 * The zone stick, in the air rotation's axes (the pawn's AirRotationStick convention): X +1 towards
	 * the side of the screen the rider's back is on (the heel edge), -1 towards the chest (the toe
	 * edge); Y +1 up (the nose), -1 down (the tail).
	 */
	FVector2D ZoneStick = FVector2D::ZeroVector;
};

/**
 * Grabs and the one-footer (T2.1, T2.2): which hand is on the board, where, for how long, and where
 * the back foot is. Pure: no UObject, no world. AKiteRiderPawn owns one and steps it in its fixed
 * step before the rider attitude and the board; the tracker reads GetGrabs, the attitude the tuck
 * target, the board the foot at touchdown, and the drawn rider the reach weight and the foot.
 *
 * One hand grabs at a time. In the air a fresh press of a grab button, with the other one not held,
 * sends that hand to the board: it reaches for ReachSeconds, sampling the zone stick (the strongest
 * push during the reach picks the zone), and when it gets there the zone is latched and the hold
 * starts. Letting go ends the grab (logged with its hold) and the hand goes back to the bar over
 * ReturnSeconds. Both buttons together are kept for the board-off (T2.3): a press while the other
 * button is held does nothing, and pressing both at once starts nothing. On the water nothing
 * starts, and a hand on the board goes back to the bar (a landing ends the grab).
 *
 * The one-footer: in the air, held, the back foot comes out of its strap over FootOutSeconds and goes
 * back over FootReturnSeconds when let go (and on the water). GetFootAtTouchdown is what a landing
 * would grade now.
 *
 * A new flight (the first airborne step after the water) clears the grab log and the one-foot time.
 */
class KITESURF_API FGrabState
{
public:
	FGrabState() = default;
	explicit FGrabState(const FGrabStateTuning& InTuning) : Tuning(InTuning) {}

	/** One fixed step of Dt seconds. */
	void Step(const FGrabStateInput& In, float Dt);

	/** Hands on the bar, both feet in, nothing logged (a reset or a crash). */
	void Reset();

	/** A hand is off the bar: reaching, holding or going back. */
	bool IsHandOffBar() const { return Phase != EPhase::OnBar; }
	/** The hand is on its socket and the hold is counting. */
	bool IsHolding() const { return Phase == EPhase::Holding; }
	/** The hand is on its way to the board. */
	bool IsReaching() const { return Phase == EPhase::Reaching; }
	/** A grab button is held in the air: the left stick picks the zone and does not rotate the rider (the rotation keeps its momentum). */
	bool IsZoneStickActive() const { return bZoneStickActive; }
	/** The hand that is (or was last) off the bar. */
	ETrickHand GetHand() const { return Hand; }
	/** Its zone: provisional while reaching (from the stick so far), latched once it holds. */
	ETrickGrabZone GetZone() const { return Zone; }
	/** How far the hand is from the bar to the socket, 0..1, eased (smoothstep of the reach): exactly 1 while holding. */
	float GetReachWeight() const;
	/** The reach weight before the last step, for drawing between steps. */
	float GetPrevReachWeight() const;
	/** The hold so far (s), counted from the step the hand reached the socket. */
	float GetHoldSeconds() const { return HoldSeconds; }
	/** The hand reached its socket on the last step (for a haptic). */
	bool DidHandArriveThisStep() const { return bArrivedThisStep; }

	/** The tuck the grab asks of the body, 0..1: the zone's tuck times the reach weight (TuckForZone). */
	float GetTuckTarget() const;

	/** The back foot, 0 in its strap .. 1 fully out (linear in time). */
	float GetFootOut() const { return FootOut; }
	float GetPrevFootOut() const { return PrevFootOut; }
	/** What a landing now would find: In, Returning (out less than FootOutAtTouchdown) or Out. */
	EFootStrapState GetFootAtTouchdown() const;
	/** How long the back foot has been fully out in this flight (s). */
	float GetOneFootSeconds() const { return OneFootSeconds; }
	/** Out long enough this flight to count as a one-footer (MinOneFootSeconds). */
	bool IsOneFooter() const { return OneFootSeconds >= Tuning.MinOneFootSeconds; }

	/** This flight's grabs, in the order the hands reached the board; the one being held is last, with its hold so far. */
	const TArray<FTrickGrab>& GetGrabs() const { return Grabs; }

	/**
	 * The zone for a zone stick (rotation axes, see FGrabStateInput::ZoneStick): inside Deadzone the
	 * toe edge (Mute for the front hand, Indy for the back); otherwise the stick's main direction, up
	 * the nose, down the tail, towards the chest the toe edge, towards the back the heel edge.
	 */
	static ETrickGrabZone ResolveZone(const FVector2D& ZoneStick, float Deadzone);

	/** How much a grab in this zone tucks the body, 0..1 (docs/tricks/T2.md RiderPoses table): the edges 1, the nose and tail 0.8. */
	static float TuckForZone(ETrickGrabZone Zone);

	/** How far the torso folds forwards at the hips to reach this zone (deg): the toe edge most, the heel edge least. */
	static float TorsoFoldDegForZone(ETrickGrabZone Zone);

	FGrabStateTuning Tuning;

private:
	enum class EPhase : uint8 { OnBar, Reaching, Holding, Returning };

	void StartReach(ETrickHand InHand, const FVector2D& Stick);
	/** Logs the grab in progress (a release or a landing) and sends the hand back. */
	void EndGrab();

	EPhase Phase = EPhase::OnBar;
	ETrickHand Hand = ETrickHand::Back;
	ETrickGrabZone Zone = ETrickGrabZone::ToeEdge;
	FVector2D StickPeak = FVector2D::ZeroVector;
	/** 0 on the bar .. 1 on the socket, linear in time. */
	float Reach = 0.0f;
	float PrevReach = 0.0f;
	float HoldSeconds = 0.0f;
	/** Index in Grabs of the grab being held, or INDEX_NONE. */
	int32 OpenGrab = INDEX_NONE;
	bool bArrivedThisStep = false;
	bool bZoneStickActive = false;

	float FootOut = 0.0f;
	float PrevFootOut = 0.0f;
	float OneFootSeconds = 0.0f;

	bool bWasAirborne = false;
	bool bFrontWasHeld = false;
	bool bBackWasHeld = false;

	TArray<FTrickGrab> Grabs;
};
