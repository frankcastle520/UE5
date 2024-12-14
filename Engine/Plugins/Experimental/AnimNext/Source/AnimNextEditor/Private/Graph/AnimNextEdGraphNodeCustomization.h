// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyEditorModule.h"
#include "RigVMCore/RigVMMemoryStorageStruct.h"

class URigVMPin;
class IDetailCategoryBuilder;
class UAnimNextEdGraphNode;
class FRigVMGraphDetailCustomizationImpl;

namespace UE::Workspace
{
	class IWorkspaceEditor;
}

namespace UE::AnimNext::Editor
{

class FAnimNextEdGraphNodeCustomization : public IDetailCustomization
{
public:
	FAnimNextEdGraphNodeCustomization() = default;
	explicit FAnimNextEdGraphNodeCustomization(const TWeakPtr<UE::Workspace::IWorkspaceEditor>& InWorkspaceEditorWeak);

protected:

	// --- IDetailCustomization Begin ---
	/** Called when details should be customized */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	/** Called when no longer used and will be deleted */
	virtual void PendingDelete() override;

	// --- IDetailCustomization End ---

	struct FCategoryDetailsData
	{
		enum class EType : uint8
		{
			TraitStack = 0,
			RigVMNode,
			// --- ---
			Invalid,
			Num = Invalid
		};

		FCategoryDetailsData() = default;
		explicit FCategoryDetailsData(EType InType)
			: Type(InType)
		{
		}
		FCategoryDetailsData(EType InType, const FName& InName)
			: Type(InType)
			, Name(InName)
		{
		}

		EType Type = EType::Invalid;
		FName Name;
		TArray<TWeakObjectPtr<UAnimNextEdGraphNode>> EdGraphNodes;
	};

	struct FTraitStackDetailsData : FCategoryDetailsData
	{
		FTraitStackDetailsData()
			: FCategoryDetailsData(FCategoryDetailsData::EType::TraitStack)
		{
		}
		explicit FTraitStackDetailsData(const FName InName)
			: FCategoryDetailsData(FCategoryDetailsData::EType::TraitStack, InName)
		{
		}

		TArray<TSharedPtr<FStructOnScope>> ScopedSharedDataInstances;
		TSharedPtr<IPropertyHandle> RootPropertyHandle;
		TArray<TSharedRef<IPropertyHandle>> PropertyHandles;
	};

	struct FRigVMNodeDetailsData : FCategoryDetailsData
	{
		FRigVMNodeDetailsData()
			: FCategoryDetailsData(FCategoryDetailsData::EType::RigVMNode)
		{
		}
		explicit FRigVMNodeDetailsData(const FName InName)
			: FCategoryDetailsData(FCategoryDetailsData::EType::RigVMNode, InName)
		{
		}

		TArray<FName> ModelPinsNamesToDisplay;
		TArray <TArray<FString>> ModelPinPaths;
		TArray <TSharedPtr<FRigVMMemoryStorageStruct>> MemoryStorages;
	};

	void CustomizeObjects(IDetailLayoutBuilder& DetailBuilder, const TArray<TWeakObjectPtr<UObject>>& InObjects);

	static void GenerateTraitData(UAnimNextEdGraphNode* EdGraphNode, TArray<TSharedPtr<FCategoryDetailsData>>& CategoryDetailsData);
	static void GenerateRigVMData(UAnimNextEdGraphNode* EdGraphNode, TArray<TSharedPtr<FCategoryDetailsData>>& CategoryDetailsData);

	static void PopulateCategory(IDetailLayoutBuilder& DetailBuilder, const TSharedPtr<FCategoryDetailsData>& CategoryDetailsData);
	static void PopulateCategory(IDetailLayoutBuilder& DetailBuilder, const TSharedPtr<FTraitStackDetailsData>& TraitData);
	static void PopulateCategory(IDetailLayoutBuilder& DetailBuilder, const TSharedPtr<FRigVMNodeDetailsData>& RigVMTypeData);

	static void GenerateMemoryStorage(const TArray<TWeakObjectPtr<URigVMPin>> & ModelPinsToDisplay, FRigVMMemoryStorageStruct& MemoryStorage);

	TWeakPtr<UE::Workspace::IWorkspaceEditor> WorkspaceEditorWeak;
	TArray<TSharedPtr<FCategoryDetailsData>> CategoryDetailsData;

	TSharedPtr<FRigVMGraphDetailCustomizationImpl> RigVMGraphDetailCustomizationImpl;
};

}
