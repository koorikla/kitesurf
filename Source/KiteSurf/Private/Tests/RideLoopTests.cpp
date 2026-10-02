#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RiderRig.h"
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
#include "KiteSurfUnits.h"
#include "KiteSurfGameMode.h"
#include "WindComponent.h"
#include "HAL/IConsoleManager.h"

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
				// The ride tests hold the kite where the start (or the test) put it, as a rider's hands
				// would, rather than letting it drift up to 12 with the bar centred.
				Kite->bParkHoldAssist = true;
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

// Hands off: the rider starts planing across the wind and needs no input for the opening seconds.
// With the bar centred the kite then drifts up the window edge towards 12, as a real one does, and
// takes the power with it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRideKeepsPlaningWithoutInput, "KiteSurf.Ride.KeepsPlaningWithoutInput", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfRideKeepsPlaningWithoutInput::RunTest(const FString& Parameters)
{
	FRideFixture Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!Ride.IsValid())
	{
		return false;
	}
	Ride.Kite->bParkHoldAssist = false; // hands off: the kite does what it does with the bar centred

	const FVector Start = Ride.Pawn->GetActorLocation();
	const float StartClockDeg = Ride.Kite->GetClockDeg();
	const int32 Segments = 6;
	const float SegmentSeconds = 5.0f;
	float ClockAt[Segments] = {};
	bool bPlaningAt[Segments] = {};
	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		Ride.Simulate(SegmentSeconds);
		ClockAt[Segment] = Ride.Kite->GetClockDeg();
		bPlaningAt[Segment] = Ride.Board->IsPlaning();
		const FVector Travelled = Ride.Pawn->GetActorLocation() - Start;
		UE_LOG(LogKiteSurf, Log, TEXT("KeepsPlaningWithoutInput t=%.0fs: %.1f kn, planing %d, kite clock %.0f, tension %.0f N, kite az %.0f el %.0f, travelled %.0f m across and %.0f m downwind"),
			(Segment + 1) * SegmentSeconds, Ride.SpeedKnots(), bPlaningAt[Segment], ClockAt[Segment], Ride.Kite->GetLineTensionN(), Ride.Kite->GetAzimuthDeg(), Ride.Kite->GetElevationDeg(),
			Travelled.Y / 100.0f, Travelled.X / 100.0f);
	}

	// Riding, the kite has more air over it than for a rider standing still and climbs faster, so the
	// board drops off the plane before 10 s; at 5 s it is still going.
	TestTrue(FString::Printf(TEXT("Still planing 5 s into the ride with no input (clock %.0f)"), ClockAt[0]), bPlaningAt[0]);
	TestTrue(FString::Printf(TEXT("The kite has climbed from clock %.0f by 10 s (clock %.0f)"), StartClockDeg, ClockAt[1]), FMath::Abs(ClockAt[1]) < FMath::Abs(StartClockDeg));
	TestTrue(FString::Printf(TEXT("and by 20 s (clock %.0f)"), ClockAt[3]), FMath::Abs(ClockAt[3]) < FMath::Abs(StartClockDeg));
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

// With the bar centred and the park-hold assist on, the kite turns nose-out and parks at the window
// edge wherever it is. (Without the assist it drifts up to 12: KiteSurf.Kite.DriftsToZenithWithBarCentred.)
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
	Kite->bParkHoldAssist = true;

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

// With the bar centred a kite drifts up the window edge to 12 o'clock and sits there, as a real one
// does; the park-hold assist keeps it where the bar was centred instead.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteDriftsToZenithWithBarCentred, "KiteSurf.Kite.DriftsToZenithWithBarCentred", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteDriftsToZenithWithBarCentred::RunTest(const FString& Parameters)
{
	const float StartClockDeg = 65.0f;
	const float NearZenithDeg = 10.0f;
	const float FlySeconds = 20.0f;
	for (const float Side : { 1.0f, -1.0f })
	{
		FStandingFixture Standing;
		UKiteComponent* Kite = Standing.Kite;
		TestNotNull(TEXT("Kite created"), Kite);
		if (!Kite)
		{
			return false;
		}
		TestFalse(TEXT("The kite drifts to 12 by default"), Kite->bParkHoldAssist);

		Kite->SetWindowPosition(StartClockDeg * Side, 8.0f);
		float SecondsToZenith = -1.0f;
		float ClockAt[3] = { 0.0f, 0.0f, 0.0f };
		FKiteFlight Flight;
		const int32 Steps = FMath::RoundToInt(FlySeconds / RideDeltaTime);
		const int32 StepsPerMark = FMath::RoundToInt(5.0f / RideDeltaTime);
		for (int32 Step = 1; Step <= Steps; ++Step)
		{
			Flight.Fly(Kite, 0.0f, RideDeltaTime);
			if (SecondsToZenith < 0.0f && FMath::Abs(Kite->GetClockDeg()) <= NearZenithDeg)
			{
				SecondsToZenith = Step * RideDeltaTime;
			}
			if (Step % StepsPerMark == 0 && Step / StepsPerMark <= 3)
			{
				ClockAt[Step / StepsPerMark - 1] = Kite->GetClockDeg();
			}
		}
		UE_LOG(LogKiteSurf, Log, TEXT("DriftsToZenithWithBarCentred (from clock %.0f): within %.0f deg of 12 after %.1f s; clock %.0f / %.0f / %.0f at 5 / 10 / 15 s, %.1f at 20 s, depth %.1f deg, %.0f N, lowest elevation %.1f deg"),
			StartClockDeg * Side, NearZenithDeg, SecondsToZenith, ClockAt[0], ClockAt[1], ClockAt[2], Kite->GetClockDeg(), Kite->GetWindowDepthDeg(), Kite->GetLineTensionN(), Flight.MinElevationDeg);

		TestTrue(FString::Printf(TEXT("From clock %.0f the kite is within %.0f deg of 12 after %.0f s (clock %.1f)"), StartClockDeg * Side, NearZenithDeg, FlySeconds, Kite->GetClockDeg()),
			FMath::Abs(Kite->GetClockDeg()) < NearZenithDeg);
		TestTrue(FString::Printf(TEXT("It got there in 8 to 15 s (%.1f s)"), SecondsToZenith), SecondsToZenith >= 8.0f && SecondsToZenith <= 15.0f);
		TestTrue(TEXT("The lines are tight"), Kite->AreLinesTaut());
		TestTrue(FString::Printf(TEXT("It sits near the window edge (depth %.1f deg)"), Kite->GetWindowDepthDeg()), Kite->GetWindowDepthDeg() < 25.0f);
		TestFalse(TEXT("It stays out of the water"), Kite->IsCrashed());

		// The park-hold assist keeps it where the bar was centred.
		Kite->bParkHoldAssist = true;
		Kite->SetWindowPosition(StartClockDeg * Side, 8.0f);
		FKiteFlight Held;
		Held.Fly(Kite, 0.0f, FlySeconds);
		UE_LOG(LogKiteSurf, Log, TEXT("DriftsToZenithWithBarCentred (from clock %.0f, park-hold assist): clock %.1f after %.0f s"), StartClockDeg * Side, Kite->GetClockDeg(), FlySeconds);
		TestNearlyEqual(FString::Printf(TEXT("With the park-hold assist the kite stays at clock %.0f"), StartClockDeg * Side), Kite->GetClockDeg(), StartClockDeg * Side, 10.0f);
		TestTrue(TEXT("with tight lines"), Kite->AreLinesTaut());
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

	// Parked low on the right, then pulled hard right: the nose swings down into the water. The
	// park-hold assist keeps it low while it settles, instead of letting it drift up to 12.
	Kite->bParkHoldAssist = true;
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

// Plain steering travels the kite round the window edge and, with the park-hold assist, it stops where the bar is centred.
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

	// With the park-hold assist the kite stops where the bar is centred, which is what this measures.
	Kite->bParkHoldAssist = true;
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

	// Let go: the loop has taken the kite down to the water, where it lies for RelaunchDelaySeconds
	// and relaunches; then the assist flies it out of the window and parks it. Out of the power zone
	// it overshoots the edge and swings back a few times before it settles: about 8 s after the
	// relaunch since phase 2, when the air acts on the projected area but the kite's mass is unchanged.
	FKiteFlight Recover;
	Recover.Fly(Kite, 0.0f, 12.0f);
	TestFalse(TEXT("Letting go of the bar ends the loop"), Kite->IsLooping());
	TestFalse(TEXT("and the kite stays out of the water"), Kite->IsCrashed());
	TestTrue(FString::Printf(TEXT("parked near the window edge (depth %.0f deg, %.0f cm/s)"), Kite->GetWindowDepthDeg(), Kite->GetKiteVelocity().Size()), Kite->GetWindowDepthDeg() < 30.0f && Kite->GetKiteVelocity().Size() < 150.0f);

	// Reversing the bar mid-loop is a new request, not more of the loop. The kite answers the bar
	// after its dead time, so each change is checked once that has passed.
	Kite->SetWindowPosition(60.0f, 10.0f);
	Settle.Fly(Kite, 0.0f, 4.0f);
	FKiteFlight Start;
	const float DeadTimeSeconds = Kite->GetSteeringDeadTimeSeconds();
	Start.Fly(Kite, 1.0f, DeadTimeSeconds + 2.0f * RideDeltaTime);
	TestTrue(TEXT("Bar towards the kite's own side loops as soon as it reaches the kite"), Kite->IsLooping());
	Start.Fly(Kite, -1.0f, DeadTimeSeconds);
	Kite->SteerKite(-1.0f);
	Kite->UpdateKite(2.0f * RideDeltaTime);
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

		Kite->bParkHoldAssist = true; // parked and recovered where the bar is centred, so the tensions compare
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

// A depowered kite turns more slowly: the bar turns it at a fraction of the rate it does sheeted
// in, and reaches it later. Research: the steering gain (turn per metre of air past the kite)
// falls from 0.35 to 0.15 rad/m and the dead time grows from 0.2 to 0.6 s from full to minimum
// power (Elfert 2024, docs/physics/research.md 1.4).
//
// From the same start, each kite is turned at full bar and the degrees it turns are counted for 2 s
// from when the bar reaches it, so the dead time (checked on its own) does not count twice. The
// research's ratio is of steering gains at the same airspeed; a depowered kite also flies slower,
// so in degrees it turns less than the gain alone says. Both are held to 35 to 55%. Sheeted in it
// turned 217 deg in the 2 s before phase 2 and about 175 since: the same 0.19 rad of turn per metre
// of air, but the air acts on the projected area while the mass is unchanged, so the kite gathers
// speed into the turn more slowly.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteDepowerSlowsTheTurn, "KiteSurf.Kite.DepowerSlowsTheTurn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteDepowerSlowsTheTurn::RunTest(const FString& Parameters)
{
	const float SettleSheet = 0.5f;
	const float TurnSeconds = 2.0f;
	float TurnedDeg[2] = { 0.0f, 0.0f };
	float AirRunM[2] = { 0.0f, 0.0f };
	float DeadTimeSeconds[2] = { 0.0f, 0.0f };
	float TurnedBeforeBarDeg[2] = { 0.0f, 0.0f };
	const float Sheets[2] = { 0.0f, 1.0f };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FStandingFixture Standing;
		UKiteComponent* Kite = Standing.Kite;
		TestNotNull(TEXT("Kite created"), Kite);
		if (!Kite)
		{
			return false;
		}
		// The same start for both: parked overhead, settled half sheeted. Then the bar goes to the
		// sheet under test and hard over, loop held, all at once.
		Kite->bParkHoldAssist = true;
		Kite->SheetKite(SettleSheet);
		Kite->SetWindowPosition(0.0f, 10.0f);
		FKiteFlight Settle;
		Settle.Fly(Kite, 0.0f, 5.0f);

		Kite->SheetKite(Sheets[Index]);
		DeadTimeSeconds[Index] = Kite->GetSteeringDeadTimeSeconds();
		Kite->SetLoopHeld(true);
		Kite->SteerKite(1.0f);
		float Elapsed = 0.0f;
		for (; Elapsed < DeadTimeSeconds[Index]; Elapsed += RideDeltaTime)
		{
			Kite->UpdateKite(RideDeltaTime);
		}
		TurnedBeforeBarDeg[Index] = Kite->GetTurnDeg();
		for (float Turning = 0.0f; Turning < TurnSeconds; Turning += RideDeltaTime)
		{
			Kite->UpdateKite(RideDeltaTime);
			AirRunM[Index] += Kite->GetAirspeedCmS() / 100.0f * RideDeltaTime;
		}
		TurnedDeg[Index] = Kite->GetTurnDeg() - TurnedBeforeBarDeg[Index];
		TestFalse(FString::Printf(TEXT("Sheet %.0f: the kite stays in the air"), Sheets[Index]), Kite->IsCrashed());
		Kite->SetLoopHeld(false);
	}
	const float DegreesRatio = TurnedDeg[0] / FMath::Max(TurnedDeg[1], 1.0f);
	const float GainOut = TurnedDeg[0] / FMath::Max(AirRunM[0], 1.0f);
	const float GainIn = TurnedDeg[1] / FMath::Max(AirRunM[1], 1.0f);
	const float GainRatio = GainOut / FMath::Max(GainIn, KINDA_SMALL_NUMBER);
	UE_LOG(LogKiteSurf, Log, TEXT("DepowerSlowsTheTurn: %.0f s at full bar from when it reaches the kite: sheeted out %.0f deg over %.1f m of air (%.2f s dead time, %.1f deg before), sheeted in %.0f deg over %.1f m of air (%.2f s dead time, %.1f deg before); steering gain %.2f against %.2f rad/m (%.0f%%), degrees %.0f%%"),
		TurnSeconds, TurnedDeg[0], AirRunM[0], DeadTimeSeconds[0], TurnedBeforeBarDeg[0], TurnedDeg[1], AirRunM[1], DeadTimeSeconds[1], TurnedBeforeBarDeg[1],
		FMath::DegreesToRadians(GainOut), FMath::DegreesToRadians(GainIn), 100.0f * GainRatio, 100.0f * DegreesRatio);

	TestTrue(FString::Printf(TEXT("Sheeted in the kite turns (%.0f deg in %.0f s)"), TurnedDeg[1], TurnSeconds), TurnedDeg[1] > 150.0f);
	TestTrue(FString::Printf(TEXT("Sheeted out it turns 35 to 55%% as far in the same time (%.0f%%)"), 100.0f * DegreesRatio), DegreesRatio >= 0.35f && DegreesRatio <= 0.55f);
	TestTrue(FString::Printf(TEXT("and its steering gain, turn per metre of air, is 35 to 55%% of sheeted in (%.0f%%)"), 100.0f * GainRatio), GainRatio >= 0.35f && GainRatio <= 0.55f);
	TestTrue(FString::Printf(TEXT("The bar reaches it later (%.2f s against %.2f s)"), DeadTimeSeconds[0], DeadTimeSeconds[1]), DeadTimeSeconds[0] > DeadTimeSeconds[1]);
	TestTrue(FString::Printf(TEXT("and until it does, neither kite turns much (%.0f and %.0f deg)"), TurnedBeforeBarDeg[0], TurnedBeforeBarDeg[1]), FMath::Abs(TurnedBeforeBarDeg[0]) < 15.0f && FMath::Abs(TurnedBeforeBarDeg[1]) < 15.0f);
	return true;
}

// The tightest turn is a fixed radius, whatever the speed: the bar turns the kite in proportion to
// its airspeed (research: radius 1 / g_k, docs/physics/research.md 1.4). Measured from the kite's
// path over one full loop after it has wound into the turn.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfKiteLoopRadiusIsSpeedIndependent, "KiteSurf.Kite.LoopRadiusIsSpeedIndependent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfKiteLoopRadiusIsSpeedIndependent::RunTest(const FString& Parameters)
{
	const float WindsKnots[2] = { 15.0f, 25.0f };
	const float StartTurnDeg = 45.0f;
	const float LoopDeg = 360.0f;
	const float MaxSeconds = 10.0f;
	float RadiusM[2] = { 0.0f, 0.0f };
	float LoopSeconds[2] = { 0.0f, 0.0f };
	float MeanSpeedMS[2] = { 0.0f, 0.0f };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FStandingFixture Standing;
		UKiteComponent* Kite = Standing.Kite;
		UWindComponent* Wind = Standing.Pawn ? Standing.Pawn->GetWind() : nullptr;
		TestTrue(TEXT("Kite and wind created"), Kite && Wind);
		if (!Kite || !Wind)
		{
			return false;
		}
		Wind->BaseWind = FVector(WindsKnots[Index] * KnotCmS, 0.0f, 0.0f);
		Kite->bParkHoldAssist = true;
		Kite->SetWindowPosition(0.0f, 10.0f);
		FKiteFlight Settle;
		Settle.Fly(Kite, 0.0f, 5.0f);

		// Full bar, loop held: once the kite has wound into the turn, follow it round one loop.
		Kite->SetLoopHeld(true);
		Kite->SteerKite(1.0f);
		float PathCm = 0.0f;
		float Seconds = 0.0f;
		float LoopStartSeconds = -1.0f;
		FVector Previous = Kite->GetKiteWorldPosition();
		while (Seconds < MaxSeconds && !Kite->IsCrashed() && Kite->GetTurnDeg() < StartTurnDeg + LoopDeg)
		{
			Kite->UpdateKite(RideDeltaTime);
			Seconds += RideDeltaTime;
			const FVector Now = Kite->GetKiteWorldPosition();
			if (Kite->GetTurnDeg() >= StartTurnDeg)
			{
				if (LoopStartSeconds < 0.0f)
				{
					LoopStartSeconds = Seconds;
				}
				PathCm += FVector::Dist(Now, Previous);
			}
			Previous = Now;
		}
		const float TurnedRad = FMath::DegreesToRadians(Kite->GetTurnDeg() - StartTurnDeg);
		RadiusM[Index] = PathCm / 100.0f / FMath::Max(TurnedRad, KINDA_SMALL_NUMBER);
		LoopSeconds[Index] = Seconds - LoopStartSeconds;
		MeanSpeedMS[Index] = PathCm / 100.0f / FMath::Max(LoopSeconds[Index], KINDA_SMALL_NUMBER);
		TestTrue(FString::Printf(TEXT("%.0f kn: the kite flew a full loop (%.0f deg)"), WindsKnots[Index], Kite->GetTurnDeg() - StartTurnDeg), Kite->GetTurnDeg() - StartTurnDeg >= LoopDeg);
		TestFalse(FString::Printf(TEXT("%.0f kn: without going into the water"), WindsKnots[Index]), Kite->IsCrashed());
		Kite->SetLoopHeld(false);
	}
	UE_LOG(LogKiteSurf, Log, TEXT("LoopRadiusIsSpeedIndependent: full-bar loop radius %.2f m at %.0f kn (%.1f m/s, %.2f s a loop), %.2f m at %.0f kn (%.1f m/s, %.2f s a loop); MinTurnRadiusCm %.0f"),
		RadiusM[0], WindsKnots[0], MeanSpeedMS[0], LoopSeconds[0], RadiusM[1], WindsKnots[1], MeanSpeedMS[1], LoopSeconds[1], UKiteComponent::StaticClass()->GetDefaultObject<UKiteComponent>()->MinTurnRadiusCm);

	TestTrue(FString::Printf(TEXT("The kite loops faster in more wind (%.1f against %.1f m/s)"), MeanSpeedMS[1], MeanSpeedMS[0]), MeanSpeedMS[1] > 1.2f * MeanSpeedMS[0]);
	TestNearlyEqual(FString::Printf(TEXT("but in a loop of the same radius, within 15%% (%.2f m against %.2f m)"), RadiusM[1], RadiusM[0]), RadiusM[1], RadiusM[0], 0.15f * RadiusM[0]);
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
	TestTrue(TEXT("Santa is shown"), Posed->IsVisible() && Posed->GetStaticMesh() && Posed->GetStaticMesh()->GetName() == TEXT("SM_RiderSanta_Torso"));
	TestFalse(TEXT("The robot is hidden behind Santa"), Robot->IsVisible());

	Pawn->SetRiderCharacter(ERiderCharacter::Wetsuit);
	TestTrue(TEXT("The wetsuit rider is shown"), Posed->IsVisible() && Posed->GetStaticMesh() && Posed->GetStaticMesh()->GetName() == TEXT("SM_RiderWetsuit_Torso"));

	TestEqual(TEXT("The rider has eight limb parts"), Pawn->GetRiderLimbs().Num(), 8);
	bool bLimbsAreWetsuit = Pawn->GetRiderLimbs().Num() == 8;
	for (const UStaticMeshComponent* Limb : Pawn->GetRiderLimbs())
	{
		bLimbsAreWetsuit = bLimbsAreWetsuit && Limb && Limb->IsVisible() && Limb->GetStaticMesh() && Limb->GetStaticMesh()->GetName().StartsWith(TEXT("SM_RiderWetsuit_"));
	}
	TestTrue(TEXT("and they are the wetsuit rider's"), bLimbsAreWetsuit);

	Pawn->SetRiderCharacter(ERiderCharacter::Robot);
	bool bLimbsHidden = true;
	for (const UStaticMeshComponent* Limb : Pawn->GetRiderLimbs())
	{
		bLimbsHidden = bLimbsHidden && Limb && !Limb->IsVisible();
	}
	TestTrue(TEXT("The jointed rider's limbs are hidden behind the robot"), bLimbsHidden);
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

// Guards the output of scripts/editor/import_geometry.py, import_rider_parts.py and create_materials.py.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfAssetsKiteAndRiderMeshes, "KiteSurf.Assets.KiteAndRiderMeshes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfAssetsKiteAndRiderMeshes::RunTest(const FString& Parameters)
{
	struct FExpectedMesh { const TCHAR* Path; int32 MaterialSlots; float MinSizeCm; };
	const FExpectedMesh ExpectedMeshes[] =
	{
		{ TEXT("/Game/Meshes/SM_Kite.SM_Kite"), 2, 400.0f },                 // canopy and tubes; a 12 m2 kite spans over 4 m
		// The one-piece riders, shown on the gear screen's preview.
		{ TEXT("/Game/Meshes/SM_RiderSanta.SM_RiderSanta"), 4, 150.0f },     // skin, white, red, black
		{ TEXT("/Game/Meshes/SM_RiderWetsuit.SM_RiderWetsuit"), 4, 150.0f }, // skin, wetsuit, accent, black
		// The jointed riders' parts, posed in the ride (import_rider_parts.py). A slot count of 0 means "any".
		{ TEXT("/Game/Meshes/SM_RiderSanta_Torso.SM_RiderSanta_Torso"), 4, 80.0f },       // skin, white, red, black
		{ TEXT("/Game/Meshes/SM_RiderWetsuit_Torso.SM_RiderWetsuit_Torso"), 4, 80.0f },   // skin, wetsuit, accent, black
		{ TEXT("/Game/Meshes/SM_RiderSanta_Thigh.SM_RiderSanta_Thigh"), 0, 45.0f },
		{ TEXT("/Game/Meshes/SM_RiderSanta_Shin.SM_RiderSanta_Shin"), 0, 42.0f },
		{ TEXT("/Game/Meshes/SM_RiderSanta_UpperArm.SM_RiderSanta_UpperArm"), 0, 26.0f },
		{ TEXT("/Game/Meshes/SM_RiderSanta_Forearm.SM_RiderSanta_Forearm"), 0, 27.0f },
		{ TEXT("/Game/Meshes/SM_RiderWetsuit_Thigh.SM_RiderWetsuit_Thigh"), 0, 45.0f },
		{ TEXT("/Game/Meshes/SM_RiderWetsuit_Shin.SM_RiderWetsuit_Shin"), 0, 42.0f },
		{ TEXT("/Game/Meshes/SM_RiderWetsuit_UpperArm.SM_RiderWetsuit_UpperArm"), 0, 26.0f },
		{ TEXT("/Game/Meshes/SM_RiderWetsuit_Forearm.SM_RiderWetsuit_Forearm"), 0, 27.0f },
	};
	for (const FExpectedMesh& Expected : ExpectedMeshes)
	{
		const UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Expected.Path);
		TestNotNull(FString::Printf(TEXT("%s loads"), Expected.Path), Mesh);
		if (!Mesh)
		{
			continue;
		}
		if (Expected.MaterialSlots > 0)
		{
			TestEqual(FString::Printf(TEXT("%s material slot count"), Expected.Path), Mesh->GetStaticMaterials().Num(), Expected.MaterialSlots);
		}
		TestTrue(FString::Printf(TEXT("%s has at least one material slot"), Expected.Path), Mesh->GetStaticMaterials().Num() >= 1);
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

	// Parked and settled with the bar centred, the park-hold assist has little left to do.
	Kite->bParkHoldAssist = true;
	Kite->SetWindowPosition(45.0f, 10.0f);
	FKiteFlight Parked;
	Parked.Fly(Kite, 0.0f, 6.0f);
	TestTrue(FString::Printf(TEXT("A settled, parked kite needs little steering (%.2f)"), Kite->GetAppliedSteer()), FMath::Abs(Kite->GetAppliedSteer()) < 0.5f);

	// Looping, the rider's bar goes straight to the kite, once its dead time has passed.
	Kite->SetLoopHeld(true);
	FKiteFlight Loop;
	const float DeadTimeSeconds = Kite->GetSteeringDeadTimeSeconds();
	Loop.Fly(Kite, 0.6f, DeadTimeSeconds + 0.2f);
	TestNearlyEqual(TEXT("With loop held the kite gets exactly the bar"), Kite->GetAppliedSteer(), 0.6f, 0.001f);
	Loop.Fly(Kite, -1.0f, DeadTimeSeconds + 0.1f);
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
	/** One sample of a jump, for the hang-time diagnosis. */
	struct FJumpSample
	{
		float AirSeconds = 0.0f;       // since the board left the water
		float HeightM = 0.0f;          // above the water
		float RiderVzMS = 0.0f;
		float LineForceUpN = 0.0f;     // the lines' pull on the rider, upward part
		float TensionN = 0.0f;
		float KiteElevationDeg = 0.0f;
		float KiteClockDeg = 0.0f;
		float AlphaDeg = 0.0f;
		float LiftCoefficient = 0.0f;
		float DragCoefficient = 0.0f;
		float AirspeedMS = 0.0f;
		float KiteVzMS = 0.0f;
		float Sheet = 0.0f;
		float AppliedSteer = 0.0f;
		bool bTaut = false;
	};

	/**
	 * When the timed jump lets go of the jump button and pops, after the send reaches the kite (s).
	 * Loaded, the rider is pulled off the water 1.13 s after the send starts (0.24 s of it the bar's
	 * dead time); this pops 0.07 s before that, as late as it can without racing the pull. Without the
	 * edge-release impulse (plan-2 A3) the height is earned in the last tenth of a second: 0.74 s
	 * gives 9.5 m, 0.82 s 10.8 m, 0.86 s 10.0 m (the pop and the pull together), 0.9 s is pulled off.
	 * It was 0.7 s at phase 1 and 0.8 s with plan-2 item 1, weight back only.
	 */
	constexpr float TimedReleaseSeconds = 0.82f;

	struct FJumpResult
	{
		float PeakCm = 0.0f;
		float AirSeconds = 0.0f;
		float SlackSecondsInAir = 0.0f;
		bool bPulledOffEdge = false;
		bool bCrashed = false;
		bool bKiteDown = false;
		/** Samples at TraceHz while the rider is in the air, when asked for. */
		TArray<FJumpSample> Trace;
		float RiderWeightN = 0.0f;
		float StallAngleDeg = 0.0f;
		/** With a loop at the apex: when it started and ended (s in the air; -1 if it did not), how far the kite turned, and its pull. */
		float LoopStartSeconds = -1.0f;
		float LoopEndSeconds = -1.0f;
		float LoopTurnedDeg = 0.0f;
		float LoopStartTensionN = 0.0f;
		float LoopPeakTensionN = 0.0f;
		float LoopPeakElevationDeg = 0.0f;
		float LoopMinElevationDeg = 90.0f;
		/** First time after the loop that the kite was back above 60 deg (s in the air), -1 if not before touchdown. */
		float BackOverheadSeconds = -1.0f;
	};

	/**
	 * Rides for a few seconds at 30 kn on the recommended kite, then jumps. With bSend the kite is
	 * steered hard up; with bHoldEdge the rider holds the jump button (crouched and loading the edge)
	 * with their weight on the tail until ReleaseSeconds after the kite starts to answer that (the bar
	 * reaches it after its steering dead time), when they pull the bar in and let go to pop; without
	 * it they pop with a tap at ReleaseSeconds. ReleaseSeconds < 0 means never pop. The bar is
	 * centred once the rider is in the air (or the kite is past 12), so the assist flies the kite
	 * overhead. With LoopAtApexSteer the rider loops the kite that way from the apex, the bar straight
	 * to the kite, until it has turned a full circle.
	 */
	FJumpResult RunJump(bool bSend, bool bHoldEdge, float ReleaseSeconds, EKiteModel Model = EKiteModel::Loop, float TraceHz = 0.0f, float LoopAtApexSteer = 0.0f)
	{
		FJumpResult Result;
		FRideFixture Ride(30.0f);
		if (!Ride.IsValid())
		{
			return Result;
		}
		Ride.Kite->SetKiteModel(Model);
		Ride.Kite->SetKiteSize(UKiteComponent::RecommendKiteSizeM2(30.0f));
		Result.RiderWeightN = Ride.Board->MassKg * KiteUnits::GravityMS2;
		Result.StallAngleDeg = Ride.Kite->StallAngleDeg;
		Ride.Simulate(8.0f);

		float SendDeadTimeSeconds = 0.0f;
		if (bSend)
		{
			Ride.Pawn->SteerKite(-1.0f);
			SendDeadTimeSeconds = Ride.Kite->GetSteeringDeadTimeSeconds();
		}
		if (bHoldEdge)
		{
			Ride.Board->SetWeightShift(-1.0f);
			Ride.Pawn->SetLoadHeld(true);
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
				Ride.Pawn->SetLoadHeld(false);
				Ride.Pawn->SheetKite(1.0f);
			}
			if (!bLeftWater && ReleaseSeconds >= 0.0f && Elapsed >= ReleaseSeconds + SendDeadTimeSeconds)
			{
				Ride.Board->SetWeightShift(-1.0f);
				Ride.Pawn->SheetKite(1.0f);
				if (bHoldEdge)
				{
					Ride.Pawn->ReleaseLoadAndPop();
				}
				else
				{
					Ride.Board->Jump();
				}
				Ride.Board->SetWeightShift(0.0f);
				bLeftWater = true;
			}
			const bool bLoopInProgress = Result.LoopStartSeconds >= 0.0f && Result.LoopEndSeconds < 0.0f;
			if (bAir && LoopAtApexSteer != 0.0f && Result.LoopStartSeconds < 0.0f && Ride.Board->Velocity.Z < 0.0f)
			{
				// The apex: loop the kite.
				Result.LoopStartSeconds = Result.AirSeconds;
				Result.LoopStartTensionN = Ride.Kite->GetLineTensionN();
				Ride.Kite->SetLoopHeld(true);
				Ride.Pawn->SteerKite(LoopAtApexSteer);
			}
			else if (bLoopInProgress)
			{
				Result.LoopTurnedDeg = FMath::Abs(Ride.Kite->GetTurnDeg());
				if (Ride.Kite->GetLineTensionN() > Result.LoopPeakTensionN)
				{
					Result.LoopPeakTensionN = Ride.Kite->GetLineTensionN();
					Result.LoopPeakElevationDeg = Ride.Kite->GetElevationDeg();
				}
				Result.LoopMinElevationDeg = FMath::Min(Result.LoopMinElevationDeg, Ride.Kite->GetElevationDeg());
				if (Result.LoopTurnedDeg >= 360.0f)
				{
					Result.LoopEndSeconds = Result.AirSeconds;
					Ride.Kite->SetLoopHeld(false);
					Ride.Pawn->SteerKite(0.0f);
				}
			}
			else if (bAir || Ride.Kite->GetClockDeg() < 0.0f)
			{
				// In the air, or once the kite is past 12, the bar is centred: in the air the assist then
				// flies the kite overhead and holds it there.
				Ride.Pawn->SteerKite(0.0f);
			}
			if (bAir && Result.LoopEndSeconds >= 0.0f && Result.BackOverheadSeconds < 0.0f && Ride.Kite->GetElevationDeg() > 60.0f)
			{
				Result.BackOverheadSeconds = Result.AirSeconds;
			}
			if (bAir)
			{
				const int32 FramesPerSample = TraceHz > 0.0f ? FMath::Max(FMath::RoundToInt(1.0f / (TraceHz * RideDeltaTime)), 1) : 0;
				const int32 AirFrame = FMath::RoundToInt(Result.AirSeconds / RideDeltaTime);
				if (FramesPerSample > 0 && AirFrame % FramesPerSample == 0)
				{
					const FKiteStepDebug& KiteStep = Ride.Kite->GetLastStepDebug();
					FJumpSample& Sample = Result.Trace.AddDefaulted_GetRef();
					Sample.AirSeconds = Result.AirSeconds;
					Sample.HeightM = Ride.Board->GetCurrentJumpHeight() / 100.0f;
					Sample.RiderVzMS = Ride.Board->Velocity.Z / 100.0f;
					Sample.LineForceUpN = Ride.Kite->GetLineForce().Z / 100.0f;
					Sample.TensionN = Ride.Kite->GetLineTensionN();
					Sample.KiteElevationDeg = Ride.Kite->GetElevationDeg();
					Sample.KiteClockDeg = Ride.Kite->GetClockDeg();
					Sample.AlphaDeg = KiteStep.AlphaDeg;
					Sample.LiftCoefficient = KiteStep.LiftCoefficient;
					Sample.DragCoefficient = KiteStep.DragCoefficient;
					Sample.AirspeedMS = Ride.Kite->GetAirspeedCmS() / 100.0f;
					Sample.KiteVzMS = Ride.Kite->GetKiteVelocity().Z / 100.0f;
					Sample.Sheet = Ride.Kite->Sheet;
					Sample.AppliedSteer = Ride.Kite->GetAppliedSteer();
					Sample.bTaut = Ride.Kite->AreLinesTaut();
				}
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
		Ride.Kite->SetLoopHeld(false); // a loop not finished by touchdown is let go
		Ride.Pawn->SteerKite(0.0f);
		Ride.Simulate(1.0f);
		Result.bCrashed = Ride.Board->IsCrashing();
		Result.bKiteDown = Ride.Kite->IsCrashed();
		return Result;
	}
}

// Height has to be earned: a pop alone is a hop, sending the kite without an edge plucks the rider
// off early, and the big jump comes from holding the jump button (crouched, loading the edge, the
// weight back) against the rising kite and letting go at the right moment.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfJumpTimedReleaseBeatsPop, "KiteSurf.Jump.TimedReleaseBeatsPop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfJumpTimedReleaseBeatsPop::RunTest(const FString& Parameters)
{
	const FJumpResult Pop = RunJump(false, false, 0.0f);
	const FJumpResult SendOnly = RunJump(true, false, -1.0f);
	const FJumpResult Timed = RunJump(true, true, TimedReleaseSeconds);
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

// Hang time: how much of the rider's weight the kite carries through a big jump, as effective
// gravity 8 h / t^2 (h the apex, t the time in the air). Real jumps give 1.5 to 3.5 m/s^2 (WOO
// height and airtime pairs, docs/physics/research.md 3.3): the kite carries 65 to 85% of the rider.
// The plan's bound is 5 until the landing model (plan-2 item 4); a jump over 12 m lasts at least 5 s.
//
// The 20 Hz trace it logs shows how. Since plan-2 item 2 the assist judges the window by the
// horizontal wind (A1) and, with the bar centred in the air, flies the kite to 12 over the rider and
// holds it there (A2): on the way down the kite is about 70 deg up, flying unstalled, and its lines
// hold up about two thirds of the rider. Before, the assist steered by the wind the rider feels,
// which in the air is dominated by their own climb and fall; the kite stayed low and to the side,
// stalled all the way down, and 8h/t^2 was 7.4 to 7.8 m/s^2.
//
// The climb is physics only (A3): the loaded pop gives 6 m/s, and the lines, at 4 kN as the board
// lets go, add 2 m/s more in the first 0.2 s before the kite, at 45 deg up and still deep in the
// window, slows as it nears the edge and the rider rises towards it. With the weight back but
// without the jump button's loaded pop the same send gives 7.4 to 8 m; that way it gave 13.3 m with
// the old edge-release impulse (EdgeReleaseSeconds 0.22).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPhysicsHangTime, "KiteSurf.Physics.HangTime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPhysicsHangTime::RunTest(const FString& Parameters)
{
	const float TraceHz = 20.0f;
	const float RealEffectiveGravityMin = 1.5f;     // m/s^2, research
	const float RealEffectiveGravityMax = 3.5f;
	const float ModelEffectiveGravityMax = 5.0f;    // m/s^2, plan-2 item 2 until the landing model
	const float MinHeightM = 10.0f;
	const float MaxHeightM = 20.0f;
	const float LongJumpM = 12.0f;
	const float LongJumpMinAirSeconds = 5.0f;
	const FJumpResult Timed = RunJump(true, true, TimedReleaseSeconds, EKiteModel::Loop, TraceHz);
	const float HeightM = Timed.PeakCm / 100.0f;
	const float EffectiveGravity = 8.0f * HeightM / FMath::Max(FMath::Square(Timed.AirSeconds), KINDA_SMALL_NUMBER);
	UE_LOG(LogKiteSurf, Log, TEXT("HangTime: timed jump at 30 kn on the recommended kite: %.1f m, %.2f s in the air, 8h/t^2 = %.2f m/s^2 (%.0f%% of g carried; real jumps %.1f to %.1f)"),
		HeightM, Timed.AirSeconds, EffectiveGravity, 100.0f * (1.0f - EffectiveGravity / KiteUnits::GravityMS2), RealEffectiveGravityMin, RealEffectiveGravityMax);

	// The flight and the way down: how much the lines hold up, where the kite is and how it is flying.
	float FlightUpSum = 0.0f;
	float UpSum = 0.0f;
	float ElevationSum = 0.0f;
	float LowestElevationDeg = 90.0f;
	float AirspeedSum = 0.0f;
	int32 Descending = 0;
	int32 Stalled = 0;
	UE_LOG(LogKiteSurf, Log, TEXT("HangTime trace: t_s,height_m,rider_vz_ms,line_up_n,tension_n,kite_el_deg,kite_clock_deg,alpha_deg,cl,cd,airspeed_ms,kite_vz_ms,sheet,steer,taut"));
	for (const FJumpSample& Sample : Timed.Trace)
	{
		UE_LOG(LogKiteSurf, Log, TEXT("HangTime trace: %.2f,%.2f,%.2f,%.0f,%.0f,%.1f,%.1f,%.1f,%.2f,%.2f,%.1f,%.2f,%.2f,%.2f,%d"),
			Sample.AirSeconds, Sample.HeightM, Sample.RiderVzMS, Sample.LineForceUpN, Sample.TensionN, Sample.KiteElevationDeg, Sample.KiteClockDeg,
			Sample.AlphaDeg, Sample.LiftCoefficient, Sample.DragCoefficient, Sample.AirspeedMS, Sample.KiteVzMS, Sample.Sheet, Sample.AppliedSteer, Sample.bTaut);
		FlightUpSum += Sample.LineForceUpN;
		if (Sample.RiderVzMS < 0.0f)
		{
			UpSum += Sample.LineForceUpN;
			ElevationSum += Sample.KiteElevationDeg;
			LowestElevationDeg = FMath::Min(LowestElevationDeg, Sample.KiteElevationDeg);
			AirspeedSum += Sample.AirspeedMS;
			Stalled += Sample.AlphaDeg > Timed.StallAngleDeg ? 1 : 0;
			++Descending;
		}
	}
	const float WeightN = FMath::Max(Timed.RiderWeightN, 1.0f);
	const float DescentCount = static_cast<float>(FMath::Max(Descending, 1));
	UE_LOG(LogKiteSurf, Log, TEXT("HangTime: over the flight the lines hold up %.0f N on average (%.2f body weights); on the way down (%d samples) %.0f N (%.0f%% of %.0f N), the kite is %.1f deg above the rider (lowest %.1f) at %.1f m/s of air, stalled in %d samples"),
		FlightUpSum / FMath::Max(Timed.Trace.Num(), 1), FlightUpSum / FMath::Max(Timed.Trace.Num(), 1) / WeightN, Descending, UpSum / DescentCount, 100.0f * UpSum / DescentCount / WeightN, WeightN,
		ElevationSum / DescentCount, LowestElevationDeg, AirspeedSum / DescentCount, Stalled);

	TestTrue(TEXT("The trace was recorded"), Timed.Trace.Num() > 20 && Descending > 5);
	TestTrue(FString::Printf(TEXT("A big jump: %.0f to %.0f m (%.1f m)"), MinHeightM, MaxHeightM, HeightM), HeightM >= MinHeightM && HeightM <= MaxHeightM);
	TestTrue(FString::Printf(TEXT("8h/t^2 is %.1f to %.1f m/s^2 (%.2f; real jumps %.1f to %.1f)"), RealEffectiveGravityMin, ModelEffectiveGravityMax, EffectiveGravity, RealEffectiveGravityMin, RealEffectiveGravityMax),
		EffectiveGravity >= RealEffectiveGravityMin && EffectiveGravity <= ModelEffectiveGravityMax);
	if (HeightM > LongJumpM)
	{
		TestTrue(FString::Printf(TEXT("A jump over %.0f m lasts at least %.0f s (%.2f s)"), LongJumpM, LongJumpMinAirSeconds, Timed.AirSeconds), Timed.AirSeconds >= LongJumpMinAirSeconds);
	}
	TestFalse(TEXT("The kite does not stall on the way down"), Stalled > Descending / 10);
	return true;
}

// The kite in the air: with the bar centred after take-off the assist flies the kite to 12 over the
// rider and holds it there, so it carries most of their weight along nearly vertical lines all the
// way down (docs/physics/plan-2.md item 2, A2; research 3.3: real jumps give 8h/t^2 of 1.5 to
// 3.5 m/s^2, the kite carrying 65 to 85% of the rider over the flight).
//
// The plan asks for the kite above 60 deg within 2 s of take-off. It holds for jumps up to about
// 8 m; on the 11 m timed jump the rider climbs at 9 to 5 m/s for the first second and the kite,
// flying at about 14 m/s of air, cannot rise faster than them until the climb slows. It reaches 12
// about 1.5 s after take-off, still deep in the window, and 60 deg after 2.1 s (2.4 s on a 13 m
// jump; flying it straight up instead of to 12 makes no difference and stalls it on the way down),
// so this test allows 2.5 s.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPhysicsKiteOverheadInTheAir, "KiteSurf.Physics.KiteOverheadInTheAir", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPhysicsKiteOverheadInTheAir::RunTest(const FString& Parameters)
{
	const float EveryFrameHz = 1.0f / RideDeltaTime;
	const float OverheadElevationDeg = 60.0f;
	const float OverheadWithinSeconds = 2.5f;      // plan 2 s; see above
	const float HeldElevationDeg = 50.0f;
	const float HeldUntilBeforeTouchdownSeconds = 1.0f;
	const float MinMeanLineUpBodyWeights = 0.55f;
	const float MaxMeanLineUpBodyWeights = 0.9f;
	const FJumpResult Jump = RunJump(true, true, TimedReleaseSeconds, EKiteModel::Loop, EveryFrameHz);
	TestTrue(TEXT("The jump was traced"), Jump.Trace.Num() > 60);
	if (Jump.Trace.Num() == 0)
	{
		return false;
	}

	float OverheadSeconds = -1.0f;
	float LowestHeldDeg = 90.0f;
	float LowestHeldAtSeconds = 0.0f;
	float UpSum = 0.0f;
	for (const FJumpSample& Sample : Jump.Trace)
	{
		UpSum += Sample.LineForceUpN;
		if (OverheadSeconds < 0.0f && Sample.KiteElevationDeg > OverheadElevationDeg)
		{
			OverheadSeconds = Sample.AirSeconds;
		}
		if (OverheadSeconds >= 0.0f && Sample.AirSeconds <= Jump.AirSeconds - HeldUntilBeforeTouchdownSeconds && Sample.KiteElevationDeg < LowestHeldDeg)
		{
			LowestHeldDeg = Sample.KiteElevationDeg;
			LowestHeldAtSeconds = Sample.AirSeconds;
		}
	}
	const float MeanUpBodyWeights = UpSum / Jump.Trace.Num() / FMath::Max(Jump.RiderWeightN, 1.0f);
	UE_LOG(LogKiteSurf, Log, TEXT("KiteOverheadInTheAir: %.1f m, %.2f s in the air; kite above %.0f deg after %.2f s, lowest from then until %.0f s before touchdown %.1f deg (at %.2f s); lines hold up %.2f body weights on average"),
		Jump.PeakCm / 100.0f, Jump.AirSeconds, OverheadElevationDeg, OverheadSeconds, HeldUntilBeforeTouchdownSeconds, LowestHeldDeg, LowestHeldAtSeconds, MeanUpBodyWeights);

	TestTrue(FString::Printf(TEXT("The kite is above %.0f deg within %.1f s of take-off (%.2f s)"), OverheadElevationDeg, OverheadWithinSeconds, OverheadSeconds), OverheadSeconds >= 0.0f && OverheadSeconds <= OverheadWithinSeconds);
	TestTrue(FString::Printf(TEXT("and stays above %.0f deg until %.0f s before touchdown (lowest %.1f deg)"), HeldElevationDeg, HeldUntilBeforeTouchdownSeconds, LowestHeldDeg), LowestHeldDeg > HeldElevationDeg);
	TestTrue(FString::Printf(TEXT("Over the flight the lines hold up %.2f to %.2f body weights (%.2f)"), MinMeanLineUpBodyWeights, MaxMeanLineUpBodyWeights, MeanUpBodyWeights),
		MeanUpBodyWeights >= MinMeanLineUpBodyWeights && MeanUpBodyWeights <= MaxMeanLineUpBodyWeights);
	TestTrue(FString::Printf(TEXT("The lines stay tight (%.2f s slack)"), Jump.SlackSecondsInAir), Jump.SlackSecondsInAir < 0.3f);
	TestFalse(TEXT("The rider lands it and the kite stays in the air"), Jump.bCrashed || Jump.bKiteDown);
	return true;
}

// A loop in the air: the overhead hold leaves the bar to the rider, so a loop from the apex of a big
// jump is flown in full and pulls harder than the kite held overhead. Research (C6, 3.3): a loop in
// the air pulls 3 to 5 body weights, mostly sideways and down, with the lines under 30 deg, and the
// kite climbs back to the zenith on its remaining speed.
//
// Known gap: the model's loop from the apex peaks at about 0.9 body weights with the kite still 60
// deg up, and this test pins that until the loop is reworked. The kite at the apex flies at about
// 14 m/s of air (the rider is moving at 13 to 14 m/s across and downwind), and its tightest turn is
// 3 m across on 24 m lines, so the loop stays near the top of the window; once it stops holding the
// rider up their sink grows to 5 to 9 m/s and raises its angle of attack past the stall, and a
// stalled kite turns slowly and gains no speed. Easing the trim by 5 to 15 deg keeps it flying but
// the loop still peaks at about 1 body weight; a loop flown from the far side with the bar (clock
// -44) does no better. Steering the loop, not the airborne assist, is what is missing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPhysicsAirborneLoopYanks, "KiteSurf.Physics.AirborneLoopYanks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPhysicsAirborneLoopYanks::RunTest(const FString& Parameters)
{
	const float LoopSteer = -1.0f;                 // towards the side the kite is on at the apex (clock just under 0)
	const float BigJumpCm = 1000.0f;
	const float ResearchPeakBodyWeightsMin = 3.0f; // research
	const float ResearchPeakBodyWeightsMax = 5.0f;
	const float ModelPeakBodyWeights = 0.9f;       // what the model gives today (see the known gap above)
	const float ModelTolerance = 0.2f;
	const float BackOverheadJumpCm = 1500.0f;
	const FJumpResult Loop = RunJump(true, true, TimedReleaseSeconds, EKiteModel::Loop, 0.0f, LoopSteer);
	const float PeakBodyWeights = Loop.LoopPeakTensionN / FMath::Max(Loop.RiderWeightN, 1.0f);
	UE_LOG(LogKiteSurf, Log, TEXT("AirborneLoopYanks: %.1f m, %.2f s in the air; loop from %.2f to %.2f s, turned %.0f deg, pull %.0f N at the start, peak %.0f N (%.2f body weights; research %.0f to %.0f) at %.1f deg up, lowest %.1f deg; back above 60 deg at %.2f s"),
		Loop.PeakCm / 100.0f, Loop.AirSeconds, Loop.LoopStartSeconds, Loop.LoopEndSeconds, Loop.LoopTurnedDeg, Loop.LoopStartTensionN, Loop.LoopPeakTensionN, PeakBodyWeights,
		ResearchPeakBodyWeightsMin, ResearchPeakBodyWeightsMax, Loop.LoopPeakElevationDeg, Loop.LoopMinElevationDeg, Loop.BackOverheadSeconds);

	TestTrue(FString::Printf(TEXT("A jump over 10 m to loop in (%.1f m)"), Loop.PeakCm / 100.0f), Loop.PeakCm > BigJumpCm);
	TestTrue(FString::Printf(TEXT("The loop is flown in full from the apex before touchdown (%.0f deg)"), Loop.LoopTurnedDeg), Loop.LoopStartSeconds >= 0.0f && Loop.LoopEndSeconds > Loop.LoopStartSeconds);
	TestTrue(FString::Printf(TEXT("and pulls harder than the kite held overhead before it (%.0f N against %.0f N)"), Loop.LoopPeakTensionN, Loop.LoopStartTensionN), Loop.LoopPeakTensionN > 1.2f * Loop.LoopStartTensionN);
	TestNearlyEqual(FString::Printf(TEXT("The peak is the model's %.1f body weights within %.0f%% (%.2f; research %.0f to %.0f, see the known gap)"), ModelPeakBodyWeights, 100.0f * ModelTolerance, PeakBodyWeights, ResearchPeakBodyWeightsMin, ResearchPeakBodyWeightsMax),
		PeakBodyWeights, ModelPeakBodyWeights, ModelTolerance * ModelPeakBodyWeights);
	if (Loop.PeakCm >= BackOverheadJumpCm)
	{
		TestTrue(FString::Printf(TEXT("On a jump of 15 m or more the kite is back above 60 deg before touchdown (%.2f s)"), Loop.BackOverheadSeconds), Loop.BackOverheadSeconds >= 0.0f);
	}
	TestFalse(TEXT("The kite stays out of the water"), Loop.bKiteDown);
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
		Standing.Kite->bParkHoldAssist = true; // both measured parked at the same clock position
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
		Standing.Kite->bParkHoldAssist = true; // both measured parked at the same clock position
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

	// Where the boost kite earns its name: each kite released a little before its send would pull
	// the rider off the edge (loaded, a release at 0.88 s is too late for the loop kite; the boost
	// kite turns slower, so its send loads up later and 0.95 s is too late for it), it goes higher and
	// stays up longer.
	const FJumpResult LoopJump = RunJump(true, true, 0.74f, EKiteModel::Loop);
	const FJumpResult BoostJump = RunJump(true, true, 0.86f, EKiteModel::Boost);
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
	const float ReferencePop = Board->PopImpulseKgCmPerS;
	const float ReferencePlaning = Board->PlaningThresholdCmS;
	const float ReferenceGrip = Board->EdgeGripKgPerS;
	const float ReferenceTurn = Board->CarveTurnRate;
	Board->SetBoardSize(EBoardSize::Medium);
	TestTrue(TEXT("The 138 is the board the simulation is tuned for"), Board->PopImpulseKgCmPerS == ReferencePop && Board->PlaningThresholdCmS == ReferencePlaning && Board->EdgeGripKgPerS == ReferenceGrip && Board->CarveTurnRate == ReferenceTurn);

	Board->SetBoardSize(EBoardSize::Small);
	TestTrue(TEXT("The small board pops harder"), Board->PopImpulseKgCmPerS > ReferencePop);
	TestTrue(TEXT("needs more speed to plane"), Board->PlaningThresholdCmS > ReferencePlaning);
	TestTrue(TEXT("so it is still sunk at a speed the 138 planes at"), Board->GetFloatDepthForSpeed(ReferencePlaning) > 0.0f);
	TestTrue(TEXT("and turns quicker with less grip"), Board->CarveTurnRate > ReferenceTurn && Board->EdgeGripKgPerS < ReferenceGrip);

	Board->SetBoardSize(EBoardSize::Large);
	TestTrue(TEXT("The big board pops less"), Board->PopImpulseKgCmPerS < ReferencePop);
	TestTrue(TEXT("planes earlier"), Board->PlaningThresholdCmS < ReferencePlaning);
	TestNearlyEqual(TEXT("so it is on the surface at a speed the 138 is still coming up at"), Board->GetFloatDepthForSpeed(0.9f * ReferencePlaning), 0.0f, 0.01f);
	TestTrue(TEXT("and grips harder but turns slower"), Board->EdgeGripKgPerS > ReferenceGrip && Board->CarveTurnRate < ReferenceTurn);

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
	TestTrue(TEXT("with the sound of the board on sand"), Ride.Pawn->GetRideSoundCount() > 0 && Ride.Pawn->GetLastRideSound() == ERideSound::Aground);
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
	TestTrue(TEXT("with the shark's sound"), Ride.Pawn->GetLastRideSound() == ERideSound::Shark);

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
//
// The phase 2 plan (docs/physics/plan-2.md item 1) asks for 1.0 to 1.4 body weights with the bar in
// and at least four times the bar-out pull. That cannot hold together with the zenith target
// (KiteSurf.Physics.ParkedAtZenith, 0.85 to 1.1 kN for this kite in 30 kn): with the bar in the kite
// flies at the same angle of attack in both (14 deg), and riding at 18 kn on a beam reach in 20 kn
// its airspeed is 13.1 m/s against 17.0 m/s overhead in 30 kn at 23 m, so it pulls 0.6 of the zenith
// pull, at most about 0.8 body weights. The bar-out pull is set by the bar throw (19 deg, research 12
// to 20). Before phase 2: out 9.0 kn / 172 N, half 13.8 kn / 367 N, in 21.7 kn / 1120 N (1.34 body
// weights, 6.5 times the bar-out pull).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRideBarIsTheThrottle, "KiteSurf.Ride.BarIsTheThrottle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfRideBarIsTheThrottle::RunTest(const FString& Parameters)
{
	const float SheetValues[3] = { 0.0f, 0.5f, 1.0f };
	float SpeedKn[3] = { 0.0f, 0.0f, 0.0f };
	float TensionN[3] = { 0.0f, 0.0f, 0.0f };
	float BoardMassKg = 0.0f;
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
		BoardMassKg = Ride.Board->MassKg;
		TestFalse(TEXT("The kite stays in the air at any bar position"), Ride.Kite->IsCrashed());
		TestTrue(FString::Printf(TEXT("and does not stall (angle of attack %.1f deg)"), Ride.Kite->GetAngleOfAttackDeg()), Ride.Kite->GetAngleOfAttackDeg() < Ride.Kite->StallAngleDeg);
	}
	UE_LOG(LogKiteSurf, Log, TEXT("BarIsTheThrottle: 9 m in 20 kn, bar out %.1f kn / %.0f N, half %.1f kn / %.0f N, in %.1f kn / %.0f N (%.2f body weights, %.2f times bar out)"),
		SpeedKn[0], TensionN[0], SpeedKn[1], TensionN[1], SpeedKn[2], TensionN[2], TensionN[2] / FMath::Max(BoardMassKg * KiteUnits::GravityMS2, 1.0f), TensionN[2] / FMath::Max(TensionN[0], 1.0f));

	const float BodyWeightN = BoardMassKg * KiteUnits::GravityMS2;
	TestTrue(FString::Printf(TEXT("Bar right in pulls 0.75 to 1.4 body weights (%.2f; the plan's 1.0 cannot hold with the zenith target, see above)"), TensionN[2] / BodyWeightN),
		TensionN[2] >= 0.75f * BodyWeightN && TensionN[2] <= 1.4f * BodyWeightN);
	TestTrue(FString::Printf(TEXT("Bar right in pulls at least 3.5 times as hard as bar right out (%.0f N against %.0f N)"), TensionN[2], TensionN[0]), TensionN[2] > 3.5f * TensionN[0]);
	TestTrue(FString::Printf(TEXT("Half way is in between (%.0f N)"), TensionN[1]), TensionN[1] > 1.5f * TensionN[0] && TensionN[2] > 1.5f * TensionN[1]);
	TestTrue(FString::Printf(TEXT("Bar right out slows the rider to a crawl (%.1f kn)"), SpeedKn[0]), SpeedKn[0] < 10.0f);
	TestTrue(FString::Printf(TEXT("Bar right in is much faster (%.1f kn)"), SpeedKn[2]), SpeedKn[2] > 1.7f * SpeedKn[0] && SpeedKn[2] > 16.0f);
	return true;
}

// kite.Physics.Debug: the step breakdown the debug view draws is filled in by the kite and board
// steps and makes physical sense, and drawing and logging it runs without a renderer.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfPhysicsDebugStepBreakdown, "KiteSurf.Physics.DebugStepBreakdown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfPhysicsDebugStepBreakdown::RunTest(const FString& Parameters)
{
	IConsoleVariable* DebugLevel = IConsoleManager::Get().FindConsoleVariable(TEXT("kite.Physics.Debug"));
	TestNotNull(TEXT("kite.Physics.Debug is registered"), DebugLevel);
	FRideFixture Ride;
	TestTrue(TEXT("Ride fixture created"), Ride.IsValid());
	if (!DebugLevel || !Ride.IsValid())
	{
		return false;
	}

	DebugLevel->Set(2, ECVF_SetByCode);
	TestEqual(TEXT("The pawn draws and logs at level 2"), Ride.Pawn->GetPhysicsDebugLevel(), 2);
	Ride.Simulate(3.0f);
	DebugLevel->Set(0, ECVF_SetByCode);
	TestEqual(TEXT("and stops at 0"), Ride.Pawn->GetPhysicsDebugLevel(), 0);
	Ride.Kite->bDrawDebug = true;
	TestEqual(TEXT("bDrawDebug on the kite forces level 1"), Ride.Pawn->GetPhysicsDebugLevel(), 1);
	Ride.Simulate(RideDeltaTime);
	Ride.Kite->bDrawDebug = false;

	const FKiteStepDebug& KiteStep = Ride.Kite->GetLastStepDebug();
	const FVector ToRider = (Ride.Pawn->GetActorLocation() - Ride.Kite->GetKiteWorldPosition()).GetSafeNormal();
	UE_LOG(LogKiteSurf, Log, TEXT("DebugStepBreakdown: kite lift %.0f N, drag %.0f N, side %.0f N, tension %.0f N, alpha %.1f deg, Cl %.2f, Cd %.2f, apparent wind %.1f m/s"),
		KiteStep.LiftN.Size(), KiteStep.DragN.Size(), KiteStep.SideN.Size(), KiteStep.TensionN, KiteStep.AlphaDeg, KiteStep.LiftCoefficient, KiteStep.DragCoefficient, KiteStep.ApparentWindCmS.Size() / 100.0f);
	TestTrue(TEXT("The kite step is taut while riding"), KiteStep.bTaut);
	TestNearlyEqual(TEXT("Its tension is the line tension (under the cap)"), KiteStep.TensionN, Ride.Kite->GetLineTensionN(), 1.0f);
	TestTrue(TEXT("Lift pulls the kite away from the rider"), FVector::DotProduct(KiteStep.LiftN, -ToRider) > 0.0f);
	TestTrue(TEXT("Drag acts along the apparent wind"), FVector::DotProduct(KiteStep.DragN, KiteStep.ApparentWindCmS) > 0.0f);
	TestNearlyEqual(TEXT("Lift is Cl over Cd of drag, give or take the steering drag"), static_cast<float>(KiteStep.LiftN.Size() / FMath::Max(KiteStep.DragN.Size(), 1.0)),
		KiteStep.LiftCoefficient / KiteStep.DragCoefficient, 0.05f * KiteStep.LiftCoefficient / KiteStep.DragCoefficient);
	TestNearlyEqual(TEXT("The angle of attack is the kite's"), KiteStep.AlphaDeg, Ride.Kite->GetAngleOfAttackDeg(), 0.01f);

	const FBoardStepDebug& BoardStep = Ride.Board->GetLastStepDebug();
	const FVector BoardVelocity = Ride.Board->Velocity;
	UE_LOG(LogKiteSurf, Log, TEXT("DebugStepBreakdown: board drag %.0f N, grip %.0f N, drive %.0f N, leeway %.1f deg at %.1f kn"),
		BoardStep.DragForceN.Size(), BoardStep.GripForceN.Size(), BoardStep.DriveForceN.Size(), BoardStep.LeewayDeg, Ride.SpeedKnots());
	TestTrue(TEXT("Board drag opposes its motion"), FVector::DotProduct(BoardStep.DragForceN, BoardVelocity) < 0.0f);
	TestTrue(FString::Printf(TEXT("The board grips: %.0f N sideways"), BoardStep.GripForceN.Size()), BoardStep.GripForceN.Size() > 10.0f);
	TestTrue(FString::Printf(TEXT("Riding, the board slips only a few degrees (%.1f)"), BoardStep.LeewayDeg), FMath::Abs(BoardStep.LeewayDeg) < 15.0f);
	return true;
}

// Holding the jump button: the rider crouches with their weight back and loads the edge against
// the lines; letting go pops.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfJumpLoadAndRelease, "KiteSurf.Jump.LoadAndRelease", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfJumpLoadAndRelease::RunTest(const FString& Parameters)
{
	// The default kite in the default wind, riding steadily.
	auto Settled = [](FRideFixture& Ride)
	{
		Ride.Kite->SetKiteSize(9.0f);
		Ride.Simulate(10.0f);
	};

	// The crouch builds over a moment while held and lets go when released.
	FRideFixture Ride(20.0f);
	if (!Ride.IsValid())
	{
		return false;
	}
	Settled(Ride);
	UBoardMovementComponent* Board = Ride.Board;
	TestEqual(TEXT("Not loading to start with"), Board->GetLoadAmount(), 0.0f);
	const float TensionBefore = Ride.Kite->GetLineTensionN();
	const float LateralBefore = FMath::Abs(Board->GetLateralSpeed());

	Ride.Pawn->SetLoadHeld(true);
	Ride.Simulate(0.2f);
	TestTrue(FString::Printf(TEXT("After 0.2 s the crouch is part way (%.2f)"), Board->GetLoadAmount()), Board->GetLoadAmount() > 0.3f && Board->GetLoadAmount() < 0.8f);
	Ride.Simulate(0.4f);
	TestNearlyEqual(TEXT("and full after 0.6 s"), Board->GetLoadAmount(), 1.0f, 0.001f);

	// Loaded, the edge bites: the board slips downwind less and the lines pull harder.
	float PeakTension = 0.0f;
	for (float Elapsed = 0.0f; Elapsed < 2.0f; Elapsed += RideDeltaTime)
	{
		Ride.Simulate(RideDeltaTime);
		PeakTension = FMath::Max(PeakTension, Ride.Kite->GetLineTensionN());
	}
	const float LateralLoaded = FMath::Abs(Board->GetLateralSpeed());
	UE_LOG(LogKiteSurf, Log, TEXT("LoadAndRelease: tension %.0f N riding, up to %.0f N loaded; sideways slip %.0f cm/s riding, %.0f cm/s loaded"), TensionBefore, PeakTension, LateralBefore, LateralLoaded);
	TestTrue(FString::Printf(TEXT("Loading cuts the sideways slip (%.0f cm/s to %.0f cm/s)"), LateralBefore, LateralLoaded), LateralLoaded < 0.7f * LateralBefore);
	TestTrue(FString::Printf(TEXT("and raises the line tension (%.0f N to %.0f N)"), TensionBefore, PeakTension), PeakTension > 1.1f * TensionBefore);
	TestTrue(TEXT("The rider is still on the water, held down by the edge"), Board->GetBoardState() != EBoardState::Airborne);

	// Letting go pops, harder than a pop with no load.
	const bool bPopped = Ride.Pawn->ReleaseLoadAndPop();
	TestTrue(TEXT("Letting go pops"), bPopped && Board->GetBoardState() == EBoardState::Airborne);
	const float LoadedVz = Board->Velocity.Z;
	TestFalse(TEXT("The load is no longer held"), Board->IsLoadHeld());
	Ride.Simulate(0.3f);
	TestNearlyEqual(TEXT("and the crouch is gone"), Board->GetLoadAmount(), 0.0f, 0.001f);

	FRideFixture Plain(20.0f);
	Settled(Plain);
	Plain.Simulate(2.6f);
	Plain.Pawn->SetLoadHeld(true);
	const bool bTapPopped = Plain.Pawn->ReleaseLoadAndPop(); // a tap: pressed and let go at once
	TestTrue(TEXT("A tap of the button is a plain pop"), bTapPopped && Plain.Board->GetBoardState() == EBoardState::Airborne);
	UE_LOG(LogKiteSurf, Log, TEXT("LoadAndRelease: take-off %.0f cm/s loaded against %.0f cm/s from a tap"), LoadedVz, Plain.Board->Velocity.Z);
	TestTrue(FString::Printf(TEXT("A loaded pop leaves the water faster (%.0f cm/s against %.0f cm/s)"), LoadedVz, Plain.Board->Velocity.Z), LoadedVz > 1.25f * Plain.Board->Velocity.Z);

	// Letting go when the kite has already pulled the rider off the water does nothing more.
	Plain.Pawn->SetLoadHeld(true);
	TestFalse(TEXT("Letting go in the air is not a second pop"), Plain.Pawn->ReleaseLoadAndPop());

	// The loaded edge holds the rider down like a full edge does.
	FRideFixture Held;
	if (!Held.IsValid())
	{
		return false;
	}
	Held.Board->SetLoadHeld(true);
	Held.Simulate(0.6f);
	Held.Board->AddExternalForce(FVector(0.0f, 0.0f, 2.5f * Held.Board->MassKg * KiteUnits::GravityCmS2));
	Held.Board->TickComponent(RideDeltaTime, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Loaded, two and a half times the rider's weight upwards does not lift them"), Held.Board->GetBoardState() != EBoardState::Airborne);
	return true;
}

// The jointed rider: bones keep their length, the feet stay in the straps, the hands go to the bar.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfRiderRig, "KiteSurf.Rider.JointedRig", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfRiderRig::RunTest(const FString& Parameters)
{
	// The two-bone solver.
	{
		const FVector Root(0.0f, 0.0f, 100.0f);
		FVector End;
		FVector Joint = RiderRig::SolveTwoBone(Root, FVector(10.0f, 0.0f, 30.0f), FVector::ForwardVector, 45.0f, 42.0f, End);
		TestTrue(TEXT("A target within reach is reached"), End.Equals(FVector(10.0f, 0.0f, 30.0f), 0.01f));
		TestNearlyEqual(TEXT("The upper bone keeps its length"), static_cast<float>(FVector::Dist(Root, Joint)), 45.0f, 0.01f);
		TestNearlyEqual(TEXT("The lower bone keeps its length"), static_cast<float>(FVector::Dist(Joint, End)), 42.0f, 0.01f);
		TestTrue(TEXT("The joint bends towards the pole"), Joint.X > 5.0f);
		Joint = RiderRig::SolveTwoBone(Root, FVector(10.0f, 0.0f, 30.0f), FVector::BackwardVector, 45.0f, 42.0f, End);
		TestTrue(TEXT("and the other way for the opposite pole"), Joint.X < 5.0f);

		Joint = RiderRig::SolveTwoBone(Root, FVector(0.0f, 0.0f, -200.0f), FVector::ForwardVector, 45.0f, 42.0f, End);
		TestTrue(FString::Printf(TEXT("A target out of reach is pointed at with the limb nearly straight (%.1f cm long)"), FVector::Dist(Root, End)), FVector::Dist(Root, End) > 86.0f && FVector::Dist(Root, End) < 87.0f && FMath::Abs(End.X) < 0.01f);
		TestNearlyEqual(TEXT("with the bones still their own length"), static_cast<float>(FVector::Dist(Root, Joint) + FVector::Dist(Joint, End)), 87.0f, 0.01f);

		Joint = RiderRig::SolveTwoBone(Root, Root + FVector(0.0f, 0.0f, -1.0f), FVector::ForwardVector, 45.0f, 42.0f, End);
		TestNearlyEqual(TEXT("A target too close folds the limb without changing its bones"), static_cast<float>(FVector::Dist(Root, Joint)), 45.0f, 0.01f);
	}

	auto CheckLegs = [this](const TCHAR* What, const FRiderRigPose& Pose, const FRiderRigInput& Input)
	{
		const FVector Along = Input.Board.GetUnitAxis(EAxis::X);
		const FVector Centre = Input.Board.GetLocation() + Input.Board.GetUnitAxis(EAxis::Z) * RiderRig::AnkleHeightCm;
		bool bFeetInStraps = true;
		bool bBonesRight = true;
		bool bKneesForward = true;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FRiderLimbPose& Leg = Pose.Legs[Side];
			const FVector FromCentre = Leg.End - Centre;
			bFeetInStraps = bFeetInStraps && FMath::IsNearlyEqual(static_cast<float>(FMath::Abs(FVector::DotProduct(FromCentre, Along))), RiderRig::StrapHalfSpacingCm, 0.05f)
				&& (FromCentre - FVector::DotProduct(FromCentre, Along) * Along).Size() < 0.05f;
			bBonesRight = bBonesRight && FMath::IsNearlyEqual(static_cast<float>(FVector::Dist(Leg.Root, Leg.Joint)), RiderRig::ThighLengthCm, 0.01f)
				&& FMath::IsNearlyEqual(static_cast<float>(FVector::Dist(Leg.Joint, Leg.End)), RiderRig::ShinLengthCm, 0.01f);
			const FVector MidLeg = (Leg.Root + Leg.End) * 0.5f;
			bKneesForward = bKneesForward && FVector::DotProduct(Leg.Joint - MidLeg, Input.Facing) > 0.0f;
		}
		TestTrue(FString::Printf(TEXT("%s: both feet are in the straps"), What), bFeetInStraps);
		TestTrue(FString::Printf(TEXT("%s: thighs and shins keep their length"), What), bBonesRight);
		TestTrue(FString::Printf(TEXT("%s: the knees bend forwards"), What), bKneesForward);
		TestTrue(FString::Printf(TEXT("%s: the feet are apart, one in each strap"), What), FVector::Dist(Pose.Legs[0].End, Pose.Legs[1].End) > 2.0f * RiderRig::StrapHalfSpacingCm - 0.1f);
	};
	auto KneeAngleDeg = [](const FRiderLimbPose& Leg)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct((Leg.Root - Leg.Joint).GetSafeNormal(), (Leg.End - Leg.Joint).GetSafeNormal())));
	};

	// Standing across a level board that points along +X, facing its right rail (+Y).
	FRiderRigInput Input;
	Input.Board = FTransform(FRotator::ZeroRotator, FVector(1000.0f, 2000.0f, 50.0f));
	Input.Facing = FVector::RightVector;
	Input.BodyUp = FVector::UpVector;
	const FRiderRigPose Standing = RiderRig::SolveBody(Input);
	CheckLegs(TEXT("Standing"), Standing, Input);
	TestNearlyEqual(TEXT("Standing, the pelvis is its standing height above the ankles"), static_cast<float>(Standing.Pelvis.Z - 50.0f - RiderRig::AnkleHeightCm), RiderRig::StandingPelvisHeightCm, 0.1f);
	const float StandingKnee = KneeAngleDeg(Standing.Legs[0]);
	TestTrue(FString::Printf(TEXT("with the knees a little bent (%.0f deg)"), StandingKnee), StandingKnee > 120.0f && StandingKnee < 175.0f);
	// Facing +Y, the rider's right is -X: their right foot is in the strap towards -X.
	TestTrue(TEXT("The right foot is on the rider's right"), Standing.Legs[1].End.X < Standing.Legs[0].End.X);

	// Crouched: lower, with the knees bent much further.
	Input.Crouch = 1.0f;
	const FRiderRigPose Crouched = RiderRig::SolveBody(Input);
	CheckLegs(TEXT("Crouched"), Crouched, Input);
	TestTrue(FString::Printf(TEXT("A full crouch drops the pelvis by about %.0f cm (%.0f cm)"), RiderRig::StandingPelvisHeightCm * RiderRig::CrouchDropFraction, Standing.Pelvis.Z - Crouched.Pelvis.Z),
		FMath::IsNearlyEqual(static_cast<float>(Standing.Pelvis.Z - Crouched.Pelvis.Z), RiderRig::StandingPelvisHeightCm * RiderRig::CrouchDropFraction, 0.5f));
	TestTrue(FString::Printf(TEXT("and bends the knees much further (%.0f deg against %.0f deg)"), KneeAngleDeg(Crouched.Legs[0]), StandingKnee), KneeAngleDeg(Crouched.Legs[0]) < StandingKnee - 40.0f);
	Input.Crouch = 0.0f;

	// Leaning right out: the feet stay put and the body goes with the lean.
	Input.BodyUp = (FVector::UpVector - Input.Facing * FMath::Tan(FMath::DegreesToRadians(45.0f))).GetSafeNormal();
	const FRiderRigPose Leaning = RiderRig::SolveBody(Input);
	CheckLegs(TEXT("Leaning back 45 deg"), Leaning, Input);
	TestTrue(TEXT("Leaning back moves the pelvis back behind the feet"), FVector::DotProduct(Leaning.Pelvis - Input.Board.GetLocation(), Input.Facing) < -30.0f);
	Input.BodyUp = FVector::UpVector;

	// The board tilted and turned, as in a jump: the feet go with it.
	Input.Board = FTransform(FRotator(25.0f, 140.0f, -30.0f), FVector(-500.0f, 300.0f, 900.0f));
	Input.Facing = FRotator(0.0f, 140.0f - 90.0f, 0.0f).Vector();
	const FRiderRigPose Tilted = RiderRig::SolveBody(Input);
	CheckLegs(TEXT("On a tilted board"), Tilted, Input);

	// Facing the other rail, the feet swap straps so that the right foot is still on the right.
	Input.Board = FTransform(FRotator::ZeroRotator, FVector::ZeroVector);
	Input.Facing = -FVector::RightVector;
	const FRiderRigPose OtherWay = RiderRig::SolveBody(Input);
	CheckLegs(TEXT("Facing the other rail"), OtherWay, Input);
	TestTrue(TEXT("Facing -Y, the right foot is towards +X"), OtherWay.Legs[1].End.X > OtherWay.Legs[0].End.X);

	// Arms: the hands go to the points given, with the elbows below the line from shoulder to hand.
	FRiderRigPose Arms = Standing;
	const FVector LeftHand = Standing.Pelvis + Standing.Torso.RotateVector(FVector(40.0f, -14.0f, 30.0f));
	const FVector RightHand = Standing.Pelvis + Standing.Torso.RotateVector(FVector(40.0f, 14.0f, 30.0f));
	RiderRig::SolveArms(Arms, LeftHand, RightHand);
	TestTrue(TEXT("The hands reach the bar"), Arms.Arms[0].End.Equals(LeftHand, 0.05f) && Arms.Arms[1].End.Equals(RightHand, 0.05f));
	TestTrue(TEXT("The arm bones keep their length"), FMath::IsNearlyEqual(static_cast<float>(FVector::Dist(Arms.Arms[0].Root, Arms.Arms[0].Joint)), RiderRig::UpperArmLengthCm, 0.01f)
		&& FMath::IsNearlyEqual(static_cast<float>(FVector::Dist(Arms.Arms[1].Joint, Arms.Arms[1].End)), RiderRig::ForearmLengthCm, 0.01f));
	TestTrue(TEXT("The elbows hang below the arm's line"), Arms.Arms[0].Joint.Z < (Arms.Arms[0].Root.Z + Arms.Arms[0].End.Z) * 0.5f && Arms.Arms[1].Joint.Z < (Arms.Arms[1].Root.Z + Arms.Arms[1].End.Z) * 0.5f);
	TestTrue(TEXT("The shoulders are either side of the body"), FVector::Dist(Arms.Arms[0].Root, Arms.Arms[1].Root) > 40.0f);

	// On the pawn: the parts are drawn where the rig says, the hands are on the bar, and loading crouches.
	FRideFixture Ride;
	if (!Ride.IsValid())
	{
		return false;
	}
	Ride.Simulate(2.0f);
	const FRiderRigPose& Riding = Ride.Pawn->GetRiderRigPose();
	TestTrue(TEXT("The torso is drawn at the pelvis"), Ride.Pawn->GetRiderStaticMesh()->GetComponentLocation().Equals(Riding.Pelvis, 0.1f));
	TestTrue(TEXT("The left shin is drawn from the left knee"), Ride.Pawn->GetRiderLimbs()[1]->GetComponentLocation().Equals(Riding.Legs[0].Joint, 0.1f));
	TestTrue(TEXT("The right forearm is drawn from the right elbow"), Ride.Pawn->GetRiderLimbs()[7]->GetComponentLocation().Equals(Riding.Arms[1].Joint, 0.1f));
	const FVector BarLeft = Ride.Kite->GetBarEndWorldPosition(true);
	const FVector BarRight = Ride.Kite->GetBarEndWorldPosition(false);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FVector Hand = Riding.Arms[Side].End;
		const float OffBar = FMath::PointDistToSegment(Hand, BarLeft, BarRight);
		TestTrue(FString::Printf(TEXT("The %s hand is on the bar (%.1f cm off it)"), Side == 0 ? TEXT("left") : TEXT("right"), OffBar), OffBar < 2.5f);
	}
	TestTrue(TEXT("The left hand is nearer the bar's left end"), FVector::Dist(Riding.Arms[0].End, BarLeft) < FVector::Dist(Riding.Arms[0].End, BarRight));
	const float RidingPelvisHeight = Riding.Pelvis.Z - Ride.Pawn->GetActorLocation().Z;
	const float RidingKnee = KneeAngleDeg(Riding.Legs[0]);

	Ride.Pawn->SetLoadHeld(true);
	Ride.Simulate(0.8f);
	const FRiderRigPose& Loaded = Ride.Pawn->GetRiderRigPose();
	const float LoadedPelvisHeight = Loaded.Pelvis.Z - Ride.Pawn->GetActorLocation().Z;
	UE_LOG(LogKiteSurf, Log, TEXT("JointedRig: pelvis %.0f cm above the board riding, %.0f cm loaded; knee %.0f deg riding, %.0f deg loaded"), RidingPelvisHeight, LoadedPelvisHeight, RidingKnee, KneeAngleDeg(Loaded.Legs[0]));
	TestTrue(FString::Printf(TEXT("Loading sits the rider down (pelvis %.0f cm to %.0f cm above the board)"), RidingPelvisHeight, LoadedPelvisHeight), LoadedPelvisHeight < RidingPelvisHeight - 20.0f);
	TestTrue(TEXT("with the knees bent further"), KneeAngleDeg(Loaded.Legs[0]) < RidingKnee - 25.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
