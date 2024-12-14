// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "EdGraph/EdGraph.h"
#include "MetasoundAssetBase.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundRouter.h"
#include "MetasoundUObjectRegistry.h"
#include "Serialization/Archive.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/SoftObjectPath.h"

#include "Metasound.generated.h"


// Forward Declarations
#if WITH_EDITOR
class FDataValidationContext;
#endif // WITH_EDITOR

namespace Metasound::Engine
{
	struct FAssetHelper;
} // namespace Metasound::Engine


UCLASS(Abstract)
class METASOUNDENGINE_API UMetasoundEditorGraphBase : public UEdGraph
{
	GENERATED_BODY()

public:
	virtual bool IsEditorOnly() const override { return true; }
	virtual bool NeedsLoadForEditorGame() const override { return false; }

	virtual void RegisterGraphWithFrontend() PURE_VIRTUAL(UMetasoundEditorGraphBase::RegisterGraphWithFrontend(), )

#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.5, "ModifyContext is to be replaced by builder API delegates providing context when items changed and it will be up to the caller to track modification deltas.")
	virtual FMetasoundFrontendDocumentModifyContext& GetModifyContext() { static FMetasoundFrontendDocumentModifyContext InvalidModifyData; return InvalidModifyData; }

	UE_DEPRECATED(5.5, "ModifyContext is to be replaced by builder API delegates providing context when items changed and it will be up to the caller to track modification deltas.")
	virtual const FMetasoundFrontendDocumentModifyContext& GetModifyContext() const { static const FMetasoundFrontendDocumentModifyContext InvalidModifyData; return InvalidModifyData; }

	UE_DEPRECATED(5.5, "Editor Graph is now transient, so versioning flag moved to AssetBase.")
	virtual void ClearVersionedOnLoad() { }

	UE_DEPRECATED(5.5, "Editor Graph is now transient, so versioning flag moved to AssetBase.")
	virtual bool GetVersionedOnLoad() const { return false; }

	UE_DEPRECATED(5.5, "Editor Graph is now transient, so versioning flag moved to AssetBase.")
	virtual void SetVersionedOnLoad() {  }

	virtual void MigrateEditorDocumentData(FMetaSoundFrontendDocumentBuilder & OutBuilder) PURE_VIRTUAL(UMetasoundEditorGraphBase::MigrateEditorDocumentData(), )
#endif // WITH_EDITORONLY_DATA

	int32 GetHighestMessageSeverity() const;
};


/**
 * This asset type is used for Metasound assets that can only be used as nodes in other Metasound graphs.
 * Because of this, they contain no required inputs or outputs.
 */
UCLASS(hidecategories = object, BlueprintType, meta = (DisplayName = "MetaSound Patch"))
class METASOUNDENGINE_API UMetaSoundPatch : public UObject, public FMetasoundAssetBase, public IMetaSoundDocumentInterface
{
	GENERATED_BODY()

	friend struct Metasound::Engine::FAssetHelper;
	friend class UMetaSoundPatchBuilder;

protected:
	UPROPERTY(EditAnywhere, Category = CustomView)
	FMetasoundFrontendDocument RootMetaSoundDocument;

	UPROPERTY()
	TSet<FString> ReferencedAssetClassKeys;

	UPROPERTY()
	TSet<TObjectPtr<UObject>> ReferencedAssetClassObjects;

	UPROPERTY()
	TSet<FSoftObjectPath> ReferenceAssetClassCache;

#if WITH_EDITORONLY_DATA
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage = "Use EditorGraph instead as it is now transient and generated via the FrontendDocument dynamically."))
	TObjectPtr<UMetasoundEditorGraphBase> Graph;

	UPROPERTY(Transient)
	TObjectPtr<UMetasoundEditorGraphBase> EditorGraph;
#endif // WITH_EDITORONLY_DATA

public:
	UMetaSoundPatch(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(AssetRegistrySearchable)
	FGuid AssetClassID;

#if WITH_EDITORONLY_DATA
	UPROPERTY(AssetRegistrySearchable)
	FString RegistryInputTypes;

	UPROPERTY(AssetRegistrySearchable)
	FString RegistryOutputTypes;

	UPROPERTY(AssetRegistrySearchable)
	int32 RegistryVersionMajor = 0;

	UPROPERTY(AssetRegistrySearchable)
	int32 RegistryVersionMinor = 0;

	UPROPERTY(AssetRegistrySearchable)
	bool bIsPreset = false;

	// Sets Asset Registry Metadata associated with this MetaSound
	virtual void SetRegistryAssetClassInfo(const Metasound::Frontend::FNodeClassInfo& InClassInfo) override;

	// Returns document name (for editor purposes, and avoids making document public for edit
	// while allowing editor to reference directly)
	static FName GetDocumentPropertyName()
	{
		return GET_MEMBER_NAME_CHECKED(UMetaSoundPatch, RootMetaSoundDocument);
	}

	// Name to display in editors
	virtual FText GetDisplayName() const override;

	// Returns the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @return Editor graph associated with UMetaSoundSource.
	virtual UEdGraph* GetGraph() const override;
	virtual UEdGraph& GetGraphChecked() const override;
	virtual void MigrateEditorGraph(FMetaSoundFrontendDocumentBuilder& OutBuilder) override;

	// Sets the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @param Editor graph associated with UMetaSoundSource.
	virtual void SetGraph(UEdGraph* InGraph) override
	{
		EditorGraph = CastChecked<UMetasoundEditorGraphBase>(InGraph);
	}
#endif // #if WITH_EDITORONLY_DATA

	virtual FTopLevelAssetPath GetAssetPathChecked() const override;
	virtual const UClass& GetBaseMetaSoundUClass() const final override;
	virtual const UClass& GetBuilderUClass() const final override;
	virtual const FMetasoundFrontendDocument& GetConstDocument() const override;

#if WITH_EDITOR
	virtual void PreDuplicate(FObjectDuplicationParameters& DupParams) override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void PostEditUndo() override;
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;

#endif // WITH_EDITOR

	virtual void BeginDestroy() override;
	virtual void PreSave(FObjectPreSaveContext InSaveContext) override;
	virtual void Serialize(FArchive& InArchive) override;
	virtual void PostLoad() override;

	virtual bool ConformObjectToDocument() override { return false; }

	virtual const TSet<FString>& GetReferencedAssetClassKeys() const override
	{
		return ReferencedAssetClassKeys;
	}
	virtual TArray<FMetasoundAssetBase*> GetReferencedAssets() override;
	virtual const TSet<FSoftObjectPath>& GetAsyncReferencedAssetClassPaths() const override;
	virtual void OnAsyncReferencedAssetsLoaded(const TArray<FMetasoundAssetBase*>& InAsyncReferences) override;

	UObject* GetOwningAsset() override
	{
		return this;
	}

	const UObject* GetOwningAsset() const override
	{
		return this;
	}

	virtual bool IsActivelyBuilding() const override;

protected:
#if WITH_EDITOR
	virtual void SetReferencedAssetClasses(TSet<Metasound::Frontend::IMetaSoundAssetManager::FAssetInfo>&& InAssetClasses) override;
#endif // #if WITH_EDITOR

	Metasound::Frontend::FDocumentAccessPtr GetDocumentAccessPtr() override;
	Metasound::Frontend::FConstDocumentAccessPtr GetDocumentConstAccessPtr() const override;

private:
	virtual FMetasoundFrontendDocument& GetDocument() override
	{
		return RootMetaSoundDocument;
	}

	virtual void OnBeginActiveBuilder() override;
	virtual void OnFinishActiveBuilder() override;

	bool bIsBuilderActive = false;
};
