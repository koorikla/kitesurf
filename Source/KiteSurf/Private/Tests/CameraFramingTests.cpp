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
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// The player always sees their kite and their rider. Every check here projects the kite and the
// rider through the camera's own transform and field of view, at a 16:9 screen, frame by frame,
// with the boom's location lag on as in the game.

namespace CameraFramingTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float DeltaTime = 1.0f / 60.0f;
	constexpr float KnotCmS = KiteUnits::CmPerKnot;
	constexpr float Aspect = 16.0f / 9.0f;
	/** The kite's centre stays inside this much of the half frame, so most of the kite shows too. */
	constexpr float KiteEdge = 0.95f;
	/** The rider's board and head stay inside this much of the half frame. */
	constexpr float RiderEdge = 0.95f;
	/** A frame-to-frame change in the field of view or the look pitch above this is a visible jump (deg). */
	constexpr float MaxStepDeg = 2.0f;
	/** How far past CameraMaxFOVDeg the view may open for a moment while the boom catches up (deg). */
	constexpr float MaxFovOvershootDeg = 10.0f;

	/** Where a point lands on a 16:9 screen through this camera: x right, y up, -1..1 at the edges. Behind the camera it is far off screen. */
	FVector2D ProjectToScreen(const UCameraComponent* Camera, const FVector& Point)
	{
		const FVector Local = Camera->GetComponentQuat().UnrotateVector(Point - Camera->GetComponentLocation());
		if (Local.X <= 1.0)
		{
			return FVector2D(100.0, 100.0);
		}
		const double TanHalfH = FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView * 0.5));
		const double TanHalfV = TanHalfH / Aspect;
		return FVector2D((Local.Y / Local.X) / TanHalfH, (Local.Z / Local.X) / TanHalfV);
	}

	double ScreenExtent(const FVector2D& Screen) { return FMath::Max(FMath::Abs(Screen.X), FMath::Abs(Screen.Y)); }

	/** The worst of every frame a case watched. */
	struct FFramingLog
	{
		double WorstKite = 0.0;
		double WorstRider = 0.0;
		float WorstRollDeg = 0.0f;
		float LowestCameraCm = TNumericLimits<float>::Max();
		float WidestFovDeg = 0.0f;
		float LongestArmCm = 0.0f;
		float HighestPitchDeg = -90.0f;
		float LargestFovStepDeg = 0.0f;
		float LargestPitchStepDeg = 0.0f;
		float HighestKiteElevationDeg = -90.0f;
		float LowestKiteElevationDeg = 90.0f;
		float HighestRiderCm = -TNumericLimits<float>::Max();
		int32 Frames = 0;
		bool bHasLast = false;
		float LastFovDeg = 0.0f;
		float LastPitchDeg = 0.0f;
	};

	/** A pawn riding in steady wind along +X, set up as the game mode starts a ride, with the boom ticked after it each frame as in the game. */
	struct FFramingRide
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;
		UWindComponent* Wind = nullptr;
		USpringArmComponent* Boom = nullptr;
		UCameraComponent* Camera = nullptr;
		FFramingLog Log;

		explicit FFramingRide(float WindKnots = 15.0f, float TackSide = 1.0f)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (!Pawn)
			{
				return;
			}
			Kite = Pawn->GetKite();
			Board = Pawn->GetBoardMovement();
			Wind = Pawn->GetWind();
			Boom = Pawn->FindComponentByClass<USpringArmComponent>();
			Camera = Pawn->FindComponentByClass<UCameraComponent>();
			if (Wind)
			{
				Wind->BaseWind = FVector(WindKnots * KnotCmS, 0.0f, 0.0f);
				Wind->GustStrength = 0.0f;
				Wind->DirectionDriftDeg = 0.0f;
			}
			AKiteSurfGameMode::InitializeRide(Pawn, 12.0f * KnotCmS, TackSide);
			if (Kite)
			{
				Kite->bParkHoldAssist = true;
			}
			if (Boom)
			{
				// As BP_KiteRider sets it (scripts/editor/make_input_assets.py).
				Boom->bEnableCameraLag = true;
				Boom->CameraLagSpeed = 6.0f;
			}
		}

		~FFramingRide()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board && Wind && Boom && Camera; }

		/** The kite's elevation seen from the board (deg). */
		float KiteElevationDeg() const
		{
			const FVector ToKite = Kite->GetKiteWorldPosition() - Pawn->GetActorLocation();
			return FMath::RadiansToDegrees(FMath::Atan2(ToKite.Z, ToKite.Size2D()));
		}

		/** One frame: the pawn, then the boom (which the bare world does not tick), then the frame is checked. */
		void Frame(bool bWatchSteps = true)
		{
			Pawn->Tick(DeltaTime);
			Boom->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
			Watch(bWatchSteps);
		}

		void Simulate(float Seconds, bool bWatchSteps = true)
		{
			const int32 Frames = FMath::RoundToInt(Seconds / DeltaTime);
			for (int32 Index = 0; Index < Frames; ++Index)
			{
				Frame(bWatchSteps);
			}
		}

		void Watch(bool bWatchSteps)
		{
			const FVector Root = Pawn->GetActorLocation();
			const double Kite2D = ScreenExtent(ProjectToScreen(Camera, Kite->GetKiteWorldPosition()));
			const double Board2D = ScreenExtent(ProjectToScreen(Camera, Root));
			const double Head2D = ScreenExtent(ProjectToScreen(Camera, Root + Pawn->GetActorUpVector() * 170.0f));
			const FRotator View = Camera->GetComponentRotation();
			Log.WorstKite = FMath::Max(Log.WorstKite, Kite2D);
			Log.WorstRider = FMath::Max3(Log.WorstRider, Board2D, Head2D);
			Log.WorstRollDeg = FMath::Max(Log.WorstRollDeg, FMath::Abs(static_cast<float>(View.Roll)));
			Log.LowestCameraCm = FMath::Min(Log.LowestCameraCm, static_cast<float>(Camera->GetComponentLocation().Z));
			Log.WidestFovDeg = FMath::Max(Log.WidestFovDeg, Camera->FieldOfView);
			Log.LongestArmCm = FMath::Max(Log.LongestArmCm, Boom->TargetArmLength);
			Log.HighestPitchDeg = FMath::Max(Log.HighestPitchDeg, static_cast<float>(View.Pitch));
			Log.HighestKiteElevationDeg = FMath::Max(Log.HighestKiteElevationDeg, KiteElevationDeg());
			Log.LowestKiteElevationDeg = FMath::Min(Log.LowestKiteElevationDeg, KiteElevationDeg());
			Log.HighestRiderCm = FMath::Max(Log.HighestRiderCm, static_cast<float>(Root.Z));
			if (Log.bHasLast && bWatchSteps)
			{
				Log.LargestFovStepDeg = FMath::Max(Log.LargestFovStepDeg, FMath::Abs(Camera->FieldOfView - Log.LastFovDeg));
				Log.LargestPitchStepDeg = FMath::Max(Log.LargestPitchStepDeg, FMath::Abs(static_cast<float>(View.Pitch) - Log.LastPitchDeg));
			}
			Log.bHasLast = true;
			Log.LastFovDeg = Camera->FieldOfView;
			Log.LastPitchDeg = static_cast<float>(View.Pitch);
			++Log.Frames;
		}

		/** Start a new case's log. */
		void ResetLog() { Log = FFramingLog(); }
	};

	/**
	 * Every frame of the case had the kite and the rider in frame, the horizon level and the camera
	 * above the water. With bCheckSmooth (not for a kite or rider moved by hand) the view also moved
	 * smoothly and opened no more than a little past CameraMaxFOVDeg.
	 */
	void AssertFramed(FAutomationTestBase& Test, const FFramingRide& Ride, const FString& Case, bool bCheckSmooth = true)
	{
		const FFramingLog& Log = Ride.Log;
		UE_LOG(LogKiteSurf, Log, TEXT("Camera framing, %s: %d frames, kite at worst %.2f and rider %.2f of the half frame, FOV up to %.1f deg, boom up to %.0f cm, look pitch up to %.1f deg, camera down to %.0f cm, kite elevation %.0f..%.0f deg, rider up to %.0f cm, steps FOV %.2f pitch %.2f deg"),
			*Case, Log.Frames, Log.WorstKite, Log.WorstRider, Log.WidestFovDeg, Log.LongestArmCm, Log.HighestPitchDeg, Log.LowestCameraCm,
			Log.LowestKiteElevationDeg, Log.HighestKiteElevationDeg, Log.HighestRiderCm, Log.LargestFovStepDeg, Log.LargestPitchStepDeg);
		Test.TestTrue(FString::Printf(TEXT("%s: watched some frames (%d)"), *Case, Log.Frames), Log.Frames > 0);
		Test.TestTrue(FString::Printf(TEXT("%s: the kite stays in frame every frame (worst %.2f of the half frame, limit %.2f)"), *Case, Log.WorstKite, KiteEdge), Log.WorstKite < KiteEdge);
		Test.TestTrue(FString::Printf(TEXT("%s: the rider stays in frame every frame (worst %.2f of the half frame, limit %.2f)"), *Case, Log.WorstRider, RiderEdge), Log.WorstRider < RiderEdge);
		Test.TestTrue(FString::Printf(TEXT("%s: the horizon stays level (worst roll %.4f deg)"), *Case, Log.WorstRollDeg), Log.WorstRollDeg < 0.01f);
		Test.TestTrue(FString::Printf(TEXT("%s: the camera stays above the water (lowest %.0f cm)"), *Case, Log.LowestCameraCm), Log.LowestCameraCm > 100.0f);
		// While the boom is still pulling back the view may open past CameraMaxFOVDeg for a moment
		// rather than lose the kite; once it has settled it is back inside.
		Test.TestTrue(FString::Printf(TEXT("%s: and ends no wider than %.0f deg (%.1f)"), *Case, Ride.Pawn->CameraMaxFOVDeg, Ride.Camera->FieldOfView),
			Ride.Camera->FieldOfView <= Ride.Pawn->CameraMaxFOVDeg + 0.5f);
		if (bCheckSmooth)
		{
			Test.TestTrue(FString::Printf(TEXT("%s: the view never goes wider than %.0f deg (%.1f)"), *Case, Ride.Pawn->CameraMaxFOVDeg + MaxFovOvershootDeg, Log.WidestFovDeg),
				Log.WidestFovDeg <= Ride.Pawn->CameraMaxFOVDeg + MaxFovOvershootDeg);
			Test.TestTrue(FString::Printf(TEXT("%s: the field of view moves smoothly (largest step %.2f deg a frame)"), *Case, Log.LargestFovStepDeg), Log.LargestFovStepDeg < MaxStepDeg);
			Test.TestTrue(FString::Printf(TEXT("%s: the look pitch moves smoothly (largest step %.2f deg a frame)"), *Case, Log.LargestPitchStepDeg), Log.LargestPitchStepDeg < MaxStepDeg);
		}
	}

	/** +1 when the kite is on the right of the window (riding the start tack), -1 on the left. */
	float KiteSide(const FFramingRide& Ride) { return Ride.Kite->GetClockDeg() >= 0.0f ? 1.0f : -1.0f; }
}

using namespace CameraFramingTest;

// Parked low at the window edge, the kite is far out to the side near the water: the camera turns
// towards it so it stays in frame as the rider rides on.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfCameraFramesKiteParkedLow, "KiteSurf.Camera.FramesKiteParkedLow", CameraFramingTest::Flags)

bool FKiteSurfCameraFramesKiteParkedLow::RunTest(const FString& Parameters)
{
	FFramingRide Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	Ride.Simulate(3.0f, false);
	Ride.Kite->SetWindowPosition(80.0f * KiteSide(Ride), 0.0f);
	Ride.ResetLog();
	Ride.Simulate(6.0f);
	TestTrue(FString::Printf(TEXT("Precondition: the kite flew low (elevation down to %.0f deg)"), Ride.Log.LowestKiteElevationDeg), Ride.Log.LowestKiteElevationDeg < 20.0f);
	AssertFramed(*this, Ride, TEXT("Parked low at the window edge"));
	return true;
}

// Parked at the zenith the kite is straight over the rider: the camera tilts up, and widens or pulls
// back if it must, to show the kite and the rider together.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfCameraFramesKiteAtZenith, "KiteSurf.Camera.FramesKiteAtZenith", CameraFramingTest::Flags)

bool FKiteSurfCameraFramesKiteAtZenith::RunTest(const FString& Parameters)
{
	FFramingRide Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	Ride.Simulate(3.0f, false);
	Ride.Kite->SetWindowPosition(0.0f, 0.0f);
	Ride.ResetLog();
	Ride.Simulate(6.0f);
	TestTrue(FString::Printf(TEXT("Precondition: the kite flew overhead (elevation up to %.0f deg)"), Ride.Log.HighestKiteElevationDeg), Ride.Log.HighestKiteElevationDeg > 75.0f);
	AssertFramed(*this, Ride, TEXT("Overhead at the zenith"));
	return true;
}

// A kite loop swings the kite round fast, far from where the camera's smoothed kite is: the real kite
// stays in frame throughout, and after the loop.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfCameraFramesKiteLoop, "KiteSurf.Camera.FramesKiteLoop", CameraFramingTest::Flags)

bool FKiteSurfCameraFramesKiteLoop::RunTest(const FString& Parameters)
{
	for (const float StartClockDeg : { 45.0f, 0.0f })
	{
		FFramingRide Ride;
		TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
		if (!Ride.IsValid())
		{
			return false;
		}
		Ride.Simulate(3.0f, false);
		const float Side = KiteSide(Ride);
		Ride.Kite->SetWindowPosition(StartClockDeg * Side, 10.0f);
		Ride.Simulate(1.0f, false);
		Ride.ResetLog();

		// Bar hard over towards the rider's side of the window, held as a loop, until a loop completes.
		const int32 RecordsBefore = Ride.Kite->GetLoopRecordCount();
		Ride.Kite->SetLoopHeld(true);
		Ride.Pawn->ApplyScriptedInput(-Side, 0.0f, 0.0f, 0.0f, true);
		int32 LoopingFrames = 0;
		for (float Seconds = 0.0f; Seconds < 6.0f; Seconds += DeltaTime)
		{
			Ride.Frame();
			LoopingFrames += Ride.Kite->IsLooping() ? 1 : 0;
		}
		Ride.Kite->SetLoopHeld(false);
		Ride.Pawn->ApplyScriptedInput(0.0f, 0.0f, 0.0f, 0.0f, false);
		Ride.Simulate(3.0f);

		const FString Case = FString::Printf(TEXT("Kite loop from clock %.0f"), StartClockDeg * Side);
		TestTrue(FString::Printf(TEXT("%s: precondition: the kite looped (%d frames looping, %d loop records)"), *Case, LoopingFrames, Ride.Kite->GetLoopRecordCount() - RecordsBefore),
			LoopingFrames > 30 && Ride.Kite->GetLoopRecordCount() > RecordsBefore);
		AssertFramed(*this, Ride, Case);
	}
	return true;
}

// A real jump: the kite sent up and back, a pop, the flight, the landing and the ride away. The kite
// is high overhead in the air and just after the landing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfCameraFramesBigJump, "KiteSurf.Camera.FramesBigJump", CameraFramingTest::Flags)

bool FKiteSurfCameraFramesBigJump::RunTest(const FString& Parameters)
{
	// The ride KiteSurf.Physics.StepRateIndependent jumps: 20 kn, a 9 m2 kite, 81 kg.
	FFramingRide Ride(20.0f);
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	Ride.Board->MassKg = 81.0f;
	Ride.Kite->SetKiteSize(9.0f);
	AKiteSurfGameMode::InitializeRide(Ride.Pawn, 12.0f * KnotCmS, 1.0f);
	Ride.Simulate(10.0f, false);
	Ride.ResetLog();

	// Send the kite up past the zenith, then pop.
	Ride.Pawn->SteerKite(-1.0f);
	Ride.Board->SetWeightShift(-1.0f);
	Ride.Simulate(1.2f);
	Ride.Pawn->SheetKite(1.0f);
	const bool bPopped = Ride.Board->Jump() == EJumpRejectReason::None;
	Ride.Board->SetWeightShift(0.0f);
	Ride.Pawn->SteerKite(0.0f);
	float ApexCm = 0.0f;
	bool bWasAirborne = false;
	float AfterLandingSeconds = 0.0f;
	for (float Seconds = 0.0f; Seconds < 12.0f && AfterLandingSeconds < 3.0f; Seconds += DeltaTime)
	{
		Ride.Frame();
		ApexCm = FMath::Max(ApexCm, Ride.Board->GetCurrentJumpHeight());
		const bool bAirborne = Ride.Board->GetBoardState() == EBoardState::Airborne;
		bWasAirborne |= bAirborne;
		if (bWasAirborne && !bAirborne)
		{
			AfterLandingSeconds += DeltaTime;
		}
	}
	TestTrue(TEXT("Precondition: the pop was taken"), bPopped);
	TestTrue(FString::Printf(TEXT("Precondition: a real jump (apex %.1f m)"), ApexCm / 100.0f), ApexCm > 150.0f);
	TestTrue(TEXT("Precondition: the rider landed and rode on"), AfterLandingSeconds > 0.0f);
	AssertFramed(*this, Ride, TEXT("Big jump"));
	return true;
}

// The hardest shot: the rider high in the air with the kite almost straight above them. Held still,
// with the kite placed at each elevation, so it does not depend on how high a jump goes today.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfCameraFramesKiteOverheadInAir, "KiteSurf.Camera.FramesKiteOverheadInAir", CameraFramingTest::Flags)

bool FKiteSurfCameraFramesKiteOverheadInAir::RunTest(const FString& Parameters)
{
	struct FCase
	{
		float ElevationDeg;
		float HeightCm;
	};
	const FCase Cases[] = { { 85.0f, 600.0f }, { 89.0f, 600.0f }, { 70.0f, 300.0f }, { 85.0f, 1200.0f }, { 60.0f, 600.0f } };
	for (const FCase& Case : Cases)
	{
		FFramingRide Ride;
		TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
		if (!Ride.IsValid())
		{
			return false;
		}
		Ride.Simulate(3.0f, false);
		Ride.Board->SetBoardState(EBoardState::Airborne);
		Ride.Pawn->bStepSimulation = false;
		Ride.Pawn->SetActorLocation(Ride.Pawn->GetActorLocation() + FVector(0.0f, 0.0f, Case.HeightCm));
		Ride.Kite->SetElevationDeg(Case.ElevationDeg);
		Ride.Kite->StepKite(0.0f); // places it now, from where the rider is
		Ride.Kite->UpdateVisuals();
		const FString Name = FString::Printf(TEXT("Kite at %.0f deg with the rider %.0f m up"), Case.ElevationDeg, Case.HeightCm / 100.0f);
		TestNearlyEqual(FString::Printf(TEXT("%s: precondition: the kite's elevation from the rider (deg)"), *Name), Ride.KiteElevationDeg(), Case.ElevationDeg, 3.0f);

		// From the very first frame after the jump to it (the camera has to catch up at once), and
		// then held for 3 s while it settles.
		Ride.ResetLog();
		Ride.Simulate(3.0f, false);
		AssertFramed(*this, Ride, Name, false);
	}
	return true;
}

// Flying the kite over the top to the other side turns the rider onto the other tack: the camera
// swings round with them and keeps the kite in frame all the way.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfCameraFramesTransition, "KiteSurf.Camera.FramesTransition", CameraFramingTest::Flags)

bool FKiteSurfCameraFramesTransition::RunTest(const FString& Parameters)
{
	FFramingRide Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	Ride.Simulate(5.0f, false);
	const float StartVelocityY = Ride.Board->Velocity.Y;
	Ride.ResetLog();
	Ride.Pawn->SteerKite(-1.0f);
	for (float Seconds = 0.0f; Ride.Kite->GetClockDeg() > -60.0f && Seconds < 10.0f; Seconds += DeltaTime)
	{
		Ride.Frame();
	}
	Ride.Pawn->SteerKite(0.0f);
	Ride.Simulate(10.0f);
	TestTrue(FString::Printf(TEXT("Precondition: the rider is on the other tack (%.0f then %.0f cm/s across)"), StartVelocityY, Ride.Board->Velocity.Y),
		FMath::Sign(StartVelocityY) != FMath::Sign(Ride.Board->Velocity.Y) && FMath::Abs(Ride.Board->Velocity.Y) > 300.0f);
	AssertFramed(*this, Ride, TEXT("Transition to the other tack"));
	return true;
}

// With the wind gone the kite falls out of the sky and lies in the water at the end of its lines:
// the camera keeps it in frame on the way down and lying there.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfCameraFramesKiteInWater, "KiteSurf.Camera.FramesKiteInWater", CameraFramingTest::Flags)

bool FKiteSurfCameraFramesKiteInWater::RunTest(const FString& Parameters)
{
	FFramingRide Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	Ride.Simulate(3.0f, false);
	Ride.ResetLog();
	Ride.Wind->BaseWind = FVector(2.0f * KnotCmS, 0.0f, 0.0f);
	Ride.Simulate(10.0f);
	TestTrue(FString::Printf(TEXT("Precondition: the kite came down to the water (elevation %.0f deg, %.0f cm up)"), Ride.KiteElevationDeg(), Ride.Kite->GetKiteWorldPosition().Z),
		Ride.Kite->GetKiteWorldPosition().Z < 300.0f);
	AssertFramed(*this, Ride, TEXT("Kite down in the water"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
