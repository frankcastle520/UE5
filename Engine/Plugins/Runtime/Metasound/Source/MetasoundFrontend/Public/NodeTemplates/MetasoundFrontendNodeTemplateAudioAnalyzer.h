// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetasoundFrontendNodeTemplateRegistry.h"

namespace Metasound::Frontend
{
	class METASOUNDFRONTEND_API FAudioAnalyzerNodeTemplate : public FNodeTemplateBase
	{
	public:
		static const FMetasoundFrontendClassName ClassName;
		static const FMetasoundFrontendVersionNumber VersionNumber;

		virtual FMetasoundFrontendNodeInterface GenerateNodeInterface(FNodeTemplateGenerateInterfaceParams InParams) const override;
		virtual const FMetasoundFrontendClassName& GetClassName() const override;
		virtual TUniquePtr<INodeTemplateTransform> GenerateNodeTransform() const override;
		virtual const FMetasoundFrontendClass& GetFrontendClass() const override;
		virtual EMetasoundFrontendVertexAccessType GetNodeInputAccessType(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, const FGuid& InVertexID) const override;
		virtual EMetasoundFrontendVertexAccessType GetNodeOutputAccessType(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, const FGuid& InVertexID) const override;
		virtual const FMetasoundFrontendVersionNumber& GetVersionNumber() const override;
		virtual bool IsInputAccessTypeDynamic() const override;
		virtual bool IsInputConnectionUserModifiable() const override;
		virtual bool IsOutputAccessTypeDynamic() const override;
		virtual bool IsOutputConnectionUserModifiable() const override;
		virtual bool IsValidNodeInterface(const FMetasoundFrontendNodeInterface& InNodeInterface) const override;
	};
} // namespace Metasound::Frontend
