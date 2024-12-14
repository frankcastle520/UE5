// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "EdMode.h"
#include "MLDeformerPaintMode.generated.h"

class USkeletalMeshComponent;

namespace UE::MLDeformer
{
	class FMLDeformerEditorToolkit;
}

UCLASS()
class MLDEFORMERFRAMEWORKEDITOR_API UMLDeformerPaintMode
	: public UBaseLegacyWidgetEdMode
{
	GENERATED_BODY()

public:
	const static FEditorModeID Id;	

	UMLDeformerPaintMode();

	// UEdMode overrides
	virtual void Enter() override;
	virtual void CreateToolkit() override;
	virtual bool UsesTransformWidget() const override	{ return false; };
	virtual bool UsesPropertyWidgets() const override	{ return false; };
	virtual bool UsesToolkits() const override			{ return true; }
	// ~END UEdMode overrides

	void SetMLDeformerEditor(UE::MLDeformer::FMLDeformerEditorToolkit* Editor);
	void UpdatePose(USkeletalMeshComponent* SkeletalMeshComponent, bool bFullUpdate=true);

private:
	TArray<FVector3f> BindPosePositions;
};
