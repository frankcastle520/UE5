// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Metasound.h"
#include "MetasoundAssetManager.h"
#include "MetasoundDocumentBuilderRegistry.h"
#include "MetasoundEngineModule.h"
#include "MetasoundFrontendDocumentIdGenerator.h"
#include "MetasoundFrontendRegistryKey.h"
#include "MetasoundGlobals.h"
#include "MetasoundSettings.h"
#include "MetasoundUObjectRegistry.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Serialization/Archive.h"

#if WITH_EDITORONLY_DATA
#include "Algo/Transform.h"
#include "Interfaces/ITargetPlatform.h"
#include "MetasoundFrontendRegistryContainer.h"
#include "Misc/DataValidation.h"
#include "UObject/GarbageCollection.h"
#include "UObject/ObjectMacros.h"
#include "UObject/StrongObjectPtrTemplates.h"
#endif // WITH_EDITORONLY_DATA

#define LOCTEXT_NAMESPACE "MetasoundEngine"

namespace Metasound::Engine
{
	/** MetaSound Engine Asset helper provides routines for UObject based MetaSound assets. 
	 * Any UObject deriving from FMetaSoundAssetBase should use these helper functions
	 * in their UObject overrides. 
	 */
	struct FAssetHelper
	{
		static bool SerializationRequiresDeterminism(bool bIsCooking)
		{
			return bIsCooking || IsRunningCookCommandlet();
		}

#if WITH_EDITOR
		static void PreDuplicate(TScriptInterface<IMetaSoundDocumentInterface> MetaSound, FObjectDuplicationParameters& DupParams)
		{
			FDocumentBuilderRegistry::GetChecked().SetEventLogVerbosity(FDocumentBuilderRegistry::ELogEvent::DuplicateEntries, ELogVerbosity::NoLogging);
		}

		static void PostDuplicate(TScriptInterface<IMetaSoundDocumentInterface> MetaSound, EDuplicateMode::Type InDuplicateMode, FGuid& OutAssetClassID)
		{
			using namespace Engine;
			using namespace Frontend;

			if (InDuplicateMode == EDuplicateMode::Normal)
			{
				UObject* MetaSoundObject = MetaSound.GetObject();
				check(MetaSoundObject);

				FDocumentBuilderRegistry& BuilderRegistry = FDocumentBuilderRegistry::GetChecked();
				UMetaSoundBuilderBase& DuplicateBuilder = BuilderRegistry.FindOrBeginBuilding(*MetaSoundObject);

				FMetaSoundFrontendDocumentBuilder& DocBuilder = DuplicateBuilder.GetBuilder();
				const FMetasoundFrontendClassName DuplicateName = DocBuilder.GetConstDocumentChecked().RootGraph.Metadata.GetClassName();
				const FMetasoundFrontendClassName NewName = DocBuilder.GenerateNewClassName();
				ensureAlwaysMsgf(IMetaSoundAssetManager::GetChecked().TryGetAssetIDFromClassName(NewName, OutAssetClassID), TEXT("Failed to retrieve newly duplicated MetaSoundClassName AssetID"));

				constexpr bool bForceUnregisterNodeClass = true;
				BuilderRegistry.FinishBuilding(DuplicateName, MetaSound->GetAssetPathChecked(), bForceUnregisterNodeClass);
				BuilderRegistry.SetEventLogVerbosity(FDocumentBuilderRegistry::ELogEvent::DuplicateEntries, ELogVerbosity::All);
			}
		}

		template <typename TMetaSoundObject>
		static void PostEditUndo(TMetaSoundObject& InMetaSound)
		{
			InMetaSound.GetModifyContext().SetForceRefreshViews();

			const FMetasoundFrontendClassName& ClassName = InMetaSound.GetConstDocument().RootGraph.Metadata.GetClassName();
			Frontend::IDocumentBuilderRegistry::GetChecked().ReloadBuilder(ClassName);

			if (UMetasoundEditorGraphBase* Graph = Cast<UMetasoundEditorGraphBase>(InMetaSound.GetGraph()))
			{
				Graph->RegisterGraphWithFrontend();
			}
		}

		template<typename TMetaSoundObject>
		static void SetReferencedAssetClasses(TMetaSoundObject& InMetaSound, TSet<Frontend::IMetaSoundAssetManager::FAssetInfo>&& InAssetClasses)
		{
			using namespace Frontend;
			
			InMetaSound.ReferencedAssetClassKeys.Reset();
			InMetaSound.ReferencedAssetClassObjects.Reset();

			for (const IMetaSoundAssetManager::FAssetInfo& AssetClass : InAssetClasses)
			{
				InMetaSound.ReferencedAssetClassKeys.Add(AssetClass.RegistryKey.ToString());
				if (UObject* Object = AssetClass.AssetPath.TryLoad())
				{
					InMetaSound.ReferencedAssetClassObjects.Add(Object);
				}
				else
				{
					UE_LOG(LogMetaSound, Error, TEXT("Failed to load referenced asset %s from asset %s"), *AssetClass.AssetPath.ToString(), *InMetaSound.GetPathName());
				}
			}
		}

		static EDataValidationResult IsClassNameUnique(const FMetasoundFrontendDocument& Document, FDataValidationContext& InOutContext)
		{
			using namespace Metasound::Frontend;
			using namespace Metasound::Engine;

			EDataValidationResult Result = EDataValidationResult::Valid;

			// Need to prime asset registry to look for duplicate class names
			IMetasoundEngineModule& MetaSoundEngineModule = FModuleManager::GetModuleChecked<IMetasoundEngineModule>("MetaSoundEngine");
			// Checking for duplicate class names only requires the asset manager to be primed, but not for assets to be loaded. 
			if (!MetaSoundEngineModule.IsAssetManagerPrimed())
			{
				MetaSoundEngineModule.PrimeAssetManager();
				// Check again, as priming relies on the asset registry being loaded so may not be complete
				if (!MetaSoundEngineModule.IsAssetManagerPrimed())
				{
					Result = EDataValidationResult::Invalid;
					InOutContext.AddError(LOCTEXT("UniqueClassNameAssetManagerNotReady",
						"MetaSound Asset Manager was unable to be primed to check for unique class names. This may be because the asset registry has not finished loading assets. Please try again later."));
					return Result;
				}
			}

			IMetaSoundAssetManager& AssetManager = IMetaSoundAssetManager::GetChecked();
			// Validation has added assets to the asset manager
			// and we don't remove them immediately after validation to optimize possible subsequent validation
			// Set this flag to prevent log spam of active assets on shutdown
			AssetManager.SetLogActiveAssetsOnShutdown(false);

			// Add error for multiple assets with the same class name
			const FAssetKey Key(Document.RootGraph.Metadata);
			const TArray<FTopLevelAssetPath> AssetPaths = AssetManager.FindAssetPaths(Key);
			if (AssetPaths.Num() > 1)
			{
				Result = EDataValidationResult::Invalid;

				TArray<FText> PathStrings;
				Algo::Transform(AssetPaths, PathStrings, [](const FTopLevelAssetPath& Path) { return FText::FromString(Path.ToString()); });
				InOutContext.AddError(FText::Format(LOCTEXT("UniqueClassNameValidation",
					"Multiple assets use the same class name which may result in unintended behavior. This may happen when an asset is moved, then the move is reverted in revision control without removing the newly created asset. Please remove the offending asset or duplicate it to automatically generate a new class name." \
					"\nConflicting Asset Paths:\n{0}"), FText::Join(FText::FromString(TEXT("\n")), PathStrings)));
			}

			// Success
			return Result;
		}

		static EDataValidationResult IsDataValid(const UObject& MetaSound, const FMetasoundFrontendDocument& Document, FDataValidationContext& InOutContext)
		{
			using namespace Metasound::Engine;

			EDataValidationResult Result = EDataValidationResult::Valid;
			if (MetasoundEngineModulePrivate::EnableMetaSoundEditorAssetValidation)
			{
				Result = IsClassNameUnique(Document, InOutContext);
			}

			const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>();
			check(Settings);

			TSet<FGuid> ValidPageIDs;
			auto ErrorIfMissing = [&](const FGuid& PageID, const FText& DataDescriptor)
			{
				if (!ValidPageIDs.Contains(PageID))
				{
					if (const FMetaSoundPageSettings* PageSettings = Settings->FindPageSettings(PageID))
					{
						ValidPageIDs.Add(PageSettings->UniqueId);
					}
					else
					{
						Result = EDataValidationResult::Invalid;
						InOutContext.AddMessage(FAssetData(&MetaSound), EMessageSeverity::Error, FText::Format(
							LOCTEXT("InvalidPageDataFormat", "MetaSound contains invalid {0} with page ID '{1}': page not found in Project 'MetaSound' Settings. Remove page data or migrate to existing page identifier."),
							DataDescriptor,
							FText::FromString(PageID.ToString())));
					}
				}
			};

			const TArray<FMetasoundFrontendGraph>& Graphs = Document.RootGraph.GetConstGraphPages();
			for (const FMetasoundFrontendGraph& Graph : Graphs)
			{
				ErrorIfMissing(Graph.PageID, LOCTEXT("GraphPageDescriptor", "graph"));
			}

			for (const FMetasoundFrontendClassInput& ClassInput : Document.RootGraph.Interface.Inputs)
			{
				ClassInput.IterateDefaults([&](const FGuid& PageID, const FMetasoundFrontendLiteral&)
				{
					ErrorIfMissing(PageID, FText::Format(LOCTEXT("InputPageDefaultDescriptorFormat", "input '{0}' default value"), FText::FromName(ClassInput.Name)));
				});
			}
			return Result;
		}

#endif // WITH_EDITOR

		template <typename TMetaSoundObject>
		static FTopLevelAssetPath GetAssetPathChecked(TMetaSoundObject& InMetaSound)
		{
			FTopLevelAssetPath Path;
			ensureAlwaysMsgf(Path.TrySetPath(&InMetaSound), TEXT("Failed to set TopLevelAssetPath from MetaSound '%s'. MetaSound must be highest level object in package."), *InMetaSound.GetPathName());
			ensureAlwaysMsgf(Path.IsValid(), TEXT("Failed to set TopLevelAssetPath from MetaSound '%s'. This may be caused by calling this function when the asset is being destroyed."), *InMetaSound.GetPathName());
			return Path;
		}

		template <typename TMetaSoundObject>
		static TArray<FMetasoundAssetBase*> GetReferencedAssets(TMetaSoundObject& InMetaSound)
		{
			TArray<FMetasoundAssetBase*> ReferencedAssets;

			IMetasoundUObjectRegistry& UObjectRegistry = IMetasoundUObjectRegistry::Get();

			for (TObjectPtr<UObject>& Object : InMetaSound.ReferencedAssetClassObjects)
			{
				if (FMetasoundAssetBase* Asset = UObjectRegistry.GetObjectAsAssetBase(Object))
				{
					ReferencedAssets.Add(Asset);
				}
				else
				{
					UE_LOG(LogMetaSound, Error, TEXT("Referenced asset \"%s\", referenced from \"%s\", is not convertible to FMetasoundAssetBase"), *Object->GetPathName(), *InMetaSound.GetPathName());
				}
			}

			return ReferencedAssets;
		}

		static void PreSaveAsset(FMetasoundAssetBase& InMetaSound, FObjectPreSaveContext InSaveContext)
		{
#if WITH_EDITORONLY_DATA
			using namespace Frontend;

			if (IMetaSoundAssetManager* AssetManager = IMetaSoundAssetManager::Get())
			{
				AssetManager->WaitUntilAsyncLoadReferencedAssetsComplete(InMetaSound);
			}

			const bool bIsCooking = InSaveContext.IsCooking();
			const bool bCanEverExecute = Metasound::CanEverExecuteGraph(bIsCooking);
			if (!bCanEverExecute)
			{
				FName PlatformName;
				if (const ITargetPlatform* TargetPlatform = InSaveContext.GetTargetPlatform())
				{
					PlatformName = *TargetPlatform->IniPlatformName();
				}
				const bool bIsDeterministic = SerializationRequiresDeterminism(bIsCooking);
				FDocumentIDGenerator::FScopeDeterminism DeterminismScope(bIsDeterministic);
				InMetaSound.UpdateAndRegisterForSerialization(PlatformName);
			}
 			else if (FApp::CanEverRenderAudio())
			{
				if (UMetasoundEditorGraphBase* MetaSoundGraph = Cast<UMetasoundEditorGraphBase>(InMetaSound.GetGraph()))
				{
					// Uses graph flavor of register with frontend to update editor systems/asset editors in case editor is enabled.
					MetaSoundGraph->RegisterGraphWithFrontend();
					InMetaSound.GetModifyContext().SetForceRefreshViews();
				}
			}
			else
			{
				UE_LOG(LogMetaSound, Warning, TEXT("PreSaveAsset for MetaSound: (%s) is doing nothing because InSaveContext.IsCooking, IsRunningCommandlet, and FApp::CanEverRenderAudio were all false")
					, *InMetaSound.GetOwningAssetName());
			}
#endif // WITH_EDITORONLY_DATA
		}

		template <typename TMetaSoundObject>
		static void SerializeToArchive(TMetaSoundObject& InMetaSound, FArchive& InArchive)
		{
#if WITH_EDITORONLY_DATA
			using namespace Frontend;

			bool bVersionedAsset = false;

			if (InArchive.IsLoading())
			{
				const bool bIsTransacting = InArchive.IsTransacting();
				TStrongObjectPtr<UMetaSoundBuilderBase> Builder;
				{
					FGCScopeGuard ScopeGuard;
					Builder.Reset(&FDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(InMetaSound, bIsTransacting));
				}

				{
					const bool bIsCooking = InArchive.IsCooking();
					const bool bIsDeterministic = SerializationRequiresDeterminism(bIsCooking);
					FDocumentIDGenerator::FScopeDeterminism DeterminismScope(bIsDeterministic);
					check(Builder.IsValid());
					bVersionedAsset = InMetaSound.VersionAsset(Builder->GetBuilder());
				}

				Builder->ClearInternalFlags(EInternalObjectFlags::Async);
			}

			if (bVersionedAsset)
			{
				InMetaSound.SetVersionedOnLoad();
			}
#endif // WITH_EDITORONLY_DATA
		}

		template<typename TMetaSoundObject>
		static void PostLoad(TMetaSoundObject& InMetaSound)
		{
			using namespace Frontend;
			// Do not call asset manager on CDO objects which may be loaded before asset 
			// manager is set.
			const bool bIsCDO = InMetaSound.HasAnyFlags(RF_ClassDefaultObject);
			if (!bIsCDO)
			{
				if (InMetaSound.GetAsyncReferencedAssetClassPaths().Num() > 0)
				{
					IMetaSoundAssetManager::GetChecked().RequestAsyncLoadReferencedAssets(InMetaSound);
				}
			}
		}

		template<typename TMetaSoundObject>
		static void OnAsyncReferencedAssetsLoaded(TMetaSoundObject& InMetaSound, const TArray<FMetasoundAssetBase*>& InAsyncReferences)
		{
			for (FMetasoundAssetBase* AssetBase : InAsyncReferences)
			{
				if (AssetBase)
				{
					if (UObject* OwningAsset = AssetBase->GetOwningAsset())
					{
						InMetaSound.ReferencedAssetClassObjects.Add(OwningAsset);
						InMetaSound.ReferenceAssetClassCache.Remove(FSoftObjectPath(OwningAsset));
					}
				}
			}
		}

#if WITH_EDITORONLY_DATA
		template <typename TMetaSoundObject>
		static void SetMetaSoundRegistryAssetClassInfo(TMetaSoundObject& InMetaSound, const Frontend::FNodeClassInfo& InClassInfo)
		{
			using namespace Frontend;

			check(AssetTags::AssetClassID == GET_MEMBER_NAME_CHECKED(TMetaSoundObject, AssetClassID));
			check(AssetTags::IsPreset == GET_MEMBER_NAME_CHECKED(TMetaSoundObject, bIsPreset));
			check(AssetTags::RegistryInputTypes == GET_MEMBER_NAME_CHECKED(TMetaSoundObject, RegistryInputTypes));
			check(AssetTags::RegistryOutputTypes == GET_MEMBER_NAME_CHECKED(TMetaSoundObject, RegistryOutputTypes));
			check(AssetTags::RegistryVersionMajor == GET_MEMBER_NAME_CHECKED(TMetaSoundObject, RegistryVersionMajor));
			check(AssetTags::RegistryVersionMinor == GET_MEMBER_NAME_CHECKED(TMetaSoundObject, RegistryVersionMinor));

			bool bMarkDirty = InMetaSound.AssetClassID != InClassInfo.AssetClassID;
			bMarkDirty |= InMetaSound.RegistryVersionMajor != InClassInfo.Version.Major;
			bMarkDirty |= InMetaSound.RegistryVersionMinor != InClassInfo.Version.Minor;
			bMarkDirty |= InMetaSound.bIsPreset != InClassInfo.bIsPreset;

			InMetaSound.AssetClassID = InClassInfo.AssetClassID;
			InMetaSound.RegistryVersionMajor = InClassInfo.Version.Major;
			InMetaSound.RegistryVersionMinor = InClassInfo.Version.Minor;
			InMetaSound.bIsPreset = InClassInfo.bIsPreset;

			{
				TArray<FString> InputTypes;
				Algo::Transform(InClassInfo.InputTypes, InputTypes, [](const FName& Name) { return Name.ToString(); });

				const FString TypeString = FString::Join(InputTypes, *AssetTags::ArrayDelim);
				bMarkDirty |= InMetaSound.RegistryInputTypes != TypeString;
				InMetaSound.RegistryInputTypes = TypeString;
			}

			{
				TArray<FString> OutputTypes;
				Algo::Transform(InClassInfo.OutputTypes, OutputTypes, [](const FName& Name) { return Name.ToString(); });

				const FString TypeString = FString::Join(OutputTypes, *AssetTags::ArrayDelim);
				bMarkDirty |= InMetaSound.RegistryOutputTypes != TypeString;
				InMetaSound.RegistryOutputTypes = TypeString;
			}
		}
#endif // WITH_EDITORONLY_DATA
	};
} // namespace Metasound::Engine
#undef LOCTEXT_NAMESPACE // MetasoundEngine
