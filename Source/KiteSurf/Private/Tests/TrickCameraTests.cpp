#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "KiteSurf.h"
#include "KiteRiderPawn.h"
#include "KiteComponent.h"
#include "BoardMovementComponent.h"
#include "KiteSurfGameMode.h"
#include "KiteSurfUnits.h"
#include "WindComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// The visual board split and the camera's air mode (T1.1a): the board that is drawn is separate
// from the physics root, and in the air the camera follows the flight, never the board.

namespace TrickCameraTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float DeltaTime = 1.0f / 60.0f;
	constexpr float KnotCmS = KiteUnits::CmPerKnot;

	/** A pawn riding in steady wind along +X, set up as the game mode starts a ride, with the boom's lag off so one boom tick puts the camera where it belongs. */
	struct FCameraRide
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		USpringArmComponent* Boom = nullptr;
		UCameraComponent* Camera = nullptr;

		explicit FCameraRide(float WindKnots = 15.0f)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (!Pawn)
			{
				return;
			}
			Kite = Pawn->GetKite();
			Board = Pawn->GetBoardMovement();
			Boom = Pawn->FindComponentByClass<USpringArmComponent>();
			Camera = Pawn->FindComponentByClass<UCameraComponent>();
			if (UWindComponent* Wind = Pawn->GetWind())
			{
				Wind->BaseWind = FVector(WindKnots * KnotCmS, 0.0f, 0.0f);
				Wind->GustStrength = 0.0f;
				Wind->DirectionDriftDeg = 0.0f;
			}
			AKiteSurfGameMode::InitializeRide(Pawn, 12.0f * KnotCmS, 1.0f);
			if (Kite)
			{
				Kite->bParkHoldAssist = true;
			}
			if (Boom)
			{
				Boom->bEnableCameraLag = false;
				Boom->bEnableCameraRotationLag = false;
			}
		}

		~FCameraRide()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Boom && Camera; }

		/** One frame: the pawn, then the boom (which the bare world does not tick). */
		void Frame()
		{
			Pawn->Tick(DeltaTime);
			Boom->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
		}

		void Simulate(float Seconds)
		{
			const int32 Frames = FMath::RoundToInt(Seconds / DeltaTime);
			for (int32 Index = 0; Index < Frames; ++Index)
			{
				Frame();
			}
		}

		/**
		 * Rides for a moment, then leaves the board in the air, held where it is: from here on the
		 * test turns the root by hand and Tick only poses the rider, the camera and the board.
		 */
		void TakeOffAndHold(float RideSeconds = 3.0f)
		{
			Simulate(RideSeconds);
			Board->SetBoardState(EBoardState::Airborne);
			Pawn->bStepSimulation = false;
		}

		/** The kite's horizontal direction from the boom pivot (deg). Neither moves while the simulation is held. */
		float KiteYawFromPivotDeg() const
		{
			return static_cast<float>((Kite->GetKiteWorldPosition() - Boom->GetComponentLocation()).Rotation().Yaw);
		}

		float CameraYawDeg() const { return static_cast<float>(Camera->GetComponentRotation().Yaw); }
		float CameraRollDeg() const { return static_cast<float>(Camera->GetComponentRotation().Roll); }
		float PivotHeightAboveRootCm() const { return static_cast<float>(Boom->GetComponentLocation().Z - Pawn->GetActorLocation().Z); }
	};

	/** The board's horizontal velocity turned to this world yaw, at its present speed (at least 8 m/s). */
	void SetFlightYaw(UBoardMovementComponent* Board, float YawDeg)
	{
		const float Speed = FMath::Max(static_cast<float>(Board->Velocity.Size2D()), 800.0f);
		const FVector Horizontal = FRotator(0.0f, YawDeg, 0.0f).Vector() * Speed;
		Board->Velocity = FVector(Horizontal.X, Horizontal.Y, Board->Velocity.Z);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPawnCameraStaysLevelThroughRoll, "KiteSurf.Pawn.CameraStaysLevelThroughRoll", TrickCameraTest::Flags)

bool FKiteSurfPawnCameraStaysLevelThroughRoll::RunTest(const FString& Parameters)
{
	using namespace TrickCameraTest;
	FCameraRide Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	Ride.TakeOffAndHold();

	// Fly 10 deg to one side of the kite, inside the clamp that keeps it in frame, so the heading
	// shows in the camera's yaw. The board itself points well away from that.
	const float KiteYawDeg = Ride.KiteYawFromPivotDeg();
	const float FlightYawDeg = FRotator::NormalizeAxis(KiteYawDeg + 10.0f);
	SetFlightYaw(Ride.Board, FlightYawDeg);
	const float BoardYawDeg = static_cast<float>(Pawn->GetActorRotation().Yaw);
	TestTrue(FString::Printf(TEXT("Precondition: the board (%.0f deg) points over 30 deg from the flight (%.0f deg)"), BoardYawDeg, FlightYawDeg),
		FMath::Abs(FMath::FindDeltaAngleDegrees(BoardYawDeg, FlightYawDeg)) > 30.0f);
	Ride.Simulate(3.0f);
	TestNearlyEqual(TEXT("In the air the camera looks along the flight, not the board (deg)"), FMath::FindDeltaAngleDegrees(FlightYawDeg, Ride.CameraYawDeg()), 0.0f, 1.0f);

	const FQuat Heading = Pawn->GetActorQuat();
	const float PivotHeightCm = Ride.PivotHeightAboveRootCm();
	TestNearlyEqual(TEXT("In the air the pivot is straight above the board (cm)"), PivotHeightCm, Pawn->CameraPivotHeightCm, 0.5f);

	float MaxRollDeg = 0.0f;
	float MaxPivotChangeCm = 0.0f;
	float MaxYawOffFlightDeg = 0.0f;
	auto Turn = [&](const FVector& LocalAxis, int32 Steps, float StepDeg)
	{
		for (int32 Step = 1; Step <= Steps; ++Step)
		{
			Pawn->SetActorRotation(Heading * FQuat(LocalAxis, FMath::DegreesToRadians(StepDeg * Step)));
			Ride.Frame();
			MaxRollDeg = FMath::Max(MaxRollDeg, FMath::Abs(Ride.CameraRollDeg()));
			MaxPivotChangeCm = FMath::Max(MaxPivotChangeCm, FMath::Abs(Ride.PivotHeightAboveRootCm() - PivotHeightCm));
			MaxYawOffFlightDeg = FMath::Max(MaxYawOffFlightDeg, FMath::Abs(FMath::FindDeltaAngleDegrees(FlightYawDeg, Ride.CameraYawDeg())));
		}
	};

	// A full roll about the board's length, then a full flip about its width, 5 deg a frame.
	Turn(FVector::ForwardVector, 72, 5.0f);
	Turn(FVector::RightVector, 72, 5.0f);
	TestTrue(FString::Printf(TEXT("Through a roll and a flip the camera never rolls (worst %.4f deg)"), MaxRollDeg), MaxRollDeg < 0.01f);
	TestTrue(FString::Printf(TEXT("and its pivot height moves %.2f cm (under 5)"), MaxPivotChangeCm), MaxPivotChangeCm < 5.0f);
	TestTrue(FString::Printf(TEXT("and it keeps looking along the flight (worst %.2f deg off)"), MaxYawOffFlightDeg), MaxYawOffFlightDeg < 1.0f);

	// Turn the flight to the kite's other side: the camera follows it round.
	const float NewFlightYawDeg = FRotator::NormalizeAxis(KiteYawDeg - 15.0f);
	SetFlightYaw(Ride.Board, NewFlightYawDeg);
	Pawn->SetActorRotation(Heading * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(120.0f)));
	Ride.Simulate(3.0f);
	TestNearlyEqual(TEXT("When the flight turns, the camera's yaw follows it (deg)"), FMath::FindDeltaAngleDegrees(NewFlightYawDeg, Ride.CameraYawDeg()), 0.0f, 1.0f);
	TestTrue(FString::Printf(TEXT("still level (%.4f deg)"), Ride.CameraRollDeg()), FMath::Abs(Ride.CameraRollDeg()) < 0.01f);

	// Too slow for the direction to mean anything: the last heading is held.
	Ride.Board->Velocity = FVector(50.0f, -50.0f, 0.0f);
	Ride.Simulate(1.0f);
	TestNearlyEqual(TEXT("Hanging almost still, the camera holds the last heading (deg)"), FMath::FindDeltaAngleDegrees(NewFlightYawDeg, Ride.CameraYawDeg()), 0.0f, 1.0f);

	// Back on the water the pivot rides on the board again, as it always has.
	Pawn->SetActorRotation(Heading);
	Ride.Board->SetBoardState(EBoardState::Planing);
	Ride.Simulate(1.0f);
	TestTrue(TEXT("Back on the water the boom sits at its old place on the board"), Ride.Boom->GetRelativeLocation().Equals(FVector(0.0f, 0.0f, Pawn->CameraPivotHeightCm), 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPawnCameraIgnoresAirSpin, "KiteSurf.Pawn.CameraIgnoresAirSpin", TrickCameraTest::Flags)

bool FKiteSurfPawnCameraIgnoresAirSpin::RunTest(const FString& Parameters)
{
	using namespace TrickCameraTest;
	FCameraRide Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	Ride.TakeOffAndHold();
	TestTrue(FString::Printf(TEXT("Precondition: flying faster than the air heading threshold (%.0f cm/s)"), Ride.Board->Velocity.Size2D()),
		Ride.Board->Velocity.Size2D() > Pawn->CameraAirMinSpeedCmS);
	Ride.Simulate(3.0f); // let the smoothed kite and the camera settle with nothing moving

	// One full turn of the board in the air, 5 deg a frame. Before the air mode the camera looked
	// along the board and swung from one side of the kite clamp to the other (about 60 deg).
	const float StartYawDeg = Ride.CameraYawDeg();
	float BoardTurnedDeg = 0.0f;
	float MaxCameraSwingDeg = 0.0f;
	for (int32 Step = 0; Step < 72; ++Step)
	{
		const float Before = static_cast<float>(Pawn->GetActorRotation().Yaw);
		Pawn->SetActorRotation(FRotator(0.0f, Before + 5.0f, 0.0f));
		BoardTurnedDeg += FMath::FindDeltaAngleDegrees(Before, static_cast<float>(Pawn->GetActorRotation().Yaw));
		Ride.Frame();
		MaxCameraSwingDeg = FMath::Max(MaxCameraSwingDeg, FMath::Abs(FMath::FindDeltaAngleDegrees(StartYawDeg, Ride.CameraYawDeg())));
	}
	TestNearlyEqual(TEXT("The board turned a full circle (deg)"), BoardTurnedDeg, 360.0f, 0.1f);
	TestTrue(FString::Printf(TEXT("The camera's yaw moved at most %.2f deg (under 10)"), MaxCameraSwingDeg), MaxCameraSwingDeg < 10.0f);
	TestTrue(FString::Printf(TEXT("and it never rolled (%.4f deg)"), Ride.CameraRollDeg()), FMath::Abs(Ride.CameraRollDeg()) < 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPawnBoardVisualFollowsRoot, "KiteSurf.Pawn.BoardVisualFollowsRoot", TrickCameraTest::Flags)

bool FKiteSurfPawnBoardVisualFollowsRoot::RunTest(const FString& Parameters)
{
	using namespace TrickCameraTest;
	FCameraRide Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	UStaticMeshComponent* Visual = Pawn->GetBoardVisual();
	UStaticMeshComponent* Root = Cast<UStaticMeshComponent>(Pawn->GetRootComponent());
	TestNotNull(TEXT("The pawn has a visual board"), Visual);
	TestNotNull(TEXT("and its root is a static mesh"), Root);
	if (!Visual || !Root)
	{
		return false;
	}

	// The root is the physics body: it keeps the board's mesh and collision, and is not drawn.
	const UStaticMesh* BoardAsset = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_KiteBoard"));
	TestNotNull(TEXT("SM_KiteBoard loads"), BoardAsset);
	TestTrue(TEXT("The root is not rendered"), !Root->IsVisible() && Root->bHiddenInGame);
	TestTrue(TEXT("The root still collides"), Root->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
	TestTrue(TEXT("The root keeps the board mesh for its collision"), Root->GetStaticMesh() == BoardAsset);
	TestTrue(TEXT("The board movement sweeps the root"), Ride.Board->UpdatedComponent == Root);
	TestTrue(TEXT("The visual board is drawn and shows the board mesh"), Visual->IsVisible() && !Visual->bHiddenInGame && Visual->GetStaticMesh() == BoardAsset);
	TestTrue(TEXT("The visual board does not collide"), Visual->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestTrue(TEXT("The visual board hangs off the root"), Visual->GetAttachParent() == Root);

	auto VisualMatchesRoot = [&]()
	{
		return Visual->GetComponentLocation().Equals(Root->GetComponentLocation(), 0.01f)
			&& Visual->GetComponentQuat().Equals(Root->GetComponentQuat(), 1e-4f);
	};
	auto FeetInVisualStraps = [&]()
	{
		const FRiderRigPose& Pose = Pawn->GetRiderRigPose();
		const FVector Between = Pose.Legs[1].End - Pose.Legs[0].End;
		const FVector Middle = (Pose.Legs[0].End + Pose.Legs[1].End) * 0.5f;
		const FTransform& Board = Visual->GetComponentTransform();
		const FVector ExpectedMiddle = Board.GetLocation() + Board.GetUnitAxis(EAxis::Z) * RiderRig::AnkleHeightCm;
		return FMath::Abs(FVector::DotProduct(Between.GetSafeNormal(), Board.GetUnitAxis(EAxis::X))) > 0.999f
			&& Middle.Equals(ExpectedMiddle, 0.5f);
	};

	// No override: wherever the root goes, the drawn board is on it.
	Ride.Simulate(1.0f);
	TestTrue(TEXT("Riding, the visual board is the root's transform"), VisualMatchesRoot());
	TestTrue(TEXT("and the feet are in its straps"), FeetInVisualStraps());
	Pawn->bStepSimulation = false;
	Ride.Board->SetBoardState(EBoardState::Airborne);
	const FVector Location = Pawn->GetActorLocation() + FVector(0.0f, 0.0f, 300.0f);
	Pawn->SetActorLocationAndRotation(Location, FRotator(20.0f, 40.0f, -30.0f));
	Ride.Frame();
	TestTrue(TEXT("Moved and tilted by hand, the visual board is still the root's transform"), VisualMatchesRoot());
	TestFalse(TEXT("with no override"), Pawn->HasBoardVisualOverride());

	// An override turns only the drawn board, and holds however the root turns; it still goes where the root goes.
	const FQuat Override = FRotator(-25.0f, 75.0f, 15.0f).Quaternion();
	Pawn->SetBoardVisualWorldRotation(Override);
	TestTrue(TEXT("Overridden, the visual board takes the given rotation"), Visual->GetComponentQuat().Equals(Override, 1e-4f));
	TestTrue(TEXT("and the root keeps its own"), Root->GetComponentQuat().Equals(FRotator(20.0f, 40.0f, -30.0f).Quaternion(), 1e-4f));
	Pawn->SetActorLocationAndRotation(Location + FVector(150.0f, -80.0f, 40.0f), FRotator(-10.0f, 170.0f, 60.0f));
	Ride.Frame();
	TestTrue(TEXT("When the root turns, the override holds"), Visual->GetComponentQuat().Equals(Override, 1e-4f) && Pawn->HasBoardVisualOverride());
	TestTrue(TEXT("and the visual board moves with the root"), Visual->GetComponentLocation().Equals(Root->GetComponentLocation(), 0.01f));
	TestTrue(TEXT("The feet follow the visual board, not the root"), FeetInVisualStraps()
		&& FMath::Abs(FVector::DotProduct((Pawn->GetRiderRigPose().Legs[1].End - Pawn->GetRiderRigPose().Legs[0].End).GetSafeNormal(), Root->GetForwardVector())) < 0.99f);

	// Cleared: back on the root.
	Pawn->ClearBoardVisualOverride();
	TestTrue(TEXT("After a clear the visual board is the root's transform again"), VisualMatchesRoot() && !Pawn->HasBoardVisualOverride());
	Pawn->SetActorRotation(FRotator(5.0f, -60.0f, 15.0f));
	Ride.Frame();
	TestTrue(TEXT("and follows it when it turns"), VisualMatchesRoot());
	TestTrue(TEXT("with the feet in its straps"), FeetInVisualStraps());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
