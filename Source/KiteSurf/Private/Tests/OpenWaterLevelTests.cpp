#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "GameFramework/PlayerStart.h"
#include "GerstnerWaterWaves.h"
#include "WaterBodyOceanActor.h"
#include "WaterZoneActor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	template <typename ActorType>
	ActorType* FindLevelActor(const UWorld* World)
	{
		for (AActor* Actor : World->PersistentLevel->Actors)
		{
			if (ActorType* Typed = Cast<ActorType>(Actor))
			{
				return Typed;
			}
		}
		return nullptr;
	}
}

// Guards the output of scripts/editor/make_open_water_level.py: the ocean must be drawable where the rider spawns.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfLevelOceanRendersAtSpawn, "KiteSurf.Level.OceanRendersAtSpawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKiteSurfLevelOceanRendersAtSpawn::RunTest(const FString& Parameters)
{
	const UWorld* World = LoadObject<UWorld>(nullptr, TEXT("/Game/Maps/L_OpenWater.L_OpenWater"));
	TestNotNull(TEXT("L_OpenWater loads"), World);
	if (!World || !World->PersistentLevel)
	{
		return false;
	}

	const AWaterBodyOcean* Ocean = FindLevelActor<AWaterBodyOcean>(World);
	const AWaterZone* Zone = FindLevelActor<AWaterZone>(World);
	const APlayerStart* PlayerStart = FindLevelActor<APlayerStart>(World);
	TestNotNull(TEXT("Level has a WaterBodyOcean"), Ocean);
	TestNotNull(TEXT("Level has a WaterZone"), Zone);
	TestNotNull(TEXT("Level has a PlayerStart"), PlayerStart);
	if (!Ocean || !Zone || !PlayerStart)
	{
		return false;
	}

	// A level generated without a renderer saves the ocean with no mesh, which is invisible in game.
	// UWaterBodyInfoMeshComponent's header is private to the Water plugin, so match it by class name.
	int32 NumInfoMeshes = 0;
	int32 NumBuiltInfoMeshes = 0;
	TInlineComponentArray<UStaticMeshComponent*> MeshComponents(Ocean);
	for (const UStaticMeshComponent* MeshComponent : MeshComponents)
	{
		if (MeshComponent->GetClass()->GetName() == TEXT("WaterBodyInfoMeshComponent"))
		{
			++NumInfoMeshes;
			NumBuiltInfoMeshes += MeshComponent->GetStaticMesh() ? 1 : 0;
		}
	}
	TestTrue(TEXT("Ocean has water info mesh components"), NumInfoMeshes > 0);
	TestEqual(TEXT("Ocean was saved with its render meshes built"), NumBuiltInfoMeshes, NumInfoMeshes);

	// The water mesh only exists inside the zone, and the ocean leaves a hole around its own origin.
	const FVector2D SpawnXY(PlayerStart->GetActorLocation());
	const FVector2D ZoneCentre(Zone->GetActorLocation());
	const FVector2D ZoneHalfExtent = Zone->GetZoneExtent() * 0.5;
	const FVector2D SpawnFromCentre = (SpawnXY - ZoneCentre).GetAbs();
	const double MinOpenWaterCm = 300000.0; // 3 km of water in every direction from spawn
	TestTrue(TEXT("Water zone reaches at least 3 km from the spawn"),
		ZoneHalfExtent.X - SpawnFromCentre.X >= MinOpenWaterCm && ZoneHalfExtent.Y - SpawnFromCentre.Y >= MinOpenWaterCm);
	TestTrue(TEXT("Ocean island hole is at least 3 km from the spawn"),
		FVector2D::Distance(SpawnXY, FVector2D(Ocean->GetActorLocation())) >= MinOpenWaterCm);

	// The cached wave list is what both the renderer and the board physics use.
	const UGerstnerWaterWaves* Waves = Cast<UGerstnerWaterWaves>(Ocean->GetWaterWaves());
	TestNotNull(TEXT("Ocean has Gerstner waves"), Waves);
	if (Waves)
	{
		const UGerstnerWaterWaveGeneratorSimple* Generator = Cast<UGerstnerWaterWaveGeneratorSimple>(Waves->GerstnerWaveGenerator);
		TestNotNull(TEXT("Waves use the simple generator"), Generator);
		const TArray<FGerstnerWave>& WaveList = Waves->GetGerstnerWaves();
		if (Generator)
		{
			TestEqual(TEXT("Cached wave list matches the generator settings"), WaveList.Num(), Generator->NumWaves);
		}

		float SteepnessSum = 0.0f;
		for (const FGerstnerWave& Wave : WaveList)
		{
			SteepnessSum += Wave.Steepness;
		}
		TestTrue(FString::Printf(TEXT("Summed wave steepness %.2f stays below 1 so the surface cannot fold over"), SteepnessSum), SteepnessSum < 1.0f);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
