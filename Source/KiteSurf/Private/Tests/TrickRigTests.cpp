#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "KiteSurf.h"
#include "RiderRig.h"
#include "Tricks/RiderAxes.h"

#if WITH_DEV_AUTOMATION_TESTS

// The jointed rig with a full body orientation (T1.3, PR B): pure maths, no world, no pawn.

namespace TrickRigTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/**
	 * The board flat under the feet with its nose along NoseSideSign * Right, in body coordinates
	 * (the same as URiderAttitudeComponent::MakeCanonicalStrapOffset).
	 */
	FQuat StrapOffset(float NoseSideSign)
	{
		return FRotationMatrix::MakeFromXZ(FVector(0.0f, NoseSideSign >= 0.0f ? 1.0f : -1.0f, 0.0f), FVector::UpVector).ToQuat();
	}

	bool PoseHasNaN(const FRiderRigPose& Pose)
	{
		bool bNaN = Pose.Pelvis.ContainsNaN() || Pose.Torso.ContainsNaN();
		for (int32 Side = 0; Side < 2; ++Side)
		{
			for (const FRiderLimbPose* Limb : { &Pose.Legs[Side], &Pose.Arms[Side] })
			{
				bNaN = bNaN || Limb->Root.ContainsNaN() || Limb->Joint.ContainsNaN() || Limb->End.ContainsNaN() || Limb->Pole.ContainsNaN();
			}
		}
		return bNaN;
	}

	/** The strap the solver puts each foot in: either side of the board's middle along its length, ankle height above the deck. */
	FVector StrapPoint(const FTransform& Board, float AlongSign)
	{
		return Board.GetLocation() + Board.GetUnitAxis(EAxis::X) * (RiderRig::StrapHalfSpacingCm * AlongSign) + Board.GetUnitAxis(EAxis::Z) * RiderRig::AnkleHeightCm;
	}

	/** The largest distance between matching joints of two poses (cm); the torso counts as its axes 100 cm long. */
	float MaxPoseDifferenceCm(const FRiderRigPose& A, const FRiderRigPose& B)
	{
		float Max = FVector::Dist(A.Pelvis, B.Pelvis);
		Max = FMath::Max(Max, 100.0f * static_cast<float>(FVector::Dist(A.Torso.GetAxisX(), B.Torso.GetAxisX())));
		Max = FMath::Max(Max, 100.0f * static_cast<float>(FVector::Dist(A.Torso.GetAxisZ(), B.Torso.GetAxisZ())));
		for (int32 Side = 0; Side < 2; ++Side)
		{
			Max = FMath::Max(Max, static_cast<float>(FVector::Dist(A.Legs[Side].Root, B.Legs[Side].Root)));
			Max = FMath::Max(Max, static_cast<float>(FVector::Dist(A.Legs[Side].End, B.Legs[Side].End)));
			Max = FMath::Max(Max, static_cast<float>(FVector::Dist(A.Legs[Side].Joint, B.Legs[Side].Joint)));
			Max = FMath::Max(Max, 100.0f * static_cast<float>(FVector::Dist(A.Legs[Side].Pole.GetSafeNormal(), B.Legs[Side].Pole.GetSafeNormal())));
			Max = FMath::Max(Max, static_cast<float>(FVector::Dist(A.Arms[Side].Root, B.Arms[Side].Root)));
			Max = FMath::Max(Max, static_cast<float>(FVector::Dist(A.Arms[Side].Joint, B.Arms[Side].Joint)));
			Max = FMath::Max(Max, static_cast<float>(FVector::Dist(A.Arms[Side].End, B.Arms[Side].End)));
		}
		return Max;
	}

	/** A pose turned by Rotation about Pivot, joint by joint. */
	FRiderRigPose RotatePose(const FRiderRigPose& Pose, const FQuat& Rotation, const FVector& Pivot)
	{
		auto Move = [&](const FVector& P) { return Pivot + Rotation.RotateVector(P - Pivot); };
		FRiderRigPose Out = Pose;
		Out.Pelvis = Move(Pose.Pelvis);
		Out.Torso = Rotation * Pose.Torso;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			for (FRiderLimbPose* Limb : { &Out.Legs[Side], &Out.Arms[Side] })
			{
				Limb->Root = Move(Limb->Root);
				Limb->Joint = Move(Limb->Joint);
				Limb->End = Move(Limb->End);
				Limb->Pole = Rotation.RotateVector(Limb->Pole);
			}
		}
		return Out;
	}

	/**
	 * A frozen copy of RiderRig::SolveBody as it was before T1.3 (main at f97300f). The level path
	 * must keep giving exactly this. Delete it when the level path is changed on purpose.
	 */
	FRiderRigPose SolveBodyBeforeT13(const FRiderRigInput& Input)
	{
		using namespace RiderRig;
		FRiderRigPose Pose;

		const FVector Facing = Input.Facing.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Input.Facing.GetSafeNormal2D();
		const FVector BodyUp = Input.BodyUp.GetSafeNormal().IsNearlyZero() ? FVector::UpVector : Input.BodyUp.GetSafeNormal();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Facing);
		Pose.Torso = FRotationMatrix::MakeFromXZ(Facing, BodyUp).ToQuat();

		const FVector BoardCentre = Input.Board.GetLocation();
		const FVector BoardAlong = Input.Board.GetUnitAxis(EAxis::X);
		const FVector BoardUp = Input.Board.GetUnitAxis(EAxis::Z);
		const float RightStrapSign = FVector::DotProduct(BoardAlong, Right) >= 0.0f ? 1.0f : -1.0f;
		FVector Ankles[2];
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const float SideSign = Side == 0 ? -1.0f : 1.0f;
			Ankles[Side] = BoardCentre + BoardAlong * (StrapHalfSpacingCm * SideSign * RightStrapSign) + BoardUp * AnkleHeightCm;
		}

		const float LegReach = (ThighLengthCm + ShinLengthCm) * 0.985f;
		float Height = StandingPelvisHeightCm * (1.0f - CrouchDropFraction * FMath::Clamp(Input.Crouch, 0.0f, 1.0f));
		for (int32 Try = 0; Try < 12; ++Try)
		{
			Pose.Pelvis = BoardCentre + BoardUp * AnkleHeightCm + BodyUp * Height;
			bool bReaches = true;
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const float SideSign = Side == 0 ? -1.0f : 1.0f;
				const FVector Hip = Pose.Pelvis + Pose.Torso.RotateVector(FVector(0.0f, HipHalfWidthCm * SideSign, 0.0f));
				bReaches = bReaches && FVector::Dist(Hip, Ankles[Side]) <= LegReach;
			}
			if (bReaches)
			{
				break;
			}
			Height *= 0.94f;
		}

		for (int32 Side = 0; Side < 2; ++Side)
		{
			const float SideSign = Side == 0 ? -1.0f : 1.0f;
			FRiderLimbPose& Leg = Pose.Legs[Side];
			Leg.Root = Pose.Pelvis + Pose.Torso.RotateVector(FVector(0.0f, HipHalfWidthCm * SideSign, 0.0f));
			Leg.Pole = Facing + Right * (0.35f * SideSign);
			Leg.Joint = SolveTwoBone(Leg.Root, Ankles[Side], Leg.Pole, ThighLengthCm, ShinLengthCm, Leg.End);
		}

		SolveArms(Pose,
			Pose.Pelvis + Pose.Torso.RotateVector(FVector(46.0f, -22.0f, 22.0f)),
			Pose.Pelvis + Pose.Torso.RotateVector(FVector(46.0f, 22.0f, 22.0f)));
		return Pose;
	}

	bool ExactlyEqual(const FRiderRigPose& A, const FRiderRigPose& B)
	{
		auto Same = [](const FVector& X, const FVector& Y) { return X.X == Y.X && X.Y == Y.Y && X.Z == Y.Z; };
		bool bSame = Same(A.Pelvis, B.Pelvis) && A.Torso.X == B.Torso.X && A.Torso.Y == B.Torso.Y && A.Torso.Z == B.Torso.Z && A.Torso.W == B.Torso.W;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FRiderLimbPose* LimbsA[2] = { &A.Legs[Side], &A.Arms[Side] };
			const FRiderLimbPose* LimbsB[2] = { &B.Legs[Side], &B.Arms[Side] };
			for (int32 Limb = 0; Limb < 2; ++Limb)
			{
				bSame = bSame && Same(LimbsA[Limb]->Root, LimbsB[Limb]->Root) && Same(LimbsA[Limb]->Joint, LimbsB[Limb]->Joint)
					&& Same(LimbsA[Limb]->End, LimbsB[Limb]->End) && Same(LimbsA[Limb]->Pole, LimbsB[Limb]->Pole);
			}
		}
		return bSame;
	}
}

// Upside down and turned 90 degrees every way, the feet stay in the straps, the bones keep their
// length and the knees bend towards the chest.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRiderInvertedKeepsFeetInStraps, "KiteSurf.Rider.InvertedKeepsFeetInStraps", TrickRigTest::Flags)

bool FKiteSurfRiderInvertedKeepsFeetInStraps::RunTest(const FString& Parameters)
{
	using namespace TrickRigTest;
	const FVector Pivot(-400.0f, 1200.0f, 900.0f);
	// Facing the board's right rail (+Y in the world), as on the water.
	const FQuat Upright = RiderRig::MakeBodyQuat(FVector::RightVector, FVector::UpVector);

	struct FOffset
	{
		const TCHAR* Name;
		FQuat Rotation;
		bool bBodyFrame;
	};
	const FVector BackRoll = RiderAxes::BackRollAxisBody(1.0f, 65.0f);
	const FOffset Offsets[] =
	{
		{ TEXT("upright"), FQuat::Identity, true },
		{ TEXT("upside down (half backflip)"), FQuat(RiderAxes::BackFlipAxisBody(), PI), true },
		{ TEXT("upside down (half cartwheel)"), FQuat(FVector::ForwardVector, PI), true },
		{ TEXT("upside down (world X)"), FQuat(FVector::ForwardVector, PI), false },
		{ TEXT("roll 90 (about Front)"), FQuat(FVector::ForwardVector, HALF_PI), true },
		{ TEXT("roll -90 (about Front)"), FQuat(FVector::ForwardVector, -HALF_PI), true },
		{ TEXT("pitch 90 (about Right)"), FQuat(FVector::RightVector, HALF_PI), true },
		{ TEXT("pitch -90 (about Right)"), FQuat(FVector::RightVector, -HALF_PI), true },
		{ TEXT("yaw 90 (about Up)"), FQuat(FVector::UpVector, HALF_PI), true },
		{ TEXT("roll 90 then pitch 90"), FQuat(FVector::ForwardVector, HALF_PI) * FQuat(FVector::RightVector, HALF_PI), true },
		{ TEXT("world roll 90"), FQuat(FVector::ForwardVector, HALF_PI), false },
		{ TEXT("world pitch 90"), FQuat(FVector::RightVector, HALF_PI), false },
		{ TEXT("world yaw 90"), FQuat(FVector::UpVector, HALF_PI), false },
		{ TEXT("back roll 120"), FQuat(BackRoll, FMath::DegreesToRadians(120.0f)), true },
		{ TEXT("back roll 180"), FQuat(BackRoll, PI), true },
		{ TEXT("backflip 90"), FQuat(RiderAxes::BackFlipAxisBody(), HALF_PI), true },
	};

	int32 Cases = 0;
	float WorstAnkleCm = 0.0f;
	float WorstBoneCm = 0.0f;
	float LeastKneeForwardCm = TNumericLimits<float>::Max();
	for (const FOffset& Offset : Offsets)
	{
		for (const float NoseSide : { 1.0f, -1.0f })
		{
			// Hanging back from the harness: the body leans back against the board it is strapped to.
			for (const float HangLeanDeg : { 0.0f, 25.0f })
			{
				for (const float Crouch : { 0.0f, 1.0f })
				{
					const FQuat Turned = Offset.bBodyFrame ? Upright * Offset.Rotation : Offset.Rotation * Upright;
					const FQuat BoardQuat = Turned * StrapOffset(NoseSide);
					// Leaning back is a turn about -Right: the head goes towards -Front.
					const FQuat Body = Turned * FQuat(FVector(0.0f, -1.0f, 0.0f), FMath::DegreesToRadians(HangLeanDeg));

					FRiderRigInput Input;
					Input.Board = FTransform(BoardQuat, Pivot);
					Input.BodyQuat = Body;
					Input.Crouch = Crouch;
					// Facing and BodyUp are ignored with a body quaternion; give them nonsense to prove it.
					Input.Facing = -FVector::RightVector;
					Input.BodyUp = FVector::DownVector;
					const FRiderRigPose Pose = RiderRig::SolveBody(Input);
					++Cases;

					const FString What = FString::Printf(TEXT("%s, nose %+.0f, hang lean %.0f deg, crouch %.0f"), Offset.Name, NoseSide, HangLeanDeg, Crouch);
					if (!TestFalse(FString::Printf(TEXT("%s: nothing is NaN"), *What), PoseHasNaN(Pose)))
					{
						continue;
					}

					const FVector Front = Body.GetAxisX();
					const FVector Up = Body.GetAxisZ();
					const FVector Straps[2] = { StrapPoint(Input.Board, -1.0f), StrapPoint(Input.Board, 1.0f) };
					float AnkleOff = 0.0f;
					float BoneOff = 0.0f;
					float KneeForward = TNumericLimits<float>::Max();
					for (int32 Side = 0; Side < 2; ++Side)
					{
						const FRiderLimbPose& Leg = Pose.Legs[Side];
						AnkleOff = FMath::Max(AnkleOff, static_cast<float>(FMath::Min(FVector::Dist(Leg.End, Straps[0]), FVector::Dist(Leg.End, Straps[1]))));
						BoneOff = FMath::Max(BoneOff, FMath::Abs(static_cast<float>(FVector::Dist(Leg.Root, Leg.Joint)) - RiderRig::ThighLengthCm));
						BoneOff = FMath::Max(BoneOff, FMath::Abs(static_cast<float>(FVector::Dist(Leg.Joint, Leg.End)) - RiderRig::ShinLengthCm));
						const FRiderLimbPose& Arm = Pose.Arms[Side];
						BoneOff = FMath::Max(BoneOff, FMath::Abs(static_cast<float>(FVector::Dist(Arm.Root, Arm.Joint)) - RiderRig::UpperArmLengthCm));
						BoneOff = FMath::Max(BoneOff, FMath::Abs(static_cast<float>(FVector::Dist(Arm.Joint, Arm.End)) - RiderRig::ForearmLengthCm));
						KneeForward = FMath::Min(KneeForward, static_cast<float>(FVector::DotProduct(Leg.Joint - (Leg.Root + Leg.End) * 0.5f, Front)));
					}
					WorstAnkleCm = FMath::Max(WorstAnkleCm, AnkleOff);
					WorstBoneCm = FMath::Max(WorstBoneCm, BoneOff);
					LeastKneeForwardCm = FMath::Min(LeastKneeForwardCm, KneeForward);

					TestTrue(FString::Printf(TEXT("%s: both ankles are at the strap points (%.3f cm off)"), *What, AnkleOff), AnkleOff < 0.5f);
					TestTrue(FString::Printf(TEXT("%s: one foot in each strap (%.1f cm apart)"), *What, FVector::Dist(Pose.Legs[0].End, Pose.Legs[1].End)),
						FVector::Dist(Pose.Legs[0].End, Pose.Legs[1].End) > 2.0f * RiderRig::StrapHalfSpacingCm - 0.5f);
					TestTrue(FString::Printf(TEXT("%s: the bones keep their length (%.4f cm off)"), *What, BoneOff), BoneOff < 0.1f);
					TestTrue(FString::Printf(TEXT("%s: the knees bend forwards relative to the body (%.1f cm)"), *What, KneeForward), KneeForward > 1.0f);

					// The right foot is on the body's right, whichever way the board's nose points.
					TestTrue(FString::Printf(TEXT("%s: the right foot is on the rider's right"), *What),
						FVector::DotProduct(Pose.Legs[1].End - Pose.Legs[0].End, Body.GetAxisY()) > 0.0f);
					// The pelvis is above the feet in the body's terms, wherever the world's up is.
					const FVector AnkleMid = (Pose.Legs[0].End + Pose.Legs[1].End) * 0.5f;
					const float PelvisUpCm = FVector::DotProduct(Pose.Pelvis - AnkleMid, Up);
					TestTrue(FString::Printf(TEXT("%s: the pelvis is above the feet along the body (%.1f cm)"), *What, PelvisUpCm), PelvisUpCm > 30.0f);
					TestTrue(FString::Printf(TEXT("%s: the torso is the body"), *What), Pose.Torso.AngularDistance(Body) < 1e-4f);
					// The hands are held out in front, with the elbows down and out.
					for (int32 Side = 0; Side < 2; ++Side)
					{
						const FRiderLimbPose& Arm = Pose.Arms[Side];
						TestTrue(FString::Printf(TEXT("%s: the %s elbow hangs below the arm's line"), *What, Side == 0 ? TEXT("left") : TEXT("right")),
							FVector::DotProduct(Arm.Joint - (Arm.Root + Arm.End) * 0.5f, Up) < 0.0f);
					}
				}
			}
		}
	}

	// The body solve does not care which way the world's up is: turning the rider and the board
	// together turns the pose and changes nothing else.
	{
		FRiderRigInput Input;
		Input.Board = FTransform(Upright * StrapOffset(-1.0f), Pivot);
		Input.BodyQuat = Upright * FQuat(FVector(0.0f, -1.0f, 0.0f), FMath::DegreesToRadians(20.0f));
		Input.Crouch = 0.6f;
		const FRiderRigPose Reference = RiderRig::SolveBody(Input);
		float WorstCm = 0.0f;
		FRandomStream Random(1303);
		for (int32 Try = 0; Try < 24; ++Try)
		{
			const FQuat Turn(Random.GetUnitVector(), Random.FRandRange(-PI, PI));
			FRiderRigInput Turned = Input;
			Turned.Board = FTransform(Turn * Input.Board.GetRotation(), Pivot);
			Turned.BodyQuat = Turn * Input.BodyQuat.GetValue();
			WorstCm = FMath::Max(WorstCm, MaxPoseDifferenceCm(RiderRig::SolveBody(Turned), RotatePose(Reference, Turn, Pivot)));
		}
		TestTrue(FString::Printf(TEXT("Turning the rider and board together turns the pose rigidly (%.5f cm off at worst)"), WorstCm), WorstCm < 1e-2f);
	}

	UE_LOG(LogKiteSurf, Log, TEXT("InvertedKeepsFeetInStraps: %d poses; ankles %.4f cm off the straps at worst, bones %.5f cm, knees at least %.1f cm forward of the leg line"),
		Cases, WorstAnkleCm, WorstBoneCm, LeastKneeForwardCm);
	return true;
}

// A body quaternion built from the level facing and an upright body gives the pose the level solve
// gives, crouched or not, on a flat, tilted or edged board.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRiderBodyQuatMatchesLevelFacing, "KiteSurf.Rider.BodyQuatMatchesLevelFacing", TrickRigTest::Flags)

bool FKiteSurfRiderBodyQuatMatchesLevelFacing::RunTest(const FString& Parameters)
{
	using namespace TrickRigTest;

	struct FCase
	{
		const TCHAR* Name;
		FTransform Board;
		FVector Facing;
		FVector BodyUp;
		float Crouch;
	};
	auto Lean = [](const FVector& Away, float Deg)
	{
		return (FVector::UpVector + Away.GetSafeNormal() * FMath::Tan(FMath::DegreesToRadians(Deg))).GetSafeNormal();
	};
	const FVector FacingY = FVector::RightVector;
	const FVector Facing50 = FRotator(0.0f, 50.0f, 0.0f).Vector();
	const FVector Facing37 = FRotator(0.0f, 37.0f, 0.0f).Vector();

	// Upright bodies: the level solve and the body solve are the same pose.
	const FCase Upright[] =
	{
		{ TEXT("Standing on a level board"), FTransform(FRotator::ZeroRotator, FVector(1000.0f, 2000.0f, 50.0f)), FacingY, FVector::UpVector, 0.0f },
		{ TEXT("Full crouch"), FTransform(FRotator::ZeroRotator, FVector(1000.0f, 2000.0f, 50.0f)), FacingY, FVector::UpVector, 1.0f },
		{ TEXT("Half crouch on a tilted board"), FTransform(FRotator(25.0f, 140.0f, -30.0f), FVector(-500.0f, 300.0f, 900.0f)), Facing50, FVector::UpVector, 0.5f },
		{ TEXT("Facing the other rail"), FTransform(FRotator::ZeroRotator, FVector::ZeroVector), -FacingY, FVector::UpVector, 0.0f },
		{ TEXT("Edging hard, crouched"), FTransform(FRotator(0.0f, -53.0f, 20.0f), FVector(40.0f, -70.0f, 5.0f)), Facing37, FVector::UpVector, 0.8f },
		{ TEXT("Facing given with a tilt"), FTransform(FRotator(-10.0f, 90.0f, 0.0f), FVector(0.0f, 0.0f, 300.0f)), FVector(-1.0f, 0.0f, 0.4f), FVector::UpVector, 0.3f },
	};
	for (const FCase& Case : Upright)
	{
		FRiderRigInput Level;
		Level.Board = Case.Board;
		Level.Facing = Case.Facing;
		Level.BodyUp = Case.BodyUp;
		Level.Crouch = Case.Crouch;
		FRiderRigInput WithQuat = Level;
		WithQuat.BodyQuat = RiderRig::MakeBodyQuat(Case.Facing, Case.BodyUp);
		const FRiderRigPose LevelPose = RiderRig::SolveBody(Level);
		const FRiderRigPose QuatPose = RiderRig::SolveBody(WithQuat);
		const float Difference = MaxPoseDifferenceCm(LevelPose, QuatPose);
		TestTrue(FString::Printf(TEXT("%s: the body quaternion gives the level pose (%.6f cm apart)"), Case.Name, Difference), Difference < 1e-3f);
		TestTrue(FString::Printf(TEXT("%s: MakeBodyQuat is the level solve's torso"), Case.Name), LevelPose.Torso.Equals(WithQuat.BodyQuat.GetValue(), 1e-6f));
	}

	// Leaning, as on the water, the level pose is not a rigid body orientation: its torso keeps the
	// facing level while the pelvis goes along the tilted BodyUp. With MakeBodyQuat the torso and the
	// feet are still the level pose's and the knees still bend forwards; the pelvis follows the
	// torso's own Up. This is why the pawn leaves BodyQuat unset on the water.
	const FCase Leaning[] =
	{
		{ TEXT("Leaning back 30 deg"), FTransform(FRotator::ZeroRotator, FVector(1000.0f, 2000.0f, 50.0f)), FacingY, Lean(-FacingY, 30.0f), 0.0f },
		{ TEXT("Leaning back 30 deg, crouched"), FTransform(FRotator::ZeroRotator, FVector(1000.0f, 2000.0f, 50.0f)), FacingY, Lean(-FacingY, 30.0f), 1.0f },
		{ TEXT("Leaning away from a kite off to the side"), FTransform(FRotator(0.0f, -40.0f, 15.0f), FVector(0.0f, 0.0f, 10.0f)), Facing50, Lean(FRotator(0.0f, 50.0f + 180.0f + 60.0f, 0.0f).Vector(), 25.0f), 0.4f },
	};
	float WorstPelvisCm = 0.0f;
	for (const FCase& Case : Leaning)
	{
		FRiderRigInput Level;
		Level.Board = Case.Board;
		Level.Facing = Case.Facing;
		Level.BodyUp = Case.BodyUp;
		Level.Crouch = Case.Crouch;
		FRiderRigInput WithQuat = Level;
		WithQuat.BodyQuat = RiderRig::MakeBodyQuat(Case.Facing, Case.BodyUp);
		const FRiderRigPose LevelPose = RiderRig::SolveBody(Level);
		const FRiderRigPose QuatPose = RiderRig::SolveBody(WithQuat);
		TestFalse(FString::Printf(TEXT("%s: nothing is NaN"), Case.Name), PoseHasNaN(QuatPose));
		// 1e-4 rad: arm64 fuses multiply-adds, which leaves the two solves ~2e-5 rad apart there.
		const double TorsoRad = LevelPose.Torso.AngularDistance(QuatPose.Torso);
		TestTrue(FString::Printf(TEXT("%s: the torso is the level pose's (%.3g rad apart)"), Case.Name, TorsoRad), TorsoRad < 1e-4);
		const float FeetCm = FMath::Max(FVector::Dist(LevelPose.Legs[0].End, QuatPose.Legs[0].End), FVector::Dist(LevelPose.Legs[1].End, QuatPose.Legs[1].End));
		TestTrue(FString::Printf(TEXT("%s: the feet are in the same straps (%.6f cm apart)"), Case.Name, FeetCm), FeetCm < 1e-3f);
		const FVector Up = WithQuat.BodyQuat.GetValue().GetAxisZ();
		const FVector AnkleMid = (QuatPose.Legs[0].End + QuatPose.Legs[1].End) * 0.5f;
		const FVector PelvisOffBodyLine = (QuatPose.Pelvis - AnkleMid) - FVector::DotProduct(QuatPose.Pelvis - AnkleMid, Up) * Up;
		TestTrue(FString::Printf(TEXT("%s: the pelvis is on the body's own Up line over the feet (%.4f cm off it)"), Case.Name, PelvisOffBodyLine.Size()), PelvisOffBodyLine.Size() < 1e-2f);
		WorstPelvisCm = FMath::Max(WorstPelvisCm, static_cast<float>(FVector::Dist(LevelPose.Pelvis, QuatPose.Pelvis)));
		const FVector Front = WithQuat.BodyQuat.GetValue().GetAxisX();
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FRiderLimbPose& Leg = QuatPose.Legs[Side];
			TestTrue(FString::Printf(TEXT("%s: the %s knee bends forwards"), Case.Name, Side == 0 ? TEXT("left") : TEXT("right")),
				FVector::DotProduct(Leg.Joint - (Leg.Root + Leg.End) * 0.5f, Front) > 1.0f);
		}
	}
	UE_LOG(LogKiteSurf, Log, TEXT("BodyQuatMatchesLevelFacing: leaning, the level solve's pelvis is up to %.1f cm from the body solve's"), WorstPelvisCm);
	return true;
}

// Without a body quaternion the rig gives exactly what it gave before T1.3, bit for bit, so the
// rider on the water is unchanged.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRiderLevelRigUnchanged, "KiteSurf.Rider.LevelRigUnchanged", TrickRigTest::Flags)

bool FKiteSurfRiderLevelRigUnchanged::RunTest(const FString& Parameters)
{
	using namespace TrickRigTest;
	FRandomStream Random(20261003);
	int32 Different = 0;
	float WorstCm = 0.0f;
	constexpr int32 Tries = 400;
	for (int32 Try = 0; Try < Tries; ++Try)
	{
		FRiderRigInput Input;
		const FRotator BoardRotation(Random.FRandRange(-60.0f, 60.0f), Random.FRandRange(-180.0f, 180.0f), Random.FRandRange(-60.0f, 60.0f));
		Input.Board = FTransform(BoardRotation, FVector(Random.FRandRange(-1e5f, 1e5f), Random.FRandRange(-1e5f, 1e5f), Random.FRandRange(-100.0f, 3000.0f)));
		// Mostly level facings, some with a tilt, and the odd degenerate one.
		Input.Facing = Try % 23 == 0 ? FVector::UpVector : FVector(Random.FRandRange(-1.0f, 1.0f), Random.FRandRange(-1.0f, 1.0f), Try % 3 == 0 ? Random.FRandRange(-0.5f, 0.5f) : 0.0f);
		Input.BodyUp = Try % 29 == 0 ? FVector::ZeroVector : (FVector::UpVector + FVector(Random.FRandRange(-1.0f, 1.0f), Random.FRandRange(-1.0f, 1.0f), 0.0f)).GetSafeNormal();
		Input.Crouch = Random.FRandRange(-0.2f, 1.2f);
		const FRiderRigPose Now = RiderRig::SolveBody(Input);
		const FRiderRigPose Before = SolveBodyBeforeT13(Input);
		if (!ExactlyEqual(Now, Before))
		{
			++Different;
		}
		WorstCm = FMath::Max(WorstCm, MaxPoseDifferenceCm(Now, Before));
	}
	TestEqual(FString::Printf(TEXT("Without a body quaternion every pose is bit for bit the pre-T1.3 pose (%d of %d differ, by %.7f cm at worst)"), Different, Tries, WorstCm), Different, 0);
	return true;
}

// A foot told to leave its strap goes to its target; the other foot stays in its strap.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRiderDetachedFootGoesToTarget, "KiteSurf.Rider.DetachedFootGoesToTarget", TrickRigTest::Flags)

bool FKiteSurfRiderDetachedFootGoesToTarget::RunTest(const FString& Parameters)
{
	using namespace TrickRigTest;
	const FVector Pivot(300.0f, -200.0f, 600.0f);
	const FQuat Upright = RiderRig::MakeBodyQuat(FVector::RightVector, FVector::UpVector);
	for (const float FlipDeg : { 0.0f, 180.0f })
	{
		for (int32 Detached = 0; Detached < 2; ++Detached)
		{
			const FQuat Body = Upright * FQuat(RiderAxes::BackFlipAxisBody(), FMath::DegreesToRadians(FlipDeg));
			FRiderRigInput Input;
			Input.Board = FTransform(Body * StrapOffset(-1.0f), Pivot);
			Input.BodyQuat = Body;
			const FRiderRigPose Strapped = RiderRig::SolveBody(Input);

			// Kick the foot forwards and out, as in a one-footer: 25 cm towards the chest, 10 cm to its side.
			const float SideSign = Detached == 0 ? -1.0f : 1.0f;
			const FVector Target = Strapped.Legs[Detached].End + Body.RotateVector(FVector(25.0f, 10.0f * SideSign, 5.0f));
			Input.Feet[Detached].AnkleTarget = Target;
			const FRiderRigPose Pose = RiderRig::SolveBody(Input);
			const FString What = FString::Printf(TEXT("Flip %.0f deg, %s foot out"), FlipDeg, Detached == 0 ? TEXT("left") : TEXT("right"));

			TestFalse(FString::Printf(TEXT("%s: nothing is NaN"), *What), PoseHasNaN(Pose));
			TestTrue(FString::Printf(TEXT("%s: the free ankle is at its target (%.3f cm off)"), *What, FVector::Dist(Pose.Legs[Detached].End, Target)),
				FVector::Dist(Pose.Legs[Detached].End, Target) < 0.05f);
			const int32 Other = 1 - Detached;
			TestTrue(FString::Printf(TEXT("%s: the other foot stays in its strap (%.3f cm off)"), *What, FVector::Dist(Pose.Legs[Other].End, Strapped.Legs[Other].End)),
				FVector::Dist(Pose.Legs[Other].End, Strapped.Legs[Other].End) < 0.05f);
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const FRiderLimbPose& Leg = Pose.Legs[Side];
				TestTrue(FString::Printf(TEXT("%s: the %s leg keeps its bone lengths"), *What, Side == 0 ? TEXT("left") : TEXT("right")),
					FMath::IsNearlyEqual(static_cast<float>(FVector::Dist(Leg.Root, Leg.Joint)), RiderRig::ThighLengthCm, 0.01f)
					&& FMath::IsNearlyEqual(static_cast<float>(FVector::Dist(Leg.Joint, Leg.End)), RiderRig::ShinLengthCm, 0.01f));
			}
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
