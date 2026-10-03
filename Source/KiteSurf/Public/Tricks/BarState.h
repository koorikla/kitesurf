#pragma once

#include "CoreMinimal.h"
#include "Tricks/TrickSignature.h"
#include "Tricks/TrickTypes.h"
#include "BarState.generated.h"

/**
 * The bar and handle-pass state machine (docs/tricks.md 6.4, docs/tricks/T3.md section 2). Pure:
 * plain structs and free functions, no UObject. AKiteRiderPawn::StepBar (T3.1 PR 2) calls
 * BarStateMachine::Step once per fixed step, after the kite and before the rider attitude.
 *
 * The wrap model. W (FBarState::WrapDeg) is how far the lines have gone round the body, relative
 * to the hips, positive backside. It is geometric: the line's azimuth in the body's Front/Right
 * plane, unwrapped from step to step. Backside spins raise W, frontside spins lower it.
 * - |W| < 90: bar in front, heelside.
 * - W reaches +90 by a backside turn: the bar goes behind the back (bRouteBehind). At +180 that
 *   is riding blind.
 * - W reaches -90 by a frontside turn: the lines run round the front and the bar stays in front.
 *   At -180 that is toeside.
 * - A handle pass subtracts 360 x its sense from W: the bar is at the same place behind the back,
 *   but the lines now run round the other side. A backside 360 with one pass goes 0, +180, pass to
 *   -180, then back to 0 (heelside).
 * - Landing with |W| at FBarTunables::WrappedLandDeg (270) or more is wrapped: the bar is lost.
 *   So a 360 needs one pass, a 720 two and a 1080 three; odd half turns land blind or toeside.
 * - W is held while the line runs along the body (|Up . d| over WrapHoldUpDot), so rolls round
 *   the lines and overhead S-bend turns do not wrap; when the line drops back the shortest step
 *   from the last azimuth is taken.
 *
 * Backside is the sense of a back roll: the chest turns towards the tail first (docs/tricks.md
 * section 2), so it depends on which side of the rider the board's nose is
 * (FBarInputs::NoseSideSign, the same sign as RiderAttitudeComponent's NoseSideSign).
 *
 * Tension is compared in body weights (TensionN / BodyWeightN). Every timer is in seconds of
 * simulation, so a fixed step of any size gives the same result to within one step.
 */

/** Which hands hold the bar. */
UENUM(BlueprintType)
enum class EBarHands : uint8
{
	Both      UMETA(DisplayName = "Both"),
	FrontOnly UMETA(DisplayName = "Front hand only"),
	BackOnly  UMETA(DisplayName = "Back hand only"),
	/** Between the hands, during a pass, or lost. */
	None      UMETA(DisplayName = "None")
};

/** Where the bar is. */
UENUM(BlueprintType)
enum class EBarPlace : uint8
{
	/** In front: heelside, toeside (lines round the front), or hooked. */
	Front      UMETA(DisplayName = "Front"),
	/** Behind the back with both hands: blind, or just after a pass. */
	BehindBack UMETA(DisplayName = "Behind the back"),
	/** Going from one hand to the other behind the back. */
	Passing    UMETA(DisplayName = "Passing"),
	/** Out of the hands, on the leash. Only a reset brings it back. */
	Lost       UMETA(DisplayName = "Lost")
};

/** Why the bar was lost. */
UENUM(BlueprintType)
enum class EBarLossCause : uint8
{
	None           UMETA(DisplayName = "None"),
	/** Unhooked, the line pulled harder than the grip limit for too long. */
	OverGrip       UMETA(DisplayName = "Over grip"),
	/** The lines loaded up while the bar was between the hands. */
	PassUnderLoad  UMETA(DisplayName = "Pass under load"),
	/** The pass was still between the hands too long after touchdown. */
	PassUnfinished UMETA(DisplayName = "Pass not finished"),
	/** Touched down with the lines wrapped round the body (a turn too many for the passes). */
	LinesWrapped   UMETA(DisplayName = "Lines wrapped")
};

/** Limits for BarStateMachine::Step. Every value is an estimate (docs/tricks.md 6.4 and 8). */
USTRUCT(BlueprintType)
struct FBarTunables
{
	GENERATED_BODY()

	/** Unhooked, tension over this many body weights... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Bar")
	float GripLimitBW = 1.3f;

	/** ...for longer than this pulls the bar from the hands (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Bar")
	float GripLimitSeconds = 0.15f;

	/** A pass starts only with the tension under this (body weights). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Bar")
	float PassSlackTensionBW = 0.3f;

	/** Tension over this while the bar is between the hands loses it (body weights). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Bar")
	float PassLoseTensionBW = 0.6f;

	/** How long the bar is between the hands (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Bar")
	float PassDurationSeconds = 0.25f;

	/** A pass starts only with the back turned to the kite within this (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Bar")
	float PassBackToKiteDeg = 60.0f;

	/** A pass may run on for this long on the water and still count, as a surface pass (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Bar")
	float SurfacePassGraceSeconds = 0.3f;

	/**
	 * On the water a pass starts with the tension under this instead of PassSlackTensionBW: the surface
	 * pass from riding blind (T3.5), with the feet on the board taking the rest of the pull (body weights).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Bar")
	float SurfacePassMaxTensionBW = 0.6f;

	/** A pass started on the water takes this long, instead of PassDurationSeconds (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Bar")
	float SurfacePassSeconds = 0.4f;

	/** Tension over this while a pass started on the water is between the hands loses it, instead of PassLoseTensionBW (body weights). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Bar")
	float SurfacePassLoseTensionBW = 0.9f;

	/** A pass press waits this long for slack and the back to the kite (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Bar")
	float PassRequestBufferSeconds = 0.2f;

	/** The wrap is held while |body Up . line direction| is over this (the line runs along the body). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Bar")
	float WrapHoldUpDot = 0.9f;

	/** On the water with |W| at this or more the lines are wrapped and the bar is lost (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Bar")
	float WrappedLandDeg = 270.0f;
};

/** What the bar machine reads each step. World directions; tension in N. */
USTRUCT(BlueprintType)
struct FBarInputs
{
	GENERATED_BODY()

	/** Line tension at the bar (N). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bar")
	float TensionN = 0.0f;

	/** The rider's weight (N); tension limits are in multiples of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bar")
	float BodyWeightN = 834.0f;

	/** Unit direction from the rider to the kite (world). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bar")
	FVector LineDirWorld = FVector::ForwardVector;

	/** The rider's body orientation: X Front, Y Right, Z Up (Tricks/RiderAxes.h). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bar")
	FQuat Body = FQuat::Identity;

	/** The board's nose is along NoseSideSign x Right (-RiderStanceSide); sets which way is backside. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bar")
	float NoseSideSign = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bar")
	bool bAirborne = false;

	/** On the water and riding (not crashed): hooking in or out is allowed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bar")
	bool bOnWaterRideable = true;

	/** The hook button went down this step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bar")
	bool bHookPressed = false;

	/** The pass button went down this step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bar")
	bool bPassPressed = false;

	/** Held: the front hand is off the bar (a grab, or a hand-off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bar")
	bool bReleaseFront = false;

	/** Held: the back hand is off the bar (a grab, or the tantrum's hand-off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bar")
	bool bReleaseBack = false;
};

/** What happened in one step. */
USTRUCT(BlueprintType)
struct FBarEvents
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	bool bUnhooked = false;

	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	bool bHooked = false;

	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	bool bPassStarted = false;

	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	bool bPassDone = false;

	/** Where the pass that finished this step finished; None when none did. */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	ETrickPassKind PassKind = ETrickPassKind::None;

	/** Set on the step the bar is lost. */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	EBarLossCause Lost = EBarLossCause::None;
};

/** One finished pass in the current jump. */
USTRUCT(BlueprintType)
struct FBarPassRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	ETrickSense Sense = ETrickSense::Backside;

	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	ETrickPassKind Kind = ETrickPassKind::Air;
};

/** The bar's state; starts hooked in with both hands in front. */
USTRUCT(BlueprintType)
struct FBarState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	bool bHooked = true;

	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	EBarHands Hands = EBarHands::Both;

	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	EBarPlace Place = EBarPlace::Front;

	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	EBarLossCause LossCause = EBarLossCause::None;

	/** The wrap W (deg, + backside); 0 while hooked. */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	float WrapDeg = 0.0f;

	/** The lines run round the back: W reached +90 by a backside turn, or a pass. Cleared under |W| 90. */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	bool bRouteBehind = false;

	/** Progress of the pass, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	float PassT = 0.0f;

	/** +1 backside, -1 frontside: the sign of W when the pass started. */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	float PassSense = 0.0f;

	/** How long the current pass has been on the water (s). */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	float PassWaterSeconds = 0.0f;

	/**
	 * The pass under way started on the water (the surface pass from riding blind, T3.5): it takes
	 * SurfacePassSeconds, is lost over SurfacePassLoseTensionBW, and the grace does not apply.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	bool bPassFromWater = false;

	/** The pass under way joins the jump's passes when it is done: started in the air, or on the water within the grace after the touchdown. */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	bool bPassJoinsJump = true;

	/**
	 * Time left after the last touchdown in which a pass started on the water still joins that jump
	 * (s): SurfacePassGraceSeconds in the air, counting down on the water. 0 before any jump.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	float SurfaceGraceLeftSeconds = 0.0f;

	/** How long the tension has been under PassSlackTensionBW (s); for the HUD's slack meter. */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	float SlackSeconds = 0.0f;

	/** How long the tension has been over GripLimitBW (s). */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	float OverGripSeconds = 0.0f;

	/** Time left on a buffered pass press (s). */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	float PassBufferLeft = 0.0f;

	/** The line azimuth the wrap was last measured at (deg). */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	float LastLineAngleDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	bool bHasLineAngle = false;

	/** Airborne on the last step: a change is a take-off or a touchdown. */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	bool bWasAirborne = false;

	/** W at the last take-off (deg). */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	float WrapAtTakeoffDeg = 0.0f;

	/** Passes finished since the last take-off, in order (surface passes after the touchdown included). */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	TArray<FBarPassRecord> JumpPasses;
};

/** A jump's bar history in FTrickSignature terms (BarStateMachine::SummariseJump). */
USTRUCT(BlueprintType)
struct FBarJumpSummary
{
	GENERATED_BODY()

	/** One entry per pass; the rotation is split 360 to each pass but the last, which takes the rest. */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	TArray<FTrickPass> Passes;

	/** The rotation that carried the passes: 360 x sum of pass senses + (W at landing - W at take-off) (deg, + backside). */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	float PassSpinDeg = 0.0f;

	/** |PassSpinDeg| in half turns. */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	int32 PassHalfTurns = 0;

	/** W at landing minus W at take-off (deg): with no pass, the body spin against the lines. */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	float WrapChangeDeg = 0.0f;

	/** From W at landing: under 90 heelside; otherwise blind on the back route, toeside on the front. */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	ETrickStance LandingStance = ETrickStance::Heelside;

	/** |W| at or over WrappedLandDeg, or the bar was lost for wrapped lines. */
	UPROPERTY(BlueprintReadOnly, Category = "Bar")
	bool bWrapped = false;
};

namespace BarStateMachine
{
	/** The line's azimuth in the body's Front/Right plane (deg, -180..180): 0 in front, +90 on the right. */
	KITESURF_API float LineAzimuthDeg(const FQuat& Body, const FVector& LineDir);

	/**
	 * How far the back is from facing the kite, in the body's Front/Right plane (deg, 0..180):
	 * 0 with the back square to the kite, 180 facing it. 90 when the line runs along Up.
	 */
	KITESURF_API float BackToKiteDeg(const FQuat& Body, const FVector& LineDir);

	/**
	 * One fixed step. In order: take-off and touchdown bookkeeping; nothing more once Lost; the
	 * hook toggle (on the water, bar in front with both hands, |W| under 90); hooked, the wrap is 0
	 * and nothing else applies. Unhooked: the grip limit; the wrap and the bar's place; a buffered
	 * pass press starts a pass with slack and the back to the kite; a pass finishes after
	 * PassDurationSeconds (air or surface) or is lost under load or on the water past the grace; a pass
	 * started on the water (T3.5, the surface pass from riding blind) starts under
	 * SurfacePassMaxTensionBW, takes SurfacePassSeconds, is lost over SurfacePassLoseTensionBW, and
	 * joins the jump's passes only if it started within SurfacePassGraceSeconds of the touchdown;
	 * wrapped lines on the water lose the bar; single-hand releases while the bar is in front.
	 */
	KITESURF_API FBarEvents Step(FBarState& State, const FBarInputs& Inputs, const FBarTunables& Tunables, float Dt);

	/** The wrap the bar will settle at: W, less 360 x sense while a pass is under way. */
	KITESURF_API float EffectiveWrapDeg(const FBarState& State);

	/** Not lost and the effective wrap under WrappedLandDeg: a touchdown now can be ridden away. */
	KITESURF_API bool CanLandRideable(const FBarState& State, const FBarTunables& Tunables = FBarTunables());

	/**
	 * A surface pass may still join the jump just landed (T3.5): unhooked with the bar, and either a
	 * pass started on the water within the grace is under way, or the lines are round the back with no
	 * pass made (W at +90 or more, back to blind) and the grace has not run out. The jump recorder waits
	 * for it before it finalises such a landing.
	 */
	KITESURF_API bool MaySurfacePassJoinJump(const FBarState& State);

	/** The stance a wrap gives: under 90 (folded) heelside; else blind on the back route, toeside on the front. */
	KITESURF_API ETrickStance StanceForWrap(float WrapDeg, bool bRouteBehind);

	/** The jump since the last take-off, read once it has settled (after the touchdown, no pass under way). */
	KITESURF_API FBarJumpSummary SummariseJump(const FBarState& State, const FBarTunables& Tunables = FBarTunables());

	/**
	 * Writes the bar's part of a signature: bHooked and Passes, and, unhooked, the landing stance.
	 * Spin, inversions and the take-off stance are left to the attitude tracker.
	 */
	KITESURF_API void ApplyToSignature(const FBarState& State, FTrickSignature& Signature, const FBarTunables& Tunables = FBarTunables());

	/** ApplyToSignature from a summary already taken (a jump record's bar fields): bHooked, and unhooked the passes and the landing stance. */
	KITESURF_API void ApplySummaryToSignature(bool bHooked, const FBarJumpSummary& Summary, FTrickSignature& Signature);

	/** The landing cause for a bar loss: PassUnfinished for an unfinished pass, BarLost otherwise, None for none. */
	KITESURF_API ELandingCause LandingCauseOf(EBarLossCause Cause);
}
