#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Materials/MaterialInterface.h"
#include "KiteSurf.h"
#include "RiderRig.h"
#include "Tricks/BoardGrabPoints.h"
#include "Tricks/RiderAxes.h"

#if WITH_DEV_AUTOMATION_TESTS

// T2.0: the rig's hands each go to their own target, and the board's grab points and straps
// (docs/tricks/T2.md). Pure maths apart from the last check, which reads SM_KiteBoard.

namespace TrickGrabPointTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const ETrickGrabZone AllZones[] =
	{
		ETrickGrabZone::Nose, ETrickGrabZone::ToeEdge, ETrickGrabZone::HeelEdge,
		ETrickGrabZone::Tail, ETrickGrabZone::BehindHeel, ETrickGrabZone::BehindToe,
	};
	const ETrickHand AllHands[] = { ETrickHand::Front, ETrickHand::Back };

	const TCHAR* ZoneName(ETrickGrabZone Zone)
	{
		switch (Zone)
		{
		case ETrickGrabZone::Nose: return TEXT("nose");
		case ETrickGrabZone::ToeEdge: return TEXT("toe edge");
		case ETrickGrabZone::HeelEdge: return TEXT("heel edge");
		case ETrickGrabZone::Tail: return TEXT("tail");
		case ETrickGrabZone::BehindHeel: return TEXT("behind, heel edge");
		case ETrickGrabZone::BehindToe: return TEXT("behind, toe edge");
		default: return TEXT("?");
		}
	}

	/** The board flat under the feet with its nose along NoseSideSign * Right, in body coordinates (URiderAttitudeComponent::MakeCanonicalStrapOffset). */
	FQuat StrapOffset(float NoseSideSign)
	{
		return FRotationMatrix::MakeFromXZ(FVector(0.0f, NoseSideSign >= 0.0f ? 1.0f : -1.0f, 0.0f), FVector::UpVector).ToQuat();
	}

	/** The rig side (0 left, 1 right) of the front or back hand: the front hand is on the nose's side. */
	int32 RigSide(ETrickHand Hand, float NoseSideSign)
	{
		const bool bNoseOnRight = NoseSideSign >= 0.0f;
		return (Hand == ETrickHand::Front) == bNoseOnRight ? 1 : 0;
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

	/** How far the arm on this side is from its bone lengths (cm). */
	float ArmBoneErrorCm(const FRiderLimbPose& Arm)
	{
		return FMath::Max(FMath::Abs(static_cast<float>(FVector::Dist(Arm.Root, Arm.Joint)) - RiderRig::UpperArmLengthCm),
			FMath::Abs(static_cast<float>(FVector::Dist(Arm.Joint, Arm.End)) - RiderRig::ForearmLengthCm));
	}

	/** The largest distance between matching arm joints and poles of two poses (cm; poles as unit vectors times 100). */
	float MaxArmDifferenceCm(const FRiderRigPose& A, const FRiderRigPose& B)
	{
		float Max = 0.0f;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			Max = FMath::Max(Max, static_cast<float>(FVector::Dist(A.Arms[Side].Root, B.Arms[Side].Root)));
			Max = FMath::Max(Max, static_cast<float>(FVector::Dist(A.Arms[Side].Joint, B.Arms[Side].Joint)));
			Max = FMath::Max(Max, static_cast<float>(FVector::Dist(A.Arms[Side].End, B.Arms[Side].End)));
			Max = FMath::Max(Max, 100.0f * static_cast<float>(FVector::Dist(A.Arms[Side].Pole.GetSafeNormal(), B.Arms[Side].Pole.GetSafeNormal())));
		}
		return Max;
	}

	/**
	 * A frozen copy of RiderRig::SolveArms as it was before T2.0 (main at 17d009e). Hands on the
	 * bar must keep giving exactly this. Delete it when the bar arms are changed on purpose.
	 */
	void SolveArmsBeforeT20(FRiderRigPose& Pose, const FVector& LeftHand, const FVector& RightHand)
	{
		using namespace RiderRig;
		const FVector BodyUp = Pose.Torso.GetAxisZ();
		const FVector Right = Pose.Torso.GetAxisY();
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const float SideSign = Side == 0 ? -1.0f : 1.0f;
			FRiderLimbPose& Arm = Pose.Arms[Side];
			Arm.Root = Pose.Pelvis + Pose.Torso.RotateVector(FVector(ShoulderOffsetCm.X, ShoulderOffsetCm.Y * SideSign, ShoulderOffsetCm.Z));
			Arm.Pole = -BodyUp + Right * (0.6f * SideSign);
			Arm.Joint = SolveTwoBone(Arm.Root, Side == 0 ? LeftHand : RightHand, Arm.Pole, UpperArmLengthCm, ForearmLengthCm, Arm.End);
		}
	}

	/** The longest the arm reaches: SolveTwoBone stops 0.5% short of straight. */
	constexpr float ArmReachCm = (RiderRig::UpperArmLengthCm + RiderRig::ForearmLengthCm) * 0.995f;
	/** The shortest: SolveTwoBone never folds the arm flatter than this. */
	constexpr float ArmFoldedCm = 0.5f + 1.0f;
}

// Each hand goes to the socket of each zone on the board, front and back hand, nose either side:
// on it when the arm reaches, otherwise stopped at the arm's reach on the line towards it. The
// other hand stays on the bar exactly.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRiderHandReachesBoardSocket, "KiteSurf.Rider.HandReachesBoardSocket", TrickGrabPointTest::Flags)

bool FKiteSurfRiderHandReachesBoardSocket::RunTest(const FString& Parameters)
{
	using namespace TrickGrabPointTest;
	const FVector Pivot(250.0f, -900.0f, 700.0f);
	const FQuat Upright = RiderRig::MakeBodyQuat(FVector::RightVector, FVector::UpVector);

	struct FBodyCase
	{
		const TCHAR* Name;
		FQuat Offset;
		float Crouch;
	};
	const FBodyCase Bodies[] =
	{
		{ TEXT("standing"), FQuat::Identity, 0.0f },
		{ TEXT("crouched"), FQuat::Identity, 1.0f },
		{ TEXT("upside down"), FQuat(RiderAxes::BackFlipAxisBody(), PI), 0.5f },
		{ TEXT("rolled 90"), FQuat(FVector::ForwardVector, HALF_PI), 0.3f },
	};

	int32 Reached = 0;
	int32 Clamped = 0;
	float WorstReachedCm = 0.0f;
	float WorstClampCm = 0.0f;
	float WorstBoneCm = 0.0f;
	for (const FBodyCase& BodyCase : Bodies)
	{
		for (const float NoseSide : { 1.0f, -1.0f })
		{
			const FQuat Body = Upright * BodyCase.Offset;
			FRiderRigInput Input;
			Input.Board = FTransform(Body * StrapOffset(NoseSide), Pivot);
			Input.BodyQuat = Body;
			Input.Crouch = BodyCase.Crouch;
			const FRiderRigPose BodyPose = RiderRig::SolveBody(Input);
			// A bar in front of the chest at waist height, as in the ride.
			const FVector BarLeft = BodyPose.Pelvis + Body.RotateVector(FVector(40.0f, -14.0f, 25.0f));
			const FVector BarRight = BodyPose.Pelvis + Body.RotateVector(FVector(40.0f, 14.0f, 25.0f));
			FRiderRigPose OnBar = BodyPose;
			RiderRig::SolveArms(OnBar, BarLeft, BarRight);

			for (const ETrickGrabZone Zone : AllZones)
			{
				for (const ETrickHand Hand : AllHands)
				{
					const int32 Side = RigSide(Hand, NoseSide);
					const FVector Socket = BoardGrabPoints::SocketFor(Zone, Hand, NoseSide);
					// Twice: on the strapped board as it is (mostly out of reach standing), and on
					// the board pulled up towards the shoulder until the socket is 80% of the arm's
					// reach away (as a grab pose will, T2.1), through the hand's own SocketBoard.
					for (int32 Pull = 0; Pull < 2; ++Pull)
					{
						FRiderRigInput HandInput = Input;
						HandInput.Hands[Side].Target = ERiderHandTarget::BoardSocket;
						HandInput.Hands[Side].BoardSocket = Socket;
						const FVector Shoulder = RiderRig::ShoulderPosition(BodyPose, Side);
						if (Pull == 1)
						{
							const FVector Strapped = Input.Board.TransformPosition(Socket);
							const FVector Wanted = Shoulder + (Strapped - Shoulder).GetSafeNormal() * (0.8f * ArmReachCm);
							FTransform Pulled = Input.Board;
							Pulled.AddToTranslation(Wanted - Strapped);
							HandInput.Hands[Side].SocketBoard = Pulled;
						}
						const FVector Target = (Pull == 1 ? HandInput.Hands[Side].SocketBoard.GetValue() : Input.Board).TransformPosition(Socket);

						FRiderRigPose Pose = BodyPose;
						RiderRig::SolveArmsPerHand(Pose, HandInput, BarLeft, BarRight);
						const FString What = FString::Printf(TEXT("%s, nose %+.0f, %s hand (rig %s), %s, %s"), BodyCase.Name, NoseSide,
							Hand == ETrickHand::Front ? TEXT("front") : TEXT("back"), Side == 0 ? TEXT("left") : TEXT("right"), ZoneName(Zone),
							Pull == 1 ? TEXT("board pulled up") : TEXT("board strapped"));
						if (!TestFalse(FString::Printf(TEXT("%s: nothing is NaN"), *What), PoseHasNaN(Pose)))
						{
							continue;
						}

						const FRiderLimbPose& Arm = Pose.Arms[Side];
						TestTrue(FString::Printf(TEXT("%s: the arm starts at the shoulder"), *What), FVector::Dist(Arm.Root, Shoulder) < 1e-3f);
						const float Bone = ArmBoneErrorCm(Arm);
						WorstBoneCm = FMath::Max(WorstBoneCm, Bone);
						TestTrue(FString::Printf(TEXT("%s: the arm keeps its bone lengths (%.4f cm off)"), *What, Bone), Bone < 0.01f);

						const float Distance = FVector::Dist(Shoulder, Target);
						if (Distance <= ArmReachCm - 0.01f && Distance >= ArmFoldedCm)
						{
							++Reached;
							const float Off = FVector::Dist(Arm.End, Target);
							WorstReachedCm = FMath::Max(WorstReachedCm, Off);
							TestTrue(FString::Printf(TEXT("%s: the hand is on the socket (%.1f cm away, %.4f cm off)"), *What, Distance, Off), Off < 1.0f);
						}
						else
						{
							++Clamped;
							const FVector Expected = Shoulder + (Target - Shoulder).GetSafeNormal() * FMath::Clamp(Distance, ArmFoldedCm, ArmReachCm);
							const float Off = FVector::Dist(Arm.End, Expected);
							WorstClampCm = FMath::Max(WorstClampCm, Off);
							TestTrue(FString::Printf(TEXT("%s: out of reach (%.1f cm), the hand stops at arm's length on the line to the socket (%.4f cm off)"), *What, Distance, Off), Off < 0.05f);
						}
						// The elbow goes out to the hand's side.
						const float SideSign = Side == 0 ? -1.0f : 1.0f;
						TestTrue(FString::Printf(TEXT("%s: the elbow points out to its side"), *What),
							FVector::DotProduct(Arm.Joint - (Arm.Root + Arm.End) * 0.5f, Pose.Torso.GetAxisY() * SideSign) > 0.0f);
						// The other hand is untouched: exactly where SolveArms puts it on the bar.
						const int32 Other = 1 - Side;
						TestTrue(FString::Printf(TEXT("%s: the other hand stays on the bar"), *What),
							Pose.Arms[Other].End == OnBar.Arms[Other].End && Pose.Arms[Other].Joint == OnBar.Arms[Other].Joint);
					}
				}
			}

			// A free point and behind the back, one hand each.
			{
				FRiderRigInput HandInput = Input;
				const FVector Free = BodyPose.Pelvis + Body.RotateVector(FVector(30.0f, -40.0f, 60.0f));
				HandInput.Hands[0].Target = ERiderHandTarget::Free;
				HandInput.Hands[0].WorldTarget = Free;
				HandInput.Hands[1].Target = ERiderHandTarget::BehindBack;
				FRiderRigPose Pose = BodyPose;
				RiderRig::SolveArmsPerHand(Pose, HandInput, BarLeft, BarRight);
				const FString What = FString::Printf(TEXT("%s, nose %+.0f"), BodyCase.Name, NoseSide);
				TestFalse(FString::Printf(TEXT("%s, free and behind the back: nothing is NaN"), *What), PoseHasNaN(Pose));
				TestTrue(FString::Printf(TEXT("%s: the free hand is on its point (%.4f cm off)"), *What, FVector::Dist(Pose.Arms[0].End, Free)),
					FVector::Dist(Pose.Arms[0].End, Free) < 1.0f);
				const FVector Behind = RiderRig::BehindBackHand(BodyPose, 1);
				TestTrue(FString::Printf(TEXT("%s: the hand behind the back is there (%.4f cm off)"), *What, FVector::Dist(Pose.Arms[1].End, Behind)),
					FVector::Dist(Pose.Arms[1].End, Behind) < 1.0f);
				TestTrue(FString::Printf(TEXT("%s: the hand behind the back is behind the body"), *What),
					FVector::DotProduct(Pose.Arms[1].End - BodyPose.Pelvis, Pose.Torso.GetAxisX()) < -15.0f);
				TestTrue(FString::Printf(TEXT("%s: the elbow behind the back points out"), *What),
					FVector::DotProduct(Pose.Arms[1].Joint - (Pose.Arms[1].Root + Pose.Arms[1].End) * 0.5f, Pose.Torso.GetAxisY()) > 0.0f);
				WorstBoneCm = FMath::Max(WorstBoneCm, FMath::Max(ArmBoneErrorCm(Pose.Arms[0]), ArmBoneErrorCm(Pose.Arms[1])));
			}
		}
	}

	TestTrue(FString::Printf(TEXT("Some sockets were in reach (%d)"), Reached), Reached > 0);
	TestTrue(FString::Printf(TEXT("Some sockets were out of reach and clamped (%d)"), Clamped), Clamped > 0);
	UE_LOG(LogKiteSurf, Log, TEXT("HandReachesBoardSocket: %d reached (worst %.4f cm off), %d clamped (worst %.4f cm off the reach line), bones %.5f cm at worst"),
		Reached, WorstReachedCm, Clamped, WorstClampCm, WorstBoneCm);
	return true;
}

// With both hands on the bar the per-hand solve is the bar solve: the same as SolveArms and as the
// frozen pre-T2.0 SolveArms, for any body, bar and board.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRiderDefaultHandsOnBar, "KiteSurf.Rider.DefaultHandsOnBar", TrickGrabPointTest::Flags)

bool FKiteSurfRiderDefaultHandsOnBar::RunTest(const FString& Parameters)
{
	using namespace TrickGrabPointTest;
	FRandomStream Random(20261020);
	constexpr int32 Tries = 300;
	float WorstNowCm = 0.0f;
	float WorstLegacyCm = 0.0f;
	int32 NotBitExact = 0;
	for (int32 Try = 0; Try < Tries; ++Try)
	{
		FRiderRigInput Input;
		const FRotator BoardRotation(Random.FRandRange(-60.0f, 60.0f), Random.FRandRange(-180.0f, 180.0f), Random.FRandRange(-60.0f, 60.0f));
		Input.Board = FTransform(BoardRotation, FVector(Random.FRandRange(-1e5f, 1e5f), Random.FRandRange(-1e5f, 1e5f), Random.FRandRange(-100.0f, 3000.0f)));
		Input.Facing = FVector(Random.FRandRange(-1.0f, 1.0f), Random.FRandRange(-1.0f, 1.0f), 0.0f);
		Input.BodyUp = (FVector::UpVector + FVector(Random.FRandRange(-0.6f, 0.6f), Random.FRandRange(-0.6f, 0.6f), 0.0f)).GetSafeNormal();
		Input.Crouch = Random.FRandRange(0.0f, 1.0f);
		// Half of them with a full body orientation, as in the air.
		if (Try % 2 == 1)
		{
			Input.BodyQuat = FQuat(Random.GetUnitVector(), Random.FRandRange(-PI, PI));
		}
		// Every other one says "bar" out loud; the rest leave the default.
		if (Try % 4 >= 2)
		{
			Input.Hands[0].Target = ERiderHandTarget::Bar;
			Input.Hands[1].Target = ERiderHandTarget::Bar;
		}
		const FRiderRigPose Body = RiderRig::SolveBody(Input);
		// Bar points from close in to well out of reach.
		const FVector BarCentre = Body.Pelvis + Body.Torso.RotateVector(FVector(Random.FRandRange(10.0f, 90.0f), Random.FRandRange(-20.0f, 20.0f), Random.FRandRange(-10.0f, 80.0f)));
		const FVector Span = Body.Torso.RotateVector(FVector(0.0f, Random.FRandRange(10.0f, 30.0f), 0.0f));
		const FVector BarLeft = BarCentre - Span;
		const FVector BarRight = BarCentre + Span;

		FRiderRigPose Now = Body;
		RiderRig::SolveArms(Now, BarLeft, BarRight);
		FRiderRigPose PerHand = Body;
		RiderRig::SolveArmsPerHand(PerHand, Input, BarLeft, BarRight);
		FRiderRigPose Legacy = Body;
		SolveArmsBeforeT20(Legacy, BarLeft, BarRight);

		WorstNowCm = FMath::Max(WorstNowCm, MaxArmDifferenceCm(PerHand, Now));
		WorstLegacyCm = FMath::Max(WorstLegacyCm, MaxArmDifferenceCm(PerHand, Legacy));
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FRiderLimbPose& A = PerHand.Arms[Side];
			const FRiderLimbPose& B = Legacy.Arms[Side];
			if (!(A.Root == B.Root && A.Joint == B.Joint && A.End == B.End && A.Pole == B.Pole))
			{
				++NotBitExact;
				break;
			}
		}
	}
	TestTrue(FString::Printf(TEXT("Hands on the bar: SolveArmsPerHand is SolveArms (%.7f cm apart at worst)"), WorstNowCm), WorstNowCm <= 1e-3f);
	TestTrue(FString::Printf(TEXT("Hands on the bar: SolveArmsPerHand is the pre-T2.0 SolveArms (%.7f cm apart at worst)"), WorstLegacyCm), WorstLegacyCm <= 1e-3f);
	UE_LOG(LogKiteSurf, Log, TEXT("DefaultHandsOnBar: %d poses, %.7f cm from SolveArms, %.7f cm from the pre-T2.0 copy, %d not bit for bit"),
		Tries, WorstNowCm, WorstLegacyCm, NotBitExact);
	return true;
}

// The grab points are on the board (inside its outline, within its thickness, the toe edge in front
// of the chest), the straps are where the rig puts the feet, and SM_KiteBoard's strap loops are
// there too.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRiderGrabSocketsOnBoard, "KiteSurf.Rider.GrabSocketsOnBoard", TrickGrabPointTest::Flags)

bool FKiteSurfRiderGrabSocketsOnBoard::RunTest(const FString& Parameters)
{
	using namespace TrickGrabPointTest;

	// The outline mirrors generate_mesh_objs.py: spot values worked out by hand from its formulas.
	TestNearlyEqual(TEXT("Half width in the middle (cm)"), BoardGrabPoints::HalfWidthCm(0.0f), 21.0f, 1e-4f);
	TestNearlyEqual(TEXT("Half width at the tips (cm): 21 x (1 - 0.3 - 0.1)"), BoardGrabPoints::HalfWidthCm(70.0f), 12.6f, 1e-4f);
	TestNearlyEqual(TEXT("Half width 35 cm out (cm): 21 x (1 - 0.075 - 0.00625)"), BoardGrabPoints::HalfWidthCm(-35.0f), 19.29375f, 1e-3f);
	TestNearlyEqual(TEXT("Rocker at the tips (cm)"), BoardGrabPoints::RockerCm(70.0f), 4.0f, 1e-4f);
	TestNearlyEqual(TEXT("Deck top in the middle (cm)"), BoardGrabPoints::DeckTopCm(0.0f, 0.0f), 1.25f, 1e-4f);
	TestNearlyEqual(TEXT("Deck top at the rail in the middle (cm)"), BoardGrabPoints::DeckTopCm(0.0f, 21.0f), 0.625f, 1e-4f);

	// Every socket inside the outline and within the board's thickness (the handle a hand's width above the deck).
	auto CheckOnBoard = [this](const FString& What, const FVector& Stance, bool bAboveDeck)
	{
		const float HalfWidth = BoardGrabPoints::HalfWidthCm(Stance.X);
		TestTrue(FString::Printf(TEXT("%s: within the board's length (x %.1f cm)"), *What, Stance.X), FMath::Abs(Stance.X) < BoardGrabPoints::BoardHalfLengthCm);
		TestTrue(FString::Printf(TEXT("%s: inside the outline (|y| %.2f <= half width %.2f cm)"), *What, FMath::Abs(Stance.Y), HalfWidth), FMath::Abs(Stance.Y) <= HalfWidth);
		const float Top = BoardGrabPoints::DeckTopCm(Stance.X, Stance.Y);
		const float Bottom = 2.0f * BoardGrabPoints::RockerCm(Stance.X) - Top;
		if (bAboveDeck)
		{
			TestTrue(FString::Printf(TEXT("%s: just above the deck (%.2f cm over it)"), *What, Stance.Z - Top), Stance.Z > Top && Stance.Z - Top < 4.0f);
		}
		else
		{
			TestTrue(FString::Printf(TEXT("%s: within the board's thickness (z %.2f in %.2f..%.2f cm)"), *What, Stance.Z, Bottom, Top), Stance.Z >= Bottom && Stance.Z <= Top);
		}
	};
	for (const ETrickGrabZone Zone : AllZones)
	{
		for (const ETrickHand Hand : AllHands)
		{
			CheckOnBoard(FString::Printf(TEXT("%s, %s hand"), ZoneName(Zone), Hand == ETrickHand::Front ? TEXT("front") : TEXT("back")),
				BoardGrabPoints::StanceSocketFor(Zone, Hand), false);
		}
	}
	auto Stance = [](const FBoardStancePointCm& P) { return FVector(P.X, P.Y, P.Z); };
	CheckOnBoard(TEXT("Toe rail, front"), Stance(BoardGrabPoints::ToeRailFront), false);
	CheckOnBoard(TEXT("Toe rail, back"), Stance(BoardGrabPoints::ToeRailBack), false);
	CheckOnBoard(TEXT("Handle"), Stance(BoardGrabPoints::Handle), true);
	TestTrue(TEXT("A hand on an edge sits towards its own foot"),
		BoardGrabPoints::StanceSocketFor(ETrickGrabZone::ToeEdge, ETrickHand::Front).X > BoardGrabPoints::StanceSocketFor(ETrickGrabZone::ToeEdge, ETrickHand::Back).X);

	// One strap spacing for the rig, the grab points and the mesh.
	TestEqual(TEXT("The grab points use the rig's strap spacing"), BoardGrabPoints::StrapHalfSpacingCm, RiderRig::StrapHalfSpacingCm);
	TestEqual(TEXT("The straps are 30 cm either side of the middle"), BoardGrabPoints::StrapHalfSpacingCm, 30.0f);

	// On a rider: the toe edge is in front of the chest, the nose on the nose side, and the feet in the straps.
	const FVector Pivot(-300.0f, 400.0f, 500.0f);
	const FQuat Upright = RiderRig::MakeBodyQuat(FVector::RightVector, FVector::UpVector);
	for (const float NoseSide : { 1.0f, -1.0f })
	{
		for (const float FlipDeg : { 0.0f, 180.0f })
		{
			const FQuat Body = Upright * FQuat(RiderAxes::BackFlipAxisBody(), FMath::DegreesToRadians(FlipDeg));
			FRiderRigInput Input;
			Input.Board = FTransform(Body * StrapOffset(NoseSide), Pivot);
			Input.BodyQuat = Body;
			const FRiderRigPose Pose = RiderRig::SolveBody(Input);
			const FString What = FString::Printf(TEXT("Nose %+.0f, flip %.0f deg"), NoseSide, FlipDeg);
			const FVector Front = Body.GetAxisX();
			const FVector Right = Body.GetAxisY();
			auto World = [&](ETrickGrabZone Zone, ETrickHand Hand) { return BoardGrabPoints::SocketWorld(Input.Board, Zone, Hand, NoseSide) - Pivot; };

			for (const ETrickHand Hand : AllHands)
			{
				TestTrue(FString::Printf(TEXT("%s: the toe edge is in front of the chest"), *What), FVector::DotProduct(World(ETrickGrabZone::ToeEdge, Hand), Front) > 15.0f);
				TestTrue(FString::Printf(TEXT("%s: the heel edge is behind"), *What), FVector::DotProduct(World(ETrickGrabZone::HeelEdge, Hand), Front) < -15.0f);
				TestTrue(FString::Printf(TEXT("%s: behind, toe edge is the toe edge"), *What), FVector::DotProduct(World(ETrickGrabZone::BehindToe, Hand), Front) > 15.0f);
				TestTrue(FString::Printf(TEXT("%s: behind, heel edge is the heel edge"), *What), FVector::DotProduct(World(ETrickGrabZone::BehindHeel, Hand), Front) < -15.0f);
				TestTrue(FString::Printf(TEXT("%s: the nose is on the nose side"), *What), FVector::DotProduct(World(ETrickGrabZone::Nose, Hand), Right) * NoseSide > 50.0f);
				TestTrue(FString::Printf(TEXT("%s: the tail is on the other side"), *What), FVector::DotProduct(World(ETrickGrabZone::Tail, Hand), Right) * NoseSide < -50.0f);
			}
			// The front hand is the one on the nose side.
			const int32 FrontSide = RigSide(ETrickHand::Front, NoseSide);
			TestTrue(FString::Printf(TEXT("%s: the front hand's shoulder is on the nose side"), *What),
				FVector::DotProduct(RiderRig::ShoulderPosition(Pose, FrontSide) - Pose.Pelvis, Right) * NoseSide > 0.0f);

			// The ankles are on the strap points: front strap under the front foot (nose side), back under the back.
			const FVector FrontStrap = Input.Board.TransformPosition(BoardGrabPoints::ToBoardLocal(BoardGrabPoints::FrontStrap, NoseSide));
			const FVector BackStrap = Input.Board.TransformPosition(BoardGrabPoints::ToBoardLocal(BoardGrabPoints::BackStrap, NoseSide));
			const float FrontOff = FVector::Dist(Pose.Legs[FrontSide].End, FrontStrap);
			const float BackOff = FVector::Dist(Pose.Legs[1 - FrontSide].End, BackStrap);
			TestTrue(FString::Printf(TEXT("%s: the front foot is in the front strap (%.4f cm off)"), *What, FrontOff), FrontOff < 0.05f);
			TestTrue(FString::Printf(TEXT("%s: the back foot is in the back strap (%.4f cm off)"), *What, BackOff), BackOff < 0.05f);
			const FVector FrontLocal = Input.Board.InverseTransformPosition(Pose.Legs[FrontSide].End);
			TestNearlyEqual(FString::Printf(TEXT("%s: the front ankle is 30 cm towards the nose (board x, cm)"), *What), static_cast<float>(FrontLocal.X), 30.0f, 0.05f);
		}
	}

	// SM_KiteBoard: the strap loops are the only geometry well above the deck (the deck tops out at
	// 5.25 cm at the tips, the loops arch 8 cm over it). They must sit at +-30 cm, under the feet.
	const UStaticMesh* Board = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_KiteBoard.SM_KiteBoard"));
	TestNotNull(TEXT("SM_KiteBoard loads"), Board);
	const FStaticMeshRenderData* RenderData = Board ? Board->GetRenderData() : nullptr;
	if (Board && TestTrue(TEXT("SM_KiteBoard has render data"), RenderData && RenderData->LODResources.Num() > 0))
	{
		// The regenerated mesh keeps its material (create_materials.py runs after import_geometry.py).
		for (const FStaticMaterial& Slot : Board->GetStaticMaterials())
		{
			TestTrue(FString::Printf(TEXT("SM_KiteBoard slot %s uses a project material"), *Slot.MaterialSlotName.ToString()),
				Slot.MaterialInterface && Slot.MaterialInterface->GetPathName().StartsWith(TEXT("/Game/Materials/")));
		}
		const FPositionVertexBuffer& Positions = RenderData->LODResources[0].VertexBuffers.PositionVertexBuffer;
		const float DeckHighestCm = BoardGrabPoints::BoardRockerCm + BoardGrabPoints::BoardThicknessHalfCm;
		int32 StrapVertices[2] = { 0, 0 };
		float NearestCm = TNumericLimits<float>::Max();
		float FurthestCm = 0.0f;
		float MaxAbsX = 0.0f;
		for (uint32 Index = 0; Index < Positions.GetNumVertices(); ++Index)
		{
			const FVector3f P = Positions.VertexPosition(Index);
			MaxAbsX = FMath::Max(MaxAbsX, FMath::Abs(P.X));
			if (P.Z > DeckHighestCm + 0.5f)
			{
				++StrapVertices[P.X >= 0.0f ? 1 : 0];
				NearestCm = FMath::Min(NearestCm, FMath::Abs(P.X));
				FurthestCm = FMath::Max(FurthestCm, FMath::Abs(P.X));
			}
		}
		TestNearlyEqual(TEXT("SM_KiteBoard runs 70 cm either side of its middle along X (cm)"), MaxAbsX, BoardGrabPoints::BoardHalfLengthCm, 0.5f);
		TestTrue(FString::Printf(TEXT("SM_KiteBoard has a strap loop each side of the middle (%d and %d vertices)"), StrapVertices[0], StrapVertices[1]),
			StrapVertices[0] > 0 && StrapVertices[1] > 0);
		// The loops are 6 cm wide along the board, so their vertices span 27..33 cm.
		TestTrue(FString::Printf(TEXT("SM_KiteBoard's strap loops are at +-30 cm, under the feet (%.2f..%.2f cm out)"), NearestCm, FurthestCm),
			NearestCm >= BoardGrabPoints::StrapHalfSpacingCm - 3.1f && FurthestCm <= BoardGrabPoints::StrapHalfSpacingCm + 3.1f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
