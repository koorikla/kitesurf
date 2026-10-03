#include "Tricks/RotationRecognizer.h"
#include "Tricks/RiderAxes.h"

namespace RotationRecognizerLocal
{
	FVector Horizontal(const FVector& V)
	{
		return FVector(V.X, V.Y, 0.0f).GetSafeNormal();
	}

	/** Wraps an angle in degrees to -180..180. */
	float WrapDeg(float Deg)
	{
		return FMath::UnwindDegrees(Deg);
	}
}

FRotationTakeoffFrame FRotationTakeoffFrame::Make(const FQuat& Body, const FVector& Velocity, const FVector& BoardForward)
{
	using namespace RotationRecognizerLocal;
	FRotationTakeoffFrame Frame;
	Frame.U = FVector::UpVector;
	FVector T = Horizontal(Velocity);
	if (T.IsZero())
	{
		T = Horizontal(BoardForward);
	}
	if (T.IsZero())
	{
		T = Horizontal(Body.GetAxisX());
	}
	Frame.T = T.IsZero() ? FVector::ForwardVector : T;
	Frame.S = FVector::CrossProduct(Frame.U, Frame.T);
	Frame.Sigma = RiderAxes::TravelSide(Body, Velocity, BoardForward);
	return Frame;
}

TArray<ETrickInversion> FRotationResult::InversionKinds() const
{
	TArray<ETrickInversion> Kinds;
	Kinds.Reserve(Inversions.Num());
	for (const FRecognizedInversion& Inversion : Inversions)
	{
		Kinds.Add(Inversion.Kind);
	}
	return Kinds;
}

int32 FRotationRecognizer::SnapHalfTurns(float SpinDeg)
{
	if (!FMath::IsFinite(SpinDeg))
	{
		return 0;
	}
	return FMath::Max(0, FMath::FloorToInt((FMath::Abs(SpinDeg) + 45.0f) / 180.0f));
}

void FRotationRecognizer::SwingTwist(const FQuat& Q, const FVector& Axis, FQuat& OutSwing, FQuat& OutTwist)
{
	const FVector V(Q.X, Q.Y, Q.Z);
	const FVector P = (V | Axis) * Axis;
	FQuat Twist(P.X, P.Y, P.Z, Q.W);
	const double Size = Twist.Size();
	if (Size < 1e-6)
	{
		OutTwist = FQuat::Identity;
	}
	else
	{
		OutTwist = Twist * (1.0 / Size);
	}
	OutSwing = Q * OutTwist.Inverse();
}

void FRotationRecognizer::Begin(const FRotationTakeoffFrame& InFrame, const FQuat& Body)
{
	bBegun = true;
	Frame = InFrame;
	Body0 = Body.GetNormalized();
	LastBody = Body0;
	AboutURad = 0.0;
	AboutTRad = 0.0;
	AboutSRad = 0.0;
	BodyAxisRad = FVector::ZeroVector;
	BodyAxisSinceArm = FVector::ZeroVector;
	// Taking off already well tilted does not arm an inversion: the rider has to be upright first.
	bArmed = (Body0.GetAxisZ() | Frame.U) > Settings.ArmDot;
	TimeSeconds = 0.0f;
	RotatingSinceSeconds = -1.0f;
	FlightTurnRad = 0.0;
	LastTravel = Frame.T;
	StepCount = 0;
	Inversions.Reset();
}

void FRotationRecognizer::Step(const FQuat& Body, const FVector& OmegaW, float Dt, const FVector& Velocity)
{
	if (!bBegun || Dt <= 0.0f || Body.ContainsNaN() || OmegaW.ContainsNaN())
	{
		return;
	}
	const FQuat Q = Body.GetNormalized();
	const float StartSeconds = TimeSeconds;
	TimeSeconds += Dt;
	++StepCount;

	AboutURad += (OmegaW | Frame.U) * Dt;
	AboutTRad += (OmegaW | Frame.T) * Dt;
	AboutSRad += (OmegaW | Frame.S) * Dt;
	const FVector OmegaBody = Q.UnrotateVector(OmegaW);
	BodyAxisRad += OmegaBody * Dt;
	BodyAxisSinceArm += OmegaBody * Dt;

	// The flight's own turn, unwrapped step by step.
	const FVector Travel = RotationRecognizerLocal::Horizontal(Velocity);
	if (!Travel.IsZero())
	{
		if (!LastTravel.IsZero())
		{
			const double Sin = FVector::CrossProduct(LastTravel, Travel) | Frame.U;
			const double Cos = LastTravel | Travel;
			FlightTurnRad += FMath::Atan2(Sin, Cos);
		}
		LastTravel = Travel;
	}

	// The roll start: the beginning of the run of fast rotation that leads into an inversion.
	if (FMath::RadiansToDegrees(OmegaW.Size()) >= Settings.RollStartRateDegS)
	{
		if (RotatingSinceSeconds < 0.0f)
		{
			RotatingSinceSeconds = StartSeconds;
		}
	}
	else
	{
		RotatingSinceSeconds = -1.0f;
	}

	const double UpDot = Q.GetAxisZ() | Frame.U;
	if (bArmed && UpDot < Settings.CountDot)
	{
		FRecognizedInversion Inversion;
		const FVector& A = BodyAxisSinceArm;
		const bool bFlip = FMath::Abs(A.Y) > FMath::Max(FMath::Abs(A.X), FMath::Abs(A.Z));
		if (bFlip)
		{
			Inversion.Kind = A.Y < 0.0 ? ETrickInversion::BackFlip : ETrickInversion::FrontFlip;
		}
		else
		{
			const bool bBack = (A | RiderAxes::BackRollAxisBody(Frame.Sigma, Settings.ReferenceRollTiltDeg)) > 0.0;
			Inversion.Kind = bBack ? ETrickInversion::BackRoll : ETrickInversion::FrontRoll;
		}
		Inversion.CountedSeconds = TimeSeconds;
		Inversion.StartSeconds = RotatingSinceSeconds >= 0.0f ? RotatingSinceSeconds : TimeSeconds;
		Inversions.Add(Inversion);
		bArmed = false;
	}
	else if (!bArmed && UpDot > Settings.ArmDot)
	{
		bArmed = true;
		BodyAxisSinceArm = FVector::ZeroVector;
	}
	LastBody = Q;
}

ETrickSense FRotationRecognizer::SenseOf(double SpinRad) const
{
	if (SpinRad == 0.0)
	{
		return ETrickSense::None;
	}
	// Backside turns as a back roll does: about -Sigma * U.
	return SpinRad * Frame.Sigma < 0.0 ? ETrickSense::Backside : ETrickSense::Frontside;
}

float FRotationRecognizer::NetHeadingDeg(const FQuat& Body, float* OutTiltDeg) const
{
	const FQuat Rel = (Body.GetNormalized() * Body0.Inverse()).GetNormalized();
	FQuat Swing;
	FQuat Twist;
	SwingTwist(Rel, Frame.U, Swing, Twist);
	if (OutTiltDeg)
	{
		*OutTiltDeg = FMath::RadiansToDegrees(static_cast<float>(Swing.GetAngle()));
	}

	float HeadingDeg = 0.0f;
	const double TwistPart = FMath::Sqrt(FMath::Square((FVector(Rel.X, Rel.Y, Rel.Z) | Frame.U)) + FMath::Square(Rel.W));
	if (TwistPart >= Settings.TwistGuard)
	{
		const double Along = FVector(Twist.X, Twist.Y, Twist.Z) | Frame.U;
		HeadingDeg = FMath::RadiansToDegrees(static_cast<float>(2.0 * FMath::Atan2(Along, static_cast<double>(Twist.W))));
	}
	else
	{
		// Near a half turn about a horizontal axis the twist is ill-conditioned: compare the fronts
		// projected on the water (0 when either is vertical).
		const FVector F0 = RotationRecognizerLocal::Horizontal(Body0.GetAxisX());
		const FVector F1 = RotationRecognizerLocal::Horizontal(Body.GetAxisX());
		if (!F0.IsZero() && !F1.IsZero())
		{
			HeadingDeg = FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(FVector::CrossProduct(F0, F1) | Frame.U, F0 | F1)));
		}
	}
	return RotationRecognizerLocal::WrapDeg(HeadingDeg - FMath::RadiansToDegrees(static_cast<float>(FlightTurnRad)));
}

void FRotationRecognizer::FillSpin(FRotationResult& Result, bool bFacingAway, bool bLanded) const
{
	const double SpinRad = AboutURad - FlightTurnRad;
	Result.SpinDeg = FMath::RadiansToDegrees(static_cast<float>(SpinRad));
	Result.FlightTurnDeg = FMath::RadiansToDegrees(static_cast<float>(FlightTurnRad));
	if (Inversions.Num() == 0)
	{
		Result.SpinHalfTurns = SnapHalfTurns(Result.SpinDeg);
		if (bLanded && (Result.SpinHalfTurns % 2 == 1) != bFacingAway)
		{
			// The landing settles it: an odd count lands facing away, an even one facing the kite.
			// Move one half turn towards the integral.
			const float Exact = FMath::Abs(Result.SpinDeg) / 180.0f;
			Result.SpinHalfTurns += Result.SpinHalfTurns > Exact ? -1 : 1;
		}
	}
	else
	{
		// Each roll turns about U by cos(tilt) of its 360 on its own, so the integral cannot tell a
		// back roll from a back roll 180: which way the rider faces does.
		Result.SpinHalfTurns = bFacingAway ? 1 : 0;
	}
	Result.SpinSense = Result.SpinHalfTurns > 0 ? LandingSense() : ETrickSense::None;
}

ETrickSense FRotationRecognizer::LandingSense() const
{
	// After a roll the turn goes on the roll's way: backside is by definition the back roll's sense
	// (back to blind). The integral about U cannot say, since a roll with the body leaning back
	// can carry either sign about U.
	if (Inversions.Num() > 0)
	{
		const ETrickInversion First = Inversions[0].Kind;
		if (First == ETrickInversion::BackRoll)
		{
			return ETrickSense::Backside;
		}
		if (First == ETrickInversion::FrontRoll)
		{
			return ETrickSense::Frontside;
		}
	}
	return SenseOf(AboutURad - FlightTurnRad);
}

FRotationResult FRotationRecognizer::GetCurrent() const
{
	FRotationResult Result;
	if (!bBegun)
	{
		return Result;
	}
	Result.Inversions = Inversions;
	Result.RollStartSeconds = Inversions.Num() > 0 ? Inversions[0].StartSeconds : -1.0f;
	// With inversions, the heading means something only once the rider is upright again.
	const bool bUpright = (LastBody.GetAxisZ() | Frame.U) > Settings.ArmDot;
	FillSpin(Result, bUpright && FMath::Abs(NetHeadingDeg(LastBody, nullptr)) > Settings.StanceHeadingDeg, false);
	return Result;
}

FRotationResult FRotationRecognizer::Finish(const FQuat& LandingBody) const
{
	FRotationResult Result;
	if (!bBegun)
	{
		return Result;
	}
	Result.Inversions = Inversions;
	Result.RollStartSeconds = Inversions.Num() > 0 ? Inversions[0].StartSeconds : -1.0f;
	Result.NetHeadingDeg = NetHeadingDeg(LandingBody, &Result.LandingTiltDeg);

	// Heelside is the chest on the kite's side of the travel, as at the take-off: the net heading
	// against the flight within StanceHeadingDeg. (The kite's own direction is no guide: it is
	// often near the zenith at touchdown.)
	const bool bFacingAway = FMath::Abs(Result.NetHeadingDeg) > Settings.StanceHeadingDeg;
	FillSpin(Result, bFacingAway, true);

	if (bFacingAway)
	{
		// Facing away from where the rider took off facing: blind after a backside turn, toeside
		// after a frontside one. With no turn to go by, the heading's own sign decides.
		ETrickSense Sense = LandingSense();
		if (Sense == ETrickSense::None)
		{
			Sense = SenseOf(FMath::DegreesToRadians(Result.NetHeadingDeg));
		}
		Result.LandingStance = Sense == ETrickSense::Frontside ? ETrickStance::Toeside : ETrickStance::Blind;
	}
	return Result;
}
