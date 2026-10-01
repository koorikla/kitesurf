#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KiteSurfHUD.generated.h"

class AKiteRiderPawn;

UCLASS()
class KITESURF_API AKiteSurfHUD : public AHUD
{
	GENERATED_BODY()

public:
	AKiteSurfHUD();

	virtual void DrawHUD() override;

	/** Converts velocity in cm/s to formatted knots string, e.g. "15.0 kn" */
	UFUNCTION(BlueprintPure, Category = "KiteSurf|HUD")
	static FString FormatKnots(float SpeedCmPerSec, bool bIncludeUnit = true);

	/** Converts cm/s to knots (1 kn = 51.44 cm/s) */
	UFUNCTION(BlueprintPure, Category = "KiteSurf|HUD")
	static float CmPerSecToKnots(float SpeedCmPerSec);

	/** Converts knots to cm/s (1 kn = 51.44 cm/s) */
	UFUNCTION(BlueprintPure, Category = "KiteSurf|HUD")
	static float KnotsToCmPerSec(float Knots);

protected:
	void DrawTelemetry(AKiteRiderPawn* RiderPawn);
	void DrawWindWindowArc(AKiteRiderPawn* RiderPawn, float CenterX, float CenterY, float Radius);
	void DrawWindCompass(const FVector& WindVec, float CenterX, float CenterY, float Radius);
	void DrawFPS(float ScreenX, float ScreenY);
};
