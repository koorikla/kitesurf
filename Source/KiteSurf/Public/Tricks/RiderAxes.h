#pragma once

#include "CoreMinimal.h"

/**
 * The rider's body frame and the rotation axes of tricks (docs/tricks.md section 2; T1 plan
 * section 3). Pure inline maths, no state.
 *
 * Body frame (the same as FRotationMatrix::MakeFromXZ(Front, Up)):
 * - X = Front, where the chest faces;
 * - Y = Right = Up x Front (UE FVector::CrossProduct(Up, Front));
 * - Z = Up, feet to head.
 * Side (hip to hip, towards the board's nose) is -StanceSide * Right; the dynamics do not need it.
 *
 * Signs: FQuat(Axis, +Angle) and dv/dt = Omega x v agree with FVector::CrossProduct. Use those two
 * everywhere.
 *
 * Take-off frame: U = world up, T = horizontal velocity direction, S = U x T.
 * Sigma = sign(Right . T) at load start or take-off (TravelSide below); a back roll turns the chest
 * towards -T (the tail) first.
 */
namespace RiderAxes
{
	/**
	 * Which way the cartwheel part of a back roll tips the head (kappa in the T1 plan). With -1 the
	 * head goes towards the tail side and points backwards (-Front) at the half turn. Open until a
	 * reference clip pins it down; tests assert only "chest to the tail first" and inversion
	 * counts, so flipping it does not break them.
	 */
	constexpr float RollInversionSign = -1.0f;

	/** Below this |Right . T| the travel direction cannot tell the sides apart and the board's nose decides. */
	constexpr float TravelSideMinDot = 0.2f;

	/** The kind of rotation a stick direction asks for; picks the full rate (spin, roll or flip). */
	enum class ERotationFamily : uint8
	{
		None,
		Spin,
		Roll,
		Flip
	};

	/**
	 * Back roll axis in body coordinates for travel side Sigma, tilted TiltDeg from Up towards
	 * Front: -Sigma * (Kappa sin(Tilt), 0, cos(Tilt)). The spin part (about Up) turns the chest
	 * towards the tail; the cartwheel part (about Front) inverts the rider. Front roll = minus this.
	 */
	inline FVector BackRollAxisBody(float Sigma, float TiltDeg)
	{
		const float B = FMath::DegreesToRadians(TiltDeg);
		const float S = Sigma >= 0.0f ? 1.0f : -1.0f;
		return (-S * FVector(RollInversionSign * FMath::Sin(B), 0.0f, FMath::Cos(B))).GetSafeNormal();
	}

	/** Backflip axis in body coordinates: -Right tips the head backwards (rotating about +Right tips it forwards). */
	inline FVector BackFlipAxisBody() { return FVector(0.0f, -1.0f, 0.0f); }

	/** Front flip axis in body coordinates. */
	inline FVector FrontFlipAxisBody() { return FVector(0.0f, 1.0f, 0.0f); }

	/**
	 * Sigma, the side the rider travels towards: sign(Right . T) with T the horizontal velocity
	 * direction. When |Right . T| < TravelSideMinDot (or the rider is barely moving), the board's
	 * nose stands in for T. Returns +1 or -1, never 0.
	 */
	inline float TravelSide(const FQuat& Body, const FVector& Velocity, const FVector& BoardFwd)
	{
		const FVector Right = Body.GetAxisY();
		const FVector T = FVector(Velocity.X, Velocity.Y, 0.0f).GetSafeNormal();
		float D = Right | T;
		if (T.IsZero() || FMath::Abs(D) < TravelSideMinDot)
		{
			const FVector Nose = FVector(BoardFwd.X, BoardFwd.Y, 0.0f).GetSafeNormal();
			if (!Nose.IsZero())
			{
				D = Right | Nose;
			}
		}
		return D >= 0.0f ? 1.0f : -1.0f;
	}

	/** What a rotation stick asks for: a unit axis in body coordinates, its family, and how hard (0..1). */
	struct FRotationAxisChoice
	{
		FVector AxisBody = FVector::ZeroVector;
		ERotationFamily Family = ERotationFamily::None;
		/** Roll axis tilt from Up (deg); 90 for a flip, 0 when there is no axis. */
		float TiltDeg = 0.0f;
		/** |Stick|, capped at 1. */
		float Magnitude = 0.0f;
	};

	/**
	 * The T1.4 stick mapping, after the pawn's side mapping. Stick.X: +1 back roll, -1 front roll.
	 * Stick.Y: +1 up, -1 down (pulled).
	 * - Within FlipSectorDeg of vertical: a flip, pulled is a backflip. Leaving the flip sector
	 *   takes HysteresisDeg more (bWasFlip).
	 * - Otherwise a roll about BackRollAxisBody(Sigma, Tilt) times sign(X), with
	 *   Tilt = DefaultTiltDeg - TiltRangeDeg * Y / |Stick|: down tilts towards inverted, up towards
	 *   a flat spin. A tilt under SpinTiltMaxDeg counts as a spin for its rate.
	 */
	inline FRotationAxisChoice ChooseAxisBody(const FVector2D& Stick, float Sigma, float DefaultTiltDeg,
		float TiltRangeDeg, float FlipSectorDeg, float SpinTiltMaxDeg, float HysteresisDeg, bool bWasFlip)
	{
		FRotationAxisChoice Choice;
		const float Size = Stick.Size();
		if (Size <= UE_KINDA_SMALL_NUMBER)
		{
			return Choice;
		}
		Choice.Magnitude = FMath::Min(Size, 1.0f);

		const float PhiDeg = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(Stick.Y), FMath::Abs(Stick.X)));
		const float FlipFromDeg = 90.0f - FlipSectorDeg - (bWasFlip ? HysteresisDeg : 0.0f);
		if (PhiDeg > FlipFromDeg)
		{
			Choice.AxisBody = Stick.Y < 0.0f ? BackFlipAxisBody() : FrontFlipAxisBody();
			Choice.Family = ERotationFamily::Flip;
			Choice.TiltDeg = 90.0f;
			return Choice;
		}

		Choice.TiltDeg = DefaultTiltDeg - TiltRangeDeg * (Stick.Y / Size);
		Choice.AxisBody = (Stick.X >= 0.0f ? 1.0f : -1.0f) * BackRollAxisBody(Sigma, Choice.TiltDeg);
		Choice.Family = Choice.TiltDeg < SpinTiltMaxDeg ? ERotationFamily::Spin : ERotationFamily::Roll;
		return Choice;
	}
}
