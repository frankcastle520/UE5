// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionBlueprintLibrary.h"

#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionExternalRenderInterface.h"

void UGeometryCollectionBlueprintLibrary::SetCustomInstanceDataByIndex(UGeometryCollectionComponent* GeometryCollectionComponent, int32 CustomDataIndex, float CustomDataValue)
{
	if (GeometryCollectionComponent == nullptr)
	{
		return;
	}

	IGeometryCollectionCustomDataInterface* CustomRenderer = Cast<IGeometryCollectionCustomDataInterface>(GeometryCollectionComponent->GetCustomRenderer());
	if (CustomRenderer == nullptr)
	{
		return;
	}

	CustomRenderer->SetCustomInstanceData(CustomDataIndex, CustomDataValue);
}

void UGeometryCollectionBlueprintLibrary::SetCustomInstanceDataByName(UGeometryCollectionComponent* GeometryCollectionComponent, FName CustomDataName, float CustomDataValue)
{
	if (GeometryCollectionComponent == nullptr)
	{
		return;
	}

	IGeometryCollectionCustomDataInterface* CustomRenderer = Cast<IGeometryCollectionCustomDataInterface>(GeometryCollectionComponent->GetCustomRenderer());
	if (CustomRenderer == nullptr)
	{
		return;
	}

	CustomRenderer->SetCustomInstanceData(CustomDataName, CustomDataValue);
}

void UGeometryCollectionBlueprintLibrary::SetISMPoolCustomInstanceData(UGeometryCollectionComponent* GeometryCollectionComponent, int32 CustomDataIndex, float CustomDataValue)
{
	SetCustomInstanceDataByIndex(GeometryCollectionComponent, CustomDataIndex, CustomDataValue);
}
