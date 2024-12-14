// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistryKey.h"
#include "MetasoundVertex.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "UObject/NoExportTypes.h"
#include "UObject/ScriptInterface.h"

#include "MetasoundFrontendNodeTemplateRegistry.generated.h"

// Forward Declarations
class IMetaSoundDocumentInterface;
struct FMetaSoundFrontendDocumentBuilder;

USTRUCT()
struct METASOUNDFRONTEND_API FNodeTemplateGenerateInterfaceParams
{
	GENERATED_BODY()

	// TODO: Currently just DataType FName. Subsequent change will replace this with a VertexHandle
	// and will add builder reference to once builder supports template nodes and controllers
	// are no longer used to add template nodes from editor code.
	UPROPERTY()
	TArray<FName> InputsToConnect;
	
	UPROPERTY()
	TArray<FName> OutputsToConnect;
};

namespace Metasound::Frontend
{
	class INodeTransform;

#if WITH_EDITOR
	class INodeController;
	class IInputController;
	class IOutputController;

	using FConstNodeHandle = TSharedRef<const INodeController>;
#endif // WITH_EDITOR

	class METASOUNDFRONTEND_API INodeTemplateTransform
	{
	public:
		virtual ~INodeTemplateTransform() = default;

		/** Return true if the builder was modified, false otherwise. */
		virtual bool Transform(const FGuid& InPageID, const FGuid& InNodeID, FMetaSoundFrontendDocumentBuilder& OutBuilder) const = 0;
	};

	/**
		* Base interface for a node template, which acts in place of frontend node class and respective instance(s).
		* Instances are preprocessed, allowing for custom graph manipulation prior to generating a respective runtime
		* graph operator representation.
		*/
	class METASOUNDFRONTEND_API INodeTemplate
	{
	public:
		virtual FMetasoundFrontendNodeInterface GenerateNodeInterface(FNodeTemplateGenerateInterfaceParams InParams) const = 0;

		virtual ~INodeTemplate() = default;

		virtual const TArray<FMetasoundFrontendClassInputDefault>* FindNodeClassInputDefaults(
			const FMetaSoundFrontendDocumentBuilder& InBuilder,
			const FGuid& InPageID,
			const FGuid& InNodeID,
			FName VertexName) const = 0;

		// Returns note template class name.
		virtual const FMetasoundFrontendClassName& GetClassName() const = 0;

		UE_DEPRECATED(5.4, "Use version that does not provide a preprocessed document")
		virtual TUniquePtr<INodeTransform> GenerateNodeTransform(FMetasoundFrontendDocument& InDocument) const;

#if WITH_EDITOR
		virtual FText GetNodeDisplayName(const IMetaSoundDocumentInterface& Interface, const FGuid& InPageID, const FGuid& InNodeID) const = 0;

		UE_DEPRECATED(5.5, "Use overload GetOutputVertexDisplayName with supplied builder & pageID instead")
		virtual FText GetInputPinDisplayName(const Frontend::IInputController& InInput) const { return FText(); }

		virtual FText GetInputVertexDisplayName(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, FName InputName) const = 0;

		UE_DEPRECATED(5.5, "Use overload GetOutputVertexDisplayName with supplied builder & pageID instead")
		virtual FText GetOutputPinDisplayName(const Frontend::IOutputController& InOutput) const { return FText(); };

		virtual FText GetOutputVertexDisplayName(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, FName OutputName) const = 0;

		static FText ResolveMemberDisplayName(FName VertexName, FText DisplayName, bool bIncludeNamespace);
#endif // WITH_EDITOR

		// Generates node transform that is used to preprocess nodes.
		virtual TUniquePtr<INodeTemplateTransform> GenerateNodeTransform() const = 0;

		// Returns the class definition for the given node class template.
		virtual const FMetasoundFrontendClass& GetFrontendClass() const = 0;

		// Returns access type of the given input within the provided builder's document
		virtual EMetasoundFrontendVertexAccessType GetNodeInputAccessType(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, const FGuid& InVertexID) const = 0;

		// Returns access type of the given output within the provided builder's document
		virtual EMetasoundFrontendVertexAccessType GetNodeOutputAccessType(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, const FGuid& InVertexID) const = 0;

		UE_DEPRECATED(5.4, "Use version number or classname instead")
		virtual const FMetasoundFrontendVersion& GetVersion() const { const static FMetasoundFrontendVersion NullVersion; return NullVersion; }

		// Returns note template class version.
		virtual const FMetasoundFrontendVersionNumber& GetVersionNumber() const = 0;

#if WITH_EDITOR
		UE_DEPRECATED(5.5, "Use version that takes a Frontend PageID, NodeID and builder instead")
		virtual bool HasRequiredConnections(FConstNodeHandle InNodeHandle, FString* OutMessage = nullptr) const { return false; }

		// Returns whether or not the given node template has the necessary
		// required connections to be preprocessed (editor only).
		virtual bool HasRequiredConnections(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, FString* OutMessage = nullptr) const = 0;
#endif // WITH_EDITOR

		// Returns whether template can dynamically assign a node's input access type (as opposed to it being assigned on the class input definition)
		virtual bool IsInputAccessTypeDynamic() const = 0;

		// Whether or not input connections are user modifiable
		virtual bool IsInputConnectionUserModifiable() const = 0;

		// Returns whether template can dynamically assign a node's output's access type (as opposed to it being assigned on the class output definition)
		virtual bool IsOutputAccessTypeDynamic() const = 0;

		// Whether or not output connections are user modifiable
		virtual bool IsOutputConnectionUserModifiable() const = 0;

		// Given the provided node interface, returns whether or not it conforms to an expected format
		// that can be successfully manipulated by a generated node template transform.
		virtual bool IsValidNodeInterface(const FMetasoundFrontendNodeInterface& InNodeInterface) const = 0;
	};

	class METASOUNDFRONTEND_API INodeTemplateRegistry
	{
	public:
		// Returns singleton template registry.
		static INodeTemplateRegistry& Get();

		virtual ~INodeTemplateRegistry() = default;

		// Find a template with the given key. Returns null if entry not found with given key.
		virtual const INodeTemplate* FindTemplate(const FNodeRegistryKey& InKey) const = 0;

		// Find a template with the given class name with the highest version. Returns null if entry not found with given name.
		virtual const INodeTemplate* FindTemplate(const FMetasoundFrontendClassName& InClassName) const = 0;
	};

	class METASOUNDFRONTEND_API FNodeTemplateBase : public INodeTemplate
	{
	public:
		virtual const TArray<FMetasoundFrontendClassInputDefault>* FindNodeClassInputDefaults(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, FName VertexName) const override;

#if WITH_EDITOR
		virtual FText GetNodeDisplayName(const IMetaSoundDocumentInterface& Interface, const FGuid& InPageID, const FGuid& InNodeID) const override;
		virtual FText GetInputVertexDisplayName(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, FName InputName) const override;
		virtual FText GetOutputVertexDisplayName(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, FName OutputName) const override;
		virtual bool HasRequiredConnections(const FMetaSoundFrontendDocumentBuilder& InBuilder, const FGuid& InPageID, const FGuid& InNodeID, FString* OutMessage = nullptr) const override;
#endif // WITH_EDITOR
	};

	// Register & Unregister are not publicly accessible implementation as the API
	// is in beta and, currently, only to be used by internal implementation (ex. reroute nodes).
	void RegisterNodeTemplate(TUniquePtr<INodeTemplate>&& InTemplate);

	UE_DEPRECATED(5.4, "Use version that provides class name and version instead")
	void UnregisterNodeTemplate(const FMetasoundFrontendVersion& InNodeTemplateVersion);

	void UnregisterNodeTemplate(const FMetasoundFrontendClassName& InClassName, const FMetasoundFrontendVersionNumber& InTemplateVersion);
} // namespace Metasound::Frontend
