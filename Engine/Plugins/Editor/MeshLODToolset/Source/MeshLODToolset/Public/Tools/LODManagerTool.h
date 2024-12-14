// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseTools/MultiSelectionMeshEditingTool.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "LODManagerTool.generated.h"

class UPreviewGeometry;
class UPreviewMesh;

// predeclarations
class UMaterialInterface;
class ULODManagerTool;

/**
 *
 */
UCLASS()
class MESHLODTOOLSET_API ULODManagerToolBuilder : public UMultiSelectionMeshEditingToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UMultiSelectionMeshEditingTool* CreateNewTool(const FToolBuilderState& SceneState) const override;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};


USTRUCT()
struct MESHLODTOOLSET_API FLODManagerLODInfo
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = LODInformation, meta=(NoResetToDefault))
	int32 VertexCount = 0;

	UPROPERTY(VisibleAnywhere, Category = LODInformation, meta=(NoResetToDefault))
	int32 TriangleCount = 0;
};



UCLASS()
class MESHLODTOOLSET_API ULODManagerLODProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category = LODInformation, meta = (NoResetToDefault))
	TArray<FLODManagerLODInfo> SourceLODs;

	UPROPERTY(VisibleAnywhere, Category = LODInformation, meta = (NoResetToDefault))
	TArray<FLODManagerLODInfo> HiResSource;


	UPROPERTY(VisibleAnywhere, Category = LODInformation, meta = (NoResetToDefault))
	TArray<FLODManagerLODInfo> RenderLODs;

	UPROPERTY(VisibleAnywhere, Category = Nanite, meta = (NoResetToDefault, DisplayName="Enabled"))
	bool bNaniteEnabled = false;

	// Percentage of triangles kept by Nanite
	UPROPERTY(VisibleAnywhere, Category = Nanite, meta = (NoResetToDefault))
	float KeepTrianglePercent = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = Materials, meta = (NoResetToDefault))
	TArray<FStaticMaterial> Materials;

};



UCLASS()
class MESHLODTOOLSET_API ULODManagerPreviewLODProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** LOD to visualise. Default option is equivalent to disabling the Tool, RenderData is the mesh used for rendering derived from the SourceModel (possibly simplified) */
	UPROPERTY(EditAnywhere, Category = LODPreview, meta = (DisplayName = "Show LOD", NoResetToDefault, GetOptions = GetLODNamesFunc))
	FString VisibleLOD;

	UFUNCTION()
	const TArray<FString>& GetLODNamesFunc() const { return LODNamesList; }

	UPROPERTY(meta = (TransientToolProperty))
	TArray<FString> LODNamesList;

	/** Control whether mesh borders are displayed */
	UPROPERTY(EditAnywhere, DisplayName = "Show Borders", Category = LODPreview)
	bool bShowSeams = true;

};







UENUM()
enum class ELODManagerToolActions
{
	NoAction,

	MoveHiResToLOD0,
	DeleteHiResSourceModel,
	RemoveUnreferencedMaterials
};



UCLASS()
class MESHLODTOOLSET_API ULODManagerActionPropertySet : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<ULODManagerTool> ParentTool;

	void Initialize(ULODManagerTool* ParentToolIn) { ParentTool = ParentToolIn; }
	void PostAction(ELODManagerToolActions Action);

};


UCLASS()
class MESHLODTOOLSET_API ULODManagerHiResSourceModelActions : public ULODManagerActionPropertySet
{
	GENERATED_BODY()
public:
	/** Move the HiRes Source Model to LOD0 */
	UFUNCTION(CallInEditor, Category = HiResSourceModel, meta = (DisplayPriority = 0))
	void MoveToLOD0()
	{
		PostAction(ELODManagerToolActions::MoveHiResToLOD0);
	}

	/** Delete the HiRes Source Model */
	UFUNCTION(CallInEditor, Category = HiResSourceModel, meta = (DisplayPriority = 1))
	void Delete()
	{
		PostAction(ELODManagerToolActions::DeleteHiResSourceModel);
	}
};


UCLASS()
class MESHLODTOOLSET_API ULODManagerMaterialActions : public ULODManagerActionPropertySet
{
	GENERATED_BODY()
public:
	/** Discard any Materials that are not referenced by any LOD */
	UFUNCTION(CallInEditor, Category = MaterialSet, meta = (DisplayPriority = 0))
	void CleanMaterials()
	{
		PostAction(ELODManagerToolActions::RemoveUnreferencedMaterials);
	}
};

// forward
namespace UE { namespace Geometry { namespace LODManagerHelper {
class FLODManagerToolChange; 
struct FProxyLODState;
class FDynamicMeshLODCache;

} } }


UINTERFACE(MinimalAPI)
class ULODManagerToolChangeTarget : public UInterface
{
	GENERATED_BODY()
};
/**
 * IMeshVertexCommandChangeTarget is an interface which is used to apply a FLODManagerToolChange
 */
class ILODManagerToolChangeTarget
{
	GENERATED_BODY()
public:
	virtual void ApplyChange(const UE::Geometry::LODManagerHelper::FLODManagerToolChange* Change, bool bRevert) = 0;
};

/**
 * Mesh Attribute Editor Tool
 */
UCLASS()
class MESHLODTOOLSET_API ULODManagerTool : public UMultiSelectionMeshEditingTool, public ILODManagerToolChangeTarget
{
	GENERATED_BODY()

public:
	ULODManagerTool();

	virtual void Setup() override;
	virtual void OnShutdown(EToolShutdownType ShutdownType) override;
	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }

	virtual void RequestAction(ELODManagerToolActions ActionType);

	virtual void ApplyChange(const UE::Geometry::LODManagerHelper::FLODManagerToolChange* Change, bool bRevert) override;

protected:

	UPROPERTY()
	TObjectPtr<ULODManagerLODProperties> LODInfoProperties;

	UPROPERTY()
	TObjectPtr<ULODManagerPreviewLODProperties> LODPreviewProperties;

	UPROPERTY()
	TObjectPtr<ULODManagerHiResSourceModelActions> HiResSourceModelActions;

	UPROPERTY()
	TObjectPtr<ULODManagerMaterialActions> MaterialActions;



public:
	UFUNCTION()
	void DeleteHiResSourceModel();

	UFUNCTION()
	void MoveHiResToLOD0();

	UFUNCTION()
	void RemoveUnreferencedMaterials();


	struct FLODName
	{
		int32 SourceModelIndex = -1;
		int32 RenderDataIndex = -1;
		int32 OtherIndex = -1;
		bool IsValid() const { return ! (SourceModelIndex == -1 && RenderDataIndex == -1 && OtherIndex == -1); }
	};

	struct FLODMeshInfo
	{
		TSharedPtr<UE::Geometry::FDynamicMesh3> Mesh;
		TArray<int> BoundaryEdges;
	};


protected:
	UStaticMesh* GetSingleStaticMesh();

	ELODManagerToolActions PendingAction = ELODManagerToolActions::NoAction;
	

	bool bLODInfoValid = false;
	// captures the material list and triangle and vertex counts for the current configuration of lods
	void UpdateLODInfo();

	// maps pretty name in UI to description of the LOD
	TMap<FString, FLODName> ActiveLODNames;
	void UpdateLODNames();

	UPROPERTY()
	TObjectPtr<UPreviewMesh> LODPreview;

	UPROPERTY()
	TObjectPtr<UPreviewGeometry> LODPreviewLines;

	bool bPreviewLODValid = false;
	void UpdatePreviewLOD();
	void UpdatePreviewLines(FLODMeshInfo& LODMeshInfo);
	void ClearPreviewLines();

	// returns the requested LOD as a dynamic mesh along with boundary edges.  Has internal cache
	TUniquePtr<ULODManagerTool::FLODMeshInfo> GetLODMeshInfo(const FLODName& LODName);

	// For undo / redo system with our custom changes.  
	TUniquePtr< UE::Geometry::LODManagerHelper::FLODManagerToolChange > ActiveChange;
	void BeginChange(FText TransactionName);
	void EndChange();

	// state information used for undo within the tool
	TUniquePtr< UE::Geometry::LODManagerHelper::FProxyLODState>  ProxyLODState;

	// cache of dynamic mesh representation for each lod, imported and renderdata.
	TUniquePtr< UE::Geometry::LODManagerHelper::FDynamicMeshLODCache > DynamicMeshCache;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#include "Drawing/PreviewGeometryActor.h"
#include "DynamicMesh/MeshTangents.h"
#include "PreviewMesh.h"
#endif
