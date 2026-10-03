#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "KiteSurfUnits.h"
#include "Tricks/RiderAttitudeComponent.h"
#include "Tricks/RiderAxes.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests on the bare URiderAttitudeComponent (T1.2, PR D): no world, no pawn. The thresholds are
// ranges and orderings, so physics tuning can move the numbers without breaking them.

namespace TrickAttitudeTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float Step240 = 1.0f / 240.0f;
	/** Hang tension the line torque scale is calibrated at (N), T1 plan section 2 item 8. */
	constexpr float HangTensionN = 800.0f;

	URiderAttitudeComponent* MakeAttitude()
	{
		return NewObject<URiderAttitudeComponent>();
	}

	/** Upright rider, identity body: Front +X, Right +Y, Up +Z. The board lies along body Right. */
	FQuat UprightBody() { return FQuat::Identity; }

	FQuat StrappedBoard(const FQuat& Body) { return Body * URiderAttitudeComponent::MakeCanonicalStrapOffset(-1.0f); }

	/** Airborne inputs far above the water (assist window closed), with a line force in N (world). */
	FAttitudeInputs Air(const FVector& LineForceN = FVector::ZeroVector, const FQuat& Body = UprightBody())
	{
		FAttitudeInputs In;
		In.bAirborne = true;
		In.SlavedBodyQuat = Body;
		In.SlavedBoardQuat = StrappedBoard(Body);
		In.VelocityCmS = FVector(0.0, 800.0, 0.0);
		In.LineForceUU = LineForceN * KiteUnits::UnrealForcePerN;
		In.bLinesTaut = true;
		In.HeightAboveWaterCm = 1.0e6f;
		In.VerticalAccelCmS2 = 0.0f;
		return In;
	}

	/** A full pre-wind towards a back roll at full load. */
	void AddBackRollPreWind(FAttitudeInputs& In, float Sigma)
	{
		In.PreWindStick = FVector2D(1.0f, 0.0f);
		In.PreWindAmount = 1.0f;
		In.TakeoffLoad = 1.0f;
		In.TravelSideSigma = Sigma;
	}

	FVector Up(const URiderAttitudeComponent* A) { return A->GetBodyQuat().GetAxisZ(); }

	double TiltDeg(const URiderAttitudeComponent* A)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Up(A).Z, -1.0, 1.0)));
	}

	/** Inversion count as in docs/tricks.md section 6.6: Up below -0.3 after being above +0.3. */
	struct FInversionTally
	{
		bool bArmed = true;
		int32 Count = 0;
		float FirstInvertedAt = -1.0f;
		float BackUprightAt = -1.0f;

		void Add(double UpDotWorldUp, float Time)
		{
			if (bArmed && UpDotWorldUp < -0.3)
			{
				++Count;
				bArmed = false;
				if (FirstInvertedAt < 0.0f) { FirstInvertedAt = Time; }
			}
			else if (!bArmed && UpDotWorldUp > 0.3)
			{
				bArmed = true;
				if (BackUprightAt < 0.0f) { BackUprightAt = Time; }
			}
		}
	};

	struct FBackRollResult
	{
		int32 Inversions = 0;
		float DurationSeconds = -1.0f;
		/** Rotation about the committed axis by the time Up is back above +0.3 (deg). */
		double TurnedAtBackDeg = 0.0;
		bool bNaN = false;
	};

	/** A full pre-wind back roll at hang tension (line straight up), counted over WindowSeconds. */
	FBackRollResult RunBackRoll(float Dt, float WindowSeconds, float Sigma = 1.0f, float TensionN = HangTensionN)
	{
		URiderAttitudeComponent* A = MakeAttitude();
		FAttitudeInputs In = Air(FVector(0.0, 0.0, TensionN));
		AddBackRollPreWind(In, Sigma);

		FBackRollResult R;
		FInversionTally Tally;
		double Turned = 0.0;
		FVector AxisW = FVector::ZeroVector;
		const int32 Steps = FMath::RoundToInt(WindowSeconds / Dt);
		for (int32 I = 0; I < Steps; ++I)
		{
			A->Step(Dt, In);
			if (I == 0) { AxisW = A->GetLastStepDebug().CommittedAxisWorld; }
			Turned += (A->GetAngularVelocity() | AxisW) * Dt;
			const float T = (I + 1) * Dt;
			const bool bWasArmedBack = Tally.BackUprightAt >= 0.0f;
			Tally.Add(Up(A).Z, T);
			if (!bWasArmedBack && Tally.BackUprightAt >= 0.0f)
			{
				R.TurnedAtBackDeg = FMath::RadiansToDegrees(Turned);
			}
			R.bNaN |= A->GetBodyQuat().ContainsNaN() || A->GetAngularMomentum().ContainsNaN();
		}
		R.Inversions = Tally.Count;
		R.DurationSeconds = Tally.BackUprightAt;
		return R;
	}
}

using namespace TrickAttitudeTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickNoInputNoRotation, "KiteSurf.Trick.NoInputNoRotation", TrickAttitudeTest::Flags)

bool FKiteSurfTrickNoInputNoRotation::RunTest(const FString& Parameters)
{
	// No line force, no pre-wind, no stick: the rider does not turn at all.
	{
		URiderAttitudeComponent* A = MakeAttitude();
		const FAttitudeInputs In = Air();
		for (int32 I = 0; I < 3 * 240; ++I) { A->Step(Step240, In); }
		TestTrue(TEXT("Airborne attitude is active"), A->IsSimulating());
		TestTrue(TEXT("No torque: body orientation unchanged (< 0.01 deg)"),
			FMath::RadiansToDegrees(A->GetBodyQuat().AngularDistance(UprightBody())) < 0.01);
		TestTrue(TEXT("No torque: angular velocity zero (< 1e-6 rad/s)"), A->GetAngularVelocity().Size() < 1.0e-6);
	}

	// Hang tension straight up and no input: the rider swings into the hang lean and no further.
	{
		URiderAttitudeComponent* A = MakeAttitude();
		const FAttitudeInputs In = Air(FVector(0.0, 0.0, HangTensionN));
		FInversionTally Tally;
		double SpinAboutUp = 0.0;
		double MaxTilt = 0.0;
		for (int32 I = 0; I < 4 * 240; ++I)
		{
			A->Step(Step240, In);
			SpinAboutUp += (A->GetAngularVelocity() | FVector::UpVector) * Step240;
			MaxTilt = FMath::Max(MaxTilt, TiltDeg(A));
			Tally.Add(Up(A).Z, (I + 1) * Step240);
		}
		AddInfo(FString::Printf(TEXT("No input at %.0f N: spin about up %.2f deg, max tilt %.1f deg"), HangTensionN,
			FMath::RadiansToDegrees(SpinAboutUp), MaxTilt));
		TestTrue(TEXT("Spin about world up under 30 deg"), FMath::Abs(FMath::RadiansToDegrees(SpinAboutUp)) < 30.0);
		TestEqual(TEXT("No inversions"), Tally.Count, 0);
		TestTrue(TEXT("Max body tilt under 50 deg"), MaxTilt < 50.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickAngularMomentumConserved, "KiteSurf.Trick.AngularMomentumConserved", TrickAttitudeTest::Flags)

bool FKiteSurfTrickAngularMomentumConserved::RunTest(const FString& Parameters)
{
	URiderAttitudeComponent* A = MakeAttitude();
	A->PostureDampingPerS = 0.0f;
	A->AirAngularDragPerS = 0.0f;
	A->InertiaStretchedKgM2 = FVector(13.0, 13.0, 2.0);

	// L 40 deg off Up: a symmetric top that precesses and spins fast about its light axis.
	const float Off = FMath::DegreesToRadians(40.0f);
	const FVector L0 = FVector(FMath::Sin(Off), 0.0f, FMath::Cos(Off)) * 20.0f;
	A->SetState(UprightBody(), L0);
	const FAttitudeInputs In = Air();

	auto Energy = [A]() { return 0.5 * (A->GetAngularMomentum() | A->GetAngularVelocity()); };
	auto OmegaUp = [A]() { return A->GetBodyQuat().UnrotateVector(A->GetAngularVelocity()).Z; };
	const double E0 = Energy();
	const double WUp0 = OmegaUp();

	double Turned = 0.0;
	for (int32 I = 0; I < 5 * 240; ++I)
	{
		A->Step(Step240, In);
		Turned += A->GetAngularVelocity().Size() * Step240;
	}

	const FVector L1 = A->GetAngularMomentum();
	const double DriftDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(L1.GetSafeNormal() | L0.GetSafeNormal(), -1.0, 1.0)));
	TestTrue(TEXT("|L| constant within 1e-4 relative"), FMath::Abs(L1.Size() / L0.Size() - 1.0) < 1.0e-4);
	TestTrue(TEXT("L direction drift under 0.1 deg"), DriftDeg < 0.1);
	TestTrue(TEXT("Kinetic energy within 1%"), FMath::Abs(Energy() / E0 - 1.0) < 0.01);
	TestTrue(TEXT("Symmetric top: omega about Up constant within 0.5%"), FMath::Abs(OmegaUp() / WUp0 - 1.0) < 0.005);
	TestTrue(TEXT("The body actually turned (> 360 deg in 5 s)"), FMath::RadiansToDegrees(Turned) > 360.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickTuckSpinsFaster, "KiteSurf.Trick.TuckSpinsFaster", TrickAttitudeTest::Flags)

bool FKiteSurfTrickTuckSpinsFaster::RunTest(const FString& Parameters)
{
	double Omega[2] = {0.0, 0.0};
	double LSize[2] = {0.0, 0.0};
	double Expected = 0.0;
	for (int32 Tuck = 0; Tuck < 2; ++Tuck)
	{
		URiderAttitudeComponent* A = MakeAttitude();
		A->PostureDampingPerS = 0.0f;
		A->AirAngularDragPerS = 0.0f;
		A->SetState(UprightBody(), FVector(0.0, 10.0, 0.0)); // about body Right (a flip axis)
		Expected = A->InertiaStretchedKgM2.Y / A->InertiaTuckedKgM2.Y;
		FAttitudeInputs In = Air();
		In.Tuck = float(Tuck);
		for (int32 I = 0; I < 240; ++I) { A->Step(Step240, In); }
		Omega[Tuck] = A->GetAngularVelocity().Size();
		LSize[Tuck] = A->GetAngularMomentum().Size();
		if (Tuck == 1)
		{
			TestTrue(TEXT("Tuck spring has all but settled after 1 s (> 0.99)"), A->GetTuckAmount() > 0.99f);
		}
	}
	const double Ratio = Omega[1] / Omega[0];
	AddInfo(FString::Printf(TEXT("Tucked/stretched rate %.3f (inertia ratio %.3f)"), Ratio, Expected));
	TestTrue(TEXT("Tucked spins faster"), Omega[1] > Omega[0]);
	TestTrue(TEXT("Rate ratio within 3% of the inertia ratio"), FMath::Abs(Ratio / Expected - 1.0) < 0.03);
	TestTrue(TEXT("|L| unchanged by the tuck within 0.1%"), FMath::Abs(LSize[1] / LSize[0] - 1.0) < 0.001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickBackRollFromPreWind, "KiteSurf.Trick.BackRollFromPreWind", TrickAttitudeTest::Flags)

bool FKiteSurfTrickBackRollFromPreWind::RunTest(const FString& Parameters)
{
	// The bare-component calibration of LineTorqueScale and PreWindRollRateDegS: a full pre-wind
	// at full load against hang tension straight up (the worst case for the line torque). The pawn
	// version on a real jump comes with the wiring (PR E).
	for (const float Sigma : {1.0f, -1.0f})
	{
		const FBackRollResult R = RunBackRoll(Step240, 3.0f, Sigma);
		AddInfo(FString::Printf(TEXT("Back roll sigma %+.0f at %.0f N: %d inversion(s), back upright after %.3f s, %.0f deg turned"),
			Sigma, HangTensionN, R.Inversions, R.DurationSeconds, R.TurnedAtBackDeg));
		TestFalse(TEXT("No NaN"), R.bNaN);
		TestEqual(TEXT("Exactly one inversion in 3 s"), R.Inversions, 1);
		TestTrue(TEXT("Roll duration 1.5 to 2.5 s"), R.DurationSeconds >= 1.5f && R.DurationSeconds <= 2.5f);
		// A real roll keeps turning the same way; falling back from a half-inverted swing is not one.
		TestTrue(TEXT("The roll goes round, not back (> 240 deg turned when upright again)"), R.TurnedAtBackDeg > 240.0);
	}

	// About 10% either side of hang tension still rolls once inside the range.
	for (const float TensionN : {0.9f * HangTensionN, 1.1f * HangTensionN})
	{
		const FBackRollResult R = RunBackRoll(Step240, 3.0f, 1.0f, TensionN);
		AddInfo(FString::Printf(TEXT("Back roll at %.0f N: %d inversion(s), %.3f s"), TensionN, R.Inversions, R.DurationSeconds));
		TestEqual(FString::Printf(TEXT("One inversion at %.0f N"), TensionN), R.Inversions, 1);
		TestTrue(FString::Printf(TEXT("Duration 1.5 to 2.5 s at %.0f N"), TensionN), R.DurationSeconds >= 1.5f && R.DurationSeconds <= 2.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickBackRollChestToTailFirst, "KiteSurf.Trick.BackRollTurnsChestToTailFirst", TrickAttitudeTest::Flags)

bool FKiteSurfTrickBackRollChestToTailFirst::RunTest(const FString& Parameters)
{
	// docs/tricks.md section 2: a back roll turns the chest towards the tail first. Travelling
	// either way along the board, with sigma from RiderAxes::TravelSide.
	for (const float Travel : {1.0f, -1.0f})
	{
		const FVector Velocity(0.0, 800.0 * Travel, 0.0);
		const FVector T = FVector(Velocity.X, Velocity.Y, 0.0).GetSafeNormal();
		const float Sigma = RiderAxes::TravelSide(UprightBody(), Velocity, StrappedBoard(UprightBody()).GetAxisX());
		TestEqual(TEXT("Sigma is the sign of Right . T"), Sigma, Travel);

		URiderAttitudeComponent* A = MakeAttitude();
		FAttitudeInputs In = Air(FVector(0.0, 0.0, HangTensionN));
		In.VelocityCmS = Velocity;
		AddBackRollPreWind(In, Sigma);
		const FVector Front0 = UprightBody().GetAxisX();
		const FVector Up0 = UprightBody().GetAxisZ();
		for (int32 I = 0; I < FMath::RoundToInt(0.15f / Step240); ++I) { A->Step(Step240, In); }
		const FVector DFront = A->GetBodyQuat().GetAxisX() - Front0;
		const FVector DUp = A->GetBodyQuat().GetAxisZ() - Up0;
		AddInfo(FString::Printf(TEXT("Travel %+.0f: chest moved %.3f towards the tail, head %.3f towards the tail side"),
			Travel, DFront | -T, DUp | -T));
		TestTrue(TEXT("Chest turns towards the tail (-T) in the first 0.15 s"), (DFront | -T) > 0.05);
		TestTrue(TEXT("Head tips towards the tail side in the first 0.15 s"), (DUp | -T) > 0.0);

		// The axis helper agrees: the spin part of a back roll about Up is -sigma.
		const FVector Axis = RiderAxes::BackRollAxisBody(Sigma, 65.0f);
		TestTrue(TEXT("Back roll axis is a unit vector"), FMath::IsNearlyEqual(Axis.Size(), 1.0, 1.0e-5));
		TestTrue(TEXT("Back roll spins in the backside sense (-sigma about Up)"), Axis.Z * Sigma < 0.0);
	}

	// Below the travel threshold the board's nose decides.
	const float NoseSigma = RiderAxes::TravelSide(UprightBody(), FVector(800.0, 10.0, 0.0), FVector(0.0, -1.0, 0.0));
	TestEqual(TEXT("Travelling along Front, sigma comes from the nose"), NoseSigma, -1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickLineTorqueHangsRiderBack, "KiteSurf.Trick.LineTorqueHangsRiderBack", TrickAttitudeTest::Flags)

bool FKiteSurfTrickLineTorqueHangsRiderBack::RunTest(const FString& Parameters)
{
	URiderAttitudeComponent* A = MakeAttitude();
	A->LineTorqueScale = 1.0f;
	const FAttitudeInputs In = Air(FVector(0.0, 0.0, HangTensionN));
	for (int32 I = 0; I < 3 * 240; ++I) { A->Step(Step240, In); }

	// The bound comes from the geometry, not a measured number.
	const FVector Hook = A->HookOffsetFromComCm;
	const double ExpectedDeg = FMath::RadiansToDegrees(FMath::Atan2(Hook.X, Hook.Z));
	const FVector HookWorld = A->GetBodyQuat().RotateVector(Hook).GetSafeNormal();
	const double HookToLineDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(HookWorld.Z, -1.0, 1.0)));
	AddInfo(FString::Printf(TEXT("Hang lean %.2f deg (geometry %.2f), hook %.2f deg off the line, |w| %.4f rad/s"),
		TiltDeg(A), ExpectedDeg, HookToLineDeg, A->GetAngularVelocity().Size()));
	TestTrue(TEXT("Body Up settles at atan(hook X / hook Z) +- 3 deg"), FMath::Abs(TiltDeg(A) - ExpectedDeg) < 3.0);
	TestTrue(TEXT("The hook points along the line (< 3 deg)"), HookToLineDeg < 3.0);
	TestTrue(TEXT("Leans back: the chest turns up towards the line"), A->GetBodyQuat().GetAxisX().Z > 0.0);
	TestTrue(TEXT("Settled without ringing: |w| < 0.05 rad/s"), A->GetAngularVelocity().Size() < 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickLineTorqueFreeAboutLine, "KiteSurf.Trick.LineTorqueFreeAboutLine", TrickAttitudeTest::Flags)

bool FKiteSurfTrickLineTorqueFreeAboutLine::RunTest(const FString& Parameters)
{
	// r x F is perpendicular to F: the lines tip the body but leave rotation about them alone.
	URiderAttitudeComponent* A = MakeAttitude();
	A->LineTorqueScale = 1.0f;
	A->PostureDampingPerS = 0.0f;
	A->AirAngularDragPerS = 0.0f;
	const FVector LineDir = FVector(0.5, 0.2, 0.84).GetSafeNormal();
	const FAttitudeInputs In = Air(LineDir * 2.0f * HangTensionN);

	const FVector L0 = LineDir * 10.0f + FVector(0.0, 3.0, 0.0);
	A->SetState(UprightBody(), L0);
	const double Along0 = L0 | LineDir;
	double MaxTorqueAlong = 0.0;
	double MaxTorque = 0.0;
	double TurnedAboutLine = 0.0;
	for (int32 I = 0; I < 3 * 240; ++I)
	{
		A->Step(Step240, In);
		const FVector Tau = A->GetLastStepDebug().LineTorqueNm;
		MaxTorqueAlong = FMath::Max(MaxTorqueAlong, FMath::Abs(Tau | LineDir));
		MaxTorque = FMath::Max(MaxTorque, Tau.Size());
		TurnedAboutLine += (A->GetAngularVelocity() | LineDir) * Step240;
	}
	const double Along1 = A->GetAngularMomentum() | LineDir;
	const double Changed = (A->GetAngularMomentum() - L0).Size();
	AddInfo(FString::Printf(TEXT("L along the line %.4f -> %.4f; |dL| %.2f; max torque %.1f N*m, along the line %.2e; turned %.0f deg about the line"),
		Along0, Along1, Changed, MaxTorque, MaxTorqueAlong, FMath::RadiansToDegrees(TurnedAboutLine)));
	TestTrue(TEXT("The line torque acts (max > 10 N*m)"), MaxTorque > 10.0);
	TestTrue(TEXT("It changed L off the line (|dL| > 1)"), Changed > 1.0);
	TestTrue(TEXT("Line torque along the line is nil (< 1e-3 of its size)"), MaxTorqueAlong < 1.0e-3 * MaxTorque);
	TestTrue(TEXT("L along the line conserved within 1%"), FMath::Abs(Along1 / Along0 - 1.0) < 0.01);
	TestTrue(TEXT("The rider kept turning about the line (> 90 deg)"), FMath::Abs(FMath::RadiansToDegrees(TurnedAboutLine)) > 90.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickPostureDampingStopsRing, "KiteSurf.Trick.PostureDampingStopsRing", TrickAttitudeTest::Flags)

bool FKiteSurfTrickPostureDampingStopsRing::RunTest(const FString& Parameters)
{
	// Start 30 deg past the hang lean and let the line pendulum swing.
	double LastSecondDev[2] = {0.0, 0.0};
	double FinalOmega[2] = {0.0, 0.0};
	double EquilibriumDeg = 0.0;
	for (int32 Damped = 0; Damped < 2; ++Damped)
	{
		URiderAttitudeComponent* A = MakeAttitude();
		A->LineTorqueScale = 1.0f;
		if (!Damped)
		{
			A->PostureDampingPerS = 0.0f;
			A->AirAngularDragPerS = 0.0f;
		}
		const FVector Hook = A->HookOffsetFromComCm;
		const double Eq = FMath::Atan2(Hook.X, Hook.Z);
		EquilibriumDeg = FMath::RadiansToDegrees(Eq);
		A->SetState(FQuat(FVector(0.0, 1.0, 0.0), -Eq - FMath::DegreesToRadians(30.0)), FVector::ZeroVector);
		const FAttitudeInputs In = Air(FVector(0.0, 0.0, HangTensionN));
		for (int32 I = 0; I < 4 * 240; ++I)
		{
			A->Step(Step240, In);
			if (I >= 3 * 240)
			{
				LastSecondDev[Damped] = FMath::Max(LastSecondDev[Damped], FMath::Abs(TiltDeg(A) - EquilibriumDeg));
			}
		}
		FinalOmega[Damped] = A->GetAngularVelocity().Size();
	}
	AddInfo(FString::Printf(TEXT("Swing about the %.1f deg hang lean in the 4th second: undamped %.1f deg, damped %.2f deg (|w| %.3f rad/s)"),
		EquilibriumDeg, LastSecondDev[0], LastSecondDev[1], FinalOmega[1]));
	TestTrue(TEXT("Without posture damping the pendulum still rings (> 20 deg)"), LastSecondDev[0] > 20.0);
	TestTrue(TEXT("With posture damping the ring is gone (< 2 deg)"), LastSecondDev[1] < 2.0);
	TestTrue(TEXT("Damped rider is still (|w| < 0.05 rad/s)"), FinalOmega[1] < 0.05);
	return true;
}

namespace TrickAttitudeTest
{
	struct FAssistRun
	{
		double InitialErrorDeg = 0.0;
		double FinalErrorDeg = 0.0;
		int32 ActiveSteps = 0;
		double MaxAssistTorque = 0.0;
	};

	/** A rider ShortDeg short of upright about the default back roll axis, still turning at OmegaRadS, falling ballistically from 300 cm at 600 cm/s. */
	FAssistRun RunNearMiss(float Strength, bool bHoldInput, float Dt = Step240, float ShortDeg = 40.0f, float OmegaRadS = 0.3f)
	{
		URiderAttitudeComponent* A = MakeAttitude();
		A->AssistStrength = Strength;
		const FVector AxisB = RiderAxes::BackRollAxisBody(1.0f, A->DefaultRollAxisTiltDeg);
		const FQuat Body = FQuat(AxisB, -FMath::DegreesToRadians(ShortDeg));
		A->SetState(Body, Body.RotateVector(A->GetBodyInertiaKgM2() * (AxisB * OmegaRadS)));

		double H = 300.0, Vz = -600.0;
		const double Az = -KiteUnits::GravityCmS2;
		FAttitudeInputs In = Air();
		In.VerticalAccelCmS2 = float(Az);
		if (bHoldInput)
		{
			In.bRotationInput = true;
			In.RotationStick = FVector2D(1.0e-3f, 0.0f);
		}
		FAssistRun R;
		FAttitudeDebug Probe;
		In.HeightAboveWaterCm = float(H);
		In.VelocityCmS = FVector(0.0, 800.0, Vz);
		A->ComputeAssistTorque(In, Probe);
		R.InitialErrorDeg = Probe.LandingErrorDeg;
		while (H > 0.0)
		{
			In.HeightAboveWaterCm = float(H);
			In.VelocityCmS = FVector(0.0, 800.0, Vz);
			A->Step(Dt, In);
			R.ActiveSteps += A->GetLastStepDebug().bAssistActive ? 1 : 0;
			R.MaxAssistTorque = FMath::Max(R.MaxAssistTorque, A->GetLastStepDebug().AssistTorqueNm.Size());
			Vz += Az * Dt;
			H += Vz * Dt;
		}
		In.HeightAboveWaterCm = 0.0f;
		A->ComputeAssistTorque(In, Probe);
		R.FinalErrorDeg = Probe.LandingErrorDeg;
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickAssistLandsNearMiss, "KiteSurf.Trick.AssistLandsNearMiss", TrickAttitudeTest::Flags)

bool FKiteSurfTrickAssistLandsNearMiss::RunTest(const FString& Parameters)
{
	const FAssistRun On = RunNearMiss(1.0f, false);
	const FAssistRun Off = RunNearMiss(0.0f, false);
	const FAssistRun Held = RunNearMiss(1.0f, true);
	AddInfo(FString::Printf(TEXT("Near miss from %.1f deg: assist on %.1f deg (%d steps active), off %.1f deg, input held %.1f deg"),
		On.InitialErrorDeg, On.FinalErrorDeg, On.ActiveSteps, Off.FinalErrorDeg, Held.FinalErrorDeg));
	TestTrue(TEXT("Starts 40 deg from a valid landing"), FMath::IsNearlyEqual(On.InitialErrorDeg, 40.0, 0.5));
	TestTrue(TEXT("Assist on: lands within 15 deg"), On.FinalErrorDeg < 15.0);
	TestTrue(TEXT("Assist off: still more than 30 deg out"), Off.FinalErrorDeg > 30.0);
	TestTrue(TEXT("Assist at least halves the error"), On.FinalErrorDeg < 0.5 * Off.FinalErrorDeg);
	TestTrue(TEXT("Assist acted"), On.ActiveSteps > 0);
	TestEqual(TEXT("Assist off: never active"), Off.ActiveSteps, 0);
	TestEqual(TEXT("Input held: assist never active"), Held.ActiveSteps, 0);
	TestEqual(TEXT("Input held: assist torque zero"), Held.MaxAssistTorque, 0.0);

	// The same at other step sizes.
	for (const float Dt : {1.0f / 120.0f, 1.0f / 480.0f})
	{
		const FAssistRun R = RunNearMiss(1.0f, false, Dt);
		TestTrue(FString::Printf(TEXT("Assist on at dt %.5f s within 2 deg of 1/240"), Dt), FMath::Abs(R.FinalErrorDeg - On.FinalErrorDeg) < 2.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickAssistOnlyInsideWindow, "KiteSurf.Trick.AssistOnlyInsideWindow", TrickAttitudeTest::Flags)

bool FKiteSurfTrickAssistOnlyInsideWindow::RunTest(const FString& Parameters)
{
	const FVector AxisB = RiderAxes::BackRollAxisBody(1.0f, 65.0f);

	// Time to contact: ballistic root, and the fallback with no acceleration.
	const float Ttc = URiderAttitudeComponent::ComputeTimeToContact(300.0f, -600.0f, -KiteUnits::GravityCmS2);
	TestTrue(TEXT("Ballistic time to contact from 300 cm at -600 cm/s is about 0.38 s"), FMath::IsNearlyEqual(Ttc, 0.381f, 0.01f));
	TestTrue(TEXT("No acceleration: h / -v"), FMath::IsNearlyEqual(URiderAttitudeComponent::ComputeTimeToContact(300.0f, -600.0f, 0.0f), 0.5f, 1.0e-4f));
	TestTrue(TEXT("Climbing with no acceleration: far away"), URiderAttitudeComponent::ComputeTimeToContact(300.0f, 200.0f, 0.0f) > 100.0f);

	// Too high: the window is closed, nothing happens.
	{
		URiderAttitudeComponent* A = MakeAttitude();
		A->SetState(FQuat(AxisB, -FMath::DegreesToRadians(30.0f)), FVector::ZeroVector);
		FAttitudeInputs In = Air();
		In.HeightAboveWaterCm = 5000.0f;
		In.VelocityCmS = FVector(0.0, 800.0, -100.0);
		In.VerticalAccelCmS2 = -KiteUnits::GravityCmS2;
		A->Step(Step240, In);
		TestTrue(TEXT("Time to contact over the window"), A->GetLastStepDebug().TimeToContactSeconds > A->AssistWindowSeconds);
		TestFalse(TEXT("Outside the window: assist off"), A->GetLastStepDebug().bAssistActive);
		TestTrue(TEXT("Outside the window: no assist torque"), A->GetLastStepDebug().AssistTorqueNm.IsZero());
	}

	// Too far from any valid attitude: the assist does not try.
	{
		URiderAttitudeComponent* A = MakeAttitude();
		A->SetState(FQuat(AxisB, -FMath::DegreesToRadians(100.0f)), FVector::ZeroVector);
		FAttitudeInputs In = Air();
		In.HeightAboveWaterCm = 100.0f;
		In.VelocityCmS = FVector(0.0, 800.0, -600.0);
		In.VerticalAccelCmS2 = -KiteUnits::GravityCmS2;
		A->Step(Step240, In);
		TestTrue(TEXT("Error over the limit"), A->GetLastStepDebug().LandingErrorDeg > A->AssistMaxErrorDeg);
		TestFalse(TEXT("Beyond the error limit: assist off"), A->GetLastStepDebug().bAssistActive);
	}

	// Yawed 160 deg from one valid landing is 20 deg from the other (switch): it turns that way.
	{
		URiderAttitudeComponent* A = MakeAttitude();
		A->SetState(FQuat(FVector::UpVector, FMath::DegreesToRadians(160.0f)), FVector::ZeroVector);
		FAttitudeInputs In = Air();
		In.HeightAboveWaterCm = 200.0f;
		In.VelocityCmS = FVector(0.0, 800.0, -500.0);
		In.VerticalAccelCmS2 = -KiteUnits::GravityCmS2;
		FAttitudeDebug Probe;
		A->ComputeAssistTorque(In, Probe);
		TestTrue(TEXT("Nearest valid attitude is 20 deg away, not 160"), FMath::IsNearlyEqual(Probe.LandingErrorDeg, 20.0f, 0.5f));
		TestTrue(TEXT("Assist active for a 20 deg yaw error"), Probe.bAssistActive);
		const double YawRateSign = Probe.AssistTorqueNm | FVector::UpVector;
		for (int32 I = 0; I < 72; ++I) { A->Step(Step240, In); }
		A->ComputeAssistTorque(In, Probe);
		TestTrue(TEXT("Assist turns forward to 180 deg (positive yaw torque)"), YawRateSign > 0.0);
		TestTrue(TEXT("Error shrinks towards the nearest attitude"), Probe.LandingErrorDeg < 15.0f);
	}

	// Assist strength 0 is off whatever the state.
	{
		URiderAttitudeComponent* A = MakeAttitude();
		A->AssistStrength = 0.0f;
		A->SetState(FQuat(AxisB, -FMath::DegreesToRadians(20.0f)), FVector::ZeroVector);
		FAttitudeInputs In = Air();
		In.HeightAboveWaterCm = 100.0f;
		In.VelocityCmS = FVector(0.0, 800.0, -600.0);
		A->Step(Step240, In);
		TestFalse(TEXT("Strength 0: assist off"), A->GetLastStepDebug().bAssistActive);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickStepSizeIndependent, "KiteSurf.Trick.StepSizeIndependent", TrickAttitudeTest::Flags)

bool FKiteSurfTrickStepSizeIndependent::RunTest(const FString& Parameters)
{
	// The plan's script: pre-wind back roll, tuck from 0.6 s, stick 0.5 from 0.5 to 1.0 s, 600 N
	// straight up, 2 s, at three fixed steps.
	const float Dts[3] = {1.0f / 120.0f, 1.0f / 240.0f, 1.0f / 480.0f};
	FQuat Final[3];
	double OmegaSize[3];
	for (int32 K = 0; K < 3; ++K)
	{
		const float Dt = Dts[K];
		URiderAttitudeComponent* A = MakeAttitude();
		FAttitudeInputs In = Air(FVector(0.0, 0.0, 600.0));
		AddBackRollPreWind(In, 1.0f);
		const int32 Steps = FMath::RoundToInt(2.0f / Dt);
		const int32 StickOn = FMath::RoundToInt(0.5f / Dt);
		const int32 StickOff = FMath::RoundToInt(1.0f / Dt);
		const int32 TuckOn = FMath::RoundToInt(0.6f / Dt);
		for (int32 I = 0; I < Steps; ++I)
		{
			In.bRotationInput = I >= StickOn && I < StickOff;
			In.RotationStick = In.bRotationInput ? FVector2D(0.5f, 0.0f) : FVector2D::ZeroVector;
			In.Tuck = I >= TuckOn ? 1.0f : 0.0f;
			A->Step(Dt, In);
		}
		Final[K] = A->GetBodyQuat();
		OmegaSize[K] = A->GetAngularVelocity().Size();
	}
	for (int32 I = 0; I < 3; ++I)
	{
		for (int32 J = I + 1; J < 3; ++J)
		{
			const double AngleDeg = FMath::RadiansToDegrees(Final[I].AngularDistance(Final[J]));
			const double RateDiff = FMath::Abs(OmegaSize[I] / OmegaSize[J] - 1.0);
			AddInfo(FString::Printf(TEXT("dt %.5f vs %.5f: %.3f deg apart, |w| %.3f%% apart"), Dts[I], Dts[J], AngleDeg, 100.0 * RateDiff));
			TestTrue(TEXT("Final body orientation within 2 deg"), AngleDeg < 2.0);
			TestTrue(TEXT("Final |w| within 2%"), RateDiff < 0.02);
		}
	}

	// The calibration scenario agrees across step sizes too.
	const FBackRollResult R120 = RunBackRoll(1.0f / 120.0f, 3.0f);
	const FBackRollResult R480 = RunBackRoll(1.0f / 480.0f, 3.0f);
	TestEqual(TEXT("Back roll inversions at 1/120 and 1/480 agree"), R120.Inversions, R480.Inversions);
	TestTrue(TEXT("Back roll duration at 1/120 and 1/480 within 0.03 s"), FMath::Abs(R120.DurationSeconds - R480.DurationSeconds) < 0.03f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSlackBurstsStayStable, "KiteSurf.Trick.SlackBurstsStayStable", TrickAttitudeTest::Flags)

bool FKiteSurfTrickSlackBurstsStayStable::RunTest(const FString& Parameters)
{
	// A storm jump (docs/physics/storm-jumps.md): 8 s in the air, the lines snapping between slack
	// and taut every 0.2 s with up to 12 kN in random directions, then a 30 kN snatch.
	URiderAttitudeComponent* A = MakeAttitude();
	FAttitudeInputs In = Air();
	AddBackRollPreWind(In, 1.0f);
	FRandomStream Rng(46);
	double MaxOmega = 0.0;
	double MaxSnatchOmega = 0.0;
	double MaxL = 0.0;
	bool bSlackTorque = false;
	bool bNaN = false;
	const int32 Steps = 8 * 240;
	for (int32 I = 0; I < Steps; ++I)
	{
		const float T = I * Step240;
		In.bLinesTaut = (FMath::FloorToInt(T / 0.2f) % 2) == 0;
		FVector ForceN(Rng.FRandRange(-5000.0f, 5000.0f), Rng.FRandRange(-5000.0f, 5000.0f), Rng.FRandRange(2000.0f, 12000.0f));
		if (I > Steps - 240)
		{
			In.bLinesTaut = true;
			ForceN = FVector(0.0, 0.0, 30000.0);
		}
		In.LineForceUU = ForceN * KiteUnits::UnrealForcePerN;
		A->Step(Step240, In);
		const FAttitudeDebug& D = A->GetLastStepDebug();
		bSlackTorque |= !In.bLinesTaut && !D.LineTorqueNm.IsZero();
		bNaN |= A->GetBodyQuat().ContainsNaN() || A->GetAngularMomentum().ContainsNaN() || A->GetAngularVelocity().ContainsNaN();
		MaxOmega = FMath::Max(MaxOmega, A->GetAngularVelocity().Size());
		if (I > Steps - 240) { MaxSnatchOmega = FMath::Max(MaxSnatchOmega, A->GetAngularVelocity().Size()); }
		MaxL = FMath::Max(MaxL, A->GetAngularMomentum().Size());
	}
	AddInfo(FString::Printf(TEXT("Storm: max |w| %.2f rad/s, during the snatch %.2f rad/s, max |L| %.1f kg*m^2/s, final |w| %.3f rad/s"),
		MaxOmega, MaxSnatchOmega, MaxL, A->GetAngularVelocity().Size()));
	TestFalse(TEXT("No NaN through slack bursts"), bNaN);
	TestFalse(TEXT("Slack lines give no line torque"), bSlackTorque);
	TestTrue(TEXT("Angular velocity stays bounded (< 25 rad/s)"), MaxOmega < 25.0);
	TestTrue(TEXT("Body quaternion stays normalised"), FMath::IsNearlyEqual(A->GetBodyQuat().Size(), 1.0, 1.0e-4));
	TestTrue(TEXT("A 30 kN snatch swings the rider without blowing up (|w| < 10 rad/s)"), MaxSnatchOmega < 10.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickSlavedOnWaterAndStrappedBoard, "KiteSurf.Trick.SlavedOnWaterAndStrappedBoard", TrickAttitudeTest::Flags)

bool FKiteSurfTrickSlavedOnWaterAndStrappedBoard::RunTest(const FString& Parameters)
{
	URiderAttitudeComponent* A = MakeAttitude();

	// On the water the attitude copies the kinematic pose; the board keeps its heel and pitch.
	const FQuat Lean = FQuat(FVector(1.0, 0.0, 0.0), FMath::DegreesToRadians(-25.0f)) * FQuat(FVector::UpVector, 0.4f);
	const FQuat Heel = FQuat(FVector(0.0, 1.0, 0.0), FMath::DegreesToRadians(10.0f));
	const FQuat Board = StrappedBoard(Lean) * Heel;
	FAttitudeInputs Water;
	Water.bAirborne = false;
	Water.SlavedBodyQuat = Lean;
	Water.SlavedBoardQuat = Board;
	A->Step(Step240, Water);
	TestFalse(TEXT("On the water: inactive"), A->IsSimulating());
	TestTrue(TEXT("On the water: body is the slaved pose"), A->GetBodyQuat().AngularDistance(Lean) < 1.0e-4);
	TestTrue(TEXT("On the water: board is the slaved board"), A->GetBoardQuat().AngularDistance(Board) < 1.0e-4);
	TestTrue(TEXT("On the water: no angular momentum"), A->GetAngularMomentum().IsZero());
	TestTrue(TEXT("On the water: visual board at the root"),
		(A->GetVisualComOffsetCm(1.0f) - A->GetBodyQuat().RotateVector(FVector(0.0, 0.0, A->ComAboveBoardCm))).Size() < 1.0e-3);

	// Take-off with no pre-wind and no line: no pop, the board eases flat under the feet.
	FAttitudeInputs In = Air(FVector::ZeroVector, Lean);
	In.SlavedBoardQuat = Board;
	A->Step(Step240, In);
	TestTrue(TEXT("Take-off: active"), A->IsSimulating());
	TestTrue(TEXT("Take-off: no visual pop (< 0.01 deg)"), FMath::RadiansToDegrees(A->GetBodyQuat().AngularDistance(Lean)) < 0.01);
	TestTrue(TEXT("Take-off: render body continuous with the water pose"),
		FMath::RadiansToDegrees(A->GetRenderBodyQuat(0.0f).AngularDistance(Lean)) < 0.01);
	for (int32 I = 0; I < 3 * 240; ++I) { A->Step(Step240, In); }
	const FQuat Canonical = URiderAttitudeComponent::MakeCanonicalStrapOffset(-1.0f);
	TestTrue(TEXT("In the air the board settles flat under the feet (< 0.5 deg)"),
		FMath::RadiansToDegrees(A->GetStrapOffset().AngularDistance(Canonical)) < 0.5);
	// 1e-4 rad: arm64 fuses multiply-adds, which leaves the product ~3e-5 rad off there.
	const double BoardRad = A->GetBoardQuat().AngularDistance(A->GetBodyQuat() * A->GetStrapOffset());
	TestTrue(FString::Printf(TEXT("Board quat is body times strap offset (%.3g rad apart)"), BoardRad), BoardRad < 1.0e-4);
	TestTrue(TEXT("The board's nose lies along -Right (stance side +1)"),
		(A->GetBoardQuat().GetAxisX() | -A->GetBodyQuat().GetAxisY()) > 0.999);
	TestTrue(TEXT("Visual centre of mass settles straight above the board"),
		(A->GetVisualComOffsetCm(1.0f) - FVector(0.0, 0.0, A->ComAboveBoardCm)).Size() < 1.0);

	// Reset puts it back on the water pose with nothing stored.
	A->Reset(UprightBody(), StrappedBoard(UprightBody()));
	TestFalse(TEXT("Reset: inactive"), A->IsSimulating());
	TestTrue(TEXT("Reset: no angular momentum"), A->GetAngularMomentum().IsZero());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
