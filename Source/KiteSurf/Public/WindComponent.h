#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WindComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class KITESURF_API UWindComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWindComponent();

	UFUNCTION(BlueprintCallable, Category = "Wind")
	FVector GetWindAt(const FVector& WorldLocation) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	FVector BaseWind;
};
