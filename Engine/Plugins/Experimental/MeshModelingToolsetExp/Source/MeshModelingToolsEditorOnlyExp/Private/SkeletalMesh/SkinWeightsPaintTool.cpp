// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMesh/SkinWeightsPaintTool.h"

#include "AssetViewerSettings.h"

#include "Math/UnrealMathUtility.h"
#include "Async/Async.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalDebugRendering.h"
#include "AnimationRuntime.h"
#include "Animation/MirrorDataTable.h"

#include "Editor/Persona/Public/IPersonaEditorModeManager.h"
#include "Editor/Persona/Public/PersonaModule.h"
#include "EditorViewportClient.h"
#include "Preferences/PersonaOptions.h"
#include "PreviewProfileController.h"
#include "BaseGizmos/BrushStampIndicator.h"

#include "InteractiveToolManager.h"
#include "ToolSetupUtil.h"
#include "ToolTargetManager.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ModelingToolTargetUtil.h"
#include "Selection/PolygonSelectionMechanic.h"
#include "ContextObjectStore.h"

#include "MeshDescription.h"
#include "MeshModelingToolsEditorOnlyExp.h"
#include "DynamicMeshToMeshDescription.h"
#include "DynamicSubmesh3.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "DynamicMesh/MeshAdapterUtil.h"
#include "DynamicMesh/NonManifoldMappingSupport.h"
#include "TargetInterfaces/DynamicMeshCommitter.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "SkeletalMeshAttributes.h"

#include "Operations/SmoothBoneWeights.h"
#include "Operations/TransferBoneWeights.h"

#include "Parameterization/MeshDijkstra.h"
#include "Spatial/PointHashGrid3.h"
#include "Parameterization/MeshLocalParam.h"
#include "Spatial/FastWinding.h"

#include "Widgets/Notifications/SNotificationList.h"
#include "HAL/PlatformApplicationMisc.h"

#include "Serialization/JsonSerializable.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SkinWeightsPaintTool)

#define LOCTEXT_NAMESPACE "USkinWeightsPaintTool"

using namespace SkinPaintTool;

static void ShowEditorMessage(ELogVerbosity::Type InMessageType, const FText& InMessage)
{
	FNotificationInfo Notification(InMessage);
	Notification.bUseSuccessFailIcons = true;
	Notification.ExpireDuration = 5.0f;

	SNotificationItem::ECompletionState State = SNotificationItem::CS_Success;

	switch(InMessageType)
	{
	case ELogVerbosity::Warning:
		UE_LOG(LogMeshModelingToolsEditor, Warning, TEXT("%s"), *InMessage.ToString());
		break;
	case ELogVerbosity::Error:
		State = SNotificationItem::CS_Fail;
		UE_LOG(LogMeshModelingToolsEditor, Error, TEXT("%s"), *InMessage.ToString());
		break;
	default:
		break; // don't log anything unless a warning or error
	}
	
	FSlateNotificationManager::Get().AddNotification(Notification)->SetCompletionState(State);
}

namespace SkinPaintTool
{
	
EMeshLODIdentifier GetLODId(const FName InLODName)
{
	static const TMap<FName, EMeshLODIdentifier> LODs({
		{"LOD0", EMeshLODIdentifier::LOD0},
		{"LOD1", EMeshLODIdentifier::LOD1},
		{"LOD2", EMeshLODIdentifier::LOD2},
		{"LOD3", EMeshLODIdentifier::LOD3},
		{"LOD4", EMeshLODIdentifier::LOD4},
		{"LOD5", EMeshLODIdentifier::LOD5},
		{"LOD6", EMeshLODIdentifier::LOD6},
		{"LOD7", EMeshLODIdentifier::LOD7},
		{"HiResSource", EMeshLODIdentifier::HiResSource},
		{"Default", EMeshLODIdentifier::Default},
		{"MaxQuality", EMeshLODIdentifier::MaxQuality}
	});

	const EMeshLODIdentifier* LODIdFound = LODs.Find(InLODName);
	return LODIdFound ? *LODIdFound : EMeshLODIdentifier::Default;
}

FName GetLODName(const EMeshLODIdentifier InLOD)
{
	static const TMap<EMeshLODIdentifier, FName> LODs({
		{EMeshLODIdentifier::LOD0, "LOD0"},
		{EMeshLODIdentifier::LOD1, "LOD1"},
		{EMeshLODIdentifier::LOD2, "LOD2"},
		{EMeshLODIdentifier::LOD3, "LOD3"},
		{EMeshLODIdentifier::LOD4, "LOD4"},
		{EMeshLODIdentifier::LOD5, "LOD5"},
		{EMeshLODIdentifier::LOD6, "LOD6"},
		{EMeshLODIdentifier::LOD7, "LOD7"},
		{EMeshLODIdentifier::HiResSource, "HiResSource"},
		{EMeshLODIdentifier::Default, "Default"},
		{EMeshLODIdentifier::MaxQuality, "MaxQuality"}
	});

	const FName* Name = LODs.Find(InLOD);
	return Name ? *Name : NAME_None;
}

USkeletalMeshComponent* GetSkeletalMeshComponent(const UToolTarget* InTarget)
{
	if (!ensure(InTarget))
	{
		return nullptr;
	}
	
	const IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(InTarget);
	if (!ensure(TargetComponent))
	{
		return nullptr;
	}
	
	USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(TargetComponent->GetOwnerComponent());
	if (!(Component && Component->GetSkeletalMeshAsset()))
	{
		return nullptr;
	}

	return Component;
}

const FName& CreateNewName()
{
	static const FName CreateNew("Create New...");
	return CreateNew;
}

FSkinWeightsVertexAttributesRef GetOrCreateSkinWeightsAttribute(FMeshDescription& InMesh, const FName InProfileName)
{
	FSkeletalMeshAttributes MeshAttribs(InMesh);
	const TArray<FName> Profiles = MeshAttribs.GetSkinWeightProfileNames();
	if (!Profiles.Contains(InProfileName))
	{
		ensure( MeshAttribs.RegisterSkinWeightAttribute(InProfileName) );
	}
	return MeshAttribs.GetVertexSkinWeights(InProfileName);
}

bool RenameSkinWeightsAttribute(FMeshDescription& InMesh, const FName InOldName, const FName InNewName)
{
	FSkeletalMeshAttributes MeshAttribs(InMesh);
	const TArray<FName> Profiles = MeshAttribs.GetSkinWeightProfileNames();
	if (Profiles.Contains(InOldName))
	{
		FSkinWeightsVertexAttributesRef NewWeightsAttr = GetOrCreateSkinWeightsAttribute(InMesh, InNewName);
		NewWeightsAttr = MeshAttribs.GetVertexSkinWeights(InOldName);
		return MeshAttribs.UnregisterSkinWeightAttribute(InOldName);
	}
	return false;
}
	
}

// thread pool to use for async operations
static EAsyncExecution SkinPaintToolAsyncExecTarget = EAsyncExecution::ThreadPool;

// any weight below this value is ignored, since it won't be representable in unsigned 16-bit precision
constexpr float MinimumWeightThreshold = 1.0f / 65535.0f;

class FPaintToolWeightsDataSource : public UE::Geometry::TBoneWeightsDataSource<int32, float>
{
public:

	FPaintToolWeightsDataSource(const FSkinToolWeights* InWeights, const FDynamicMesh3& InDynaMesh)
		: Weights(InWeights)
		, NonManifoldMappingSupport(InDynaMesh)
	{
		checkSlow(Weights);
	}

	virtual ~FPaintToolWeightsDataSource() = default;

	virtual int32 GetBoneNum(const int32 VertexID) override
	{
		const int32 SrcVertexId = GetSourceVertexId(VertexID);
		return Weights->PreChangeWeights[SrcVertexId].Num();
	}

	virtual int32 GetBoneIndex(const int32 VertexID, const int32 Index) override
	{
		const int32 SrcVertexId = GetSourceVertexId(VertexID);
		return Weights->PreChangeWeights[SrcVertexId][Index].BoneID;
	}

	virtual float GetBoneWeight(const int32 VertexID, const int32 Index) override
	{
		const int32 SrcVertexId = GetSourceVertexId(VertexID);
		return Weights->PreChangeWeights[SrcVertexId][Index].Weight;
	}

	virtual float GetWeightOfBoneOnVertex(const int32 VertexID, const int32 BoneIndex) override
	{
		const int32 SrcVertexId = GetSourceVertexId(VertexID);
		return Weights->GetWeightOfBoneOnVertex(BoneIndex, SrcVertexId, Weights->PreChangeWeights);
	}

protected:

	int32 GetSourceVertexId(const int32 InVertexID) const
	{
		return NonManifoldMappingSupport.GetOriginalNonManifoldVertexID(InVertexID);
	}
	
	const FSkinToolWeights* Weights = nullptr;
	const UE::Geometry::FNonManifoldMappingSupport NonManifoldMappingSupport;
};

void FDirectEditWeightState::Reset()
{
	bInTransaction = false;
	StartValue = CurrentValue = GetModeDefaultValue();
}

float FDirectEditWeightState::GetModeDefaultValue()
{
	static TMap<EWeightEditOperation, float> DefaultModeValues = {
		{EWeightEditOperation::Add, 0.0f},
		{EWeightEditOperation::Replace, .0f},
		{EWeightEditOperation::Multiply, 1.f},
		{EWeightEditOperation::Relax, 0.f}
	};

	return DefaultModeValues[EditMode];
}

float FDirectEditWeightState::GetModeMinValue()
{
	static TMap<EWeightEditOperation, float> MinModeValues = {
		{EWeightEditOperation::Add, -1.f},
		{EWeightEditOperation::Replace, 0.f},
		{EWeightEditOperation::Multiply, 0.f},
		{EWeightEditOperation::Relax, 0.f}
	};

	return MinModeValues[EditMode];
}

float FDirectEditWeightState::GetModeMaxValue()
{
	static TMap<EWeightEditOperation, float> MaxModeValues = {
		{EWeightEditOperation::Add, 1.f},
		{EWeightEditOperation::Replace, 1.f},
		{EWeightEditOperation::Multiply, 2.f},
		{EWeightEditOperation::Relax, 10.f}
	};

	return MaxModeValues[EditMode];
}

USkinWeightsPaintToolProperties::USkinWeightsPaintToolProperties()
{
	BrushConfigs.Add(EWeightEditOperation::Add, &BrushConfigAdd);
	BrushConfigs.Add(EWeightEditOperation::Replace, &BrushConfigReplace);
	BrushConfigs.Add(EWeightEditOperation::Multiply, &BrushConfigMultiply);
	BrushConfigs.Add(EWeightEditOperation::Relax, &BrushConfigRelax);

	LoadConfig();

	if (ColorRamp.IsEmpty())
	{
		// default color ramp simulates a heat map
		ColorRamp.Add(FLinearColor(0.8f, 0.4f, 0.8f)); // Purple
		ColorRamp.Add(FLinearColor(0.0f, 0.0f, 0.5f)); // Dark Blue
		ColorRamp.Add(FLinearColor(0.2f, 0.2f, 1.0f)); // Light Blue
		ColorRamp.Add(FLinearColor(0.0f, 1.0f, 0.0f)); // Green
		ColorRamp.Add(FLinearColor(1.0f, 1.0f, 0.0f)); // Yellow
		ColorRamp.Add(FLinearColor(1.0f, 0.65f, 0.0f)); // Orange
		ColorRamp.Add(FLinearColor(1.0f, 0.0f, 0.0f, 0.0f)); // Red
	}
}

namespace SkinWeightLayer
{

TArray<FName> GetLODs(UToolTarget* InTarget)
{
    static TArray<FName> Dummy;

    if (!InTarget)
    {
    	return Dummy;
    }

    bool bOutSupportsLODs = false;
	constexpr bool bOnlyReturnDefaultLOD = false;
	// NOTE: currently auto-generated LODs do not have USkeletalMesh::SourceModels and so the returned MeshDescription will be null
	// for this reason, we do not allow transferring weights to/from auto-generated LODs
	constexpr bool bExcludeAutoGeneratedLODs = true;
    const TArray<EMeshLODIdentifier> LODIDSs = UE::ToolTarget::GetMeshDescriptionLODs(InTarget, bOutSupportsLODs, bOnlyReturnDefaultLOD, bExcludeAutoGeneratedLODs);
    if (!ensure(bOutSupportsLODs))
    {
    	return Dummy;
    }

    TArray<FName> LODs;
    LODs.Reserve(LODIDSs.Num());
    for (const EMeshLODIdentifier LODId: LODIDSs)
    {
    	const FName LODName = GetLODName(LODId);
    	if (LODName != NAME_None)
    	{
    		LODs.Add(LODName);
    	}
    }
    ensure(!LODs.IsEmpty());
    
    return LODs;
}

TArray<FName> GetProfilesFromToolTarget(UToolTarget* InTarget, const FName InLOD)
{
	if (InTarget)
	{
		TArray<FName> Profiles;
		const EMeshLODIdentifier LODId = GetLODId(InLOD);
		const FGetMeshParameters Params(true, LODId);
		if (const FMeshDescription* MeshDescription = UE::ToolTarget::GetMeshDescription(InTarget, Params))
		{
			const FSkeletalMeshConstAttributes MeshAttribs(*MeshDescription);
			return MeshAttribs.GetSkinWeightProfileNames();
		}
	}
	
	static const TArray EmptyProfiles = {FSkeletalMeshAttributesShared::DefaultSkinWeightProfileName, CreateNewName()};	
	return EmptyProfiles;
};

}

TArray<FName> USkinWeightsPaintToolProperties::GetTargetLODsFunc() const
{
	return SkinWeightLayer::GetLODs(WeightTool->GetTarget());
}

TArray<FName> USkinWeightsPaintToolProperties::GetTargetSkinWeightProfilesFunc() const
{
	TArray<FName> Profiles = SkinWeightLayer::GetProfilesFromToolTarget(WeightTool->GetTarget(), SourceLOD);
	Profiles.Add(CreateNewName());
	return Profiles;
}

TArray<FName> USkinWeightsPaintToolProperties::GetSourceLODsFunc() const
{
	return SkinWeightLayer::GetLODs(WeightTool->GetWeightTransferManager()->GetTarget());
}

TArray<FName> USkinWeightsPaintToolProperties::GetSourceSkinWeightProfilesFunc() const
{
	UToolTarget* SourceToolTarget = WeightTool->GetWeightTransferManager()->GetTarget();
	return SkinWeightLayer::GetProfilesFromToolTarget(SourceToolTarget, SourceLOD);
}

void UWeightToolMeshSelector::InitialSetup(
	UWorld* InWorld,
	UInteractiveTool* InParentTool,
	FEditorViewportClient* InViewportClient,
	TFunction<void()> OnSelectionChangedFunc)
{
	World = InWorld;

	ViewportClient = InViewportClient;
	
	// set up vertex selection mechanic
	PolygonSelectionMechanic = NewObject<UPolygonSelectionMechanic>(this);
	PolygonSelectionMechanic->bAddSelectionFilterPropertiesToParentTool = false;
	PolygonSelectionMechanic->Setup(InParentTool);
	PolygonSelectionMechanic->SetIsEnabled(false, false);
	PolygonSelectionMechanic->OnSelectionChanged.AddLambda(OnSelectionChangedFunc );

	// set up style of vertex selection
	constexpr FLinearColor VertexSelectedPurple = FLinearColor(0.78f, 0.f, 0.78f);
	constexpr FLinearColor VertexSelectedYellow = FLinearColor(1.f,1.f,0.f);
	// adjust selection rendering for this context
	PolygonSelectionMechanic->HilightRenderer.PointColor = FLinearColor::Blue;
	PolygonSelectionMechanic->HilightRenderer.PointSize = 10.0f;
	// vertex highlighting once selected
	PolygonSelectionMechanic->SelectionRenderer.LineThickness = 1.0f;
	PolygonSelectionMechanic->SelectionRenderer.PointColor = VertexSelectedYellow;
	PolygonSelectionMechanic->SelectionRenderer.PointSize = 5.0f;
	PolygonSelectionMechanic->SelectionRenderer.DepthBias = 2.0f;
	// despite the name, this renders the vertices
	PolygonSelectionMechanic->PolyEdgesRenderer.PointColor = VertexSelectedPurple;
	PolygonSelectionMechanic->PolyEdgesRenderer.PointSize = 5.0f;
	PolygonSelectionMechanic->PolyEdgesRenderer.DepthBias = 2.0f;
	PolygonSelectionMechanic->PolyEdgesRenderer.LineThickness = 1.0f;
}

void UWeightToolMeshSelector::SetMesh(
	UPreviewMesh* InPreviewMesh,
	const FMeshDescription* InMeshDescription,
	const FTransform3d& InMeshTransform)
{
	// store the mesh this is operating on
	PreviewMesh = InPreviewMesh;
	// copy the mesh description for selection validation
	MeshDescription = *InMeshDescription;

	if (!ensure(World))
	{
		return;
	}

	if (!PreviewMesh)
	{
		SetIsEnabled(false);
		return;
	}

	// reset selection topology and mesh spatial data
	static constexpr bool bAutoBuild = true;
	const FDynamicMesh3* DynamicMesh = PreviewMesh->GetMesh();
	SelectionTopology = MakeUnique<UE::Geometry::FTriangleGroupTopology>(DynamicMesh, bAutoBuild);
	MeshSpatial = MakeUnique<FDynamicMeshAABBTree3>(DynamicMesh, bAutoBuild);

	// initialize the selection mechanic
	PolygonSelectionMechanic->Initialize(
		DynamicMesh,
		InMeshTransform,
		World,
		SelectionTopology.Get(),
		[this]() { return MeshSpatial.Get(); }
	);

	// clear the selection (old selection is invalid on new topo)
	PolygonSelectionMechanic->ClearSelection();
	PolygonSelectionMechanic->ClearHighlight();

	// selection colors
	constexpr FLinearColor FaceSelectedOrange = FLinearColor(0.886f, 0.672f, 0.473f);
	// configure secondary render material for selected triangles
	// NOTE: the material returned by ToolSetupUtil::GetSelectionMaterial has a checkerboard pattern on back faces which makes it hard to use
	if (UMaterialInterface* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolsetExp/Materials/SculptMaterial")))
	{
		if (UMaterialInstanceDynamic* MatInstance = UMaterialInstanceDynamic::Create(Material, World))
		{
			MatInstance->SetVectorParameterValue(TEXT("Color"), FaceSelectedOrange);
			PreviewMesh->SetSecondaryRenderMaterial(MatInstance);
		}
	}

	// secondary triangle buffer used to render face selection
	PreviewMesh->EnableSecondaryTriangleBuffers([this](const FDynamicMesh3* Mesh, int32 TriangleID)
	{
		return PolygonSelectionMechanic->GetActiveSelection().IsSelectedTriangle(Mesh, SelectionTopology.Get(), TriangleID);
	});
	// notify preview mesh when triangle selection has been updated
	PolygonSelectionMechanic->OnSelectionChanged.AddWeakLambda(this, [this]()
	{
		PreviewMesh->FastNotifySecondaryTrianglesChanged();
	});
	PolygonSelectionMechanic->OnFaceSelectionPreviewChanged.AddWeakLambda(this, [this]()
	{
		PreviewMesh->FastNotifySecondaryTrianglesChanged();
	});
}

void UWeightToolMeshSelector::Shutdown()
{
	if (PolygonSelectionMechanic)
	{
		PolygonSelectionMechanic->Shutdown();
	}
	
	PolygonSelectionMechanic = nullptr;
}

void UWeightToolMeshSelector::SetIsEnabled(bool bIsEnabled)
{
	if (!PolygonSelectionMechanic)
	{
		return;
	}

	// force off if there's no preview mesh
	bIsEnabled = PreviewMesh ? bIsEnabled : false; 
	
	PolygonSelectionMechanic->SetIsEnabled(bIsEnabled, bIsEnabled);
}

void UWeightToolMeshSelector::SetComponentSelectionMode(EComponentSelectionMode InMode)
{
	if (!(PolygonSelectionMechanic))
	{
		return;
	}
	
	PolygonSelectionMechanic->Properties->bSelectVertices = InMode == EComponentSelectionMode::Vertices;
	PolygonSelectionMechanic->Properties->bSelectEdges = InMode == EComponentSelectionMode::Edges;
	PolygonSelectionMechanic->Properties->bSelectFaces = InMode == EComponentSelectionMode::Faces;
	PolygonSelectionMechanic->SetShowSelectableCorners(InMode == EComponentSelectionMode::Vertices);
	PolygonSelectionMechanic->SetShowEdges(InMode == EComponentSelectionMode::Edges);
}

void UWeightToolMeshSelector::SetTransform(const FTransform3d& InTargetTransform)
{
	if (PolygonSelectionMechanic)
	{
		PolygonSelectionMechanic->SetTransform(InTargetTransform);
	}
}

void UWeightToolMeshSelector::UpdateAfterMeshDeformation()
{
	MeshSpatial->Build();
	constexpr bool bTopologyDeformed = true;
	constexpr bool bTopologyModified = false;
	PolygonSelectionMechanic->GetTopologySelector()->Invalidate(bTopologyDeformed, bTopologyModified);
}

void UWeightToolMeshSelector::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	if (!PolygonSelectionMechanic)
	{
		return;
	}

	PolygonSelectionMechanic->DrawHUD(Canvas, RenderAPI);
}
	
void UWeightToolMeshSelector::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (!(PolygonSelectionMechanic && PreviewMesh))
	{
		return;
	}
	
	PolygonSelectionMechanic->Render(RenderAPI);
}

const TArray<int32>& UWeightToolMeshSelector::GetSelectedVertices()
{
	using UE::Geometry::FNonManifoldMappingSupport;
	
	SelectedVerticesInternal.Empty();
	if (!(PolygonSelectionMechanic && PreviewMesh))
	{
		return SelectedVerticesInternal;
	}
	
	const FGroupTopologySelection& Selection = PolygonSelectionMechanic->GetActiveSelection();
	const FDynamicMesh3* DynamicMesh = PreviewMesh->GetMesh();
	const FVertexArray& AllVertices = MeshDescription.Vertices();
	const FNonManifoldMappingSupport NonManifoldMappingSupport(*DynamicMesh);

	// validate and add vertices to the output array
	auto AddVertices = [this, &AllVertices, &NonManifoldMappingSupport](const TSet<int32>& VerticesToAdd)
	{
		// we have to make sure that the vertex ids are safe to use as PolygonSelectionMechanic does not act on the
		// mesh description but on the dynamic mesh that can duplicate vertices when dealing with degenerate triangles.
		// cf. FMeshDescriptionToDynamicMesh::Convert for more details.
		for (const int32 VertexToAdd : VerticesToAdd)
		{
			const int32 SrcVertexID = NonManifoldMappingSupport.GetOriginalNonManifoldVertexID(VertexToAdd);
			if (AllVertices.IsValid(SrcVertexID))
			{
				SelectedVerticesInternal.Add(SrcVertexID);
			}
		}
	};

	// add selected vertices
	AddVertices(Selection.SelectedCornerIDs);

	// add vertices on selected edges
	{
		TSet<int32> VerticesInSelectedEdges;
		for (const int32 SelectedEdgeIndex : Selection.SelectedEdgeIDs)
		{
			FDynamicMesh3::FEdge CurrentEdge = DynamicMesh->GetEdge(SelectedEdgeIndex);
			VerticesInSelectedEdges.Add(CurrentEdge.Vert.A);
			VerticesInSelectedEdges.Add(CurrentEdge.Vert.B);
		}
		
		AddVertices(VerticesInSelectedEdges);
	}

	// add vertices in selected faces
	{
		TSet<int32> VerticesInSelectedFaces;
		for (const int32 SelectedFaceIndex : Selection.SelectedGroupIDs)
		{
			UE::Geometry::FIndex3i TriangleVertices = DynamicMesh->GetTriangleRef(SelectedFaceIndex);
			VerticesInSelectedFaces.Add(TriangleVertices[0]);
			VerticesInSelectedFaces.Add(TriangleVertices[1]);
			VerticesInSelectedFaces.Add(TriangleVertices[2]);
		}
		
		AddVertices(VerticesInSelectedFaces);
	}
	
	return SelectedVerticesInternal;
}

bool UWeightToolMeshSelector::IsAnyComponentSelected() const
{
	if (!PolygonSelectionMechanic)
	{
		return false;
	}

	return PolygonSelectionMechanic->HasSelection();
}

void UWeightToolMeshSelector::GetSelectedTriangles(TArray<int32>& OutTriangleIndices) const
{
	OutTriangleIndices.Empty();
	if (!ensure(PolygonSelectionMechanic))
	{
		return;
	}
	
	const FGroupTopologySelection& Selection = PolygonSelectionMechanic->GetActiveSelection();
	const FDynamicMesh3* DynamicMesh = PreviewMesh->GetMesh();
	TSet<int32> TriangleSet;

	// add triangles connected to selected vertices
	for (const int32 VertexIndex : Selection.SelectedCornerIDs)
	{
		DynamicMesh->EnumerateVertexTriangles(VertexIndex, [&TriangleSet](int32 TriangleIndex)
		{
			TriangleSet.Add(TriangleIndex);
		});
	}

	// add triangles connected to selected edges
	for (const int32 EdgeIndex : Selection.SelectedEdgeIDs)
	{
		DynamicMesh->EnumerateEdgeTriangles(EdgeIndex, [&TriangleSet](int32 TriangleIndex)
		{
			TriangleSet.Add(TriangleIndex);
		});
	}

	// add selected triangles
	TriangleSet.Append(Selection.SelectedGroupIDs);	

	OutTriangleIndices = TriangleSet.Array();
}

void UWeightToolMeshSelector::GrowSelection() const
{
	if (!ensure(PolygonSelectionMechanic))
	{
		return;
	}
	
	PolygonSelectionMechanic->GrowSelection(/*bAsTriangleTopology*/ true);
}

void UWeightToolMeshSelector::ShrinkSelection() const
{
	if (!ensure(PolygonSelectionMechanic))
	{
		return;
	}
	
	PolygonSelectionMechanic->ShrinkSelection(/*bAsTriangleTopology*/ true);
}

void UWeightToolMeshSelector::FloodSelection() const
{
	if (!ensure(PolygonSelectionMechanic))
	{
		return;
	}
	
	PolygonSelectionMechanic->FloodSelection();
}

void UWeightToolMeshSelector::SelectBorder() const
{
	if (!ensure(PolygonSelectionMechanic))
	{
		return;
	}
	
	PolygonSelectionMechanic->ConvertSelectionToBorderVertices(/*bAsTriangleTopology*/ true);
}

void UWeightToolTransferManager::InitialSetup(USkinWeightsPaintTool* InWeightTool, FEditorViewportClient* InViewportClient)
{
	WeightTool = InWeightTool;

	// always reset back to target selection
	WeightTool->GetWeightToolProperties()->MeshSelectMode = EMeshTransferOption::Target;

	// create the mesh selector and run initial setup
	// NOTE: currently this must happen inside the Setup of a UInteractiveTool so that input is routed to the selection mechanic
	MeshSelector = NewObject<UWeightToolMeshSelector>(this);
	auto DoNothingOnSelection = [](){};
	MeshSelector->InitialSetup(WeightTool->GetTargetWorld(), WeightTool.Get(), InViewportClient,  DoNothingOnSelection);
}

void UWeightToolTransferManager::SetSourceMesh(USkeletalMesh* InSkeletalMesh)
{
	SourceSkeletalMesh = InSkeletalMesh;

	USkinWeightsPaintToolProperties* ToolProperties = WeightTool->GetWeightToolProperties();
	
	// reset to prepare for new mesh (or possibly no mesh)
	{
		if (SourcePreviewMesh)
		{
			SourcePreviewMesh->SetVisible(false);
			SourcePreviewMesh->Disconnect();
			SourcePreviewMesh = nullptr;
		}

		if (SourceTarget)
		{
			SourceTarget = nullptr;
		}

		if (MeshSelector)
		{
			MeshSelector->SetIsEnabled(false);
		}
	}
	
	if (!InSkeletalMesh)
	{
		ToolProperties->SourceSkinWeightProfile = FSkeletalMeshAttributesShared::DefaultSkinWeightProfileName;
		ToolProperties->SourceLOD = "LOD0";
		WeightTool->UpdateSelectorState();
		return;
	}
	
	// create the preview mesh (this creates the skeletal mesh component in the world)
	SourcePreviewMesh = NewObject<UPreviewMesh>(this);
	SourcePreviewMesh->CreateInWorld(WeightTool->GetTargetWorld(), FTransform::Identity);

	// create a new tool target for this mesh
	SourceTarget = WeightTool->GetTargetManager()->BuildTarget(InSkeletalMesh, FToolTargetTypeRequirements());
	
	// move source mesh beside the main mesh (to the left in screen space)
	{
		UToolTarget* MainTarget = WeightTool->GetTarget();
		if (!MainTarget)
		{
			return;
		}

		const USkeletalMeshComponent* SkeletalMeshComponent = GetSkeletalMeshComponent(MainTarget);
		if (!SkeletalMeshComponent)
		{
			return;
		}
		
		const FBoxSphereBounds TargetBounds = SkeletalMeshComponent->GetSkeletalMeshAsset()->GetBounds();
		const FBoxSphereBounds SourceBounds = InSkeletalMesh->GetBounds();
		FTransform Transform = UE::ToolTarget::GetLocalToWorldTransform(MainTarget);
		FVector Location = Transform.GetLocation();
		Location.X -= TargetBounds.GetBoxExtrema(1).X;
		Location.X -= 1.1 * SourceBounds.GetBoxExtrema(1).X;
		Transform.SetLocation(Location);
		WeightTool->GetWeightToolProperties()->SourcePreviewOffset = Transform;
		SourcePreviewMesh->SetTransform(Transform);
	}

	// replace the dynamic mesh contained in the preview mesh
	ToolSetupUtil::ApplyRenderingConfigurationToPreview(SourcePreviewMesh, SourceTarget);
	SourcePreviewMesh->SetTangentsMode(EDynamicMeshComponentTangentsMode::AutoCalculated);
	const EMeshLODIdentifier SourceLODId = GetLODId(ToolProperties->SourceLOD);
	const FGetMeshParameters SourceParams(true, SourceLODId);
	SourcePreviewMesh->ReplaceMesh(UE::ToolTarget::GetDynamicMeshCopy(SourceTarget, SourceParams));

	// setup materials and visibility of the preview mesh
	const FComponentMaterialSet MaterialSet = UE::ToolTarget::GetMaterialSet(SourceTarget);
	SourcePreviewMesh->SetMaterials(MaterialSet.Materials);

	// setup the mesh selection for the source
	const FMeshDescription* MeshDescription =  UE::ToolTarget::GetMeshDescription(WeightTool->GetTarget(), SourceParams);
	MeshSelector->SetMesh(SourcePreviewMesh, MeshDescription, ToolProperties->SourcePreviewOffset);
	MeshSelector->SetIsEnabled(ToolProperties->EditingMode == EWeightEditMode::Mesh);
	MeshSelector->SetComponentSelectionMode(ToolProperties->ComponentSelectionMode);

	WeightTool->UpdateSelectorState();
}

void UWeightToolTransferManager::Shutdown()
{
	SetSourceMesh(nullptr);
	
	if (MeshSelector)
	{
		MeshSelector->Shutdown();
	}

	MeshSelector = nullptr;
	SourcePreviewMesh = nullptr;
	SourceTarget = nullptr;
}

void UWeightToolTransferManager::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (MeshSelector)
	{
		MeshSelector->Render(RenderAPI);	
	}
}

void UWeightToolTransferManager::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	if (MeshSelector)
	{
		MeshSelector->DrawHUD(Canvas, RenderAPI);
	}
}

void UWeightToolTransferManager::TransferWeights()
{
	if (!ensure(SourceTarget))
	{
		// to transfer weights from another mesh we need a source mesh
		// (UI should prevent us from getting here)
		const FText NotificationText = LOCTEXT("NoSourceTarget", "No source skeletal mesh specified. No weights were transferred.");
		ShowEditorMessage(ELogVerbosity::Error, NotificationText);
		return;
	}

	USkinWeightsPaintToolProperties* ToolProperties = WeightTool->GetWeightToolProperties();

	const USkeletalMesh* TargetSkeletalMesh = GetSkeletalMeshComponent(WeightTool->GetTarget())->GetSkeletalMeshAsset();
	const bool bSameMesh = SourceSkeletalMesh == TargetSkeletalMesh;
	const bool bSameLOD = GetLODId(ToolProperties->ActiveLOD) == GetLODId(ToolProperties->SourceLOD);
	const bool bSameProfile = ToolProperties->ActiveSkinWeightProfile == ToolProperties->SourceSkinWeightProfile;
	const bool bHasSelectedVertices = ToolProperties->EditingMode == EWeightEditMode::Mesh && !WeightTool->GetMainMeshSelector()->GetSelectedVertices().IsEmpty();
	
	// cannot transfer between same mesh/LOD/profile without selection (identical weights)
	if (bSameMesh && bSameLOD && bSameProfile && !bHasSelectedVertices)
	{
		const FText NotificationText = LOCTEXT("IdenticalSourceAndTarget", "Cannot copy weights from the same mesh, LOD and profile without anything selected. No weights were transferred.");
		ShowEditorMessage(ELogVerbosity::Error, NotificationText);
		return;
	}

	// can weights be transferred copying the attribute directly?
	if (bSameMesh && bSameLOD && !bSameProfile)
	{
		TransferWeightsFromSameMeshAndLOD();
	}
	else
	{
		TransferWeightsFromOtherMeshOrSubset();
	}
}

void UWeightToolTransferManager::TransferWeightsFromOtherMeshOrSubset()
{
	using UE::Geometry::FDynamicMeshAttributeSet;
	using UE::Geometry::FDynamicMeshVertexSkinWeightsAttribute;
	using UE::Geometry::FTransferBoneWeights;
	using UE::AnimationCore::FBoneWeights;

	if (!ensure(SourceTarget))
	{
		return;
	}

	USkinWeightsPaintToolProperties* ToolProperties = WeightTool->GetWeightToolProperties();

	// get LOD IDs
	const EMeshLODIdentifier TargetLODId = GetLODId(ToolProperties->ActiveLOD);
	const EMeshLODIdentifier SourceLODId = GetLODId(ToolProperties->SourceLOD);

	// get selection
	TArray<int32> SourceTrianglesToIsolate;
	MeshSelector->GetSelectedTriangles(SourceTrianglesToIsolate);
	
	TArray<int> TargetSelectedVertices = WeightTool->GetMainMeshSelector()->GetSelectedVertices();
	
	// if transferring between the same mesh, ensure that the LODs are different and that the transfer is done on a subset
	const USkeletalMesh* TargetSkeletalMesh = GetSkeletalMeshComponent(WeightTool->GetTarget())->GetSkeletalMeshAsset();
	const bool bSameMeshAndLOD = SourceSkeletalMesh == TargetSkeletalMesh && TargetLODId == SourceLODId;
	if (bSameMeshAndLOD)
	{
		if (SourceTrianglesToIsolate.IsEmpty() && TargetSelectedVertices.IsEmpty())
		{
			const FText NotificationText = LOCTEXT("SameLODAndNoSelection", "Cannot copy weights between the same LOD on the same mesh without anything selected. No weights were transferred.");
			ShowEditorMessage(ELogVerbosity::Error, NotificationText);
			return;
		}
	}

	// get target dynamic mesh
	const FGetMeshParameters TargetParams(true, TargetLODId);
	FDynamicMesh3 TargetMesh = UE::ToolTarget::GetDynamicMeshCopy(WeightTool->GetTarget(), TargetParams);
	
	// get the source dynamic mesh and validate it
	FDynamicMesh3 TmpSourceCopy;
	auto GetSourceMesh = [this, bSameMeshAndLOD, SourceLODId, &TmpSourceCopy](const TArray<int32>& ToIsolate) -> const FDynamicMesh3*
	{
		// use the current EditedMesh to get the current data without having to commit
		FCleanedEditMesh* CleanedEditMesh = WeightTool->GetCurrentCleanedEditMesh();
		if (const FDynamicMesh3* EditedMesh = bSameMeshAndLOD && CleanedEditMesh ? &CleanedEditMesh->GetOriginalMesh() : nullptr)
		{
			if (ToIsolate.IsEmpty())
			{
				return EditedMesh;
			}

			// create a sub-mesh from the selected triangles to filter the transfer so that it only copies from the selected components
			const UE::Geometry::FDynamicSubmesh3 PartialSubMesh(EditedMesh, ToIsolate);
			TmpSourceCopy = PartialSubMesh.GetSubmesh();			
			FDynamicMeshAttributeSet* SourceAttributes = TmpSourceCopy.Attributes();
			SourceAttributes->CopyBoneAttributes(*EditedMesh->Attributes());
			return &TmpSourceCopy;
		}

		// otherwise, get the corresponding DynamicMesh for that LOD
		const FGetMeshParameters SourceParams(true, SourceLODId);
		FDynamicMesh3 SourceMeshOrig = UE::ToolTarget::GetDynamicMeshCopy(SourceTarget, SourceParams);
		
		// create a sub-mesh from the selected triangles to filter the transfer so that it only copies from the selected components
		if (!ToIsolate.IsEmpty())
		{
			const UE::Geometry::FDynamicSubmesh3 PartialSubMesh(&SourceMeshOrig, ToIsolate);
			TmpSourceCopy = PartialSubMesh.GetSubmesh();
			FDynamicMeshAttributeSet* SourceAttributes = TmpSourceCopy.Attributes();
			SourceAttributes->CopyBoneAttributes(*SourceMeshOrig.Attributes());
		}
		else
		{
			TmpSourceCopy = MoveTemp(SourceMeshOrig);
		}
		return &TmpSourceCopy;
	};
	
	const FDynamicMesh3* SourceMesh = GetSourceMesh(SourceTrianglesToIsolate);
	if (!ensure(SourceMesh))
	{
		const FText NotificationText = LOCTEXT("NoSourceMesh", "Cannot retrieve any source mesh form the current properties.");
		ShowEditorMessage(ELogVerbosity::Error, NotificationText);
		return;
	}
	
	if (!SourceMesh->HasAttributes() || !SourceMesh->Attributes()->HasBones())
	{
		const FText NotificationText = LOCTEXT("NoWeightsFoundInTransfer", "No skin weights were found in the source skeletal mesh. No weights were transferred.");
		ShowEditorMessage(ELogVerbosity::Error, NotificationText);
		return;
	}
	if (SourceMesh->Attributes()->GetNumBones() == 0)
	{
		const FText NotificationText = LOCTEXT("NoBonesFoundInTransfer", "No bones were found in the source skeletal mesh. No weights were transferred.");
		ShowEditorMessage(ELogVerbosity::Error, NotificationText);
		return;
	}

	FTransferBoneWeights TransferBoneWeights(SourceMesh, ToolProperties->SourceSkinWeightProfile);
	TransferBoneWeights.TransferMethod = FTransferBoneWeights::ETransferBoneWeightsMethod::InpaintWeights;
	
	if (!TargetMesh.HasAttributes())
	{
		TargetMesh.EnableAttributes();
	}

	FDynamicMeshAttributeSet* TargetAttributes = TargetMesh.Attributes();
	if (!TargetAttributes->HasBones())
	{
		TargetAttributes->CopyBoneAttributes(*SourceMesh->Attributes());
	}
	else
	{
		const FReferenceSkeleton& TargetRefSkeleton = TargetSkeletalMesh->GetRefSkeleton();
		if (!ensure(TargetAttributes->GetNumBones() == TargetRefSkeleton.GetRawBoneNum()))
		{
			const FText NotificationText = LOCTEXT("TransferWeightsInvalidBoneCount", "Target mesh bone attributes mismatch. No weights were transferred.");
			ShowEditorMessage(ELogVerbosity::Error, NotificationText);
			return;
		}
	}

	// NOTE should we expose all the options?
	// 	TransferBoneWeights.NormalThreshold;
	// 	TransferBoneWeights.SearchRadius
	// 	TransferBoneWeights.NumSmoothingIterations;
	// 	TransferBoneWeights.SmoothingStrength;
	// 	TransferBoneWeights.LayeredMeshSupport;
	// 	TransferBoneWeights.ForceInpaintWeightMapName;
	
	if (TransferBoneWeights.Validate() != UE::Geometry::EOperationValidationResult::Ok)
	{
		const FText NotificationText = LOCTEXT("TransferWeightsNotValid", "Transfer weights operation unable to validate meshes. No weights were transferred.");
		ShowEditorMessage(ELogVerbosity::Error, NotificationText);
		return;
	}

	if (ToolProperties->EditingMode == EWeightEditMode::Mesh)
	{
		TransferBoneWeights.TargetVerticesSubset = MoveTemp(TargetSelectedVertices);
	}

	const FName TargetProfile = ToolProperties->GetActiveSkinWeightProfile();
	if (!TransferBoneWeights.TransferWeightsToMesh(TargetMesh, TargetProfile))
	{
		const FText NotificationText = LOCTEXT("TransferWeightsUnknownIssue", "Transfer weights operation encountered an unknown issue. No weights were transferred.");
		ShowEditorMessage(ELogVerbosity::Error, NotificationText);
		return;
	}

	FDynamicMeshVertexSkinWeightsAttribute* TransferredSkinWeights = nullptr;
	TransferredSkinWeights = TargetAttributes->GetSkinWeightsAttribute(TargetProfile);
	
	TArray<int32> VertexSubset = MoveTemp(TransferBoneWeights.TargetVerticesSubset);

	// apply the weight changes as a transaction
	ApplyTranferredWeightsAsTransaction(TransferredSkinWeights, VertexSubset, TargetMesh);
}

void UWeightToolTransferManager::TransferWeightsFromSameMeshAndLOD()
{
	if (!ensure(SourceTarget))
	{
		return;
	}
	
	using UE::Geometry::FDynamicMeshAttributeSet;
	using UE::Geometry::FDynamicMeshVertexSkinWeightsAttribute;
	using UE::AnimationCore::FBoneWeights;
	
	const USkinWeightsPaintToolProperties* ToolProperties = WeightTool->GetWeightToolProperties();
	
	// get target dynamic mesh
	const EMeshLODIdentifier TargetLODId = GetLODId(ToolProperties->ActiveLOD);
	const FName TargetProfile = ToolProperties->GetActiveSkinWeightProfile();
	const FGetMeshParameters TargetParams(true, TargetLODId);
	FDynamicMesh3 TargetMesh = UE::ToolTarget::GetDynamicMeshCopy(WeightTool->GetTarget(), TargetParams);
	
	const FDynamicMeshAttributeSet* TargetAttributes = TargetMesh.Attributes();
	FDynamicMeshVertexSkinWeightsAttribute* TransferredSkinWeights = TargetAttributes->GetSkinWeightsAttribute(TargetProfile);
	if (!TransferredSkinWeights)
	{
		TransferredSkinWeights = new FDynamicMeshVertexSkinWeightsAttribute(&TargetMesh);
		TargetMesh.Attributes()->AttachSkinWeightsAttribute(TargetProfile, TransferredSkinWeights);
	}
		
	const FDynamicMeshVertexSkinWeightsAttribute* SourceAttributes = TargetAttributes->GetSkinWeightsAttribute(ToolProperties->SourceSkinWeightProfile);
	check(SourceAttributes != nullptr);

	if (!ensure(SourceAttributes == TransferredSkinWeights))
	{
		const FText NotificationText = LOCTEXT("TransferBetweenSame", "Mesh attribute mismatch. No weights were transferred.");
		ShowEditorMessage(ELogVerbosity::Error, NotificationText);
		return;
	}

	TArray<int32> VertexSubset;
	if (ToolProperties->EditingMode == EWeightEditMode::Mesh)
	{
		VertexSubset = MeshSelector->GetSelectedVertices();
	}

	if (VertexSubset.IsEmpty())
	{
		// copy weights for ALL vertices in the mesh
		TransferredSkinWeights->Copy(*SourceAttributes);
	}
	else
	{
		// copy vertex weights for a subset of vertices
		for (const int32 VertexID: VertexSubset)
		{
			FBoneWeights BoneWeights;
			SourceAttributes->GetValue(VertexID, BoneWeights);
			TransferredSkinWeights->SetValue(VertexID, BoneWeights);
		}
	}

	// apply the weight changes as a transaction
	ApplyTranferredWeightsAsTransaction(TransferredSkinWeights, VertexSubset, TargetMesh);
}

bool UWeightToolTransferManager::CanTransferWeights() const
{
	return SourceTarget && WeightTool->GetTarget();
}

void UWeightToolTransferManager::OnPropertyModified(const USkinWeightsPaintToolProperties* WeightToolProperties, const FProperty* ModifiedProperty)
{
	if (ModifiedProperty->GetName() == GET_MEMBER_NAME_STRING_CHECKED(USkinWeightsPaintToolProperties, SourceSkeletalMesh))
	{
		SetSourceMesh(WeightToolProperties->SourceSkeletalMesh.Get());
	}

	if (ModifiedProperty->GetName() == GET_MEMBER_NAME_STRING_CHECKED(USkinWeightsPaintToolProperties, SourceLOD))
	{
		if (SourcePreviewMesh)
		{
			// reapply mesh (will use the new LOD)
			SetSourceMesh(SourceSkeletalMesh);
		}
	}

	if (ModifiedProperty->GetName() == GET_MEMBER_NAME_STRING_CHECKED(USkinWeightsPaintToolProperties, SourcePreviewOffset))
	{
		if (SourcePreviewMesh)
		{
			SourcePreviewMesh->SetTransform(WeightToolProperties->SourcePreviewOffset);
			MeshSelector->SetTransform(WeightToolProperties->SourcePreviewOffset);
		}
	}
}

void UWeightToolTransferManager::ApplyTranferredWeightsAsTransaction(
	const UE::Geometry::FDynamicMeshVertexSkinWeightsAttribute* TransferredSkinWeights,
	const TArray<int32>& VertexSubset,
	FDynamicMesh3& TargetMesh)
{
	check(TransferredSkinWeights);
	
	// weight edits for transaction
	FMultiBoneWeightEdits WeightEdits;

	// get the weight data (used for making edits)
	FSkinToolWeights& Weights = WeightTool->GetWeights();
	
	// spin through all the transferred skin weights and record a weight edit to apply as a transaction
	static constexpr float ZeroWeight = 0.f;
	const bool bUseSubset = !VertexSubset.IsEmpty();
	const int32 NumVertices = bUseSubset ? VertexSubset.Num() : TargetMesh.VertexCount();
	const UE::Geometry::FNonManifoldMappingSupport NonManifoldMappingSupport(TargetMesh);
	for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
	{
		const int32 VertexID = bUseSubset ? VertexSubset[VertexIndex] : VertexIndex;
		const int32 SrcVertexID = NonManifoldMappingSupport.GetOriginalNonManifoldVertexID(VertexID);
		
		// remove all weight on vertex
		const VertexWeights& VertexBoneWeights = Weights.PreChangeWeights[SrcVertexID];
		if (!VertexBoneWeights.IsEmpty())
		{
			for (const FVertexBoneWeight& BoneWeight : VertexBoneWeights)
			{
				// when transferring weights we do prune influences because we would prefer the results to be identical to the source
				constexpr bool bPruneInfluence = true;
				WeightEdits.MergeSingleEdit(BoneWeight.BoneID, SrcVertexID, ZeroWeight, bPruneInfluence, Weights.PreChangeWeights);
			}
		}
		else
		{
			// in the unlikely event that the target vertex has no weight, "fake" remove it from the root so that undo will put it back there
			constexpr bool bPruneInfluence = false;
			constexpr BoneIndex RootBoneIndex = 0;
			WeightEdits.MergeSingleEdit(RootBoneIndex, SrcVertexID, ZeroWeight, bPruneInfluence, Weights.PreChangeWeights);
		}

		// update with new weight
		UE::AnimationCore::FBoneWeights TransferedBoneWeights;
		TransferredSkinWeights->GetValue(VertexID, TransferedBoneWeights);
		for (const UE::AnimationCore::FBoneWeight& BoneWeight: TransferedBoneWeights)
		{
			const int32 BoneIndex = BoneWeight.GetBoneIndex();					
			const float NewWeight = BoneWeight.GetWeight();
			constexpr bool bPruneInfluence = false;
			WeightEdits.MergeSingleEdit(BoneIndex, SrcVertexID, NewWeight, bPruneInfluence, Weights.PreChangeWeights);
		}
	}

	// apply the changes as a transaction
	const FText TransactionLabel = LOCTEXT("TransferWeightsChange", "Transfer skin weights.");
	WeightTool->ApplyWeightEditsAsTransaction(WeightEdits, TransactionLabel);
	
	// put the mesh back in it's current pose
	Weights.Deformer.SetAllVerticesToBeUpdated();

	// notify user that weights were transferred.
	const FText NotificationText = LOCTEXT("WeightsTransferred", "Skin weights transferred.");
	ShowEditorMessage(ELogVerbosity::Log, NotificationText);
}

FName USkinWeightsPaintToolProperties::GetActiveSkinWeightProfile() const
{
	return bShowNewProfileName ? NewSkinWeightProfile : ActiveSkinWeightProfile;
}

FSkinWeightBrushConfig& USkinWeightsPaintToolProperties::GetBrushConfig()
{
	return *BrushConfigs[BrushMode];
}

void FMultiBoneWeightEdits::MergeSingleEdit(
	const int32 BoneIndex,
	const int32 VertexID,
	const float NewWeight,
	bool bPruneInfluence,
	const TArray<VertexWeights>& InPreChangeWeights)
{
	if (!ensure(BoneIndex!=INDEX_NONE))
	{
		return;
	}
	
	if (bPruneInfluence)
	{
		// should never be pruning an influence while also trying to add weight to it
		if (!ensure(FMath::IsNearlyEqual(NewWeight, 0.f)))
		{
			return;
		}
	}

	// get the old weight of this influence and check whether it was already influences this vertex
	float OldWeight = 0.f;
	bool bWasAlreadyAnInfluence = false;
	const VertexWeights& VertexWeights = InPreChangeWeights[VertexID];
	for (const FVertexBoneWeight& BoneWeight : VertexWeights)
	{
		if (BoneWeight.BoneID == BoneIndex)
		{
			OldWeight = BoneWeight.Weight;
			bWasAlreadyAnInfluence = true;
			break;
		}
	}

	// don't prune influence unless they are actually connected to the vertex
	if (bPruneInfluence && !bWasAlreadyAnInfluence)
	{
		bPruneInfluence = false;
	}

	// record a modification of this vertex weight
	FSingleBoneWeightEdits& BoneWeightEdit = PerBoneWeightEdits.FindOrAdd(BoneIndex);
	BoneWeightEdit.BoneIndex = BoneIndex;
	BoneWeightEdit.NewWeights.Add(VertexID, NewWeight);
	BoneWeightEdit.OldWeights.FindOrAdd(VertexID, OldWeight);

	if (bPruneInfluence)
	{
		// record that this influence was REMOVED from this vertex
		BoneWeightEdit.VerticesRemovedFrom.AddUnique(VertexID);
		// remove from the AddedTo list in the off chance that vertex was added by a prior edit (last edit takes precedence)
		BoneWeightEdit.VerticesAddedTo.Remove(VertexID);
	}
	else if (!bWasAlreadyAnInfluence && NewWeight > MinimumWeightThreshold)
	{
		// record that this influence was ADDED to this vertex
		BoneWeightEdit.VerticesAddedTo.AddUnique(VertexID);
		// remove from the pruned list in the off chance that vertex was pruned by a prior edit (last edit takes precedence)
		BoneWeightEdit.VerticesRemovedFrom.Remove(VertexID);
	}
}

void FMultiBoneWeightEdits::MergeEdits(const FSingleBoneWeightEdits& BoneWeightEdits)
{
	ensure(BoneWeightEdits.BoneIndex!=INDEX_NONE);
	
	// make sure bone has an entry in the map of weight edits
	const int32 BoneIndex = BoneWeightEdits.BoneIndex;
	PerBoneWeightEdits.FindOrAdd(BoneIndex);
	PerBoneWeightEdits[BoneIndex].BoneIndex = BoneIndex;
	
	for (const TTuple<int32, float>& NewWeight : BoneWeightEdits.NewWeights)
	{
		int32 VertexIndex = NewWeight.Key;
		PerBoneWeightEdits[BoneIndex].NewWeights.Add(VertexIndex, NewWeight.Value);
		PerBoneWeightEdits[BoneIndex].OldWeights.FindOrAdd(VertexIndex, BoneWeightEdits.OldWeights[VertexIndex]);
	}
}

float FMultiBoneWeightEdits::GetVertexDeltaFromEdits(const int32 BoneIndex, const int32 VertexIndex)
{
	PerBoneWeightEdits.FindOrAdd(BoneIndex);
	if (const float* NewVertexWeight = PerBoneWeightEdits[BoneIndex].NewWeights.Find(VertexIndex))
	{
		return *NewVertexWeight - PerBoneWeightEdits[BoneIndex].OldWeights[VertexIndex];
	}

	return 0.0f;
}

void FMultiBoneWeightEdits::AddEditedVerticesToSet(TSet<int32>& OutEditedVertexSet) const
{
	TSet<int32> VerticesInEdit;
	for (const TTuple<int32, FSingleBoneWeightEdits>& Pair : PerBoneWeightEdits)
	{
		Pair.Value.NewWeights.GetKeys(VerticesInEdit);
		OutEditedVertexSet.Append(VerticesInEdit);
	}
}

void FSkinToolDeformer::Initialize(const USkeletalMeshComponent* InSkelMeshComponent, const FMeshDescription* InMeshDescription)
{
	// get all bone transforms in the reference pose store a copy in component space
	Component = InSkelMeshComponent;
	const FReferenceSkeleton& RefSkeleton = Component->GetSkeletalMeshAsset()->GetRefSkeleton();
	const TArray<FTransform> &LocalSpaceBoneTransforms = RefSkeleton.GetRefBonePose();
	const int32 NumBones = LocalSpaceBoneTransforms.Num();
	InvCSRefPoseTransforms.SetNumUninitialized(NumBones);
	for (int32 BoneIndex=0; BoneIndex<NumBones; ++BoneIndex)
	{
		const int32 ParentBoneIndex = RefSkeleton.GetParentIndex(BoneIndex);
		const FTransform& LocalTransform = LocalSpaceBoneTransforms[BoneIndex];
		if (ParentBoneIndex != INDEX_NONE)
		{
			InvCSRefPoseTransforms[BoneIndex] = LocalTransform * InvCSRefPoseTransforms[ParentBoneIndex];
		}
		else
		{
			InvCSRefPoseTransforms[BoneIndex] = LocalTransform;
		}
	}
	
	for (int32 BoneIndex=0; BoneIndex<NumBones; ++BoneIndex)
	{
		// pre-invert the transforms so we don't have to at runtime
		InvCSRefPoseTransforms[BoneIndex] = InvCSRefPoseTransforms[BoneIndex].Inverse();

		// store map of bone indices to bone names
		FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
		BoneNames.Add(BoneName);
		BoneNameToIndexMap.Add(BoneName, BoneIndex);
	}

	// store reference pose vertex positions
	const TArrayView<const FVector3f> VertexPositions = InMeshDescription->GetVertexPositions().GetRawArray();
	RefPoseVertexPositions = VertexPositions;

	// set all vertices to be updated on first tick
	SetAllVerticesToBeUpdated();

	// record "prev" bone transforms to detect change in pose
	PreviousPoseComponentSpace = Component->GetComponentSpaceTransforms();
}

void FSkinToolDeformer::SetAllVerticesToBeUpdated()
{
	VerticesWithModifiedWeights.Empty(RefPoseVertexPositions.Num());
	for (int32 VertexID=0; VertexID<RefPoseVertexPositions.Num(); ++VertexID)
	{
		VerticesWithModifiedWeights.Add(VertexID);
	}
}

void FSkinToolDeformer::SetToRefPose(USkinWeightsPaintTool* Tool)
{
	// get ref pose
	const FReferenceSkeleton& RefSkeleton = Component->GetSkeletalMeshAsset()->GetRefSkeleton();
	const TArray<FTransform>& RefPoseLocalSpace = RefSkeleton.GetRefBonePose();
	// convert to global space and store in current pose
	FAnimationRuntime::FillUpComponentSpaceTransforms(RefSkeleton, RefPoseLocalSpace, RefPoseComponentSpace);
	// update mesh to new pose
	UpdateVertexDeformation(Tool, RefPoseComponentSpace);
}

void FSkinToolDeformer::UpdateVertexDeformation(
	USkinWeightsPaintTool* Tool,
	const TArray<FTransform>& PoseComponentSpace)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::UpdateDeformationTotal);

	// if no weights have been modified, we must check for a modified pose which requires re-calculation of skinning
	if (VerticesWithModifiedWeights.IsEmpty())
	{
		for (int32 BoneIndex=0; BoneIndex<PoseComponentSpace.Num(); ++BoneIndex)
		{
			if (!Tool->Weights.IsBoneWeighted[BoneIndex])
			{
				continue;
			}
			
			const FTransform& CurrentBoneTransform = PoseComponentSpace[BoneIndex];
			const FTransform& PrevBoneTransform = PreviousPoseComponentSpace[BoneIndex];
			if (!CurrentBoneTransform.Equals(PrevBoneTransform))
			{
				SetAllVerticesToBeUpdated();
				break;
			}
		}
	}

	if (VerticesWithModifiedWeights.IsEmpty())
	{
		return;
	}
	
	// update vertex positions
	UPreviewMesh* PreviewMesh = Tool->PreviewMesh;
	const TArray<VertexWeights>& CurrentWeights = Tool->Weights.CurrentWeights;
	PreviewMesh->DeferredEditMesh([this, &CurrentWeights, &PoseComponentSpace](FDynamicMesh3& Mesh)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::UpdateDeformation);
		const TArray<int32> VertexIndices = VerticesWithModifiedWeights.Array();
		
		ParallelFor( VerticesWithModifiedWeights.Num(), [this, &VertexIndices, &Mesh, &PoseComponentSpace, &CurrentWeights](int32 Index)
		{
			const int32 VertexID = VertexIndices[Index];
			FVector VertexNewPosition = FVector::ZeroVector;
			const VertexWeights& VertexPerBoneData = CurrentWeights[VertexID];
			for (const FVertexBoneWeight& VertexData : VertexPerBoneData)
			{
				if (!ensure(VertexData.BoneID != INDEX_NONE))
				{
					continue;
				}
				const FTransform& CurrentTransform = PoseComponentSpace[VertexData.BoneID];
				VertexNewPosition += CurrentTransform.TransformPosition(VertexData.VertexInBoneSpace) * VertexData.Weight;
			}
			
			Mesh.SetVertex(VertexID, VertexNewPosition, false);
		});
	}, false);
	PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate, EMeshRenderAttributeFlags::Positions, false);

	// what mode are we in?
	const EWeightEditMode EditingMode = Tool->WeightToolProperties->EditingMode;
	
	// update data structures used by the brush mode	
	if (EditingMode == EWeightEditMode::Brush)
	{
		// update vertex acceleration structure
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::UpdateVertexOctree);
			Tool->VerticesOctree->RemoveVertices(VerticesWithModifiedWeights);
			Tool->VerticesOctree->InsertVertices(VerticesWithModifiedWeights);
		}
		
		// update triangle acceleration structure
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::UpdateTriangleOctree);

			// create list of triangles that were affected by the vertices that were deformed
			TArray<int32>& AffectedTriangles = Tool->TrianglesToReinsert; // reusable buffer of triangles to update
			{
				AffectedTriangles.Reset();

				// reinsert all triangles containing an updated vertex
				const FDynamicMesh3* DynamicMesh = PreviewMesh->GetMesh();
				for (const int32 TriangleID : DynamicMesh->TriangleIndicesItr())
				{
					UE::Geometry::FIndex3i TriVerts = DynamicMesh->GetTriangle(TriangleID);
					bool bIsTriangleAffected = VerticesWithModifiedWeights.Contains(TriVerts[0]);
					bIsTriangleAffected = VerticesWithModifiedWeights.Contains(TriVerts[1]) ? true : bIsTriangleAffected;
					bIsTriangleAffected = VerticesWithModifiedWeights.Contains(TriVerts[2]) ? true : bIsTriangleAffected;
					if (bIsTriangleAffected)
					{
						AffectedTriangles.Add(TriangleID);
					}
				}
			}

			// ensure previous async update is finished before queuing the next one...
			Tool->TriangleOctreeFuture.Wait();
		
			// asynchronously update the octree, this normally finishes well before the next update
			// but in the unlikely event that it does not, it would result in a frame where the paint brush
			// is not perfectly aligned with the mesh; not a deal breaker.
			UE::Geometry::FDynamicMeshOctree3& OctreeToUpdate = *Tool->TrianglesOctree;
			Tool->TriangleOctreeFuture = Async(SkinPaintToolAsyncExecTarget, [&OctreeToUpdate, &AffectedTriangles]()
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::TriangleOctreeReinsert);	
				OctreeToUpdate.ReinsertTriangles(AffectedTriangles);
			});
		}
	}

	// update data structures used by the selection mode
	if (EditingMode == EWeightEditMode::Mesh)
	{
		// update AABB Tree for vertex selection
		TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::UpdateAABBTree);
		Tool->MeshSelector->UpdateAfterMeshDeformation();
	}

	// empty queue of vertices to update
	VerticesWithModifiedWeights.Reset();

	// record the skeleton state we used to update the deformations
	PreviousPoseComponentSpace = PoseComponentSpace;
}

void FSkinToolDeformer::SetVertexNeedsUpdated(int32 VertexIndex)
{
	VerticesWithModifiedWeights.Add(VertexIndex);
}

void FSkinToolWeights::InitializeSkinWeights(
	const USkeletalMeshComponent* SkeletalMeshComponent,
	const FMeshDescription* Mesh)
{
	static constexpr int32 RootBoneIndex = 0;
	static constexpr float FullWeight = 1.f;

	// initialize deformer data
	Deformer.Initialize(SkeletalMeshComponent, Mesh);

	// initialize current weights (using compact format: num_verts * max_influences)
	const FSkeletalMeshConstAttributes MeshAttribs(*Mesh);
	const FSkinWeightsVertexAttributesConstRef VertexSkinWeights = MeshAttribs.GetVertexSkinWeights(Profile);
	const int32 NumVertices = Mesh->Vertices().Num();
	TMap<BoneIndex, float> NormalizedWeights;
	CurrentWeights.SetNum(NumVertices);
	for (int32 VertexIndex = 0; VertexIndex < NumVertices; VertexIndex++)
	{
		// we have to normalize here because there are edges cases where skeletal meshes are loaded with non-normalized weights.
		// even when/if we fix these, it's best to be 100% sure that things are normalized since weight data can be generated by tools we don't control
		NormalizedWeights.Reset();
		int32 NumInfluences = 0;
		const FVertexBoneWeightsConst BoneWeights = VertexSkinWeights.Get(FVertexID(VertexIndex));
		for (const UE::AnimationCore::FBoneWeight BoneWeight: BoneWeights)
		{
			if (!ensure(NumInfluences < MAX_TOTAL_INFLUENCES))
			{
				// cut-off weights if they exceed max influences (this shouldn't happen)
				break;
			}
			NormalizedWeights.Add(BoneWeight.GetBoneIndex(), BoneWeight.GetWeight());
			++NumInfluences;
		}
		USkinWeightsPaintTool::NormalizeWeightMap(NormalizedWeights);
		
		// if there are no bone weights, default to root bone 
		if (NumInfluences == 0)
		{
			const FVector& RefPoseVertexPosition = Deformer.RefPoseVertexPositions[VertexIndex];
			const FTransform& InvRefPoseTransform = Deformer.InvCSRefPoseTransforms[RootBoneIndex];
			const FVector& BoneLocalPositionInRefPose = InvRefPoseTransform.TransformPosition(RefPoseVertexPosition);
			CurrentWeights[VertexIndex].Emplace(RootBoneIndex, BoneLocalPositionInRefPose, FullWeight);
			continue;
		}

		// load into the main weights data structure
		for (TTuple<BoneIndex, float>& SingleBoneWeight : NormalizedWeights)
		{
			BoneIndex BoneIndex = SingleBoneWeight.Key;
			const float Weight = SingleBoneWeight.Value;
			if (!ensure(Deformer.InvCSRefPoseTransforms.IsValidIndex(BoneIndex)))
			{
				UE_LOG(LogMeshModelingToolsEditor, Warning, TEXT("InitializeSkinWeights: Invalid bone index provided (%d); falling back to 0 (root) as bone index."), BoneIndex);
				BoneIndex = 0;
			}
			const FVector& RefPoseVertexPosition = Deformer.RefPoseVertexPositions[VertexIndex];
			const FTransform& InvRefPoseTransform = Deformer.InvCSRefPoseTransforms[BoneIndex];
			const FVector& BoneLocalPositionInRefPose = InvRefPoseTransform.TransformPosition(RefPoseVertexPosition);
			CurrentWeights[VertexIndex].Emplace(BoneIndex, BoneLocalPositionInRefPose, Weight);
		}
	}
	
	// maintain duplicate weight map
	PreChangeWeights = CurrentWeights;

	// maintain relax-per stroke map
	MaxFalloffPerVertexThisStroke.SetNumZeroed(NumVertices);

	// maintain bool-per-bone if weighted or not
	IsBoneWeighted.Init(false, Deformer.BoneNames.Num());
	for (const VertexWeights& VertexData : CurrentWeights)
	{
		for (const FVertexBoneWeight& VertexBoneData : VertexData)
		{
			if (VertexBoneData.Weight > UE::AnimationCore::BoneWeightThreshold)
			{
				IsBoneWeighted[VertexBoneData.BoneID] = true;
			}
		}
	}
}

void FSkinToolWeights::CreateWeightEditForVertex(
	const int32 BoneToHoldIndex,
	const int32 VertexID,
	float NewWeightValue,
	FMultiBoneWeightEdits& WeightEdits)
{
	// this operation should never prune weights
	constexpr bool bPruneInfluence = false;
	
	// clamp new weight
	NewWeightValue = FMath::Clamp(NewWeightValue, 0.0f, 1.0f);

	// calculate the sum of all the weights on this vertex (not including the one we currently applied)
	TArray<int32> RecordedBonesOnVertex;
	TArray<float> ValuesToNormalize;
	float Total = 0.0f;
	const VertexWeights& VertexData = PreChangeWeights[VertexID];
	for (const FVertexBoneWeight& VertexBoneData : VertexData)
	{
		if (VertexBoneData.BoneID == BoneToHoldIndex)
		{
			continue;
		}

		if (!ensure(VertexBoneData.BoneID != INDEX_NONE))
		{
			continue;
		}
		
		RecordedBonesOnVertex.Add(VertexBoneData.BoneID);
		ValuesToNormalize.Add(VertexBoneData.Weight);
		Total += VertexBoneData.Weight;
	}

	// assigning full weight to this vertex?
	if (FMath::IsNearlyEqual(NewWeightValue, 1.f))
	{
		// in this case normalization is trivial, just assign the full weight directly and zero all others
		constexpr float FullWeight = 1.0f;
		WeightEdits.MergeSingleEdit(BoneToHoldIndex, VertexID, FullWeight, bPruneInfluence, PreChangeWeights);

		// zero all others
		for (int32 i=0; i<ValuesToNormalize.Num(); ++i)
		{
			const int32 BoneIndex = RecordedBonesOnVertex[i];
			constexpr float NewWeight = 0.f;
			WeightEdits.MergeSingleEdit(BoneIndex, VertexID, NewWeight, bPruneInfluence, PreChangeWeights);
		}

		return;
	}

	// do any other influences have any weight on this vertex?
	//
	// In the case that:
	// 1. user applied any weight < 1 to this vertex AND
	// 2. there are NO other weights on this vertex
	// then we need to decide where to put the remaining influence...
	//
	// the logic here attempts to find a reasonable and "least surprising" place to put the remaining weight based on artist feedback
	const bool bVertexHasNoOtherWeightedInfluences = Total <= MinimumWeightThreshold;
	if (bVertexHasNoOtherWeightedInfluences)
	{
		// does this vertex have any other recorded influences on it?
		// a "recorded" influence here is one that used to have weight, but no longer does
		if (!RecordedBonesOnVertex.IsEmpty())
		{
			// this vertex:
			// 1. was previously weighted to other influences
			// 2. has subsequently had all other weight removed
			// In this case, we evenly split the remaining weight among the recorded influences

			// distribute remaining weight evenly over other recorded influences
			const float WeightToDistribute = (1.0f - NewWeightValue) / RecordedBonesOnVertex.Num();
			for (int32 i=0; i<ValuesToNormalize.Num(); ++i)
			{
				const int32 BoneIndex = RecordedBonesOnVertex[i];
				const float NewWeight = WeightToDistribute;
				WeightEdits.MergeSingleEdit(BoneIndex, VertexID, NewWeight, bPruneInfluence, PreChangeWeights);
			}

			// set current bone value to user assigned weight
			WeightEdits.MergeSingleEdit(BoneToHoldIndex, VertexID, NewWeightValue, bPruneInfluence, PreChangeWeights);
		}
		else
		{
			// this vertex:
			// 1. has no other recorded influences
			// 2. user is assigning PARTIAL weight to it (less than 1.0)
			// so in this case we push the remaining weight onto the PARENT bone
				
			// assign remaining weight to the parent
			const int32 ParentBoneIndex = GetParentBoneToWeightTo(BoneToHoldIndex);
			if (ParentBoneIndex == BoneToHoldIndex)
			{
				// was unable to find parent OR child bone!
				// this could only happen if user is trying to remove weight from the ONLY bone in the whole skeleton
				// in this case just assign the full weight to the bone (there's no other valid configuration)
				// this is a "do nothing" operation, but at least it generates an undo transaction to let user know the input was received
				constexpr float FullWeight = 1.0f;
				WeightEdits.MergeSingleEdit(BoneToHoldIndex, VertexID, FullWeight, bPruneInfluence, PreChangeWeights);
			}
			else
			{
				// assign remaining weight to parent
				const float NewParentWeight = 1.0f - NewWeightValue;
				WeightEdits.MergeSingleEdit(ParentBoneIndex, VertexID, NewParentWeight, bPruneInfluence, PreChangeWeights);
				// and assign user requested weight to the current bone
				WeightEdits.MergeSingleEdit(BoneToHoldIndex, VertexID, NewWeightValue, bPruneInfluence, PreChangeWeights);
			}
		}
		
		return;
	}

	// a normal weight edit where we assign the weight as requested
	// and split the remainder amongst the recorded influences
	{
		// calculate amount we have to spread across the other bones affecting this vertex
		const float AvailableTotal = 1.0f - NewWeightValue;

		// normalize weights into available space not set by current bone
		for (int32 i=0; i<ValuesToNormalize.Num(); ++i)
		{
			float NormalizedValue = 0.f;
			if (AvailableTotal > MinimumWeightThreshold && Total > KINDA_SMALL_NUMBER)
			{
				NormalizedValue = (ValuesToNormalize[i] / Total) * AvailableTotal;	
			}
			const int32 BoneIndex = RecordedBonesOnVertex[i];
			const float NewWeight = NormalizedValue;
			WeightEdits.MergeSingleEdit(BoneIndex, VertexID, NewWeight, bPruneInfluence, PreChangeWeights);
		}

		// record current bone edit
		WeightEdits.MergeSingleEdit(BoneToHoldIndex, VertexID, NewWeightValue, bPruneInfluence, PreChangeWeights);
	}
}

void FSkinToolWeights::ApplyCurrentWeightsToMeshDescription(FMeshDescription* MeshDescription)
{
	FSkeletalMeshAttributes MeshAttribs(*MeshDescription);
	FSkinWeightsVertexAttributesRef VertexWeightAttrs = MeshAttribs.GetVertexSkinWeights(Profile);
	
	UE::AnimationCore::FBoneWeightsSettings Settings;
	Settings.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::None);

	TArray<UE::AnimationCore::FBoneWeight> BoneWeightsToApply;
	BoneWeightsToApply.Reserve(UE::AnimationCore::MaxInlineBoneWeightCount);

	const int32 NumVertices = MeshDescription->Vertices().Num();
	if (!ensure(CurrentWeights.Num() == NumVertices))
	{
		// weights are out of sync with mesh description you're trying to apply them to
		return;
	}
	
	for (int32 VertexIndex = 0; VertexIndex < NumVertices; VertexIndex++)
	{
		BoneWeightsToApply.Reset();

		const VertexWeights& VertexWeights = CurrentWeights[VertexIndex];
		for (const FVertexBoneWeight& SingleBoneWeight : VertexWeights)
		{
			if (!ensure(SingleBoneWeight.BoneID != INDEX_NONE))
			{
				continue;
			}
			BoneWeightsToApply.Add(UE::AnimationCore::FBoneWeight(SingleBoneWeight.BoneID, SingleBoneWeight.Weight));
		}

		VertexWeightAttrs.Set(FVertexID(VertexIndex), UE::AnimationCore::FBoneWeights::Create(BoneWeightsToApply, Settings));
	}
}

float FSkinToolWeights::GetWeightOfBoneOnVertex(
	const int32 BoneIndex,
	const int32 VertexID,
	const TArray<VertexWeights>& InVertexWeights)
{
	const VertexWeights& VertexWeights = InVertexWeights[VertexID];
	for (const FVertexBoneWeight& BoneWeight : VertexWeights)
	{
		if (BoneWeight.BoneID == BoneIndex)
		{
			return BoneWeight.Weight;
		}
	}
	
	return 0.f;
}

void FSkinToolWeights::SetWeightOfBoneOnVertex(
	const int32 BoneIndex,
	const int32 VertexID,
	const float Weight,
	TArray<VertexWeights>& InOutVertexWeights)
{
	Deformer.SetVertexNeedsUpdated(VertexID);

	if (!ensure(BoneIndex != INDEX_NONE))
	{
		return;
	}
	
	// incoming weights are assumed to be normalized already, so set it directly
	VertexWeights& VertexWeights = InOutVertexWeights[VertexID];
	for (FVertexBoneWeight& BoneWeight : VertexWeights)
	{
		if (BoneWeight.BoneID == BoneIndex)
		{
			BoneWeight.Weight = Weight;
			return;
		}
	}

	// bone not already an influence on this vertex, so we need to add it..

	// if the weight was pruned, it won't be recorded in the VertexWeights array,
	// but we also don't want to add it back
	if (FMath::IsNearlyEqual(Weight, 0.f))
	{
		return;
	}

	// if vertex has room for more influences, then simply add it
	if (VertexWeights.Num() < UE::AnimationCore::MaxInlineBoneWeightCount)
	{
		// add a new influence to this vertex
		AddNewInfluenceToVertex(VertexID, BoneIndex, Weight,InOutVertexWeights);
		return;
	}

	//
	// uh oh, we're out of room for more influences on this vertex, so lets kick the smallest influence to make room
	//

	// find the smallest influence
	float SmallestInfluence = TNumericLimits<float>::Max();
	int32 SmallestInfluenceIndex = INDEX_NONE;
	for (int32 InfluenceIndex=0; InfluenceIndex<VertexWeights.Num(); ++InfluenceIndex)
	{
		const FVertexBoneWeight& BoneWeight = VertexWeights[InfluenceIndex];
		if (BoneWeight.Weight <= SmallestInfluence)
		{
			SmallestInfluence = BoneWeight.Weight;
			SmallestInfluenceIndex = InfluenceIndex;
		}
	}

	// replace smallest influence
	FVertexBoneWeight& BoneWeightToReplace = VertexWeights[SmallestInfluenceIndex];
	BoneWeightToReplace.Weight = Weight;
	BoneWeightToReplace.BoneID = BoneIndex;
	BoneWeightToReplace.VertexInBoneSpace = Deformer.InvCSRefPoseTransforms[BoneIndex].TransformPosition(Deformer.RefPoseVertexPositions[VertexID]);

	// now we need to re-normalize because the stamp does not handle maximum influences
	float TotalWeight = 0.f;
	for (const FVertexBoneWeight& BoneWeight : VertexWeights)
	{
		TotalWeight += BoneWeight.Weight;
	}
	for (FVertexBoneWeight& BoneWeight : VertexWeights)
	{
		BoneWeight.Weight /= TotalWeight;
	}
}

void FSkinToolWeights::RemoveInfluenceFromVertex(
	const VertexIndex InVertexID,
	const BoneIndex InBoneID,
	TArray<VertexWeights>& InOutVertexWeights)
{
	// should never be pruning a vertex that doesn't exist
	if (!ensure(InOutVertexWeights.IsValidIndex(InVertexID)))
	{
		return;
	}
	
	VertexWeights& SingleVertexWeights = InOutVertexWeights[InVertexID];
	const int32 IndexOfBoneInVertex = SingleVertexWeights.IndexOfByPredicate([&InBoneID](const FVertexBoneWeight& CurrentVertexWeight)
	{
		return CurrentVertexWeight.BoneID == InBoneID;
	});
	
	// can't prune an influence that doesn't exist on a vertex
	// this may happen if the calling code already pruned the influence to avoid normalization weights
	if (!ensure(IndexOfBoneInVertex != INDEX_NONE))
	{
		return;
	}
	
	SingleVertexWeights.RemoveAt(IndexOfBoneInVertex);
}

void FSkinToolWeights::AddNewInfluenceToVertex(
	const VertexIndex InVertexID,
	const BoneIndex InBoneID,
	const float Weight,
	TArray<VertexWeights>& InOutVertexWeights)
{		
	// should never be adding an influence to a vertex that doesn't exist
	if (!ensure(InOutVertexWeights.IsValidIndex(InVertexID)))
	{
		return;
	}

	// get list of weights on this single vertex
	VertexWeights& SingleVertexWeights = InOutVertexWeights[InVertexID];

	// should never be trying to add more influences beyond the max per-vertex limit
	if (!ensure(SingleVertexWeights.Num() < UE::AnimationCore::MaxInlineBoneWeightCount))
	{
		return;
	}

	const int32 IndexOfBoneInVertex = SingleVertexWeights.IndexOfByPredicate([&InBoneID](const FVertexBoneWeight& CurrentVertexWeight)
	{
		return CurrentVertexWeight.BoneID == InBoneID;
	});

	// should never be adding an influence that already exists on a vertex
	if (!ensure(IndexOfBoneInVertex == INDEX_NONE))
	{
		return;
	}

	// should never be adding an influence that doesn't exist in the skeleton
	if (!ensure(Deformer.InvCSRefPoseTransforms.IsValidIndex(InBoneID)))
	{
		return;
	}

	// add a new influence to this vertex
	const FVector PosLocalToBone = Deformer.InvCSRefPoseTransforms[InBoneID].TransformPosition(Deformer.RefPoseVertexPositions[InVertexID]);
	SingleVertexWeights.Emplace(InBoneID, PosLocalToBone, Weight);
}

void FSkinToolWeights::SyncWeightBuffers()
{
	PreChangeWeights = CurrentWeights;

	for (int32 i=0; i<MaxFalloffPerVertexThisStroke.Num(); ++i)
	{
		MaxFalloffPerVertexThisStroke[i] = 0.f;
	}
}

float FSkinToolWeights::SetCurrentFalloffAndGetMaxFalloffThisStroke(int32 VertexID, float CurrentStrength)
{
	float& MaxFalloffThisStroke = MaxFalloffPerVertexThisStroke[VertexID];
	if (MaxFalloffThisStroke < CurrentStrength)
	{
		MaxFalloffThisStroke = CurrentStrength;
	}
	return MaxFalloffThisStroke;
}

void FSkinToolWeights::ApplyEditsToCurrentWeights(const FMultiBoneWeightEdits& Edits)
{
	// apply weight edits to the CurrentWeights data
	for (const TTuple<BoneIndex, FSingleBoneWeightEdits>& BoneWeightEdits : Edits.PerBoneWeightEdits)
	{
		const FSingleBoneWeightEdits& WeightEdits = BoneWeightEdits.Value;
		const int32 BoneIndex = WeightEdits.BoneIndex;
		check(BoneIndex != INDEX_NONE);
		for (const TTuple<int32, float>& NewWeight : WeightEdits.NewWeights)
		{
			const int32 VertexID = NewWeight.Key;
			const float Weight = NewWeight.Value;
			SetWeightOfBoneOnVertex(BoneIndex, VertexID, Weight, CurrentWeights);
		}
	}

	// weights on Bones were modified, so update IsBoneWeighted array
	for (const TTuple<BoneIndex, FSingleBoneWeightEdits>& BoneWeightEdits : Edits.PerBoneWeightEdits)
	{
		const BoneIndex CurrentBoneIndex = BoneWeightEdits.Key;
		UpdateIsBoneWeighted(CurrentBoneIndex);
	}
}

void FSkinToolWeights::UpdateIsBoneWeighted(BoneIndex BoneToUpdate)
{
	IsBoneWeighted[BoneToUpdate] = false;
	for (const VertexWeights& VertexData : CurrentWeights)
	{
		for (const FVertexBoneWeight& VertexBoneData : VertexData)
		{
			if (VertexBoneData.BoneID == BoneToUpdate && VertexBoneData.Weight > UE::AnimationCore::BoneWeightThreshold)
			{
				IsBoneWeighted[BoneToUpdate] = true;
				break;
			}
		}
		if (IsBoneWeighted[BoneToUpdate])
		{
			break;
		}
	}
}

BoneIndex FSkinToolWeights::GetParentBoneToWeightTo(BoneIndex ChildBone)
{
	int32 ParentBoneIndex = 0;
	if (const USkeletalMesh* SkeletalMesh = Deformer.Component->GetSkeletalMeshAsset())
	{
		ParentBoneIndex = SkeletalMesh->GetRefSkeleton().GetParentIndex(ChildBone);
	}

	// are we at the root? (no parent)
	if (ParentBoneIndex == INDEX_NONE)
	{
		ParentBoneIndex = 0; // fallback to root
		
		// in this case return the first child bone, if there is one
		// NOTE: this allows the user to forcibly remove all weight on the root bone, without having another recorded influence on it
		if (const USkeletalMesh* SkeletalMesh = Deformer.Component->GetSkeletalMeshAsset())
		{
			TArray<int32> RootsChildren;
			SkeletalMesh->GetRefSkeleton().GetDirectChildBones(0, RootsChildren);
			if (!RootsChildren.IsEmpty())
			{
				ParentBoneIndex = RootsChildren[0];
			}
		}
	}

	return ParentBoneIndex;
}

void FMeshSkinWeightsChange::Apply(UObject* Object)
{
	USkinWeightsPaintTool* Tool = CastChecked<USkinWeightsPaintTool>(Object);

	Tool->ExternalUpdateSkinWeightLayer(LOD, SkinWeightProfile);
	
	// apply weight edits
	for (TTuple<BoneIndex, FSingleBoneWeightEdits>& Pair : AllWeightEdits.PerBoneWeightEdits)
	{
		const BoneIndex BoneID = Pair.Key;
		
		// add and/or remove this bone from vertices
		// (any weight removed by this influence is presumed to be added back in a normalized fashion by ExternalUpdateWeights)
		Tool->ExternalRemoveInfluenceFromVertices(BoneID, Pair.Value.VerticesRemovedFrom);
		Tool->ExternalAddInfluenceToVertices(BoneID, Pair.Value.VerticesAddedTo);

		// update the weights on this bone to use the new weights
		Tool->ExternalUpdateWeights(BoneID, Pair.Value.NewWeights);
	}
}

void FMeshSkinWeightsChange::Revert(UObject* Object)
{
	USkinWeightsPaintTool* Tool = CastChecked<USkinWeightsPaintTool>(Object);

	// update the skin weight profile
	Tool->ExternalUpdateSkinWeightLayer(LOD, SkinWeightProfile);
	
	// apply weight edits
	for (TTuple<int32, FSingleBoneWeightEdits>& Pair : AllWeightEdits.PerBoneWeightEdits)
	{
		const BoneIndex BoneID = Pair.Key;
		
		// add back vertices that this bone was removed from
		Tool->ExternalAddInfluenceToVertices(BoneID, Pair.Value.VerticesRemovedFrom);
		// remove vertices that this bone was added to
		Tool->ExternalRemoveInfluenceFromVertices(BoneID, Pair.Value.VerticesAddedTo);
		
		// set the weights back to what they were before this change
		Tool->ExternalUpdateWeights(BoneID, Pair.Value.OldWeights);
	}

	// notify dependent systems
	Tool->OnWeightsChanged.Broadcast();
}

void FMeshSkinWeightsChange::StoreBoneWeightEdit(const FSingleBoneWeightEdits& BoneWeightEdit, const TFunction<int32(int32)>& VertexIndexConverter)
{
	if (VertexIndexConverter)
	{
		// remap vertex indices of old and new weights using the supplied converter function
		FSingleBoneWeightEdits RemappedBoneWeightEdits = BoneWeightEdit;
		TMap<VertexIndex, float> RemappedWeights;

		// remap vertex indices in NEW weights
		for (TPair<VertexIndex, float>& NewWeight : RemappedBoneWeightEdits.NewWeights)
		{
			RemappedWeights.Add(VertexIndexConverter(NewWeight.Key), NewWeight.Value);
		}
		RemappedBoneWeightEdits.NewWeights = RemappedWeights;

		// remap vertex indices in OLD weights
		RemappedWeights.Reset();
		for (TPair<VertexIndex, float>& OldWeight : RemappedBoneWeightEdits.OldWeights)
		{
			RemappedWeights.Add(VertexIndexConverter(OldWeight.Key), OldWeight.Value);
		}
		RemappedBoneWeightEdits.OldWeights = RemappedWeights;

		// remap vertex indices that had this influence added to them
		for (int32 VertArrayIndex=0; VertArrayIndex<RemappedBoneWeightEdits.VerticesAddedTo.Num(); ++VertArrayIndex)
		{
			RemappedBoneWeightEdits.VerticesAddedTo[VertArrayIndex] = VertexIndexConverter(VertArrayIndex);
		}

		// remap vertex indices that had this influence removed from them
		for (int32 VertArrayIndex=0; VertArrayIndex<RemappedBoneWeightEdits.VerticesRemovedFrom.Num(); ++VertArrayIndex)
		{
			RemappedBoneWeightEdits.VerticesRemovedFrom[VertArrayIndex] = VertexIndexConverter(VertArrayIndex);
		}

		// merge the weight edits
		AllWeightEdits.MergeEdits(RemappedBoneWeightEdits);
		return;
	}

	// store weight edits directly	
	AllWeightEdits.MergeEdits(BoneWeightEdit);
}

void FMeshSkinWeightsChange::StoreMultipleWeightEdits(const FMultiBoneWeightEdits& WeightEdits, const TFunction<int32(int32)>& VertexIndexConverter)
{
	// store weight edits
	for (const TTuple<BoneIndex, FSingleBoneWeightEdits>& BoneWeightEdits : WeightEdits.PerBoneWeightEdits)
	{
		StoreBoneWeightEdit(BoneWeightEdits.Value, VertexIndexConverter);
	}
}

/*
 * ToolBuilder
 */

UMeshSurfacePointTool* USkinWeightsPaintToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	USkinWeightsPaintTool* Tool = NewObject<USkinWeightsPaintTool>(SceneState.ToolManager);
	Tool->Init(SceneState);
	return Tool;
}

const FToolTargetTypeRequirements& USkinWeightsPaintToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UMaterialProvider::StaticClass(),
		UMeshDescriptionProvider::StaticClass(),
		UMeshDescriptionCommitter::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass()
		});
	return TypeRequirements;
}

void USkinWeightsPaintTool::Init(const FToolBuilderState& InSceneState)
{
	const UContextObjectStore* ContextObjectStore = InSceneState.ToolManager->GetContextObjectStore();
	EditorContext = ContextObjectStore->FindContext<USkeletalMeshEditorContextObjectBase>();
	PersonaModeManagerContext = ContextObjectStore->FindContext<UPersonaEditorModeManagerContext>();
	TargetManager = InSceneState.TargetManager;
}

void USkinWeightsPaintTool::Setup()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::Setup);
	
	UDynamicMeshBrushTool::Setup();

	const USkeletalMeshComponent* Component = GetSkeletalMeshComponent(Target);
	check(Component && Component->GetSkeletalMeshAsset())

	// create a mesh description for editing (this must be done before calling UpdateBonePositionInfos)
	
	bool bSupportsLODs = false;
	const EMeshLODIdentifier DefaultLOD = UE::ToolTarget::GetTargetMeshDescriptionLOD(Target, bSupportsLODs);
	ensure(bSupportsLODs);
	const FGetMeshParameters Params(true, DefaultLOD);
	// copy the mesh description from the tool target to edit it
	const FMeshDescription* ToolTargetMeshDescription = UE::ToolTarget::GetMeshDescription(Target, Params);
	EditedMeshes.Emplace(DefaultLOD, FCleanedEditMesh(*PreviewMesh->GetMesh(), *ToolTargetMeshDescription));
	CurrentlyEditedLOD = DefaultLOD;

	// create a custom set of properties inheriting from the base tool properties
	WeightToolProperties = NewObject<USkinWeightsPaintToolProperties>(this);
	WeightToolProperties->RestoreProperties(this);
	WeightToolProperties->WeightTool = this;
	WeightToolProperties->bSpecifyRadius = true;
	// watch for skin weight layer changes
	WeightToolProperties->ActiveLOD = GetLODName(DefaultLOD);
	int32 WatcherIndex = WeightToolProperties->WatchProperty(WeightToolProperties->ActiveLOD, [this](FName) { OnActiveLODChanged(); });
	WeightToolProperties->SilentUpdateWatcherAtIndex(WatcherIndex);
	WeightToolProperties->ActiveSkinWeightProfile = FSkeletalMeshAttributesShared::DefaultSkinWeightProfileName;
	WatcherIndex = WeightToolProperties->WatchProperty(WeightToolProperties->ActiveSkinWeightProfile, [this](FName) { OnActiveSkinWeightProfileChanged(); });
	WeightToolProperties->SilentUpdateWatcherAtIndex(WatcherIndex);
	WatcherIndex = WeightToolProperties->WatchProperty(WeightToolProperties->NewSkinWeightProfile, [this](FName) { OnNewSkinWeightProfileChanged(); });
	WeightToolProperties->SilentUpdateWatcherAtIndex(WatcherIndex);
	WeightToolProperties->SourceSkeletalMesh = nullptr;
    WeightToolProperties->SourcePreviewOffset = FTransform::Identity;
		
	// replace the base brush properties
	ReplaceToolPropertySource(BrushProperties, WeightToolProperties);
	BrushProperties = WeightToolProperties;
	// brush render customization
	BrushStampIndicator->bScaleNormalByStrength = true;
	BrushStampIndicator->SecondaryLineThickness = 1.0f;
	BrushStampIndicator->SecondaryLineColor = FLinearColor::Yellow;
	RecalculateBrushRadius();

	// default to the root bone as current bone
	PendingCurrentBone = CurrentBone = Component->GetSkeletalMeshAsset()->GetRefSkeleton().GetBoneName(0);

	// configure preview mesh
	PreviewMesh->SetTangentsMode(EDynamicMeshComponentTangentsMode::AutoCalculated);
	PreviewMesh->SetShadowsEnabled(false);

	// create the transfer manager
	TransferManager = NewObject<UWeightToolTransferManager>();
	TransferManager->InitialSetup(this, GetViewportClient());

	// create the isolated selection manager
	SelectionIsolator = NewObject<UWeightToolSelectionIsolator>();
	SelectionIsolator->InitialSetup(this);

	// setup selection for the main mesh
	MeshSelector = NewObject<UWeightToolMeshSelector>(this);
	auto OnSelectionChangedLambda = [this](){OnSelectionChanged.Broadcast();};
	MeshSelector->InitialSetup(TargetWorld.Get(), this, GetViewportClient(), OnSelectionChangedLambda);

	// run all initialization for mesh/weights
	UpdateCurrentlyEditedMesh(Component, *GetCurrentlyEditedDynamicMesh(), *GetCurrentlyEditedMeshDescription());

	// bind the skeletal mesh editor context
	if (EditorContext.IsValid())
	{
		EditorContext->BindTo(this);
	}

	// trigger last used mode
	ToggleEditingMode();

	// modify viewport render settings to optimize for painting weights
	FPreviewProfileController PreviewProfileController;
	PreviewProfileToRestore = PreviewProfileController.GetActiveProfile();
	PreviewProfileController.SetActiveProfile(UDefaultEditorProfiles::EditingProfileName.ToString());
	if (FEditorViewportClient* ViewportClient = GetViewportClient())
	{
		ViewportClient->SetViewMode(VMI_Lit_Wireframe);
	}
	// turn on bone colors
	bBoneColorsToRestore = GetDefault<UPersonaOptions>()->bShowBoneColors;
	GetMutableDefault<UPersonaOptions>()->bShowBoneColors = true;
	// set focus to viewport so brush hotkey works
	SetFocusInViewport();
	
	// inform user of tool keys
	// TODO talk with UX team about viewport overlay to show hotkeys
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartSkinWeightsPaint", "Paint per-bone skin weights. [ and ] change brush size, Ctrl to Erase/Subtract, Shift to Smooth"),
		EToolMessageLevel::UserNotification);
}

void USkinWeightsPaintTool::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	Super::DrawHUD(Canvas, RenderAPI);

	if (MeshSelector)
	{
		MeshSelector->DrawHUD(Canvas, RenderAPI);
	}
			
	GetWeightTransferManager()->DrawHUD(Canvas, RenderAPI);
}

void USkinWeightsPaintTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	switch (WeightToolProperties->EditingMode)
	{
	case EWeightEditMode::Brush:
		Super::Render(RenderAPI);
	case EWeightEditMode::Mesh:
		{
			if (MeshSelector)
			{
				MeshSelector->Render(RenderAPI);	
			}

			GetWeightTransferManager()->Render(RenderAPI);
			
			break;
		}
	default:
		return;
	}
}

FBox USkinWeightsPaintTool::GetWorldSpaceFocusBox()
{
	if (!WeightToolProperties)
	{
		return PreviewMesh->GetActor()->GetComponentsBoundingBox();
	}
	
	// 1. Prioritize Brush & Vertex modes
	switch (WeightToolProperties->EditingMode)
	{
	case EWeightEditMode::Brush:
			{
				const FVector Radius(CurrentBrushRadius);
				return FBox(LastBrushStamp.WorldPosition - Radius, LastBrushStamp.WorldPosition + Radius);
			}
			break;
	case EWeightEditMode::Mesh:
		{
			FAxisAlignedBox3d Bounds = FAxisAlignedBox3d::Empty();
			static const TArray<int32> Dummy;
			const TArray<int32>& SelectedVertices = MeshSelector ? MeshSelector->GetSelectedVertices() : Dummy;
			if (!SelectedVertices.IsEmpty())
			{
				const FDynamicMesh3* Mesh = PreviewMesh->GetMesh();
				const FTransform3d Transform(PreviewMesh->GetTransform());
				for (const int32 VertexID : SelectedVertices)
				{
					Bounds.Contain(Transform.TransformPosition(Mesh->GetVertex(VertexID)));
				}
			}
			if (Bounds.MaxDim() > FMathf::ZeroTolerance)
			{
				return static_cast<FBox>(Bounds);
			}
		}
		break;
	case EWeightEditMode::Bones:
	default:
		break;
	}

	// 2. Fallback on framing selected bones (if there are any)
	// TODO, there are several places in the engine that frame bone selections. Let's consolidate this logic.
	if (!SelectedBoneIndices.IsEmpty())
	{
		const USkeletalMeshComponent* MeshComponent = Weights.Deformer.Component;
		const FReferenceSkeleton& RefSkeleton = MeshComponent->GetSkeletalMeshAsset()->GetRefSkeleton();
		const TArray<FTransform>& CurrentBoneTransforms = MeshComponent->GetComponentSpaceTransforms();
		if (!CurrentBoneTransforms.IsEmpty())
		{
			FAxisAlignedBox3d Bounds = FAxisAlignedBox3d::Empty();
			for (const int32 BoneIndex : SelectedBoneIndices)
			{
				// add bone position and position of all direct children to the frame bounds
				const FVector BonePosition = CurrentBoneTransforms[BoneIndex].GetLocation();
				Bounds.Contain(BonePosition);
				TArray<int32> ChildrenIndices;
				RefSkeleton.GetDirectChildBones(BoneIndex, ChildrenIndices);
				if (ChildrenIndices.IsEmpty())
				{
					constexpr float SingleBoneSize = 10.f;
					FVector BoneOffset = FVector(SingleBoneSize, SingleBoneSize, SingleBoneSize);
					Bounds.Contain(BonePosition + BoneOffset);
					Bounds.Contain(BonePosition - BoneOffset);
				}
				else
				{
					for (const int32 ChildIndex : ChildrenIndices)
					{
						Bounds.Contain(CurrentBoneTransforms[ChildIndex].GetLocation());
					}
				}	
			}
			if (Bounds.MaxDim() > FMathf::ZeroTolerance)
			{
				return static_cast<FBox>(Bounds);
			}
		}
	}

	// 3. Finally, fallback on component bounds if nothing else is selected
	static constexpr bool bNonColliding = true;
	FBox PreviewBox = PreviewMesh->GetActor()->GetComponentsBoundingBox(bNonColliding);
	// expand the bounds by the source transfer mesh (if there is one and it's visible)
	if (UPreviewMesh* SourcePreviewMesh = GetWeightTransferManager()->GetPreviewMesh())
	{
		if (AActor* SourceActor = SourcePreviewMesh->GetActor())
		{
			PreviewBox += SourceActor->GetComponentsBoundingBox(bNonColliding);
		}
	}

	return PreviewBox;
}

void USkinWeightsPaintTool::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	// toggle Relax mode on while shift key is held, then swap back to prior mode on release
	if (ModifierID == ShiftModifier)
	{
		if (bIsOn)
		{
			// when shift key is pressed
			if (!bShiftToggle)
			{
				WeightToolProperties->PriorBrushMode = WeightToolProperties->BrushMode;
				WeightToolProperties->SetBrushMode(EWeightEditOperation::Relax);
			}
		}
		else
		{
			// when shift key is released
			if (bShiftToggle)
			{
				WeightToolProperties->SetBrushMode(WeightToolProperties->PriorBrushMode);
			}
		}
	}

	Super::OnUpdateModifierState(ModifierID, bIsOn);
}

FInputRayHit USkinWeightsPaintTool::CanBeginClickDragSequence(const FInputDeviceRay& InPressPos)
{
	// NOTE: this function is only overridden to prevent left-click fly camera behavior while brushing
	// this should eventually be removed once we have a clear way of disabling the fly-cam mode
	
	if (WeightToolProperties->EditingMode != EWeightEditMode::Brush)
	{
		return FInputRayHit(); // allow other behaviors to capture mouse while not brushing
	}
	
	const FInputRayHit Hit = Super::CanBeginClickDragSequence(InPressPos);
	if (Hit.bHit)
	{
		return Hit;
	}

	// always return a hit so we always capture and prevent accidental camera movement
	return FInputRayHit(TNumericLimits<float>::Max());
}

void USkinWeightsPaintTool::OnTick(float DeltaTime)
{
	if (SelectionIsolator)
	{
		SelectionIsolator->UpdateIsolatedSelection();
	}
	
	if (bStampPending)
	{
		ApplyStamp(LastStamp);
		bStampPending = false;
	}

	if (PendingCurrentBone.IsSet())
	{
		UpdateCurrentBone(*PendingCurrentBone);
		PendingCurrentBone.Reset();
	}

	if (bVertexColorsNeedUpdated)
	{
		UpdateVertexColorForAllVertices();
		bVertexColorsNeedUpdated = false;
	}

	if (!VerticesToUpdateColor.IsEmpty())
	{
		UpdateVertexColorForSubsetOfVertices();
		VerticesToUpdateColor.Empty();
	}

	// sparsely updates vertex positions (only on vertices with modified weights)
	Weights.Deformer.UpdateVertexDeformation(this, Weights.Deformer.Component->GetComponentSpaceTransforms());
}

void USkinWeightsPaintTool::UpdateCurrentlyEditedMesh(
	const USkeletalMeshComponent* InComponent,
	const FDynamicMesh3& InDynamicMesh,
	const FMeshDescription& InMeshDescription)
{
	// update the preview mesh in the viewport
	PreviewMesh->ReplaceMesh(InDynamicMesh);
	PreviewMesh->EditMesh([](FDynamicMesh3& Mesh)
	{
		Mesh.EnableAttributes();
		Mesh.Attributes()->DisablePrimaryColors();
		Mesh.Attributes()->EnablePrimaryColors();
		Mesh.Attributes()->PrimaryColors()->CreateFromPredicate([](int ParentVID, int TriIDA, int TriIDB){return true;}, 0.f);
	});
	SetDisplayVertexColors(WeightToolProperties->ColorMode != EWeightColorMode::FullMaterial);
	
	// update vertices & triangle octrees (this must be done after PreviewMesh has been updated)
	InitializeOctrees();

	// update the mesh selection mechanic (this must be done after PreviewMesh has been updated)
	MeshSelector->SetMesh(PreviewMesh, &InMeshDescription, InComponent->GetComponentTransform());

	// update weights
	Weights = FSkinToolWeights();
	if (!IsProfileValid(WeightToolProperties->GetActiveSkinWeightProfile()))
	{
		WeightToolProperties->ActiveSkinWeightProfile = FSkeletalMeshAttributesShared::DefaultSkinWeightProfileName;
		WeightToolProperties->bShowNewProfileName = false;
	}
	Weights.Profile = WeightToolProperties->GetActiveSkinWeightProfile();
	Weights.InitializeSkinWeights(InComponent, &InMeshDescription);
	bVertexColorsNeedUpdated = true;
	
	// update smooth operator (this must be done after PreviewMesh & Weights have been updated)
	InitializeSmoothWeightsOperator();

	// after any mesh change, the mirror tables will need rebuilt next time mirroring is used
	MirrorData.SetNeedsReinitialized();
}

void USkinWeightsPaintToolProperties::SetComponentMode(EComponentSelectionMode InComponentMode)
{
	ComponentSelectionMode = InComponentMode;
	
	WeightTool->UpdateSelectorState();
	WeightTool->SetFocusInViewport();
}

void USkinWeightsPaintToolProperties::SetFalloffMode(EWeightBrushFalloffMode InFalloffMode)
{
	GetBrushConfig().FalloffMode = InFalloffMode;
	SaveConfig();

	WeightTool->SetFocusInViewport();
}

void USkinWeightsPaintToolProperties::SetColorMode(EWeightColorMode InColorMode)
{
	ColorMode = InColorMode;
	WeightTool->SetDisplayVertexColors(ColorMode!=EWeightColorMode::FullMaterial);
	WeightTool->SetFocusInViewport();
}

void USkinWeightsPaintToolProperties::SetBrushMode(EWeightEditOperation InBrushMode)
{
	BrushMode = InBrushMode;

	// sync base tool settings with the mode specific saved values
	// these are the source of truth for the base class viewport rendering of brush
	BrushRadius = GetBrushConfig().Radius;
	BrushStrength = GetBrushConfig().Strength;
	BrushFalloffAmount = GetBrushConfig().Falloff;

	WeightTool->SetFocusInViewport();
}

void FIsolateSelectionChange::Apply(UObject* Object)
{
	USkinWeightsPaintTool* Tool = Cast<USkinWeightsPaintTool>(Object);
	if (Tool)
	{
		Tool->GetSelectionIsolator()->SetTrianglesToIsolate(IsolatedTrianglesAfter);
	}	
}

void FIsolateSelectionChange::Revert(UObject* Object)
{
	USkinWeightsPaintTool* Tool = Cast<USkinWeightsPaintTool>(Object);
	if (Tool)
	{
		Tool->GetSelectionIsolator()->SetTrianglesToIsolate(IsolatedTrianglesBefore);
	}
}

FString FIsolateSelectionChange::ToString() const
{
	return FToolCommandChange::ToString();
}

bool USkinWeightsPaintTool::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	const bool bIsInBrushMode = WeightToolProperties->EditingMode == EWeightEditMode::Brush;
	if (!bIsInBrushMode)
	{
		return false;
	}
	
	// do not query the triangle octree until all async ops are finished
	TriangleOctreeFuture.Wait();
	
	// put ray in local space of skeletal mesh component
	// currently no way to transform skeletal meshes in the editor,
	// but at some point in the future we may add the ability to move parts around
	const IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	const FTransform3d CurTargetTransform(TargetComponent->GetWorldTransform());
	FRay3d LocalRay(
		CurTargetTransform.InverseTransformPosition((FVector3d)Ray.Origin),
		CurTargetTransform.InverseTransformVector((FVector3d)Ray.Direction));
	UE::Geometry::Normalize(LocalRay.Direction);
	
	const FDynamicMesh3* Mesh = PreviewMesh->GetMesh();

	FViewCameraState StateOut;
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(StateOut);
	FVector3d LocalEyePosition(CurTargetTransform.InverseTransformPosition((FVector3d)StateOut.Position));
	const int32 TriID = TrianglesOctree->FindNearestHitObject(
		LocalRay,
		[this, Mesh, &LocalEyePosition](int TriangleID)
	{
		FVector3d Normal, Centroid;
		double Area;
		Mesh->GetTriInfo(TriangleID, Normal, Area, Centroid);
		return Normal.Dot((Centroid - LocalEyePosition)) < 0;
	});
	
	if (TriID != IndexConstants::InvalidID)
	{	
		FastTriWinding::FTriangle3d Triangle;
		Mesh->GetTriVertices(TriID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
		UE::Geometry::FIntrRay3Triangle3d Query(LocalRay, Triangle);
		Query.Find();

		StampLocalPos = LocalRay.PointAt(Query.RayParameter);
		TriangleUnderStamp = TriID;

		OutHit.FaceIndex = TriID;
		OutHit.Distance = Query.RayParameter;
		OutHit.Normal = CurTargetTransform.TransformVector(Mesh->GetTriNormal(TriID));
		OutHit.ImpactPoint = CurTargetTransform.TransformPosition(StampLocalPos);
		return true;
	}
	
	return false;
}

void USkinWeightsPaintTool::OnBeginDrag(const FRay& WorldRay)
{
	UDynamicMeshBrushTool::OnBeginDrag(WorldRay);

	bInvertStroke = GetCtrlToggle();
	BeginChange();
	StartStamp = UBaseBrushTool::LastBrushStamp;
	LastStamp = StartStamp;
	bStampPending = true;
	LongTransactions.Open(LOCTEXT("PaintWeightChange", "Paint skin weights."), GetToolManager());
}

void USkinWeightsPaintTool::OnUpdateDrag(const FRay& WorldRay)
{
	UDynamicMeshBrushTool::OnUpdateDrag(WorldRay);
	
	LastStamp = UBaseBrushTool::LastBrushStamp;
	bStampPending = true;
}

void USkinWeightsPaintTool::OnEndDrag(const FRay& Ray)
{
	UDynamicMeshBrushTool::OnEndDrag(Ray);

	bInvertStroke = false;
	bStampPending = false;

	if (ActiveChange)
	{
		// close change, record transaction
		const FText TransactionLabel = LOCTEXT("PaintWeightChange", "Paint skin weights.");
		EndChange(TransactionLabel);
		LongTransactions.Close(GetToolManager());
	}
}

bool USkinWeightsPaintTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	UDynamicMeshBrushTool::OnUpdateHover(DevicePos);
	return true;
}

double USkinWeightsPaintTool::EstimateMaximumTargetDimension()
{
	if (const IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target))
	{
		const USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(TargetComponent->GetOwnerComponent());
		if (USkeletalMesh* SkeletalMesh = Component->GetSkeletalMeshAsset())
		{
			return SkeletalMesh->GetBounds().SphereRadius * 2.0f;
		}
	}
	
	return Super::EstimateMaximumTargetDimension();
}

void USkinWeightsPaintTool::CalculateVertexROI(
	const FBrushStampData& InStamp,
	TArray<VertexIndex>& OutVertexIDs,
	TArray<float>& OutVertexFalloffs)
{
	using namespace UE::Geometry;

	TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::CalculateVertexROI);

	auto DistanceToFalloff = [this](int32 InVertexID, float InDistanceSq)-> float
	{
		const float CurrentFalloff = CalculateBrushFalloff(FMath::Sqrt(InDistanceSq));
		const float UseFalloff = Weights.SetCurrentFalloffAndGetMaxFalloffThisStroke(InVertexID, CurrentFalloff);
		return UseFalloff;
	};
	
	if (WeightToolProperties->GetBrushConfig().FalloffMode == EWeightBrushFalloffMode::Volume)
	{
		const IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
		const FTransform3d Transform(TargetComponent->GetWorldTransform());
		const FVector3d StampPosLocal = Transform.InverseTransformPosition(InStamp.WorldPosition);
		const float RadiusSqr = CurrentBrushRadius * CurrentBrushRadius;
		const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();
		const FAxisAlignedBox3d QueryBox(StampPosLocal, CurrentBrushRadius);
		VerticesOctree->RangeQuery(QueryBox,
			[&](int32 VertexID) { return FVector3d::DistSquared(Mesh->GetVertex(VertexID), StampPosLocal) < RadiusSqr; },
			OutVertexIDs);

		const FNonManifoldMappingSupport NonManifoldMappingSupport(*Mesh);
		TArray<VertexIndex> SourceVertexIDs;
		SourceVertexIDs.Reserve(OutVertexIDs.Num());
		OutVertexFalloffs.Reserve(OutVertexIDs.Num());
		for (const int32 VertexID : OutVertexIDs)
		{
			const float DistSq = FVector3d::DistSquared(Mesh->GetVertex(VertexID), StampPosLocal);

			const int32 SrcVertexId = NonManifoldMappingSupport.GetOriginalNonManifoldVertexID(VertexID);
			SourceVertexIDs.Add(SrcVertexId);
			OutVertexFalloffs.Add(DistanceToFalloff(SrcVertexId, DistSq));
		}
		OutVertexIDs = MoveTemp(SourceVertexIDs);
		
		return;
	}

	if (WeightToolProperties->GetBrushConfig().FalloffMode == EWeightBrushFalloffMode::Surface)
	{
		// create the ExpMap generator, computes vertex polar coordinates in a plane tangent to the surface
		const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();
		FFrame3d SeedFrame = Mesh->GetTriFrame(TriangleUnderStamp);
		SeedFrame.Origin = InStamp.WorldPosition;
		
		TMeshLocalParam<FDynamicMesh3> Param(Mesh);
		Param.ParamMode = ELocalParamTypes::PlanarProjection;
		const FIndex3i TriVerts = Mesh->GetTriangle(TriangleUnderStamp);
		Param.ComputeToMaxDistance(SeedFrame, TriVerts, InStamp.Radius * 1.5f);
		// store vertices under the brush and their distances from the stamp
		const float StampRadSq = FMath::Pow(InStamp.Radius, 2);
		const FNonManifoldMappingSupport NonManifoldMappingSupport(*Mesh);
		for (int32 VertexID : Mesh->VertexIndicesItr())
		{
			if (!Param.HasUV(VertexID))
			{
				continue;
			}
			
			FVector2d UV = Param.GetUV(VertexID);
			const float DistSq = UV.SizeSquared();
			if (DistSq >= StampRadSq)
			{
				continue;
			}

			const int32 SrcVertexId = NonManifoldMappingSupport.GetOriginalNonManifoldVertexID(VertexID);
			OutVertexFalloffs.Add(DistanceToFalloff(SrcVertexId, DistSq));
			OutVertexIDs.Add(SrcVertexId);
		}
		
		return;
	}
	
	checkNoEntry();
}

FVector4f USkinWeightsPaintTool::GetColorOfVertex(VertexIndex InVertexIndex, BoneIndex InCurrentBoneIndex) const
{
	switch (WeightToolProperties->ColorMode)
	{
	case EWeightColorMode::Greyscale:
		{
			if (InCurrentBoneIndex == INDEX_NONE)
			{
				return FLinearColor::Black; // with no bone selected, all vertices are drawn black
			}
			const float Value = Weights.GetWeightOfBoneOnVertex(InCurrentBoneIndex, InVertexIndex, Weights.CurrentWeights);
			return FMath::Lerp(FLinearColor::Black, FLinearColor::White, Value);
		}
	case EWeightColorMode::Ramp:
		{
			if (InCurrentBoneIndex == INDEX_NONE)
			{
				return FLinearColor::Black; // with no bone selected, all vertices are drawn black
			}

			// get user-specified colors
			const TArray<FLinearColor>& Colors = WeightToolProperties->ColorRamp;
			// get weight value
			float Value = Weights.GetWeightOfBoneOnVertex(InCurrentBoneIndex, InVertexIndex, Weights.CurrentWeights);
			Value = FMath::Clamp(Value, 0.0f, 1.0f);

			// ZERO user supplied colors, then revert to greyscale
			if (Colors.IsEmpty())
			{
				return FMath::Lerp(FLinearColor::Black, FLinearColor::White, Value);
			}

			// ONE user defined color, blend it with black
			if (Colors.Num() == 1)
			{
				return FMath::Lerp(FLinearColor::Black, Colors[0], Value);
			}

			// TWO user defined color, simple LERP
			if (Colors.Num() == 2)
			{
				return FMath::Lerp(Colors[0], Colors[1], Value);
			}
			
			// blend colors between min and max value
			constexpr float MinValue = 0.1f;
			constexpr float MaxValue = 0.9f;
			
			// early out zero weights to min color
			if (Value <= MinValue)
			{
				return Colors[0];
			}

			// early out full weights to max color
			if (Value >= MaxValue)
			{
				return Colors.Last();
			}
			
			// remap from 0-1 to range of MinValue to MaxValue
			const float ScaledValue = (Value - MinValue) * 1.0f / (MaxValue - MinValue);
			// interpolate within two nearest ramp colors
			const float PerColorRange = 1.0f / (Colors.Num() - 1);
			const int ColorIndex = static_cast<int>(ScaledValue / PerColorRange);
			const float RangeStart = ColorIndex * PerColorRange;
			const float RangeEnd = (ColorIndex + 1) * PerColorRange;
			const float Param = (ScaledValue - RangeStart) / (RangeEnd - RangeStart);
			const FLinearColor& StartColor = Colors[ColorIndex];
			const FLinearColor& EndColor = Colors[ColorIndex+1];
			return UE::Geometry::ToVector4<float>(FMath::Lerp(StartColor, EndColor, Param));
		}
	case EWeightColorMode::BoneColors:
		{
			FVector4f Color = FVector4f::Zero();
			const VertexWeights& VertexWeights = Weights.CurrentWeights[InVertexIndex];
			for (const FVertexBoneWeight& BoneWeight : VertexWeights)
			{
				if (BoneWeight.Weight < KINDA_SMALL_NUMBER)
				{
					continue;
				}
				
				const float Value = InCurrentBoneIndex == BoneWeight.BoneID ? 1.0f: 0.6f;
				constexpr float Saturation = 0.75f;
				const FLinearColor BoneColor = SkeletalDebugRendering::GetSemiRandomColorForBone(BoneWeight.BoneID, Value, Saturation);
				Color = FMath::Lerp(Color, BoneColor, BoneWeight.Weight);
			}
			return Color;
		}
	case EWeightColorMode::FullMaterial:
		return FLinearColor::White;
	default:
		checkNoEntry();
		return FLinearColor::Black;
	}	
}


void USkinWeightsPaintTool::UpdateVertexColorForAllVertices()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::UpdateVertexColors);
	
	const int32 CurrentBoneIndex = GetBoneIndexFromName(CurrentBone);
	
	// update mesh with new value colors
	PreviewMesh->DeferredEditMesh([this, &CurrentBoneIndex](FDynamicMesh3& Mesh)
	{
		const UE::Geometry::FNonManifoldMappingSupport NonManifoldMappingSupport(Mesh);
		UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = Mesh.Attributes()->PrimaryColors();
		for (const int32 ElementId : ColorOverlay->ElementIndicesItr())
		{
			const int32 VertexID = ColorOverlay->GetParentVertex(ElementId);	
			const int32 SrcVertexID = NonManifoldMappingSupport.GetOriginalNonManifoldVertexID(VertexID);
			ColorOverlay->SetElement(ElementId, GetColorOfVertex(SrcVertexID, CurrentBoneIndex));
		}
		
	}, false);
	PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate, EMeshRenderAttributeFlags::VertexColors, false);
}

void USkinWeightsPaintTool::UpdateVertexColorForSubsetOfVertices()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::UpdateVertexColors);
	
	PreviewMesh->DeferredEditMesh([this](FDynamicMesh3& Mesh)
		{
			if (CurrentBone == NAME_None)
			{
				
			}
			TArray<int> ElementIds;
			UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = Mesh.Attributes()->PrimaryColors();
			const int32 CurrentBoneIndex = GetBoneIndexFromName(CurrentBone);
			for (const int32 VertexID : VerticesToUpdateColor)
			{
				FVector4f NewColor(GetColorOfVertex(VertexID, CurrentBoneIndex));
				ColorOverlay->GetVertexElements(VertexID, ElementIds);
				for (const int32 ElementId : ElementIds)
				{
					ColorOverlay->SetElement(ElementId, NewColor);
				}
				ElementIds.Reset();
			}
			
		}, false);
	PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate, EMeshRenderAttributeFlags::VertexColors, false);
}

float USkinWeightsPaintTool::CalculateBrushFalloff(float Distance) const
{
	const float f = FMathd::Clamp(1.f - BrushProperties->BrushFalloffAmount, 0.f, 1.f);
	float d = Distance / CurrentBrushRadius;
	double w = 1;
	if (d > f)
	{
		d = FMathd::Clamp((d - f) / (1.0 - f), 0.0, 1.0);
		w = (1.0 - d * d);
		w = w * w * w;
	}
	return w;
}

void USkinWeightsPaintTool::ApplyStamp(const FBrushStampData& Stamp)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::ApplyStamp);

	// must select a bone to paint in all modes EXCEPT relax-mode which operates on ALL bones
	const bool bIsInRelaxMode = WeightToolProperties->BrushMode == EWeightEditOperation::Relax;
	if (!bIsInRelaxMode && CurrentBone == NAME_None)
	{
		return;
	}
	
	// get the vertices under the brush, and their squared distances to the brush center
	// when using "Volume" brush, distances are straight line
	// when using "Surface" brush, distances are geodesics
	TArray<int32> VerticesInStamp;
	TArray<float> VertexFalloffs;
	CalculateVertexROI(Stamp, VerticesInStamp, VertexFalloffs);

	// gather sparse set of modifications made from this stamp, these edits are merged throughout
	// the lifetime of a single brush stroke in the "ActiveChange" allowing for undo/redo
	FMultiBoneWeightEdits WeightEditsFromStamp;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::EditWeightOfVerticesInStamp);
		// generate a weight edit from this stamp (includes modifications caused by normalization)
		if (WeightToolProperties->BrushMode == EWeightEditOperation::Relax)
		{
			// use mesh topology to iteratively smooth weights across neighboring vertices
			const float UseStrength = CalculateBrushStrengthToUse(EWeightEditOperation::Relax);
			constexpr int32 RelaxIterationsPerStamp = 3;
			CreateWeightEditsToRelaxVertices(VerticesInStamp, VertexFalloffs, UseStrength, RelaxIterationsPerStamp, WeightEditsFromStamp);
		}
		else
		{
			// edit weight; either by "Add", "Remove", "Replace", "Multiply"
			const float UseStrength = CalculateBrushStrengthToUse(WeightToolProperties->BrushMode);
			const int32 CurrentBoneIndex = GetCurrentBoneIndex();
			CreateWeightEditsForVertices(
				WeightToolProperties->BrushMode,
				CurrentBoneIndex,
				VerticesInStamp,
				VertexFalloffs,
				UseStrength,
				WeightEditsFromStamp);
		}
	}

	// apply weight edits to the mesh without closing the transaction
	ApplyWeightEditsWithoutTransaction(WeightEditsFromStamp);
}

float USkinWeightsPaintTool::CalculateBrushStrengthToUse(EWeightEditOperation EditMode) const
{
	float UseStrength = BrushProperties->BrushStrength;

	// invert brush strength differently depending on brush mode
	switch (EditMode)
	{
	case EWeightEditOperation::Add:
		{
			UseStrength *= bInvertStroke ? -1.0f : 1.0f;
			break;
		}
	case EWeightEditOperation::Replace:
		{
			UseStrength = bInvertStroke ? 1.0f - UseStrength : UseStrength;
			break;
		}
	case EWeightEditOperation::Multiply:
		{
			UseStrength = bInvertStroke ? 1.0f + UseStrength : UseStrength;
			break;
		}
	case EWeightEditOperation::Relax:
		{
			UseStrength = bInvertStroke ? 1.0f - UseStrength : UseStrength;
			break;
		}
	default:
		checkNoEntry();
	}

	return UseStrength;
}

void USkinWeightsPaintTool::CreateWeightEditsForVertices(
	EWeightEditOperation EditOperation,
	const BoneIndex Bone,
	const TArray<int32>& VertexIndices,
	const TArray<float>& VertexFalloffs,
	const float InValue,
	FMultiBoneWeightEdits& InOutWeightEdits)
{
	// spin through the vertices in the stamp and store new weight values in NewValuesFromStamp
	// afterwards, these values are normalized while taking into consideration the user's desired changes
	const int32 NumVerticesInStamp = VertexIndices.Num();
	for (int32 Index = 0; Index < NumVerticesInStamp; ++Index)
	{
		const int32 VertexID = VertexIndices[Index];
		const float UseFalloff = VertexFalloffs.IsValidIndex(Index) ? VertexFalloffs[Index] : 1.f;
		const float ValueBeforeStroke = Weights.GetWeightOfBoneOnVertex(Bone, VertexID, Weights.PreChangeWeights);

		// calculate new weight value
		float NewValueAfterStamp = ValueBeforeStroke;
		switch (EditOperation)
		{
		case EWeightEditOperation::Add:
			{
				NewValueAfterStamp = ValueBeforeStroke + (InValue * UseFalloff);
				break;
			}
		case EWeightEditOperation::Replace:
			{
				NewValueAfterStamp = FMath::Lerp(ValueBeforeStroke, InValue, UseFalloff);
				break;
			}
		case EWeightEditOperation::Multiply:
			{
				const float DeltaFromThisStamp = ((ValueBeforeStroke * InValue) - ValueBeforeStroke) * UseFalloff;
				NewValueAfterStamp = ValueBeforeStroke + DeltaFromThisStamp;
				break;
			}
		case EWeightEditOperation::RelativeScale:
			{
				// LERP the weight from it's current value towards 1 (for positive values) or towards 0 (for negative values)
				if (InValue >= 0.f)
				{
					NewValueAfterStamp = FMath::Lerp(ValueBeforeStroke, 1.0f, FMath::Abs(InValue) * UseFalloff);
				}
				else
				{
					NewValueAfterStamp = FMath::Lerp(ValueBeforeStroke, 0.0f, FMath::Abs(InValue) * UseFalloff);
				}
				break;
			}
		default:
			// relax operation not supported by this function, use RelaxWeightOnVertices()
			checkNoEntry();
		}

		// normalize the values across all bones affecting this vertex, and record the bone edits
		// normalization is done while holding all weights on the current bone constant so that user edits are not overwritten
		Weights.CreateWeightEditForVertex(
			Bone,
			VertexID,
			NewValueAfterStamp,
			InOutWeightEdits);
	}
}

void USkinWeightsPaintTool::CreateWeightEditsToRelaxVertices(
	TArray<int32> VertexIndices,
	TArray<float> VertexFalloffs,
	const float Strength,
	const int32 Iterations,
	FMultiBoneWeightEdits& InOutWeightEdits)
{
	if (!ensure(SmoothWeightsOp))
	{
		return;
	}
	
	for (int32 Iteration=0; Iteration < Iterations; ++Iteration)
	{
		for (int32 VertexIndex = 0; VertexIndex < VertexIndices.Num(); ++VertexIndex)
		{
			const int32 VertexID = VertexIndices[VertexIndex];
			constexpr float PercentPerIteration = 0.95f;
			const float UseFalloff = (VertexFalloffs.IsValidIndex(VertexIndex) ? VertexFalloffs[VertexIndex] * Strength : Strength) * PercentPerIteration;

			TMap<int32, float> FinalWeights;
			const bool bSmoothSuccess = SmoothWeightsOp->SmoothWeightsAtVertex(VertexID, UseFalloff, FinalWeights);
			if (!ensure(bSmoothSuccess))
			{
				continue;
			}

			// apply weight edits
			for (const TTuple<BoneIndex, float>& FinalWeight : FinalWeights)
			{
				// record an edit for this vertex, for this bone
				const int32 BoneIndex = FinalWeight.Key;
				const float NewWeight = FinalWeight.Value;
				constexpr bool bPruneInfluence = false;
				InOutWeightEdits.MergeSingleEdit(BoneIndex, VertexID, NewWeight, bPruneInfluence, Weights.PreChangeWeights);
			}
		}
	}
}

void USkinWeightsPaintTool::InitializeOctrees()
{
	if (!ensure(PreviewMesh && PreviewMesh->GetMesh()))
	{
		return;
	}

	// build octree for vertices
	VerticesOctree = MakeUnique<DynamicVerticesOctree>();
	VerticesOctree->Initialize(PreviewMesh->GetMesh(), true);

	// build octree for triangles
	TrianglesOctree = MakeUnique<DynamicTrianglesOctree>();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::InitTriangleOctree);
		
		TriangleOctreeFuture = Async(SkinPaintToolAsyncExecTarget, [&]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::InitTriangleOctreeRun);
			TrianglesOctree->Initialize(PreviewMesh->GetMesh());
		});
	}
}

void USkinWeightsPaintTool::InitializeSmoothWeightsOperator()
{
	if (!ensure(PreviewMesh && PreviewMesh->GetMesh()))
	{
		return;
	}

	// NOTE: this could probably be initialized lazily as it's only used with the relax brush
	const FDynamicMesh3* DynaMesh = PreviewMesh->GetMesh();
	SmoothWeightsDataSource = MakeUnique<FPaintToolWeightsDataSource>(&Weights, *DynaMesh);
	SmoothWeightsOp = MakeUnique<UE::Geometry::TSmoothBoneWeights<int32, float>>(DynaMesh, SmoothWeightsDataSource.Get());
	SmoothWeightsOp->MinimumWeightThreshold = MinimumWeightThreshold;
}

void USkinWeightsPaintTool::ApplyWeightEditsWithoutTransaction(const FMultiBoneWeightEdits& WeightEdits)
{
	// apply weights to current weights (triggers sparse deformation update)
	Weights.ApplyEditsToCurrentWeights(WeightEdits);
	// update list of vertices needing updated vertex colors
	WeightEdits.AddEditedVerticesToSet(VerticesToUpdateColor);
	// store changes in the transaction buffer, but keep it open
	auto PartialToFullVertexConverter = [this](int32 InVertexIndex){ return GetSelectionIsolator()->PartialToFullMeshVertexIndex(InVertexIndex); };
	ActiveChange->StoreMultipleWeightEdits(WeightEdits, PartialToFullVertexConverter);
}

void USkinWeightsPaintTool::ApplyWeightEditsAsTransaction(const FMultiBoneWeightEdits& WeightEdits, const FText& TransactionLabel)
{
	// clear the active change to start a new one
	BeginChange();
	// apply the weights
	ApplyWeightEditsWithoutTransaction(WeightEdits);
	// store active change in the transaction buffer and sync weight buffers
	EndChange(TransactionLabel);
}

void USkinWeightsPaintTool::UpdateCurrentBone(const FName& BoneName)
{
	CurrentBone = BoneName;
	bVertexColorsNeedUpdated = true;
	OnSelectionChanged.Broadcast();
}

BoneIndex USkinWeightsPaintTool::GetBoneIndexFromName(const FName BoneName) const
{
	if (BoneName == NAME_None)
	{
		return INDEX_NONE;		
	}
	const BoneIndex* Found = Weights.Deformer.BoneNameToIndexMap.Find(BoneName);
	return Found ? *Found : INDEX_NONE;
}

void USkinWeightsPaintTool::SetFocusInViewport() const
{
	if (PersonaModeManagerContext.IsValid())
	{
		PersonaModeManagerContext->SetFocusInViewport();	
	}
}

void USkinWeightsPaintTool::OnShutdown(EToolShutdownType ShutdownType)
{
	// shutdown must be performed on full mesh, so end isolated selection
	if (SelectionIsolator)
	{
		SelectionIsolator->RestoreFullMesh();
	}
	
	// save tool properties
	WeightToolProperties->SaveProperties(this);

	// shutdown polygon selection mechanic
	if (MeshSelector)
	{
		MeshSelector->Shutdown();
	}

	// apply changes to asset
	if (ShutdownType == EToolShutdownType::Accept)
	{
		// profile to edit
		const FName ActiveProfile = WeightToolProperties->GetActiveSkinWeightProfile();
	
		// apply the currently edited weights to the mesh description
		Weights.ApplyCurrentWeightsToMeshDescription(GetCurrentlyEditedMeshDescription());

		// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
		GetToolManager()->BeginUndoTransaction(LOCTEXT("SkinWeightsPaintTool", "Paint Skin Weights"));
		static constexpr bool bHaveTargetLOD = true;
		for (auto& [LOD, CleanedMesh] : EditedMeshes)
		{
			CleanedMesh.CopyWeightsToOriginalMesh(ActiveProfile);
			const FCommitMeshParameters CommitParams(bHaveTargetLOD, LOD);
			UE::ToolTarget::CommitMeshDescriptionUpdate(Target, &CleanedMesh.GetOriginalMeshDescription(), nullptr, CommitParams);	
		}
		GetToolManager()->EndUndoTransaction();
	}

	// restore viewport show flags and preview settings
	FPreviewProfileController PreviewProfileController;
	PreviewProfileController.SetActiveProfile(PreviewProfileToRestore);
	GetMutableDefault<UPersonaOptions>()->bShowBoneColors = bBoneColorsToRestore;

	if (EditorContext.IsValid())
	{
		EditorContext->UnbindFrom(this);
	}

	if (PersonaModeManagerContext.IsValid())
	{
		PersonaModeManagerContext->GetPersonaEditorModeManager()->DeactivateMode(FPersonaEditModes::SkeletonSelection);
	}

	GetWeightTransferManager()->Shutdown();
}

FEditorViewportClient* USkinWeightsPaintTool::GetViewportClient() const
{
	FEditorViewportClient* ViewportClient = nullptr;
	if (const UPersonaEditorModeManagerContext* ModeManagerContext = PersonaModeManagerContext.Get())
	{
		if (const IPersonaEditorModeManager* PersonaEditorModeManager = ModeManagerContext->GetPersonaEditorModeManager())
		{
			ViewportClient = PersonaEditorModeManager->GetHoveredViewportClient();
			if (!ViewportClient)
			{
				ViewportClient = PersonaEditorModeManager->GetFocusedViewportClient();
			}
		}
	}

	ensure(ViewportClient);
	return ViewportClient;
}

USkinWeightsPaintToolProperties* USkinWeightsPaintTool::GetWeightToolProperties() const
{
	return WeightToolProperties;
}

FCleanedEditMesh* USkinWeightsPaintTool::GetCurrentCleanedEditMesh() const
{
	if (EditedMeshes.Contains(CurrentlyEditedLOD))
	{
		return &EditedMeshes[CurrentlyEditedLOD];
	}

	return nullptr;
}

FMeshDescription* USkinWeightsPaintTool::GetCurrentlyEditedMeshDescription() const
{
	if (FCleanedEditMesh* CleanedEditMesh = GetCurrentCleanedEditMesh())
	{
		return &CleanedEditMesh->GetEditableMeshDescription();
	}
	
	return nullptr;
}

FDynamicMesh3* USkinWeightsPaintTool::GetCurrentlyEditedDynamicMesh() const
{
	if (FCleanedEditMesh* CleanedEditMesh = GetCurrentCleanedEditMesh())
	{
		return &CleanedEditMesh->GetEditableMesh();
	}
	
	return nullptr;
}

void USkinWeightsPaintTool::BeginChange()
{
	const EMeshLODIdentifier LOD = GetLODId(WeightToolProperties->ActiveLOD);
	const FName SkinProfile = WeightToolProperties->GetActiveSkinWeightProfile();
	ActiveChange = MakeUnique<FMeshSkinWeightsChange>(LOD, SkinProfile);
}

void USkinWeightsPaintTool::EndChange(const FText& TransactionLabel)
{
	// sync weight buffers
	Weights.SyncWeightBuffers();
	
	// record transaction
	GetToolManager()->BeginUndoTransaction(TransactionLabel);
	GetToolManager()->EmitObjectChange(this, MoveTemp(ActiveChange), TransactionLabel);
	GetToolManager()->EndUndoTransaction();

	// notify dependent systems
	OnWeightsChanged.Broadcast();
}

void USkinWeightsPaintTool::ExternalUpdateWeights(const int32 BoneIndex, const TMap<int32, float>& NewValues)
{
	for (const TTuple<int32, float>& Pair : NewValues)
	{
		// weights are always stored in transactions as full mesh indices
		// if we are in an isolated selection, we must convert them to the partial mesh for them to be applied
		const int32 VertexID = SelectionIsolator->FullToPartialMeshVertexIndex(Pair.Key);
		const float Weight = Pair.Value;
		Weights.SetWeightOfBoneOnVertex(BoneIndex, VertexID, Weight, Weights.CurrentWeights);
		Weights.SetWeightOfBoneOnVertex(BoneIndex, VertexID, Weight, Weights.PreChangeWeights);

		// queue update of vertex colors
		VerticesToUpdateColor.Add(VertexID);
	}

	Weights.UpdateIsBoneWeighted(BoneIndex);
}

void USkinWeightsPaintTool::ExternalUpdateSkinWeightLayer(const EMeshLODIdentifier InLOD, const FName InSkinWeightProfile)
{
	enum class ESkinWeightChangeState
	{
		SkinProfile,
		LOD,
		None
	} State = ESkinWeightChangeState::None;
	
	if (InSkinWeightProfile != WeightToolProperties->GetActiveSkinWeightProfile())
	{
		WeightToolProperties->ActiveSkinWeightProfile = InSkinWeightProfile;
		State = ESkinWeightChangeState::SkinProfile;
	}

	const FName LODName = GetLODName(InLOD);
	if (LODName != NAME_None && LODName != WeightToolProperties->ActiveLOD)
	{
		WeightToolProperties->ActiveLOD = LODName;
		State = ESkinWeightChangeState::LOD;
	}

	switch (State)
	{
	case ESkinWeightChangeState::SkinProfile:
		return OnActiveSkinWeightProfileChanged();
	case ESkinWeightChangeState::LOD:
		return OnActiveLODChanged();
	case ESkinWeightChangeState::None:
		default:
		break;
	}
}

void USkinWeightsPaintTool::ExternalAddInfluenceToVertices(
	const BoneIndex InfluenceToAdd,
	const TArray<VertexIndex>& Vertices)
{
	for (const VertexIndex VertexID : Vertices)
	{
		constexpr float DefaultWeight = 0.f;
		Weights.AddNewInfluenceToVertex(VertexID, InfluenceToAdd, DefaultWeight, Weights.CurrentWeights);
		Weights.AddNewInfluenceToVertex(VertexID, InfluenceToAdd, DefaultWeight, Weights.PreChangeWeights);
	}
}

void USkinWeightsPaintTool::ExternalRemoveInfluenceFromVertices(
	const BoneIndex InfluenceToRemove,
	const TArray<VertexIndex>& Vertices)
{
	for (const VertexIndex VertexID : Vertices)
	{
		Weights.RemoveInfluenceFromVertex(VertexID, InfluenceToRemove, Weights.CurrentWeights);
		Weights.RemoveInfluenceFromVertex(VertexID, InfluenceToRemove, Weights.PreChangeWeights);
	}
}

void FSkinMirrorData::EnsureMirrorDataIsUpdated(
    const TArray<FName>& BoneNames,
    const TMap<FName, BoneIndex>& BoneNameToIndexMap,
	const FReferenceSkeleton& RefSkeleton,
	const TArray<FVector>& RefPoseVertices,
	EAxis::Type InMirrorAxis,
	EMirrorDirection InMirrorDirection)
{
	if (bIsInitialized && InMirrorAxis == Axis && InMirrorDirection==Direction)
	{
		// already initialized, just re-use cached data
		return;
	}

	// need to re-initialize
	bIsInitialized = false;
	Axis = InMirrorAxis;
	Direction = InMirrorDirection;
	BoneMap.Reset();
	VertexMap.Reset();
	
	// build bone map for mirroring
	// TODO, provide some way to edit the mirror bone mapping, either by providing a UMirrorDataTable input or editing directly in the hierarchy view.
	for (FName BoneName : BoneNames)
	{
		FName MirroredBoneName = UMirrorDataTable::FindBestMirroredBone(BoneName, RefSkeleton, Axis);

		int32 BoneIndex = BoneNameToIndexMap[BoneName];
		int32 MirroredBoneIndex = BoneNameToIndexMap[MirroredBoneName];
		BoneMap.Add(BoneIndex, MirroredBoneIndex);
		
		// debug view bone mapping
		//UE_LOG(LogTemp, Log, TEXT("Bone    : %s"), *BoneName.ToString());
		//UE_LOG(LogTemp, Log, TEXT("Mirrored: %s"), *MirroredBoneName.ToString());
		//UE_LOG(LogTemp, Log, TEXT("-------"));
	}

	// build a spatial hash grid
	constexpr float HashGridCellSize = 2.0f; // the length of the cell size in the point hash grid
	UE::Geometry::TPointHashGrid3f<int32> VertHash(HashGridCellSize, INDEX_NONE);
	VertHash.Reserve(RefPoseVertices.Num());
	for (int32 VertexID = 0; VertexID < RefPoseVertices.Num(); ++VertexID)
	{
		VertHash.InsertPointUnsafe(VertexID, static_cast<FVector3f>(RefPoseVertices[VertexID]));
	}
	
	// generate a map of point IDs on the target side, to their equivalent vertex ID on the source side 
	for (int32 TargetVertexID = 0; TargetVertexID < RefPoseVertices.Num(); ++TargetVertexID)
	{
		const FVector& TargetPosition = RefPoseVertices[TargetVertexID];

		// we only generate the mirror map for vertices on the target side of the mirror plane
		// if the mirror plane is flipped, the map must be regenerated because there is no guarantee that the mirror map is symmetrical
		if (!IsPointOnTargetMirrorSide(TargetPosition))
		{
			continue;
		}

		// flip position across the mirror axis
		FVector3f MirroredPosition = FVector3f(TargetPosition);
		MirroredPosition[Axis-1] *= -1.f;

		// query spatial hash near mirrored position, gradually increasing search radius until at least 1 point is found
		TPair<int32, double> ClosestMirroredPoint = {INDEX_NONE, TNumericLimits<double>::Max()};
		float SearchRadius = HashGridCellSize;
		while(ClosestMirroredPoint.Key == INDEX_NONE)
		{
			ClosestMirroredPoint = VertHash.FindNearestInRadius(
				MirroredPosition,
				SearchRadius,
				[&RefPoseVertices, MirroredPosition](int32 VID)
				{
					return FVector3f::DistSquared(FVector3f(RefPoseVertices[VID]), MirroredPosition);
				});
			
			SearchRadius += HashGridCellSize;

			// forcibly break out if search radius gets bigger than the maximum search radius
			static float MaxSearchRadius = 15.f; // TODO we may want to expose this value to the user...
			if (SearchRadius >= MaxSearchRadius)
			{
				break;
			}
		}
		
		// disallow copying from vertices that are on the target side of the mirror plane
		if (ClosestMirroredPoint.Key != INDEX_NONE)
		{
			const FVector& SourcePointPosition = RefPoseVertices[ClosestMirroredPoint.Key];
			if (IsPointOnTargetMirrorSide(SourcePointPosition))
			{
				ClosestMirroredPoint.Key = INDEX_NONE;	
			}
		}
		
		// record the mirrored vertex ID for this vertex (may be INDEX_NONE)
		VertexMap.FindOrAdd(TargetVertexID, ClosestMirroredPoint.Key); // (TO, FROM)
	}
	
	bIsInitialized = true;
}

const TMap<int32, int32>& FSkinMirrorData::GetVertexMap() const
{
	ensure(bIsInitialized);
	return VertexMap;
}

bool FSkinMirrorData::IsPointOnTargetMirrorSide(const FVector& InPoint) const
{
	if (Direction == EMirrorDirection::PositiveToNegative && InPoint[Axis-1] >= 0.f)
	{
		return false; // target is negative side, but point is on positive side
	}
	if (Direction == EMirrorDirection::NegativeToPositive && InPoint[Axis-1] <= 0.f)
	{
		return false; // target is positive side, but vertex is on negative side
	}

	return true;
}

void USkinWeightsPaintTool::MirrorWeights(EAxis::Type Axis, EMirrorDirection Direction)
{
	check(Axis != EAxis::None);
	
	// get all ref pose vertices
	const TArray<FVector>& RefPoseVertices = Weights.Deformer.RefPoseVertexPositions;
	const FReferenceSkeleton& RefSkeleton = Weights.Deformer.Component->GetSkeletalMeshAsset()->GetRefSkeleton();

	// refresh mirror tables (cached / lazy generated)
	MirrorData.EnsureMirrorDataIsUpdated(
		Weights.Deformer.BoneNames,
		Weights.Deformer.BoneNameToIndexMap,
		RefSkeleton,
		RefPoseVertices,
		Axis,
		Direction);

	// get a reference to the mirror tables
	const TMap<BoneIndex, BoneIndex>& BoneMap = MirrorData.GetBoneMap();
	const TMap<VertexIndex, VertexIndex>& VertexMirrorMap = MirrorData.GetVertexMap(); // <Target, Source>

	// get the selected vertices
	const TArray<int32>& SelectedVertices = MeshSelector->GetSelectedVertices();
	// we need to convert selection to the equivalent target vertex indices (on the target side of the mirror plane)
	// if a vertex is already on the target side, great
	// if the user selected vertices on the source side, we convert them to the mirrored equivalent on the target side
	TSet<VertexIndex> TargetVertices;
	TArray<VertexIndex> MissingVertices;
	for (const VertexIndex SelectedVertex : SelectedVertices)
	{
		int32 TargetVertexID = INDEX_NONE;
		const bool bIsOnTargetSide = VertexMirrorMap.Contains(SelectedVertex);
		
		if (bIsOnTargetSide)
		{
			// vertex is located across the mirror plane (target side, to copy TO)
			TargetVertexID = SelectedVertex;
		}
		else
		{
			// vertex is located on the source side (to copy FROM), so we need to search for it's mirror target vertex
			for (const TPair<VertexIndex, VertexIndex>& ToFromPair : VertexMirrorMap)
			{
				if (ToFromPair.Value == SelectedVertex)
				{
					TargetVertexID = ToFromPair.Key;
					break;
				}
			}
		}

		// selected vertex did not have a mirrored equivalent
		if (TargetVertexID == INDEX_NONE)
		{
			if (bIsOnTargetSide)
			{
				MissingVertices.Add(TargetVertexID);
			}
			
			continue;
		}
		
		// add to the list of target vertices to set weights on
		TargetVertices.Add(TargetVertexID);
	}
	
	// spin through all target vertices to mirror and copy weights from source
	FMultiBoneWeightEdits WeightEditsFromMirroring;
	TMap<BoneIndex, float> NewBoneWeights;
	NewBoneWeights.Reserve(MAX_TOTAL_INFLUENCES);
	for (const VertexIndex TargetVertexID : TargetVertices)
	{
		const VertexIndex SourceVertexID = VertexMirrorMap[TargetVertexID];

		// store in the missing vertices array if no mirrored vertex was found
		if (SourceVertexID == INDEX_NONE)
		{
			MissingVertices.Add(TargetVertexID);
			continue;
		}

		// remove all weight on vertex
		for (const FVertexBoneWeight& TargetBoneWeight : Weights.PreChangeWeights[TargetVertexID])
		{
			static bool bPruneInfluence = true;
			static float NewWeight = 0.f;
			WeightEditsFromMirroring.MergeSingleEdit(TargetBoneWeight.BoneID, TargetVertexID, NewWeight, bPruneInfluence, Weights.PreChangeWeights);
		}

		// copy source weights, but with mirrored bones
		// NOTE: we have to normalize here because it's possible that while searching for mirrored bones, multiple source bones will be
		// mapped to the same target bone. When that happens, only the last applied weight on that bone would be recorded in an edit
		// If that happens, the final weight may not sum to 1.
		NewBoneWeights.Reset();
		for (const FVertexBoneWeight& SourceBoneWeight : Weights.PreChangeWeights[SourceVertexID])
		{
			const BoneIndex MirroredBoneIndex = BoneMap[SourceBoneWeight.BoneID];
			const float NewWeight = SourceBoneWeight.Weight;
			float& Weight = NewBoneWeights.FindOrAdd(MirroredBoneIndex, 0.0f);
			Weight += NewWeight;
		}
		NormalizeWeightMap(NewBoneWeights);

		// apply weight edits
		for (const TPair<BoneIndex, float>& NewBoneWeight : NewBoneWeights)
		{
			const BoneIndex BoneID = NewBoneWeight.Key;
			const float NewWeight = NewBoneWeight.Value;
			static bool bPruneInfluence = false;
			WeightEditsFromMirroring.MergeSingleEdit(BoneID, TargetVertexID, NewWeight, bPruneInfluence, Weights.PreChangeWeights);
		}
	}

	// apply the changes
	const FText TransactionLabel = LOCTEXT("MirrorWeightChange", "Mirror skin weights.");
	ApplyWeightEditsAsTransaction(WeightEditsFromMirroring, TransactionLabel);

	// warn if some vertices were not mirrored
	if (!MissingVertices.IsEmpty())
	{
		UE_LOG(LogMeshModelingToolsEditor, Log, TEXT("Mirror Skin Weights: %d vertex weights were not mirrored because a vertex was not found close enough to the mirrored location."), MissingVertices.Num());
	}
}

void USkinWeightsPaintTool::EditWeightsOnVertices(
	BoneIndex Bone,
	const float Value,
	const int32 Iterations,
	EWeightEditOperation EditOperation,
	const TArray<VertexIndex>& VertexIndices,
	const bool bShouldTransact)
{
	if (!(Weights.Deformer.InvCSRefPoseTransforms.IsValidIndex(Bone)))
	{
		return;
	}
	
	// create weight edits from setting the weight directly
	FMultiBoneWeightEdits DirectWeightEdits;
	const TArray<float> VertexFalloffs = {}; // no falloff

	if (EditOperation == EWeightEditOperation::Relax)
	{
		CreateWeightEditsToRelaxVertices(
			MeshSelector->GetSelectedVertices(),
			VertexFalloffs,
			Value,
			Iterations,
			DirectWeightEdits);
	}
	else
	{
		CreateWeightEditsForVertices(
				EditOperation,
				Bone,
				VertexIndices,
				VertexFalloffs,
				Value,
				DirectWeightEdits);
	}
	
	// apply the changes
	if (bShouldTransact)
	{
		const FText TransactionLabel = LOCTEXT("EditWeightChange", "Edit skin weights directly.");
		ApplyWeightEditsAsTransaction(DirectWeightEdits, TransactionLabel);
	}
	else
	{
		ApplyWeightEditsWithoutTransaction(DirectWeightEdits);
	}
}

void USkinWeightsPaintTool::PruneWeights(float Threshold, const TArray<BoneIndex>& BonesToPrune)
{
	// set weights below the given threshold to zero AND remove them as a recorded influence on that vertex
	FMultiBoneWeightEdits WeightEditsFromPrune;
	const TArray<VertexIndex>& VerticesToPrune = MeshSelector->GetSelectedVertices();
	for (const VertexIndex VertexID : VerticesToPrune)
	{
		TArray<BoneIndex> InfluencesToPrune;
		const VertexWeights& VertexWeights = Weights.CurrentWeights[VertexID];
		for (const FVertexBoneWeight& BoneWeight : VertexWeights)
		{
			if (BoneWeight.Weight < Threshold || BonesToPrune.Contains(BoneWeight.BoneID))
			{
				InfluencesToPrune.Add(BoneWeight.BoneID);

				// store a weight edit to remove this weight
				constexpr float NewWeight = 0.f;
				constexpr bool bPruneInfluence = true;
				WeightEditsFromPrune.MergeSingleEdit(BoneWeight.BoneID, VertexID, NewWeight, bPruneInfluence, Weights.PreChangeWeights);
			}
		}

		// remove the influence from the vertex in the CURRENT weights
		// this prevents ApplyWeightEdits() from ever using this influence as a dumping ground for normalization
		for (const BoneIndex InfluenceToPrune : InfluencesToPrune)
		{
			Weights.RemoveInfluenceFromVertex(VertexID, InfluenceToPrune, Weights.CurrentWeights);
		}

		// at this point, influences are pruned but this may leave the vertex non-normalized
		if (VertexWeights.IsEmpty())
		{
			// we pruned ALL influences from a vertex, so dump all weight on root
			constexpr BoneIndex RootBoneIndex = 0;
			constexpr bool bPruneInfluence = false;
			constexpr float NewWeight = 1.f;
			WeightEditsFromPrune.MergeSingleEdit(RootBoneIndex, VertexID, NewWeight, bPruneInfluence, Weights.PreChangeWeights);
		}
		else
		{
			// re-normalize all existing weights
			float TotalWeight = 0.f;
			for (const FVertexBoneWeight& BoneWeight : VertexWeights)
			{
				TotalWeight += BoneWeight.Weight;
			}

			// if there were no other weights to normalize (all zero), then simply evenly distribute the weight on the recorded influences
			const bool bNoOtherWeights = FMath::IsNearlyEqual(TotalWeight, 0.f);
			const float EvenlySplitWeight = 1.0f / VertexWeights.Num();

			// record weight edits to normalize the weight across the remaining influences
			for (const FVertexBoneWeight& BoneWeight : VertexWeights)
			{
				constexpr bool bPruneInfluence = false;
				const float NewWeight = bNoOtherWeights ? EvenlySplitWeight : BoneWeight.Weight / TotalWeight;
				WeightEditsFromPrune.MergeSingleEdit(BoneWeight.BoneID, VertexID, NewWeight, bPruneInfluence, Weights.PreChangeWeights);
			}
		}
	}

	// apply the changes
	const FText TransactionLabel = LOCTEXT("PruneWeightValuesChange", "Prune skin weights.");
	ApplyWeightEditsAsTransaction(WeightEditsFromPrune, TransactionLabel);
}

void USkinWeightsPaintTool::AverageWeights(const float Strength)
{
	// if strength is zero, don't do anything
	if (FMath::IsNearlyEqual(Strength, 0.0f))
	{
		return;
	}
	
	// get vertices to edit weights on
	const TArray<VertexIndex>& VerticesToAverage = MeshSelector->GetSelectedVertices();
	TMap<BoneIndex, float> AveragedWeights;
	AccumulateWeights(Weights.PreChangeWeights, VerticesToAverage, AveragedWeights);
	TruncateWeightMap(AveragedWeights);
	NormalizeWeightMap(AveragedWeights);

	// store weight edits to apply averaging to selected vertices
	FMultiBoneWeightEdits WeightEditsFromAveraging;
	
	// FULLY apply averaged weights to vertices if strength is 1.0
	if (FMath::IsNearlyEqual(Strength, 1.0f))
	{
		for (const VertexIndex VertexID : VerticesToAverage)
		{
			// remove influences not a part of the average results
			for (const FVertexBoneWeight& BoneWeight : Weights.PreChangeWeights[VertexID])
			{
				if (!AveragedWeights.Contains(BoneWeight.BoneID))
				{
					constexpr bool bPruneInfluence = false;
					constexpr float NewWeight = 0.f;
					WeightEditsFromAveraging.MergeSingleEdit(BoneWeight.BoneID, VertexID, NewWeight, bPruneInfluence, Weights.PreChangeWeights);
				}
			}

			// add influences from the averaging results
			for (const TTuple<BoneIndex, float>& AveragedWeight : AveragedWeights)
			{
				const BoneIndex IndexOfBone = AveragedWeight.Key;
				constexpr bool bPruneInfluence = false;
				const float NewWeight = AveragedWeight.Value;
				WeightEditsFromAveraging.MergeSingleEdit(IndexOfBone, VertexID, NewWeight, bPruneInfluence, Weights.PreChangeWeights);
			}
		}
	}
	else
	{
		// blend averaged weight with the existing weight based on the strength value
		const float OldWeightStrength = 1.0f - Strength;
		const float NewWeightStrength = Strength;
		for (const VertexIndex VertexID : VerticesToAverage)
		{
			// storage for final blended weights on this vertex
			TMap<BoneIndex, float> BlendedWeights;

			// scale the existing weights by OldWeightStrength
			for (const FVertexBoneWeight& BoneWeight : Weights.PreChangeWeights[VertexID])
			{
				BlendedWeights.Add(BoneWeight.BoneID, BoneWeight.Weight * OldWeightStrength);
			}
			
			// accumulate existing weights with the scaled averaged weights
			for (const TTuple<BoneIndex, float>& AveragedWeight : AveragedWeights)
			{
				if (BlendedWeights.Contains(AveragedWeight.Key))
				{
					BlendedWeights[AveragedWeight.Key] += AveragedWeight.Value * NewWeightStrength;
				}
				else
				{
					BlendedWeights.Add(AveragedWeight.Key, AveragedWeight.Value * NewWeightStrength);
				}
			}

			// enforce max influences and normalize
			TruncateWeightMap(BlendedWeights);
			NormalizeWeightMap(BlendedWeights);
			
			// apply blended weights to this vertex
			for (const TTuple<BoneIndex, float>& BlendedWeight : BlendedWeights)
			{
				const BoneIndex BoneID = BlendedWeight.Key;
				constexpr bool bPruneInfluence = false;
				const float NewWeight = BlendedWeight.Value;
				WeightEditsFromAveraging.MergeSingleEdit(BoneID, VertexID, NewWeight, bPruneInfluence, Weights.PreChangeWeights);
			}
		}
	}

	// apply the changes
	const FText TransactionLabel = LOCTEXT("AverageWeightValuesChange", "Average skin weights.");
	ApplyWeightEditsAsTransaction(WeightEditsFromAveraging, TransactionLabel);
}

void USkinWeightsPaintTool::NormalizeWeights()
{
	// re-set a weight on each vertex to force normalization
	FMultiBoneWeightEdits WeightEditsFromNormalization;
	const TArray<VertexIndex> VerticesToNormalize = MeshSelector->GetSelectedVertices();
	for (const VertexIndex VertexID : VerticesToNormalize)
	{
		const VertexWeights& VertexWeights = Weights.CurrentWeights[VertexID];
		if (VertexWeights.IsEmpty())
		{
			// ALL influences have been pruned from vertex, so assign it to the root
			constexpr BoneIndex RootBoneIndex = 0;
			constexpr float FullWeight = 1.f;
			Weights.CreateWeightEditForVertex(RootBoneIndex, VertexID, FullWeight,WeightEditsFromNormalization);
		}
		else
		{
			// set first weight to current value, just to force re-normalization
			const FVertexBoneWeight& BoneWeight = VertexWeights[0];
			Weights.CreateWeightEditForVertex(BoneWeight.BoneID, VertexID, BoneWeight.Weight,WeightEditsFromNormalization);
		}
	}

	// apply the changes
	const FText TransactionLabel = LOCTEXT("NormalizeWeightValuesChange", "Normalize skin weights.");
	ApplyWeightEditsAsTransaction(WeightEditsFromNormalization, TransactionLabel);
}

void USkinWeightsPaintTool::HammerWeights()
{
	// get selected vertices
	const TArray<VertexIndex> SelectedVerts = MeshSelector->GetSelectedVertices();
	if (SelectedVerts.IsEmpty())
	{
		return;
	}

	// reset mesh to ref pose so that Dijkstra path lengths are not deformed
	Weights.Deformer.SetToRefPose(this);
	
	// find 1-ring neighbors of the current selection, lets call these "Surrounding" vertices
	const FDynamicMesh3* Mesh = PreviewMesh->GetMesh();
	TSet<int32> SurroundingVertices;
	for (const int32 SelectedVertex : SelectedVerts)
	{
		for (const int32 NeighborIndex : Mesh->VtxVerticesItr(SelectedVertex))
		{
			if (!SelectedVerts.Contains(NeighborIndex))
			{
				SurroundingVertices.Add(NeighborIndex);
			}
		}
	}

	// seed a Dijkstra path finder with the surrounding vertices
	UE::Geometry::TMeshDijkstra<FDynamicMesh3> PathFinder(Mesh);
	TArray<UE::Geometry::TMeshDijkstra<FDynamicMesh3>::FSeedPoint> SeedPoints;
	for (const int32 SurroundingVertex : SurroundingVertices)
	{
		SeedPoints.Add({ SurroundingVertex, SurroundingVertex, 0 });
	}
	PathFinder.ComputeToMaxDistance(SeedPoints, TNumericLimits<double>::Max());

	// create set of weight edits that hammer the weights
	FMultiBoneWeightEdits HammerWeightEdits;
	
	// for each selected vertex, find the nearest surrounding vertex and copy it's weights
	TArray<int32> VertexPath;
	for (const int32 SelectedVertex : SelectedVerts)
	{
		// find the closest surrounding vertex to this selected vertex
		if (!PathFinder.FindPathToNearestSeed(SelectedVertex, VertexPath))
		{
			continue;
		}
		const int32 ClosestVertex = VertexPath.Last();

		// remove all current weights (pruning since this operation completely replaces the weight)
		for (const FVertexBoneWeight& BoneWeight : Weights.PreChangeWeights[SelectedVertex])
		{
			constexpr float NewWeight = 0.f;
			constexpr bool bPruneInfluence = true;
			HammerWeightEdits.MergeSingleEdit(BoneWeight.BoneID, SelectedVertex, NewWeight, bPruneInfluence, Weights.PreChangeWeights);
		}

		// replace weights with values from the closest vertex
		for (const FVertexBoneWeight& BoneWeight : Weights.PreChangeWeights[ClosestVertex])
		{
			const float NewWeight = BoneWeight.Weight;
			constexpr bool bPruneInfluence = false;
			HammerWeightEdits.MergeSingleEdit(BoneWeight.BoneID, SelectedVertex, NewWeight, bPruneInfluence, Weights.PreChangeWeights);
		}
	}
	
	// apply the changes
	const FText TransactionLabel = LOCTEXT("HammerWeightsChange", "Hammer skin weights.");
	ApplyWeightEditsAsTransaction(HammerWeightEdits, TransactionLabel);

	// put the mesh back in it's current pose
	Weights.Deformer.SetAllVerticesToBeUpdated();
}

const FString USkinWeightsPaintTool::CopyPasteWeightsIdentifier = TEXT("UNREAL_VERTEX_WEIGHTS:");

void USkinWeightsPaintTool::CopyWeights()
{
	// get all the selected vertices
	const TArray<VertexIndex>& VerticesToCopy = MeshSelector->GetSelectedVertices();
	if (VerticesToCopy.IsEmpty())
	{
		const FText NotificationText = FText::FromString(TEXT("No vertices were selected. No weights were copied to the clipboard."));
		ShowEditorMessage(ELogVerbosity::Error, NotificationText);
		return;
	}

	// get a map of the average weight of all the selected vertices (accumulating and normalizing produces an average)
	TMap<BoneIndex, float> WeightsToCopy;
	AccumulateWeights(Weights.PreChangeWeights, VerticesToCopy, WeightsToCopy);
	TruncateWeightMap(WeightsToCopy);
	NormalizeWeightMap(WeightsToCopy);

	//
	// serialize and store in the clipboard
	//

	// create a JSON array to hold our pairs
	TArray<TSharedPtr<FJsonValue>> JSONArray;
	for (const TPair<BoneIndex, float>& BoneWeight : WeightsToCopy)
	{
		TSharedPtr<FJsonObject> WeightJSON = MakeShared<FJsonObject>();
		WeightJSON->SetStringField("BoneName", GetBoneNameFromIndex(BoneWeight.Key).ToString());
		WeightJSON->SetNumberField("Weight", BoneWeight.Value);
		JSONArray.Add(MakeShared<FJsonValueObject>(WeightJSON));
	}

	// convert JSON array to string
	FString JSONString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JSONString);
	FJsonSerializer::Serialize(JSONArray, Writer);

	// add a custom prefix to identify our data
	const FString ClipboardString = CopyPasteWeightsIdentifier + JSONString;
        
	// copy to clipboard
	FPlatformApplicationMisc::ClipboardCopy(*ClipboardString);

	// notify user
	const FText NotificationText = FText::FromString("Copied weights to clipboard.");
	ShowEditorMessage(ELogVerbosity::Log, NotificationText);
}

void USkinWeightsPaintTool::PasteWeights()
{
	const TArray<VertexIndex>& VerticesToPasteOn = MeshSelector->GetSelectedVertices();
	if (VerticesToPasteOn.IsEmpty())
	{
		const FText NotificationText = FText::FromString("No vertices were selected. No weights were pasted.");
		ShowEditorMessage(ELogVerbosity::Error, NotificationText);
		return;
	}
	
	// get the clipboard content and check if it matches our format
	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);
	if (!ClipboardContent.StartsWith(CopyPasteWeightsIdentifier))
	{
		const FText NotificationText = FText::FromString("Failed to paste vertex weights from clipboard. Expected header not found.");
		ShowEditorMessage(ELogVerbosity::Fatal, NotificationText);
		return;
	}

	// deserialize the string into JSON and parse it as name/weight pairs
	TMap<BoneIndex, float> LoadedWeights;
	const FString JsonString = ClipboardContent.RightChop(CopyPasteWeightsIdentifier.Len());
	TSharedPtr<FJsonValue> JsonParsed;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	bool bFoundImproperlyFormattedData = false;
	bool bFoundNonExistantBone = false;
	if (FJsonSerializer::Deserialize(Reader, JsonParsed) && JsonParsed.IsValid())
	{
		TArray<TSharedPtr<FJsonValue>> JsonArray = JsonParsed->AsArray();
		for (const TSharedPtr<FJsonValue>& JsonValue : JsonArray)
		{
			TSharedPtr<FJsonObject> JsonObject = JsonValue->AsObject();
			if (!JsonObject.IsValid())
			{
				continue;
			}
			FString NameString;
			double ValueAsDouble;
			if (!(JsonObject->TryGetStringField(TEXT("BoneName"), NameString) && JsonObject->TryGetNumberField(TEXT("Weight"), ValueAsDouble)))
			{
				bFoundImproperlyFormattedData = true;
				continue;
			}
			
			BoneIndex IndexOfBone = GetBoneIndexFromName(FName(NameString));
			float Weight = ValueAsDouble;
			if (IndexOfBone == INDEX_NONE)
			{
				bFoundNonExistantBone = true;
				UE_LOG(LogMeshModelingToolsEditor, Warning, TEXT("Pasted weights referenced a missing bone: %s"), *NameString);
				continue;
			}

			// store the loaded weight value
			LoadedWeights.Add(IndexOfBone, Weight);
		}
	}

	if (bFoundNonExistantBone)
	{
		const FText NotificationText = FText::FromString("Pasted weights referenced a missing bone. See output for details.");
		ShowEditorMessage(ELogVerbosity::Warning, NotificationText);
	}
	
	if (bFoundImproperlyFormattedData)
	{
		const FText NotificationText = FText::FromString("Found improperly formatted data while pasting weights from clipboard. Expected array of (BoneName,Weight) pairs.");
		ShowEditorMessage(ELogVerbosity::Warning, NotificationText);
	}

	if (LoadedWeights.IsEmpty())
	{
		const FText NotificationText = FText::FromString("No weights were loaded from the clipboard. Paste aborted.");
		ShowEditorMessage(ELogVerbosity::Fatal, NotificationText);
		return;
	}

	// truncate and normalize (these weights could have come from anywhere)
	TruncateWeightMap(LoadedWeights);
	NormalizeWeightMap(LoadedWeights);

	// create set of weight edits that paste the weights
	FMultiBoneWeightEdits PasteWeightEdits;
	for (const int32 SelectedVertex : VerticesToPasteOn)
	{
		// remove all current weights
		for (const FVertexBoneWeight& BoneWeight : Weights.PreChangeWeights[SelectedVertex])
		{
			// when pasting weights, we want a complete replacement, so we prune everything first
			constexpr bool bPruneInfluence = true;
			constexpr float ZeroWeight = 0.f;
			PasteWeightEdits.MergeSingleEdit(BoneWeight.BoneID, SelectedVertex, ZeroWeight, bPruneInfluence, Weights.PreChangeWeights);
		}

		// add weights from clipboard
		for (const TPair<BoneIndex, float>& LoadedWeight : LoadedWeights)
		{
			const BoneIndex BoneID = LoadedWeight.Key;
			const float NewWeight = LoadedWeight.Value;
			constexpr bool bPruneInfluence = false;
			PasteWeightEdits.MergeSingleEdit(BoneID, SelectedVertex, NewWeight, bPruneInfluence, Weights.PreChangeWeights);
		}
	}
	
	// apply the changes
	const FText TransactionLabel = LOCTEXT("PasteWeightsChange", "Paste skin weights.");
	ApplyWeightEditsAsTransaction(PasteWeightEdits, TransactionLabel);

	// notify user
	const FText NotificationText = FText::FromString("Pasted weights.");
	ShowEditorMessage(ELogVerbosity::Log, NotificationText);
}

void USkinWeightsPaintTool::TruncateWeightMap(TMap<BoneIndex, float>& InOutWeights)
{
	// sort influences by total weight
	InOutWeights.ValueSort([](const float& A, const float& B)
	{
		return A > B;
	});

	// truncate to MaxInfluences
	int32 Index = 0;
	for (TMap<BoneIndex, float>::TIterator It(InOutWeights); It; ++It)
	{
		if (Index >= MAX_TOTAL_INFLUENCES)
		{
			It.RemoveCurrent();
		}
		else
		{
			++Index;
		}
	}
}

void USkinWeightsPaintTool::NormalizeWeightMap(TMap<BoneIndex, float>& InOutWeights)
{
	// normalize remaining influences
	float TotalWeight = 0.f;
	for (const TTuple<BoneIndex, float>& Weight : InOutWeights)
	{
		TotalWeight += Weight.Value;
	}
	for (TTuple<BoneIndex, float>& Weight : InOutWeights)
	{
		Weight.Value /= TotalWeight > SMALL_NUMBER ? TotalWeight : 1.f;
	}
}

void USkinWeightsPaintTool::AccumulateWeights(
	const TArray<SkinPaintTool::VertexWeights>& AllWeights,
	const TArray<VertexIndex>& VerticesToAccumulate,
	TMap<BoneIndex, float>& OutWeights)
{
	for (const VertexIndex VertexID : VerticesToAccumulate)
	{
		for (const FVertexBoneWeight& BoneWeight : AllWeights[VertexID])
		{
			float& AccumulatedWeight = OutWeights.FindOrAdd(BoneWeight.BoneID);
			AccumulatedWeight += BoneWeight.Weight;
		}
	}
}

void USkinWeightsPaintTool::HandleSkeletalMeshModified(const TArray<FName>& InBoneNames, const ESkeletalMeshNotifyType InNotifyType)
{
	switch (InNotifyType)
	{
	case ESkeletalMeshNotifyType::BonesAdded:
		break;
	case ESkeletalMeshNotifyType::BonesRemoved:
		break;
	case ESkeletalMeshNotifyType::BonesMoved:
		{
			// TODO update only vertices weighted to modified bones (AND CHILDREN!?)
			Weights.Deformer.SetAllVerticesToBeUpdated();
			break;	
		}
	case ESkeletalMeshNotifyType::BonesSelected:
		{
			// store selected bones
			SelectedBoneNames = InBoneNames;
			PendingCurrentBone = InBoneNames.IsEmpty() ? NAME_None : InBoneNames[0];

			// update selected bone indices from names
			SelectedBoneIndices.Reset();
			for (const FName SelectedBoneName : SelectedBoneNames)
			{
				SelectedBoneIndices.Add(GetBoneIndexFromName(SelectedBoneName));
			}
		}
		break;
	case ESkeletalMeshNotifyType::BonesRenamed:
		break;
	case ESkeletalMeshNotifyType::HierarchyChanged:
		break;
	default:
		checkNoEntry();
	}
}

void USkinWeightsPaintTool::OnActiveLODChanged()
{
	const USkeletalMeshComponent* Component = GetSkeletalMeshComponent(Target);
	if (!ensure(Component))
	{
		return;
	}

	if (SelectionIsolator->IsSelectionIsolated())
	{
		SelectionIsolator->RestoreFullMesh();
	}

	// apply previous changes
	Weights.ApplyCurrentWeightsToMeshDescription(GetCurrentlyEditedMeshDescription());

	// update current mesh using the new LOD
	const EMeshLODIdentifier ActiveLODID = GetLODId(WeightToolProperties->ActiveLOD);
	const FGetMeshParameters Params(true, ActiveLODID);
	FCleanedEditMesh* CleanedEditMesh = EditedMeshes.Find(ActiveLODID);
	if (!CleanedEditMesh)
	{
		const FDynamicMesh3 DynamicMesh = UE::ToolTarget::GetDynamicMeshCopy(Target, Params);
		const FMeshDescription* MeshDescription = UE::ToolTarget::GetMeshDescription(Target, Params);
		CleanedEditMesh = &EditedMeshes.Add(ActiveLODID, FCleanedEditMesh(DynamicMesh, *MeshDescription));
	}
	
	// assign new LOD as the currently edited one
	CurrentlyEditedLOD = ActiveLODID;

	// reinitialize all mesh data structures
	UpdateCurrentlyEditedMesh(Component, *GetCurrentlyEditedDynamicMesh(), *GetCurrentlyEditedMeshDescription());
}

void USkinWeightsPaintTool::OnActiveSkinWeightProfileChanged()
{
	const USkeletalMeshComponent* SkeletalMeshComponent = GetSkeletalMeshComponent(Target);
	if (!SkeletalMeshComponent)
	{
		return;
	}

	WeightToolProperties->bShowNewProfileName = WeightToolProperties->ActiveSkinWeightProfile == CreateNewName();

	if (SelectionIsolator->IsSelectionIsolated())
	{
		SelectionIsolator->RestoreFullMesh();
	}

	if (WeightToolProperties->bShowNewProfileName)
	{
		if (!IsProfileValid(WeightToolProperties->NewSkinWeightProfile))
		{
			GetOrCreateSkinWeightsAttribute(*GetCurrentlyEditedMeshDescription(), WeightToolProperties->NewSkinWeightProfile);
		} 
	}
	
	if (!IsProfileValid(WeightToolProperties->GetActiveSkinWeightProfile()))
	{
		WeightToolProperties->ActiveSkinWeightProfile = FSkeletalMeshAttributesShared::DefaultSkinWeightProfileName;
		WeightToolProperties->bShowNewProfileName = false;
	}
	
	if (WeightToolProperties->GetActiveSkinWeightProfile() == Weights.Profile)
	{
		return;
	}

	// apply previous changes
	Weights.ApplyCurrentWeightsToMeshDescription(GetCurrentlyEditedMeshDescription());

	// re-init Weights with new skin profile
	Weights = FSkinToolWeights();
	Weights.Profile = WeightToolProperties->GetActiveSkinWeightProfile();
	Weights.InitializeSkinWeights(SkeletalMeshComponent, GetCurrentlyEditedMeshDescription());
	bVertexColorsNeedUpdated = true;
}

void USkinWeightsPaintTool::OnNewSkinWeightProfileChanged()
{
	if (WeightToolProperties->bShowNewProfileName && WeightToolProperties->NewSkinWeightProfile != Weights.Profile)
	{
		const bool bRenamed = RenameSkinWeightsAttribute(*GetCurrentlyEditedMeshDescription(), Weights.Profile, WeightToolProperties->NewSkinWeightProfile);
		if (ensure(bRenamed))
		{
			Weights.Profile = WeightToolProperties->NewSkinWeightProfile;
		}
	}
}

bool USkinWeightsPaintTool::IsProfileValid(const FName InProfileName) const
{
	const USkeletalMeshComponent* SkeletalMeshComponent = GetSkeletalMeshComponent(Target);
	if (!SkeletalMeshComponent)
	{
		return false;
	}

	// check current MeshDescription
	const FSkeletalMeshConstAttributes MeshAttribs(*GetCurrentlyEditedMeshDescription());
	const TArray<FName> MeshDescProfiles = MeshAttribs.GetSkinWeightProfileNames();
	const bool bHasProfile = MeshDescProfiles.ContainsByPredicate([InProfileName](const FName Name)
	{
		return Name == InProfileName;
	});
	
	return bHasProfile;
}

void USkinWeightsPaintTool::ToggleEditingMode()
{
	Weights.Deformer.SetAllVerticesToBeUpdated();

	// toggle brush mode
	SetBrushEnabled(WeightToolProperties->EditingMode == EWeightEditMode::Brush);

	// toggle which mesh we're selecting and what components (vert/edge/face)
	UpdateSelectorState();

	// toggle bone select mode
	// this mode is set to be compatible with the 
	if (PersonaModeManagerContext.IsValid())
	{
		if (WeightToolProperties->EditingMode == EWeightEditMode::Bones)
		{
			PersonaModeManagerContext->GetPersonaEditorModeManager()->ActivateMode(FPersonaEditModes::SkeletonSelection);	
		}
		else
		{
			PersonaModeManagerContext->GetPersonaEditorModeManager()->DeactivateMode(FPersonaEditModes::SkeletonSelection);	
		}
	}

	SetFocusInViewport();
}

void USkinWeightsPaintTool::UpdateSelectorState() const
{
	const USkinWeightsPaintToolProperties* ToolProperties = GetWeightToolProperties();
	
	const bool bIsMeshEditing = ToolProperties->EditingMode == EWeightEditMode::Mesh;
	const bool bHasSourceMesh = TransferManager->GetPreviewMesh() != nullptr;
	const bool bWasSetToSelectSource = ToolProperties->MeshSelectMode == EMeshTransferOption::Source;
	const bool bEnableSourceMeshSelector = bIsMeshEditing && bWasSetToSelectSource && bHasSourceMesh;
	const bool bEnableTargetMeshSelector = bIsMeshEditing && !bWasSetToSelectSource;
	
	// source mesh selector
	UWeightToolMeshSelector* SourceMeshSelector = TransferManager->GetMeshSelector();
	SourceMeshSelector->SetIsEnabled(bEnableSourceMeshSelector);
	
	// main mesh selector
	MeshSelector->SetIsEnabled(bEnableTargetMeshSelector);

	// update component mode
	MeshSelector->SetComponentSelectionMode(WeightToolProperties->ComponentSelectionMode);
	SourceMeshSelector->SetComponentSelectionMode(WeightToolProperties->ComponentSelectionMode);
}

UWeightToolMeshSelector* USkinWeightsPaintTool::GetMainMeshSelector()
{
	return MeshSelector;
}

UWeightToolMeshSelector* USkinWeightsPaintTool::GetActiveMeshSelector()
{
	if (WeightToolProperties->MeshSelectMode == EMeshTransferOption::Source)
	{
		return GetWeightTransferManager()->GetMeshSelector();
	}

	return MeshSelector;
}

void UWeightToolSelectionIsolator::InitialSetup(USkinWeightsPaintTool* InTool)
{
	WeightTool = InTool;
}

void UWeightToolSelectionIsolator::UpdateIsolatedSelection()
{
	// this is queued to run on Tick() because modifying the mesh from other threads can cause the tool's Render() to be out of sync
	if (bIsolatedMeshNeedsUpdated)
	{
		if (CurrentlyIsolatedTriangles.IsEmpty())
		{
			RestoreFullMesh();	
		}
		else
		{
			CreatePartialMesh();
		}
		
		bIsolatedMeshNeedsUpdated = false;
	}
}

bool UWeightToolSelectionIsolator::IsSelectionIsolated() const
{
	return PartialMeshDescription.IsValid();
}

void UWeightToolSelectionIsolator::IsolateSelectionAsTransaction()
{
	const FText& TransactionLabel = LOCTEXT("IsolateSelectTransaction", "Isolate Selection");

	TUniquePtr<FIsolateSelectionChange> ActiveChange = MakeUnique<FIsolateSelectionChange>();
	ActiveChange->IsolatedTrianglesBefore = GetIsolatedTriangles();
	WeightTool->GetMainMeshSelector()->GetSelectedTriangles(ActiveChange->IsolatedTrianglesAfter);

	UpdateIsolatedSelection();
	SetTrianglesToIsolate(ActiveChange->IsolatedTrianglesAfter);
	
	UInteractiveToolManager* ToolManager = WeightTool->GetToolManager();
	ToolManager->BeginUndoTransaction(TransactionLabel);
	ToolManager->EmitObjectChange(WeightTool, MoveTemp(ActiveChange), TransactionLabel);
	ToolManager->EndUndoTransaction();
}

void UWeightToolSelectionIsolator::UnIsolateSelectionAsTransaction()
{
	const FText& TransactionLabel =  LOCTEXT("ShowAllTransaction", "Show All");

	TUniquePtr<FIsolateSelectionChange> ActiveChange = MakeUnique<FIsolateSelectionChange>();
	ActiveChange->IsolatedTrianglesBefore = GetIsolatedTriangles();
	ActiveChange->IsolatedTrianglesAfter = {};

	UpdateIsolatedSelection();
	SetTrianglesToIsolate(ActiveChange->IsolatedTrianglesAfter);
	
	UInteractiveToolManager* ToolManager = WeightTool->GetToolManager();
	ToolManager->BeginUndoTransaction(TransactionLabel);
	ToolManager->EmitObjectChange(WeightTool, MoveTemp(ActiveChange), TransactionLabel);
	ToolManager->EndUndoTransaction();
}

void UWeightToolSelectionIsolator::SetTrianglesToIsolate(const TArray<int32>& TrianglesToIsolate)
{
	if (bIsolatedMeshNeedsUpdated)
	{
		// cannot queue up multiple changes
		// we must allow previous selection isolations to be applied to keep the weight buffer in-sync
		return;
	}
	
	// record the triangles we are isolating (if empty, this will restore the full mesh)
	CurrentlyIsolatedTriangles = TrianglesToIsolate;
	// queue an update for next tick
	bIsolatedMeshNeedsUpdated = true;
}

void UWeightToolSelectionIsolator::RestoreFullMesh()
{
	const USkeletalMeshComponent* SkeletalMeshComponent = GetSkeletalMeshComponent(WeightTool->GetTarget());
	if (!SkeletalMeshComponent)
	{
		// this can happen at shutdown
		return;
	}

	if (!PartialMeshDescription)
	{
		// nothing hidden
		return;
	}

	FSkinToolWeights& Weights = WeightTool->GetWeights();

	// apply partial mesh weights to partial mesh description
	Weights.ApplyCurrentWeightsToMeshDescription(PartialMeshDescription.Get());

	// reinitialize with full mesh
	FMeshDescription* CurrentlyEditedMesh = WeightTool->GetCurrentlyEditedMeshDescription();
	FDynamicMesh3* CurrentlyEditedDynaMesh = WeightTool->GetCurrentlyEditedDynamicMesh();
	// this resizes Weights to the full mesh size
	WeightTool->UpdateCurrentlyEditedMesh(SkeletalMeshComponent, *CurrentlyEditedDynaMesh, *CurrentlyEditedMesh);

	// copy the remapped weights back to the full mesh
	const FSkeletalMeshConstAttributes MeshAttribs(*PartialMeshDescription.Get());
	const FName ActiveProfile = WeightTool->GetWeightToolProperties()->GetActiveSkinWeightProfile();
	const FSkinWeightsVertexAttributesConstRef AllVertexWeights = MeshAttribs.GetVertexSkinWeights(ActiveProfile);
	const int32 NumVerticesInPartialMesh = PartialMeshDescription.Get()->Vertices().Num();
	UE::Geometry::FNonManifoldMappingSupport NonManifoldMapping(*CurrentlyEditedDynaMesh);
	for (int32 VertexIndexPartial = 0; VertexIndexPartial < NumVerticesInPartialMesh; VertexIndexPartial++)
	{
		// get the equivalent vertex on the full mesh
		int32 VertexIndexFull = PartialSubMesh.MapVertexToBaseMesh(VertexIndexPartial);
		VertexIndexFull = NonManifoldMapping.GetOriginalNonManifoldVertexID(VertexIndexFull);

		// clear all the weights on this vertex
		Weights.CurrentWeights[VertexIndexFull].Reset();
		// replace with weights from partial mesh
		const FVertexBoneWeightsConst& VertexWeightsPartial = AllVertexWeights.Get(VertexIndexPartial);
		for (const UE::AnimationCore::FBoneWeight& SingleBoneWeight : VertexWeightsPartial)
		{
			const FBoneIndexType BoneID = SingleBoneWeight.GetBoneIndex();
			const float Weight = SingleBoneWeight.GetWeight();
			const FVector& RefPoseVertexPosition = Weights.Deformer.RefPoseVertexPositions[VertexIndexFull];
			const FTransform& InvRefPoseBoneTransform = Weights.Deformer.InvCSRefPoseTransforms[BoneID];
			const FVector VertexInBoneSpace = InvRefPoseBoneTransform.TransformPosition(RefPoseVertexPosition);
			// store the new influence on this vertex
			Weights.CurrentWeights[VertexIndexFull].Emplace(BoneID, VertexInBoneSpace, Weight);
		}
	}
	// sync both weight buffers
	Weights.PreChangeWeights = Weights.CurrentWeights;
	// apply full mesh weights to full mesh description
	Weights.ApplyCurrentWeightsToMeshDescription(CurrentlyEditedMesh);

	// restore selection (a convenience for the user that allows for easily adjusting the isolation by going back/forth as needed)
	if (UPolygonSelectionMechanic* SelectionMechanic = WeightTool->GetMainMeshSelector()->GetSelectionMechanic())
	{
		SelectionMechanic->SetSelection_AsTriangleTopology(IsolatedSelectionToRestoreVertices);
		SelectionMechanic->SetSelection_AsTriangleTopology(IsolatedSelectionToRestoreEdges);
		SelectionMechanic->SetSelection_AsTriangleTopology(IsolatedSelectionToRestoreFaces);
	}

	PartialMeshDescription = nullptr;
	CurrentlyIsolatedTriangles.Reset();
}

int32 UWeightToolSelectionIsolator::PartialToFullMeshVertexIndex(int32 PartialMeshVertexIndex) const
{
	if (!PartialMeshDescription)
	{
		return PartialMeshVertexIndex;
	}

	return PartialSubMesh.MapVertexToBaseMesh(PartialMeshVertexIndex);
}

int32 UWeightToolSelectionIsolator::FullToPartialMeshVertexIndex(int32 FullMeshVertexIndex) const
{
	if (!PartialMeshDescription)
	{
		return FullMeshVertexIndex;
	}

	return PartialSubMesh.MapVertexToSubmesh(FullMeshVertexIndex);
}

void UWeightToolSelectionIsolator::CreatePartialMesh()
{
	UPolygonSelectionMechanic* SelectionMechanic = WeightTool->GetMainMeshSelector()->GetSelectionMechanic();
	if (!ensure(SelectionMechanic))
	{
		return;
	}

	const USkeletalMeshComponent* SkeletalMeshComponent = GetSkeletalMeshComponent(WeightTool->GetTarget());
	if (!ensure(SkeletalMeshComponent))
	{
		return;
	}

	if (!ensure(!CurrentlyIsolatedTriangles.IsEmpty()))
	{
		return;
	}

	FMeshDescription* EditedMeshDesc = WeightTool->GetCurrentlyEditedMeshDescription();
	FDynamicMesh3* EditedDynaMesh = WeightTool->GetCurrentlyEditedDynamicMesh();
	if (!ensure(EditedMeshDesc && EditedDynaMesh))
	{
		return;
	}
	
	// get the weights
	FSkinToolWeights& Weights = WeightTool->GetWeights();

	// apply the current weights to the full mesh description
	Weights.ApplyCurrentWeightsToMeshDescription(EditedMeshDesc);
	
	// put into ref pose, BEFORE copying the mesh, so that submesh deformer initializes with vertices in ref pose
	Weights.Deformer.SetToRefPose(WeightTool);

	// store selection to be restored
	IsolatedSelectionToRestoreVertices.Reset();
	IsolatedSelectionToRestoreEdges.Reset();
	IsolatedSelectionToRestoreFaces.Reset();
	IsolatedSelectionToRestoreVertices.ElementType = UE::Geometry::EGeometryElementType::Vertex;
	IsolatedSelectionToRestoreEdges.ElementType = UE::Geometry::EGeometryElementType::Edge;
	IsolatedSelectionToRestoreFaces.ElementType = UE::Geometry::EGeometryElementType::Face;
	SelectionMechanic->GetSelection_AsTriangleTopology(IsolatedSelectionToRestoreVertices);
	SelectionMechanic->GetSelection_AsTriangleTopology(IsolatedSelectionToRestoreEdges);
	SelectionMechanic->GetSelection_AsTriangleTopology(IsolatedSelectionToRestoreFaces);

	// copy the full mesh description data to the full dynamic mesh
	// 
	// NOTE: this copies skin weights to the dynamic mesh.
	//	 	 this is important because the partial mesh is created from the dynamic mesh NOT the mesh description where the weight have been applied
	// NOTE: this conversion also restores NonManifold mappings to the converted dynamic mesh
	EditedDynaMesh->Clear();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(EditedMeshDesc, *EditedDynaMesh);
	
	// create a partial sub-mesh from a subset of triangles on the dynamic mesh
	PartialSubMesh = UE::Geometry::FDynamicSubmesh3(EditedDynaMesh, CurrentlyIsolatedTriangles);

	// remap the non-manifold mapping from full mesh to partial mesh if needed
	const UE::Geometry::FNonManifoldMappingSupport OriginalNonManifoldMapping(*EditedDynaMesh);
	if (OriginalNonManifoldMapping.IsNonManifoldVertexInSource())
	{
		FDynamicMesh3& IsolatedMesh = PartialSubMesh.GetSubmesh();
		
		TArray<int32> IsolatedMeshVertexToNonManifoldVertexID;
		IsolatedMeshVertexToNonManifoldVertexID.Reserve(IsolatedMesh.VertexCount());
		for (int32 IsolatedVertexID = 0; IsolatedVertexID < IsolatedMesh.VertexCount(); ++IsolatedVertexID)
		{
			const int32 OrigMeshVertexID = PartialSubMesh.MapVertexToBaseMesh(IsolatedVertexID);
			const int32 SrcBaseMeshVertexID = OriginalNonManifoldMapping.GetOriginalNonManifoldVertexID(OrigMeshVertexID);
			int32 SrcSubMeshVertexID = PartialSubMesh.MapVertexToSubmesh(SrcBaseMeshVertexID);
			if (SrcSubMeshVertexID == INDEX_NONE)
			{
				SrcSubMeshVertexID = IsolatedVertexID;
			}
			IsolatedMeshVertexToNonManifoldVertexID.Add(SrcSubMeshVertexID);
		}

		// replace the non-manifold vertex map in the cleaned mesh attributes
		UE::Geometry::FNonManifoldMappingSupport CleanedNonManifoldMapping(IsolatedMesh);
		CleanedNonManifoldMapping.AttachNonManifoldVertexMappingData(IsolatedMeshVertexToNonManifoldVertexID, IsolatedMesh);
	}
	
	// create mesh description for sub-mesh
	PartialMeshDescription = MakeShared<FMeshDescription>();
	// registering skeletal mesh attributes is required to create room to copy attributes during conversion from dynamic mesh
	FSkeletalMeshAttributes Attributes(*PartialMeshDescription);
	Attributes.Register();
	// convert the partial dynamic mesh to a mesh description
	// NOTE: this copies vertex weights to partial mesh description (later used to load weights into the tool)
	FDynamicMeshToMeshDescription DnyToDescConverter;
	constexpr bool bCopyTangents = true;
	DnyToDescConverter.Convert(&PartialSubMesh.GetSubmesh(), *PartialMeshDescription, bCopyTangents);
	
	// reinitialize all mesh data structures
	WeightTool->UpdateCurrentlyEditedMesh(SkeletalMeshComponent, PartialSubMesh.GetSubmesh(), *PartialMeshDescription.Get());
}

FCleanedEditMesh::FCleanedEditMesh(
	const FDynamicMesh3& InDynamicMesh,
	const FMeshDescription& InMeshDescription) : OriginalDynamicMesh(InDynamicMesh), OriginalMeshDescription(InMeshDescription)
{
	using namespace UE::Geometry;
	
	// store copy of the original mesh data
	OriginalDynamicMesh = InDynamicMesh;
	OriginalMeshDescription = InMeshDescription;
	
	// if the mesh doesn't have any unused vertices, then we can skip making a cleaned duplicate and just use it directly
	// (the getters will return the original mesh)
	if (!OriginalDynamicMesh.HasUnusedVertices())
	{
		return;
	}
	
	// create a submesh from all the triangles in the mesh
	// NOTE: this is where the cleaning happens. this will leave out unused vertices because they don't belong to any triangle
	TArray<int32> AllTris;
	AllTris.Reserve(OriginalDynamicMesh.TriangleCount());
	for (const int32 TriID : OriginalDynamicMesh.TriangleIndicesItr())
	{
		AllTris.Add(TriID);
	}
	CleanedSubMesh = FDynamicSubmesh3(&OriginalDynamicMesh, AllTris);

	// remap the non-manifold mapping from full mesh to partial mesh
	FNonManifoldMappingSupport OriginalNonManifoldMapping(OriginalDynamicMesh);
	TArray<int32> SubMeshVertexToNonManifoldVertexIDMap;
	FDynamicMesh3& SubMeshDynamicMesh = CleanedSubMesh.GetSubmesh();
	for (int32 SubMeshVertexID=0; SubMeshVertexID<SubMeshDynamicMesh.VertexCount(); ++SubMeshVertexID)
	{
		int32 BaseMeshVertexID = CleanedSubMesh.MapVertexToBaseMesh(SubMeshVertexID);
		int32 SrcBaseMeshVertexID = OriginalNonManifoldMapping.GetOriginalNonManifoldVertexID(BaseMeshVertexID);
		int32 SrcSubMeshVertexID = CleanedSubMesh.MapVertexToSubmesh(SrcBaseMeshVertexID);
		if (SrcSubMeshVertexID == INDEX_NONE)
		{
			SrcSubMeshVertexID = SubMeshVertexID;
		}
		SubMeshVertexToNonManifoldVertexIDMap.Add(SrcSubMeshVertexID);
	}

	// copy bone attributes
	const FDynamicMeshAttributeSet* OrigAttributes = InDynamicMesh.Attributes();
	if (OrigAttributes && OrigAttributes->HasBones())
	{
		if (!SubMeshDynamicMesh.HasAttributes())
		{
			SubMeshDynamicMesh.EnableAttributes();
		}
		FDynamicMeshAttributeSet* SubMeshAttributes = SubMeshDynamicMesh.Attributes();
		SubMeshAttributes->CopyBoneAttributes(*InDynamicMesh.Attributes());
	}

	// replace the non-manifold vertex map in the cleaned mesh attributes
	FNonManifoldMappingSupport CleanedNonManifoldMapping(CleanedSubMesh.GetSubmesh());
	CleanedNonManifoldMapping.AttachNonManifoldVertexMappingData(SubMeshVertexToNonManifoldVertexIDMap,CleanedSubMesh.GetSubmesh());

	// create mesh description for sub-mesh
	CleanedSubMeshDescription = MakeShared<FMeshDescription>();
	// registering skeletal mesh attributes is required to create room to copy attributes during conversion from dynamic mesh
	FSkeletalMeshAttributes Attributes(*CleanedSubMeshDescription);
	Attributes.Register();
	// convert the partial dynamic mesh to a mesh description
	// NOTE: this copies vertex weights to partial mesh description (later used to load weights into the tool)
	FDynamicMeshToMeshDescription DnyToDescConverter;
	constexpr bool bCopyTangents = true;
	DnyToDescConverter.Convert(&CleanedSubMesh.GetSubmesh(), *CleanedSubMeshDescription, bCopyTangents);
}

void FCleanedEditMesh::CopyWeightsToOriginalMesh(const FName InProfile)
{
	// if the mesh was not duplicated and cleaned, we have nothing to copy from
	if (!CleanedSubMeshDescription)
	{
		return;
	}

	// ensure that this profile is available on the cleaned mesh description
	const FSkeletalMeshConstAttributes SubMeshAttrs(*CleanedSubMeshDescription);
	const FSkinWeightsVertexAttributesConstRef SubMeshWeightAttrs = SubMeshAttrs.GetVertexSkinWeights(InProfile);
	if (!SubMeshWeightAttrs.IsValid())
	{
		const FText NotificationText = FText::Format(LOCTEXT("NoSubProfile", "Failed copying skin weights, profile '{0}' not found on sub mesh."), FText::FromName(InProfile));
		ShowEditorMessage(ELogVerbosity::Error, NotificationText);
		return;
	}

	FSkinWeightsVertexAttributesRef BaseMeshWeightAttrs = GetOrCreateSkinWeightsAttribute(OriginalMeshDescription, InProfile);
	if (!BaseMeshWeightAttrs.IsValid())
	{
		const FText NotificationText = FText::Format(LOCTEXT("NoBaseProfile", "Failed copying skin weights, profile '{0}' not found on base mesh."), FText::FromName(InProfile));
		ShowEditorMessage(ELogVerbosity::Error, NotificationText);
		return;
	}

	UE::AnimationCore::FBoneWeightsSettings Settings;
	Settings.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::None);

	TArray<UE::AnimationCore::FBoneWeight> BoneWeightsToApply;
	BoneWeightsToApply.Reserve(UE::AnimationCore::MaxInlineBoneWeightCount);
	
	const int32 NumVerticesInSubMesh = CleanedSubMeshDescription.Get()->Vertices().Num();
	for (int32 SubVertexID=0; SubVertexID < NumVerticesInSubMesh; SubVertexID++)
	{
		BoneWeightsToApply.Reset();
		
		const FVertexBoneWeightsConst SubVertexWeights = SubMeshWeightAttrs.Get(SubVertexID);
		for (UE::AnimationCore::FBoneWeight SingleBoneWeight : SubVertexWeights)
		{
			BoneWeightsToApply.Add(UE::AnimationCore::FBoneWeight(SingleBoneWeight.GetBoneIndex(), SingleBoneWeight.GetWeight()));
		}

		// remap the submesh vertex ID to the base mesh and then copy the weights over
		int32 BaseVertexID = CleanedSubMesh.MapVertexToBaseMesh(SubVertexID);
		BaseMeshWeightAttrs.Set(FVertexID(BaseVertexID), UE::AnimationCore::FBoneWeights::Create(BoneWeightsToApply, Settings));
	}
}

FDynamicMesh3& FCleanedEditMesh::GetEditableMesh()
{
	return CleanedSubMeshDescription ? CleanedSubMesh.GetSubmesh() : OriginalDynamicMesh;
}

FMeshDescription& FCleanedEditMesh::GetEditableMeshDescription()
{
	return CleanedSubMeshDescription ? *CleanedSubMeshDescription.Get() : OriginalMeshDescription;
}

FDynamicMesh3& FCleanedEditMesh::GetOriginalMesh()
{
	return OriginalDynamicMesh;
}

FMeshDescription& FCleanedEditMesh::GetOriginalMeshDescription()
{
	return OriginalMeshDescription;
}

bool USkinWeightsPaintTool::HasActiveSelectionOnMainMesh()
{
	const bool bSelectingTargetMesh = WeightToolProperties->MeshSelectMode == EMeshTransferOption::Target;
	const bool bTargetMeshHasSelection = GetMainMeshSelector()->IsAnyComponentSelected();
	return bSelectingTargetMesh && bTargetMeshHasSelection;
}

void USkinWeightsPaintTool::SelectAffected() const
{
	UPolygonSelectionMechanic* SelectionMechanic = MeshSelector->GetSelectionMechanic();
	if (!ensure(SelectionMechanic))
	{
		return;
	}
	
	GetToolManager()->BeginUndoTransaction(LOCTEXT("AffectedSelectionChange", "Select Affected"));
	SelectionMechanic->BeginChange();
	
	// get all vertices affected by all selected bones
	TSet<int32> AffectedVertices;
	for (const BoneIndex SelectedBone : SelectedBoneIndices)
	{
		GetVerticesAffectedByBone(SelectedBone, AffectedVertices);
	}
	
	// create selection set
	FGroupTopologySelection Selection;

	// optionally add/remove/replace selection based on modifier key state
	const FGroupTopologySelection& CurrentSelection = SelectionMechanic->GetActiveSelection();
	if (bShiftToggle)
	{
		// ADD to selection
		Selection.SelectedCornerIDs.Append(CurrentSelection.SelectedCornerIDs);
		Selection.SelectedCornerIDs.Append(AffectedVertices);
	}
	else if (bCtrlToggle)
	{
		// REMOVE from selection
		Selection.SelectedCornerIDs = CurrentSelection.SelectedCornerIDs.Difference(AffectedVertices);
	}
	else
	{
		// REPLACE selection
		Selection.SelectedCornerIDs = MoveTemp(AffectedVertices);
	}
	
	// select vertices
	constexpr bool bBroadcast = true;
	SelectionMechanic->SetSelection(Selection, bBroadcast);
	SelectionMechanic->EndChangeAndEmitIfModified();
	GetToolManager()->EndUndoTransaction();
}

void USkinWeightsPaintTool::GetVerticesAffectedByBone(BoneIndex IndexOfBone, TSet<int32>& OutVertexIndices) const
{
	VertexIndex VertexID = 0;
	for (const VertexWeights& VertWeights : Weights.PreChangeWeights)
	{
		for (const FVertexBoneWeight& BoneWeight : VertWeights)
		{
			if (BoneWeight.BoneID != IndexOfBone)
			{
				continue;
			}

			if (BoneWeight.Weight < MinimumWeightThreshold)
			{
				continue;
			}
			
			OutVertexIndices.Add(VertexID);
		}
		
		++VertexID;
	}
}

void USkinWeightsPaintTool::GetInfluences(const TArray<int32>& VertexIndices, TArray<BoneIndex>& OutBoneIndices)
{
	for (const int32 SelectedVertex : VertexIndices)
	{
		for (const FVertexBoneWeight& VertexBoneData : Weights.CurrentWeights[SelectedVertex])
		{
			OutBoneIndices.AddUnique(VertexBoneData.BoneID);
		}
	}
	
	// sort hierarchically (bone indices are sorted root to leaf)
	OutBoneIndices.Sort([](BoneIndex A, BoneIndex B) {return A < B;});
}

float USkinWeightsPaintTool::GetAverageWeightOnBone(
	const BoneIndex InBoneIndex,
	const TArray<int32>& VertexIndices)
{
	float TotalWeight = 0.f;
	float NumVerticesInfluencedByBone = 0.f;
	
	for (const VertexIndex VertexID : VertexIndices)
	{
		if (!Weights.CurrentWeights.IsValidIndex(VertexID))
		{
			continue;
		}
		
		for (const FVertexBoneWeight& VertexBoneData : Weights.CurrentWeights[VertexID])
		{
			if (VertexBoneData.BoneID == InBoneIndex)
			{
				++NumVerticesInfluencedByBone;
				TotalWeight += VertexBoneData.Weight;
			}
		}
	}

	return NumVerticesInfluencedByBone > 0 ? TotalWeight / NumVerticesInfluencedByBone : TotalWeight;
}

FName USkinWeightsPaintTool::GetBoneNameFromIndex(BoneIndex InIndex) const
{
	const TArray<FName>& Names = Weights.Deformer.BoneNames;
	if (Names.IsValidIndex(InIndex))
	{
		return Names[InIndex];
	}

	return NAME_None;
}


BoneIndex USkinWeightsPaintTool::GetCurrentBoneIndex() const
{
	return GetBoneIndexFromName(CurrentBone);
}

void USkinWeightsPaintTool::SetDisplayVertexColors(bool bShowVertexColors)
{
	if (bShowVertexColors)
	{
		UMaterialInterface* VtxColorMaterial = GetToolManager()->GetContextQueriesAPI()->GetStandardMaterial(EStandardToolContextMaterials::VertexColorMaterial);
		PreviewMesh->SetOverrideRenderMaterial(VtxColorMaterial);
		bVertexColorsNeedUpdated = true;
	}
	else
	{
		PreviewMesh->ClearOverrideRenderMaterial();
	}
}

void USkinWeightsPaintTool::OnPropertyModified(UObject* ModifiedObject, FProperty* ModifiedProperty)
{
	Super::OnPropertyModified(ModifiedObject, ModifiedProperty);

	if (ModifiedProperty->GetName() == GET_MEMBER_NAME_STRING_CHECKED(USkinWeightsPaintToolProperties, BrushStrength))
	{
		WeightToolProperties->GetBrushConfig().Strength = WeightToolProperties->BrushStrength;
	}
	if (ModifiedProperty->GetName() == GET_MEMBER_NAME_STRING_CHECKED(USkinWeightsPaintToolProperties, BrushRadius))
	{
		WeightToolProperties->GetBrushConfig().Radius = WeightToolProperties->BrushRadius;
	}
	if (ModifiedProperty->GetName() == GET_MEMBER_NAME_STRING_CHECKED(USkinWeightsPaintToolProperties, BrushFalloffAmount))
	{
		WeightToolProperties->GetBrushConfig().Falloff = WeightToolProperties->BrushFalloffAmount;
	}
	
	const FString NameOfModifiedProperty = ModifiedProperty->GetNameCPP();

	// invalidate vertex color cache when any weight color properties are modified
	const TArray<FString> ColorPropertyNames = {
		GET_MEMBER_NAME_STRING_CHECKED(USkinWeightsPaintToolProperties, ColorMode),
		GET_MEMBER_NAME_STRING_CHECKED(USkinWeightsPaintToolProperties, ColorRamp),
		GET_MEMBER_NAME_STRING_CHECKED(FLinearColor, R),
		GET_MEMBER_NAME_STRING_CHECKED(FLinearColor, G),
		GET_MEMBER_NAME_STRING_CHECKED(FLinearColor, B),
		GET_MEMBER_NAME_STRING_CHECKED(FLinearColor, A)};
	if (ColorPropertyNames.Contains(NameOfModifiedProperty))
	{
		bVertexColorsNeedUpdated = true;

		// force all colors to have Alpha = 1
		for (FLinearColor& Color : WeightToolProperties->ColorRamp)
		{
			Color.A = 1.f;
		}
	}

	// let the mesh transfer system react to properties being set
	TransferManager->OnPropertyModified(WeightToolProperties, ModifiedProperty);
	
	SetFocusInViewport();
}

#undef LOCTEXT_NAMESPACE