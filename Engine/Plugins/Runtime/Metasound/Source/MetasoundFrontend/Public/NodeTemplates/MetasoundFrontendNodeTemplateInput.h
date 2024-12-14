// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendNodeTemplateRegistry.h"
#include "MetasoundFrontendNodeTemplateReroute.h"
#include "MetasoundFrontendTransform.h"
#include "Templates/SharedPointer.h"
#include "UObject/NoExportTypes.h"


// Forward Declarations
struct FMetaSoundFrontendDocumentBuilder;


namespace Metasound::Frontend
{
	// Specialized node that connects an input node's single output to various input destinations.
	// While similar to reroute nodes, primarily exists to visually distinguish an input having multiple
	// locations in a visual graph while sharing implementation at runtime, while also differentiating
	// general input style from a typical reroute.
	class METASOUNDFRONTEND_API FInputNodeTemplate : public FRerouteNodeTemplate
	{
	public:
		static const FMetasoundFrontendClassName ClassName;
		static const FMetasoundFrontendVersionNumber VersionNumber;

		static const FInputNodeTemplate& GetChecked();
		static const FNodeRegistryKey& GetRegistryKey();

		virtual ~FInputNodeTemplate() = default;

#if WITH_EDITOR
		// Adds template node and connects with the input of the provided name on the page with
		// the provided id (defaults to builder's build page ID if not provided)
		static const FMetasoundFrontendNode* CreateNode(FMetaSoundFrontendDocumentBuilder& InOutBuilder, FName InputName, const FGuid* InPageID = nullptr);
#endif // WITH_EDITOR

		virtual const TArray<FMetasoundFrontendClassInputDefault>* FindNodeClassInputDefaults(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, FName VertexName) const override;
		virtual const FMetasoundFrontendClassName& GetClassName() const override;

#if WITH_EDITOR
		virtual FText GetNodeDisplayName(const IMetaSoundDocumentInterface& Interface, const FGuid& InPageID, const FGuid& InNodeID) const override;
#endif // WITH_EDITOR

		virtual const FMetasoundFrontendClass& GetFrontendClass() const override;
		virtual EMetasoundFrontendVertexAccessType GetNodeInputAccessType(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, const FGuid& InVertexID) const override;
		virtual EMetasoundFrontendVertexAccessType GetNodeOutputAccessType(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, const FGuid& InVertexID) const override;

#if WITH_EDITOR
		virtual FText GetOutputVertexDisplayName(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, FName OutputName) const override;
		virtual bool HasRequiredConnections(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, FString* OutMessage = nullptr) const override;

		// Injects template nodes between builder's document inputs not connected
		// to existing template inputs, copying locational data from the represented
		// input metadata. If bForceNodeCreation is false, only generates a template
		// input node if a connection between the input and other nodes exists. If true,
		// will inject template node irrespective of whether or not the input has connections.
		bool Inject(FMetaSoundFrontendDocumentBuilder& InOutBuilder, bool bForceNodeCreation = false) const;
#endif // WITH_EDITOR

		virtual bool IsInputAccessTypeDynamic() const override;
		virtual bool IsInputConnectionUserModifiable() const override;
		virtual bool IsOutputAccessTypeDynamic() const override;
	};
} // namespace Metasound::Frontend
