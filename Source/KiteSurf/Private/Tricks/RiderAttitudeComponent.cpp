#include "Tricks/RiderAttitudeComponent.h"

#include "KiteSurfUnits.h"
#include "Math/RotationMatrix.h"

namespace
{
	/** Exact step of a critically damped spring towards a constant target: x'' = -2 w x' - w^2 (x - target). */
	void StepCriticalSpring(float& X, float& V, float Target, float Omega, float Dt)
	{
		const float X0 = X - Target;
		const float B = V + Omega * X0;
		const float E = FMath::Exp(-Omega * Dt);
		X = Target + (X0 + B * Dt) * E;
		V = (V - B * Omega * Dt) * E;
	}

	/** Smallest inertia the maths divides by (kg*m^2). */
	constexpr double MinInertiaKgM2 = 1.0e-4;

	FVector SafeDivide(const FVector& A, const FVector& B)
	{
		return FVector(A.X / FMath::Max(B.X, MinInertiaKgM2), A.Y / FMath::Max(B.Y, MinInertiaKgM2),
			A.Z / FMath::Max(B.Z, MinInertiaKgM2));
	}
}

URiderAttitudeComponent::URiderAttitudeComponent()
{
	// The pawn steps it inside its fixed step.
	PrimaryComponentTick.bCanEverTick = false;

	InertiaStretchedKgM2 = FVector(13.0f, 13.0f, 2.0f);
	InertiaTuckedKgM2 = FVector(7.0f, 7.0f, 1.5f);
	TuckSmoothSeconds = 0.15f;
	HookOffsetFromComCm = FVector(12.0f, 0.0f, 20.0f);
	ComAboveBoardCm = 75.0f;
	LineTorqueScale = 0.1f;
	PreWindRollRateDegS = 250.0f;
	PreWindFlipRateDegS = 260.0f;
	PreWindSpinRateDegS = 360.0f;
	SpinAxisTiltMaxDeg = 20.0f;
	DefaultRollAxisTiltDeg = 65.0f;
	RollAxisTiltRangeDeg = 45.0f;
	AirStickTiltWithoutPreWindDeg = 0.0f;
	FlipSectorDeg = 20.0f;
	FlipSectorHysteresisDeg = 5.0f;
	PreWindLoadFloor = 0.5f;
	AirControlFractionPerS = 0.3f;
	AirControlResponseSeconds = 0.25f;
	PostureDampingPerS = 3.0f;
	PostureMaxTorqueNm = 60.0f;
	AirAngularDragPerS = 0.05f;
	AssistStrength = 1.0f;
	AssistWindowSeconds = 1.0f; // 0.7 in the plan: on a real jump the lines hold a rolled rider 60 to 80 deg off upright until the last second
	AssistMaxErrorDeg = 90.0f;  // 60 in the plan, for the same reason
	AssistNaturalFreqHz = 1.2f;
	AssistDampingRatio = 0.9f;
	AssistMaxTorqueNm = 120.0f;
	TravelAlignNaturalFreqHz = 0.8f;
	TravelAlignDampingRatio = 1.0f;
	TravelAlignMaxTorqueNm = 40.0f;
	TravelAlignMinSpeedCmS = 200.0f;
	StrapSettleSeconds = 0.3f;
	ComOffsetSettleSeconds = 0.5f;

	// The pawn's default stance side is +1, so the nose is on the body's -Right side.
	CanonicalStrapOffset = MakeCanonicalStrapOffset(-1.0f);
	StrapOffset = CanonicalStrapOffset;
	PrevStrapOffset = StrapOffset;
	ComOffsetWorldCm = FVector(0.0f, 0.0f, ComAboveBoardCm);
	PrevComOffsetWorldCm = ComOffsetWorldCm;
}

FQuat URiderAttitudeComponent::MakeCanonicalStrapOffset(float NoseSideSign)
{
	const float S = NoseSideSign >= 0.0f ? 1.0f : -1.0f;
	return FRotationMatrix::MakeFromXZ(FVector(0.0f, S, 0.0f), FVector::UpVector).ToQuat();
}

FVector URiderAttitudeComponent::GetBodyInertiaKgM2() const
{
	return FMath::Lerp(InertiaStretchedKgM2, InertiaTuckedKgM2, FMath::Clamp(TuckNow, 0.0f, 1.0f));
}

double URiderAttitudeComponent::InertiaAbout(const FVector& AxisWorld, const FVector& Ib) const
{
	const FVector A = Q.UnrotateVector(AxisWorld.GetSafeNormal());
	return Ib.X * A.X * A.X + Ib.Y * A.Y * A.Y + Ib.Z * A.Z * A.Z;
}

FVector URiderAttitudeComponent::OmegaFrom(const FVector& Ib) const
{
	return Q.RotateVector(SafeDivide(Q.UnrotateVector(L), Ib));
}

RiderAxes::FRotationAxisChoice URiderAttitudeComponent::ChooseAxis(const FVector2D& Stick, float Sigma, bool bWasFlip, float DefaultTiltDeg) const
{
	return RiderAxes::ChooseAxisBody(Stick, Sigma, DefaultTiltDeg, RollAxisTiltRangeDeg, FlipSectorDeg,
		SpinAxisTiltMaxDeg, FlipSectorHysteresisDeg, bWasFlip);
}

float URiderAttitudeComponent::FullRateRadS(RiderAxes::ERotationFamily Family) const
{
	switch (Family)
	{
	case RiderAxes::ERotationFamily::Spin: return FMath::DegreesToRadians(PreWindSpinRateDegS);
	case RiderAxes::ERotationFamily::Roll: return FMath::DegreesToRadians(PreWindRollRateDegS);
	case RiderAxes::ERotationFamily::Flip: return FMath::DegreesToRadians(PreWindFlipRateDegS);
	default: return 0.0f;
	}
}

void URiderAttitudeComponent::Reset(const FQuat& Body, const FQuat& Board)
{
	Q = Body.GetNormalized();
	PrevQ = Q;
	StrapOffset = Q.Inverse() * Board.GetNormalized();
	PrevStrapOffset = StrapOffset;
	L = FVector::ZeroVector;
	OmegaW = FVector::ZeroVector;
	CommittedAxisBody = FVector::ZeroVector;
	TuckNow = 0.0f;
	TuckVel = 0.0f;
	bActive = false;
	bWasAirborne = false;
	bControlWasFlip = false;
	bTookOffRotating = false;
	ComOffsetWorldCm = Q.GetAxisZ() * ComAboveBoardCm;
	PrevComOffsetWorldCm = ComOffsetWorldCm;
	AirSeconds = 0.0f;
	LastDebug = FAttitudeDebug();
}

void URiderAttitudeComponent::SetState(const FQuat& Body, const FVector& AngularMomentumKgM2S)
{
	Q = Body.GetNormalized();
	PrevQ = Q;
	PrevStrapOffset = StrapOffset;
	// No take-off runs, so the board settles flat on the side its nose already points to, as BeginAir does.
	CanonicalStrapOffset = MakeCanonicalStrapOffset(StrapOffset.GetAxisX().Y >= 0.0 ? 1.0f : -1.0f);
	L = AngularMomentumKgM2S;
	const FVector Ib = GetBodyInertiaKgM2();
	OmegaW = OmegaFrom(Ib);
	CommittedAxisBody = Q.UnrotateVector(OmegaW).GetSafeNormal();
	bTookOffRotating = !CommittedAxisBody.IsZero();
	bActive = true;
	bWasAirborne = true;
	bControlWasFlip = false;
	PrevComOffsetWorldCm = ComOffsetWorldCm;
}

void URiderAttitudeComponent::BeginAir(const FAttitudeInputs& In)
{
	// The slaved kinematic pose (lean included) is where the rotation starts, so there is no pop.
	Q = In.SlavedBodyQuat.GetNormalized();
	StrapOffset = Q.Inverse() * In.SlavedBoardQuat.GetNormalized();
	PrevStrapOffset = StrapOffset;

	// Flat under the feet with the nose on the side it already points to.
	const float NoseSide = StrapOffset.GetAxisX().Y >= 0.0 ? 1.0f : -1.0f;
	CanonicalStrapOffset = MakeCanonicalStrapOffset(NoseSide);

	ComOffsetWorldCm = Q.GetAxisZ() * ComAboveBoardCm;
	PrevComOffsetWorldCm = ComOffsetWorldCm;

	TuckNow = 0.0f;
	TuckVel = 0.0f;
	bControlWasFlip = false;
	bTookOffRotating = false;
	L = FVector::ZeroVector;
	CommittedAxisBody = FVector::ZeroVector;

	// Pre-wind: a target rate on the chosen axis (not L along it, which would make the light Up
	// axis whip round), scaled by how far it was built and by the load at the pop.
	const RiderAxes::FRotationAxisChoice Choice = ChooseAxis(In.PreWindStick, In.TravelSideSigma, false, DefaultRollAxisTiltDeg);
	const float Amount = FMath::Clamp(In.PreWindAmount, 0.0f, 1.0f);
	const float Load = FMath::Clamp(In.TakeoffLoad, 0.0f, 1.0f);
	const float Rate = Amount * Choice.Magnitude * (PreWindLoadFloor + (1.0f - PreWindLoadFloor) * Load) * FullRateRadS(Choice.Family);
	if (Rate > UE_KINDA_SMALL_NUMBER && !Choice.AxisBody.IsZero())
	{
		const FVector Ib = GetBodyInertiaKgM2();
		L = Q.RotateVector(Ib * (Choice.AxisBody * Rate));
		CommittedAxisBody = Choice.AxisBody;
		LastDebug.Family = Choice.Family;
		bTookOffRotating = true;
	}
}

float URiderAttitudeComponent::ComputeTimeToContact(float HeightCm, float VerticalSpeedCmS, float VerticalAccelCmS2)
{
	if (HeightCm <= 0.0f)
	{
		return 0.0f;
	}
	// h + v t + a t^2 / 2 = 0
	if (FMath::Abs(VerticalAccelCmS2) > UE_KINDA_SMALL_NUMBER)
	{
		const double A = 0.5 * VerticalAccelCmS2;
		const double Disc = double(VerticalSpeedCmS) * VerticalSpeedCmS - 4.0 * A * HeightCm;
		if (Disc >= 0.0)
		{
			const double Sq = FMath::Sqrt(Disc);
			const double R1 = (-VerticalSpeedCmS + Sq) / (2.0 * A);
			const double R2 = (-VerticalSpeedCmS - Sq) / (2.0 * A);
			double Best = TNumericLimits<double>::Max();
			if (R1 > 0.0) { Best = FMath::Min(Best, R1); }
			if (R2 > 0.0) { Best = FMath::Min(Best, R2); }
			if (Best < TNumericLimits<double>::Max())
			{
				return float(Best);
			}
		}
	}
	return HeightCm / FMath::Max(-VerticalSpeedCmS, 1.0f);
}

FQuat URiderAttitudeComponent::ComputeLandingTarget(const FVector& VelocityCmS, const FVector& WaterNormal) const
{
	FVector N = WaterNormal.GetSafeNormal();
	if (N.IsZero())
	{
		N = FVector::UpVector;
	}
	// The board lies along body Right, so the target has Right along the travel, either way round.
	FVector Travel = FVector::VectorPlaneProject(VelocityCmS, N);
	if (Travel.SizeSquared() < 1.0)
	{
		Travel = FVector::VectorPlaneProject(Q.GetAxisY(), N);
	}
	if (Travel.SizeSquared() < UE_KINDA_SMALL_NUMBER)
	{
		Travel = FVector::VectorPlaneProject(Q.GetAxisX(), N);
	}
	Travel = Travel.GetSafeNormal();

	FQuat Best = Q;
	double BestDot = -1.0;
	for (const float Sign : {1.0f, -1.0f})
	{
		const FVector Right = Travel * Sign;
		const FVector Front = FVector::CrossProduct(Right, N);
		const FQuat Candidate = FRotationMatrix::MakeFromXZ(Front, N).ToQuat();
		const double D = FMath::Abs(Candidate | Q);
		if (D > BestDot)
		{
			BestDot = D;
			Best = Candidate;
		}
	}
	return Best;
}

FVector URiderAttitudeComponent::ComputeAssistTorque(const FAttitudeInputs& In, FAttitudeDebug& OutDebug) const
{
	OutDebug.TimeToContactSeconds = ComputeTimeToContact(In.HeightAboveWaterCm, float(In.VelocityCmS.Z), In.VerticalAccelCmS2);

	const FQuat Target = ComputeLandingTarget(In.VelocityCmS, In.WaterNormal);
	FQuat Err = Target * Q.Inverse();
	if (Err.W < 0.0)
	{
		Err = FQuat(-Err.X, -Err.Y, -Err.Z, -Err.W);
	}
	Err.Normalize();
	FVector ErrAxis;
	float ErrAngle = 0.0f;
	Err.ToAxisAndAngle(ErrAxis, ErrAngle);
	if (ErrAngle < UE_KINDA_SMALL_NUMBER)
	{
		ErrAxis = FVector::ZeroVector;
		ErrAngle = 0.0f;
	}
	OutDebug.LandingErrorDeg = FMath::RadiansToDegrees(ErrAngle);
	const FVector OmegaHat = OmegaW.GetSafeNormal();
	OutDebug.ErrorAlongSpin = OmegaHat.IsZero() || ErrAxis.IsZero() ? 0.0f : float(ErrAxis | OmegaHat);

	const bool bActiveNow = AssistStrength > 0.0f && !In.bRotationInput
		&& OutDebug.TimeToContactSeconds < AssistWindowSeconds
		&& OutDebug.LandingErrorDeg < AssistMaxErrorDeg;
	OutDebug.bAssistActive = bActiveNow;
	OutDebug.AssistTorqueNm = FVector::ZeroVector;
	if (!bActiveNow)
	{
		return FVector::ZeroVector;
	}

	const FVector Ib = GetBodyInertiaKgM2();
	const double Ie = ErrAxis.IsZero() ? Ib.X : InertiaAbout(ErrAxis, Ib);
	const double Wn = UE_TWO_PI * AssistNaturalFreqHz;
	const double Kp = Ie * Wn * Wn;
	const double Kd = 2.0 * AssistDampingRatio * Ie * Wn;
	const FVector Omega = OmegaFrom(Ib);
	const FVector Tau = (ErrAxis * (Kp * ErrAngle) - Omega * Kd).GetClampedToMaxSize(AssistMaxTorqueNm);
	OutDebug.AssistTorqueNm = Tau * FMath::Clamp(AssistStrength, 0.0f, 1.0f);
	return OutDebug.AssistTorqueNm;
}

FVector URiderAttitudeComponent::ComputeTravelAlignTorque(const FAttitudeInputs& In) const
{
	if (TravelAlignNaturalFreqHz <= 0.0f || In.bRotationInput || !CommittedAxisBody.IsZero())
	{
		return FVector::ZeroVector;
	}
	const FVector Travel(In.VelocityCmS.X, In.VelocityCmS.Y, 0.0f);
	const FVector Nose = GetBoardQuat().GetAxisX();
	const FVector Nose2D(Nose.X, Nose.Y, 0.0f);
	constexpr float MinNoseLength = 0.2f;
	if (Travel.Size() < TravelAlignMinSpeedCmS || Nose2D.Size() < MinNoseLength)
	{
		return FVector::ZeroVector;
	}
	// The yaw from the nose to the travel, either end of the board: within +-90 deg.
	double Error = FMath::Atan2(Travel.Y, Travel.X) - FMath::Atan2(Nose2D.Y, Nose2D.X);
	Error = FMath::UnwindRadians(Error);
	if (Error > UE_HALF_PI) { Error -= UE_PI; }
	else if (Error < -UE_HALF_PI) { Error += UE_PI; }

	const FVector Ib = GetBodyInertiaKgM2();
	const double IUp = InertiaAbout(FVector::UpVector, Ib);
	const double Wn = UE_TWO_PI * TravelAlignNaturalFreqHz;
	const double Torque = IUp * (Wn * Wn * Error - 2.0 * TravelAlignDampingRatio * Wn * (OmegaW | FVector::UpVector));
	// It comes in over the strap settle time after the take-off, while the rider is still busy with the pop.
	const double RampIn = 1.0 - FMath::Exp(-AirSeconds / FMath::Max(StrapSettleSeconds, 0.01f));
	return FVector::UpVector * (RampIn * FMath::Clamp(Torque, -double(TravelAlignMaxTorqueNm), double(TravelAlignMaxTorqueNm)));
}

void URiderAttitudeComponent::RotateFree(float Dt, const FVector& Ib)
{
	// Free rigid body with L fixed in the world: H = sum Lb_i^2 / (2 I_i), split as
	// |L|^2 / (2 I_x) + Lb_z^2 (1/I_z - 1/I_x) / 2 + Lb_y^2 (1/I_y - 1/I_x) / 2.
	// The first part is a rotation about L in the world, the others about body axes; each is exact.
	// Strang splitting of the body parts is second order, conserves |L| exactly and keeps the
	// energy bounded; for a symmetric top (I_x = I_y) the whole step is exact.
	const double LSize = L.Size();
	if (LSize < UE_SMALL_NUMBER)
	{
		return;
	}
	const double InvX = 1.0 / FMath::Max(Ib.X, MinInertiaKgM2);
	Q = (FQuat(L / LSize, LSize * InvX * Dt) * Q).GetNormalized();

	auto BodyTurn = [this, &Ib, InvX](int32 Axis, double H)
	{
		const FVector Lb = Q.UnrotateVector(L);
		const double Rate = Lb[Axis] * (1.0 / FMath::Max(Ib[Axis], MinInertiaKgM2) - InvX);
		const double Angle = Rate * H;
		if (FMath::Abs(Angle) > UE_DOUBLE_SMALL_NUMBER)
		{
			FVector E = FVector::ZeroVector;
			E[Axis] = 1.0;
			Q = (Q * FQuat(E, Angle)).GetNormalized();
		}
	};
	BodyTurn(2, 0.5 * Dt);
	BodyTurn(1, Dt);
	BodyTurn(2, 0.5 * Dt);
}

void URiderAttitudeComponent::Step(float Dt, const FAttitudeInputs& In)
{
	PrevQ = Q;
	PrevStrapOffset = StrapOffset;
	PrevComOffsetWorldCm = ComOffsetWorldCm;
	if (Dt <= 0.0f)
	{
		return;
	}

	if (!In.bAirborne)
	{
		// Slaved on the water, while landing and in a crash.
		Q = In.SlavedBodyQuat.GetNormalized();
		StrapOffset = Q.Inverse() * In.SlavedBoardQuat.GetNormalized();
		L = FVector::ZeroVector;
		OmegaW = FVector::ZeroVector;
		CommittedAxisBody = FVector::ZeroVector;
		TuckNow = 0.0f;
		TuckVel = 0.0f;
		ComOffsetWorldCm = Q.GetAxisZ() * ComAboveBoardCm;
		bActive = false;
		bWasAirborne = false;
		bControlWasFlip = false;
		bTookOffRotating = false;
		AirSeconds = 0.0f;
		LastDebug = FAttitudeDebug();
		return;
	}

	FAttitudeDebug Debug;
	Debug.Family = LastDebug.Family;
	if (!bWasAirborne)
	{
		AirSeconds = 0.0f;
		BeginAir(In);
		PrevQ = Q;
		PrevStrapOffset = StrapOffset;
		Debug.Family = LastDebug.Family;
	}
	bWasAirborne = true;
	bActive = true;

	const FQuat QStart = Q;
	const FVector LStart = L;

	// Pose -> inertia, through a critically damped tuck.
	StepCriticalSpring(TuckNow, TuckVel, FMath::Clamp(In.Tuck, 0.0f, 1.0f), 2.0f / FMath::Max(TuckSmoothSeconds, 0.01f), Dt);
	TuckNow = FMath::Clamp(TuckNow, 0.0f, 1.0f);
	const FVector Ib = GetBodyInertiaKgM2();

	// 1) Torques at the start of the step (N*m, world).
	FVector Tau = FVector::ZeroVector;
	if (In.bLinesTaut)
	{
		// r x F at the hook about the centre of mass. It pulls Up towards the lines and does no work
		// on rotation about them.
		const FVector RM = Q.RotateVector(HookOffsetFromComCm) / KiteUnits::CmPerM;
		const FVector FN = In.LineForceUU / KiteUnits::UnrealForcePerN;
		Debug.LineTorqueNm = LineTorqueScale * FVector::CrossProduct(RM, FN);
		Tau += Debug.LineTorqueNm;
	}

	OmegaW = OmegaFrom(Ib);
	// A jump that left the water rotating keeps the plan's mapping, so stick X drives the roll it is
	// in; one that left with no rotation spins flat on stick X alone (AirStickTiltWithoutPreWindDeg).
	const float ControlDefaultTiltDeg = bTookOffRotating ? DefaultRollAxisTiltDeg : AirStickTiltWithoutPreWindDeg;
	const RiderAxes::FRotationAxisChoice Control = In.bRotationInput
		? ChooseAxis(In.RotationStick, In.TravelSideSigma, bControlWasFlip, ControlDefaultTiltDeg)
		: RiderAxes::FRotationAxisChoice();
	if (In.bRotationInput)
	{
		// Capped torque towards a target rate on the stick's axis; while held, that axis is committed
		// so posture damping does not fight it. Released, nothing brakes the rotation.
		if (!Control.AxisBody.IsZero())
		{
			const FVector AxisW = Q.RotateVector(Control.AxisBody);
			const double IAxis = InertiaAbout(AxisW, Ib);
			const double Full = FullRateRadS(Control.Family);
			const double TargetRate = Control.Magnitude * Full;
			const double TauMax = AirControlFractionPerS * IAxis * Full;
			const double TauAlong = FMath::Clamp(IAxis * (TargetRate - (OmegaW | AxisW)) / double(FMath::Max(AirControlResponseSeconds, 0.01f)), -TauMax, TauMax);
			Debug.ControlTorqueNm = AxisW * TauAlong;
			Tau += Debug.ControlTorqueNm;
			CommittedAxisBody = Control.AxisBody;
			bControlWasFlip = Control.Family == RiderAxes::ERotationFamily::Flip;
			Debug.Family = Control.Family;
		}
	}
	// The landing assist (zero with rotation input or outside its window); it also fills the
	// landing error for the evaluator every step.
	ComputeAssistTorque(In, Debug);
	if (Debug.bAssistActive)
	{
		Tau += Debug.AssistTorqueNm;
		CommittedAxisBody = FVector::ZeroVector;
	}
	else
	{
		// No trick going on: the rider keeps the board along the flight.
		Debug.TravelAlignTorqueNm = ComputeTravelAlignTorque(In);
		Tau += Debug.TravelAlignTorqueNm;
	}
	L += Tau * Dt;

	// 2) Posture damping off the committed axis (all rotation when none is committed) and drag, as
	// exact exponential decays of body-frame omega: they cannot overshoot at any step.
	const FVector Lb = Q.UnrotateVector(L);
	FVector Wb = SafeDivide(Lb, Ib);
	const FVector A = CommittedAxisBody;
	const double Wa = A.IsZero() ? 0.0 : (Wb | A);
	const FVector WOff = Wb - A * Wa;
	const FVector WOffDamped = WOff * FMath::Exp(-PostureDampingPerS * Dt);
	const FVector DLb = (Ib * (WOffDamped - WOff)).GetClampedToMaxSize(PostureMaxTorqueNm * Dt);
	Debug.PostureTorqueNm = Q.RotateVector(DLb) / Dt;
	Wb = SafeDivide(Lb + DLb, Ib) * FMath::Exp(-AirAngularDragPerS * Dt);
	L = Q.RotateVector(Ib * Wb);

	// 3) The free rotation for the step with this L.
	RotateFree(Dt, Ib);
	OmegaW = OmegaFrom(Ib);

	AirSeconds += Dt;

	// 4) The strapped board eases to flat under the feet; the visual centre of mass to above the board.
	StrapOffset = FQuat::Slerp(StrapOffset, CanonicalStrapOffset, 1.0f - FMath::Exp(-Dt / FMath::Max(StrapSettleSeconds, 0.01f))).GetNormalized();
	ComOffsetWorldCm = FMath::Lerp(ComOffsetWorldCm, FVector(0.0f, 0.0f, ComAboveBoardCm), 1.0f - FMath::Exp(-Dt / FMath::Max(ComOffsetSettleSeconds, 0.01f)));

	if (!ensureAlwaysMsgf(!Q.ContainsNaN() && !L.ContainsNaN(), TEXT("Rider attitude went NaN; holding the last pose")))
	{
		Q = QStart;
		L = LStart.ContainsNaN() ? FVector::ZeroVector : LStart;
		OmegaW = FVector::ZeroVector;
	}

	Debug.CommittedAxisWorld = CommittedAxisBody.IsZero() ? FVector::ZeroVector : Q.RotateVector(CommittedAxisBody);
	LastDebug = Debug;
}
