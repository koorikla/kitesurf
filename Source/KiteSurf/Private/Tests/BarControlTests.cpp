#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "KiteComponent.h"
#include "KiteMotionBar.h"
#include "KiteRiderPawn.h"
#include "KiteSurfHUD.h"
#include "UI/KiteSurfControlsLegend.h"
#include "UI/KiteSurfGameInstance.h"
#include "UI/KiteSurfSaveGame.h"

#if WITH_DEV_AUTOMATION_TESTS

// The bar for the player: the sheet input springs back to the middle (the "Bar returns to middle"
// setting), the motion bar's recentre button, and the motion bar's Move mode, where moving the pad
// down or up sets the power. The player path is driven through the handlers Enhanced Input calls
// (OnSheetTriggered, OnRecenterMotionTriggered); the scripted path (SheetKite, SetSheetRateInput,
// ApplyScriptedInput) must keep holding the bar where it is put.

namespace BarControlTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float FrameSeconds = 1.0f / 60.0f;
	constexpr float GravityMS2 = 9.80665f;

	/** A pawn in a bare world, ticked by hand. The kite and the board are not stepped: only the input is under test. */
	struct FBarFixture
	{
		UWorld* World = nullptr;
		AKiteRiderPawn* Pawn = nullptr;

		FBarFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Pawn = World ? World->SpawnActor<AKiteRiderPawn>() : nullptr;
			if (Pawn)
			{
				Pawn->bStepSimulation = false;
			}
		}

		~FBarFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		void Tick(float Seconds)
		{
			const int32 Steps = FMath::RoundToInt(Seconds / FrameSeconds);
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				Pawn->Tick(FrameSeconds);
			}
		}

		/** The player's keys, stick or triggers on IA_Sheet. */
		void PlayerSheet(float Value)
		{
			Pawn->OnSheetTriggered(FInputActionValue(Value));
		}

		float Sheet() const { return Pawn->GetCurrentSheetInput(); }
	};

	/** A controller whose next reading the test sets before every tick. */
	class FTraceMotionSource : public IKiteMotionSource
	{
	public:
		virtual bool Poll(FKiteMotionSample& OutSample) override
		{
			OutSample = Sample;
			return true;
		}

		virtual FString GetDeviceName() const override { return TEXT("Trace Controller"); }

		FKiteMotionSample Sample;
	};

	/** "Up" in the controller's axes for a pad rolled right and pitched towards the player (deg), as FMotionBarFilter reads them. */
	FVector UpFor(float RollDeg, float PitchDeg)
	{
		const float Roll = FMath::DegreesToRadians(RollDeg);
		const float Pitch = FMath::DegreesToRadians(PitchDeg);
		return FVector(-FMath::Sin(Roll) * FMath::Cos(Pitch), FMath::Cos(Roll) * FMath::Cos(Pitch), FMath::Sin(Pitch)).GetSafeNormal();
	}

	/** Minimum-jerk position, 0..1 over Tau 0..1, and its second derivative (per Tau^2). */
	float MinJerk(float Tau) { Tau = FMath::Clamp(Tau, 0.0f, 1.0f); return Tau * Tau * Tau * (10.0f - 15.0f * Tau + 6.0f * Tau * Tau); }
	float MinJerkAccel(float Tau) { Tau = FMath::Clamp(Tau, 0.0f, 1.0f); return 60.0f * Tau - 180.0f * Tau * Tau + 120.0f * Tau * Tau * Tau; }
	float MinJerkRate(float Tau) { Tau = FMath::Clamp(Tau, 0.0f, 1.0f); return 30.0f * Tau * Tau - 60.0f * Tau * Tau * Tau + 30.0f * Tau * Tau * Tau * Tau; }

	/**
	 * Drives a pawn's motion bar with synthetic IMU readings: the pad held at a pitch, moved up and
	 * down or tilted, with an optional accelerometer and gyro bias and noise, as a real pad reads.
	 */
	struct FImuTrace
	{
		FBarFixture& Fixture;
		TSharedPtr<FTraceMotionSource> Source = MakeShared<FTraceMotionSource>();
		float PitchDeg = 30.0f;
		FVector AccelBiasG = FVector::ZeroVector;
		FVector GyroBiasRadS = FVector::ZeroVector;
		float AccelNoiseG = 0.0f;
		float GyroNoiseRadS = 0.0f;
		FRandomStream Noise = FRandomStream(1234);

		explicit FImuTrace(FBarFixture& InFixture) : Fixture(InFixture) {}

		void Send(const FVector& AccelG, const FVector& GyroRadS)
		{
			const FVector AccelNoise(Noise.FRandRange(-1.0f, 1.0f), Noise.FRandRange(-1.0f, 1.0f), Noise.FRandRange(-1.0f, 1.0f));
			const FVector GyroNoise(Noise.FRandRange(-1.0f, 1.0f), Noise.FRandRange(-1.0f, 1.0f), Noise.FRandRange(-1.0f, 1.0f));
			Source->Sample.AccelG = AccelG + AccelBiasG + AccelNoise * AccelNoiseG;
			Source->Sample.GyroRadS = GyroRadS + GyroBiasRadS + GyroNoise * GyroNoiseRadS;
			Fixture.Pawn->Tick(FrameSeconds);
		}

		/** Holds the pad still at its pitch. */
		void Still(float Seconds)
		{
			const int32 Steps = FMath::RoundToInt(Seconds / FrameSeconds);
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				Send(UpFor(0.0f, PitchDeg), FVector::ZeroVector);
			}
		}

		/** Moves the pad straight down (positive cm) or up (negative cm) over this long, without turning it. */
		void StrokeDown(float Cm, float Seconds)
		{
			const int32 Steps = FMath::RoundToInt(Seconds / FrameSeconds);
			const FVector Up = UpFor(0.0f, PitchDeg);
			for (int32 Step = 1; Step <= Steps; ++Step)
			{
				// Height goes down by Cm along a minimum-jerk path: its acceleration upwards is -Cm s''/T^2.
				const float Tau = static_cast<float>(Step) / Steps;
				const float UpAccelMS2 = -Cm * 0.01f * MinJerkAccel(Tau) / (Seconds * Seconds);
				Send(Up * (1.0f + UpAccelMS2 / GravityMS2), FVector::ZeroVector);
			}
		}

		/** Tips the pad towards the player by this many degrees over this long, turning about itself, without moving it. */
		void Tilt(float DeltaPitchDeg, float Seconds)
		{
			const int32 Steps = FMath::RoundToInt(Seconds / FrameSeconds);
			const float StartDeg = PitchDeg;
			for (int32 Step = 1; Step <= Steps; ++Step)
			{
				const float Tau = static_cast<float>(Step) / Steps;
				const float Pitch = StartDeg + DeltaPitchDeg * MinJerk(Tau);
				const float PitchRateRadS = FMath::DegreesToRadians(DeltaPitchDeg) * MinJerkRate(Tau) / Seconds;
				// As FMotionBarFilter turns "up": d(Up)/dt = Up x Gyro, so pitching towards the player is -X.
				Send(UpFor(0.0f, Pitch), FVector(-PitchRateRadS, 0.0f, 0.0f));
			}
			PitchDeg = StartDeg + DeltaPitchDeg;
		}

		/** Switches the motion bar on in this mode, with the bar at Sheet, and lets the pad settle. */
		void Start(EMotionSheetMode Mode, float Sheet = 0.5f)
		{
			Fixture.Pawn->SheetKite(Sheet);
			Fixture.Pawn->SetMotionSource(Source);
			Fixture.Pawn->SetMotionSheetMode(Mode);
			Fixture.Pawn->SetMotionBarEnabled(true);
			Still(1.0f);
		}
	};
}

using namespace BarControlTest;

// Keys held move the bar towards power or depower; let go, it springs back to the middle.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputBarSpringsToMiddle, "KiteSurf.Input.BarSpringsToMiddle", BarControlTest::Flags)

bool FKiteSurfInputBarSpringsToMiddle::RunTest(const FString& Parameters)
{
	FBarFixture Bar;
	if (!TestNotNull(TEXT("Pawn spawned"), Bar.Pawn))
	{
		return false;
	}
	TestTrue(TEXT("The bar returns to the middle by default"), Bar.Pawn->GetBarReturnsToMiddle());
	TestEqual(TEXT("The middle is half way"), Bar.Pawn->BarNeutralSheet, 0.5f);

	// The ride starts with the bar placed by script, and it stays there until the player touches it.
	Bar.Pawn->SheetKite(0.7f);
	Bar.Tick(1.0f);
	TestNearlyEqual(TEXT("Untouched, the bar stays where the ride put it"), Bar.Sheet(), 0.7f, 0.0001f);

	// Down held: power. The bar moves at SheetRatePerSec, not in one jump.
	Bar.PlayerSheet(1.0f);
	Bar.Tick(FrameSeconds);
	TestTrue(TEXT("Power held moves the bar in at the bar's own rate"), Bar.Sheet() > 0.7f && Bar.Sheet() <= 0.7f + Bar.Pawn->SheetRatePerSec * FrameSeconds + 0.0001f);
	Bar.Tick(0.3f);
	TestNearlyEqual(TEXT("Held, it reaches fully in"), Bar.Sheet(), 1.0f, 0.0001f);
	Bar.Tick(1.0f);
	TestNearlyEqual(TEXT("and stays fully in while held"), Bar.Sheet(), 1.0f, 0.0001f);

	// Let go: back to the middle at BarReturnRatePerSec.
	const float ReturnSeconds = 0.5f / Bar.Pawn->BarReturnRatePerSec;
	Bar.PlayerSheet(0.0f);
	Bar.Tick(0.5f * ReturnSeconds);
	TestTrue(FString::Printf(TEXT("Half way through its return the bar is on its way (%.2f)"), Bar.Sheet()), Bar.Sheet() > 0.6f && Bar.Sheet() < 0.9f);
	Bar.Tick(0.5f * ReturnSeconds + 0.05f);
	TestNearlyEqual(FString::Printf(TEXT("Let go, it is back in the middle within %.2f s"), ReturnSeconds), Bar.Sheet(), 0.5f, 0.0001f);
	Bar.Tick(1.0f);
	TestNearlyEqual(TEXT("and rests there"), Bar.Sheet(), 0.5f, 0.0001f);

	// Up held: depower, and back again.
	Bar.PlayerSheet(-1.0f);
	Bar.Tick(0.3f);
	TestNearlyEqual(TEXT("Depower held lets the bar right out"), Bar.Sheet(), 0.0f, 0.0001f);
	Bar.PlayerSheet(0.0f);
	Bar.Tick(ReturnSeconds + 0.05f);
	TestNearlyEqual(TEXT("Let go, it comes back to the middle"), Bar.Sheet(), 0.5f, 0.0001f);
	TestNearlyEqual(TEXT("The kite gets the bar"), Bar.Pawn->GetKite()->Sheet, 0.5f, 0.0001f);
	return true;
}

// Analog inputs set how far from the middle the bar goes, in proportion.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputBarSpringIsProportional, "KiteSurf.Input.BarSpringIsProportional", BarControlTest::Flags)

bool FKiteSurfInputBarSpringIsProportional::RunTest(const FString& Parameters)
{
	FBarFixture Bar;
	if (!TestNotNull(TEXT("Pawn spawned"), Bar.Pawn))
	{
		return false;
	}
	Bar.Pawn->SheetKite(0.5f);

	Bar.PlayerSheet(0.5f); // half the right trigger
	Bar.Tick(FrameSeconds);
	TestTrue(TEXT("Half a trigger moves the bar no faster than the bar's rate"), Bar.Sheet() <= 0.5f + Bar.Pawn->SheetRatePerSec * FrameSeconds + 0.0001f);
	Bar.Tick(1.0f);
	TestNearlyEqual(TEXT("Half a trigger is half way from the middle to fully in"), Bar.Sheet(), 0.75f, 0.001f);

	Bar.PlayerSheet(0.25f);
	Bar.Tick(1.0f);
	TestNearlyEqual(TEXT("Easing off to a quarter brings it back to a quarter of the way"), Bar.Sheet(), 0.625f, 0.001f);

	Bar.PlayerSheet(-0.5f); // half the left trigger
	Bar.Tick(1.0f);
	TestNearlyEqual(TEXT("Half the other trigger is half way to fully out"), Bar.Sheet(), 0.25f, 0.001f);

	Bar.PlayerSheet(0.0f);
	Bar.Tick(1.0f);
	TestNearlyEqual(TEXT("Released, the bar is in the middle"), Bar.Sheet(), 0.5f, 0.001f);
	return true;
}

// With the setting off, the bar stays where the keys leave it, as before.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputBarSpringOff, "KiteSurf.Input.BarSpringOff", BarControlTest::Flags)

bool FKiteSurfInputBarSpringOff::RunTest(const FString& Parameters)
{
	FBarFixture Bar;
	if (!TestNotNull(TEXT("Pawn spawned"), Bar.Pawn))
	{
		return false;
	}
	Bar.Pawn->SetBarReturnsToMiddle(false);
	Bar.Pawn->SheetKite(0.5f);

	Bar.PlayerSheet(1.0f);
	Bar.Tick(0.1f);
	TestNearlyEqual(TEXT("Held, the input moves the bar at SheetRatePerSec"), Bar.Sheet(), 0.5f + 0.1f * Bar.Pawn->SheetRatePerSec, 0.001f);
	Bar.PlayerSheet(0.0f);
	Bar.Tick(1.0f);
	TestNearlyEqual(TEXT("Let go, the bar stays where it was left"), Bar.Sheet(), 0.75f, 0.001f);

	Bar.PlayerSheet(0.5f);
	Bar.Tick(0.1f);
	TestNearlyEqual(TEXT("Half a trigger moves it at half the rate"), Bar.Sheet(), 0.75f + 0.05f * Bar.Pawn->SheetRatePerSec, 0.001f);
	Bar.PlayerSheet(0.0f);

	// Switching the setting on mid-ride: the next touch springs.
	Bar.Pawn->SetBarReturnsToMiddle(true);
	Bar.PlayerSheet(0.0f);
	Bar.Tick(1.0f);
	TestNearlyEqual(TEXT("Switched on, the bar returns to the middle"), Bar.Sheet(), 0.5f, 0.001f);
	return true;
}

// Scripted control holds the bar where it is put, spring or not.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputBarScriptedHolds, "KiteSurf.Input.BarScriptedHolds", BarControlTest::Flags)

bool FKiteSurfInputBarScriptedHolds::RunTest(const FString& Parameters)
{
	FBarFixture Bar;
	if (!TestNotNull(TEXT("Pawn spawned"), Bar.Pawn))
	{
		return false;
	}
	TestTrue(TEXT("The spring is on"), Bar.Pawn->GetBarReturnsToMiddle());

	// The player has had the bar and let go of it: it is sprung.
	Bar.PlayerSheet(1.0f);
	Bar.Tick(0.5f);
	Bar.PlayerSheet(0.0f);
	Bar.Tick(1.0f);
	TestTrue(TEXT("The player's input owns the bar"), Bar.Pawn->IsPlayerSheetInputActive());

	Bar.Pawn->SheetKite(0.8f);
	TestFalse(TEXT("SheetKite takes the bar back"), Bar.Pawn->IsPlayerSheetInputActive());
	Bar.Tick(2.0f);
	TestNearlyEqual(TEXT("and the bar holds where it was put"), Bar.Sheet(), 0.8f, 0.0001f);

	Bar.Pawn->SetSheetRateInput(-1.0f);
	Bar.Tick(0.1f);
	Bar.Pawn->SetSheetRateInput(0.0f);
	const float AfterRate = Bar.Sheet();
	TestNearlyEqual(TEXT("A scripted rate moves the bar"), AfterRate, 0.8f - 0.1f * Bar.Pawn->SheetRatePerSec, 0.001f);
	Bar.Tick(2.0f);
	TestNearlyEqual(TEXT("and it stays where the rate left it"), Bar.Sheet(), AfterRate, 0.0001f);

	// kitesurf.Input's path.
	Bar.Pawn->ApplyScriptedInput(0.0f, 1.0f, 0.0f, 0.0f, false);
	Bar.Tick(0.5f);
	Bar.Pawn->ApplyScriptedInput(0.0f, 0.0f, 0.0f, 0.0f, false);
	Bar.Tick(2.0f);
	TestNearlyEqual(TEXT("ApplyScriptedInput holds the bar fully in"), Bar.Sheet(), 1.0f, 0.0001f);

	// The player takes it again with the next press, and it springs.
	Bar.PlayerSheet(-1.0f);
	Bar.Tick(0.5f);
	Bar.PlayerSheet(0.0f);
	Bar.Tick(1.0f);
	TestNearlyEqual(TEXT("The player's next press springs back to the middle again"), Bar.Sheet(), 0.5f, 0.001f);
	return true;
}

// The two new settings are kept with the others; old saves load with the spring on and Tilt.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputBarSettingsSaved, "KiteSurf.Input.BarSettingsSaved", BarControlTest::Flags)

bool FKiteSurfInputBarSettingsSaved::RunTest(const FString& Parameters)
{
	const float VolumeBefore = FApp::GetVolumeMultiplier(); // ApplySaveGame sets it from the save

	UKiteSurfGameInstance* Fresh = NewObject<UKiteSurfGameInstance>();
	TestTrue(TEXT("A new game has the bar returning to the middle"), Fresh->bBarReturnsToMiddle);
	TestEqual(TEXT("and the motion bar's power on Tilt"), Fresh->MotionSheetMode, EMotionSheetMode::Tilt);
	const UKiteSurfSaveGame* NewSave = NewObject<UKiteSurfSaveGame>();
	TestTrue(TEXT("A save without the setting has it on"), NewSave->bBarReturnsToMiddle);
	TestEqual(TEXT("and Tilt"), NewSave->MotionSheetModeIndex, static_cast<int32>(EMotionSheetMode::Tilt));

	// Changed, written, through the bytes a save slot holds (in memory, never the player's slot), and read back.
	UKiteSurfGameInstance* Writer = NewObject<UKiteSurfGameInstance>();
	Writer->SetBarReturnsToMiddle(false);
	Writer->SetMotionSheetMode(EMotionSheetMode::Move);
	UKiteSurfSaveGame* Written = NewObject<UKiteSurfSaveGame>();
	Writer->WriteToSaveGame(*Written);
	TArray<uint8> Bytes;
	TestTrue(TEXT("The save serialises"), UGameplayStatics::SaveGameToMemory(Written, Bytes));
	const UKiteSurfSaveGame* Loaded = Cast<UKiteSurfSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("and loads"), Loaded))
	{
		return false;
	}
	TestFalse(TEXT("The spring setting survives the round trip"), Loaded->bBarReturnsToMiddle);
	TestEqual(TEXT("and the motion power mode"), Loaded->MotionSheetModeIndex, static_cast<int32>(EMotionSheetMode::Move));

	UKiteSurfGameInstance* Reader = NewObject<UKiteSurfGameInstance>();
	Reader->ApplySaveGame(*Loaded);
	TestFalse(TEXT("The game instance takes the spring setting from the save"), Reader->bBarReturnsToMiddle);
	TestEqual(TEXT("and the motion power mode"), Reader->MotionSheetMode, EMotionSheetMode::Move);

	// A mode index from nowhere reads as Tilt.
	UKiteSurfSaveGame* Odd = NewObject<UKiteSurfSaveGame>();
	Odd->MotionSheetModeIndex = 7;
	Reader->ApplySaveGame(*Odd);
	TestEqual(TEXT("An unknown mode index loads as Tilt"), Reader->MotionSheetMode, EMotionSheetMode::Tilt);

	FApp::SetVolumeMultiplier(VolumeBefore);
	return true;
}

// The recentre button is an Enhanced Input action on the right stick click and Home.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputRecenterMotionAction, "KiteSurf.Input.RecenterMotionAction", BarControlTest::Flags)

bool FKiteSurfInputRecenterMotionAction::RunTest(const FString& Parameters)
{
	UInputAction* Recenter = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/IA_RecenterMotion.IA_RecenterMotion"));
	if (!TestNotNull(TEXT("IA_RecenterMotion exists and loads"), Recenter))
	{
		return false;
	}
	TestEqual(TEXT("IA_RecenterMotion is a button"), Recenter->ValueType, EInputActionValueType::Boolean);

	const UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default"));
	if (!TestNotNull(TEXT("IMC_Default loads"), IMC))
	{
		return false;
	}
	TSet<FKey> RecenterKeys;
	for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
	{
		if (Mapping.Action == Recenter)
		{
			RecenterKeys.Add(Mapping.Key);
		}
		// Nothing else is on these keys.
		if (Mapping.Key == EKeys::Gamepad_RightThumbstick || Mapping.Key == EKeys::Home)
		{
			TestEqual(FString::Printf(TEXT("%s is only the recentre button"), *Mapping.Key.ToString()), Mapping.Action.Get(), static_cast<const UInputAction*>(Recenter));
		}
	}
	TestTrue(TEXT("The right stick click recentres"), RecenterKeys.Contains(EKeys::Gamepad_RightThumbstick));
	TestTrue(TEXT("Home recentres"), RecenterKeys.Contains(EKeys::Home));

	UClass* RiderClass = StaticLoadClass(AKiteRiderPawn::StaticClass(), nullptr, TEXT("/Game/Blueprints/BP_KiteRider.BP_KiteRider_C"));
	const AKiteRiderPawn* CDO = RiderClass ? Cast<AKiteRiderPawn>(RiderClass->GetDefaultObject()) : nullptr;
	if (TestNotNull(TEXT("BP_KiteRider loads"), CDO))
	{
		TestEqual(TEXT("BP_KiteRider binds IA_RecenterMotion"), CDO->GetRecenterMotionAction(), Recenter);
	}

	// The legend has it, with a key and a button.
	bool bInLegend = false;
	for (const FKiteSurfControlBinding& Binding : KiteSurfControlsLegend::GetBindings())
	{
		if (FString(Binding.Action).Contains(TEXT("recentre")))
		{
			bInLegend = FString(Binding.Keyboard).Contains(TEXT("Home")) && FString(Binding.Gamepad).Contains(TEXT("Right stick click"));
		}
	}
	TestTrue(TEXT("The controls legend shows Home and the right stick click for the recentre"), bInLegend);
	return true;
}

// Pressing recentre takes the way the pad is held now as level, with the bar in the middle, and says so.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputRecenterMotionCalibrates, "KiteSurf.Input.RecenterMotionCalibrates", BarControlTest::Flags)

bool FKiteSurfInputRecenterMotionCalibrates::RunTest(const FString& Parameters)
{
	FBarFixture Bar;
	APlayerController* PC = Bar.World ? Bar.World->SpawnActor<APlayerController>() : nullptr;
	AKiteSurfHUD* HUD = Bar.World ? Bar.World->SpawnActor<AKiteSurfHUD>() : nullptr;
	if (!TestTrue(TEXT("Controller, HUD and pawn spawned"), PC && HUD && Bar.Pawn))
	{
		return false;
	}
	PC->SetPlayerState(Bar.World->SpawnActor<APlayerState>());
	HUD->PlayerOwner = PC;
	PC->MyHUD = HUD;
	PC->Possess(Bar.Pawn);

	FImuTrace Imu(Bar);

	// With the motion bar off, the button does nothing: the right stick is the bar then.
	Bar.Pawn->OnRecenterMotionTriggered(FInputActionValue(true));
	TestEqual(TEXT("Off, the button does not recentre"), Bar.Pawn->GetMotionRecentreButtonCount(), 0);
	TestTrue(TEXT("and shows nothing"), HUD->GetJumpRejectionText().IsEmpty());

	Imu.Start(EMotionSheetMode::Tilt, 0.7f);
	TestTrue(TEXT("The motion bar follows the controller"), Bar.Pawn->IsMotionBarActive());

	// Held some other way: steering and power off the middle.
	Imu.Tilt(10.0f, 0.3f);
	for (int32 Step = 0; Step < 240; ++Step)
	{
		Imu.Send(UpFor(20.0f, Imu.PitchDeg), FVector::ZeroVector);
	}
	TestTrue(FString::Printf(TEXT("Tilted, it steers (%.2f)"), Bar.Pawn->GetCurrentSteerInput()), Bar.Pawn->GetCurrentSteerInput() > 0.3f);
	TestTrue(FString::Printf(TEXT("and the bar is in past where it was (%.2f)"), Bar.Sheet()), Bar.Sheet() > 0.8f);

	Bar.Pawn->OnRecenterMotionTriggered(FInputActionValue(true));
	TestEqual(TEXT("The button recentres"), Bar.Pawn->GetMotionRecentreButtonCount(), 1);
	TestEqual(TEXT("and says so"), HUD->GetJumpRejectionText(), FString(TEXT("Controller recentred")));
	for (int32 Step = 0; Step < 3; ++Step)
	{
		Imu.Send(UpFor(20.0f, Imu.PitchDeg), FVector::ZeroVector);
	}
	TestNearlyEqual(TEXT("The way it is held is now level"), Bar.Pawn->GetCurrentSteerInput(), 0.0f, 0.02f);
	TestNearlyEqual(TEXT("with the bar in the middle"), Bar.Sheet(), Bar.Pawn->BarNeutralSheet, 0.01f);
	TestNearlyEqual(TEXT("The mapping's level roll is the hold (deg)"), Bar.Pawn->MotionBarMapping.NeutralRollDeg, 20.0f, 1.0f);
	TestNearlyEqual(TEXT("and its level pitch (deg)"), Bar.Pawn->MotionBarMapping.NeutralPitchDeg, 40.0f, 1.0f);
	TestNearlyEqual(TEXT("and its bar position is the middle"), Bar.Pawn->MotionBarMapping.SheetAtNeutral, Bar.Pawn->BarNeutralSheet, 0.0001f);

	// From there, tipping it pulls the bar in as before.
	Imu.Tilt(10.0f, 0.3f);
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Imu.Send(UpFor(20.0f, Imu.PitchDeg), FVector::ZeroVector);
	}
	TestNearlyEqual(TEXT("Tilt mode: 10 deg towards you from the new level is a fifth of the throw"), Bar.Sheet(), 0.5f + 10.0f / Bar.Pawn->MotionBarMapping.SheetRangeDeg, 0.03f);
	return true;
}

// While the motion bar has the bar, the keys trim it and the trim springs back.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputBarSpringOnMotionBar, "KiteSurf.Input.BarSpringOnMotionBar", BarControlTest::Flags)

bool FKiteSurfInputBarSpringOnMotionBar::RunTest(const FString& Parameters)
{
	FBarFixture Bar;
	if (!TestNotNull(TEXT("Pawn spawned"), Bar.Pawn))
	{
		return false;
	}
	FImuTrace Imu(Bar);
	Imu.Start(EMotionSheetMode::Tilt, 0.6f);
	TestNearlyEqual(TEXT("The motion bar starts where the bar was"), Bar.Sheet(), 0.6f, 0.01f);

	Bar.PlayerSheet(1.0f);
	Imu.Still(0.5f);
	TestNearlyEqual(TEXT("Power held takes the bar fully in on top of the controller"), Bar.Sheet(), 1.0f, 0.001f);
	Bar.PlayerSheet(0.0f);
	Imu.Still(1.0f);
	TestNearlyEqual(TEXT("Let go, the bar is back where the controller has it"), Bar.Sheet(), 0.6f, 0.01f);
	TestNearlyEqual(TEXT("without moving the controller's own middle"), Bar.Pawn->MotionBarMapping.SheetAtNeutral, 0.6f, 0.0001f);

	// With the spring off, the keys move where the controller's bar sits, as before.
	Bar.Pawn->SetBarReturnsToMiddle(false);
	Bar.PlayerSheet(-1.0f);
	Imu.Still(0.1f);
	Bar.PlayerSheet(0.0f);
	Imu.Still(1.0f);
	TestNearlyEqual(TEXT("Spring off: the keys move the controller's bar and it stays"), Bar.Sheet(), 0.6f - 0.1f * Bar.Pawn->SheetRatePerSec, 0.02f);
	return true;
}

// Move mode: a 25 cm stroke down is full power, and it holds.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputMoveStrokeDownIsPower, "KiteSurf.Input.MoveStrokeDownIsPower", BarControlTest::Flags)

bool FKiteSurfInputMoveStrokeDownIsPower::RunTest(const FString& Parameters)
{
	FBarFixture Bar;
	if (!TestNotNull(TEXT("Pawn spawned"), Bar.Pawn))
	{
		return false;
	}
	Bar.Pawn->SetBarReturnsToMiddle(false); // held where the stroke leaves it, like a mouse
	FImuTrace Imu(Bar);
	Imu.Start(EMotionSheetMode::Move, 0.5f);
	TestEqual(TEXT("The motion bar is in the Move mode"), Bar.Pawn->GetMotionSheetMode(), EMotionSheetMode::Move);
	TestNearlyEqual(TEXT("Still, the bar is where it started"), Bar.Sheet(), 0.5f, 0.01f);

	// A small stroke moves it in proportion: 6 cm of 25 is about a quarter of the throw.
	Imu.StrokeDown(6.0f, 0.3f);
	Imu.Still(1.0f);
	TestNearlyEqual(FString::Printf(TEXT("A 6 cm stroke down pulls the bar in about a quarter (%.2f)"), Bar.Sheet()), Bar.Sheet(), 0.5f + 6.0f / Bar.Pawn->MotionBarMapping.MoveSheetFullStrokeCm, 0.06f);
	UE_LOG(LogTemp, Display, TEXT("MoveStrokeDownIsPower: 6 cm stroke reads %.2f cm"), Bar.Pawn->GetMotionStroke().GetDisplacementCm());

	Bar.Pawn->OnRecenterMotionTriggered(FInputActionValue(true)); // no controller HUD here; recentres all the same
	Imu.Still(0.5f);
	TestNearlyEqual(TEXT("Recentred, the bar is in the middle"), Bar.Sheet(), 0.5f, 0.01f);

	Imu.StrokeDown(25.0f, 0.5f);
	Imu.Still(1.0f);
	TestNearlyEqual(FString::Printf(TEXT("A 25 cm stroke down is full power (%.2f)"), Bar.Sheet()), Bar.Sheet(), 1.0f, 0.01f);
	Imu.Still(5.0f);
	TestNearlyEqual(TEXT("and with the pad held still there, it holds"), Bar.Sheet(), 1.0f, 0.01f);
	TestTrue(TEXT("The pad reads as still"), Bar.Pawn->GetMotionStroke().IsStill());
	return true;
}

// Move mode: a stroke up lets the bar out.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputMoveStrokeUpDepowers, "KiteSurf.Input.MoveStrokeUpDepowers", BarControlTest::Flags)

bool FKiteSurfInputMoveStrokeUpDepowers::RunTest(const FString& Parameters)
{
	FBarFixture Bar;
	if (!TestNotNull(TEXT("Pawn spawned"), Bar.Pawn))
	{
		return false;
	}
	Bar.Pawn->SetBarReturnsToMiddle(false);
	FImuTrace Imu(Bar);
	Imu.Start(EMotionSheetMode::Move, 0.5f);

	Imu.StrokeDown(-25.0f, 0.5f);
	Imu.Still(2.0f);
	TestNearlyEqual(FString::Printf(TEXT("A 25 cm stroke up lets the bar right out (%.2f)"), Bar.Sheet()), Bar.Sheet(), 0.0f, 0.01f);

	// From the stop, a stroke down comes straight back in: the travel stops at the bar's ends.
	Imu.StrokeDown(6.0f, 0.3f);
	Imu.Still(1.0f);
	TestTrue(FString::Printf(TEXT("From fully out, 6 cm down pulls it in at once (%.2f)"), Bar.Sheet()), Bar.Sheet() > 0.15f && Bar.Sheet() < 0.35f);
	return true;
}

// Move mode: held still for 10 s with a real pad's bias and noise, the bar barely drifts.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputMoveStillDoesNotDrift, "KiteSurf.Input.MoveStillDoesNotDrift", BarControlTest::Flags)

bool FKiteSurfInputMoveStillDoesNotDrift::RunTest(const FString& Parameters)
{
	FBarFixture Bar;
	if (!TestNotNull(TEXT("Pawn spawned"), Bar.Pawn))
	{
		return false;
	}
	Bar.Pawn->SetBarReturnsToMiddle(false); // no relaxation to hide a drift
	FImuTrace Imu(Bar);
	Imu.AccelBiasG = FVector(0.01f, 0.02f, -0.01f);
	Imu.GyroBiasRadS = FVector(0.01f, -0.005f, 0.008f);
	Imu.AccelNoiseG = 0.008f;
	Imu.GyroNoiseRadS = 0.02f;
	Imu.Start(EMotionSheetMode::Move, 0.5f);
	const float Start = Bar.Sheet();

	float MostOff = 0.0f;
	for (int32 Second = 0; Second < 10; ++Second)
	{
		Imu.Still(1.0f);
		MostOff = FMath::Max(MostOff, FMath::Abs(Bar.Sheet() - Start));
	}
	UE_LOG(LogTemp, Display, TEXT("MoveStillDoesNotDrift: most off in 10 s still %.4f of the throw"), MostOff);
	TestTrue(FString::Printf(TEXT("10 s held still drifts under 5%% of the throw (%.3f)"), MostOff), MostOff < 0.05f);

	// A shakier hand: noise that often breaks the stillness test, so the speed is only zeroed now and then.
	Imu.AccelNoiseG = 0.03f;
	Imu.GyroNoiseRadS = 0.1f;
	const float ShakyStart = Bar.Sheet();
	float ShakyMostOff = 0.0f;
	for (int32 Second = 0; Second < 10; ++Second)
	{
		Imu.Still(1.0f);
		ShakyMostOff = FMath::Max(ShakyMostOff, FMath::Abs(Bar.Sheet() - ShakyStart));
	}
	UE_LOG(LogTemp, Display, TEXT("MoveStillDoesNotDrift: most off in 10 s with a shaky hand %.4f of the throw"), ShakyMostOff);
	TestTrue(FString::Printf(TEXT("10 s held by a shaky hand drifts under 5%% of the throw (%.3f)"), ShakyMostOff), ShakyMostOff < 0.05f);
	Imu.AccelNoiseG = 0.008f;
	Imu.GyroNoiseRadS = 0.02f;
	Imu.Still(1.0f);

	// And a stroke still works on that pad.
	Imu.StrokeDown(25.0f, 0.5f);
	Imu.Still(1.0f);
	TestTrue(FString::Printf(TEXT("A 25 cm stroke on the noisy pad is full power (%.2f)"), Bar.Sheet()), Bar.Sheet() > 0.95f);
	return true;
}

// Move mode ignores a tilt that does not move the pad; Tilt mode is unchanged.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputMoveIgnoresTilt, "KiteSurf.Input.MoveIgnoresTilt", BarControlTest::Flags)

bool FKiteSurfInputMoveIgnoresTilt::RunTest(const FString& Parameters)
{
	{
		FBarFixture Bar;
		if (!TestNotNull(TEXT("Pawn spawned"), Bar.Pawn))
		{
			return false;
		}
		Bar.Pawn->SetBarReturnsToMiddle(false);
		FImuTrace Imu(Bar);
		Imu.Start(EMotionSheetMode::Move, 0.5f);
		Imu.Tilt(30.0f, 0.5f);
		Imu.Still(1.0f);
		UE_LOG(LogTemp, Display, TEXT("MoveIgnoresTilt: bar after a 30 deg tilt in Move mode %.4f"), Bar.Sheet());
		TestTrue(FString::Printf(TEXT("Move mode: tipped 30 deg towards you without moving, the bar barely moves (%.3f)"), Bar.Sheet()), FMath::Abs(Bar.Sheet() - 0.5f) < 0.05f);
		Imu.Tilt(-45.0f, 0.5f);
		Imu.Still(1.0f);
		TestTrue(FString::Printf(TEXT("nor tipped 45 deg the other way (%.3f)"), Bar.Sheet()), FMath::Abs(Bar.Sheet() - 0.5f) < 0.05f);
		// Steering is still the roll.
		for (int32 Step = 0; Step < 120; ++Step)
		{
			Imu.Send(UpFor(25.0f, Imu.PitchDeg), FVector::ZeroVector);
		}
		TestTrue(FString::Printf(TEXT("Move mode still steers with the roll (%.2f)"), Bar.Pawn->GetCurrentSteerInput()), Bar.Pawn->GetCurrentSteerInput() > 0.5f);
	}
	{
		FBarFixture Bar;
		if (!TestNotNull(TEXT("Pawn spawned"), Bar.Pawn))
		{
			return false;
		}
		FImuTrace Imu(Bar);
		Imu.Start(EMotionSheetMode::Tilt, 0.5f);
		TestEqual(TEXT("Tilt is the default mode"), Bar.Pawn->GetMotionSheetMode(), EMotionSheetMode::Tilt);
		Imu.Tilt(10.0f, 0.5f);
		Imu.Still(1.0f);
		TestNearlyEqual(TEXT("Tilt mode: 10 deg towards you is a fifth of the throw, as before"), Bar.Sheet(), 0.5f + 10.0f / Bar.Pawn->MotionBarMapping.SheetRangeDeg, 0.01f);
		Imu.StrokeDown(25.0f, 0.5f);
		Imu.Still(1.0f);
		TestNearlyEqual(TEXT("and moving the pad does nothing to the bar"), Bar.Sheet(), 0.5f + 10.0f / Bar.Pawn->MotionBarMapping.SheetRangeDeg, 0.01f);
	}
	return true;
}

// Move mode with the bar returning to the middle: the travel creeps back to the middle.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputMoveRelaxesToMiddle, "KiteSurf.Input.MoveRelaxesToMiddle", BarControlTest::Flags)

bool FKiteSurfInputMoveRelaxesToMiddle::RunTest(const FString& Parameters)
{
	FBarFixture Bar;
	if (!TestNotNull(TEXT("Pawn spawned"), Bar.Pawn))
	{
		return false;
	}
	TestTrue(TEXT("The bar returns to the middle by default"), Bar.Pawn->GetBarReturnsToMiddle());
	FImuTrace Imu(Bar);
	Imu.Start(EMotionSheetMode::Move, 0.5f);
	Imu.StrokeDown(25.0f, 0.5f);
	Imu.Still(0.3f);
	const float AfterStroke = Bar.Sheet();
	TestTrue(FString::Printf(TEXT("The stroke pulls the bar in (%.2f)"), AfterStroke), AfterStroke > 0.9f);
	Imu.Still(2.0f);
	const float AfterTwo = Bar.Sheet();
	UE_LOG(LogTemp, Display, TEXT("MoveRelaxesToMiddle: %.3f after the stroke, %.3f 2 s later"), AfterStroke, AfterTwo);
	TestTrue(FString::Printf(TEXT("Held, it creeps back towards the middle (%.2f after 2 s)"), AfterTwo), AfterTwo < AfterStroke - 0.05f && AfterTwo > 0.6f);
	Imu.Still(4.0f * Bar.Pawn->MoveRelaxSeconds);
	TestNearlyEqual(TEXT("and in the end rests in the middle"), Bar.Sheet(), 0.5f, 0.02f);
	return true;
}

// The stroke reads the same at any frame rate.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfInputMoveStrokeStepRate, "KiteSurf.Input.MoveStrokeStepRate", BarControlTest::Flags)

bool FKiteSurfInputMoveStrokeStepRate::RunTest(const FString& Parameters)
{
	const float Rates[] = { 60.0f, 120.0f, 240.0f };
	float Read[3] = { 0.0f, 0.0f, 0.0f };
	const FVector Up = UpFor(0.0f, 30.0f);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float Dt = 1.0f / Rates[Index];
		FMotionBarStroke Stroke;
		FKiteMotionSample Sample;
		Sample.AccelG = Up;
		for (int32 Step = 0; Step < FMath::RoundToInt(1.0f / Dt); ++Step)
		{
			Stroke.Update(Sample, Up, Dt);
		}
		const int32 StrokeSteps = FMath::RoundToInt(0.5f / Dt);
		for (int32 Step = 1; Step <= StrokeSteps; ++Step)
		{
			const float Tau = static_cast<float>(Step) / StrokeSteps;
			Sample.AccelG = Up * (1.0f - 0.10f * MinJerkAccel(Tau) / (0.25f * GravityMS2));
			Stroke.Update(Sample, Up, Dt);
		}
		Sample.AccelG = Up;
		for (int32 Step = 0; Step < FMath::RoundToInt(1.0f / Dt); ++Step)
		{
			Stroke.Update(Sample, Up, Dt);
		}
		Read[Index] = Stroke.GetDisplacementCm();
		TestTrue(FString::Printf(TEXT("%.0f Hz: still after the stroke"), Rates[Index]), Stroke.IsStill());
	}
	UE_LOG(LogTemp, Display, TEXT("MoveStrokeStepRate: a 10 cm stroke reads %.2f / %.2f / %.2f cm at 60 / 120 / 240 Hz"), Read[0], Read[1], Read[2]);
	TestNearlyEqual(TEXT("A 10 cm stroke reads about 10 cm (cm)"), Read[0], 10.0f, 1.5f);
	TestNearlyEqual(TEXT("120 Hz reads as 60 Hz within 5% (cm)"), Read[1], Read[0], 0.05f * Read[0]);
	TestNearlyEqual(TEXT("240 Hz reads as 60 Hz within 5% (cm)"), Read[2], Read[0], 0.05f * Read[0]);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
