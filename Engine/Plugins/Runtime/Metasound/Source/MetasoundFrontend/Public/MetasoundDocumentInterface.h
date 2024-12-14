// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Interface.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentModifyDelegates.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundOperatorSettings.h"
#include "Templates/Function.h"
#include "UObject/TopLevelAssetPath.h"

#include "MetasoundDocumentInterface.generated.h"


// Forward Declarations
struct FMetaSoundFrontendDocumentBuilder;

namespace Metasound::Frontend
{
	struct FAssetKey;
}

// UInterface for all MetaSound UClasses that implement a MetaSound document
// as a means for accessing data via code, scripting, execution, or node
// class generation.
UINTERFACE(BlueprintType, meta = (DisplayName = "MetaSound Document Interface", CannotImplementInterfaceInBlueprint))
class METASOUNDFRONTEND_API UMetaSoundDocumentInterface : public UInterface
{
	GENERATED_BODY()
};

class METASOUNDFRONTEND_API IMetaSoundDocumentInterface : public IInterface
{
	GENERATED_BODY()

public:
	virtual FTopLevelAssetPath GetAssetPathChecked() const = 0;

	// Returns read-only reference to the the MetaSoundFrontendDocument
	// containing all MetaSound runtime & editor data.
	UE_DEPRECATED(5.4, "Use GetConstDocument instead")
	virtual const FMetasoundFrontendDocument& GetDocument() const 
	{
		return GetConstDocument();
	}

	// Returns read-only reference to the the MetaSoundFrontendDocument
	// containing all MetaSound runtime & editor data.
	virtual const FMetasoundFrontendDocument& GetConstDocument() const = 0;

	// Returns the parent class registered with the MetaSound UObject registry.
	virtual const UClass& GetBaseMetaSoundUClass() const = 0;

	// Returns the builder class used to modify the given document.
	virtual const UClass& GetBuilderUClass() const = 0;

	// Conforms UProperty data outside the Frontend Document Model to the document's data.
	// Returns whether or not object data was modified.
	virtual bool ConformObjectToDocument() = 0;

	// Returns whether or not a document builder is currently active and can mutate the given interface's document
	virtual bool IsActivelyBuilding() const = 0;

private:
	virtual FMetasoundFrontendDocument& GetDocument() = 0;

	// Derived classes can implement these methods to react to a builder beginning
	// or finishing. Begin and Finish are tied to the lifetime of the active 
	// FMetaSoundFrontendDocumentBuilder.
	virtual void OnBeginActiveBuilder() = 0;
	virtual void OnFinishActiveBuilder() = 0;

	friend struct FMetaSoundFrontendDocumentBuilder;
};

namespace Metasound::Frontend
{
	class METASOUNDFRONTEND_API IDocumentBuilderRegistry
	{
	public:
		virtual ~IDocumentBuilderRegistry() = default;

#if WITH_EDITORONLY_DATA
		// Given the provided builder, removes paged data within the associated document for a cooked build.
		// This function removes graphs and input defaults which are not to ever be used by a given cook
		// platform, allowing users to optimize away data and scale the amount of memory required for
		// initial load of input UObjects and graph topology, which can also positively effect runtime
		// performance as well, etc. Returns true if builder modified the document, false if not.
		virtual bool CookPages(FName CookPlatformName, FMetaSoundFrontendDocumentBuilder& Builder) const = 0;
#endif // WITH_EDITORONLY_DATA

		virtual FMetaSoundFrontendDocumentBuilder* FindBuilder(TScriptInterface<IMetaSoundDocumentInterface> MetaSound) const = 0;
		virtual FMetaSoundFrontendDocumentBuilder* FindBuilder(const FMetasoundFrontendClassName& ClassName, const FTopLevelAssetPath& AssetPath) const = 0;
		virtual FMetaSoundFrontendDocumentBuilder* FindOutermostBuilder(const UObject& InSubObject) const = 0;

#if WITH_EDITORONLY_DATA
		// Find the existing builder for the given MetaSound, or optionally begin building by attaching a new builder.  Only available
		// in builds with editor only data as building serialized assets (which may have template nodes, cooked builds do not) is only
		// supported when editor data is loaded. Creating transient builders can simply be done by passing a new MetaSound asset to
		// a FMetaSoundFrontendDocumentBuilder constructor, or this registry's implementation may supply its own create call for tracking
		// and reuse purposes.
		virtual FMetaSoundFrontendDocumentBuilder& FindOrBeginBuilding(TScriptInterface<IMetaSoundDocumentInterface> MetaSound) = 0;
#endif // WITH_EDITORONLY_DATA

		// Removes builder from registry, clearing any cached builder state. (Optionally) forces unregistration from the Frontend Node Class Registry
		// (If the builder has outstanding transactions, unregistration from the Node Class Registry will occur regardless).
		virtual bool FinishBuilding(const FMetasoundFrontendClassName& InClassName, bool bForceUnregisterNodeClass = false) const = 0;
		virtual bool FinishBuilding(const FMetasoundFrontendClassName& InClassName, const FTopLevelAssetPath& InAssetPath, bool bForceUnregisterNodeClass = false) const = 0;

		UE_DEPRECATED(5.5, "Document cache can now be invalidated by retrieving an asset builder and calling 'ReloadBuilder'")
		virtual void InvalidateDocumentCache(const FMetasoundFrontendClassName& InClassName) const { }

		// Reloads the given builder, maintaining all modify delegate subscriptions. Returns true if builder was found and reloaded,
		// false if not found.
		virtual bool ReloadBuilder(const FMetasoundFrontendClassName& InClassName) const = 0;

		// Given the provided document and its respective pages, provides the PageID to be used for runtime IGraph and proxy generation.
		// Returns true if valid page was found, false if not.
		virtual FGuid ResolveTargetPageID(const FMetasoundFrontendGraphClass& InGraphClass) const = 0;

		// Given the provided input and its respective paged default values, provides the default PageID to be used for runtime IGraph and proxy generation.
		// Returns true if valid page was found, false if not.
		virtual FGuid ResolveTargetPageID(const FMetasoundFrontendClassInput& InClassInput) const = 0;

		// Given the provided array of default values, provides the default PageID to be used for runtime IGraph and proxy generation.
		// Returns true if valid page was found, false if not.
		virtual FGuid ResolveTargetPageID(const TArray<FMetasoundFrontendClassInputDefault>& Defaults) const = 0;

		static IDocumentBuilderRegistry* Get();
		static IDocumentBuilderRegistry& GetChecked();
		static void Deinitialize();
		static void Initialize(TUniquePtr<IDocumentBuilderRegistry>&& InBuilderRegistry);

	protected:
		UE_DEPRECATED(5.5, "Use 'Initialize' instead")
		static void Set(TUniqueFunction<IDocumentBuilderRegistry&()>&& InGetInstance) { checkNoEntry(); }
	};

	class METASOUNDFRONTEND_API IMetaSoundDocumentBuilderRegistry : public IDocumentBuilderRegistry
	{
	public:
		UE_DEPRECATED(5.5, "Class Renamed: use IDocumentBuilderRegistry instead")
		IMetaSoundDocumentBuilderRegistry() = default;
		virtual ~IMetaSoundDocumentBuilderRegistry() = default;

		UE_DEPRECATED(5.4, "Public exposition of modify delegates no longer available to discourage unsafe manipulation of builder document cache")
		virtual const FDocumentModifyDelegates* FindModifyDelegates(const FMetasoundFrontendClassName& InClassName) const { return nullptr; }

		UE_DEPRECATED(5.4, "Use 'IDocumentBuilderRegistry' instead")
		static IMetaSoundDocumentBuilderRegistry& GetChecked();
	};
} // namespace Metasound::Frontend
