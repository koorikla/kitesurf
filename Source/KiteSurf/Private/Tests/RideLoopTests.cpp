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

	const float GlideRatio = FMath::Lerp(Kite->GlideRatioSheetedOut, Kite->GlideRatioSheetedIn, Kite->Sheet);
	const float ParkedDepthDeg = 90.0f - FMath::RadiansToDegrees(FMath::Atan(GlideRatio));

	for (float ClockDeg : { 0.0f, 45.0f, -60.0f })
	{
		// Start well inside the window: the kite should climb out to the edge and stop.
		Kite->SetWindowPosition(ClockDeg, 30.0f);
		FKiteFlight Flight;
		Flight.Fly(Kite, 0.0f, 8.0f);

		TestNearlyEqual(FString::Printf(TEXT("From clock %.0f the kite settles at the depth its glide ratio gives"), ClockDeg), Kite->GetWindowDepthDeg(), ParkedDepthDeg, 1.5f);
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
	TestNearlyEqual(TEXT("The kite is at the water"), Kite->GetElevationDeg(), Kite->CrashElevationDeg, 0.5f);
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
	TestTrue(FString::Printf(TEXT("It stays near the window edge on the way (lowest elevation %.1f deg)"), Travel.MinElevationDeg), Travel.MinElevationDeg >= Kite->MinElevationDeg - 0.5f);
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
	TestTrue(FString::Printf(TEXT("and stays off the water (lowest elevation %.1f deg)"), Back.MinElevationDeg), Back.MinElevationDeg >= Kite->MinElevationDeg - 0.5f);
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
		Loop.Fly(Kite, SteerDirection, 6.0f);
		Kite->SetLoopHeld(false);
		const float TurnedDeg = Kite->GetTurnDeg() * SteerDirection;
		UE_LOG(LogKiteSurf, Log, TEXT("LoopsWhenSteerHeld (steer %+.0f): turned %.0f deg, peak %.0f N vs parked %.0f N, lowest elevation %.1f deg"),
			SteerDirection, TurnedDeg, Loop.PeakTensionN, ParkedTensionN, Loop.MinElevationDeg);

		TestTrue(FString::Printf(TEXT("Steer %+.0f for 6 s flies at least two full loops (%.0f deg)"), SteerDirection, TurnedDeg), TurnedDeg >= 720.0f);
		TestTrue(FString::Printf(TEXT("Looping peaks at %.0f N, at least twice the parked %.0f N"), Loop.PeakTensionN, ParkedTensionN), Loop.PeakTensionN >= 2.0f * ParkedTensionN);
		TestTrue(TEXT("Tension stays within the cap"), Loop.PeakTensionN <= Kite->MaxLineTensionN + 0.1f);
		TestFalse(TEXT("Loops started from the top of the window stay out of the water"), Kite->IsCrashed());

		// Centre the bar: the kite flies back out to the edge and settles down again.
		FKiteFlight Recovery;
		Recovery.Fly(Kite, 0.0f, 8.0f);
		TestTrue(FString::Printf(TEXT("After the loop the kite parks again (%.0f cm/s)"), Kite->GetKiteVelocity().Size()), Kite->GetKiteVelocity().Size() < 60.0f);
		TestNearlyEqual(TEXT("Parked tension returns to what it was"), Kite->GetLineTensionN(), ParkedTensionN, ParkedTensionN * 0.25f);
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

// Sending the kite overhead with the bar in lifts the rider off the water, and they come down riding.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRideKiteLiftsRiderOff, "KiteSurf.Ride.KiteLiftsRiderOff", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfRideKiteLiftsRiderOff::RunTest(const FString& Parameters)
{
	FRideFixture Ride(22.0f);
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}

	Ride.Simulate(5.0f);
	TestTrue(TEXT("On the water before the kite is sent"), Ride.Board->GetBoardState() == EBoardState::Planing);

	// Send it: bar in, kite steered up towards the zenith.
	Ride.Pawn->SheetKite(1.0f);
	Ride.Pawn->SteerKite(-1.0f);
	bool bLiftedOff = false;
	float PeakHeightCm = 0.0f;
	for (float Elapsed = 0.0f; Elapsed < 6.0f; Elapsed += RideDeltaTime)
	{
		Ride.Simulate(RideDeltaTime);
		if (Ride.Kite->GetClockDeg() < 0.0f)
		{
			Ride.Pawn->SteerKite(0.0f); // keep the kite overhead
		}
		bLiftedOff |= Ride.Board->GetBoardState() == EBoardState::Airborne;
		PeakHeightCm = FMath::Max(PeakHeightCm, Ride.Board->GetCurrentJumpHeight());
	}
	UE_LOG(LogKiteSurf, Log, TEXT("KiteLiftsRiderOff: lifted %d, peak height %.0f cm, state %d, crashing %d"), bLiftedOff, PeakHeightCm, (int32)Ride.Board->GetBoardState(), Ride.Board->IsCrashing());

	TestTrue(TEXT("The kite lifted the rider off the water without a pop"), bLiftedOff);
	TestTrue(FString::Printf(TEXT("The jump reached at least 1 m (%.0f cm)"), PeakHeightCm), PeakHeightCm >= 100.0f);

	// Sheet out and come down.
	Ride.Pawn->SheetKite(0.2f);
	Ride.Simulate(8.0f);
	TestTrue(TEXT("Back on the water"), Ride.Board->GetBoardState() != EBoardState::Airborne);
	TestTrue(TEXT("The landing was clean"), Ride.Board->WasLastLandingClean() && !Ride.Board->IsCrashing());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
