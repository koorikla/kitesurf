#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "BoardMovementComponent.h"
#include "BoardWakeComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "RiderCharacter.h"
#include "UI/KiteSurfSettingsWidget.h"
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

		explicit FRideFixture(float WindKnots = 15.0f, float TackSide = 1.0f)
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
				AKiteSurfGameMode::InitializeRide(Pawn, 12.0f * KnotCmS, TackSide);
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

		/** Hold the bar left until the kite is past the given clock position on the left side, then centre it. Returns the seconds taken. */
		float SteerKiteToLeftSide(float ClockDeg = -60.0f, float TimeoutSeconds = 10.0f)
		{
			Pawn->SteerKite(-1.0f);
			float Seconds = 0.0f;
			while (Kite->GetClockDeg() > ClockDeg && Seconds < TimeoutSeconds)
			{
				Simulate(RideDeltaTime);
				Seconds += RideDeltaTime;
			}
			Pawn->SteerKite(0.0f);
			return Seconds;
		}
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
	TestTrue(FString::Printf(TEXT("The kite stays parked on the side it started (clock %.0f)"), Ride.Kite->GetClockDeg()), FMath::IsNearlyEqual(Ride.Kite->GetClockDeg(), AKiteSurfGameMode::StartKiteClockDeg, 15.0f));
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

	// Hold the bar left until the kite has flown over the top to the left side, then centre it.
	const float SteerSeconds = Ride.SteerKiteToLeftSide();
	TestTrue(FString::Printf(TEXT("Kite crossed to the left side in %.1f s"), SteerSeconds), SteerSeconds < 10.0f);

	Ride.Simulate(15.0f);
	UE_LOG(LogKiteSurf, Log, TEXT("TransitionReversesTack: %.1f kn, velocity (%.0f, %.0f), yaw %.0f, kite clock %.0f"),
		Ride.SpeedKnots(), Ride.Board->Velocity.X, Ride.Board->Velocity.Y, Ride.Pawn->GetActorRotation().Yaw, Ride.Kite->GetClockDeg());

	TestTrue(FString::Printf(TEXT("Riding to the left after the transition (Vy %.0f cm/s)"), Ride.Board->Velocity.Y), Ride.Board->Velocity.Y < -400.0f);
	TestTrue(TEXT("Planing again on the new tack"), Ride.Board->IsPlaning());
	TestTrue(TEXT("Board nose points the way it is travelling"), Ride.Board->GetForwardSpeed() > 0.0f);
	return true;
}

namespace
{
	/** Flies the kite of a stationary rider and records what it did. */
	struct FKiteFlight
	{
		float PeakTensionN = 0.0f;
		float MinElevationDeg = 90.0f;

		void Fly(UKiteComponent* Kite, float Steer, float Seconds)
		{
			Kite->SteerKite(Steer);
			for (float Elapsed = 0.0f; Elapsed < Seconds; Elapsed += RideDeltaTime)
			{
				Kite->UpdateKite(RideDeltaTime);
				PeakTensionN = FMath::Max(PeakTensionN, Kite->GetLineTensionN());
				MinElevationDeg = FMath::Min(MinElevationDeg, Kite->GetElevationDeg());
			}
			Kite->SteerKite(0.0f);
		}
	};

	/** A stationary rider in steady 15 kn wind along +X. */
	struct FStandingFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;
		UKiteComponent* Kite = nullptr;

		FStandingFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (Pawn)
			{
				Kite = Pawn->GetKite();
				if (UWindComponent* Wind = Pawn->GetWind())
				{
					Wind->BaseWind = FVector(15.0f * KnotCmS, 0.0f, 0.0f);
					Wind->GustStrength = 0.0f;
					Wind->DirectionDriftDeg = 0.0f;
				}
				Kite->SheetKite(0.7f);
			}
		}

		~FStandingFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
	};
}

// With the bar centred the kite turns nose-out and parks at the window edge, wherever it is.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteParksAtWindowEdge, "KiteSurf.Kite.ParksAtWindowEdge", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteParksAtWindowEdge::RunTest(const FString& Parameters)
{
	FStandingFixture Standing;
	UKiteComponent* Kite = Standing.Kite;
	TestNotNull(TEXT("Kite created"), Kite);
	if (!Kite)
	{
		return false;
	}


	for (float ClockDeg : { 0.0f, 45.0f, -60.0f })
	{
		// Start well inside the window: the kite should climb out to the edge and stop.
		Kite->SetWindowPosition(ClockDeg, 30.0f);
		FKiteFlight Flight;
		Flight.Fly(Kite, 0.0f, 8.0f);

		TestTrue(FString::Printf(TEXT("From clock %.0f the kite settles near the window edge (depth %.1f deg)"), ClockDeg, Kite->GetWindowDepthDeg()),
			Kite->GetWindowDepthDeg() > 3.0f && Kite->GetWindowDepthDeg() < 25.0f);
		TestTrue(FString::Printf(TEXT("From clock %.0f the lines are tight"), ClockDeg), Kite->AreLinesTaut());
		TestNearlyEqual(FString::Printf(TEXT("From clock %.0f the kite stays at that clock position"), ClockDeg), Kite->GetClockDeg(), ClockDeg, 4.0f);
		TestTrue(FString::Printf(TEXT("From clock %.0f the kite has stopped moving (%.0f cm/s)"), ClockDeg, Kite->GetKiteVelocity().Size()), Kite->GetKiteVelocity().Size() < 60.0f);
	}
	return true;
}

// A loop taken too low puts the kite in the water; it lies there with slack lines and then relaunches.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteCrashesAndRelaunches, "KiteSurf.Kite.CrashesAndRelaunches", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteCrashesAndRelaunches::RunTest(const FString& Parameters)
{
	FStandingFixture Standing;
	UKiteComponent* Kite = Standing.Kite;
	TestNotNull(TEXT("Kite created"), Kite);
	if (!Kite)
	{
		return false;
	}

	// Parked low on the right, then pulled hard right: the nose swings down into the water.
	Kite->SetWindowPosition(70.0f, 10.0f);
	FKiteFlight Settle;
	Settle.Fly(Kite, 0.0f, 4.0f);
	TestFalse(TEXT("A parked kite is flying"), Kite->IsCrashed());

	Kite->SetLoopHeld(true);
	Kite->SteerKite(1.0f);
	float SecondsToCrash = 0.0f;
	while (!Kite->IsCrashed() && SecondsToCrash < 5.0f)
	{
		Kite->UpdateKite(RideDeltaTime);
		SecondsToCrash += RideDeltaTime;
	}
	Kite->SetLoopHeld(false);
	Kite->SteerKite(0.0f);

	TestTrue(FString::Printf(TEXT("Looping a low kite puts it in the water (%.1f s)"), SecondsToCrash), Kite->IsCrashed());
	TestTrue(FString::Printf(TEXT("The kite is at the water (%.0f cm)"), Kite->GetKiteWorldPosition().Z), Kite->GetKiteWorldPosition().Z <= Kite->CrashHeightCm + 0.1f);
	TestNearlyEqual(TEXT("A kite on the water does not pull"), Kite->GetLineTensionN(), 0.0f, 0.001f);
	TestTrue(TEXT("It is on the side it was flying on"), Kite->GetAzimuthDeg() > 20.0f);

	// It stays down for the relaunch delay...
	FKiteFlight Down;
	Down.Fly(Kite, 0.0f, Kite->RelaunchDelaySeconds - 0.5f);
	TestTrue(TEXT("It stays down until the relaunch delay has passed"), Kite->IsCrashed());
	TestNearlyEqual(TEXT("Still no pull while it is down"), Down.PeakTensionN, 0.0f, 0.001f);

	// ...then relaunches by itself and flies again.
	FKiteFlight Up;
	Up.Fly(Kite, 0.0f, 4.0f);
	TestFalse(TEXT("It relaunches by itself"), Kite->IsCrashed());
	TestTrue(FString::Printf(TEXT("and is back above the water (%.1f deg)"), Kite->GetElevationDeg()), Kite->GetElevationDeg() >= Kite->MinElevationDeg - 0.5f);
	TestTrue(FString::Printf(TEXT("and pulling again (%.0f N)"), Kite->GetLineTensionN()), Kite->GetLineTensionN() > 100.0f);

	// With no wind at all the kite cannot fly: it falls out of the sky.
	if (UWindComponent* Wind = Standing.Pawn->GetWind())
	{
		Wind->BaseWind = FVector::ZeroVector;
	}
	Kite->SetWindowPosition(0.0f, 45.0f);
	FKiteFlight Becalmed;
	Becalmed.Fly(Kite, 0.0f, 10.0f);
	TestTrue(TEXT("With no wind the kite falls into the water"), Kite->IsCrashed());
	return true;
}

// Plain steering travels the kite round the window edge and it stops where the bar is centred.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteSteeringTravelsRoundTheWindow, "KiteSurf.Kite.SteeringTravelsRoundTheWindow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteSteeringTravelsRoundTheWindow::RunTest(const FString& Parameters)
{
	FStandingFixture Standing;
	UKiteComponent* Kite = Standing.Kite;
	TestNotNull(TEXT("Kite created"), Kite);
	if (!Kite)
	{
		return false;
	}

	Kite->SetWindowPosition(60.0f, 10.0f);
	FKiteFlight Settle;
	Settle.Fly(Kite, 0.0f, 4.0f);

	// Bar left: up over the top and down the left side.
	FKiteFlight Travel;
	float Seconds = 0.0f;
	while (Kite->GetClockDeg() > -45.0f && Seconds < 12.0f)
	{
		Travel.Fly(Kite, -1.0f, RideDeltaTime);
		Seconds += RideDeltaTime;
	}
	TestTrue(FString::Printf(TEXT("Steering left carries the kite from 2 o'clock to 10:30 in %.1f s"), Seconds), Seconds < 12.0f);
	TestTrue(FString::Printf(TEXT("It stays above the water on the way (lowest elevation %.1f deg)"), Travel.MinElevationDeg), Travel.MinElevationDeg >= Kite->MinElevationDeg - 4.0f);
	TestNearlyEqual(TEXT("Plain steering does not count as looping"), Kite->GetTurnDeg(), 0.0f, 0.1f);

	// Bar centred: it stops there.
	const float ClockAtRelease = Kite->GetClockDeg();
	FKiteFlight Park;
	Park.Fly(Kite, 0.0f, 6.0f);
	TestTrue(FString::Printf(TEXT("Centring the bar parks the kite (%.0f cm/s)"), Kite->GetKiteVelocity().Size()), Kite->GetKiteVelocity().Size() < 60.0f);
	TestNearlyEqual(TEXT("It parks close to where the bar was centred"), Kite->GetClockDeg(), ClockAtRelease, 25.0f);

	// Bar right held all the way: it comes back over and stops above the water on the right.
	FKiteFlight Back;
	Back.Fly(Kite, 1.0f, 15.0f);
	TestTrue(FString::Printf(TEXT("Held right, the kite ends low on the right (clock %.0f)"), Kite->GetClockDeg()), Kite->GetClockDeg() > 60.0f);
	TestFalse(TEXT("and stays out of the water"), Kite->IsCrashed());
	TestTrue(FString::Printf(TEXT("pulling out within a few degrees of the minimum elevation (lowest %.1f deg)"), Back.MinElevationDeg), Back.MinElevationDeg >= Kite->MinElevationDeg - 4.0f);
	TestTrue(FString::Printf(TEXT("and stays near the window edge (depth %.0f deg)"), Kite->GetWindowDepthDeg()), Kite->GetWindowDepthDeg() < 30.0f);
	return true;
}

// Holding the bar over flies the kite round in a loop, and a looping kite pulls much harder than a parked one.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteLoopsWhenSteerHeld, "KiteSurf.Kite.LoopsWhenSteerHeld", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteLoopsWhenSteerHeld::RunTest(const FString& Parameters)
{
	for (float SteerDirection : { 1.0f, -1.0f })
	{
		FStandingFixture Standing;
		UKiteComponent* Kite = Standing.Kite;
		TestNotNull(TEXT("Kite created"), Kite);
		if (!Kite)
		{
			return false;
		}

		Kite->SetWindowPosition(0.0f, 10.0f);
		FKiteFlight Parked;
		Parked.Fly(Kite, 0.0f, 5.0f);
		const float ParkedTensionN = Kite->GetLineTensionN();

		FKiteFlight Loop;
		Kite->SetLoopHeld(true);
		Loop.Fly(Kite, SteerDirection, 4.5f);
		Kite->SetLoopHeld(false);
		const float TurnedDeg = Kite->GetTurnDeg() * SteerDirection;
		UE_LOG(LogKiteSurf, Log, TEXT("LoopsWhenSteerHeld (steer %+.0f): turned %.0f deg, peak %.0f N vs parked %.0f N, lowest elevation %.1f deg"),
			SteerDirection, TurnedDeg, Loop.PeakTensionN, ParkedTensionN, Loop.MinElevationDeg);

		TestTrue(FString::Printf(TEXT("Steer %+.0f for 4.5 s winds the kite into at least one full loop (%.0f deg)"), SteerDirection, TurnedDeg), TurnedDeg >= 360.0f);
		TestTrue(FString::Printf(TEXT("Looping peaks at %.0f N, at least twice the parked %.0f N"), Loop.PeakTensionN, ParkedTensionN), Loop.PeakTensionN >= 2.0f * ParkedTensionN);
		TestTrue(TEXT("Tension stays within the cap"), Loop.PeakTensionN <= Kite->MaxLineTensionN + 0.1f);
		TestFalse(TEXT("Loops started from the top of the window stay out of the water"), Kite->IsCrashed());

		// Centre the bar: the kite flies back out to the edge and settles down again.
		FKiteFlight Recovery;
		Recovery.Fly(Kite, 0.0f, 8.0f);
		TestTrue(FString::Printf(TEXT("After the loop the kite parks again (%.0f cm/s)"), Kite->GetKiteVelocity().Size()), Kite->GetKiteVelocity().Size() < 60.0f);
		TestNearlyEqual(TEXT("Parked tension returns to about what it was"), Kite->GetLineTensionN(), ParkedTensionN, ParkedTensionN * 0.4f);
		TestNearlyEqual(TEXT("The turn counter clears once the bar is centred"), Kite->GetTurnDeg(), 0.0f, 0.1f);
	}
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
	// In 28 kn a 12 m kite at the default bar position lifts the rider off the water, so that
	// case rides sheeted out, as a rider would.
	struct FWindCase { float WindKnots; float Sheet; float MinKnots; float MaxKnots; };
	const FWindCase Cases[] = { { 12.0f, 0.7f, 9.0f, 18.0f }, { 20.0f, 0.7f, 16.0f, 28.0f }, { 28.0f, 0.3f, 18.0f, 34.0f } };

	for (const FWindCase& Case : Cases)
	{
		FRideFixture Ride(Case.WindKnots);
		TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
		if (!Ride.IsValid())
		{
			return false;
		}
		Ride.Pawn->SheetKite(Case.Sheet);
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

	// Fly the kite over to the other side and ride away on the new tack.
	Ride.SteerKiteToLeftSide();
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

// Carving is the same on both tacks: A on one mirrors D on the other.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRideCarveIsSymmetric, "KiteSurf.Ride.CarveIsSymmetric", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfRideCarveIsSymmetric::RunTest(const FString& Parameters)
{
	auto CarveDownwind = [](float TackSide) -> float
	{
		FRideFixture Ride(15.0f, TackSide);
		if (!Ride.IsValid())
		{
			return 0.0f;
		}
		Ride.Simulate(3.0f);
		const float StartYaw = Ride.Pawn->GetActorRotation().Yaw;
		// Turn towards downwind: left on the right-hand tack, right on the left-hand tack.
		Ride.Pawn->EdgeBoard(-TackSide);
		Ride.Simulate(1.5f);
		return FRotator::NormalizeAxis(Ride.Pawn->GetActorRotation().Yaw - StartYaw);
	};

	const float RightTackTurnDeg = CarveDownwind(1.0f);
	const float LeftTackTurnDeg = CarveDownwind(-1.0f);
	UE_LOG(LogKiteSurf, Log, TEXT("CarveIsSymmetric: right tack turned %.1f deg, left tack turned %.1f deg"), RightTackTurnDeg, LeftTackTurnDeg);

	TestTrue(FString::Printf(TEXT("A full carve for 1.5 s turns the board at least 40 deg (%.1f)"), RightTackTurnDeg), RightTackTurnDeg <= -40.0f);
	TestNearlyEqual(TEXT("The mirrored carve on the other tack turns the same amount the other way"), LeftTackTurnDeg, -RightTackTurnDeg, 1.0f);
	return true;
}

// Weight along the board trades grip for speed: on the tail the rail digs in and the board slips
// less; on the nose the board runs flatter and slides off downwind more.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRideWeightShiftChangesLeeway, "KiteSurf.Ride.WeightShiftChangesLeeway", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfRideWeightShiftChangesLeeway::RunTest(const FString& Parameters)
{
	struct FRun { float LeewayDeg; float PitchDeg; };
	auto RideWithWeight = [](float WeightShift) -> FRun
	{
		FRideFixture Ride;
		if (!Ride.IsValid())
		{
			return { 0.0f, 0.0f };
		}
		Ride.Board->SetWeightShift(WeightShift);
		Ride.Simulate(15.0f);
		return {
			FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(Ride.Board->GetLateralSpeed()), Ride.Board->GetForwardSpeed())),
			static_cast<float>(Ride.Pawn->GetActorRotation().Pitch) };
	};

	const FRun OnNose = RideWithWeight(1.0f);
	const FRun Neutral = RideWithWeight(0.0f);
	const FRun OnTail = RideWithWeight(-1.0f);
	UE_LOG(LogKiteSurf, Log, TEXT("WeightShiftChangesLeeway: nose %.1f deg, neutral %.1f deg, tail %.1f deg; pitch nose %.1f, tail %.1f"),
		OnNose.LeewayDeg, Neutral.LeewayDeg, OnTail.LeewayDeg, OnNose.PitchDeg, OnTail.PitchDeg);

	TestTrue(FString::Printf(TEXT("Weight on the tail slips less than neutral (%.1f < %.1f deg)"), OnTail.LeewayDeg, Neutral.LeewayDeg), OnTail.LeewayDeg < Neutral.LeewayDeg);
	TestTrue(FString::Printf(TEXT("Weight on the nose slips more than neutral (%.1f > %.1f deg)"), OnNose.LeewayDeg, Neutral.LeewayDeg), OnNose.LeewayDeg > Neutral.LeewayDeg);
	TestTrue(FString::Printf(TEXT("Weight on the nose tips the nose down (pitch %.1f)"), OnNose.PitchDeg), OnNose.PitchDeg < -4.0f);
	TestTrue(FString::Printf(TEXT("Weight on the tail lifts the nose (pitch %.1f)"), OnTail.PitchDeg), OnTail.PitchDeg > 4.0f);
	return true;
}

// Sending the kite up overhead and sheeting in as it gets there lifts the rider off the water, and they come down riding.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRideKiteLiftsRiderOff, "KiteSurf.Ride.KiteLiftsRiderOff", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfRideKiteLiftsRiderOff::RunTest(const FString& Parameters)
{
	FRideFixture Ride(26.0f);
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}

	Ride.Pawn->SheetKite(0.5f);
	Ride.Simulate(5.0f);
	TestTrue(TEXT("On the water before the kite is sent"), Ride.Board->GetBoardState() == EBoardState::Planing);

	// Send it: steer the kite up towards the zenith, then pull the bar in as it comes overhead.
	Ride.Pawn->SteerKite(-1.0f);
	bool bLiftedOff = false;
	float PeakHeightCm = 0.0f;
	for (float Elapsed = 0.0f; Elapsed < 7.0f; Elapsed += RideDeltaTime)
	{
		Ride.Simulate(RideDeltaTime);
		if (Ride.Kite->GetClockDeg() < 30.0f)
		{
			Ride.Pawn->SheetKite(1.0f);
		}
		if (Ride.Kite->GetClockDeg() < 0.0f)
		{
			Ride.Pawn->SteerKite(0.0f); // keep the kite overhead
		}
		bLiftedOff |= Ride.Board->GetBoardState() == EBoardState::Airborne;
		PeakHeightCm = FMath::Max(PeakHeightCm, Ride.Board->GetCurrentJumpHeight());
	}
	UE_LOG(LogKiteSurf, Log, TEXT("KiteLiftsRiderOff: lifted %d, peak height %.0f cm, state %d, crashing %d"), bLiftedOff, PeakHeightCm, (int32)Ride.Board->GetBoardState(), Ride.Board->IsCrashing());

	TestTrue(TEXT("The kite lifted the rider off the water without a pop"), bLiftedOff);
	// Without a pop the kite only plucks the rider up: once they are off the water nothing stops
	// them being pulled downwind, which takes the wind out of the kite.
	TestTrue(FString::Printf(TEXT("The kite alone carried the rider at least 40 cm up (%.0f cm)"), PeakHeightCm), PeakHeightCm >= 40.0f);

	// Sheet out and come down.
	Ride.Pawn->SheetKite(0.3f);
	Ride.Simulate(8.0f);
	TestTrue(TEXT("Back on the water"), Ride.Board->GetBoardState() != EBoardState::Airborne);
	TestFalse(TEXT("The rider did not crash"), Ride.Board->IsCrashing());
	return true;
}

// The kite only pulls while its lines are tight. When the wind dies, they go slack and the kite
// falls; when the rider outruns the wind, the same happens.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteFallsWhenLinesGoSlack, "KiteSurf.Kite.FallsWhenLinesGoSlack", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteFallsWhenLinesGoSlack::RunTest(const FString& Parameters)
{
	FStandingFixture Standing;
	UKiteComponent* Kite = Standing.Kite;
	UWindComponent* Wind = Standing.Pawn ? Standing.Pawn->GetWind() : nullptr;
	TestTrue(TEXT("Kite and wind created"), Kite && Wind);
	if (!Kite || !Wind)
	{
		return false;
	}

	Kite->SetWindowPosition(0.0f, 10.0f);
	FKiteFlight Parked;
	Parked.Fly(Kite, 0.0f, 5.0f);
	const float ParkedHeightCm = Kite->GetKiteWorldPosition().Z;
	TestTrue(TEXT("Parked overhead with tight lines"), Kite->AreLinesTaut() && Kite->GetLineTensionN() > 150.0f);
	TestTrue(FString::Printf(TEXT("Parked angle of attack %.1f deg is below the stall"), Kite->GetAngleOfAttackDeg()), Kite->GetAngleOfAttackDeg() > 0.0f && Kite->GetAngleOfAttackDeg() < Kite->StallAngleDeg);

	// The wind dies. Within a second or two the lines are slack, the pull is gone and the kite is coming down.
	Wind->BaseWind = FVector::ZeroVector;
	FKiteFlight Lull;
	Lull.Fly(Kite, 0.0f, 1.5f);
	TestFalse(TEXT("With no wind the lines go slack"), Kite->AreLinesTaut());
	TestNearlyEqual(TEXT("Slack lines do not pull"), Kite->GetLineTensionN(), 0.0f, 0.001f);
	TestTrue(FString::Printf(TEXT("The kite is falling (%.0f cm/s)"), Kite->GetKiteVelocity().Z), Kite->GetKiteVelocity().Z < -50.0f);
	TestTrue(TEXT("and has lost height"), Kite->GetKiteWorldPosition().Z < ParkedHeightCm - 100.0f);

	// The wind comes back before it reaches the water: the lines come tight and it flies again.
	Wind->BaseWind = FVector(15.0f * KnotCmS, 0.0f, 0.0f);
	FKiteFlight Recovery;
	Recovery.Fly(Kite, 0.0f, 8.0f);
	TestFalse(TEXT("With the wind back the kite did not end up in the water"), Kite->IsCrashed());
	TestTrue(TEXT("The lines are tight again"), Kite->AreLinesTaut());
	TestTrue(FString::Printf(TEXT("and it pulls again (%.0f N)"), Kite->GetLineTensionN()), Kite->GetLineTensionN() > 150.0f);

	// Left without wind, it ends up in the water and stays there.
	Wind->BaseWind = FVector::ZeroVector;
	FKiteFlight Becalmed;
	Becalmed.Fly(Kite, 0.0f, 20.0f);
	TestTrue(TEXT("Left without wind the kite falls into the water"), Kite->IsCrashed());
	TestNearlyEqual(TEXT("and does not pull"), Kite->GetLineTensionN(), 0.0f, 0.001f);
	return true;
}

// The rider on the board follows the chosen character.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPawnRiderCharacterSelection, "KiteSurf.Pawn.RiderCharacterSelection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPawnRiderCharacterSelection::RunTest(const FString& Parameters)
{
	FStandingFixture Standing;
	AKiteRiderPawn* Pawn = Standing.Pawn;
	TestNotNull(TEXT("Pawn created"), Pawn);
	if (!Pawn || !Pawn->GetRiderStaticMesh() || !Pawn->GetRiderMesh())
	{
		return false;
	}
	const UStaticMeshComponent* Posed = Pawn->GetRiderStaticMesh();
	const USkeletalMeshComponent* Robot = Pawn->GetRiderMesh();

	TestEqual(TEXT("Santa is the default rider"), Pawn->GetRiderCharacter(), ERiderCharacter::Santa);
	TestTrue(TEXT("Santa is shown"), Posed->IsVisible() && Posed->GetStaticMesh() && Posed->GetStaticMesh()->GetName() == TEXT("SM_RiderSanta"));
	TestFalse(TEXT("The robot is hidden behind Santa"), Robot->IsVisible());

	Pawn->SetRiderCharacter(ERiderCharacter::Wetsuit);
	TestTrue(TEXT("The wetsuit rider is shown"), Posed->IsVisible() && Posed->GetStaticMesh() && Posed->GetStaticMesh()->GetName() == TEXT("SM_RiderWetsuit"));

	Pawn->SetRiderCharacter(ERiderCharacter::Robot);
	TestTrue(TEXT("The robot is shown"), Robot->IsVisible());
	TestFalse(TEXT("The posed rider is hidden behind the robot"), Posed->IsVisible());

	// The settings button steps through every rider and comes back round.
	ERiderCharacter Character = ERiderCharacter::Santa;
	TSet<ERiderCharacter> Seen;
	for (int32 Step = 0; Step < static_cast<int32>(ERiderCharacter::Count); ++Step)
	{
		Seen.Add(Character);
		Character = RiderCharacter::Next(Character);
	}
	TestEqual(TEXT("Cycling visits every rider"), Seen.Num(), static_cast<int32>(ERiderCharacter::Count));
	TestEqual(TEXT("and returns to the first"), Character, ERiderCharacter::Santa);
	TestEqual(TEXT("A stored index out of range falls back to Santa"), RiderCharacter::FromIndex(99), ERiderCharacter::Santa);

	UKiteSurfSettingsWidget* Settings = NewObject<UKiteSurfSettingsWidget>();
	const ERiderCharacter Before = Settings->CurrentRiderCharacter;
	Settings->CycleRiderCharacter();
	TestEqual(TEXT("The settings screen steps to the next rider"), Settings->CurrentRiderCharacter, RiderCharacter::Next(Before));
	return true;
}

// Guards the output of scripts/editor/import_geometry.py and create_materials.py.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfAssetsKiteAndRiderMeshes, "KiteSurf.Assets.KiteAndRiderMeshes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfAssetsKiteAndRiderMeshes::RunTest(const FString& Parameters)
{
	struct FExpectedMesh { const TCHAR* Path; int32 MaterialSlots; float MinSizeCm; };
	const FExpectedMesh ExpectedMeshes[] =
	{
		{ TEXT("/Game/Meshes/SM_Kite.SM_Kite"), 2, 400.0f },                 // canopy and tubes; a 12 m2 kite spans over 4 m
		{ TEXT("/Game/Meshes/SM_RiderSanta.SM_RiderSanta"), 4, 150.0f },     // skin, white, red, black
		{ TEXT("/Game/Meshes/SM_RiderWetsuit.SM_RiderWetsuit"), 4, 150.0f }, // skin, wetsuit, accent, black
	};
	for (const FExpectedMesh& Expected : ExpectedMeshes)
	{
		const UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Expected.Path);
		TestNotNull(FString::Printf(TEXT("%s loads"), Expected.Path), Mesh);
		if (!Mesh)
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s material slot count"), Expected.Path), Mesh->GetStaticMaterials().Num(), Expected.MaterialSlots);
		for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
		{
			TestTrue(FString::Printf(TEXT("%s slot %s uses a project material"), Expected.Path, *Slot.MaterialSlotName.ToString()),
				Slot.MaterialInterface && Slot.MaterialInterface->GetPathName().StartsWith(TEXT("/Game/Materials/")));
		}
		const float LargestExtentCm = static_cast<float>(Mesh->GetBounds().BoxExtent.GetMax()) * 2.0f;
		TestTrue(FString::Printf(TEXT("%s is at least %.0f cm across (%.0f)"), Expected.Path, Expected.MinSizeCm, LargestExtentCm), LargestExtentCm >= Expected.MinSizeCm);
	}

	TestNotNull(TEXT("The kite canopy texture loads"), LoadObject<UTexture2D>(nullptr, TEXT("/Game/Textures/T_KiteCanopy.T_KiteCanopy")));
	const UMaterialInterface* Canopy = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_KiteCanopy.M_KiteCanopy"));
	TestTrue(TEXT("The canopy material is two-sided, so the kite shows from both sides"), Canopy && Canopy->IsTwoSided());
	return true;
}

// A rider without planing speed floats sunk in the water, and comes up onto the surface as the kite gets them going.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRideFloatsUntilPlaning, "KiteSurf.Ride.FloatsUntilPlaning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfRideFloatsUntilPlaning::RunTest(const FString& Parameters)
{
	FRideFixture Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	UBoardMovementComponent* Board = Ride.Board;

	TestNearlyEqual(TEXT("At planing speed the board rides on the surface"), Board->GetFloatDepthForSpeed(Board->PlaningThresholdCmS), 0.0f, 0.01f);
	TestNearlyEqual(TEXT("Stopped, it floats at the full depth"), Board->GetFloatDepthForSpeed(0.0f), Board->FloatSubmersionCm, 0.01f);

	// Planing at the start of a ride: on the surface.
	Ride.Simulate(3.0f);
	TestFalse(TEXT("A planing rider is not floating"), Board->IsFloating());
	TestTrue(FString::Printf(TEXT("and rides at the surface (Z %.0f cm)"), Ride.Pawn->GetActorLocation().Z), Ride.Pawn->GetActorLocation().Z > -20.0f);

	// Bar right out with the kite parked overhead: the board stops and the rider sinks to floating depth.
	Ride.Pawn->SheetKite(0.0f);
	Ride.Kite->SetWindowPosition(0.0f, 10.0f);
	Ride.Simulate(15.0f);
	UE_LOG(LogKiteSurf, Log, TEXT("FloatsUntilPlaning: stopped at %.1f kn, Z %.0f cm, float depth %.0f cm"), Ride.SpeedKnots(), Ride.Pawn->GetActorLocation().Z, Board->GetFloatDepthCm());
	TestTrue(FString::Printf(TEXT("Depowered under a parked kite the board slows right down (%.1f kn)"), Ride.SpeedKnots()), Ride.SpeedKnots() < 3.5f);
	TestTrue(TEXT("The rider is floating"), Board->IsFloating());
	TestTrue(FString::Printf(TEXT("sunk towards the floating depth (Z %.0f cm)"), Ride.Pawn->GetActorLocation().Z), Ride.Pawn->GetActorLocation().Z < -0.6f * Board->FloatSubmersionCm);

	// Kite back down to the side and the bar in: a water start.
	Ride.Pawn->SheetKite(0.8f);
	Ride.Kite->SetWindowPosition(65.0f, 8.0f);
	Ride.Simulate(20.0f);
	UE_LOG(LogKiteSurf, Log, TEXT("FloatsUntilPlaning: water start reached %.1f kn, Z %.0f cm"), Ride.SpeedKnots(), Ride.Pawn->GetActorLocation().Z);
	TestTrue(TEXT("The kite pulls the rider back onto the plane"), Board->IsPlaning());
	TestFalse(TEXT("and out of the water"), Board->IsFloating());
	TestTrue(FString::Printf(TEXT("riding at the surface again (Z %.0f cm)"), Ride.Pawn->GetActorLocation().Z), Ride.Pawn->GetActorLocation().Z > -20.0f);
	return true;
}

// The HUD shows the steering that reaches the kite next to the rider's bar: the bar itself while
// looping, the assist's correction otherwise, and nothing while the kite is in the water.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteAppliedSteer, "KiteSurf.Kite.AppliedSteer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteAppliedSteer::RunTest(const FString& Parameters)
{
	FStandingFixture Standing;
	UKiteComponent* Kite = Standing.Kite;
	TestNotNull(TEXT("Kite created"), Kite);
	if (!Kite)
	{
		return false;
	}

	// Parked and settled with the bar centred, the assist has little left to do.
	Kite->SetWindowPosition(45.0f, 10.0f);
	FKiteFlight Parked;
	Parked.Fly(Kite, 0.0f, 6.0f);
	TestTrue(FString::Printf(TEXT("A settled, parked kite needs little steering (%.2f)"), Kite->GetAppliedSteer()), FMath::Abs(Kite->GetAppliedSteer()) < 0.5f);

	// Looping, the rider's bar goes straight to the kite.
	Kite->SetLoopHeld(true);
	FKiteFlight Loop;
	Loop.Fly(Kite, 0.6f, 0.2f);
	TestNearlyEqual(TEXT("With loop held the kite gets exactly the bar"), Kite->GetAppliedSteer(), 0.6f, 0.001f);
	Loop.Fly(Kite, -1.0f, 0.1f);
	TestNearlyEqual(TEXT("in both directions"), Kite->GetAppliedSteer(), -1.0f, 0.001f);
	Kite->SetLoopHeld(false);

	// Bar hard over without the loop: the assist steers the kite that way to start it travelling.
	Kite->SetWindowPosition(0.0f, 10.0f);
	FKiteFlight Travel;
	Travel.Fly(Kite, 1.0f, 0.3f);
	const float TravelRight = Kite->GetAppliedSteer();
	Kite->SetWindowPosition(0.0f, 10.0f);
	Travel.Fly(Kite, -1.0f, 0.3f);
	const float TravelLeft = Kite->GetAppliedSteer();
	TestTrue(FString::Printf(TEXT("Bar right and bar left steer the kite opposite ways (%.2f, %.2f)"), TravelRight, TravelLeft), TravelRight * TravelLeft < 0.0f);
	TestTrue(TEXT("Applied steering stays within -1..1"), FMath::Abs(TravelRight) <= 1.0f && FMath::Abs(TravelLeft) <= 1.0f);
	return true;
}

// The rider's feet are in the straps: they stand square across the board and turn with it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRiderSpinsWithBoard, "KiteSurf.Rider.SpinsWithBoard", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfRiderSpinsWithBoard::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A preferred facing on the board's right picks the right rail"), AKiteRiderPawn::ChooseStanceSide(0.0f, 80.0f), 1.0f);
	TestEqual(TEXT("and one on its left picks the left rail"), AKiteRiderPawn::ChooseStanceSide(0.0f, -100.0f), -1.0f);
	TestEqual(TEXT("across the +-180 seam too"), AKiteRiderPawn::ChooseStanceSide(170.0f, -110.0f), 1.0f);

	FRideFixture Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	AKiteRiderPawn* Pawn = Ride.Pawn;
	auto FacingOffBoardDeg = [Pawn]() -> float { return FRotator::NormalizeAxis(Pawn->GetRiderFacingYawDeg() - static_cast<float>(Pawn->GetActorRotation().Yaw)); };
	auto FacingTowardsKite = [Pawn, &Ride]() -> float
	{
		const FVector ToKite = (Ride.Kite->GetKiteWorldPosition() - Pawn->GetActorLocation()).GetSafeNormal2D();
		return FVector::DotProduct(FRotator(0.0f, Pawn->GetRiderFacingYawDeg(), 0.0f).Vector(), ToKite);
	};

	// Riding: square across the board, on the rail that faces the kite.
	Ride.Simulate(2.0f);
	TestNearlyEqual(TEXT("The rider stands square across the board"), FMath::Abs(FacingOffBoardDeg()), 90.0f, 0.02f);
	TestTrue(FString::Printf(TEXT("and faces the kite (%.2f)"), FacingTowardsKite()), FacingTowardsKite() > 0.3f);
	const float Side = Pawn->GetRiderStanceSide();
	const float OffBoardDeg = FacingOffBoardDeg();

	// Spin the board one full turn, as an air spin does. The rider goes round with it, the same
	// rail under their toes the whole way, and so ends up with their back to the kite half way.
	float RiderTurnedDeg = 0.0f;
	float PreviousFacingDeg = Pawn->GetRiderFacingYawDeg();
	float FacingKiteAtHalfTurn = 1.0f;
	bool bStayedSquare = true;
	const float SpinStepDeg = 5.0f;
	const int32 SpinSteps = 72;
	for (int32 Step = 1; Step <= SpinSteps; ++Step)
	{
		Pawn->SetActorRotation(FRotator(0.0f, Pawn->GetActorRotation().Yaw + SpinStepDeg, 0.0f));
		Pawn->Tick(RideDeltaTime);
		RiderTurnedDeg += FRotator::NormalizeAxis(Pawn->GetRiderFacingYawDeg() - PreviousFacingDeg);
		PreviousFacingDeg = Pawn->GetRiderFacingYawDeg();
		bStayedSquare = bStayedSquare && FMath::IsNearlyEqual(FacingOffBoardDeg(), OffBoardDeg, 0.01f);
		if (Step == SpinSteps / 2)
		{
			FacingKiteAtHalfTurn = FacingTowardsKite();
		}
	}
	TestTrue(TEXT("Through the spin the rider keeps the same stance on the board"), bStayedSquare && Pawn->GetRiderStanceSide() == Side);
	TestNearlyEqual(TEXT("The rider turned as far as the board did"), RiderTurnedDeg, 360.0f, 0.1f);
	TestTrue(FString::Printf(TEXT("Half way round the rider has their back to the kite (%.2f)"), FacingKiteAtHalfTurn), FacingKiteAtHalfTurn < -0.3f);

	// A twin-tip swapping ends is the board's heading flipping, not the rider turning round.
	const float FacingBeforeSwapDeg = Pawn->GetRiderFacingYawDeg();
	Pawn->SetActorRotation(FRotator(0.0f, Pawn->GetActorRotation().Yaw + 180.0f, 0.0f));
	Pawn->Tick(RideDeltaTime);
	TestNearlyEqual(TEXT("When the board swaps ends the rider stays facing the same way"), static_cast<float>(FRotator::NormalizeAxis(Pawn->GetRiderFacingYawDeg() - FacingBeforeSwapDeg)), 0.0f, 0.01f);
	TestEqual(TEXT("which is the other rail of the renamed board"), Pawn->GetRiderStanceSide(), -Side);

	// Carve half a turn so the rider is riding toeside, back to the kite, and stays that way.
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Pawn->SetActorRotation(FRotator(0.0f, Pawn->GetActorRotation().Yaw + 3.0f, 0.0f));
		Pawn->Tick(RideDeltaTime);
	}
	TestTrue(FString::Printf(TEXT("After carving half a turn the rider is toeside, back to the kite (%.2f)"), FacingTowardsKite()), FacingTowardsKite() < -0.3f);

	// A reset puts the rider back on the board facing the kite.
	Ride.Board->ResetToTack(12.0f);
	Ride.Simulate(1.0f);
	TestNearlyEqual(TEXT("After a reset the rider is square across the board"), FMath::Abs(FacingOffBoardDeg()), 90.0f, 0.02f);
	TestTrue(FString::Printf(TEXT("and faces the kite again (%.2f)"), FacingTowardsKite()), FacingTowardsKite() > 0.3f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
