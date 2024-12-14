// Copyright Epic Games, Inc. All Rights Reserved.

#include "Glyph.h"
#include "Materials/Material.h"
#include "Engine/StaticMesh.h"

FText3DGlyph::FText3DGlyph() :
	StaticMeshAttributes(MeshDescription)
{
	StaticMeshAttributes.Register();
	Groups.SetNum(static_cast<int32>(EText3DGroupType::TypeCount));
	MeshDescription.ReserveNewPolygonGroups(Groups.Num());

	for (int32 Index = 0; Index < Groups.Num(); Index++)
	{
		MeshDescription.CreatePolygonGroup();
	}
}

void FText3DGlyph::Build(UStaticMesh* StaticMesh, UMaterial* DefaultMaterial)
{
	check(StaticMesh);

	auto AddMaterial = [](UStaticMesh* InMesh, UMaterial* InMaterial, int32 InIndex)
	{
		using namespace UE::Text3D::Materials;

		check(SlotNames.IsValidIndex(InIndex))

		FName MaterialName = SlotNames[InIndex];

#if WITH_EDITORONLY_DATA
		FStaticMaterial& StaticMaterial = InMesh->GetStaticMaterials().Emplace_GetRef(InMaterial, MaterialName, MaterialName);
#else
		FStaticMaterial& StaticMaterial = InMesh->GetStaticMaterials().Emplace_GetRef(InMaterial, MaterialName);
#endif

		StaticMaterial.UVChannelData = FMeshUVChannelInfo(1.0f);

		return MaterialName;
	};

	for (int32 Index = 0; Index < Groups.Num(); Index++)
	{
		const FPolygonGroupID PolyGroup(Index);
		if (MeshDescription.GetNumPolygonGroupTriangles(PolyGroup) != 0)
		{
			StaticMeshAttributes.GetPolygonGroupMaterialSlotNames()[PolyGroup] = AddMaterial(StaticMesh, DefaultMaterial, Index);
		}
	}

	TArray<const FMeshDescription*> MeshDescriptions;
	MeshDescriptions.Add(&MeshDescription);

	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bCommitMeshDescription = true;
	Params.bFastBuild = true;
	Params.bAllowCpuAccess = true;
	StaticMesh->BuildFromMeshDescriptions(MeshDescriptions, Params);
}

FMeshDescription& FText3DGlyph::GetMeshDescription()
{
	return MeshDescription;
}

FStaticMeshAttributes& FText3DGlyph::GetStaticMeshAttributes()
{
	return StaticMeshAttributes;
}

TText3DGroupList& FText3DGlyph::GetGroups()
{
	return Groups;
}
