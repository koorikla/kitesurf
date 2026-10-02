#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimationAsset.h"
#include "Blueprint/UserWidget.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "KiteComponent.h"
#include "KiteGear.h"
#include "Materials/MaterialInterface.h"
#include "RiderCharacter.h"
#include "UI/KiteSurfGearPreview.h"
#include "UI/KiteSurfGearWidget.h"

// Every choice on the gear screen has something to show, on the water and in the preview. A rider,
// kite model or board added without its visuals fails here.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfGearEveryChoiceHasVisuals, "KiteSurf.Gear.EveryChoiceHasVisuals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfGearEveryChoiceHasVisuals::RunTest(const FString& Parameters)
{
	for (int32 Index = 0; Index < static_cast<int32>(ERiderCharacter::Count); ++Index)
	{
		const ERiderCharacter Rider = static_cast<ERiderCharacter>(Index);
		const FString Name = RiderCharacter::GetDisplayName(Rider);
		TestFalse(FString::Printf(TEXT("%s has a description"), *Name), FString(RiderCharacter::GetDescription(Rider)).IsEmpty());
		if (const TCHAR* Path = RiderCharacter::GetStaticMeshPath(Rider))
		{
			TestNotNull(FString::Printf(TEXT("%s's mesh %s loads"), *Name, Path), LoadObject<UStaticMesh>(nullptr, Path));
		}
		else
		{
			TestNotNull(FString::Printf(TEXT("%s is the mannequin, which loads"), *Name), LoadObject<USkeletalMesh>(nullptr, RiderCharacter::MannequinMeshPath));
			TestNotNull(TEXT("with its idle"), LoadObject<UAnimationAsset>(nullptr, RiderCharacter::MannequinIdlePath));
		}
	}

	TSet<FString> KiteMeshes;
	for (int32 Index = 0; Index < static_cast<int32>(EKiteModel::Count); ++Index)
	{
		const EKiteModel Model = static_cast<EKiteModel>(Index);
		const FString Path = KiteGear::GetMeshPath(Model);
		KiteMeshes.Add(Path);
		const UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
		if (!TestNotNull(FString::Printf(TEXT("%s's mesh %s loads"), KiteGear::GetDisplayName(Model), *Path), Mesh))
		{
			continue;
		}
		// Every slot has one of our materials, not the importer's grey.
		for (int32 Slot = 0; Slot < Mesh->GetStaticMaterials().Num(); ++Slot)
		{
			const UMaterialInterface* Material = Mesh->GetMaterial(Slot);
			TestTrue(FString::Printf(TEXT("%s slot %d has a project material (%s)"), *Path, Slot, Material ? *Material->GetPathName() : TEXT("none")),
				Material && Material->GetPathName().StartsWith(TEXT("/Game/Materials/")));
		}
	}
	TestEqual(TEXT("Each kite model looks different"), KiteMeshes.Num(), static_cast<int32>(EKiteModel::Count));

	TestTrue(TEXT("Boards get longer from the 132 to the 145"),
		KiteGear::GetLengthScale(EBoardSize::Small) < KiteGear::GetLengthScale(EBoardSize::Medium)
		&& KiteGear::GetLengthScale(EBoardSize::Medium) < KiteGear::GetLengthScale(EBoardSize::Large));
	TestEqual(TEXT("The 138 is the board as modelled"), KiteGear::GetLengthScale(EBoardSize::Medium), 1.0f);
	return true;
}

// The stand shows exactly what is chosen, only to its own camera, and turns.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfGearPreviewShowsTheChoice, "KiteSurf.Gear.PreviewShowsTheChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfGearPreviewShowsTheChoice::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World created"), World))
	{
		return false;
	}
	AKiteSurfGearPreview* Preview = World->SpawnActor<AKiteSurfGearPreview>(AKiteSurfGearPreview::GetStageLocation(), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Preview spawned"), Preview))
	{
		World->DestroyWorld(false);
		return false;
	}

	// The picture.
	UTextureRenderTarget2D* Picture = Preview->GetRenderTarget();
	TestNotNull(TEXT("It has a picture as soon as it is spawned"), Picture);
	if (Picture)
	{
		TestEqual(TEXT("picture width (px)"), Picture->SizeX, AKiteSurfGearPreview::ImageWidth);
		TestEqual(TEXT("picture height (px)"), Picture->SizeY, AKiteSurfGearPreview::ImageHeight);
		TestTrue(TEXT("which its camera draws into"), Preview->GetCapture()->TextureTarget == Picture);
	}
	TestEqual(TEXT("The camera draws only the stand"), Preview->GetCapture()->PrimitiveRenderMode, ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList);
	TestTrue(TEXT("and the stand is on its list"), Preview->GetCapture()->ShowOnlyActors.Contains(Preview));

	// Out of the game view: everything on the stand is for the preview camera only.
	TInlineComponentArray<UPrimitiveComponent*> Primitives(Preview);
	for (const UPrimitiveComponent* Primitive : Primitives)
	{
		if (Primitive->IsEditorOnly())
		{
			continue; // the camera's own frustum and icon, never in a game
		}
		TestTrue(FString::Printf(TEXT("%s is seen by the preview camera only"), *Primitive->GetName()), Primitive->bVisibleInSceneCaptureOnly);
	}
	TestTrue(TEXT("The stand is far above the water"), Preview->GetActorLocation().Z >= 50000.0);

	// A rider with a posed mesh, the boost kite in 12 m, the 145.
	Preview->ShowGear(ERiderCharacter::Wetsuit, EKiteModel::Boost, 12.0f, EBoardSize::Large);
	TestTrue(TEXT("Wetsuit: the posed mesh is shown"), Preview->GetRiderStaticMesh()->IsVisible()
		&& Preview->GetRiderStaticMesh()->GetStaticMesh() == LoadObject<UStaticMesh>(nullptr, RiderCharacter::GetStaticMeshPath(ERiderCharacter::Wetsuit)));

	TestTrue(TEXT("The boost kite is shown"), Preview->GetKiteMesh()->GetStaticMesh() == LoadObject<UStaticMesh>(nullptr, KiteGear::GetMeshPath(EKiteModel::Boost)));
	TestNearlyEqual(TEXT("A 12 m kite at the size it was modelled"), static_cast<float>(Preview->GetKiteMesh()->GetRelativeScale3D().X), 1.0f, 0.001f);
	TestNearlyEqual(TEXT("The 145 is longer than the 138 (length scale)"), static_cast<float>(Preview->GetBoardMesh()->GetRelativeScale3D().X), 145.0f / 138.0f, 0.001f);

	// The robot, the loop kite in 6 m, the 132.
	Preview->ShowGear(ERiderCharacter::Robot, EKiteModel::Loop, 6.0f, EBoardSize::Small);
	TestTrue(TEXT("Robot: the posed mesh is shown"), Preview->GetRiderStaticMesh()->IsVisible() && Preview->GetRiderStaticMesh()->GetStaticMesh() == LoadObject<UStaticMesh>(nullptr, RiderCharacter::GetStaticMeshPath(ERiderCharacter::Robot)));
	TestTrue(TEXT("The loop kite is shown"), Preview->GetKiteMesh()->GetStaticMesh() == LoadObject<UStaticMesh>(nullptr, KiteGear::GetMeshPath(EKiteModel::Loop)));
	TestNearlyEqual(TEXT("A 6 m kite is drawn at sqrt(6/12) of the 12 m"), static_cast<float>(Preview->GetKiteMesh()->GetRelativeScale3D().X), FMath::Sqrt(0.5f), 0.001f);
	TestNearlyEqual(TEXT("The 132 is shorter (length scale)"), static_cast<float>(Preview->GetBoardMesh()->GetRelativeScale3D().X), 132.0f / 138.0f, 0.001f);

	// It turns by itself; a turn by hand holds it there for a moment.
	const float StartDeg = Preview->GetTurnYawDeg();
	Preview->Tick(1.0f);
	TestNearlyEqual(TEXT("It turns by itself (deg in 1 s)"), FMath::FindDeltaAngleDegrees(StartDeg, Preview->GetTurnYawDeg()), Preview->AutoTurnDegPerSec, 0.01f);
	Preview->TurnBy(-45.0f);
	const float HandDeg = Preview->GetTurnYawDeg();
	Preview->Tick(1.0f);
	TestNearlyEqual(TEXT("After a turn by hand it waits (deg moved in 1 s)"), FMath::FindDeltaAngleDegrees(HandDeg, Preview->GetTurnYawDeg()), 0.0f, 0.01f);
	Preview->Tick(Preview->AutoTurnResumeSeconds);
	Preview->Tick(1.0f);
	TestTrue(TEXT("and then turns by itself again"), !FMath::IsNearlyEqual(Preview->GetTurnYawDeg(), HandDeg, 0.01f));

	World->DestroyWorld(false);
	return true;
}

// The gear screen puts up the stand, dresses it as the rows change, and takes it away when it closes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfGearScreenDrivesThePreview, "KiteSurf.Gear.ScreenDrivesThePreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfGearScreenDrivesThePreview::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World created"), World))
	{
		return false;
	}
	UKiteSurfGearWidget* Gear = CreateWidget<UKiteSurfGearWidget>(World, UKiteSurfGearWidget::StaticClass());
	if (!TestNotNull(TEXT("Gear widget created"), Gear))
	{
		World->DestroyWorld(false);
		return false;
	}
	// Built and held, as the viewport holds it while it is on screen.
	TSharedPtr<SWidget> OnScreen = Gear->TakeWidget();

	AKiteSurfGearPreview* Preview = Gear->GetPreview();
	if (TestNotNull(TEXT("Opening the gear screen puts up the preview"), Preview))
	{
		auto ShownRiderPath = [Preview]()
		{
			return Preview->GetRiderStaticMesh()->IsVisible() && Preview->GetRiderStaticMesh()->GetStaticMesh()
				? Preview->GetRiderStaticMesh()->GetStaticMesh()->GetPathName() : FString(TEXT("mannequin"));
		};
		auto ExpectedRiderPath = [](ERiderCharacter Rider)
		{
			const TCHAR* Path = RiderCharacter::GetStaticMeshPath(Rider);
			return Path ? LoadObject<UStaticMesh>(nullptr, Path)->GetPathName() : FString(TEXT("mannequin"));
		};

		TestEqual(TEXT("It shows the chosen rider"), ShownRiderPath(), ExpectedRiderPath(Gear->CurrentRider));
		for (int32 Click = 0; Click < static_cast<int32>(ERiderCharacter::Count); ++Click)
		{
			Gear->CycleRider();
			TestEqual(FString::Printf(TEXT("> shows %s"), RiderCharacter::GetDisplayName(Gear->CurrentRider)), ShownRiderPath(), ExpectedRiderPath(Gear->CurrentRider));
		}
		const ERiderCharacter Before = Gear->CurrentRider;
		Gear->CycleRiderBack();
		TestNotEqual(TEXT("< goes to another rider"), Gear->CurrentRider, Before);
		Gear->CycleRider();
		TestEqual(TEXT("and > comes back"), Gear->CurrentRider, Before);

		Gear->CycleKiteModel();
		TestTrue(TEXT("Changing the kite changes the kite shown"),
			Preview->GetKiteMesh()->GetStaticMesh() == LoadObject<UStaticMesh>(nullptr, KiteGear::GetMeshPath(Gear->CurrentKiteModel)));
		const float ScaleBefore = Preview->GetKiteMesh()->GetRelativeScale3D().X;
		Gear->SetKiteSizeM2(17.0f);
		TestTrue(TEXT("A bigger kite is drawn bigger"), Preview->GetKiteMesh()->GetRelativeScale3D().X > ScaleBefore);
		Gear->CycleBoardSize();
		TestNearlyEqual(TEXT("Changing the board changes its length"), static_cast<float>(Preview->GetBoardMesh()->GetRelativeScale3D().X),
			KiteGear::GetLengthScale(Gear->CurrentBoardSize), 0.001f);

		// Off the screen: its Slate widget goes, and with it the stand. (The bare test world has no
		// engine context, which destroying an actor in it warns about.)
		AddExpectedMessage(TEXT("World has no context"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		OnScreen.Reset();
		TestTrue(TEXT("Closing the gear screen takes the preview away"), !IsValid(Preview) || Preview->IsActorBeingDestroyed());
		TestNull(TEXT("and forgets it"), Gear->GetPreview());
	}

	World->DestroyWorld(false);
	return true;
}

#endif
