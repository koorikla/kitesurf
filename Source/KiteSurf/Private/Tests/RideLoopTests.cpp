#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "BoardMovementComponent.h"
#include "BoardWakeComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "KiteComponent.h"
#include "KiteRiderPawn.h"
#include "KiteSurf.h"
#include "KiteSurfGameMode.h"
#include "WindComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const float RideDeltaTime = 1.0f / 60.0f;
	const float KnotCmS = 51.44f;

	/** A pawn in a throwaway world, set up the way the game mode starts a ride, in steady wind along +X. */
	struct FRideFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;
		UBoardMovementComponent* Board = nullptr;

		explicit FRideFixture(float WindKnots = 15.0f)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (Pawn)
			{
				Kite = Pawn->GetKite();
				Board = Pawn->GetBoardMovement();
				if (UWindComponent* Wind = Pawn->GetWind())
				{
					Wind->BaseWind = FVector(WindKnots * KnotCmS, 0.0f, 0.0f);
					Wind->GustStrength = 0.0f;
					Wind->DirectionDriftDeg = 0.0f;
				}
				AKiteSurfGameMode::InitializeRide(Pawn, 12.0f * KnotCmS);
			}
		}

		~FRideFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Pawn && Kite && Board; }

		void Simulate(float Seconds)
		{
			const int32 Steps = FMath::RoundToInt(Seconds / RideDeltaTime);
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				Kite->UpdateKite(RideDeltaTime);
				Pawn->Tick(RideDeltaTime);
				Board->TickComponent(RideDeltaTime, LEVELTICK_All, nullptr);
			}
		}

		float SpeedKnots() const { return Board->Velocity.Size2D() / KnotCmS; }
	};
}

// The opening seconds must not need any input: the rider starts planing across the wind and stays there.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRideKeepsPlaningWithoutInput, "KiteSurf.Ride.KeepsPlaningWithoutInput", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfRideKeepsPlaningWithoutInput::RunTest(const FString& Parameters)
{
	FRideFixture Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}

	const FVector Start = Ride.Pawn->GetActorLocation();
	for (int32 Segment = 0; Segment < 6; ++Segment)
	{
		Ride.Simulate(5.0f);
		UE_LOG(LogKiteSurf, Log, TEXT("KeepsPlaningWithoutInput t=%ds: %.1f kn, fwd %.0f lat %.0f cm/s, yaw %.0f, tension %.0f N, kite az %.0f el %.0f"),
			(Segment + 1) * 5, Ride.SpeedKnots(), Ride.Board->GetForwardSpeed(), Ride.Board->GetLateralSpeed(),
			Ride.Pawn->GetActorRotation().Yaw, Ride.Kite->GetLineTensionN(), Ride.Kite->GetAzimuthDeg(), Ride.Kite->GetElevationDeg());
	}

	const FVector Travelled = Ride.Pawn->GetActorLocation() - Start;
	TestTrue(TEXT("Still planing after 30 s with no input"), Ride.Board->IsPlaning());
	TestTrue(FString::Printf(TEXT("Speed %.1f kn is at least 10 kn after 30 s"), Ride.SpeedKnots()), Ride.SpeedKnots() >= 10.0f);
	TestTrue(FString::Printf(TEXT("Speed %.1f kn is at most 30 kn after 30 s"), Ride.SpeedKnots()), Ride.SpeedKnots() <= 30.0f);
	TestTrue(FString::Printf(TEXT("Rode %.0f m across the wind"), Travelled.Y / 100.0f), Travelled.Y >= 15000.0f);
	TestTrue(FString::Printf(TEXT("Lost %.0f m downwind, less than the distance ridden across"), Travelled.X / 100.0f), Travelled.X < Travelled.Y);
	TestTrue(TEXT("The kite stays where it was parked"), FMath::IsNearlyEqual(Ride.Kite->GetClockDeg(), AKiteSurfGameMode::StartKiteClockDeg, 2.0f));
	return true;
}

// Flying the kite over the top to the other side turns the rider around onto the other tack.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRideTransitionReversesTack, "KiteSurf.Ride.TransitionReversesTack", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfRideTransitionReversesTack::RunTest(const FString& Parameters)
{
	FRideFixture Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}

	Ride.Simulate(5.0f);
	TestTrue(TEXT("Riding to the right before the transition"), Ride.Board->Velocity.Y > 400.0f);

	// Hold steer left until the kite is low on the left side, then let go.
	Ride.Pawn->SteerKite(-1.0f);
	float SteerSeconds = 0.0f;
	while (Ride.Kite->GetClockDeg() > -AKiteSurfGameMode::StartKiteClockDeg && SteerSeconds < 10.0f)
	{
		Ride.Simulate(RideDeltaTime);
		SteerSeconds += RideDeltaTime;
	}
	Ride.Pawn->SteerKite(0.0f);
	TestTrue(FString::Printf(TEXT("Kite crossed to the left side in %.1f s"), SteerSeconds), SteerSeconds < 10.0f);

	Ride.Simulate(15.0f);
	UE_LOG(LogKiteSurf, Log, TEXT("TransitionReversesTack: %.1f kn, velocity (%.0f, %.0f), yaw %.0f, kite clock %.0f"),
		Ride.SpeedKnots(), Ride.Board->Velocity.X, Ride.Board->Velocity.Y, Ride.Pawn->GetActorRotation().Yaw, Ride.Kite->GetClockDeg());

	TestTrue(FString::Printf(TEXT("Riding to the left after the transition (Vy %.0f cm/s)"), Ride.Board->Velocity.Y), Ride.Board->Velocity.Y < -400.0f);
	TestTrue(TEXT("Planing again on the new tack"), Ride.Board->IsPlaning());
	TestTrue(TEXT("Board nose points the way it is travelling"), Ride.Board->GetForwardSpeed() > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteFliesAroundTheWindow, "KiteSurf.Kite.FliesAroundTheWindow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteFliesAroundTheWindow::RunTest(const FString& Parameters)
{
	FRideFixture Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	UKiteComponent* Kite = Ride.Kite;

	auto Fly = [Kite](float Steer, float Seconds)
	{
		Kite->SteerKite(Steer);
		for (float Elapsed = 0.0f; Elapsed < Seconds; Elapsed += RideDeltaTime)
		{
			Kite->UpdateKite(RideDeltaTime);
		}
		Kite->SteerKite(0.0f);
	};

	// From the zenith, steering right flies the kite down the right side until it stops above the water.
	Kite->SetWindowPosition(0.0f, 20.0f);
	Fly(1.0f, 4.0f);
	TestTrue(FString::Printf(TEXT("Kite is on the right (azimuth %.1f)"), Kite->GetAzimuthDeg()), Kite->GetAzimuthDeg() > 30.0f);
	TestNearlyEqual(TEXT("Kite stops at the minimum elevation"), Kite->GetElevationDeg(), Kite->MinElevationDeg, 1.0f);

	// Steering left brings it back up over the top and down the left side.
	Fly(-1.0f, 1.0f);
	TestTrue(FString::Printf(TEXT("Kite climbs when steered back (elevation %.1f)"), Kite->GetElevationDeg()), Kite->GetElevationDeg() > Kite->MinElevationDeg + 10.0f);
	Fly(-1.0f, 6.0f);
	TestTrue(FString::Printf(TEXT("Kite is on the left (azimuth %.1f)"), Kite->GetAzimuthDeg()), Kite->GetAzimuthDeg() < -30.0f);

	// With no steering it holds its clock position, and the sheet sets how deep it sits.
	Kite->SetWindowPosition(40.0f, 20.0f);
	Kite->SheetKite(1.0f);
	Fly(0.0f, 5.0f);
	TestNearlyEqual(TEXT("Clock position holds without steering"), Kite->GetClockDeg(), 40.0f, 0.5f);
	TestNearlyEqual(TEXT("Sheeted in, the kite settles at the power depth"), Kite->GetWindowDepthDeg(), Kite->PowerDepthDeg, 0.5f);
	Kite->SheetKite(0.0f);
	Fly(0.0f, 5.0f);
	TestNearlyEqual(TEXT("Sheeted out, the kite settles at the window edge"), Kite->GetWindowDepthDeg(), Kite->EdgeDepthDeg, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPawnSheetStaysWherePut, "KiteSurf.Pawn.SheetStaysWherePut", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPawnSheetStaysWherePut::RunTest(const FString& Parameters)
{
	FRideFixture Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;

	TestNearlyEqual(TEXT("Ride starts at the default bar position"), Pawn->GetCurrentSheetInput(), AKiteSurfGameMode::StartSheet, 0.001f);

	Pawn->SetSheetRateInput(1.0f);
	Ride.Simulate(0.25f);
	const float SheetedIn = Pawn->GetCurrentSheetInput();
	TestNearlyEqual(TEXT("Holding sheet-in moves the bar at SheetRatePerSec"), SheetedIn, AKiteSurfGameMode::StartSheet + 0.25f * Pawn->SheetRatePerSec, 0.02f);

	Pawn->SetSheetRateInput(0.0f);
	Ride.Simulate(1.0f);
	TestNearlyEqual(TEXT("Releasing the key leaves the bar where it is"), Pawn->GetCurrentSheetInput(), SheetedIn, 0.001f);
	TestNearlyEqual(TEXT("The kite uses the bar position"), Ride.Kite->Sheet, SheetedIn, 0.001f);

	Pawn->SetSheetRateInput(-1.0f);
	Ride.Simulate(3.0f);
	TestNearlyEqual(TEXT("Holding sheet-out reaches fully depowered"), Pawn->GetCurrentSheetInput(), 0.0f, 0.001f);
	return true;
}

// Board speed should track the wind, not run away with it: the window swings back as the rider speeds up.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRideSpeedScalesWithWind, "KiteSurf.Ride.SpeedScalesWithWind", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfRideSpeedScalesWithWind::RunTest(const FString& Parameters)
{
	struct FWindCase { float WindKnots; float MinKnots; float MaxKnots; };
	const FWindCase Cases[] = { { 12.0f, 8.0f, 16.0f }, { 20.0f, 15.0f, 27.0f }, { 28.0f, 20.0f, 35.0f } };

	for (const FWindCase& Case : Cases)
	{
		FRideFixture Ride(Case.WindKnots);
		TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
		if (!Ride.IsValid())
		{
			return false;
		}
		Ride.Simulate(30.0f);
		UE_LOG(LogKiteSurf, Log, TEXT("SpeedScalesWithWind: wind %.0f kn -> board %.1f kn, tension %.0f N, planing %d"),
			Case.WindKnots, Ride.SpeedKnots(), Ride.Kite->GetLineTensionN(), Ride.Board->IsPlaning());
		TestTrue(FString::Printf(TEXT("In %.0f kn of wind the board settles at %.1f kn, within %.0f..%.0f kn"), Case.WindKnots, Ride.SpeedKnots(), Case.MinKnots, Case.MaxKnots),
			Ride.SpeedKnots() >= Case.MinKnots && Ride.SpeedKnots() <= Case.MaxKnots);
		TestTrue(TEXT("Still planing"), Ride.Board->IsPlaning());
	}
	return true;
}

// The chase camera must keep the kite on screen while riding, with a level horizon.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPawnCameraFramesKite, "KiteSurf.Pawn.CameraFramesKite", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPawnCameraFramesKite::RunTest(const FString& Parameters)
{
	FRideFixture Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	USpringArmComponent* Boom = Ride.Pawn->FindComponentByClass<USpringArmComponent>();
	const UCameraComponent* Camera = Ride.Pawn->FindComponentByClass<UCameraComponent>();
	TestTrue(TEXT("Pawn has a boom and a camera"), Boom && Camera);
	if (!Boom || !Camera)
	{
		return false;
	}

	// The boom only moves the camera when it ticks, which the fixture does not do; without lag a
	// single tick puts the camera where it belongs.
	Boom->bEnableCameraLag = false;

	auto CheckFraming = [&](const TCHAR* When)
	{
		Boom->TickComponent(RideDeltaTime, LEVELTICK_All, nullptr);

		const FRotator View = Camera->GetComponentRotation();
		const FRotator ToKite = (Ride.Kite->GetKiteWorldPosition() - Camera->GetComponentLocation()).Rotation();
		const float HalfHorizontalFovDeg = Ride.Pawn->CameraFOVDeg * 0.5f;
		const float HalfVerticalFovDeg = FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(HalfHorizontalFovDeg)) * 9.0f / 16.0f));
		const float YawOffDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(View.Yaw, ToKite.Yaw));
		const float PitchOffDeg = FMath::Abs(ToKite.Pitch - View.Pitch);

		TestTrue(FString::Printf(TEXT("%s: kite is %.1f deg off centre horizontally, inside the %.1f deg half field of view"), When, YawOffDeg, HalfHorizontalFovDeg), YawOffDeg < HalfHorizontalFovDeg);
		TestTrue(FString::Printf(TEXT("%s: kite is %.1f deg off centre vertically, inside the %.1f deg half field of view"), When, PitchOffDeg, HalfVerticalFovDeg), PitchOffDeg < HalfVerticalFovDeg);
		TestNearlyEqual(FString::Printf(TEXT("%s: horizon is level"), When), static_cast<float>(View.Roll), 0.0f, 0.1f);
		TestTrue(FString::Printf(TEXT("%s: camera is above the water (%.0f cm)"), When, Camera->GetComponentLocation().Z), Camera->GetComponentLocation().Z > 100.0f);
	};

	Ride.Simulate(5.0f);
	CheckFraming(TEXT("Riding on the start tack"));

	// Fly the kite to 45 deg on the other side and ride away on the new tack.
	Ride.Pawn->SteerKite(-1.0f);
	for (float Elapsed = 0.0f; Ride.Kite->GetClockDeg() > -45.0f && Elapsed < 10.0f; Elapsed += RideDeltaTime)
	{
		Ride.Simulate(RideDeltaTime);
	}
	Ride.Pawn->SteerKite(0.0f);
	Ride.Simulate(10.0f);
	CheckFraming(TEXT("Riding on the other tack"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfWakeTrailsWhilePlaning, "KiteSurf.Wake.TrailsWhilePlaning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfWakeTrailsWhilePlaning::RunTest(const FString& Parameters)
{
	FRideFixture Ride;
	UBoardWakeComponent* Wake = Ride.IsValid() ? Ride.Pawn->GetWake() : nullptr;
	TestNotNull(TEXT("Pawn has a wake component"), Wake);
	if (!Wake)
	{
		return false;
	}

	auto SimulateWithWake = [&](float Seconds)
	{
		for (float Elapsed = 0.0f; Elapsed < Seconds; Elapsed += RideDeltaTime)
		{
			Ride.Simulate(RideDeltaTime);
			Wake->Simulate(RideDeltaTime);
		}
	};

	SimulateWithWake(3.0f);
	TestTrue(TEXT("Planing while the wake is measured"), Ride.Board->IsPlaning());
	TestTrue(FString::Printf(TEXT("Planing leaves a foam trail (%d patches)"), Wake->GetNumFoamPatches()), Wake->GetNumFoamPatches() >= 20);
	TestTrue(FString::Printf(TEXT("Planing throws spray (%d drops)"), Wake->GetNumSprayDrops()), Wake->GetNumSprayDrops() >= 10);
	TestTrue(TEXT("Foam pool stays within its budget"), Wake->GetNumFoamPatches() <= Wake->MaxFoamPatches);
	TestTrue(TEXT("Spray pool stays within its budget"), Wake->GetNumSprayDrops() <= Wake->MaxSprayDrops);

	// In the air the board lays no new foam, and what is on the water fades away.
	Ride.Board->SetBoardState(EBoardState::Airborne);
	Ride.Pawn->SetActorLocation(Ride.Pawn->GetActorLocation() + FVector(0.0f, 0.0f, 500.0f));
	for (float Elapsed = 0.0f; Elapsed < Wake->FoamLifetime + 0.5f; Elapsed += RideDeltaTime)
	{
		Wake->Simulate(RideDeltaTime);
	}
	TestEqual(TEXT("Foam has faded once the board stopped laying it"), Wake->GetNumFoamPatches(), 0);
	TestEqual(TEXT("Spray has fallen back"), Wake->GetNumSprayDrops(), 0);

	Wake->EmitSplash(1.0f);
	TestTrue(TEXT("A landing throws a splash"), Wake->GetNumSprayDrops() >= 10);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
