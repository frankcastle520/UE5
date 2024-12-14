// Copyright Epic Games, Inc. All Rights Reserved.
#include "Metasound.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Internationalization/Text.h"
#include "Logging/TokenizedMessage.h"
#include "MetasoundAssetBase.h"
#include "MetasoundAssetManager.h"
#include "MetasoundAudioFormats.h"
#include "MetasoundBuilderSubsystem.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundEngineAsset.h"
#include "MetasoundEngineEnvironment.h"
#include "MetasoundEnvironment.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendRegistryContainer.h"
#include "MetasoundFrontendRegistryKey.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundGenerator.h"
#include "MetasoundLog.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundParameterTransmitter.h"
#include "MetasoundPrimitives.h"
#include "MetasoundReceiveNode.h"
#include "MetasoundTrigger.h"
#include "MetasoundUObjectRegistry.h"
#include "UObject/ObjectSaveContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Metasound)


#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif // WITH_EDITOR


#if WITH_EDITORONLY_DATA
#include "EdGraph/EdGraph.h"
#endif // WITH_EDITORONLY_DATA

#define LOCTEXT_NAMESPACE "MetaSound"


int32 UMetasoundEditorGraphBase::GetHighestMessageSeverity() const
{
	int32 HighestMessageSeverity = EMessageSeverity::Info;

	for (const UEdGraphNode* Node : Nodes)
	{
		// Lower integer value is "higher severity"
		if (Node->ErrorType < HighestMessageSeverity)
		{
			HighestMessageSeverity = Node->ErrorType;
		}
	}

	return HighestMessageSeverity;
}

UMetaSoundPatch::UMetaSoundPatch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FMetasoundAssetBase()
{
}

Metasound::Frontend::FDocumentAccessPtr UMetaSoundPatch::GetDocumentAccessPtr()
{
	using namespace Metasound::Frontend;

	// Mutation of a document via the soft deprecated access ptr/controller system is not tracked by
	// the builder registry, so the document cache is invalidated here.
	if (IDocumentBuilderRegistry* BuilderRegistry = IDocumentBuilderRegistry::Get())
	{
		BuilderRegistry->ReloadBuilder(RootMetaSoundDocument.RootGraph.Metadata.GetClassName());
	}

	// Return document using FAccessPoint to inform the TAccessPtr when the 
	// object is no longer valid.
	return MakeAccessPtr<FDocumentAccessPtr>(RootMetaSoundDocument.AccessPoint, RootMetaSoundDocument);
}

Metasound::Frontend::FConstDocumentAccessPtr UMetaSoundPatch::GetDocumentConstAccessPtr() const
{
	using namespace Metasound::Frontend;
	// Return document using FAccessPoint to inform the TAccessPtr when the 
	// object is no longer valid.
	return MakeAccessPtr<FConstDocumentAccessPtr>(RootMetaSoundDocument.AccessPoint, RootMetaSoundDocument);
}

const UClass& UMetaSoundPatch::GetBaseMetaSoundUClass() const
{
	return *UMetaSoundPatch::StaticClass();
}

const UClass& UMetaSoundPatch::GetBuilderUClass() const
{
	return *UMetaSoundPatchBuilder::StaticClass();
}

const FMetasoundFrontendDocument& UMetaSoundPatch::GetConstDocument() const
{
	return RootMetaSoundDocument;
}

#if WITH_EDITOR
void UMetaSoundPatch::PreDuplicate(FObjectDuplicationParameters& DupParams)
{
	Super::PreDuplicate(DupParams);
	Metasound::Engine::FAssetHelper::PreDuplicate(this, DupParams);
}

void UMetaSoundPatch::PostDuplicate(EDuplicateMode::Type InDuplicateMode)
{
	Super::PostDuplicate(InDuplicateMode);
	Metasound::Engine::FAssetHelper::PostDuplicate(this, InDuplicateMode, AssetClassID);
}

void UMetaSoundPatch::PostEditUndo()
{
	Super::PostEditUndo();
	Metasound::Engine::FAssetHelper::PostEditUndo(*this);
}

EDataValidationResult UMetaSoundPatch::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Result = Metasound::Engine::FAssetHelper::IsDataValid(*this, RootMetaSoundDocument, Context);
	return CombineDataValidationResults(Result, Super::IsDataValid(Context));
}

#endif // WITH_EDITOR

void UMetaSoundPatch::BeginDestroy()
{
	OnNotifyBeginDestroy();
	Super::BeginDestroy();
}

void UMetaSoundPatch::PreSave(FObjectPreSaveContext InSaveContext)
{
	Super::PreSave(InSaveContext);
	Metasound::Engine::FAssetHelper::PreSaveAsset(*this, InSaveContext);
}

void UMetaSoundPatch::Serialize(FArchive& InArchive)
{
	Super::Serialize(InArchive);
	Metasound::Engine::FAssetHelper::SerializeToArchive(*this, InArchive);
}

#if WITH_EDITORONLY_DATA
void UMetaSoundPatch::MigrateEditorGraph(FMetaSoundFrontendDocumentBuilder& OutBuilder)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (Graph)
	{
		Graph->MigrateEditorDocumentData(OutBuilder);
		Graph = nullptr;
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

UEdGraph* UMetaSoundPatch::GetGraph() const
{
	return EditorGraph;
}

UEdGraph& UMetaSoundPatch::GetGraphChecked() const
{
	check(EditorGraph);
	return *EditorGraph;
}
FText UMetaSoundPatch::GetDisplayName() const
{
	FString TypeName = UMetaSoundPatch::StaticClass()->GetName();
	return FMetasoundAssetBase::GetDisplayName(MoveTemp(TypeName));
}


void UMetaSoundPatch::SetRegistryAssetClassInfo(const Metasound::Frontend::FNodeClassInfo& InNodeInfo)
{
	Metasound::Engine::FAssetHelper::SetMetaSoundRegistryAssetClassInfo(*this, InNodeInfo);
}
#endif // WITH_EDITORONLY_DATA


FTopLevelAssetPath UMetaSoundPatch::GetAssetPathChecked() const
{
	return Metasound::Engine::FAssetHelper::GetAssetPathChecked(*this);
}

void UMetaSoundPatch::PostLoad() 
{
	Super::PostLoad();
	Metasound::Engine::FAssetHelper::PostLoad(*this);
}

#if WITH_EDITOR
void UMetaSoundPatch::SetReferencedAssetClasses(TSet<Metasound::Frontend::IMetaSoundAssetManager::FAssetInfo>&& InAssetClasses)
{
	Metasound::Engine::FAssetHelper::SetReferencedAssetClasses(*this, MoveTemp(InAssetClasses));
}
#endif

TArray<FMetasoundAssetBase*> UMetaSoundPatch::GetReferencedAssets()
{
	return Metasound::Engine::FAssetHelper::GetReferencedAssets(*this);
}

const TSet<FSoftObjectPath>& UMetaSoundPatch::GetAsyncReferencedAssetClassPaths() const 
{
	return ReferenceAssetClassCache;
}

void UMetaSoundPatch::OnAsyncReferencedAssetsLoaded(const TArray<FMetasoundAssetBase*>& InAsyncReferences)
{
	Metasound::Engine::FAssetHelper::OnAsyncReferencedAssetsLoaded(*this, InAsyncReferences);
}

bool UMetaSoundPatch::IsActivelyBuilding() const
{
	return bIsBuilderActive;
}

void UMetaSoundPatch::OnBeginActiveBuilder()
{
	using namespace Metasound::Frontend;

	if (bIsBuilderActive)
	{
		UE_LOG(LogMetaSound, Error, TEXT("OnBeginActiveBuilder() call while prior builder is still active. This may indicate that multiple builders are attempting to modify the MetaSound %s concurrently."), *GetOwningAssetName())
	}

	// If a builder is activating, make sure any in-flight registration
	// tasks have completed. Async registration tasks use the FMetasoundFrontendDocument
	// that lives on this object. We need to make sure that registration task
	// completes so that the FMetasoundFrontendDocument does not get modified
	// by a builder while it is also being read by async registration.
	const FGraphRegistryKey GraphKey = GetGraphRegistryKey();
	if (GraphKey.IsValid())
	{
		FMetasoundFrontendRegistryContainer::Get()->WaitForAsyncGraphRegistration(GraphKey);
	}

	bIsBuilderActive = true;
}

void UMetaSoundPatch::OnFinishActiveBuilder()
{
	bIsBuilderActive = false;
}

#undef LOCTEXT_NAMESPACE // MetaSound

