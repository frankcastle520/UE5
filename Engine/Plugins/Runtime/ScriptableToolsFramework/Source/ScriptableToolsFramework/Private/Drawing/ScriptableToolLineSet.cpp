// Copyright Epic Games, Inc. All Rights Reserved.

#include "Drawing/ScriptableToolLineSet.h"

#include "Drawing/ScriptableToolLine.h"
#include "Drawing/PreviewGeometryActor.h"
#include "Misc/Guid.h"

void UScriptableToolLineSet::Initialize(TObjectPtr<UPreviewGeometry> PreviewGeometry)
{
	FString LineSetID = FGuid::NewGuid().ToString();
	LineSet = PreviewGeometry->AddLineSet(LineSetID);
}

void UScriptableToolLineSet::OnTick()
{
	for (TObjectPtr<UScriptableToolLine> LineComponent : LineComponents)
	{
		if (LineComponent->IsDirty())
		{
			int32 LineID = LineComponent->GetLineID();
			FRenderableLine LineDescription = LineComponent->GenerateLineDescription();

			LineSet->SetLineStart(LineID, LineDescription.Start);
			LineSet->SetLineEnd(LineID, LineDescription.End);
			LineSet->SetLineColor(LineID, LineDescription.Color);
			LineSet->SetLineThickness(LineID, LineDescription.Thickness);
		}
	}
}

UScriptableToolLine* UScriptableToolLineSet::AddLine()
{
	LineComponents.Add(NewObject<UScriptableToolLine>(this));
	UScriptableToolLine* NewLine = LineComponents.Last();
	NewLine->SetLineID(LineSet->AddLine(NewLine->GenerateLineDescription()));
	return NewLine;
}

void UScriptableToolLineSet::RemoveLine(UScriptableToolLine* Line)
{
	if (ensure(Line))
	{
		LineSet->RemoveLine(Line->GetLineID());
		LineComponents.Remove(Line);
	}
}

void UScriptableToolLineSet::RemoveAllLines()
{
	LineSet->Clear();
	LineComponents.Empty();
}

void UScriptableToolLineSet::SetAllLinesColor(FColor Color)
{
	LineSet->SetAllLinesColor(Color);
}

void UScriptableToolLineSet::SetAllLinesThickness(float Thickness)
{
	LineSet->SetAllLinesThickness(Thickness);
}