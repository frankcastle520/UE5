// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentBuilder.h"
#include "MetasoundFrontendNodeTemplateRegistry.h"
#include "MetasoundFrontendTransform.h"
#include "Templates/SharedPointer.h"
#include "UObject/NoExportTypes.h"


namespace Metasound::Frontend
{
	class METASOUNDFRONTEND_API FRerouteNodeTemplate : public FNodeTemplateBase
	{
	public:
		static const FMetasoundFrontendClassName ClassName;

		static const FMetasoundFrontendVersionNumber VersionNumber;

		static const FNodeRegistryKey& GetRegistryKey();

		UE_DEPRECATED(5.5, "Look-up template via registry and use non-static GenerateNodeInterface instead with provided params")
		static FMetasoundFrontendNodeInterface CreateNodeInterfaceFromDataType(FName InDataType) { return { }; }

		virtual ~FRerouteNodeTemplate() = default;

		virtual const FMetasoundFrontendClassName& GetClassName() const override;

#if WITH_EDITOR
		virtual FText GetNodeDisplayName(const IMetaSoundDocumentInterface& DocumentInterface, const FGuid& InPageID, const FGuid& InNodeID) const override;
#endif // WITH_EDITOR

		UE_DEPRECATED(5.4, "Use version that does not require mutating a provided PreprocessedDocument")
		virtual TUniquePtr<INodeTransform> GenerateNodeTransform(FMetasoundFrontendDocument& InPreprocessedDocument) const override { return { }; }

		virtual FMetasoundFrontendNodeInterface GenerateNodeInterface(FNodeTemplateGenerateInterfaceParams InParams) const override;
		virtual TUniquePtr<INodeTemplateTransform> GenerateNodeTransform() const override;
		virtual const FMetasoundFrontendClass& GetFrontendClass() const override;
		virtual const TArray<FMetasoundFrontendClassInputDefault>* FindNodeClassInputDefaults(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, FName VertexName) const override;
		virtual EMetasoundFrontendVertexAccessType GetNodeInputAccessType(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, const FGuid& InVertexID) const override;
		virtual EMetasoundFrontendVertexAccessType GetNodeOutputAccessType(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, const FGuid& InVertexID) const override;
		virtual const FMetasoundFrontendVersionNumber& GetVersionNumber() const override;
		virtual bool IsInputAccessTypeDynamic() const override;
		virtual bool IsInputConnectionUserModifiable() const override;
		virtual bool IsOutputConnectionUserModifiable() const override;
		virtual bool IsOutputAccessTypeDynamic() const override;
		virtual bool IsValidNodeInterface(const FMetasoundFrontendNodeInterface& InNodeInterface) const override;

#if WITH_EDITOR
		virtual bool HasRequiredConnections(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, FString* OutMessage = nullptr) const override;
#endif // WITH_EDITOR
	};
} // namespace Metasound::Frontend
