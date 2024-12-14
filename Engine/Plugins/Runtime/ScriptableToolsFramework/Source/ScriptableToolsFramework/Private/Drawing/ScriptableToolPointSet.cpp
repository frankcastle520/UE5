// Copyright Epic Games, Inc. All Rights Reserved.

#include "Drawing/ScriptableToolPointSet.h"

#include "Drawing/ScriptableToolPoint.h"
#include "Drawing/PreviewGeometryActor.h"
#include "Misc/Guid.h"

void UScriptableToolPointSet::Initialize(TObjectPtr<UPreviewGeometry> PreviewGeometry)
{
	FString PointSetID = FGuid::NewGuid().ToString();
	PointSet = PreviewGeometry->AddPointSet(PointSetID);
}

void UScriptableToolPointSet::OnTick()
{
	for (TObjectPtr<UScriptableToolPoint> PointComponent : PointComponents)
	{
		if (PointComponent->IsDirty())
		{
			int32 PointID = PointComponent->GetPointID();
			FRenderablePoint PointDescription = PointComponent->GeneratePointDescription();

			PointSet->SetPointPosition(PointID, PointDescription.Position);			
			PointSet->SetPointColor(PointID, PointDescription.Color);
			PointSet->SetPointSize(PointID, PointDescription.Size);
		}
	}
}

UScriptableToolPoint* UScriptableToolPointSet::AddPoint()
{
	PointComponents.Add(NewObject<UScriptableToolPoint>(this));
	UScriptableToolPoint* NewPoint = PointComponents.Last();
	NewPoint->SetPointID(PointSet->AddPoint(NewPoint->GeneratePointDescription()));
	return NewPoint;
}

void UScriptableToolPointSet::RemovePoint(UScriptableToolPoint* Point)
{
	if (ensure(Point))
	{
		PointSet->RemovePoint(Point->GetPointID());
		PointComponents.Remove(Point);
	}
}

void UScriptableToolPointSet::RemoveAllPoints()
{
	PointSet->Clear();
	PointComponents.Empty();
}

void UScriptableToolPointSet::SetAllPointsColor(FColor Color)
{
	PointSet->SetAllPointsColor(Color);
}

void UScriptableToolPointSet::SetAllPointsSize(float Size)
{
	PointSet->SetAllPointsSize(Size);
}