// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeModifierClipMorph.h"

#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/ICustomizableObjectEditor.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"
#include "MuCOE/Nodes/CustomizableObjectNodeObject.h"

class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


UCustomizableObjectNodeModifierClipMorph::UCustomizableObjectNodeModifierClipMorph()
	: Super()
{
	StartOffset = FVector::ZeroVector;
	bLocalStartOffset = true;
	B = 0.f;
	Radius = 8.f;
	Radius2 = 4.f;
	RotationAngle = 0.f;
	Exponent = 1.f;

	Origin = FVector::ZeroVector;
	Normal = -FVector::UpVector;
	MaxEffectRadius = -1.f;
}


FVector UCustomizableObjectNodeModifierClipMorph::GetOriginWithOffset() const
{
	FVector NewOrigin;

	if (bLocalStartOffset)
	{
		FVector XAxis, YAxis, ZAxis;
		FindLocalAxes(XAxis, YAxis, ZAxis);

		NewOrigin = Origin + StartOffset.X * XAxis + StartOffset.Y * YAxis + StartOffset.Z * ZAxis;
	}
	else
	{
		NewOrigin = Origin + StartOffset;
	}

	return NewOrigin;
}

void UCustomizableObjectNodeModifierClipMorph::FindLocalAxes(FVector& XAxis, FVector& YAxis, FVector& ZAxis) const
{
	YAxis = FVector(0.f, 1.f, 0.f);

	if (FMath::Abs(FVector::DotProduct(Normal, YAxis)) > 0.95f)
	{
		YAxis = FVector(0.f, 0.f, 1.f);
	}

	XAxis = FVector::CrossProduct(Normal, YAxis);
	XAxis = XAxis.RotateAngleAxis(RotationAngle, Normal);
	YAxis = FVector::CrossProduct(Normal, XAxis);
	ZAxis = Normal;

	XAxis.Normalize();
	YAxis.Normalize();
}


void UCustomizableObjectNodeModifierClipMorph::ChangeStartOffsetTransform()
{
	// Local Offset
	FVector XAxis, YAxis, ZAxis;
	FindLocalAxes(XAxis, YAxis, ZAxis);

	if (bLocalStartOffset)
	{
		StartOffset = FVector(FVector::DotProduct(StartOffset, XAxis), FVector::DotProduct(StartOffset, YAxis),
							   FVector::DotProduct(StartOffset, ZAxis));
	}
	else
	{
		StartOffset = StartOffset.X * XAxis + StartOffset.Y * YAxis + StartOffset.Z * ZAxis;
	}
}


UEdGraphPin* UCustomizableObjectNodeModifierClipMorph::OutputPin() const
{
	return FindPin(TEXT("Modifier"));
}


void UCustomizableObjectNodeModifierClipMorph::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == "bLocalStartOffset")
	{
		ChangeStartOffsetTransform();
	}

	GetGraphEditor()->ShowGizmoClipMorph(*this);

	Super::PostEditChangeProperty(PropertyChangedEvent);
}


void UCustomizableObjectNodeModifierClipMorph::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FCustomizableObjectCustomVersion::GUID);

	if (Ar.CustomVer(FCustomizableObjectCustomVersion::GUID) < FCustomizableObjectCustomVersion::PostLoadToCustomVersion &&
		bOldOffset_DEPRECATED &&
		bLocalStartOffset)
	{
		// Previous Offset
		FVector Tangent, Binormal;
		Origin.FindBestAxisVectors(Tangent, Binormal);
		FVector OldOffset = StartOffset.X * Tangent + StartOffset.Y * Binormal + StartOffset.Z * Normal;

		// Local Offset
		FVector XAxis, YAxis, ZAxis;
		FindLocalAxes(XAxis, YAxis, ZAxis);

		StartOffset = FVector(FVector::DotProduct(OldOffset, XAxis), FVector::DotProduct(OldOffset, YAxis),
							  FVector::DotProduct(OldOffset, ZAxis));
	}
}


void UCustomizableObjectNodeModifierClipMorph::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	UEdGraphPin* OutputPin = CustomCreatePin(EGPD_Output, Schema->PC_Modifier, FName("Modifier"));
}


FText UCustomizableObjectNodeModifierClipMorph::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("ClipMeshWithPlaneAndMorph", "Clip Mesh With Plane and Morph");
}


void UCustomizableObjectNodeModifierClipMorph::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	if (Pin == OutputPin())
	{
		TSharedPtr<ICustomizableObjectEditor> Editor = GetGraphEditor();

		if (Editor.IsValid())
		{
			Editor->UpdateGraphNodeProperties();
		}
	}
}


void UCustomizableObjectNodeModifierClipMorph::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);

	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::UnifyRequiredTags)
	{
		RequiredTags = Tags_DEPRECATED;
		Tags_DEPRECATED.Empty();
	}

	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::SnapToBoneComponentIndexToName)
	{
		ReferenceSkeletonComponent = FName(FString::FromInt(ReferenceSkeletonIndex_DEPRECATED));
	}
}


FText UCustomizableObjectNodeModifierClipMorph::GetTooltipText() const
{
	return LOCTEXT("Clip_Mesh_Morph_Tooltip", "Defines a cutting plane on a bone to cut tagged Materials that go past it, while morphing the mesh after the cut to blend in more naturally.\nIt only cuts and morphs mesh that receives some influence of that bone or other descendant bones.");
}


#undef LOCTEXT_NAMESPACE
