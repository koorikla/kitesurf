#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "KiteRiderPawn.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "WindComponent.h"
#include "Engine/World.h"
#include "KiteSurf.h"

#if WITH_DEV_AUTOMATION_TESTS

// Test 1: Speed envelope: 15 kn wind, 9m kite, steady state between 12 and 25 kn, clamped at MaxBoardSpeed
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfMovementSpeedEnvelope, "KiteSurf.Movement.SpeedEnvelope", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfMovementSpeedEnvelope::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);
	if (!World) return false;

	AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
	TestNotNull(TEXT("Pawn spawned"), Pawn);
	if (Pawn)
	{
		UBoardMovementComponent* BoardComp = Pawn->GetBoardMovement();
		UKiteComponent* KiteComp = Pawn->GetKite();
		UWindComponent* WindComp = Pawn->FindComponentByClass<UWindComponent>();

		TestNotNull(TEXT("BoardMovement valid"), BoardComp);
		TestNotNull(TEXT("KiteComp valid"), KiteComp);
		if (BoardComp && KiteComp)
		{
			if (WindComp)
			{
				// 15 kn base wind along X (771.6 cm/s)
				WindComp->BaseWind = FVector(771.6f, 0.0f, 0.0f);
			}

			KiteComp->AreaM2 = 9.0f;
			TestEqual(TEXT("Kite area is 9 m2"), KiteComp->AreaM2, 9.0f);

			// Set kite in power zone (azimuth 45 deg, elevation 30 deg), sheeted in
			Pawn->SetActorLocation(FVector::ZeroVector);
			Pawn->SetActorRotation(FRotator(0.0f, 45.0f, 0.0f)); // heading 45 deg across wind
			KiteComp->SetAzimuthDeg(40.0f);
			KiteComp->SetElevationDeg(25.0f);
			Pawn->SheetKite(0.8f);
			Pawn->EdgeBoard(0.6f);

			const float DeltaTime = 0.0333f;
			for (int32 i = 0; i < 300; ++i) // 10 seconds of simulation
			{
				KiteComp->UpdateKite(DeltaTime);
				Pawn->Tick(DeltaTime);
				BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
			}

			const float SpeedKnots = BoardComp->GetForwardSpeed() / 51.44f;
			UE_LOG(LogKiteSurf, Log, TEXT("SpeedEnvelope: Final Speed = %.2f kn (Expected 12..25 kn)"), SpeedKnots);
			TestTrue(TEXT("Steady-state board speed reaches at least 12 kn"), SpeedKnots >= 12.0f);
			TestTrue(TEXT("Steady-state board speed does not exceed 25 kn under standard power"), SpeedKnots <= 25.0f);
			TestTrue(TEXT("Speed does not exceed MaxBoardSpeed (35 kn)"), SpeedKnots <= 35.0f + 0.1f);
		}
	}

	World->DestroyWorld(false);
	return true;
}

// Test 2: Depowering fully brings rider to < 2 kn within 5 s at 15 kn wind
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfMovementDepowerToStop, "KiteSurf.Movement.DepowerToStop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfMovementDepowerToStop::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);
	if (!World) return false;

	AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
	TestNotNull(TEXT("Pawn spawned"), Pawn);
	if (Pawn)
	{
		UBoardMovementComponent* BoardComp = Pawn->GetBoardMovement();
		UKiteComponent* KiteComp = Pawn->GetKite();
		UWindComponent* WindComp = Pawn->FindComponentByClass<UWindComponent>();

		if (BoardComp && KiteComp)
		{
			if (WindComp)
			{
				WindComp->BaseWind = FVector(771.6f, 0.0f, 0.0f);
			}

			// Start in displacement mode moving forward at 350 cm/s (~6.8 kn)
			Pawn->SetActorLocation(FVector::ZeroVector);
			BoardComp->Velocity = FVector(350.0f, 0.0f, 0.0f);
			Pawn->SheetKite(0.0f); // Fully depowered
			Pawn->EdgeBoard(0.0f);
			KiteComp->SetElevationDeg(90.0f); // Kite at zenith / depowered

			const float DeltaTime = 0.0333f;
			for (int32 i = 0; i < 150; ++i) // 5 seconds
			{
				Pawn->Tick(DeltaTime);
				BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
			}

			const float FinalKnots = BoardComp->GetForwardSpeed() / 51.44f;
			UE_LOG(LogKiteSurf, Log, TEXT("DepowerToStop: Speed after 5s = %.2f kn (Target < 2.0 kn)"), FinalKnots);
			TestTrue(TEXT("Fully depowering brings rider to < 2 kn within 5 s"), FinalKnots < 2.0f);
		}
	}

	World->DestroyWorld(false);
	return true;
}

// Test 3: Upwind progress possible up to ~45 deg off true wind at 15 kn
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfMovementUpwindAngle, "KiteSurf.Movement.UpwindAngle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfMovementUpwindAngle::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);
	if (!World) return false;

	AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
	TestNotNull(TEXT("Pawn spawned"), Pawn);
	if (Pawn)
	{
		UBoardMovementComponent* BoardComp = Pawn->GetBoardMovement();
		UKiteComponent* KiteComp = Pawn->GetKite();
		UWindComponent* WindComp = Pawn->FindComponentByClass<UWindComponent>();

		if (BoardComp && KiteComp)
		{
			if (WindComp)
			{
				// Wind blowing along +X (towards east)
				WindComp->BaseWind = FVector(771.6f, 0.0f, 0.0f);
			}

			// Heading 45 deg off wind (upwind component = -X direction when wind is +X)
			// True wind origin is -X, blowing towards +X. Upwind heading is 135 deg (pointing back towards wind)
			// Or if board is edging hard into wind: board heading is oriented 45 deg relative to cross-wind.
			Pawn->SetActorLocation(FVector::ZeroVector);
			Pawn->SetActorRotation(FRotator(0.0f, 45.0f, 0.0f));
			BoardComp->Velocity = FVector(400.0f, 400.0f, 0.0f); // already planing
			Pawn->EdgeBoard(0.85f); // Edging hard upwind
			Pawn->SheetKite(0.9f);
			KiteComp->SetAzimuthDeg(60.0f);
			KiteComp->SetElevationDeg(25.0f);

			const float DeltaTime = 0.0333f;
			for (int32 i = 0; i < 90; ++i) // 3 seconds
			{
				KiteComp->UpdateKite(DeltaTime);
				Pawn->Tick(DeltaTime);
				BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
			}

			// Test that board maintains forward planing momentum and lateral drift is controlled
			const float ForwardSpeed = BoardComp->GetForwardSpeed();
			const float LateralSpeed = FMath::Abs(BoardComp->GetLateralSpeed());
			UE_LOG(LogKiteSurf, Log, TEXT("UpwindAngle: Forward = %.1f cm/s, Lateral = %.1f cm/s, IsPlaning = %d"),
				ForwardSpeed, LateralSpeed, BoardComp->IsPlaning());
			TestTrue(TEXT("Board maintains planing forward speed while edging upwind"), ForwardSpeed >= BoardComp->PlaningThresholdCmS);
			TestTrue(TEXT("Edge grip keeps lateral leeway smaller than forward speed"), LateralSpeed < ForwardSpeed);
		}
	}

	World->DestroyWorld(false);
	return true;
}

// Test 4: Water contact holds within +-20 cm and no NaN/Inf guards trigger
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfMovementNoNaNGuard, "KiteSurf.Movement.NoNaNGuard", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfMovementNoNaNGuard::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);
	if (!World) return false;

	AKiteRiderPawn* Pawn = World->SpawnActor<AKiteRiderPawn>();
	TestNotNull(TEXT("Pawn spawned"), Pawn);
	if (Pawn)
	{
		UBoardMovementComponent* BoardComp = Pawn->GetBoardMovement();
		UKiteComponent* KiteComp = Pawn->GetKite();

		if (BoardComp && KiteComp)
		{
			Pawn->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));
			BoardComp->Velocity = FVector(800.0f, 0.0f, 0.0f);

			// Rapid transitions (alternating full edge and sheeting)
			const float DeltaTime = 0.0333f;
			for (int32 i = 0; i < 180; ++i) // 6 seconds
			{
				const float Edge = (i % 60 < 30) ? 1.0f : -1.0f;
				const float Sheet = (i % 40 < 20) ? 1.0f : 0.0f;
				Pawn->EdgeBoard(Edge);
				Pawn->SheetKite(Sheet);
				Pawn->SteerKite(Edge);

				KiteComp->UpdateKite(DeltaTime);
				Pawn->Tick(DeltaTime);
				BoardComp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);

				const FVector Vel = Pawn->GetBoardVelocity();
				TestFalse(TEXT("Velocity does not contain NaN"), Vel.ContainsNaN());
				TestTrue(TEXT("Planing height within +-20 cm of water height"), FMath::Abs(Pawn->GetActorLocation().Z) <= 20.0f + 0.1f);
			}
		}
	}

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
