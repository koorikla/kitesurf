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
#include "InputTriggers.h"

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

	// The trick buttons (held) and the hook and pass buttons (pressed): Boolean, each mapped once on the keyboard and once on the pad.
	// IA_Rotate (batch A) is held too: a key (LeftShift) and the gamepad's left trigger.
	const TCHAR* TrickActionNames[] = { TEXT("IA_GrabFront"), TEXT("IA_GrabBack"), TEXT("IA_OneFoot"), TEXT("IA_Hook"), TEXT("IA_Pass"), TEXT("IA_Rotate") };
	TArray<UInputAction*> TrickActions;
	for (const TCHAR* Name : TrickActionNames)
	{
		UInputAction* Action = LoadObject<UInputAction>(nullptr, *FString::Printf(TEXT("/Game/Input/%s.%s"), Name, Name));
		TestNotNull(FString::Printf(TEXT("%s asset exists and loads"), Name), Action);
		if (Action)
		{
			TestEqual(FString::Printf(TEXT("%s is Boolean"), Name), Action->ValueType, EInputActionValueType::Boolean);
			TrickActions.Add(Action);
		}
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
		for (const UInputAction* TrickAction : TrickActions)
		{
			int32 Count = 0;
			for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
			{
				if (Mapping.Action == TrickAction)
				{
					++Count;
					TestEqual(FString::Printf(TEXT("%s on %s has no modifier"), *TrickAction->GetName(), *Mapping.Key.ToString()), Mapping.Modifiers.Num(), 0);
				}
			}
			TestEqual(FString::Printf(TEXT("%s has a key and a button"), *TrickAction->GetName()), Count, 2);
		}

		// Gamepad_LeftTriggerAxis left this set in batch A: it is IA_Rotate's now (an InputTriggerDown,
		// not a negate modifier), since LT no longer lets the bar out.
		TSet<FKey> NegativeKeys = { EKeys::Left, EKeys::Up, EKeys::A, EKeys::S, EKeys::Gamepad_RightY };
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
		TestEqual(TEXT("All 5 negative inputs are mapped in IMC_Default"), FoundNegativeKeys.Num(), NegativeKeys.Num());

		// IA_Rotate's gamepad mapping is a digital press past half travel (InputTriggerDown), not a
		// raw non-zero value: the default trigger-less behaviour the engine would otherwise fall back to.
		for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
		{
			if (Mapping.Key == EKeys::Gamepad_LeftTriggerAxis)
			{
				bool bHasDown = false;
				for (const TObjectPtr<UInputTrigger>& Trigger : Mapping.Triggers)
				{
					if (Trigger && Trigger->IsA<UInputTriggerDown>())
					{
						bHasDown = true;
						TestEqual(TEXT("Gamepad_LeftTriggerAxis's Down trigger fires past half travel"), Cast<UInputTriggerDown>(Trigger.Get())->ActuationThreshold, 0.5f);
					}
				}
				TestTrue(TEXT("Gamepad_LeftTriggerAxis has a Down trigger"), bHasDown);
			}
		}

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
			// Grabs and the one-footer (T2.1, T2.2; docs/tricks.md 6.3).
			{ EKeys::Q, TEXT("IA_GrabFront") }, { EKeys::Gamepad_LeftShoulder, TEXT("IA_GrabFront") },
			{ EKeys::E, TEXT("IA_GrabBack") }, { EKeys::Gamepad_RightShoulder, TEXT("IA_GrabBack") },
			{ EKeys::C, TEXT("IA_OneFoot") }, { EKeys::Gamepad_LeftThumbstick, TEXT("IA_OneFoot") },
			// Unhooked riding (T3.1; docs/tricks/T3.md 1.6).
			{ EKeys::F, TEXT("IA_Hook") }, { EKeys::Gamepad_FaceButton_Top, TEXT("IA_Hook") },
			{ EKeys::X, TEXT("IA_Pass") }, { EKeys::Gamepad_FaceButton_Left, TEXT("IA_Pass") },
			// The rotation gate (batch A): LeftShift moved here from IA_Pass; LT is the gamepad's.
			{ EKeys::LeftShift, TEXT("IA_Rotate") }, { EKeys::Gamepad_LeftTriggerAxis, TEXT("IA_Rotate") },
		};
		// Looping needs no key of its own: it is the bar held towards the kite's side. The shift keys and
		// RB may be bound to tricks (RB is the back hand's grab, T2.1) but never to the steering
		// (docs/tricks/README.md decision 7).
		for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
		{
			const bool bModifierKey = Mapping.Key == EKeys::LeftShift || Mapping.Key == EKeys::RightShift || Mapping.Key == EKeys::Gamepad_RightShoulder;
			TestFalse(FString::Printf(TEXT("%s is not bound to IA_Steer as a loop modifier"), *Mapping.Key.ToString()),
				bModifierKey && Mapping.Action && Mapping.Action->GetName() == TEXT("IA_Steer"));
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
			TestNotNull(TEXT("BP_KiteRider has GrabFrontAction"), CDO->GetGrabFrontAction());
			TestNotNull(TEXT("BP_KiteRider has GrabBackAction"), CDO->GetGrabBackAction());
			TestNotNull(TEXT("BP_KiteRider has OneFootAction"), CDO->GetOneFootAction());
			TestNotNull(TEXT("BP_KiteRider has HookAction"), CDO->GetHookAction());
			TestNotNull(TEXT("BP_KiteRider has PassAction"), CDO->GetPassAction());
			TestNotNull(TEXT("BP_KiteRider has RotateAction"), CDO->GetRotateAction());
			if (CDO->GetHookAction() && CDO->GetPassAction())
			{
				TestEqual(TEXT("BP_KiteRider's HookAction is IA_Hook"), CDO->GetHookAction()->GetName(), FString(TEXT("IA_Hook")));
				TestEqual(TEXT("BP_KiteRider's PassAction is IA_Pass"), CDO->GetPassAction()->GetName(), FString(TEXT("IA_Pass")));
			}
			if (CDO->GetRotateAction())
			{
				TestEqual(TEXT("BP_KiteRider's RotateAction is IA_Rotate"), CDO->GetRotateAction()->GetName(), FString(TEXT("IA_Rotate")));
			}
			if (CDO->GetGrabBackAction())
			{
				TestEqual(TEXT("BP_KiteRider's GrabBackAction is IA_GrabBack"), CDO->GetGrabBackAction()->GetName(), FString(TEXT("IA_GrabBack")));
			}
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

	// A harder landing standing (the crouch let go of on the water first): a crash.
	LandAt(10.0f, false);
	UE_LOG(LogTemp, Log, TEXT("LandingCard: standing at 10 m/s the card reads '%s'"), *HUD->GetLandingCardText());
	TestEqual(TEXT("Standing, a 10 m/s landing is a crash"), HUD->GetLandingCardText(), AKiteSurfHUD::FormatLandingCard(Board->GetLastLandingG(), true, false));
	TestTrue(FString::Printf(TEXT("over CrashLandingG (%.2f g)"), Board->GetLastLandingG()), Board->GetLastLandingG() > Board->CrashLandingG);

	World->DestroyWorld(false);
	return true;
}

// The frame-rate readout counts real frames: a lesson's slow motion dilates game time, and the dilated
// frame time read 0.6x the frame rate.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDFPSUsesRealFrameTime, "KiteSurf.HUD.FPSUsesRealFrameTime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfHUDFPSUsesRealFrameTime::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A 60 Hz frame reads 60"), AKiteSurfHUD::FormatFPS(1.0f / 60.0f), FString(TEXT("FPS: 60")));
	TestEqual(TEXT("No frame reads 0"), AKiteSurfHUD::FormatFPS(0.0f), FString(TEXT("FPS: 0")));
	TestEqual(TEXT("No world: no frame"), AKiteSurfHUD::GetRealFrameSeconds(nullptr), 0.0f);

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World created"), World))
	{
		return false;
	}
	// A 60 Hz frame in the middle of a 0.6x slow motion.
	World->DeltaRealTimeSeconds = 1.0f / 60.0f;
	World->DeltaTimeSeconds = 0.6f / 60.0f;
	TestNearlyEqual(TEXT("The readout's frame is the real one, not the dilated one (s)"), AKiteSurfHUD::GetRealFrameSeconds(World), 1.0f / 60.0f, 1e-6f);
	TestEqual(TEXT("So it reads 60 through the slow motion, not 36"), AKiteSurfHUD::FormatFPS(AKiteSurfHUD::GetRealFrameSeconds(World)), FString(TEXT("FPS: 60")));
	World->DestroyWorld(false);
	return true;
}

// "NEW BEST" is for a jump the rider landed: a crashed jump is never a new best, whatever its height,
// and is not shown under a crash card.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfHUDNoNewBestOnCrash, "KiteSurf.HUD.NoNewBestOnCrash", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfHUDNoNewBestOnCrash::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Landed higher than the best landed jump: a new best"), AKiteSurfHUD::IsNewBestJump(250.0f, 200.0f, true));
	TestFalse(TEXT("Crashed higher than it: not a new best"), AKiteSurfHUD::IsNewBestJump(550.0f, 200.0f, false));
	TestFalse(TEXT("No landed jump before: nothing to beat"), AKiteSurfHUD::IsNewBestJump(250.0f, 0.0f, true));
	TestFalse(TEXT("Lower than the best: not a new best"), AKiteSurfHUD::IsNewBestJump(150.0f, 200.0f, true));

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AKiteSurfHUD* HUD = World ? World->SpawnActor<AKiteSurfHUD>() : nullptr;
	AKiteRiderPawn* Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
	UBoardMovementComponent* Board = Pawn ? Pawn->GetBoardMovement() : nullptr;
	if (!TestTrue(TEXT("HUD, rider and board created"), HUD && Board))
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
	// Drops the rider from this height onto flat water, crouched or standing, and lets the HUD follow the jump.
	auto DropFrom = [&](float HeightM, bool bCrouch) -> bool
	{
		Pawn->SetActorRotation(FRotator::ZeroRotator);
		Pawn->SetActorLocation(FVector::ZeroVector);
		Board->Velocity = FVector(800.0f, 0.0f, 0.0f);
		Board->SetLoadHeld(bCrouch);
		Board->Simulate(0.5f);
		HUD->UpdateJumpReadout(Board, Frame);
		Pawn->SetActorLocation(FVector(0.0f, 0.0f, KiteUnits::MToCm(HeightM)));
		Board->Velocity = FVector(800.0f, 0.0f, 0.0f);
		Board->SetBoardState(EBoardState::Airborne);
		Board->SetCurrentJumpAirtime(0.5f);
		const int32 Before = Board->GetJumpCount();
		for (float T = 0.0f; T < 4.0f && Board->GetJumpCount() == Before; T += Frame)
		{
			Board->Simulate(Frame);
			HUD->UpdateJumpReadout(Board, Frame);
		}
		Board->SetLoadHeld(false);
		HUD->UpdateJumpReadout(Board, Frame);
		return Board->GetJumpCount() > Before;
	};

	TestTrue(TEXT("A first jump from 2 m lands"), DropFrom(2.0f, true));
	TestTrue(FString::Printf(TEXT("crouched, Clean (%.1f g)"), Board->GetLastLandingG()), Board->WasLastLandingClean());
	TestFalse(TEXT("The first jump has nothing to beat"), HUD->IsJumpReadoutNewBest());

	TestTrue(TEXT("A higher one from 5.5 m"), DropFrom(5.5f, false));
	TestFalse(FString::Printf(TEXT("standing, a crash (%.1f g)"), Board->GetLastLandingG()), Board->WasLastLandingClean());
	TestTrue(TEXT("The readout shows the crashed jump"), !HUD->GetJumpReadoutText().IsEmpty());
	TestFalse(TEXT("It is higher than the best, but crashed: no new best"), HUD->IsJumpReadoutNewBest());
	TestFalse(TEXT("and no NEW BEST under it"), HUD->ShouldShowNewBest());
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
