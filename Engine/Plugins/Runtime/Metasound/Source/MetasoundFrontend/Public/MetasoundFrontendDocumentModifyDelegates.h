// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Delegates/DelegateCombinations.h"
#include "MetasoundFrontendDocument.h"
#include "Templates/SharedPointer.h"


// TODO: Move these to namespace
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMetaSoundFrontendDocumentMutateArray, int32 /* Index */);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMetaSoundFrontendDocumentMutateInterfaceArray, const FMetasoundFrontendInterface& /* Interface */);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMetaSoundFrontendDocumentRemoveSwappingArray, int32 /* Index */, int32 /* LastIndex */);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMetaSoundFrontendDocumentRenameClass, const int32 /* Index */, const FMetasoundFrontendClassName& /* NewName */);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnMetaSoundFrontendDocumentMutateNodeInputLiteralArray, int32 /* NodeIndex */, int32 /* VertexIndex */, int32 /* LiteralIndex */);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMetaSoundFrontendDocumentRenameVertex, FName /* OldName */, FName /* NewName */);

namespace Metasound::Frontend
{
	struct METASOUNDFRONTEND_API FDocumentMutatePageArgs
	{
		FGuid PageID;
	};

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDocumentPageAdded, const FDocumentMutatePageArgs& /* Args */);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDocumentRemovingPage, const FDocumentMutatePageArgs& /* Args */);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDocumentPageSet, const FDocumentMutatePageArgs& /* Args */);

	struct METASOUNDFRONTEND_API FPageModifyDelegates
	{
		FOnDocumentPageAdded OnPageAdded;
		FOnDocumentRemovingPage OnRemovingPage;
		FOnDocumentPageSet OnPageSet;
	};

	struct METASOUNDFRONTEND_API FInterfaceModifyDelegates
	{
		FOnMetaSoundFrontendDocumentMutateInterfaceArray OnInterfaceAdded;
		FOnMetaSoundFrontendDocumentMutateInterfaceArray OnRemovingInterface;

		FOnMetaSoundFrontendDocumentMutateArray OnInputAdded;
		FOnMetaSoundFrontendDocumentMutateArray OnInputDefaultChanged;
		FOnMetaSoundFrontendDocumentRenameVertex OnInputNameChanged;
		FOnMetaSoundFrontendDocumentMutateArray OnRemovingInput;

		FOnMetaSoundFrontendDocumentMutateArray OnOutputAdded;
		FOnMetaSoundFrontendDocumentRenameVertex OnOutputNameChanged;
		FOnMetaSoundFrontendDocumentMutateArray OnRemovingOutput;
	};

	struct METASOUNDFRONTEND_API FNodeModifyDelegates
	{
		FOnMetaSoundFrontendDocumentMutateArray OnNodeAdded;
		FOnMetaSoundFrontendDocumentRemoveSwappingArray OnRemoveSwappingNode;

		FOnMetaSoundFrontendDocumentMutateNodeInputLiteralArray OnNodeInputLiteralSet;
		FOnMetaSoundFrontendDocumentMutateNodeInputLiteralArray OnRemovingNodeInputLiteral;
	};

	struct METASOUNDFRONTEND_API FEdgeModifyDelegates
	{
		FOnMetaSoundFrontendDocumentMutateArray OnEdgeAdded;
		FOnMetaSoundFrontendDocumentRemoveSwappingArray OnRemoveSwappingEdge;
	};

	struct METASOUNDFRONTEND_API FDocumentModifyDelegates : TSharedFromThis<FDocumentModifyDelegates>
	{
		FDocumentModifyDelegates();
		FDocumentModifyDelegates(const FDocumentModifyDelegates& InModifyDelegates);
		FDocumentModifyDelegates(FDocumentModifyDelegates&&);
		FDocumentModifyDelegates(const FMetasoundFrontendDocument& Document);
		FDocumentModifyDelegates& operator=(const FDocumentModifyDelegates&);
		FDocumentModifyDelegates& operator=(FDocumentModifyDelegates&&);


		FOnMetaSoundFrontendDocumentMutateArray OnDependencyAdded;
		FOnMetaSoundFrontendDocumentRemoveSwappingArray OnRemoveSwappingDependency;
		FOnMetaSoundFrontendDocumentRenameClass OnRenamingDependencyClass;

		FPageModifyDelegates PageDelegates;
		FInterfaceModifyDelegates InterfaceDelegates;

		UE_DEPRECATED(5.5, "Public exposition of NodeDelegates will be removed in a future build.  Use accessor 'FindNodeDelegates' instead")
		FNodeModifyDelegates NodeDelegates;

		UE_DEPRECATED(5.5, "Public exposition of EdgeDelegates will be removed in a future build.  Use accessor 'FindEdgeDelegates' instead")
		FEdgeModifyDelegates EdgeDelegates;

		void AddPageDelegates(const FGuid& InPageID);
		void RemovePageDelegates(const FGuid& InPageID);

	private:
		TSortedMap<FGuid, FNodeModifyDelegates> PageNodeDelegates;
		TSortedMap<FGuid, FEdgeModifyDelegates> PageEdgeDelegates;

	public:
		FNodeModifyDelegates& FindNodeDelegatesChecked(const FGuid& InPageID);
		FEdgeModifyDelegates& FindEdgeDelegatesChecked(const FGuid& InPageID);

		void IterateGraphEdgeDelegates(TFunctionRef<void(FEdgeModifyDelegates&)> Func)
		{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			Func(EdgeDelegates);

			for (TPair<FGuid, FEdgeModifyDelegates>& Delegates : PageEdgeDelegates)
			{
				Func(Delegates.Value);
			}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}

		void IterateGraphNodeDelegates(TFunctionRef<void(FNodeModifyDelegates&)> Func)
		{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			Func(NodeDelegates);

			for (TPair<FGuid, FNodeModifyDelegates>& Delegates : PageNodeDelegates)
			{
				Func(Delegates.Value);
			}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
	};

	class METASOUNDFRONTEND_API IDocumentBuilderTransactionListener : public TSharedFromThis<IDocumentBuilderTransactionListener>
	{
	public:
		virtual ~IDocumentBuilderTransactionListener() = default;

		// Called when the builder is reloaded, at which point the document cache and delegates are refreshed
		virtual void OnBuilderReloaded(FDocumentModifyDelegates& OutDelegates) = 0;
	};
} // namespace Metasound::Frontend
