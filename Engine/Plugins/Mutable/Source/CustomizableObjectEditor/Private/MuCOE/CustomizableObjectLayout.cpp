// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/CustomizableObjectLayout.h"

#include "Engine/StaticMesh.h"
#include "MuCO/CustomizableObjectCompilerTypes.h"
#include "MuCO/CustomizableObjectInstancePrivate.h"
#include "MuCOE/CustomizableObjectCompiler.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceLayout.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/MutableUtils.h"
#include "MuCOE/ICustomizableObjectEditor.h"
#include "MuCOE/Nodes/CustomizableObjectNodeSkeletalMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTable.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"

UCustomizableObjectLayout::UCustomizableObjectLayout()
{
	GridSize = FIntPoint(4, 4);
	MaxGridSize = FIntPoint(4, 4);

	FCustomizableObjectLayoutBlock Block(FIntPoint(0, 0), FIntPoint(4, 4));
	Blocks.Add(Block);

	PackingStrategy = ECustomizableObjectTextureLayoutPackingStrategy::Resizable;
	BlockReductionMethod = ECustomizableObjectLayoutBlockReductionMethod::Halve;
}


void UCustomizableObjectLayout::SetLayout(int32 LODIndex, int32 MatIndex, int32 UVIndex)
{
	LOD = LODIndex;
	Material = MatIndex;
	UVChannel = UVIndex;
}


mu::EPackStrategy ConvertLayoutStrategy(const ECustomizableObjectTextureLayoutPackingStrategy LayoutPackStrategy)
{
	mu::EPackStrategy PackStrategy = mu::EPackStrategy::Fixed;

	switch (LayoutPackStrategy)
	{
	case ECustomizableObjectTextureLayoutPackingStrategy::Fixed:
		PackStrategy = mu::EPackStrategy::Fixed;
		break;

	case ECustomizableObjectTextureLayoutPackingStrategy::Resizable:
		PackStrategy = mu::EPackStrategy::Resizeable;
		break;

	case ECustomizableObjectTextureLayoutPackingStrategy::Overlay:
		PackStrategy = mu::EPackStrategy::Overlay;
		break;

	default:
		checkNoEntry();
	}

	return PackStrategy;
}


void UCustomizableObjectLayout::SetGridSize(FIntPoint Size)
{
	GridSize = Size;
}


void UCustomizableObjectLayout::SetMaxGridSize(FIntPoint Size)
{
	MaxGridSize = Size;
}


void UCustomizableObjectLayout::SetLayoutName(FString Name)
{
	LayoutName = Name;
}


void UCustomizableObjectLayout::GenerateAutomaticBlocksFromUVs()
{
	UCustomizableObjectNode* Node = Cast<UCustomizableObjectNode>(GetOuter());
	const UObject* Mesh = GetMesh();

	if (!Node || !Mesh)
	{
		return;
	}

	if (AutomaticBlocksStrategy == ECustomizableObjectLayoutAutomaticBlocksStrategy::Ignore)
	{
		return;
	}

	// Create a GenerationContext
	FCustomizableObjectCompiler Compiler;
	UCustomizableObject* Object = Node->GetGraphEditor()->GetCustomizableObject();
	FCompilationOptions Options = Object->GetPrivate()->GetCompileOptions();

	FMutableGraphGenerationContext GenerationContext(Object, &Compiler, Options);
	GenerationContext.bSkipParticipatingObjectsPass = true;

	bool bOutWasEmpty = false;
	mu::Ptr<mu::NodeLayout> LayoutNode = CreateMutableLayoutNode(GenerationContext, this, false, bOutWasEmpty );

	if (LayoutNode)
	{
		AutomaticBlocks.Empty();

		// Generate the editor layout blocks from the mutable layout
		for (int32 i = 0; i < LayoutNode->Blocks.Num(); ++i)
		{
			FIntPoint Min = FIntPoint(LayoutNode->Blocks[i].Min.X, LayoutNode->Blocks[i].Min.Y);
			FIntPoint Size = FIntPoint(LayoutNode->Blocks[i].Size.X, LayoutNode->Blocks[i].Size.Y);

			// Ignore blocks contained inside any block in the initial block set.
			bool bContainedInInitialSet = false;
			for (const FCustomizableObjectLayoutBlock& Block : Blocks)
			{
				FInt32Rect ExistingRect(Block.Min, Block.Max + FIntPoint(1));
				if (ExistingRect.Contains(Min) && ExistingRect.Contains(Min + Size))
				{
					bContainedInInitialSet = true;
					break;
				}
			}
			if (bContainedInInitialSet)
			{
				continue;
			}

			FCustomizableObjectLayoutBlock Block(FIntPoint(Min.X, Min.Y), FIntPoint(Min.X + Size.X, Min.Y + Size.Y));
			Block.bIsAutomatic = true;
			Block.Id = FGuid::NewGuid();

			mu::Ptr<mu::Image> Mask = LayoutNode->Blocks[i].Mask;
			if (Mask)
			{
				UTexture2D* UnrealImage = NewObject<UTexture2D>(UTexture2D::StaticClass());

				FMutableModelImageProperties Props;
				Props.Filter = TF_Nearest;
				Props.SRGB = true;
				Props.LODBias = 0;
				ConvertImage(UnrealImage, Mask, Props );
				UnrealImage->NeverStream = true;
				UnrealImage->UpdateResource();

				Block.Mask = UnrealImage;
			}
			AutomaticBlocks.Add(Block);
		}

		Node->PostEditChange();
		Node->GetGraph()->MarkPackageDirty();
	}
}


void UCustomizableObjectLayout::ConsolidateAutomaticBlocks()
{
	Blocks.Append(AutomaticBlocks);
	AutomaticBlocks.Empty();
	for (FCustomizableObjectLayoutBlock& Block : Blocks)
	{
		Block.Id = FGuid::NewGuid();
		Block.bIsAutomatic = false;
	}
}


void UCustomizableObjectLayout::GetUVs(TArray<FVector2f>& UVs) const
{
	if(const UObject* Mesh = GetMesh())
	{
		if (const USkeletalMesh* SkeletalMesh = Cast<const USkeletalMesh>(Mesh))
		{
			UVs = GetUV(*SkeletalMesh, LOD, Material, UVChannel);
		}
		else if (const UStaticMesh* StaticMesh = Cast<const UStaticMesh>(Mesh))
		{
			UVs = GetUV(*StaticMesh, LOD, Material, UVChannel);
		}
	}
}


int32 UCustomizableObjectLayout::FindBlock(const FGuid& InId) const
{
	for (int32 Index = 0; Index < Blocks.Num(); ++Index)
	{
		if (Blocks[Index].Id == InId)
		{
			return Index;
		}
	}

	return -1;
}


void UCustomizableObjectLayout::SetIgnoreVertexLayoutWarnings(bool bValue)
{
	bIgnoreUnassignedVertexWarning = bValue;
}


void UCustomizableObjectLayout::SetIgnoreWarningsLOD(int32 LODValue)
{
	FirstLODToIgnore = LODValue;
}


UObject* UCustomizableObjectLayout::GetMesh() const
{
	if (UObject* Node = GetOuter())
	{
		if (const UCustomizableObjectNodeSkeletalMesh* TypedNodeSkeletalMesh = Cast<UCustomizableObjectNodeSkeletalMesh>(Node))
		{
			return TypedNodeSkeletalMesh->GetMesh();
		}
		else if (const UCustomizableObjectNodeTable* TypedNodeTable = Cast<UCustomizableObjectNodeTable>(Node))
		{
			return TypedNodeTable->GetDefaultMeshForLayout(this);
		}
	}

	return nullptr;
}


#undef LOCTEXT_NAMESPACE
