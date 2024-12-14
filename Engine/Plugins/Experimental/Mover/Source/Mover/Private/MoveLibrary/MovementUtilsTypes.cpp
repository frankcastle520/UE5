// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoveLibrary/MovementUtilsTypes.h"
#include "Components/PrimitiveComponent.h"
#include "MoverComponent.h"



FMovingComponentSet::FMovingComponentSet(USceneComponent* InUpdatedComponent)
{
	UpdatedComponent = InUpdatedComponent;

	if (UpdatedComponent.IsValid())
	{
		UpdatedPrimitive = Cast<UPrimitiveComponent>(UpdatedComponent);
		MoverComponent = UpdatedComponent->GetOwner()->FindComponentByClass<UMoverComponent>();

		checkf(!MoverComponent.IsValid() || UpdatedComponent == MoverComponent->GetUpdatedComponent(), TEXT("Expected MoverComponent to have the same UpdatedComponent"));
	}
}

FMovingComponentSet::FMovingComponentSet(UMoverComponent* InMoverComponent)
{
	MoverComponent = InMoverComponent;

	if (MoverComponent.IsValid())
	{
		UpdatedComponent = MoverComponent->GetUpdatedComponent();
		UpdatedPrimitive = Cast<UPrimitiveComponent>(UpdatedComponent);
	}
}
