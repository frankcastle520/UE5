// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/GenerateMutableSource/GenerateMutableSourceLayout.h"

#include "MuCOE/GenerateMutableSource/GenerateMutableSourceMesh.h"
#include "MuCOE/CustomizableObjectCompiler.h"
#include "MuCOE/CustomizableObjectLayout.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/Nodes/CustomizableObjectNodeLayoutBlocks.h"
#include "MuCOE/Nodes/CustomizableObjectNodeSkeletalMesh.h"
#include "Engine/StaticMesh.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"

mu::Ptr<mu::NodeLayout> CreateDefaultLayout()
{
	constexpr int32 GridSize = 4;

	mu::Ptr<mu::NodeLayout> LayoutNode = new mu::NodeLayout();
	LayoutNode->Size = { GridSize, GridSize };
	LayoutNode->MaxSize = { GridSize, GridSize };
	LayoutNode->Strategy = mu::EPackStrategy::Resizeable;
	LayoutNode->ReductionMethod = mu::EReductionMethod::Halve;
	LayoutNode->Blocks.SetNum(1);
	LayoutNode->Blocks[0].Min = { 0, 0 };
	LayoutNode->Blocks[0].Size = { GridSize, GridSize };
	LayoutNode->Blocks[0].Priority = 0;
	LayoutNode->Blocks[0].bReduceBothAxes = false;
	LayoutNode->Blocks[0].bReduceByTwo = false;

	return LayoutNode;
}


mu::Ptr<mu::NodeLayout> CreateMutableLayoutNode(FMutableGraphGenerationContext& GenerationContext, const UCustomizableObjectLayout* UnrealLayout, bool bIgnoreLayoutWarnings, bool& bWasEmpty )
{
	bWasEmpty = false;
	mu::Ptr<mu::NodeLayout> LayoutNode = new mu::NodeLayout;

	LayoutNode->Size = FIntVector2(UnrealLayout->GetGridSize().X, UnrealLayout->GetGridSize().Y);
	LayoutNode->MaxSize = FIntVector2(UnrealLayout->GetMaxGridSize().X, UnrealLayout->GetMaxGridSize().Y);

	mu::EPackStrategy PackStrategy = ConvertLayoutStrategy(UnrealLayout->PackingStrategy);
	LayoutNode->Strategy = PackStrategy;

	LayoutNode->ReductionMethod = (UnrealLayout->BlockReductionMethod == ECustomizableObjectLayoutBlockReductionMethod::Halve ? mu::EReductionMethod::Halve : mu::EReductionMethod::Unitary);

	if (bIgnoreLayoutWarnings)
	{
		// Layout warnings can be safely ignored in this case. Vertices that do not belong to any layout block will be removed (Extend Materials only)
		LayoutNode->FirstLODToIgnoreWarnings = 0;
	}
	else
	{
		LayoutNode->FirstLODToIgnoreWarnings = UnrealLayout->GetIgnoreVertexLayoutWarnings() ? UnrealLayout->GetFirstLODToIgnoreWarnings() : -1;
	}

	LayoutNode->Blocks.SetNum(UnrealLayout->Blocks.Num());
	for (int32 BlockIndex = 0; BlockIndex < UnrealLayout->Blocks.Num(); ++BlockIndex)
	{
		LayoutNode->Blocks[BlockIndex] = ToMutable(GenerationContext, UnrealLayout->Blocks[BlockIndex]);
	}


	ECustomizableObjectLayoutAutomaticBlocksStrategy AutomaticBlockStrategy = UnrealLayout->AutomaticBlocksStrategy;
	ECustomizableObjectTextureLayoutPackingStrategy PackingStrategy = UnrealLayout->PackingStrategy;
	
	if (AutomaticBlockStrategy == ECustomizableObjectLayoutAutomaticBlocksStrategy::Ignore || 
		PackingStrategy == ECustomizableObjectTextureLayoutPackingStrategy::Overlay)
 	{
		// Legacy behavior
		if (UnrealLayout->Blocks.IsEmpty())
		{
			bWasEmpty = true;
			LayoutNode->Blocks.SetNum(1);
			LayoutNode->Blocks[0].Min = { 0,0 };
			LayoutNode->Blocks[0].Size = LayoutNode->Size;
			LayoutNode->Blocks[0].Priority = 0;
			LayoutNode->Blocks[0].bReduceBothAxes = false;
			LayoutNode->Blocks[0].bReduceByTwo = false;
		}
	}
	else
	{
		// Convert the UE mesh in the layout into a Mutable mesh.
		mu::Ptr<mu::Mesh> MutableMesh = nullptr;

		if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(UnrealLayout->GetMesh()))
		{
			// We don't need all the data to generate the blocks
			const EMutableMeshConversionFlags ShapeFlags =
				EMutableMeshConversionFlags::IgnoreSkinning |
				EMutableMeshConversionFlags::IgnorePhysics;

			GenerationContext.MeshGenerationFlags.Push(ShapeFlags);

			MutableMesh = ConvertSkeletalMeshToMutable(SkeletalMesh, TSoftClassPtr<UAnimInstance>(), UnrealLayout->GetLOD(), UnrealLayout->GetMaterial(), UnrealLayout->GetLOD(), UnrealLayout->GetMaterial(), 0, GenerationContext, nullptr, nullptr);

			GenerationContext.MeshGenerationFlags.Pop();
		}
		else if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(UnrealLayout->GetMesh()))
		{
			MutableMesh = ConvertStaticMeshToMutable(StaticMesh, UnrealLayout->GetLOD(), UnrealLayout->GetMaterial(), GenerationContext, nullptr);
		}

		if (MutableMesh)
		{
			// Generating blocks with the mutable mesh
			if (AutomaticBlockStrategy == ECustomizableObjectLayoutAutomaticBlocksStrategy::Rectangles)
			{
				LayoutNode->GenerateLayoutBlocks(MutableMesh, UnrealLayout->GetUVChannel());
			}
			else if (AutomaticBlockStrategy == ECustomizableObjectLayoutAutomaticBlocksStrategy::UVIslands)
			{
				bool bMergeChildBlocks = UnrealLayout->AutomaticBlocksMergeStrategy == ECustomizableObjectLayoutAutomaticBlocksMergeStrategy::MergeChildBlocks;
				LayoutNode->GenerateLayoutBlocksFromUVIslands(MutableMesh, UnrealLayout->GetUVChannel(), bMergeChildBlocks);
			}
			else
			{
				// Unimplemented
				check(false);
			}
		}
	}

	return LayoutNode;
}


mu::FSourceLayoutBlock ToMutable(FMutableGraphGenerationContext& GenerationContext, const FCustomizableObjectLayoutBlock& UnrealBlock)
{
	mu::FSourceLayoutBlock MutableBlock;

	MutableBlock.Min = { uint16(UnrealBlock.Min.X), uint16(UnrealBlock.Min.Y) };
	FIntPoint Size = UnrealBlock.Max - UnrealBlock.Min;
	MutableBlock.Size = { uint16(Size.X), uint16(Size.Y) };

	MutableBlock.Priority = UnrealBlock.Priority;
	MutableBlock.bReduceBothAxes = UnrealBlock.bReduceBothAxes;
	MutableBlock.bReduceByTwo = UnrealBlock.bReduceByTwo;

	if (UnrealBlock.Mask)
	{
		GenerationContext.AddParticipatingObject(*UnrealBlock.Mask);

		// In the editor the src data can be directly accessed
		mu::Ptr<mu::Image> MaskImage = new mu::Image();

		FMutableSourceTextureData Tex(*UnrealBlock.Mask);
		EUnrealToMutableConversionError Error = ConvertTextureUnrealSourceToMutable(MaskImage.get(), Tex, 0);
		if (Error != EUnrealToMutableConversionError::Success)
		{
			// This should never happen, so details are not necessary.
			UE_LOG(LogMutable, Warning, TEXT("Failed to convert layout block mask texture."));
		}
		else
		{
			MutableBlock.Mask = MaskImage;
		}
	}

	return MutableBlock;
}


#undef LOCTEXT_NAMESPACE

