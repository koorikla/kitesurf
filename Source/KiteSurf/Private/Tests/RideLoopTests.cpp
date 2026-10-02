#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "KiteSurfSpot.h"
#include "UI/KiteSurfGameInstance.h"
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
				Pawn->Tick(RideDeltaTime);
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

	// Bar left, away from the kite: up over the top and onto the left side.
	FKiteFlight Travel;
	float Seconds = 0.0f;
	while (Kite->GetClockDeg() > -30.0f && Seconds < 12.0f)
	{
		Travel.Fly(Kite, -1.0f, RideDeltaTime);
		Seconds += RideDeltaTime;
	}
	TestTrue(FString::Printf(TEXT("Steering left carries the kite from 2 o'clock to 11 in %.1f s"), Seconds), Seconds < 12.0f);
	TestTrue(FString::Printf(TEXT("It stays above the water on the way (lowest elevation %.1f deg)"), Travel.MinElevationDeg), Travel.MinElevationDeg >= Kite->MinElevationDeg - 4.0f);
	TestNearlyEqual(TEXT("Flying the kite across is not a loop"), Kite->GetTurnDeg(), 0.0f, 0.1f);
	TestFalse(TEXT("and the assist is still flying it"), Kite->IsLooping());

	// Bar centred: it stops there.
	const float ClockAtRelease = Kite->GetClockDeg();
	FKiteFlight Park;
	Park.Fly(Kite, 0.0f, 6.0f);
	TestTrue(FString::Printf(TEXT("Centring the bar parks the kite (%.0f cm/s)"), Kite->GetKiteVelocity().Size()), Kite->GetKiteVelocity().Size() < 60.0f);
	TestNearlyEqual(TEXT("It parks close to where the bar was centred"), Kite->GetClockDeg(), ClockAtRelease, 25.0f);

	// Bar right: it comes back over the top. Kept held once the kite is on the right, the bar
	// goes straight to the kite and it loops there; no other key is involved.
	FKiteFlight Back;
	Seconds = 0.0f;
	while (!Kite->IsLooping() && Seconds < 12.0f)
	{
		Back.Fly(Kite, 1.0f, RideDeltaTime);
		Seconds += RideDeltaTime;
	}
	TestTrue(FString::Printf(TEXT("Held right, the kite crosses to the right and starts to loop (clock %.0f after %.1f s)"), Kite->GetClockDeg(), Seconds), Kite->IsLooping() && Kite->GetClockDeg() >= Kite->LoopClockDeg - 1.0f);
	TestTrue(FString::Printf(TEXT("staying above the water on the way (lowest elevation %.1f deg)"), Back.MinElevationDeg), Back.MinElevationDeg >= Kite->MinElevationDeg - 4.0f);

	FKiteFlight Loop;
	Seconds = 0.0f;
	while (FMath::Abs(Kite->GetTurnDeg()) < 360.0f && !Kite->IsCrashed() && Seconds < 6.0f)
	{
		Loop.Fly(Kite, 1.0f, RideDeltaTime);
		Seconds += RideDeltaTime;
	}
	UE_LOG(LogKiteSurf, Log, TEXT("SteeringTravelsRoundTheWindow: loop on the kite's own side took %.1f s, lowest elevation %.1f deg, peak %.0f N"), Seconds, Loop.MinElevationDeg, Loop.PeakTensionN);
	TestTrue(FString::Printf(TEXT("Still held, it flies a full loop (%.0f deg in %.1f s)"), Kite->GetTurnDeg(), Seconds), FMath::Abs(Kite->GetTurnDeg()) >= 360.0f);
	TestFalse(TEXT("without going into the water"), Kite->IsCrashed());

	// Let go: the assist takes the kite back and parks it.
	FKiteFlight Recover;
	Recover.Fly(Kite, 0.0f, 6.0f);
	TestFalse(TEXT("Letting go of the bar ends the loop"), Kite->IsLooping());
	TestFalse(TEXT("and the kite stays out of the water"), Kite->IsCrashed());
	TestTrue(FString::Printf(TEXT("parked near the window edge (depth %.0f deg, %.0f cm/s)"), Kite->GetWindowDepthDeg(), Kite->GetKiteVelocity().Size()), Kite->GetWindowDepthDeg() < 30.0f && Kite->GetKiteVelocity().Size() < 150.0f);

	// Reversing the bar mid-loop is a new request, not more of the loop.
	Kite->SetWindowPosition(60.0f, 10.0f);
	Settle.Fly(Kite, 0.0f, 4.0f);
	FKiteFlight Start;
	Start.Fly(Kite, 1.0f, 0.3f);
	TestTrue(TEXT("Bar towards the kite's own side loops at once"), Kite->IsLooping());
	Start.Fly(Kite, -1.0f, RideDeltaTime);
	Kite->SteerKite(-1.0f);
	Kite->UpdateKite(RideDeltaTime);
	TestFalse(TEXT("Bar reversed: the kite is being flown to the other side instead"), Kite->IsLooping());
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

	// A tap moves the bar a little, at SheetRatePerSec.
	Pawn->SetSheetRateInput(-1.0f);
	Ride.Simulate(0.1f);
	const float SheetedOut = Pawn->GetCurrentSheetInput();
	TestNearlyEqual(TEXT("Holding sheet-out moves the bar at SheetRatePerSec"), SheetedOut, AKiteSurfGameMode::StartSheet - 0.1f * Pawn->SheetRatePerSec, 0.05f);

	Pawn->SetSheetRateInput(0.0f);
	Ride.Simulate(1.0f);
	TestNearlyEqual(TEXT("Releasing the key leaves the bar where it is"), Pawn->GetCurrentSheetInput(), SheetedOut, 0.001f);
	TestNearlyEqual(TEXT("The kite uses the bar position"), Ride.Kite->Sheet, SheetedOut, 0.001f);

	// The whole throw takes well under a second either way: the bar keeps up with the rider's hands.
	Pawn->SetSheetRateInput(1.0f);
	Ride.Simulate(0.5f);
	TestNearlyEqual(TEXT("Half a second of sheet-in reaches full power"), Pawn->GetCurrentSheetInput(), 1.0f, 0.001f);
	Pawn->SetSheetRateInput(-1.0f);
	Ride.Simulate(0.5f);
	TestNearlyEqual(TEXT("Half a second of sheet-out reaches fully depowered"), Pawn->GetCurrentSheetInput(), 0.0f, 0.001f);
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

	auto BarInFrontOfBody = [Pawn, &Ride]()
	{
		const FVector BarCentre = (Ride.Kite->GetBarEndWorldPosition(true) + Ride.Kite->GetBarEndWorldPosition(false)) * 0.5f;
		const FVector BodyFacing = FRotator(0.0f, Pawn->GetRiderBodyYawDeg(), 0.0f).Vector();
		const FVector FromHook = BarCentre - Pawn->GetHarnessHookWorldPosition();
		return FVector::DotProduct(FromHook, BodyFacing) > 0.0f && FromHook.Size() < 80.0f;
	};
	TestTrue(TEXT("The lines start at the bar, just in front of the harness hook"), BarInFrontOfBody());
	const FVector HookFromFeet = Pawn->GetHarnessHookWorldPosition() - Pawn->GetActorLocation();
	TestTrue(FString::Printf(TEXT("The hook is at the front of the waist (%.0f cm up)"), HookFromFeet.Z), HookFromFeet.Z > 70.0f && HookFromFeet.Z < 130.0f
		&& FVector::DotProduct(HookFromFeet, FRotator(0.0f, Pawn->GetRiderBodyYawDeg(), 0.0f).Vector()) > 0.0f);

	// Spin the board one full turn in the air. The rider goes round with it, the same rail under
	// their toes the whole way, and so has their back to the kite half way; the bar stays in
	// front of them with the lines coming over their shoulder.
	Ride.Board->SetBoardState(EBoardState::Airborne);
	Pawn->bStepSimulation = false; // the board is turned by hand from here on; Tick only poses the rider
	float RiderTurnedDeg = 0.0f;
	float PreviousFacingDeg = Pawn->GetRiderFacingYawDeg();
	float FacingKiteAtHalfTurn = 1.0f;
	bool bStayedSquare = true;
	bool bBarStayedInFront = true;
	const float SpinStepDeg = 5.0f;
	const int32 SpinSteps = 72;
	for (int32 Step = 1; Step <= SpinSteps; ++Step)
	{
		Pawn->SetActorRotation(FRotator(0.0f, Pawn->GetActorRotation().Yaw + SpinStepDeg, 0.0f));
		Pawn->Tick(RideDeltaTime);
		RiderTurnedDeg += FRotator::NormalizeAxis(Pawn->GetRiderFacingYawDeg() - PreviousFacingDeg);
		PreviousFacingDeg = Pawn->GetRiderFacingYawDeg();
		bStayedSquare = bStayedSquare && FMath::IsNearlyEqual(FacingOffBoardDeg(), OffBoardDeg, 0.01f);
		bBarStayedInFront = bBarStayedInFront && BarInFrontOfBody();
		if (Step == SpinSteps / 2)
		{
			FacingKiteAtHalfTurn = FacingTowardsKite();
		}
	}
	TestTrue(TEXT("Through the spin the rider keeps the same stance on the board"), bStayedSquare && Pawn->GetRiderStanceSide() == Side);
	TestNearlyEqual(TEXT("The rider turned as far as the board did"), RiderTurnedDeg, 360.0f, 0.1f);
	TestTrue(FString::Printf(TEXT("Half way round the rider has their back to the kite (%.2f)"), FacingKiteAtHalfTurn), FacingKiteAtHalfTurn < -0.3f);
	TestTrue(TEXT("The bar stays in front of the rider's body through the spin"), bBarStayedInFront);
	Ride.Board->SetBoardState(EBoardState::Planing);

	// A twin-tip swapping ends is the board's heading flipping, not the rider turning round.
	const float FacingBeforeSwapDeg = Pawn->GetRiderFacingYawDeg();
	Pawn->SetActorRotation(FRotator(0.0f, Pawn->GetActorRotation().Yaw + 180.0f, 0.0f));
	Pawn->Tick(RideDeltaTime);
	TestNearlyEqual(TEXT("When the board swaps ends the rider stays facing the same way"), static_cast<float>(FRotator::NormalizeAxis(Pawn->GetRiderFacingYawDeg() - FacingBeforeSwapDeg)), 0.0f, 0.01f);
	TestEqual(TEXT("which is the other rail of the renamed board"), Pawn->GetRiderStanceSide(), -Side);

	// Carve half a turn on the water. That leaves the rider's back to the kite, and the hook is
	// on their front: within a moment they slide the board round and face the kite again.
	for (int32 Step = 0; Step < 12; ++Step)
	{
		Pawn->SetActorRotation(FRotator(0.0f, Pawn->GetActorRotation().Yaw + 15.0f, 0.0f));
		Pawn->Tick(RideDeltaTime);
	}
	TestTrue(FString::Printf(TEXT("Straight after carving half a turn the rider has their back to the kite (%.2f)"), FacingTowardsKite()), FacingTowardsKite() < -0.3f);
	for (int32 Step = 0; Step < 90; ++Step)
	{
		Pawn->Tick(RideDeltaTime);
	}
	TestTrue(FString::Printf(TEXT("A moment later they have slid round to face it (%.2f)"), FacingTowardsKite()), FacingTowardsKite() > 0.3f);
	TestNearlyEqual(TEXT("still square across the board"), FMath::Abs(FacingOffBoardDeg()), 90.0f, 0.02f);
	TestNearlyEqual(TEXT("and the slide round has finished"), static_cast<float>(FRotator::NormalizeAxis(Pawn->GetRiderBodyYawDeg() - Pawn->GetRiderFacingYawDeg())), 0.0f, 0.01f);

	// A reset puts the rider back on the board facing the kite.
	Pawn->bStepSimulation = true;
	Ride.Board->ResetToTack(12.0f);
	Ride.Simulate(1.0f);
	TestNearlyEqual(TEXT("After a reset the rider is square across the board"), FMath::Abs(FacingOffBoardDeg()), 90.0f, 0.02f);
	TestTrue(FString::Printf(TEXT("and faces the kite again (%.2f)"), FacingTowardsKite()), FacingTowardsKite() > 0.3f);
	return true;
}

namespace
{
	struct FJumpResult
	{
		float PeakCm = 0.0f;
		float AirSeconds = 0.0f;
		float SlackSecondsInAir = 0.0f;
		bool bPulledOffEdge = false;
		bool bCrashed = false;
		bool bKiteDown = false;
	};

	/**
	 * Rides for a few seconds at 30 kn on the recommended kite, then jumps. With bSend the kite is
	 * steered hard up; with bHoldEdge the rider's weight is on the tail until ReleaseSeconds after
	 * that, when they pull the bar in and pop. ReleaseSeconds < 0 means never pop.
	 */
	FJumpResult RunJump(bool bSend, bool bHoldEdge, float ReleaseSeconds, EKiteModel Model = EKiteModel::Loop)
	{
		FJumpResult Result;
		FRideFixture Ride(30.0f);
		if (!Ride.IsValid())
		{
			return Result;
		}
		Ride.Kite->SetKiteModel(Model);
		Ride.Kite->SetKiteSize(UKiteComponent::RecommendKiteSizeM2(30.0f));
		Ride.Simulate(8.0f);

		if (bSend)
		{
			Ride.Pawn->SteerKite(-1.0f);
		}
		if (bHoldEdge)
		{
			Ride.Board->SetWeightShift(-1.0f);
		}
		bool bLeftWater = false;
		for (float Elapsed = 0.0f; Elapsed < 20.0f; Elapsed += RideDeltaTime)
		{
			Ride.Simulate(RideDeltaTime);
			const bool bAir = Ride.Board->GetBoardState() == EBoardState::Airborne;
			if (bAir && !bLeftWater)
			{
				// Off the water before the release: the kite pulled the rider off their edge.
				bLeftWater = true;
				Result.bPulledOffEdge = true;
				Ride.Board->SetWeightShift(0.0f);
				Ride.Pawn->SheetKite(1.0f);
			}
			if (!bLeftWater && ReleaseSeconds >= 0.0f && Elapsed >= ReleaseSeconds)
			{
				Ride.Board->SetWeightShift(-1.0f);
				Ride.Pawn->SheetKite(1.0f);
				Ride.Board->Jump();
				Ride.Board->SetWeightShift(0.0f);
				bLeftWater = true;
			}
			if (Ride.Kite->GetClockDeg() < 0.0f)
			{
				Ride.Pawn->SteerKite(0.0f); // keep the kite overhead
			}
			if (bAir)
			{
				Result.AirSeconds += RideDeltaTime;
				if (!Ride.Kite->AreLinesTaut())
				{
					Result.SlackSecondsInAir += RideDeltaTime;
				}
			}
			Result.PeakCm = FMath::Max(Result.PeakCm, Ride.Board->GetCurrentJumpHeight());
			if (bLeftWater && !bAir && Result.AirSeconds > 0.3f)
			{
				break;
			}
		}
		Ride.Simulate(1.0f);
		Result.bCrashed = Ride.Board->IsCrashing();
		Result.bKiteDown = Ride.Kite->IsCrashed();
		return Result;
	}
}

// Height has to be earned: a pop alone is a hop, sending the kite without an edge plucks the rider
// off early, and the big jump comes from holding the edge against the rising kite and letting go
// at the right moment.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfJumpTimedReleaseBeatsPop, "KiteSurf.Jump.TimedReleaseBeatsPop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfJumpTimedReleaseBeatsPop::RunTest(const FString& Parameters)
{
	const FJumpResult Pop = RunJump(false, false, 0.0f);
	const FJumpResult SendOnly = RunJump(true, false, -1.0f);
	const FJumpResult Timed = RunJump(true, true, 0.7f);
	const FJumpResult TooEarly = RunJump(true, true, 0.3f);
	const FJumpResult TooLate = RunJump(true, true, 3.0f);
	UE_LOG(LogKiteSurf, Log, TEXT("TimedReleaseBeatsPop: pop %.1f m, send only %.1f m, timed %.1f m (%.1f s in the air), early %.1f m, late %.1f m (pulled off %d)"),
		Pop.PeakCm / 100.0f, SendOnly.PeakCm / 100.0f, Timed.PeakCm / 100.0f, Timed.AirSeconds, TooEarly.PeakCm / 100.0f, TooLate.PeakCm / 100.0f, TooLate.bPulledOffEdge);

	TestTrue(FString::Printf(TEXT("A pop with the kite parked is a hop under 2 m (%.1f m)"), Pop.PeakCm / 100.0f), Pop.PeakCm > 30.0f && Pop.PeakCm < 200.0f);
	TestTrue(TEXT("Sending the kite without an edge pulls the rider off the water"), SendOnly.bPulledOffEdge);
	TestTrue(FString::Printf(TEXT("A well-timed release goes over 10 m (%.1f m)"), Timed.PeakCm / 100.0f), Timed.PeakCm > 1000.0f);
	TestFalse(TEXT("and was the rider's release, not the kite's"), Timed.bPulledOffEdge);
	TestTrue(TEXT("It beats sending the kite without holding an edge by half as much again"), Timed.PeakCm > 1.5f * SendOnly.PeakCm);
	TestTrue(TEXT("Letting go too early, before the kite has loaded up, is lower"), TooEarly.PeakCm < 0.8f * Timed.PeakCm);
	TestTrue(TEXT("Holding on too long gets the rider pulled off their edge"), TooLate.bPulledOffEdge);
	TestTrue(TEXT("which is lower too"), TooLate.PeakCm < 0.8f * Timed.PeakCm);

	// The rider hangs under a flying kite all the way.
	TestTrue(FString::Printf(TEXT("The lines stay tight through the big jump (%.2f s slack)"), Timed.SlackSecondsInAir), Timed.SlackSecondsInAir < 0.3f);
	TestTrue(FString::Printf(TEXT("and through the hop (%.2f s slack)"), Pop.SlackSecondsInAir), Pop.SlackSecondsInAir < 0.3f);
	TestFalse(TEXT("The kite stays in the air"), Timed.bKiteDown || Pop.bKiteDown || SendOnly.bKiteDown);
	TestFalse(TEXT("The rider lands the big jump"), Timed.bCrashed);
	return true;
}

// An edging rider leans against the lines with the board dug in and is much harder to lift.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfJumpEdgeHoldsRiderDown, "KiteSurf.Jump.EdgeHoldsRiderDown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfJumpEdgeHoldsRiderDown::RunTest(const FString& Parameters)
{
	for (const bool bEdging : { false, true })
	{
		FRideFixture Ride;
		if (!Ride.IsValid())
		{
			return false;
		}
		UBoardMovementComponent* Board = Ride.Board;
		const float WeightForce = Board->MassKg * 980.0f;
		Board->SetWeightShift(bEdging ? -1.0f : 0.0f);

		// Twice the rider's weight straight up: enough to lift a flat board, not an edged one.
		Board->AddExternalForce(FVector(0.0f, 0.0f, 2.0f * WeightForce));
		Board->TickComponent(RideDeltaTime, LEVELTICK_All, nullptr);
		if (bEdging)
		{
			TestTrue(TEXT("Edging, the rider holds twice their weight down"), Board->GetBoardState() != EBoardState::Airborne);

			// Past the edge's limit they go.
			Board->AddExternalForce(FVector(0.0f, 0.0f, (Board->LiftoffWeightFactor + Board->EdgedLiftoffWeightBonus + 0.2f) * WeightForce));
			Board->TickComponent(RideDeltaTime, LEVELTICK_All, nullptr);
			TestTrue(TEXT("but not more than the edge can take"), Board->GetBoardState() == EBoardState::Airborne);
		}
		else
		{
			TestTrue(TEXT("Riding flat, twice the rider's weight lifts them off"), Board->GetBoardState() == EBoardState::Airborne);
		}
	}
	return true;
}

// Riders rig for the wind: small kites when it blows, big ones when it does not.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteSizes, "KiteSurf.Kite.SizeForWind", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteSizes::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("15 kn: 12 m"), UKiteComponent::RecommendKiteSizeM2(15.0f), 12.0f);
	TestEqual(TEXT("20 kn: 9 m"), UKiteComponent::RecommendKiteSizeM2(20.0f), 9.0f);
	TestEqual(TEXT("30 kn: 6 m"), UKiteComponent::RecommendKiteSizeM2(30.0f), 6.0f);
	TestEqual(TEXT("40 kn: 5 m"), UKiteComponent::RecommendKiteSizeM2(40.0f), 5.0f);
	TestEqual(TEXT("8 kn: the biggest kite there is"), UKiteComponent::RecommendKiteSizeM2(8.0f), 17.0f);
	TestTrue(TEXT("A lighter rider takes a smaller kite"), UKiteComponent::RecommendKiteSizeM2(20.0f, 60.0f) < UKiteComponent::RecommendKiteSizeM2(20.0f, 85.0f));
	float Previous = 100.0f;
	for (float Knots = 8.0f; Knots <= 40.0f; Knots += 1.0f)
	{
		const float Size = UKiteComponent::RecommendKiteSizeM2(Knots);
		TestTrue(FString::Printf(TEXT("%.0f kn: %.0f m is a size on offer and no bigger than for less wind"), Knots, Size), UKiteComponent::GetKiteSizesM2().Contains(Size) && Size <= Previous);
		Previous = Size;
	}

	// A small kite pulls less, is lighter and turns tighter than a big one in the same wind.
	float ParkedTensionN[2] = { 0.0f, 0.0f };
	float TurnRadiusCm[2] = { 0.0f, 0.0f };
	const float Sizes[2] = { 7.0f, 14.0f };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FStandingFixture Standing;
		if (!Standing.Kite)
		{
			return false;
		}
		Standing.Kite->SetKiteSize(Sizes[Index]);
		TestEqual(TEXT("The kite is the size that was rigged"), Standing.Kite->AreaM2, Sizes[Index]);
		Standing.Kite->SetWindowPosition(45.0f, 10.0f);
		FKiteFlight Parked;
		Parked.Fly(Standing.Kite, 0.0f, 6.0f);
		ParkedTensionN[Index] = Standing.Kite->GetLineTensionN();
		TurnRadiusCm[Index] = Standing.Kite->MinTurnRadiusCm;
		TestTrue(TEXT("It flies: lines tight, not stalled"), Standing.Kite->AreLinesTaut() && Standing.Kite->GetAngleOfAttackDeg() < Standing.Kite->StallAngleDeg);
	}
	UE_LOG(LogKiteSurf, Log, TEXT("SizeForWind: parked in 15 kn, 7 m pulls %.0f N and 14 m pulls %.0f N"), ParkedTensionN[0], ParkedTensionN[1]);
	TestTrue(FString::Printf(TEXT("Twice the area pulls about twice as hard (%.0f N against %.0f N)"), ParkedTensionN[1], ParkedTensionN[0]), ParkedTensionN[1] > 1.5f * ParkedTensionN[0] && ParkedTensionN[1] < 2.6f * ParkedTensionN[0]);
	TestTrue(TEXT("The small kite turns tighter"), TurnRadiusCm[0] < TurnRadiusCm[1]);

	// The game instance rigs the recommended size unless one has been chosen.
	UKiteSurfGameInstance* GI = NewObject<UKiteSurfGameInstance>();
	GI->SetPendingWindKnots(30.0f);
	TestEqual(TEXT("A new game instance rigs the 9 m"), GI->GetEffectiveKiteSizeM2(), 9.0f);
	GI->SetKiteSizeM2(0.0f);
	TestEqual(TEXT("With no size chosen the kite is the recommended one"), GI->GetEffectiveKiteSizeM2(), 6.0f);
	GI->SetKiteSizeM2(9.0f);
	TestEqual(TEXT("A chosen size is used"), GI->GetEffectiveKiteSizeM2(), 9.0f);
	GI->SetKiteSizeM2(8.5f);
	TestEqual(TEXT("A size that is not on offer falls back to the recommended one"), GI->GetEffectiveKiteSizeM2(), 6.0f);
	return true;
}

// Gear changes how the kite and the board behave, in the direction the gear screen says.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfGearChangesBehaviour, "KiteSurf.Gear.ChangesBehaviour", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfGearChangesBehaviour::RunTest(const FString& Parameters)
{
	// Kite models: the same size parked in the same wind, then looped.
	float ParkedTensionN[2] = { 0.0f, 0.0f };
	float LoopTurnDeg[2] = { 0.0f, 0.0f };
	const EKiteModel Models[2] = { EKiteModel::Loop, EKiteModel::Boost };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FStandingFixture Standing;
		if (!Standing.Kite)
		{
			return false;
		}
		Standing.Kite->SetKiteModel(Models[Index]);
		TestEqual(TEXT("The kite is the model that was rigged"), Standing.Kite->GetKiteModel(), Models[Index]);
		TestEqual(TEXT("Changing the model keeps the size"), Standing.Kite->AreaM2, 12.0f);
		Standing.Kite->SetWindowPosition(45.0f, 10.0f);
		FKiteFlight Parked;
		Parked.Fly(Standing.Kite, 0.0f, 6.0f);
		ParkedTensionN[Index] = Standing.Kite->GetLineTensionN();

		Standing.Kite->SetWindowPosition(0.0f, 10.0f);
		Parked.Fly(Standing.Kite, 0.0f, 4.0f);
		Standing.Kite->SetLoopHeld(true);
		FKiteFlight Loop;
		Loop.Fly(Standing.Kite, 1.0f, 3.0f);
		LoopTurnDeg[Index] = FMath::Abs(Standing.Kite->GetTurnDeg());
		Standing.Kite->SetLoopHeld(false);
	}
	UE_LOG(LogKiteSurf, Log, TEXT("GearChangesBehaviour: parked pull loop %.0f N, boost %.0f N; turned in 3 s loop %.0f deg, boost %.0f deg"), ParkedTensionN[0], ParkedTensionN[1], LoopTurnDeg[0], LoopTurnDeg[1]);
	TestTrue(FString::Printf(TEXT("The boost kite pulls at least as hard parked (%.0f N against %.0f N)"), ParkedTensionN[1], ParkedTensionN[0]), ParkedTensionN[1] >= ParkedTensionN[0]);

	// Where the boost kite earns its name: each kite released at its own best moment (the boost
	// kite turns slower, so its send takes a little longer to load up), it goes higher and
	// stays up longer.
	const FJumpResult LoopJump = RunJump(true, true, 0.7f, EKiteModel::Loop);
	const FJumpResult BoostJump = RunJump(true, true, 0.8f, EKiteModel::Boost);
	UE_LOG(LogKiteSurf, Log, TEXT("GearChangesBehaviour: best timed jump, loop kite %.1f m / %.1f s, boost kite %.1f m / %.1f s (pulled off %d %d)"), LoopJump.PeakCm / 100.0f, LoopJump.AirSeconds, BoostJump.PeakCm / 100.0f, BoostJump.AirSeconds, LoopJump.bPulledOffEdge, BoostJump.bPulledOffEdge);
	TestFalse(TEXT("Neither rider was pulled off their edge"), LoopJump.bPulledOffEdge || BoostJump.bPulledOffEdge);
	TestTrue(FString::Printf(TEXT("The boost kite jumps higher (%.1f m against %.1f m)"), BoostJump.PeakCm / 100.0f, LoopJump.PeakCm / 100.0f), BoostJump.PeakCm > 1.1f * LoopJump.PeakCm);
	TestTrue(FString::Printf(TEXT("and hangs longer (%.1f s against %.1f s)"), BoostJump.AirSeconds, LoopJump.AirSeconds), BoostJump.AirSeconds > LoopJump.AirSeconds);
	TestTrue(FString::Printf(TEXT("The loop kite turns further in the same time (%.0f deg against %.0f deg)"), LoopTurnDeg[0], LoopTurnDeg[1]), LoopTurnDeg[0] > 1.15f * LoopTurnDeg[1]);

	// Boards: the reference board is the component's own defaults.
	FRideFixture Ride;
	if (!Ride.IsValid())
	{
		return false;
	}
	UBoardMovementComponent* Board = Ride.Board;
	const float ReferencePop = Board->BaseJumpImpulse;
	const float ReferencePlaning = Board->PlaningThresholdCmS;
	const float ReferenceGrip = Board->EdgeGripCoef;
	const float ReferenceTurn = Board->CarveTurnRate;
	Board->SetBoardSize(EBoardSize::Medium);
	TestTrue(TEXT("The 138 is the board the simulation is tuned for"), Board->BaseJumpImpulse == ReferencePop && Board->PlaningThresholdCmS == ReferencePlaning && Board->EdgeGripCoef == ReferenceGrip && Board->CarveTurnRate == ReferenceTurn);

	Board->SetBoardSize(EBoardSize::Small);
	TestTrue(TEXT("The small board pops harder"), Board->BaseJumpImpulse > ReferencePop);
	TestTrue(TEXT("needs more speed to plane"), Board->PlaningThresholdCmS > ReferencePlaning);
	TestTrue(TEXT("so it is still sunk at a speed the 138 planes at"), Board->GetFloatDepthForSpeed(ReferencePlaning) > 0.0f);
	TestTrue(TEXT("and turns quicker with less grip"), Board->CarveTurnRate > ReferenceTurn && Board->EdgeGripCoef < ReferenceGrip);

	Board->SetBoardSize(EBoardSize::Large);
	TestTrue(TEXT("The big board pops less"), Board->BaseJumpImpulse < ReferencePop);
	TestTrue(TEXT("planes earlier"), Board->PlaningThresholdCmS < ReferencePlaning);
	TestNearlyEqual(TEXT("so it is on the surface at a speed the 138 is still coming up at"), Board->GetFloatDepthForSpeed(0.9f * ReferencePlaning), 0.0f, 0.01f);
	TestTrue(TEXT("and grips harder but turns slower"), Board->EdgeGripCoef > ReferenceGrip && Board->CarveTurnRate < ReferenceTurn);

	// Light wind, starting slow: the big board gets up and planes where the small one stays sunk.
	float SpeedKn[2] = { 0.0f, 0.0f };
	float DepthCm[2] = { 0.0f, 0.0f };
	bool bPlaning[2] = { false, false };
	const EBoardSize Boards[2] = { EBoardSize::Small, EBoardSize::Large };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FRideFixture LightWind(12.0f);
		LightWind.Kite->SetKiteSize(UKiteComponent::RecommendKiteSizeM2(12.0f));
		LightWind.Board->SetBoardSize(Boards[Index]);
		LightWind.Board->ResetToTack(4.0f);
		LightWind.Simulate(30.0f);
		SpeedKn[Index] = LightWind.SpeedKnots();
		DepthCm[Index] = LightWind.Board->GetFloatDepthCm();
		bPlaning[Index] = LightWind.Board->IsPlaning();
	}
	UE_LOG(LogKiteSurf, Log, TEXT("GearChangesBehaviour: 30 s after a slow start in 12 kn the small board does %.1f kn (planing %d, %.0f cm deep) and the big board %.1f kn (planing %d, %.0f cm deep)"),
		SpeedKn[0], bPlaning[0], DepthCm[0], SpeedKn[1], bPlaning[1], DepthCm[1]);
	TestTrue(FString::Printf(TEXT("In 12 kn the big board gets up and planes (%.1f kn)"), SpeedKn[1]), bPlaning[1] && DepthCm[1] < 5.0f);
	TestTrue(FString::Printf(TEXT("while the small board stays sunk and slow (%.1f kn, %.0f cm deep)"), SpeedKn[0], DepthCm[0]), !bPlaning[0] && DepthCm[0] > 20.0f && SpeedKn[0] < SpeedKn[1]);
	return true;
}

// The spot's sand: laid out clear of the start, a crash to ride onto, and something to jump over.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSpotSand, "KiteSurf.Spot.Sand", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfSpotSand::RunTest(const FString& Parameters)
{
	FRideFixture Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	AKiteSurfSpot* Spot = Ride.World->SpawnActor<AKiteSurfSpot>();
	TestNotNull(TEXT("Spot spawned"), Spot);
	if (!Spot)
	{
		return false;
	}
	const FVector Origin = Ride.Pawn->GetActorLocation();
	const FVector Downwind = Ride.Kite->GetDownwindDir();
	const FVector Across = FVector::CrossProduct(FVector::UpVector, Downwind);

	// Everything off: open water.
	Spot->Setup(Ride.Pawn, Origin, Downwind, false, false, false);
	TestEqual(TEXT("With everything off there is no sand"), Spot->GetObstacles().Num(), 0);
	TestEqual(TEXT("and no sharks"), Spot->GetSharks().Num(), 0);

	// Sand on.
	Spot->Setup(Ride.Pawn, Origin, Downwind, true, true, false);
	int32 Islands = 0;
	int32 Sandbars = 0;
	for (const FSpotObstacle& Obstacle : Spot->GetObstacles())
	{
		(Obstacle.Type == ESpotObstacleType::Island ? Islands : Sandbars)++;
	}
	TestTrue(FString::Printf(TEXT("There are islands and sandbars (%d, %d)"), Islands, Sandbars), Islands >= 2 && Sandbars >= 3);

	// The ride opens on clear water: nothing within the clear radius of the start.
	bool bStartClear = true;
	for (float AngleDeg = 0.0f; AngleDeg < 360.0f; AngleDeg += 5.0f)
	{
		for (float Radius = 0.0f; Radius <= Spot->ClearStartRadiusCm; Radius += 250.0f)
		{
			bStartClear = bStartClear && Spot->GetSandHeightCm(Origin + FRotator(0.0f, AngleDeg, 0.0f).Vector() * Radius) <= 0.0f;
		}
	}
	TestTrue(TEXT("No sand within the clear radius of the start"), bStartClear);

	// The first sandbar lies across the opening reach, and stands out of the water along its middle.
	const FSpotObstacle* FirstBar = Spot->GetObstacles().FindByPredicate([](const FSpotObstacle& O) { return O.Type == ESpotObstacleType::Sandbar; });
	TestNotNull(TEXT("There is a sandbar"), FirstBar);
	if (!FirstBar)
	{
		return false;
	}
	const FVector BarCentre(FirstBar->Centre.X, FirstBar->Centre.Y, Origin.Z);
	const float TopCm = Spot->GetSandHeightCm(BarCentre);
	TestTrue(FString::Printf(TEXT("The sandbar's crest is 30 to 80 cm out of the water (%.0f cm)"), TopCm), TopCm > 30.0f && TopCm < 80.0f);
	TestTrue(TEXT("It is ahead of the start on the opening reach"), FVector::DotProduct(BarCentre - Origin, Across) > Spot->ClearStartRadiusCm);
	TestTrue(TEXT("There is water either side of it along the reach"), Spot->GetSandHeightCm(BarCentre + Across * 1000.0f) <= 0.0f && Spot->GetSandHeightCm(BarCentre - Across * 1000.0f) <= 0.0f);
	TestTrue(TEXT("Its long axis lies along the wind, across the reach"), FirstBar->GetVisibleExtent().X > 2000.0f && Spot->GetSandHeightCm(BarCentre + Downwind * 2000.0f) > 0.0f);

	// Riding onto it is a crash, and the rider is put back in the water on the side they came from.
	const FVector Approach = BarCentre - Across * 900.0f;
	Ride.Pawn->SetActorLocation(Approach);
	Ride.Board->Velocity = Across * 900.0f;
	Ride.Board->SetBoardState(EBoardState::Planing);
	bool bCrashed = false;
	for (int32 Step = 0; Step < 240 && !bCrashed; ++Step)
	{
		Ride.Simulate(RideDeltaTime);
		Spot->StepSpot(RideDeltaTime);
		bCrashed = Ride.Board->IsCrashing();
	}
	TestTrue(TEXT("Riding onto the sandbar is a crash"), bCrashed);
	TestEqual(TEXT("and the spot says why"), Spot->GetLastEvent(), FString(TEXT("Ran aground")));
	TestTrue(FString::Printf(TEXT("The rider is back in the water (sand %.0f cm)"), Spot->GetSandHeightCm(Ride.Pawn->GetActorLocation())), Spot->GetSandHeightCm(Ride.Pawn->GetActorLocation()) <= 0.0f);
	TestTrue(TEXT("on the side they came from"), FVector::DotProduct(Ride.Pawn->GetActorLocation() - BarCentre, Across) < 0.0f);

	// Jumping it is fine: above the crest there is nothing to hit.
	Spot->Setup(Ride.Pawn, Origin, Downwind, true, true, false);
	Ride.Board->ResetToTack(12.0f);
	Ride.Pawn->SetActorLocation(BarCentre + FVector(0.0f, 0.0f, 250.0f));
	Ride.Board->Velocity = Across * 900.0f;
	Ride.Board->SetBoardState(EBoardState::Airborne);
	Spot->StepSpot(RideDeltaTime);
	TestFalse(TEXT("A rider 2.5 m above the sandbar clears it"), Ride.Board->IsCrashing());
	TestTrue(TEXT("and nothing happened"), Spot->GetLastEvent().IsEmpty());

	// Switching the sand off mid-ride removes it.
	Spot->SetFeatures(false, false, false);
	TestTrue(TEXT("Switched off, the sandbar is gone"), Spot->GetSandHeightCm(BarCentre) <= 0.0f);
	return true;
}

// Sharks patrol, come for a rider who is down in the water, and are a crash to ride into.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfSpotSharks, "KiteSurf.Spot.Sharks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfSpotSharks::RunTest(const FString& Parameters)
{
	FRideFixture Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	AKiteSurfSpot* Spot = Ride.World->SpawnActor<AKiteSurfSpot>();
	if (!Spot)
	{
		return false;
	}
	const FVector Origin = Ride.Pawn->GetActorLocation();
	Spot->Setup(Ride.Pawn, Origin, Ride.Kite->GetDownwindDir(), false, false, true);
	TestEqual(TEXT("The sharks are in the water"), Spot->GetSharks().Num(), Spot->SharkCount);
	TestEqual(TEXT("and there is no sand"), Spot->GetObstacles().Num(), 0);
	if (Spot->GetSharks().Num() == 0)
	{
		return false;
	}

	// With the rider up and riding far away they keep to their patrol circles.
	bool bStartClear = true;
	for (const FSpotShark& Shark : Spot->GetSharks())
	{
		bStartClear = bStartClear && FVector2D::Distance(Shark.Position, FVector2D(Origin.X, Origin.Y)) > 5000.0f;
	}
	TestTrue(TEXT("No shark starts within 50 m of the rider"), bStartClear);
	Ride.Pawn->SetActorLocation(Origin - FVector(300000.0f, 0.0f, 0.0f));
	const FVector2D FirstPosition = Spot->GetSharks()[0].Position;
	for (int32 Step = 0; Step < 600; ++Step)
	{
		Spot->StepSpot(RideDeltaTime);
	}
	const FSpotShark& Patroller = Spot->GetSharks()[0];
	TestFalse(TEXT("A shark with no rider near is not hunting"), Patroller.bHunting);
	TestTrue(TEXT("It has moved"), FVector2D::Distance(Patroller.Position, FirstPosition) > 500.0f);
	TestNearlyEqual(TEXT("and stayed on its patrol circle"), static_cast<float>(FVector2D::Distance(Patroller.Position, Patroller.PatrolCentre)), Spot->SharkPatrolRadiusCm, 150.0f);

	// A rider up on the board and riding near a shark is left alone.
	const FVector2D SharkAt = Spot->GetSharks()[0].Position;
	const FVector NearShark(SharkAt.X + 2500.0f, SharkAt.Y, Origin.Z);
	Ride.Board->ResetToTack(12.0f);
	Ride.Pawn->SetActorLocation(NearShark);
	Spot->StepSpot(RideDeltaTime);
	TestFalse(TEXT("A planing rider 25 m away is not hunted"), Spot->GetSharks()[0].bHunting);

	// Down in the water at the same place, the shark comes, and gets there.
	Ride.Board->Velocity = FVector::ZeroVector;
	Ride.Pawn->SheetKite(0.0f);
	Ride.Kite->SetWindowPosition(0.0f, 10.0f);
	for (int32 Step = 0; Step < 180; ++Step)
	{
		Ride.Simulate(RideDeltaTime);
		Ride.Board->Velocity = FVector(0.0f, 0.0f, Ride.Board->Velocity.Z); // held still, as a rider who has lost the kite
	}
	TestTrue(TEXT("The rider is floating"), Ride.Board->IsFloating());
	const float DistanceBefore = FVector2D::Distance(Spot->GetSharks()[0].Position, FVector2D(Ride.Pawn->GetActorLocation()));
	bool bHunted = false;
	bool bBitten = false;
	float Seconds = 0.0f;
	for (; Seconds < 30.0f && !bBitten; Seconds += RideDeltaTime)
	{
		Spot->StepSpot(RideDeltaTime);
		bHunted = bHunted || Spot->GetSharks()[0].bHunting;
		bBitten = Ride.Board->IsCrashing();
	}
	TestTrue(TEXT("The shark goes for a rider floating nearby"), bHunted);
	TestTrue(FString::Printf(TEXT("and reaches them (from %.0f m, in %.1f s)"), DistanceBefore / 100.0f, Seconds), bBitten);
	TestEqual(TEXT("The spot says what happened"), Spot->GetLastEvent(), FString(TEXT("Shark!")));

	// Having had its bite it leaves the rider alone for a while.
	Spot->StepSpot(RideDeltaTime);
	TestFalse(TEXT("The shark that bit is not hunting straight afterwards"), Spot->GetSharks()[0].bHunting);

	// Riding over a shark is a crash; jumping over one is not.
	Spot->Setup(Ride.Pawn, Origin, Ride.Kite->GetDownwindDir(), false, false, true);
	const FVector2D Fin = Spot->GetSharks()[1].Position;
	Ride.Board->ResetToTack(12.0f);
	Ride.Pawn->SetActorLocation(FVector(Fin.X, Fin.Y, Origin.Z + 300.0f));
	Ride.Board->SetBoardState(EBoardState::Airborne);
	Spot->StepSpot(RideDeltaTime);
	TestFalse(TEXT("Three metres above a shark the rider clears it"), Ride.Board->IsCrashing());
	Ride.Pawn->SetActorLocation(FVector(Fin.X, Fin.Y, Origin.Z));
	Ride.Board->SetBoardState(EBoardState::Planing);
	Spot->StepSpot(RideDeltaTime);
	TestTrue(TEXT("On the water on top of a shark is a crash"), Ride.Board->IsCrashing());
	return true;
}

// The bar is the throttle: right out the kite barely pulls, right in it pulls several times harder.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRideBarIsTheThrottle, "KiteSurf.Ride.BarIsTheThrottle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfRideBarIsTheThrottle::RunTest(const FString& Parameters)
{
	const float SheetValues[3] = { 0.0f, 0.5f, 1.0f };
	float SpeedKn[3] = { 0.0f, 0.0f, 0.0f };
	float TensionN[3] = { 0.0f, 0.0f, 0.0f };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		// The default kite in the default wind.
		FRideFixture Ride(20.0f);
		if (!Ride.IsValid())
		{
			return false;
		}
		Ride.Kite->SetKiteSize(9.0f);
		Ride.Pawn->SheetKite(SheetValues[Index]);
		Ride.Simulate(20.0f);
		SpeedKn[Index] = Ride.SpeedKnots();
		TensionN[Index] = Ride.Kite->GetLineTensionN();
		TestFalse(TEXT("The kite stays in the air at any bar position"), Ride.Kite->IsCrashed());
		TestTrue(FString::Printf(TEXT("and does not stall (angle of attack %.1f deg)"), Ride.Kite->GetAngleOfAttackDeg()), Ride.Kite->GetAngleOfAttackDeg() < Ride.Kite->StallAngleDeg);
	}
	UE_LOG(LogKiteSurf, Log, TEXT("BarIsTheThrottle: 9 m in 20 kn, bar out %.1f kn / %.0f N, half %.1f kn / %.0f N, in %.1f kn / %.0f N"), SpeedKn[0], TensionN[0], SpeedKn[1], TensionN[1], SpeedKn[2], TensionN[2]);

	TestTrue(FString::Printf(TEXT("Bar right in pulls at least five times as hard as bar right out (%.0f N against %.0f N)"), TensionN[2], TensionN[0]), TensionN[2] > 5.0f * TensionN[0]);
	TestTrue(FString::Printf(TEXT("Half way is in between (%.0f N)"), TensionN[1]), TensionN[1] > 1.5f * TensionN[0] && TensionN[2] > 1.5f * TensionN[1]);
	TestTrue(FString::Printf(TEXT("Bar right out slows the rider to a crawl (%.1f kn)"), SpeedKn[0]), SpeedKn[0] < 10.0f);
	TestTrue(FString::Printf(TEXT("Bar right in is at least twice as fast (%.1f kn)"), SpeedKn[2]), SpeedKn[2] > 2.0f * SpeedKn[0] && SpeedKn[2] > 18.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
