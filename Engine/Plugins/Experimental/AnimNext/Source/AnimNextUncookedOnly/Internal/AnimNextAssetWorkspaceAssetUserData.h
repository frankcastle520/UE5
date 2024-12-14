// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Entries/AnimNextVariableEntry.h"
#include "WorkspaceAssetRegistryInfo.h"
#include "Param/ParamType.h"
#include "IAnimNextRigVMGraphInterface.h"
#include "RigVMModel/Nodes/RigVMFunctionReferenceNode.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Module/AnimNextModule.h"
#include "DataInterface/AnimNextDataInterface.h"

#include "AnimNextAssetWorkspaceAssetUserData.generated.h"

class UAnimNextRigVMAssetEntry;
class URigVMEdGraphNode;

// Base struct used to identify asset entries
USTRUCT()
struct FAnimNextRigVMAssetOutlinerData : public FWorkspaceOutlinerItemData
{
	GENERATED_BODY()

	FAnimNextRigVMAssetOutlinerData() = default;

	UPROPERTY(VisibleAnywhere, Category=AnimNext)
	TObjectPtr<UAnimNextRigVMAsset> Asset;
};

USTRUCT()
struct FAnimNextModuleOutlinerData : public FAnimNextRigVMAssetOutlinerData
{
	GENERATED_BODY()

	FAnimNextModuleOutlinerData() = default;

	UAnimNextModule* GetModule() const
	{
		return Cast<UAnimNextModule>(Asset);
	}
};

USTRUCT()
struct FAnimNextAnimationGraphOutlinerData : public FAnimNextRigVMAssetOutlinerData
{
	GENERATED_BODY()

	FAnimNextAnimationGraphOutlinerData() = default;

	UAnimNextAnimationGraph* GetAnimationGraph() const
	{
		return Cast<UAnimNextAnimationGraph>(Asset);
	}
};

USTRUCT()
struct FAnimNextDataInterfaceOutlinerData : public FAnimNextRigVMAssetOutlinerData
{
	GENERATED_BODY()

	FAnimNextDataInterfaceOutlinerData() = default;

	UAnimNextDataInterface* GetDataInteface() const
	{
		return Cast<UAnimNextDataInterface>(Asset);
	}
};

// Base struct used to identify asset sub-entries
USTRUCT()
struct FAnimNextAssetEntryOutlinerData : public FWorkspaceOutlinerItemData
{
	GENERATED_BODY()
	
	FAnimNextAssetEntryOutlinerData() = default;

	UPROPERTY(VisibleAnywhere, Category=AnimNext)
	TObjectPtr<UAnimNextRigVMAssetEntry> Entry;
};

USTRUCT()
struct FAnimNextVariableOutlinerData : public FAnimNextAssetEntryOutlinerData
{
	GENERATED_BODY()
	
	FAnimNextVariableOutlinerData() = default;

	UPROPERTY(VisibleAnywhere, Category=AnimNext)
	FAnimNextParamType Type;
};

USTRUCT()
struct FAnimNextCollapseGraphsOutlinerDataBase : public FWorkspaceOutlinerItemData
{
	GENERATED_BODY()
	
	FAnimNextCollapseGraphsOutlinerDataBase() = default;

	UPROPERTY(VisibleAnywhere, Category=AnimNext)
	TWeakObjectPtr<URigVMEdGraph> EditorObject;
};

USTRUCT()
struct FAnimNextCollapseGraphOutlinerData : public FAnimNextCollapseGraphsOutlinerDataBase
{
	GENERATED_BODY()
	
	FAnimNextCollapseGraphOutlinerData() = default;
};

USTRUCT()
struct FAnimNextGraphFunctionOutlinerData : public FAnimNextCollapseGraphsOutlinerDataBase
{
	GENERATED_BODY()
	
	FAnimNextGraphFunctionOutlinerData() = default;

	UPROPERTY(VisibleAnywhere, Category=AnimNext)
	TWeakObjectPtr<URigVMEdGraphNode> EdGraphNode;
};

USTRUCT()
struct FAnimNextGraphOutlinerData : public FAnimNextAssetEntryOutlinerData
{
	GENERATED_BODY()
	
	FAnimNextGraphOutlinerData() = default;

	UPROPERTY(VisibleAnywhere, Category=AnimNext)
	TScriptInterface<IAnimNextRigVMGraphInterface> GraphInterface;
};

UCLASS()
class UAnimNextAssetWorkspaceAssetUserData : public UAssetUserData
{
	GENERATED_BODY()

	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
};
