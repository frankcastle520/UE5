// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DynamicMeshComponent.h"
#include "SVGBaseDynamicMeshComponent.generated.h"

UCLASS()
class USVGBaseDynamicMeshComponent : public UDynamicMeshComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	USVGBaseDynamicMeshComponent();

	//~ Begin UActorComponent
	virtual void OnRegister() override;
	//~ End UActorComponent

protected:
	void MarkSVGMeshUpdated();

private:
	bool bMeshHasBeenUpdated;
};
