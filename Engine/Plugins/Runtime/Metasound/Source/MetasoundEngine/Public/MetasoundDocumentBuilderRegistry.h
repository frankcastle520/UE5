// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Algo/Find.h"
#include "Async/Async.h"
#include "MetasoundBuilderBase.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundLog.h"
#include "Misc/Optional.h"
#include "Misc/ScopeLock.h"


// Forward Declarations
class UMetaSoundSettings;

namespace Metasound::Engine
{
#if WITH_EDITOR
	struct FPageResolutionEditorResults
	{
		FName PlatformName;
		TOptional<FGuid> PageID;
	};

	DECLARE_DELEGATE_RetVal_OneParam(FPageResolutionEditorResults, FOnResolveEditorPage, const TArray<FGuid>& /* InPageIDs */);
#endif // WITH_EDITOR

	DECLARE_DELEGATE_RetVal_OneParam(FGuid, FOnResolvePage, const TArray<FGuid>& /* InPageIDs */);

	class METASOUNDENGINE_API FDocumentBuilderRegistry : public Frontend::IDocumentBuilderRegistry
	{
		mutable TMultiMap<FMetasoundFrontendClassName, TWeakObjectPtr<UMetaSoundBuilderBase>> Builders;

		// Critical section primarily for allowing builder collection mutation during async loading of MetaSound assets.
		mutable FCriticalSection BuildersCriticalSection;

	public:
		FDocumentBuilderRegistry() = default;
		virtual ~FDocumentBuilderRegistry();

		static FDocumentBuilderRegistry& GetChecked()
		{
			return static_cast<FDocumentBuilderRegistry&>(IDocumentBuilderRegistry::GetChecked());
		}

		enum class ELogEvent : uint8
		{
			DuplicateEntries
		};

		template <typename BuilderClass>
		BuilderClass& CreateTransientBuilder(FName BuilderName = FName())
		{
			using namespace Metasound::Frontend;

			checkf(IsInGameThread(), TEXT("Transient MetaSound builder cannot be created in non - game thread as it may result in UObject creation"));

			const EObjectFlags NewObjectFlags = RF_Public | RF_Transient;
			UPackage* TransientPackage = GetTransientPackage();
			const FName ObjectName = MakeUniqueObjectName(TransientPackage, BuilderClass::StaticClass(), BuilderName);
			TObjectPtr<BuilderClass> NewBuilder = NewObject<BuilderClass>(TransientPackage, ObjectName, NewObjectFlags);
			check(NewBuilder);
			NewBuilder->Initialize();
			const FMetasoundFrontendDocument& Document = NewBuilder->GetConstBuilder().GetConstDocumentChecked();
			const FMetasoundFrontendClassName& ClassName = Document.RootGraph.Metadata.GetClassName();
			Builders.Add(ClassName, NewBuilder);
			return *NewBuilder.Get();
		}

#if WITH_EDITORONLY_DATA
		// Find or begin building a MetaSound asset.  Optionally, provide
		// whether or not the builder is being accessed during a transaction.
		// If false, enforces MetaSound being built is an asset.  If true, does
		// not enforce (transactions may result in assets being moved and becoming
		// transient wherein the builder can and should be valid to act on the
		// transient UObject in these rare cases).
		template <typename BuilderClass = UMetaSoundBuilderBase>
		BuilderClass& FindOrBeginBuilding(UObject& InMetaSoundObject, bool bIsTransacting = false) const
		{
			if (!bIsTransacting)
			{
				check(InMetaSoundObject.IsAsset());
			}

			TScriptInterface<IMetaSoundDocumentInterface> DocInterface = &InMetaSoundObject;
			check(DocInterface.GetObject());

			if (UMetaSoundBuilderBase* Builder = FindBuilderObject(&InMetaSoundObject))
			{
				return *CastChecked<BuilderClass>(Builder);
			}

			FNameBuilder BuilderName;
			BuilderName.Append(InMetaSoundObject.GetName());
			BuilderName.Append(TEXT("_Builder"));
			const UClass& BuilderUClass = DocInterface->GetBuilderUClass();
			const FName NewName = MakeUniqueObjectName(nullptr, &BuilderUClass, FName(*BuilderName));

			TObjectPtr<UMetaSoundBuilderBase> NewBuilder;
			{
				FScopeLock Lock(&BuildersCriticalSection);
				NewBuilder = CastChecked<UMetaSoundBuilderBase>(NewObject<UObject>(&InMetaSoundObject, &BuilderUClass, NewName, RF_Transactional));
				FMetaSoundFrontendDocumentBuilder& BuilderRef = NewBuilder->GetBuilder();
				BuilderRef = FMetaSoundFrontendDocumentBuilder(DocInterface);
				const FMetasoundFrontendDocument& Document = DocInterface->GetConstDocument();
				const FMetasoundFrontendClassName& ClassName = Document.RootGraph.Metadata.GetClassName();
				if (!ClassName.IsValid())
				{
					BuilderRef.InitDocument();
				}

				checkf(ClassName.IsValid(), TEXT("Document initialization must result in a valid class name being generated"));
				AddBuilderInternal(ClassName, NewBuilder);
			}

			return *CastChecked<BuilderClass>(NewBuilder);
		}
#endif // WITH_EDITORONLY_DATA

		// Frontend::IDocumentBuilderRegistry Implementation
#if WITH_EDITORONLY_DATA
		// Given the provided builder, removes paged data within the associated document for a cooked build.
		// This function removes graphs and input defaults which are not to ever be used by a given cook
		// platform, allowing users to optimize away data and scale the amount of memory required for
		// initial load of input UObjects and graph topology, which can also positively effect runtime
		// performance as well, etc. Returns true if builder modified the document, false if not.
		virtual bool CookPages(FName PlatformName, FMetaSoundFrontendDocumentBuilder& Builder) const override;
		virtual FMetaSoundFrontendDocumentBuilder& FindOrBeginBuilding(TScriptInterface<IMetaSoundDocumentInterface> MetaSound) override;
#endif // WITH_EDITORONLY_DATA

		virtual FMetaSoundFrontendDocumentBuilder* FindBuilder(TScriptInterface<IMetaSoundDocumentInterface> MetaSound) const override;
		virtual FMetaSoundFrontendDocumentBuilder* FindBuilder(const FMetasoundFrontendClassName& InClassName, const FTopLevelAssetPath& AssetPath) const override;

		virtual FMetaSoundFrontendDocumentBuilder* FindOutermostBuilder(const UObject& InSubObject) const override;

		virtual bool FinishBuilding(const FMetasoundFrontendClassName& InClassName, bool bForceUnregisterNodeClass = false) const override;
		virtual bool FinishBuilding(const FMetasoundFrontendClassName& InClassName, const FTopLevelAssetPath& AssetPath, bool bForceUnregisterNodeClass = false) const override;

		// Returns the builder object associated with the given MetaSound asset if one is registered and active.
		UMetaSoundBuilderBase* FindBuilderObject(TScriptInterface<const IMetaSoundDocumentInterface> MetaSound) const;

		// Returns the builder object associated with the given ClassName if one is registered and active.
		// Optionally, if provided the AssetPath and there is a conflict (i.e. more than one asset is registered
		// with a given ClassName), will return the one with the provided AssetPath.  Otherwise, will arbitrarily
		// return one.
		UMetaSoundBuilderBase* FindBuilderObject(const FMetasoundFrontendClassName& InClassName, const FTopLevelAssetPath& AssetPath) const;

		// Returns all builder objects registered and active associated with the given ClassName.
		TArray<UMetaSoundBuilderBase*> FindBuilderObjects(const FMetasoundFrontendClassName& InClassName) const;

#if WITH_EDITOR
		FOnResolveEditorPage& GetOnResolveAuditionPageDelegate();
#endif // WITH_EDITOR

		FOnResolvePage& GetOnResolveProjectPageOverrideDelegate();

		bool ReloadBuilder(const FMetasoundFrontendClassName& InClassName) const override;

		// Given the provided document and its respective pages, returns the PageID to be used for runtime IGraph and proxy generation.
		virtual FGuid ResolveTargetPageID(const FMetasoundFrontendGraphClass& InGraphClass) const override;
		virtual FGuid ResolveTargetPageID(const FMetasoundFrontendClassInput& InClassInput) const override;
		virtual FGuid ResolveTargetPageID(const TArray<FMetasoundFrontendClassInputDefault>& Defaults) const override;

		void SetEventLogVerbosity(ELogEvent Event, ELogVerbosity::Type Verbosity);

	private:
		void AddBuilderInternal(const FMetasoundFrontendClassName& InClassName, UMetaSoundBuilderBase* NewBuilder) const;
		bool CanPostEventLog(ELogEvent Event, ELogVerbosity::Type Verbosity) const;
		void FinishBuildingInternal(UMetaSoundBuilderBase& Builder, bool bForceUnregisterNodeClass) const;
		FGuid ResolveTargetPageIDInternal(const TArray<FGuid>& PageIdsToResolve) const;
		FGuid ResolveTargetPageIDInternal(const UMetaSoundSettings& Settings, const TArray<FGuid>& PageIdsToResolve, const FGuid& TargetPageID, FName PlatformName) const;

#if WITH_EDITOR
		FOnResolveEditorPage OnResolveAuditionPage;
#endif // WITH_EDITOR

		FOnResolvePage OnResolveProjectPage;

		// Reuseable scratch array of pages to resolve, which is used to
		// optimize/reduce number of allocations required when resolving document.
		mutable TArray<FGuid> TargetPageResolveScratch;
		mutable FCriticalSection TargetPageResolveScratchCritSec;

		TSortedMap<ELogEvent, ELogVerbosity::Type> EventLogVerbosity;
	};
} // namespace Metasound::Engine