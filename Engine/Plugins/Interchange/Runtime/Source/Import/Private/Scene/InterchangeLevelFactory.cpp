// Copyright Epic Games, Inc. All Rights Reserved.

#include "Scene/InterchangeLevelFactory.h"

#include "Engine/World.h"
#include "InterchangeAssetImportData.h"
#include "InterchangeEditorUtilitiesBase.h"
#include "InterchangeImportCommon.h"
#include "InterchangeImportLog.h"
#include "InterchangeLevelFactoryNode.h"
#include "InterchangeLevelInstanceActorFactoryNode.h"
#include "InterchangeManager.h"
#include "InterchangeResult.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "InterchangeVariantSetNode.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "Misc/App.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Class.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"
#include "WorldPartition/WorldPartitionEditorLoaderAdapter.h"
#include "WorldPartition/WorldPartition.h"

#define LOCTEXT_NAMESPACE "InterchangeLevelFactory"

namespace UE::Interchange::Private::InterchangeLevelFactory
{
	const UInterchangeLevelFactoryNode* GetFactoryNode(const UInterchangeFactoryBase::FImportAssetObjectParams& Arguments, UClass* TargetClass)
	{
		if (!Arguments.NodeContainer || !Arguments.AssetNode || !Arguments.AssetNode->GetObjectClass()->IsChildOf(TargetClass))
		{
			return nullptr;
		}

		return Cast<UInterchangeLevelFactoryNode>(Arguments.AssetNode);
	}

	UObject* FindOrCreateAsset(const UInterchangeFactoryBase::FImportAssetObjectParams& Arguments, UClass* TargetClass)
	{

		UObject* TargetAsset = nullptr;
		if (Arguments.ReimportObject)
		{
			TargetAsset = Arguments.ReimportObject;
		}
		else
		{
			FSoftObjectPath ExistingWorldAssetPath;
			if (Arguments.AssetNode->GetCustomReferenceObject(ExistingWorldAssetPath))
			{
				TargetAsset = ExistingWorldAssetPath.TryLoad();
			}
			if (!TargetAsset)
			{
				TargetAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);
			}
		}
		
		bool bCreateWorldPartition = false;
		//Verify and ensure if we should create a level or not
		if (UInterchangeLevelFactoryNode* LevelFactoryNode = Cast<UInterchangeLevelFactoryNode>(Arguments.AssetNode))
		{
			bool bShouldCreateLevel = false;
			if (LevelFactoryNode->GetCustomShouldCreateLevel(bShouldCreateLevel))
			{
				ensure(bShouldCreateLevel ? TargetAsset == nullptr : TargetAsset != nullptr);
			}
			LevelFactoryNode->GetCustomCreateWorldPartitionLevel(bCreateWorldPartition);
		}
		// Create a new asset or return existing asset, if possible
		if (!TargetAsset)
		{
			if(!ensure(IsInGameThread()))
			{
				return nullptr;
			}
			
			// Create a new world.
			const bool bAddToRoot = false;

			constexpr bool bEnableWorldPartitionStreaming = false;
			constexpr bool bInformEngineOfWorld = true;
			bool bIsRuntimeOrPIE = false;
			if (UInterchangeEditorUtilitiesBase* EditorUtilities = UInterchangeManager::GetInterchangeManager().GetEditorUtilities())
			{
				bIsRuntimeOrPIE = EditorUtilities->IsRuntimeOrPIE();
			}

			// Those are the init values taken from the default in UWorld::CreateWorld + CreateWorldPartition.
			UWorld::InitializationValues InitValues = UWorld::InitializationValues()
				.ShouldSimulatePhysics(false)
				.EnableTraceCollision(true)
				.CreateNavigation(!bIsRuntimeOrPIE)
				.CreateAISystem(!bIsRuntimeOrPIE)
				.CreateWorldPartition(bCreateWorldPartition)
				.EnableWorldPartitionStreaming(bEnableWorldPartitionStreaming);

			UWorld* NewWorld = UWorld::CreateWorld(EWorldType::Editor, bInformEngineOfWorld, *Arguments.AssetName, Cast<UPackage>(Arguments.Parent), bAddToRoot, ERHIFeatureLevel::Num, &InitValues);
			NewWorld->SetFlags(RF_Public | RF_Standalone);
			TargetAsset = NewWorld;
		}
		else if (!TargetAsset->GetClass()->IsChildOf(TargetClass))
		{
			TargetAsset = nullptr;
		}

		return TargetAsset;
	}
}


UClass* UInterchangeLevelFactory::GetFactoryClass() const
{
	return UWorld::StaticClass();
}


UInterchangeFactoryBase::FImportAssetResult UInterchangeLevelFactory::BeginImportAsset_GameThread(const FImportAssetObjectParams& Arguments)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeLevelFactory::BeginImportAsset_GameThread);

	using namespace UE::Interchange::Private::InterchangeLevelFactory;

	UClass* TargetClass = GetFactoryClass();

	if (GetFactoryNode(Arguments, TargetClass) == nullptr)
	{
		return {};
	}

	UWorld* WorldAsset = Cast<UWorld>(FindOrCreateAsset(Arguments, TargetClass));

	if (!WorldAsset)
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("Could not create world asset %s"), *Arguments.AssetName);
	}
	//Getting the file Hash will cache it into the source data
	else if (ensure(Arguments.SourceData))
	{
		Arguments.SourceData->GetFileContentHash();
	}

#if WITH_EDITOR
	UInterchangeEditorUtilitiesBase* EditorUtilities = UInterchangeManager::GetInterchangeManager().GetEditorUtilities();
	if ((!EditorUtilities || !EditorUtilities->IsRuntimeOrPIE()) && WorldAsset)
	{
		WorldAsset->PreEditChange(nullptr);
	}
#endif //WITH_EDITOR

	FImportAssetResult Result;
	Result.ImportedObject = WorldAsset;

	return Result;
}

void UInterchangeLevelFactory::SetupObject_GameThread(const FSetupObjectParams& Arguments)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeLevelFactory::SetupObject_GameThread);
	Super::SetupObject_GameThread(Arguments);
	if (!ensure(IsInGameThread()))
	{
		return;
	}

	UWorld* World = Cast<UWorld>(Arguments.ImportedObject);
	UInterchangeLevelFactoryNode* FactoryNode = Cast<UInterchangeLevelFactoryNode>(Arguments.FactoryNode);
	if (ensure(FactoryNode && World && Arguments.SourceData))
	{
		/** Apply all FactoryNode custom attributes to the level sequence asset */
		FactoryNode->ApplyAllCustomAttributeToObject(World);

		//The scene import data object will add them self to this world has a sub object
	}
}


#undef LOCTEXT_NAMESPACE