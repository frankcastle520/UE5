// Copyright Epic Games, Inc. All Rights Reserved.

#include "Drawing/ScriptableToolTriangleSet.h"

#include "Drawing/ScriptableToolTriangle.h"
#include "Drawing/PreviewGeometryActor.h"
#include "Misc/Guid.h"

void UScriptableToolTriangleSet::Initialize(TObjectPtr<UPreviewGeometry> PreviewGeometry)
{
	FString TriangleSetID = FGuid::NewGuid().ToString();
	TriangleSet = PreviewGeometry->AddTriangleSet(TriangleSetID);
}

void UScriptableToolTriangleSet::OnTick()
{
	for (TObjectPtr<UScriptableToolTriangle> TriangleComponent : TriangleComponents)
	{
		if (TriangleComponent->IsDirty())
		{
			int32 TriangleID = TriangleComponent->GetTriangleID();
			FRenderableTriangle TriangleDescription = TriangleComponent->GenerateTriangleDescription();

			TriangleSet->RemoveTriangle(TriangleID);
			TriangleSet->InsertTriangle(TriangleID, TriangleDescription);
		}
	}

	for (TObjectPtr<UScriptableToolQuad> QuadComponent : QuadComponents)
	{
		if (QuadComponent->IsDirty())
		{
			int32 TriangleAID = QuadComponent->GetTriangleAID();
			int32 TriangleBID = QuadComponent->GetTriangleBID();

			TPair<FRenderableTriangle, FRenderableTriangle> TriangleDescriptions = QuadComponent->GenerateQuadDescription();

			TriangleSet->RemoveTriangle(TriangleAID);
			TriangleSet->InsertTriangle(TriangleAID, TriangleDescriptions.Key);
			TriangleSet->RemoveTriangle(TriangleBID);
			TriangleSet->InsertTriangle(TriangleBID, TriangleDescriptions.Value);

		}
	}
}

UScriptableToolTriangle* UScriptableToolTriangleSet::AddTriangle()
{
	TriangleComponents.Add(NewObject<UScriptableToolTriangle>(this));
	UScriptableToolTriangle* NewTriangle = TriangleComponents.Last();
	NewTriangle->SetTriangleID(TriangleSet->AddTriangle(NewTriangle->GenerateTriangleDescription()));
	return NewTriangle;
}

UScriptableToolQuad* UScriptableToolTriangleSet::AddQuad()
{
	QuadComponents.Add(NewObject<UScriptableToolQuad>(this));
	UScriptableToolQuad* NewQuad = QuadComponents.Last();

	TPair<FRenderableTriangle, FRenderableTriangle> TriDescriptions = NewQuad->GenerateQuadDescription();

	int32 TriAID = TriangleSet->AddTriangle(TriDescriptions.Key);
	int32 TriBID = TriangleSet->AddTriangle(TriDescriptions.Value);

	NewQuad->SetTriangleAID(TriAID);
	NewQuad->SetTriangleBID(TriBID);

	return NewQuad;
}

void UScriptableToolTriangleSet::RemoveTriangle(UScriptableToolTriangle* Triangle)
{
	if (ensure(Triangle))
	{
		TriangleSet->RemoveTriangle(Triangle->GetTriangleID());
		TriangleComponents.Remove(Triangle);
	}
}

void UScriptableToolTriangleSet::RemoveQuad(UScriptableToolQuad* Quad)
{
	if (ensure(Quad))
	{
		TriangleSet->RemoveTriangle(Quad->GetTriangleAID());
		TriangleSet->RemoveTriangle(Quad->GetTriangleBID());
		QuadComponents.Remove(Quad);
	}
}


void UScriptableToolTriangleSet::RemoveAllFaces()
{
	TriangleSet->Clear();
	TriangleComponents.Empty();
}

void UScriptableToolTriangleSet::SetAllTrianglesColor(FColor Color)
{
	TriangleSet->SetAllTrianglesColor(Color);
}

void UScriptableToolTriangleSet::SetAllTrianglesMaterial(UMaterialInterface* Material)
{
	TriangleSet->SetAllTrianglesMaterial(Material);
}