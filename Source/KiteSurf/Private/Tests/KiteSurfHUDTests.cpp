#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "KiteSurfHUD.h"
#include "KiteRiderPawn.h"
#include "KiteSurfGameMode.h"
#include "InputMappingContext.h"
#include "InputAction.h"

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
			TestEqual(TEXT("Shows Not planing"), HUD->GetJumpRejectionText(), TEXT("Not planing"));
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

#endif // WITH_DEV_AUTOMATION_TESTS
