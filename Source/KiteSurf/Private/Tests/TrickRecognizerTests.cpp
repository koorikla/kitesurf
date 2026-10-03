#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "KiteSurfHUD.h"
#include "KiteSurfUnits.h"
#include "Math/RandomStream.h"
#include "Tricks/JumpRecord.h"
#include "Tricks/JumpRecorder.h"
#include "Tricks/RiderAxes.h"
#include "Tricks/RotationRecognizer.h"
#include "Tricks/TrickNaming.h"
#include "Tricks/TrickRecognition.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests on the pure rotation recogniser (T1.6, docs/tricks.md 6.6) and the record fields it fills:
// synthetic angular-velocity streams, no world. Also the recorder carrying the rotation and the
// landing cause into the record, the names that come out, and the trick card's cause line (T2.6).

namespace TrickRecognizerTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/**
	 * A rider riding heelside along +X, facing the kite on -Y: Front = -Y, Up = +Z, so Right = Up x
	 * Front = +X points along the travel and Sigma = +1. A back roll turns the chest towards -X.
	 */
	FQuat RidingBody()
	{
		return FRotationMatrix::MakeFromXZ(FVector(0.0f, -1.0f, 0.0f), FVector::UpVector).ToQuat();
	}

	const FVector TravelVelocity(1200.0f, 0.0f, 0.0f);

	/** A recogniser begun on RidingBody, travelling along +X. */
	FRotationRecognizer Begun(const FQuat& Body0)
	{
		FRotationRecognizer Recognizer;
		Recognizer.Begin(FRotationTakeoffFrame::Make(Body0, TravelVelocity, FVector::ForwardVector), Body0);
		return Recognizer;
	}

	/**
	 * Turns the body AngleDeg about a world axis at RateDegS, stepping the recogniser every Dt with the
	 * exact orientation and the constant angular velocity. Returns the body at the end. The last step
	 * is shortened so the angle comes out exact.
	 */
	FQuat Rotate(FRotationRecognizer& Recognizer, FQuat Body, const FVector& AxisWorld, float AngleDeg, float RateDegS, float Dt)
	{
		const FVector Axis = AxisWorld.GetSafeNormal();
		const float Sign = AngleDeg >= 0.0f ? 1.0f : -1.0f;
		const float RateRad = FMath::DegreesToRadians(RateDegS) * Sign;
		float Left = FMath::Abs(AngleDeg) / RateDegS;
		while (Left > 1e-6f)
		{
			const float Step = FMath::Min(Dt, Left);
			Left -= Step;
			Body = (FQuat(Axis, RateRad * Step) * Body).GetNormalized();
			Recognizer.Step(Body, Axis * RateRad, Step, TravelVelocity);
		}
		return Body;
	}

	/** Holds still for Seconds (no rotation). */
	void Hold(FRotationRecognizer& Recognizer, const FQuat& Body, float Seconds, float Dt)
	{
		for (float T = 0.0f; T < Seconds - 1e-6f; T += Dt)
		{
			Recognizer.Step(Body, FVector::ZeroVector, Dt, TravelVelocity);
		}
	}

	/** World axis of a body-frame axis on the riding body. */
	FVector World(const FVector& AxisBody)
	{
		return RidingBody().RotateVector(AxisBody);
	}

	FString Kinds(const FRotationResult& R)
	{
		FString Text;
		for (const FRecognizedInversion& Inversion : R.Inversions)
		{
			Text += (Text.IsEmpty() ? TEXT("") : TEXT(", ")) + UEnum::GetValueAsString(Inversion.Kind);
		}
		return Text.IsEmpty() ? FString(TEXT("none")) : Text;
	}

	FString Describe(const FRotationResult& R)
	{
		return FString::Printf(TEXT("inversions [%s], spin %.1f deg -> %d half turns %s, heading %.1f deg, tilt %.1f deg, landed %s, roll start %.3f s"),
			*Kinds(R), R.SpinDeg, R.SpinHalfTurns, *UEnum::GetValueAsString(R.SpinSense), R.NetHeadingDeg, R.LandingTiltDeg,
			*UEnum::GetValueAsString(R.LandingStance), R.RollStartSeconds);
	}

	/** A record holding a rotation result, landed clean, for naming. */
	FJumpRecord RecordWith(const FRotationResult& R)
	{
		FJumpRecord Record;
		Record.bRotationTracked = true;
		Record.Inversions = R.InversionKinds();
		Record.SpinHalfTurns = R.SpinHalfTurns;
		Record.SpinSense = R.SpinSense;
		Record.LandingStance = R.LandingStance;
		Record.RollStartSinceTakeoffSeconds = R.RollStartSeconds;
		Record.LandingG = 2.0f;
		Record.KiteElevationAtLandingDeg = 60.0f;
		return Record;
	}

	FString NameOf(const FRotationResult& R)
	{
		return TrickNaming::Name(TrickRecognition::SignatureFromJump(RecordWith(R)));
	}

	/** The back roll's world axis for the riding body (Sigma +1, the default 65 deg tilt). */
	FVector BackRollAxis(float TiltDeg = 65.0f)
	{
		return World(RiderAxes::BackRollAxisBody(1.0f, TiltDeg));
	}

	/** A spin about world up: backside is about -Sigma * U, so -Z here. */
	FVector SpinAxis(bool bBackside)
	{
		return bBackside ? -FVector::UpVector : FVector::UpVector;
	}
}


// One back roll, a double, front roll, flips: counted with the doc's hysteresis and classed by the
// body-frame axis, back or front by RiderAxes' conventions.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRecognizerCountsInversions, "KiteSurf.Trick.RecognizerCountsInversions", TrickRecognizerTest::Flags)

bool FKiteSurfTrickRecognizerCountsInversions::RunTest(const FString& Parameters)
{
	using namespace TrickRecognizerTest;
	const float Dt = 1.0f / 240.0f;
	const FQuat Body0 = RidingBody();

	struct FCase
	{
		const TCHAR* Name;
		FVector Axis;
		float AngleDeg;
		TArray<ETrickInversion> Expected;
		const TCHAR* ExpectedName;
	};
	const TArray<FCase> Cases = {
		{ TEXT("Back roll 360 (65 deg tilt)"), BackRollAxis(), 360.0f, { ETrickInversion::BackRoll }, TEXT("Back roll") },
		{ TEXT("Back roll 360 (90 deg tilt, a cartwheel)"), BackRollAxis(90.0f), 360.0f, { ETrickInversion::BackRoll }, TEXT("Back roll") },
		{ TEXT("Back roll 360 (110 deg tilt, stick down)"), BackRollAxis(110.0f), 360.0f, { ETrickInversion::BackRoll }, TEXT("Back roll") },
		{ TEXT("Double back roll 720"), BackRollAxis(), 720.0f, { ETrickInversion::BackRoll, ETrickInversion::BackRoll }, TEXT("Double back roll") },
		{ TEXT("Triple back roll 1080"), BackRollAxis(), 1080.0f, { ETrickInversion::BackRoll, ETrickInversion::BackRoll, ETrickInversion::BackRoll }, TEXT("Triple back roll") },
		{ TEXT("Front roll 360"), -BackRollAxis(), 360.0f, { ETrickInversion::FrontRoll }, TEXT("Front roll") },
		{ TEXT("Front flip 360"), World(RiderAxes::FrontFlipAxisBody()), 360.0f, { ETrickInversion::FrontFlip }, TEXT("Front flip") },
		{ TEXT("Backflip 360"), World(RiderAxes::BackFlipAxisBody()), 360.0f, { ETrickInversion::BackFlip }, TEXT("Backflip") },
	};
	for (const FCase& Case : Cases)
	{
		FRotationRecognizer Recognizer = Begun(Body0);
		Hold(Recognizer, Body0, 0.25f, Dt);
		const FQuat Land = Rotate(Recognizer, Body0, Case.Axis, Case.AngleDeg, 240.0f, Dt);
		Hold(Recognizer, Land, 0.25f, Dt);
		const FRotationResult R = Recognizer.Finish(Land);
		AddInfo(FString::Printf(TEXT("%s: %s"), Case.Name, *Describe(R)));
		TestEqual(FString::Printf(TEXT("%s: inversions"), Case.Name), Kinds(R), Kinds([&Case]
		{
			FRotationResult Expected;
			for (const ETrickInversion Kind : Case.Expected) { Expected.Inversions.AddDefaulted_GetRef().Kind = Kind; }
			return Expected;
		}()));
		TestEqual(FString::Printf(TEXT("%s: no spin credited"), Case.Name), R.SpinHalfTurns, 0);
		TestEqual(FString::Printf(TEXT("%s: lands heelside"), Case.Name), R.LandingStance, ETrickStance::Heelside);
		TestTrue(FString::Printf(TEXT("%s: the roll start is when it began turning (%.3f s)"), Case.Name, R.RollStartSeconds),
			FMath::IsNearlyEqual(R.RollStartSeconds, 0.25f, Dt + 1e-4f));
		TestEqual(FString::Printf(TEXT("%s: named"), Case.Name), NameOf(R), FString(Case.ExpectedName));
	}

	// A back roll is "chest to the tail first": the recogniser's back roll axis must agree.
	{
		const FQuat Tenth = FQuat(BackRollAxis(), FMath::DegreesToRadians(10.0f)) * Body0;
		TestTrue(TEXT("The back roll axis turns the chest towards the tail (-travel) first"), ((Tenth.GetAxisX() - Body0.GetAxisX()) | -TravelVelocity.GetSafeNormal()) > 0.0f);
	}

	// Taking off from a hang lean (30 deg back from the kite, Up tipped towards +Y) still counts one back roll.
	{
		const FQuat Leaning = FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-30.0f)) * Body0;
		TestTrue(TEXT("The hang lean tips Up away from the kite"), Leaning.GetAxisZ().Y > 0.4f);
		FRotationRecognizer Recognizer = Begun(Leaning);
		const FQuat Land = Rotate(Recognizer, Leaning, Leaning.RotateVector(RiderAxes::BackRollAxisBody(1.0f, 65.0f)), 360.0f, 200.0f, Dt);
		const FRotationResult R = Recognizer.Finish(Land);
		AddInfo(FString::Printf(TEXT("Back roll from a hang lean: %s"), *Describe(R)));
		TestEqual(TEXT("From a hang lean: one back roll"), Kinds(R), FString(TEXT("ETrickInversion::BackRoll")));
	}

	// The other travel side (Sigma -1): the mirrored back roll is still a back roll.
	{
		const FQuat Mirrored = FRotationMatrix::MakeFromXZ(FVector(0.0f, 1.0f, 0.0f), FVector::UpVector).ToQuat(); // Right = -X
		FRotationRecognizer Recognizer = Begun(Mirrored);
		TestEqual(TEXT("Riding the other way round the frame has Sigma -1"), Recognizer.GetFrame().Sigma, -1.0f);
		const FQuat Land = Rotate(Recognizer, Mirrored, Mirrored.RotateVector(RiderAxes::BackRollAxisBody(-1.0f, 65.0f)), 360.0f, 240.0f, Dt);
		TestEqual(TEXT("Sigma -1: one back roll"), Kinds(Recognizer.Finish(Land)), FString(TEXT("ETrickInversion::BackRoll")));
	}
	return true;
}

// Flat spins: half turns snapped as floor((|spin| + 45) / 180), the sense from Sigma, the landing
// stance from the net heading at touchdown.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRecognizerCountsSpins, "KiteSurf.Trick.RecognizerCountsSpins", TrickRecognizerTest::Flags)

bool FKiteSurfTrickRecognizerCountsSpins::RunTest(const FString& Parameters)
{
	using namespace TrickRecognizerTest;
	const float Dt = 1.0f / 240.0f;
	const FQuat Body0 = RidingBody();
	struct FCase
	{
		const TCHAR* Name;
		bool bBackside;
		float AngleDeg;
		int32 HalfTurns;
		ETrickStance Stance;
		const TCHAR* ExpectedName;
	};
	const FCase Cases[] = {
		{ TEXT("BS 360"), true, 360.0f, 2, ETrickStance::Heelside, TEXT("Backside 360") },
		{ TEXT("FS 360"), false, 360.0f, 2, ETrickStance::Heelside, TEXT("Frontside 360") },
		{ TEXT("BS 180"), true, 180.0f, 1, ETrickStance::Blind, TEXT("Backside 180 to blind") },
		{ TEXT("FS 180"), false, 180.0f, 1, ETrickStance::Toeside, TEXT("Frontside 180 to toeside") },
		{ TEXT("BS 540"), true, 540.0f, 3, ETrickStance::Blind, TEXT("Backside 540 to blind") },
		{ TEXT("FS 540"), false, 540.0f, 3, ETrickStance::Toeside, TEXT("Frontside 540 to toeside") },
		{ TEXT("BS 720"), true, 720.0f, 4, ETrickStance::Heelside, TEXT("Backside 720") },
		{ TEXT("140 deg counts as a 180"), true, 140.0f, 1, ETrickStance::Blind, nullptr },
		{ TEXT("120 deg landing blind: the landing makes it a 180"), true, 120.0f, 1, ETrickStance::Blind, nullptr },
		{ TEXT("80 deg lands facing the kite: none"), true, 80.0f, 0, ETrickStance::Heelside, nullptr },
		{ TEXT("300 deg landing facing the kite: the landing makes it a 360"), true, 300.0f, 2, ETrickStance::Heelside, nullptr },
		{ TEXT("320 deg counts as a 360"), true, 320.0f, 2, ETrickStance::Heelside, nullptr },
	};
	for (const FCase& Case : Cases)
	{
		FRotationRecognizer Recognizer = Begun(Body0);
		const FQuat Land = Rotate(Recognizer, Body0, SpinAxis(Case.bBackside), Case.AngleDeg, 360.0f, Dt);
		const FRotationResult R = Recognizer.Finish(Land);
		AddInfo(FString::Printf(TEXT("%s: %s"), Case.Name, *Describe(R)));
		TestEqual(FString::Printf(TEXT("%s: no inversion"), Case.Name), R.Inversions.Num(), 0);
		TestEqual(FString::Printf(TEXT("%s: half turns"), Case.Name), R.SpinHalfTurns, Case.HalfTurns);
		if (Case.HalfTurns > 0)
		{
			TestEqual(FString::Printf(TEXT("%s: sense"), Case.Name), R.SpinSense, Case.bBackside ? ETrickSense::Backside : ETrickSense::Frontside);
		}
		TestEqual(FString::Printf(TEXT("%s: landing stance"), Case.Name), R.LandingStance, Case.Stance);
		TestNearlyEqual(FString::Printf(TEXT("%s: spin (deg)"), Case.Name), FMath::Abs(R.SpinDeg), Case.AngleDeg, 0.01f);
		if (Case.ExpectedName)
		{
			TestEqual(FString::Printf(TEXT("%s: named"), Case.Name), NameOf(R), FString(Case.ExpectedName));
		}
	}

	TestEqual(TEXT("Snap 0"), FRotationRecognizer::SnapHalfTurns(0.0f), 0);
	TestEqual(TEXT("Snap 134.9"), FRotationRecognizer::SnapHalfTurns(134.9f), 0);
	TestEqual(TEXT("Snap 135"), FRotationRecognizer::SnapHalfTurns(135.0f), 1);
	TestEqual(TEXT("Snap -360"), FRotationRecognizer::SnapHalfTurns(-360.0f), 2);
	TestEqual(TEXT("Snap NaN"), FRotationRecognizer::SnapHalfTurns(NAN), 0);

	// Back roll to blind: a back roll, then a further backside 180 the same way.
	{
		FRotationRecognizer Recognizer = Begun(Body0);
		FQuat Body = Rotate(Recognizer, Body0, BackRollAxis(), 360.0f, 240.0f, Dt);
		Body = Rotate(Recognizer, Body, SpinAxis(true), 180.0f, 360.0f, Dt);
		const FRotationResult R = Recognizer.Finish(Body);
		AddInfo(FString::Printf(TEXT("Back roll to blind: %s"), *Describe(R)));
		TestEqual(TEXT("Back roll to blind: one back roll"), Kinds(R), FString(TEXT("ETrickInversion::BackRoll")));
		TestEqual(TEXT("Back roll to blind: one backside half turn"), R.SpinHalfTurns, 1);
		TestEqual(TEXT("Back roll to blind: backside"), R.SpinSense, ETrickSense::Backside);
		TestEqual(TEXT("Back roll to blind: lands blind"), R.LandingStance, ETrickStance::Blind);
		TestEqual(TEXT("Back roll to blind: named"), NameOf(R), FString(TEXT("Back roll backside 180 to blind")));
	}

	// A back roll that ends facing away after turning the other way about up (a rider leaning far back
	// can roll with either sign about U): still a back roll to blind, the roll's sense.
	{
		FRotationRecognizer Recognizer = Begun(Body0);
		FQuat Body = Rotate(Recognizer, Body0, BackRollAxis(), 360.0f, 240.0f, Dt);
		Body = Rotate(Recognizer, Body, SpinAxis(false), 180.0f, 360.0f, Dt);
		const FRotationResult R = Recognizer.Finish(Body);
		AddInfo(FString::Printf(TEXT("Back roll then a frontside half turn: %s"), *Describe(R)));
		TestEqual(TEXT("Back roll, facing away: backside"), R.SpinSense, ETrickSense::Backside);
		TestEqual(TEXT("Back roll, facing away: blind"), R.LandingStance, ETrickStance::Blind);
	}
	// A front roll that lands facing away: frontside, toeside.
	{
		FRotationRecognizer Recognizer = Begun(Body0);
		FQuat Body = Rotate(Recognizer, Body0, -BackRollAxis(), 360.0f, 240.0f, Dt);
		Body = Rotate(Recognizer, Body, SpinAxis(false), 180.0f, 360.0f, Dt);
		const FRotationResult R = Recognizer.Finish(Body);
		TestEqual(TEXT("Front roll to toeside: one front roll"), Kinds(R), FString(TEXT("ETrickInversion::FrontRoll")));
		TestEqual(TEXT("Front roll to toeside: frontside half turn"), R.SpinSense, ETrickSense::Frontside);
		TestEqual(TEXT("Front roll to toeside: toeside"), R.LandingStance, ETrickStance::Toeside);
		TestEqual(TEXT("Front roll to toeside: named"), NameOf(R), FString(TEXT("Front roll frontside 180 to toeside")));
	}

	// The flight's own turn is not a spin: a body turning 160 deg with a flight that turned 160 deg
	// the same way (the travel align following the kite's pull) lands heelside with no spin.
	{
		FRotationRecognizer Recognizer = Begun(Body0);
		FQuat Body = Body0;
		const int32 Steps = 240;
		const float TurnRad = FMath::DegreesToRadians(-160.0f);
		for (int32 I = 1; I <= Steps; ++I)
		{
			const float Angle = TurnRad * I / Steps;
			Body = FQuat(FVector::UpVector, Angle) * Body0;
			const FVector Velocity = FQuat(FVector::UpVector, Angle).RotateVector(TravelVelocity);
			Recognizer.Step(Body, FVector::UpVector * (TurnRad / (Steps * Dt)), Dt, Velocity);
		}
		const FRotationResult R = Recognizer.Finish(Body);
		AddInfo(FString::Printf(TEXT("Following a 160 deg flight turn: %s, flight %.1f deg"), *Describe(R), R.FlightTurnDeg));
		TestNearlyEqual(TEXT("Following the flight: the flight turned -160 deg"), R.FlightTurnDeg, -160.0f, 0.1f);
		TestEqual(TEXT("Following the flight: no spin"), R.SpinHalfTurns, 0);
		TestEqual(TEXT("Following the flight: heelside"), R.LandingStance, ETrickStance::Heelside);
	}

	// A turn that follows a flight turning the other way is a spin against the flight: 100 deg of
	// body turn against a flight that turned 80 deg the other way lands facing away, a 180.
	{
		FRotationRecognizer Recognizer = Begun(Body0);
		FQuat Body = Body0;
		const int32 Steps = 240;
		const float BodyRad = FMath::DegreesToRadians(-100.0f);
		const float FlightRad = FMath::DegreesToRadians(80.0f);
		for (int32 I = 1; I <= Steps; ++I)
		{
			Body = FQuat(FVector::UpVector, BodyRad * I / Steps) * Body0;
			const FVector Velocity = FQuat(FVector::UpVector, FlightRad * I / Steps).RotateVector(TravelVelocity);
			Recognizer.Step(Body, FVector::UpVector * (BodyRad / (Steps * Dt)), Dt, Velocity);
		}
		const FRotationResult R = Recognizer.Finish(Body);
		AddInfo(FString::Printf(TEXT("Body -100 against a +80 flight: %s, flight %.1f deg"), *Describe(R), R.FlightTurnDeg));
		TestNearlyEqual(TEXT("Against the flight: 180 deg relative to it"), FMath::Abs(R.SpinDeg), 180.0f, 0.5f);
		TestEqual(TEXT("Against the flight: a backside 180"), R.SpinHalfTurns, 1);
		TestEqual(TEXT("Against the flight: backside"), R.SpinSense, ETrickSense::Backside);
		TestEqual(TEXT("Against the flight: blind"), R.LandingStance, ETrickStance::Blind);
	}
	return true;
}

// Wobbles and noise do not count: the hysteresis needs Up below -0.3 after being above +0.3, and a
// spin needs 135 deg.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRecognizerIgnoresNoise, "KiteSurf.Trick.RecognizerIgnoresNoise", TrickRecognizerTest::Flags)

bool FKiteSurfTrickRecognizerIgnoresNoise::RunTest(const FString& Parameters)
{
	using namespace TrickRecognizerTest;
	const float Dt = 1.0f / 240.0f;
	const FQuat Body0 = RidingBody();

	// A +-100 deg wobble about the front, twice: Up dips to cos(100) = -0.17, past -0.2's neighbourhood but not -0.3.
	{
		FRotationRecognizer Recognizer = Begun(Body0);
		const FVector Axis = Body0.GetAxisX();
		const float Period = 1.0f;
		const float AmpRad = FMath::DegreesToRadians(100.0f);
		FQuat Body = Body0;
		double MinUp = 1.0;
		for (float T = Dt; T <= 2.0f * Period + 1e-4f; T += Dt)
		{
			const float Angle = AmpRad * FMath::Sin(2.0f * PI * T / Period);
			const float Rate = AmpRad * 2.0f * PI / Period * FMath::Cos(2.0f * PI * T / Period);
			Body = FQuat(Axis, Angle) * Body0;
			MinUp = FMath::Min(MinUp, static_cast<double>(Body.GetAxisZ().Z));
			Recognizer.Step(Body, Axis * Rate, Dt, TravelVelocity);
		}
		const FRotationResult R = Recognizer.Finish(Body);
		AddInfo(FString::Printf(TEXT("Wobble: lowest up %.3f; %s"), MinUp, *Describe(R)));
		TestTrue(FString::Printf(TEXT("The wobble went past horizontal (up %.2f)"), MinUp), MinUp < -0.15);
		TestEqual(TEXT("A +-100 deg wobble is no inversion"), R.Inversions.Num(), 0);
		TestEqual(TEXT("and no spin"), R.SpinHalfTurns, 0);
	}

	// Random angular velocity noise up to 60 deg/s on every axis for 4 s.
	{
		FRandomStream Random(1234);
		FRotationRecognizer Recognizer = Begun(Body0);
		FQuat Body = Body0;
		for (float T = 0.0f; T < 4.0f; T += Dt)
		{
			const FVector Omega = FVector(Random.FRandRange(-1.0f, 1.0f), Random.FRandRange(-1.0f, 1.0f), Random.FRandRange(-1.0f, 1.0f)) * FMath::DegreesToRadians(60.0f);
			Body = (FQuat(Omega.GetSafeNormal(), Omega.Size() * Dt) * Body).GetNormalized();
			Recognizer.Step(Body, Omega, Dt, TravelVelocity);
		}
		const FRotationResult R = Recognizer.Finish(Body);
		AddInfo(FString::Printf(TEXT("Noise: %s"), *Describe(R)));
		TestEqual(TEXT("Noise: no inversion"), R.Inversions.Num(), 0);
		TestEqual(TEXT("Noise: no spin"), R.SpinHalfTurns, 0);
		TestEqual(TEXT("Noise: heelside"), R.LandingStance, ETrickStance::Heelside);
		TestTrue(TEXT("Noise: no roll start"), R.RollStartSeconds < 0.0f);
	}

	// Taking off already past the arming dot (a rider thrown off tilted 80 deg) does not count the rest of that fall.
	{
		const FQuat Tilted = FQuat(Body0.GetAxisX(), FMath::DegreesToRadians(-80.0f)) * Body0;
		FRotationRecognizer Recognizer = Begun(Tilted);
		const FQuat Land = Rotate(Recognizer, Tilted, Body0.GetAxisX(), -60.0f, 60.0f, Dt);
		TestEqual(TEXT("Starting tilted and falling further: no inversion"), Recognizer.Finish(Land).Inversions.Num(), 0);
	}

	// Swing-twist at 179.9 deg of tilt, and at exactly 180: finite, with the guard.
	{
		for (const float TiltDeg : { 179.9f, 180.0f })
		{
			const FQuat Q(FVector::ForwardVector, FMath::DegreesToRadians(TiltDeg));
			FQuat Swing;
			FQuat Twist;
			FRotationRecognizer::SwingTwist(Q, FVector::UpVector, Swing, Twist);
			TestFalse(FString::Printf(TEXT("Swing-twist at %.1f deg: finite"), TiltDeg), Swing.ContainsNaN() || Twist.ContainsNaN());
			TestTrue(FString::Printf(TEXT("Swing-twist at %.1f deg: Swing * Twist is the rotation"), TiltDeg), (Swing * Twist).Equals(Q, 1e-4f) || (Swing * Twist).Equals(Q * -1.0f, 1e-4f));
			FRotationRecognizer Recognizer = Begun(Body0);
			Hold(Recognizer, Body0, 0.1f, Dt);
			const FRotationResult R = Recognizer.Finish(Q * Body0);
			AddInfo(FString::Printf(TEXT("Landing %.1f deg tilted: %s"), TiltDeg, *Describe(R)));
			TestTrue(FString::Printf(TEXT("Landing %.1f deg tilted: a finite heading and tilt"), TiltDeg), FMath::IsFinite(R.NetHeadingDeg) && FMath::IsFinite(R.LandingTiltDeg));
			TestTrue(FString::Printf(TEXT("Landing %.1f deg tilted: the tilt is about 180 (%.1f)"), TiltDeg, R.LandingTiltDeg), R.LandingTiltDeg > 175.0f);
		}
	}
	return true;
}

// The same rotations stepped at 120 and 240 Hz give the same result.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRecognizerStepSizeIndependent, "KiteSurf.Trick.RecognizerStepSizeIndependent", TrickRecognizerTest::Flags)

bool FKiteSurfTrickRecognizerStepSizeIndependent::RunTest(const FString& Parameters)
{
	using namespace TrickRecognizerTest;
	const FQuat Body0 = RidingBody();
	auto Run = [&Body0](float Dt, int32 Which)
	{
		FRotationRecognizer Recognizer = Begun(Body0);
		FQuat Body = Body0;
		Hold(Recognizer, Body, 0.2f, Dt);
		switch (Which)
		{
		case 0: Body = Rotate(Recognizer, Body, BackRollAxis(), 720.0f, 250.0f, Dt); break;
		case 1: Body = Rotate(Recognizer, Body, SpinAxis(true), 540.0f, 400.0f, Dt); break;
		default:
			Body = Rotate(Recognizer, Body, World(RiderAxes::BackFlipAxisBody()), 360.0f, 300.0f, Dt);
			Body = Rotate(Recognizer, Body, SpinAxis(false), 180.0f, 300.0f, Dt);
			break;
		}
		Hold(Recognizer, Body, 0.2f, Dt);
		return Recognizer.Finish(Body);
	};
	const TCHAR* Names[] = { TEXT("double back roll"), TEXT("BS 540"), TEXT("backflip then FS 180") };
	for (int32 Which = 0; Which < 3; ++Which)
	{
		const FRotationResult A = Run(1.0f / 120.0f, Which);
		const FRotationResult B = Run(1.0f / 240.0f, Which);
		AddInfo(FString::Printf(TEXT("%s at 120 Hz: %s"), Names[Which], *Describe(A)));
		AddInfo(FString::Printf(TEXT("%s at 240 Hz: %s"), Names[Which], *Describe(B)));
		TestEqual(FString::Printf(TEXT("%s: the same inversions"), Names[Which]), Kinds(A), Kinds(B));
		TestEqual(FString::Printf(TEXT("%s: the same half turns"), Names[Which]), A.SpinHalfTurns, B.SpinHalfTurns);
		TestEqual(FString::Printf(TEXT("%s: the same sense"), Names[Which]), A.SpinSense, B.SpinSense);
		TestEqual(FString::Printf(TEXT("%s: the same landing stance"), Names[Which]), A.LandingStance, B.LandingStance);
		TestNearlyEqual(FString::Printf(TEXT("%s: the same spin (deg)"), Names[Which]), A.SpinDeg, B.SpinDeg, 0.5f);
		TestNearlyEqual(FString::Printf(TEXT("%s: the same heading (deg, %.2f against %.2f)"), Names[Which], A.NetHeadingDeg, B.NetHeadingDeg),
			FMath::FindDeltaAngleDegrees(A.NetHeadingDeg, B.NetHeadingDeg), 0.0f, 0.5f);
		TestNearlyEqual(FString::Printf(TEXT("%s: the same roll start (s)"), Names[Which]), A.RollStartSeconds, B.RollStartSeconds, 1.0f / 120.0f + 1e-4f);
		if (Which == 0)
		{
			TestEqual(TEXT("Double back roll: two back rolls"), A.Inversions.Num(), 2);
		}
		if (Which == 2)
		{
			TestEqual(TEXT("Backflip then FS 180: one backflip"), Kinds(A), FString(TEXT("ETrickInversion::BackFlip")));
			TestEqual(TEXT("Backflip then FS 180: lands toeside"), A.LandingStance, ETrickStance::Toeside);
		}
	}
	return true;
}

// The recorder runs the recogniser from the attitude fields: the live record shows the rotation as
// it is credited, the finished record carries it and the landing cause, and the name follows.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickRecorderCarriesRotation, "KiteSurf.Trick.RecorderCarriesRotation", TrickRecognizerTest::Flags)

bool FKiteSurfTrickRecorderCarriesRotation::RunTest(const FString& Parameters)
{
	using namespace TrickRecognizerTest;
	const float Hz = 240.0f;
	const float Dt = 1.0f / Hz;
	const FQuat Body0 = RidingBody();
	const FVector Axis = BackRollAxis();
	const float RateRad = FMath::DegreesToRadians(240.0f);

	for (const bool bWithAttitude : { true, false })
	{
		const TCHAR* Which = bWithAttitude ? TEXT("with the attitude") : TEXT("no attitude");
		FJumpRecorder Recorder;
		FJumpRecorderInput In;
		In.BoardState = EBoardState::Planing;
		In.Velocity = TravelVelocity;
		In.TensionN = 900.0f;
		In.KiteElevationDeg = 60.0f;
		In.bHasAttitude = bWithAttitude;
		In.BodyQuat = Body0;
		int32 Step = 0;
		FJumpRecord Out;
		auto Feed = [&]() { ++Step; In.BoardTimeSeconds = Step * Dt; In.KiteTimeSeconds = In.BoardTimeSeconds; return Recorder.Step(In, Out); };
		Feed(); // primes

		// Take-off: the attitude is still the riding pose on this step.
		++In.TakeoffCount;
		In.LastTakeoffTimeSeconds = (Step + 1) * Dt;
		In.BoardState = EBoardState::Airborne;
		Feed();
		TestTrue(FString::Printf(TEXT("%s: the jump opened"), Which), Recorder.IsJumpOpen());

		// 0.2 s of hang, then a 360 back roll at 240 deg/s (1.5 s), then 0.3 s upright.
		FQuat Body = Body0;
		bool bTickedBackRoll = false;
		float TickedAt = -1.0f;
		const int32 RollSteps = FMath::RoundToInt(1.5f * Hz);
		for (int32 I = 0; I < FMath::RoundToInt(0.2f * Hz) + RollSteps + FMath::RoundToInt(0.3f * Hz); ++I)
		{
			const bool bRolling = I >= FMath::RoundToInt(0.2f * Hz) && I < FMath::RoundToInt(0.2f * Hz) + RollSteps;
			if (bRolling)
			{
				Body = (FQuat(Axis, RateRad * Dt) * Body).GetNormalized();
			}
			In.bAttitudeActive = bWithAttitude;
			In.BodyQuat = Body;
			In.AngularVelocityRadS = bRolling ? Axis * RateRad : FVector::ZeroVector;
			In.Location.Z = 500.0f;
			Feed();
			const FJumpRecord& Live = Recorder.GetLive();
			if (!bTickedBackRoll && Live.Inversions.Num() == 1)
			{
				bTickedBackRoll = true;
				TickedAt = Live.AirtimeSeconds;
				TestEqual(FString::Printf(TEXT("%s: the ticker names the back roll as it is counted"), Which), AKiteSurfHUD::FormatTrickTicker(Live), FString(TEXT("Back roll")));
			}
		}
		TestEqual(FString::Printf(TEXT("%s: the live record credited the back roll in the air"), Which), bTickedBackRoll, bWithAttitude);
		if (bWithAttitude)
		{
			TestTrue(FString::Printf(TEXT("%s: credited before the roll was over (%.2f s)"), Which, TickedAt), TickedAt > 0.2f && TickedAt < 1.7f);
		}

		// An under-rotated crash verdict at touchdown, from the board.
		++In.JumpCount;
		In.bLastLandingClean = false;
		In.bCrashing = true;
		In.BoardState = EBoardState::Landing;
		In.LastLandingCause = ELandingCause::UnderRotated;
		In.LastApexCm = 500.0f;
		In.LastAirtimeSeconds = 2.0f;
		In.LastLandingG = 3.0f;
		const bool bFinalised = Feed();
		TestTrue(FString::Printf(TEXT("%s: one record at touchdown"), Which), bFinalised);
		AddInfo(FString::Printf(TEXT("%s: '%s', %d inversion(s), spin %.1f deg, heading %.1f deg, roll start %.3f s, cause %s"), Which, *Out.TrickName,
			Out.Inversions.Num(), Out.SpinDeg, Out.NetHeadingDeg, Out.RollStartSinceTakeoffSeconds, *UEnum::GetValueAsString(Out.LandingCause)));
		TestEqual(FString::Printf(TEXT("%s: the landing cause is the verdict's"), Which), Out.LandingCause, ELandingCause::UnderRotated);
		TestEqual(FString::Printf(TEXT("%s: rotation tracked"), Which), Out.bRotationTracked, bWithAttitude);
		if (bWithAttitude)
		{
			TestEqual(TEXT("With the attitude: one back roll in the record"), Out.Inversions.Num() == 1 && Out.Inversions[0] == ETrickInversion::BackRoll, true);
			TestEqual(TEXT("With the attitude: named"), Out.TrickName, FString(TEXT("Back roll")));
			TestNearlyEqual(TEXT("With the attitude: the roll started 0.2 s after the take-off (s)"), Out.RollStartSinceTakeoffSeconds, 0.2f, 2.0f * Dt);
			TestTrue(TEXT("With the attitude: an inversion scores technicality"), Out.Score.Technicality > 0.0f);
		}
		else
		{
			TestEqual(TEXT("No attitude: no inversion"), Out.Inversions.Num(), 0);
			TestEqual(TEXT("No attitude: Straight air"), Out.TrickName, FString(TEXT("Straight air")));
		}
	}
	return true;
}

// A megaloop with a back roll in it is named from the record (the T1.6 synthetic test), and the roll's
// start against the loop's yank makes it early or late.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfTrickNamesMegaloopBackRollFromRecord, "KiteSurf.Trick.NamesMegaloopBackRollFromRecord", TrickRecognizerTest::Flags)

bool FKiteSurfTrickNamesMegaloopBackRollFromRecord::RunTest(const FString& Parameters)
{
	using namespace TrickRecognizerTest;
	const FLoopClassifySettings Settings;
	FJumpRecord Record;
	Record.ApexHeightCm = 1500.0f;
	Record.LandingG = 2.5f;
	Record.KiteElevationAtLandingDeg = 60.0f;
	FJumpLoop& Loop = Record.Loops.AddDefaulted_GetRef();
	Loop.Loop.Direction = 1;
	Loop.Loop.RiderTravelSide = 1;
	Loop.Loop.bCompleted = true;
	Loop.Loop.TurnDeg = 360.0f;
	Loop.Loop.StartTimeSeconds = 100.0f;
	Loop.Loop.DurationSeconds = 1.6f;
	Loop.Loop.StartElevationDeg = 60.0f;
	Loop.Loop.MinElevationDeg = 15.0f;
	Loop.Loop.PeakTensionN = 3.5f * Settings.RiderMassKg * KiteUnits::GravityMS2;
	Loop.Loop.PeakTensionTimeSeconds = 100.8f; // the yank 0.8 s into the loop
	Loop.StartSinceTakeoffSeconds = 1.0f;
	Loop.StartSinceApexSeconds = -0.5f;
	Loop.RiderHeightAtStartCm = 1000.0f;
	TestEqual(TEXT("The loop is a megaloop"), TrickRecognition::ClassifyLoop(Loop, Settings), ETrickLoopKind::Megaloop);
	TestNearlyEqual(TEXT("Its yank is 1.8 s after the take-off"), TrickRecognition::PeakTensionSinceTakeoffSeconds(Loop), 1.8f, 1e-4f);

	auto NameWith = [&Record](float RollStart)
	{
		FJumpRecord R = Record;
		R.bRotationTracked = true;
		R.Inversions = { ETrickInversion::BackRoll };
		R.RollStartSinceTakeoffSeconds = RollStart;
		return TrickNaming::Name(TrickRecognition::SignatureFromJump(R));
	};
	TestEqual(TEXT("No roll: Megaloop"), TrickNaming::Name(TrickRecognition::SignatureFromJump(Record)), FString(TEXT("Megaloop")));
	TestEqual(TEXT("Roll at the yank: Megaloop back roll"), NameWith(1.8f), FString(TEXT("Megaloop back roll")));
	TestEqual(TEXT("Roll 0.5 s before the yank: Early megaloop back roll"), NameWith(1.3f), FString(TEXT("Early megaloop back roll")));
	TestEqual(TEXT("Roll 0.5 s after the yank: Late megaloop back roll"), NameWith(2.3f), FString(TEXT("Late megaloop back roll")));
	TestEqual(TEXT("No roll start known: no timing"), NameWith(-1.0f), FString(TEXT("Megaloop back roll")));
	return true;
}

// T2.6: every cause has a short, distinct line; a card carries the verdict's cause as a third line
// whenever there is one, which is only for a sketchy landing or a crash.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDJumpCardShowsCause, "KiteSurf.HUD.JumpCardShowsCause", TrickRecognizerTest::Flags)

bool FKiteSurfHUDJumpCardShowsCause::RunTest(const FString& Parameters)
{
	using namespace TrickRecognizerTest;
	TestTrue(TEXT("None has no line"), AKiteSurfHUD::LandingCauseLine(ELandingCause::None).IsEmpty());
	TSet<FString> Seen;
	const UEnum* CauseEnum = StaticEnum<ELandingCause>();
	for (int32 Index = 0; CauseEnum && Index < CauseEnum->NumEnums() - 1; ++Index)
	{
		const ELandingCause Cause = static_cast<ELandingCause>(CauseEnum->GetValueByIndex(Index));
		if (Cause == ELandingCause::None)
		{
			continue;
		}
		const FString Line = AKiteSurfHUD::LandingCauseLine(Cause);
		TestFalse(FString::Printf(TEXT("%s has a line"), *CauseEnum->GetNameStringByIndex(Index)), Line.IsEmpty());
		TestTrue(FString::Printf(TEXT("%s's line is short ('%s', %d chars)"), *CauseEnum->GetNameStringByIndex(Index), *Line, Line.Len()), Line.Len() <= 40);
		TestFalse(FString::Printf(TEXT("%s's line is its own"), *CauseEnum->GetNameStringByIndex(Index)), Seen.Contains(Line));
		Seen.Add(Line);
	}
	TestEqual(TEXT("Under-rotated"), AKiteSurfHUD::LandingCauseLine(ELandingCause::UnderRotated), FString(TEXT("Under-rotated: commit the roll earlier")));
	TestEqual(TEXT("Kite too low"), AKiteSurfHUD::LandingCauseLine(ELandingCause::KiteTooLow), FString(TEXT("Kite too low at touchdown")));
	TestEqual(TEXT("Too hard"), AKiteSurfHUD::LandingCauseLine(ELandingCause::TooHard), FString(TEXT("Landed too hard: redirect the kite")));
	TestEqual(TEXT("Sideways"), AKiteSurfHUD::LandingCauseLine(ELandingCause::Sideways), FString(TEXT("Board sideways at touchdown")));
	TestEqual(TEXT("Inverted"), AKiteSurfHUD::LandingCauseLine(ELandingCause::Inverted), FString(TEXT("Upside down at touchdown")));

	FJumpRecord Crash;
	Crash.TrickName = TEXT("Back roll");
	Crash.Grade = ELandingGrade::Crash;
	Crash.LandingG = 5.1f;
	Crash.LandingCause = ELandingCause::UnderRotated;
	TestEqual(TEXT("An under-rotated crash: the cause is the third line"), AKiteSurfHUD::FormatJumpCard(Crash),
		FString(TEXT("Back roll  CRASH  0 pts\n5.1 g landing\nUnder-rotated: commit the roll earlier")));

	FJumpRecord Sketchy = Crash;
	Sketchy.Grade = ELandingGrade::Sketchy;
	Sketchy.LandingCause = ELandingCause::KiteTooLow;
	TestTrue(TEXT("A sketchy landing with a cause shows it"), AKiteSurfHUD::FormatJumpCard(Sketchy).EndsWith(TEXT("\nKite too low at touchdown")));

	// The grade and the cause are both the board's verdict's (decision 6), which names a cause only
	// for a sketchy landing or a crash: a clean card has no cause and two lines.
	FJumpRecord Clean = Crash;
	Clean.Grade = ELandingGrade::Clean;
	Clean.LandingCause = ELandingCause::None;
	TestEqual(TEXT("A clean landing keeps two lines"), AKiteSurfHUD::FormatJumpCard(Clean), FString(TEXT("Back roll  CLEAN  0 pts\n5.1 g landing")));

	FJumpRecord NoCause = Crash;
	NoCause.LandingCause = ELandingCause::None;
	TestEqual(TEXT("A crash with no cause keeps two lines"), AKiteSurfHUD::FormatJumpCard(NoCause), FString(TEXT("Back roll  CRASH  0 pts\n5.1 g landing")));

	UWorld* HudWorld = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteSurfHUD* HUD = HudWorld ? HudWorld->SpawnActor<AKiteSurfHUD>() : nullptr;
	TestNotNull(TEXT("HUD spawned"), HUD);
	if (HUD)
	{
		HUD->ShowJumpCard(Crash);
		TestEqual(TEXT("The card shows the cause line"), HUD->GetJumpCardCauseText(), FString(TEXT("Under-rotated: commit the roll earlier")));
		HUD->ShowJumpCard(NoCause);
		TestTrue(TEXT("and none when there is no cause"), HUD->GetJumpCardCauseText().IsEmpty());
	}
	if (HudWorld)
	{
		HudWorld->DestroyWorld(false);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
