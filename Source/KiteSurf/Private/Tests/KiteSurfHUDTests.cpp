#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "KiteSurfHUD.h"
#include "KiteRiderPawn.h"
#include "BoardMovementComponent.h"
#include "KiteComponent.h"
#include "WindComponent.h"
#include "KiteSurfUnits.h"
#include "KiteSurfGameMode.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDFormatsKnots, "KiteSurf.HUD.FormatsKnots", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfHUDFormatsKnots::RunTest(const FString& Parameters)
{
	// 0 cm/s -> 0.0 kn
	TestEqual(TEXT("0 cm/s formats to 0.0 kn"), AKiteSurfHUD::FormatKnots(0.0f), TEXT("0.0 kn"));

	// 1 knot = 51.44 cm/s
	TestEqual(TEXT("51.44 cm/s formats to 1.0 kn"), AKiteSurfHUD::FormatKnots(51.44f), TEXT("1.0 kn"));

	// 15 knots = 771.6 cm/s (BaseWind)
	TestEqual(TEXT("771.6 cm/s formats to 15.0 kn"), AKiteSurfHUD::FormatKnots(771.6f), TEXT("15.0 kn"));

	// 25 knots = 1286.0 cm/s
	TestEqual(TEXT("1286.0 cm/s formats to 25.0 kn"), AKiteSurfHUD::FormatKnots(1286.0f), TEXT("25.0 kn"));

	// Without unit
	TestEqual(TEXT("51.44 cm/s without unit formats to 1.0"), AKiteSurfHUD::FormatKnots(51.44f, false), TEXT("1.0"));

	// Unit conversion helper
	TestEqual(TEXT("CmPerSecToKnots(51.44f) == 1.0f"), AKiteSurfHUD::CmPerSecToKnots(51.44f), 1.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputAssetsValid, "KiteSurf.Input.AssetsValid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfInputAssetsValid::RunTest(const FString& Parameters)
{
	// Test Enhanced Input action assets load
	UInputAction* SteerAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/IA_Steer.IA_Steer"));
	TestNotNull(TEXT("IA_Steer asset exists and loads"), SteerAction);
	if (SteerAction)
	{
		TestEqual(TEXT("IA_Steer is Axis1D"), SteerAction->ValueType, EInputActionValueType::Axis1D);
	}

	UInputAction* SheetAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/IA_Sheet.IA_Sheet"));
	TestNotNull(TEXT("IA_Sheet asset exists and loads"), SheetAction);
	if (SheetAction)
	{
		TestEqual(TEXT("IA_Sheet is Axis1D"), SheetAction->ValueType, EInputActionValueType::Axis1D);
	}

	UInputAction* JumpAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/IA_Jump.IA_Jump"));
	TestNotNull(TEXT("IA_Jump asset exists and loads"), JumpAction);
	if (JumpAction)
	{
		TestEqual(TEXT("IA_Jump is Boolean"), JumpAction->ValueType, EInputActionValueType::Boolean);
	}

	UInputAction* PauseAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/IA_Pause.IA_Pause"));
	TestNotNull(TEXT("IA_Pause asset exists and loads"), PauseAction);
	if (PauseAction)
	{
		TestEqual(TEXT("IA_Pause is Boolean"), PauseAction->ValueType, EInputActionValueType::Boolean);
	}

	UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default"));
	TestNotNull(TEXT("IMC_Default asset exists and loads"), IMC);
	if (IMC)
	{
		TestTrue(TEXT("IMC_Default has mappings"), IMC->GetMappings().Num() > 0);
		bool bHasJumpMapping = false;
		bool bHasPauseMapping = false;
		for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
		{
			if (Mapping.Action == JumpAction)
			{
				bHasJumpMapping = true;
			}
			if (Mapping.Action == PauseAction)
			{
				bHasPauseMapping = true;
			}
		}
		TestTrue(TEXT("IMC_Default maps IA_Jump"), bHasJumpMapping);
		TestTrue(TEXT("IMC_Default maps IA_Pause"), bHasPauseMapping);

		TSet<FKey> NegativeKeys = { EKeys::Left, EKeys::Up, EKeys::A, EKeys::S, EKeys::Gamepad_LeftTriggerAxis, EKeys::Gamepad_RightY };
		TSet<FKey> FoundNegativeKeys;
		for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
		{
			if (NegativeKeys.Contains(Mapping.Key))
			{
				FoundNegativeKeys.Add(Mapping.Key);
				TestTrue(FString::Printf(TEXT("Mapping for %s has modifiers"), *Mapping.Key.ToString()), Mapping.Modifiers.Num() > 0);
				bool bHasNegate = false;
				for (const TObjectPtr<UInputModifier>& Mod : Mapping.Modifiers)
				{
					if (Mod && Mod->IsA<UInputModifierNegate>())
					{
						bHasNegate = true;
						break;
					}
				}
				TestTrue(FString::Printf(TEXT("Mapping for %s has InputModifierNegate"), *Mapping.Key.ToString()), bHasNegate);
			}
		}
		TestEqual(TEXT("All 6 negative inputs are mapped in IMC_Default"), FoundNegativeKeys.Num(), NegativeKeys.Num());

		// Down pulls the bar in (power), so it is the positive direction of IA_Sheet. The right
		// stick is negated above for the same reason: pulled back (its negative axis) is power.
		for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
		{
			if (Mapping.Key == EKeys::Down)
			{
				const bool bNegated = Mapping.Modifiers.ContainsByPredicate([](const TObjectPtr<UInputModifier>& Mod) { return Mod && Mod->IsA<UInputModifierNegate>(); });
				TestFalse(TEXT("Down is sheet-in: not negated"), bNegated);
			}
		}

		// The bar is on the arrows and the right stick, the board on WASD and the left stick.
		struct FExpectedMapping { FKey Key; const TCHAR* ActionName; };
		const FExpectedMapping ExpectedMappings[] =
		{
			{ EKeys::Left, TEXT("IA_Steer") }, { EKeys::Right, TEXT("IA_Steer") }, { EKeys::Gamepad_RightX, TEXT("IA_Steer") },
			{ EKeys::Up, TEXT("IA_Sheet") }, { EKeys::Down, TEXT("IA_Sheet") }, { EKeys::Gamepad_RightY, TEXT("IA_Sheet") },
			{ EKeys::A, TEXT("IA_Edge") }, { EKeys::D, TEXT("IA_Edge") }, { EKeys::Gamepad_LeftX, TEXT("IA_Edge") },
			{ EKeys::W, TEXT("IA_WeightShift") }, { EKeys::S, TEXT("IA_WeightShift") }, { EKeys::Gamepad_LeftY, TEXT("IA_WeightShift") },
			{ EKeys::SpaceBar, TEXT("IA_Jump") }, { EKeys::Escape, TEXT("IA_Pause") }, { EKeys::R, TEXT("IA_Reset") },
		};
		// Looping needs no key of its own: it is the bar held towards the kite's side.
		for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
		{
			TestFalse(FString::Printf(TEXT("%s is not bound to a loop modifier"), *Mapping.Key.ToString()), Mapping.Key == EKeys::LeftShift || Mapping.Key == EKeys::RightShift || Mapping.Key == EKeys::Gamepad_RightShoulder);
		}

		for (const FExpectedMapping& Expected : ExpectedMappings)
		{
			FString MappedAction;
			for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
			{
				if (Mapping.Key == Expected.Key && Mapping.Action)
				{
					MappedAction = Mapping.Action->GetName();
					break;
				}
			}
			TestEqual(FString::Printf(TEXT("%s is mapped to %s"), *Expected.Key.ToString(), Expected.ActionName), MappedAction, FString(Expected.ActionName));
		}
	}

	// Test BP_KiteRider Blueprint class and CDO defaults
	UClass* RiderClass = StaticLoadClass(AKiteRiderPawn::StaticClass(), nullptr, TEXT("/Game/Blueprints/BP_KiteRider.BP_KiteRider_C"));
	TestNotNull(TEXT("BP_KiteRider class exists and loads"), RiderClass);
	if (RiderClass)
	{
		AKiteRiderPawn* CDO = Cast<AKiteRiderPawn>(RiderClass->GetDefaultObject());
		TestNotNull(TEXT("BP_KiteRider CDO valid"), CDO);
		if (CDO)
		{
			TestNotNull(TEXT("BP_KiteRider has DefaultMappingContext"), CDO->GetDefaultMappingContext());
			TestNotNull(TEXT("BP_KiteRider has SteerAction"), CDO->GetSteerAction());
			TestNotNull(TEXT("BP_KiteRider has SheetAction"), CDO->GetSheetAction());
			TestNotNull(TEXT("BP_KiteRider has EdgeAction (IA_Edge wired to BoardMovement edging)"), CDO->GetEdgeAction());
			TestNotNull(TEXT("BP_KiteRider has WeightShiftAction"), CDO->GetWeightShiftAction());
			TestNotNull(TEXT("BP_KiteRider has JumpAction"), CDO->GetJumpAction());
			TestNotNull(TEXT("BP_KiteRider has PauseAction"), CDO->GetPauseAction());
			TestNotNull(TEXT("BP_KiteRider CDO has Kite component"), CDO->GetKite());
			TestNotNull(TEXT("BP_KiteRider CDO has BoardMovement component"), CDO->GetBoardMovement());
		}
	}

	// Test BP_KiteSurfGameMode Blueprint class and CDO defaults
	UClass* GameModeClass = StaticLoadClass(AKiteSurfGameMode::StaticClass(), nullptr, TEXT("/Game/Blueprints/BP_KiteSurfGameMode.BP_KiteSurfGameMode_C"));
	TestNotNull(TEXT("BP_KiteSurfGameMode class exists and loads"), GameModeClass);
	if (GameModeClass)
	{
		AKiteSurfGameMode* CDO = Cast<AKiteSurfGameMode>(GameModeClass->GetDefaultObject());
		TestNotNull(TEXT("BP_KiteSurfGameMode CDO valid"), CDO);
		if (CDO)
		{
			TestNotNull(TEXT("BP_KiteSurfGameMode DefaultPawnClass set"), CDO->DefaultPawnClass.Get());
			TestNotNull(TEXT("BP_KiteSurfGameMode HUDClass set"), CDO->HUDClass.Get());
			TestTrue(TEXT("InitialSpawnSpeedCmPerSec default is ~12 knots (617.28)"), CDO->InitialSpawnSpeedCmPerSec > 600.0f);
			if (UClass* PawnClass = CDO->DefaultPawnClass.Get())
			{
				TestTrue(TEXT("BP_KiteSurfGameMode pawn is an AKiteRiderPawn"), PawnClass->IsChildOf(AKiteRiderPawn::StaticClass()));
				if (AKiteRiderPawn* PawnCDO = Cast<AKiteRiderPawn>(PawnClass->GetDefaultObject()))
				{
					TestNotNull(TEXT("GameMode pawn has Kite component"), PawnCDO->GetKite());
					TestNotNull(TEXT("GameMode pawn has BoardMovement component"), PawnCDO->GetBoardMovement());
				}
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDJumpRejection, "KiteSurf.HUD.JumpRejection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfHUDJumpRejection::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);
	if (World)
	{
		AKiteSurfHUD* HUD = World->SpawnActor<AKiteSurfHUD>();
		TestNotNull(TEXT("HUD spawned"), HUD);
		if (HUD)
		{
			TestTrue(TEXT("Initially rejection text is empty"), HUD->GetJumpRejectionText().IsEmpty());

			HUD->ShowJumpRejection(EJumpRejectReason::NotPlaning);
			TestEqual(TEXT("Shows why there is no pop"), HUD->GetJumpRejectionText(), FString(TEXT("Get up on the board first")));
			TestTrue(TEXT("Remaining time > 0"), HUD->GetJumpRejectionRemainingTime() > 0.0f);

			HUD->ShowJumpRejection(EJumpRejectReason::TooSlow);
			TestEqual(TEXT("Shows Need more speed"), HUD->GetJumpRejectionText(), TEXT("Need more speed"));

			HUD->ShowJumpRejection(EJumpRejectReason::NotEdged);
			TestEqual(TEXT("Shows Edge harder"), HUD->GetJumpRejectionText(), TEXT("Edge harder"));

			HUD->ShowJumpRejection(EJumpRejectReason::None);
			TestTrue(TEXT("None clears rejection text"), HUD->GetJumpRejectionText().IsEmpty());
		}
		World->DestroyWorld(false);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDOnboardingFlow, "KiteSurf.HUD.OnboardingFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfHUDOnboardingFlow::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("World created"), World);
	if (World)
	{
		AKiteSurfHUD* HUD = World->SpawnActor<AKiteSurfHUD>();
		TestNotNull(TEXT("HUD spawned"), HUD);
		if (HUD)
		{
			// Default state
			TestTrue(TEXT("Onboarding is initially active"), HUD->IsOnboardingActive());
			TestEqual(TEXT("Starts at Step 0 (Steer)"), HUD->GetCurrentOnboardingStep(), 0);
			TestTrue(TEXT("Step 0 prompt mentions steer"), HUD->GetCurrentPromptText().Contains(TEXT("Steer")));

			// Advance to Step 1 (Sheet)
			HUD->AdvanceOnboardingStep();
			TestEqual(TEXT("Advanced to Step 1 (Sheet)"), HUD->GetCurrentOnboardingStep(), 1);
			TestTrue(TEXT("Step 1 prompt mentions sheet"), HUD->GetCurrentPromptText().Contains(TEXT("Sheet")));

			// Advance to Step 2 (Edge)
			HUD->AdvanceOnboardingStep();
			TestEqual(TEXT("Advanced to Step 2 (Edge)"), HUD->GetCurrentOnboardingStep(), 2);
			TestTrue(TEXT("Step 2 prompt mentions edge"), HUD->GetCurrentPromptText().Contains(TEXT("edge")));

			// Advance to Step 3 (Jump)
			HUD->AdvanceOnboardingStep();
			TestEqual(TEXT("Advanced to Step 3 (Jump)"), HUD->GetCurrentOnboardingStep(), 3);
			TestTrue(TEXT("Step 3 prompt mentions jump"), HUD->GetCurrentPromptText().Contains(TEXT("jump")));

			// Advance to Step 4 (Complete)
			HUD->AdvanceOnboardingStep();
			TestEqual(TEXT("Advanced to Step 4 (Completed)"), HUD->GetCurrentOnboardingStep(), 4);
			TestFalse(TEXT("Onboarding inactive once completed"), HUD->IsOnboardingActive());
			TestTrue(TEXT("Step 4 prompt mentions open water"), HUD->GetCurrentPromptText().Contains(TEXT("open water")));

			// Restart onboarding
			HUD->StartOnboarding();
			TestTrue(TEXT("Onboarding active after restart"), HUD->IsOnboardingActive());
			TestEqual(TEXT("Step reset to 0 after restart"), HUD->GetCurrentOnboardingStep(), 0);

			// Skip onboarding
			HUD->SkipOnboarding();
			TestFalse(TEXT("Onboarding inactive after skip"), HUD->IsOnboardingActive());
			TestEqual(TEXT("Step is 4 after skip"), HUD->GetCurrentOnboardingStep(), 4);
		}
		World->DestroyWorld(false);
	}
	return true;
}

// The bar display must move the way a bar does: down the throw as it is pulled in, and tilted
// towards the hand that is pulling.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDBarDisplay, "KiteSurf.HUD.BarDisplay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfHUDBarDisplay::RunTest(const FString& Parameters)
{
	const FVector2D ThrowTop(500.0, 100.0);
	const float ThrowLength = 80.0f;
	const float HalfWidth = 60.0f;
	const float MaxTiltDeg = 24.0f;
	FVector2D Left;
	FVector2D Right;

	// Centred and fully sheeted out: level, at the top of the throw.
	AKiteSurfHUD::GetBarEnds(0.0f, 0.0f, ThrowTop, ThrowLength, HalfWidth, MaxTiltDeg, Left, Right);
	TestNearlyEqual(TEXT("A centred bar is level"), Left.Y, Right.Y, 0.01);
	TestNearlyEqual(TEXT("Sheeted out it sits at the top of the throw"), Left.Y, 100.0, 0.01);
	TestNearlyEqual(TEXT("and is centred on the throw"), (Left.X + Right.X) * 0.5, 500.0, 0.01);

	// Pulled right in: at the bottom of the throw (screen Y grows downwards, towards the rider).
	AKiteSurfHUD::GetBarEnds(0.0f, 1.0f, ThrowTop, ThrowLength, HalfWidth, MaxTiltDeg, Left, Right);
	TestNearlyEqual(TEXT("Pulled in it sits at the bottom of the throw"), Left.Y, 180.0, 0.01);

	// Half in
	AKiteSurfHUD::GetBarEnds(0.0f, 0.5f, ThrowTop, ThrowLength, HalfWidth, MaxTiltDeg, Left, Right);
	TestNearlyEqual(TEXT("Half pulled in it sits half way"), Left.Y, 140.0, 0.01);

	// Steering right pulls the right hand in: the right end drops towards the rider.
	AKiteSurfHUD::GetBarEnds(1.0f, 0.5f, ThrowTop, ThrowLength, HalfWidth, MaxTiltDeg, Left, Right);
	TestTrue(TEXT("Steering right lowers the right end"), Right.Y > Left.Y + 10.0);
	TestTrue(TEXT("The left end stays on the left"), Left.X < Right.X);
	TestNearlyEqual(TEXT("The bar keeps its length when tilted"), FVector2D::Distance(Left, Right), 2.0 * HalfWidth, 0.01);
	TestNearlyEqual(TEXT("and stays centred on the throw"), (Left.Y + Right.Y) * 0.5, 140.0, 0.01);
	const double FullRightDrop = Right.Y - Left.Y;

	// Steering left mirrors it.
	AKiteSurfHUD::GetBarEnds(-1.0f, 0.5f, ThrowTop, ThrowLength, HalfWidth, MaxTiltDeg, Left, Right);
	TestNearlyEqual(TEXT("Steering left lowers the left end by the same amount"), Left.Y - Right.Y, FullRightDrop, 0.01);

	// Half steer tilts less, and out-of-range input does not tilt or slide it further.
	AKiteSurfHUD::GetBarEnds(0.5f, 0.5f, ThrowTop, ThrowLength, HalfWidth, MaxTiltDeg, Left, Right);
	TestTrue(TEXT("Half steer tilts less than full steer"), Right.Y - Left.Y > 0.0 && Right.Y - Left.Y < FullRightDrop);
	AKiteSurfHUD::GetBarEnds(3.0f, 2.0f, ThrowTop, ThrowLength, HalfWidth, MaxTiltDeg, Left, Right);
	TestNearlyEqual(TEXT("Steer is clamped"), Right.Y - Left.Y, FullRightDrop, 0.01);
	TestNearlyEqual(TEXT("Sheet is clamped to the throw"), (Left.Y + Right.Y) * 0.5, 180.0, 0.01);
	return true;
}

// The landing card (docs/physics/plan-2.md item 4): after each landing from a jump the HUD shows its load
// in g for a few seconds, "HOT" when the rider sank fast or the kite was low, and "CRASH" for a crash.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDLandingCard, "KiteSurf.HUD.LandingCard", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfHUDLandingCard::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A clean landing reads its g"), AKiteSurfHUD::FormatLandingCard(4.2f, false, true), FString(TEXT("LANDED 4.2 g")));
	TestEqual(TEXT("a hot one says so"), AKiteSurfHUD::FormatLandingCard(6.68f, true, true), FString(TEXT("LANDED 6.7 g  HOT")));
	TestEqual(TEXT("and a crash is a crash"), AKiteSurfHUD::FormatLandingCard(9.24f, true, false), FString(TEXT("CRASH 9.2 g  HOT")));

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteSurfHUD* HUD = World ? World->SpawnActor<AKiteSurfHUD>() : nullptr;
	AKiteRiderPawn* Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
	UBoardMovementComponent* Board = Pawn ? Pawn->GetBoardMovement() : nullptr;
	TestTrue(TEXT("HUD, rider and board created"), HUD && Board);
	if (!HUD || !Board)
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}
	Pawn->GetWind()->BaseWind = FVector::ZeroVector;
	Pawn->GetKite()->SetElevationDeg(80.0f);
	const float Frame = 1.0f / 60.0f;

	// Drops the board onto flat water at this sink (m/s), lined up with its course, and lets the HUD
	// look at it once the landing has happened.
	auto LandAt = [&](float SinkMS, bool bCrouch)
	{
		Pawn->SetActorRotation(FRotator::ZeroRotator);
		Pawn->SetActorLocation(FVector::ZeroVector);
		Board->Velocity = FVector(800.0f, 0.0f, 0.0f);
		Board->SetLoadHeld(bCrouch);
		Board->Simulate(0.5f);
		Pawn->SetActorLocation(FVector(0.0f, 0.0f, 10.0f));
		Board->Velocity = FVector(800.0f, 0.0f, -KiteUnits::MToCm(SinkMS));
		Board->SetBoardState(EBoardState::Airborne);
		Board->SetCurrentJumpAirtime(1.0f);
		Board->Simulate(Frame);
		Board->SetLoadHeld(false);
		HUD->UpdateLandingCard(Board, Frame);
	};

	HUD->UpdateLandingCard(Board, Frame);
	TestTrue(TEXT("No card before a landing"), HUD->GetLandingCardText().IsEmpty());

	LandAt(7.0f, true);
	const FString HotCard = HUD->GetLandingCardText();
	UE_LOG(LogTemp, Log, TEXT("LandingCard: crouched at 7 m/s the card reads '%s'; the board says %.2f g, hot %d, clean %d"), *HotCard, Board->GetLastLandingG(), Board->WasLastLandingHot(), Board->WasLastLandingClean());
	TestEqual(TEXT("A crouched landing at 7 m/s shows its g and HOT"), HotCard, AKiteSurfHUD::FormatLandingCard(Board->GetLastLandingG(), true, true));
	TestTrue(TEXT("and the card is hot"), HUD->IsLandingCardHot());
	for (float Seconds = 0.0f; Seconds < HUD->LandingCardSeconds + 0.1f; Seconds += Frame)
	{
		HUD->UpdateLandingCard(Board, Frame);
	}
	TestTrue(TEXT("It is gone a few seconds later"), HUD->GetLandingCardText().IsEmpty());

	// The same landing standing (the crouch let go of on the water first): a crash.
	LandAt(7.0f, false);
	UE_LOG(LogTemp, Log, TEXT("LandingCard: standing at 7 m/s the card reads '%s'"), *HUD->GetLandingCardText());
	TestEqual(TEXT("Standing, the same landing is a crash"), HUD->GetLandingCardText(), AKiteSurfHUD::FormatLandingCard(Board->GetLastLandingG(), true, false));
	TestTrue(FString::Printf(TEXT("over CrashLandingG (%.2f g)"), Board->GetLastLandingG()), Board->GetLastLandingG() > Board->CrashLandingG);

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
