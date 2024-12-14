// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Map.h"
#include "Interfaces/MetasoundFrontendInterfaceRegistry.h"
#include "MetasoundFrontendDocumentCacheInterface.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentModifyDelegates.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundVertex.h"
#include "Templates/Function.h"
#include "UObject/ScriptInterface.h"

#include "MetasoundFrontendDocumentBuilder.generated.h"


// Forward Declarations
class FMetasoundAssetBase;


namespace Metasound::Frontend
{
	// Forward Declarations
	class INodeTemplate;

	using FConstClassAndNodeFunctionRef = TFunctionRef<void(const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)>;
	using FFinalizeNodeFunctionRef = TFunctionRef<void(FMetasoundFrontendNode&, const Metasound::Frontend::FNodeRegistryKey&)>;

	enum class EInvalidEdgeReason : uint8
	{
		None = 0,
		MismatchedAccessType,
		MismatchedDataType,
		MissingInput,
		MissingOutput,
		COUNT
	};

	METASOUNDFRONTEND_API FString LexToString(const EInvalidEdgeReason& InReason);

	struct METASOUNDFRONTEND_API FNamedEdge
	{
		const FGuid OutputNodeID;
		const FName OutputName;
		const FGuid InputNodeID;
		const FName InputName;

		friend bool operator==(const FNamedEdge& InLHS, const FNamedEdge& InRHS)
		{
			return InLHS.OutputNodeID == InRHS.OutputNodeID
				&& InLHS.OutputName == InRHS.OutputName
				&& InLHS.InputNodeID == InRHS.InputNodeID
				&& InLHS.InputName == InRHS.InputName;
		}

		friend bool operator!=(const FNamedEdge& InLHS, const FNamedEdge& InRHS)
		{
			return !(InLHS == InRHS);
		}

		friend FORCEINLINE uint32 GetTypeHash(const FNamedEdge& InBinding)
		{
			const int32 NameHash = HashCombineFast(GetTypeHash(InBinding.OutputName), GetTypeHash(InBinding.InputName));
			const int32 GuidHash = HashCombineFast(GetTypeHash(InBinding.OutputNodeID), GetTypeHash(InBinding.InputNodeID));
			return HashCombineFast(NameHash, GuidHash);
		}
	};

	struct METASOUNDFRONTEND_API FModifyInterfaceOptions
	{
		FModifyInterfaceOptions(const TArray<FMetasoundFrontendInterface>& InInterfacesToRemove, const TArray<FMetasoundFrontendInterface>& InInterfacesToAdd);
		FModifyInterfaceOptions(TArray<FMetasoundFrontendInterface>&& InInterfacesToRemove, TArray<FMetasoundFrontendInterface>&& InInterfacesToAdd);
		FModifyInterfaceOptions(const TArray<FMetasoundFrontendVersion>& InInterfaceVersionsToRemove, const TArray<FMetasoundFrontendVersion>& InInterfaceVersionsToAdd);

		TArray<FMetasoundFrontendInterface> InterfacesToRemove;
		TArray<FMetasoundFrontendInterface> InterfacesToAdd;

		// Function used to determine if an old of a removed interface
		// and new member of an added interface are considered equal and
		// to be swapped, retaining preexisting connections (and locations
		// if in editor and 'SetDefaultNodeLocations' option is set)
		TFunction<bool(FName, FName)> NamePairingFunction;

#if WITH_EDITORONLY_DATA
		bool bSetDefaultNodeLocations = true;
#endif // WITH_EDITORONLY_DATA
	};
} // namespace Metasound::Frontend


// Builder Document UObject, which is only used for registration purposes when attempting
// async registration whereby the original document is serialized and must not be mutated.
UCLASS()
class METASOUNDFRONTEND_API UMetaSoundBuilderDocument : public UObject, public IMetaSoundDocumentInterface
{
	GENERATED_BODY()

public:
	UE_DEPRECATED(5.5, "Use overload supplying MetaSound to copy (builder documents no longer supported for cases outside of cloned document registration.")
	static UMetaSoundBuilderDocument& Create(const UClass& InBuilderClass);

	// Create and return a valid builder document which copies the provided interface's document & class
	static UMetaSoundBuilderDocument& Create(const IMetaSoundDocumentInterface& InDocToCopy);

	virtual bool ConformObjectToDocument() override;

	// Returns the document
	virtual const FMetasoundFrontendDocument& GetConstDocument() const override;

	// Returns temp path of builder document
	virtual FTopLevelAssetPath GetAssetPathChecked() const override;

	// Returns the base class registered with the MetaSound UObject registry.
	virtual const UClass& GetBaseMetaSoundUClass() const final override;

	// Returns the builder class used to modify the given document.
	virtual const UClass& GetBuilderUClass() const final override;

	// Returns if the document is being actively built (always true as builder documents are always being actively built)
	virtual bool IsActivelyBuilding() const final override;

private:
	virtual FMetasoundFrontendDocument& GetDocument() override;

	virtual void OnBeginActiveBuilder() override;
	virtual void OnFinishActiveBuilder() override;

	UPROPERTY(Transient)
	FMetasoundFrontendDocument Document;

	UPROPERTY(Transient)
	TObjectPtr<const UClass> MetaSoundUClass = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<const UClass> BuilderUClass = nullptr;
};

// Builder used to support dynamically generating MetaSound documents at runtime. Builder contains caches that speed up
// common search and modification operations on a given document, which may result in slower performance on construction,
// but faster manipulation of its managed document.  The builder's managed copy of a document is expected to not be modified
// by any external system to avoid cache becoming stale.
USTRUCT()
struct METASOUNDFRONTEND_API FMetaSoundFrontendDocumentBuilder
{
	GENERATED_BODY()

public:
	// Default ctor should typically never be used directly as builder interface (and optionally delegates) should be specified on construction (Default exists only to make UObject reflection happy).
	FMetaSoundFrontendDocumentBuilder(TScriptInterface<IMetaSoundDocumentInterface> InDocumentInterface = { }, TSharedPtr<Metasound::Frontend::FDocumentModifyDelegates> InDocumentDelegates = { }, bool bPrimeCache = false);
	virtual ~FMetaSoundFrontendDocumentBuilder();

	// Call when the builder will no longer modify the IMetaSoundDocumentInterface
	void FinishBuilding();

	const FMetasoundFrontendClass* AddDependency(const FMetasoundFrontendClass& InClass);
	void AddEdge(FMetasoundFrontendEdge&& InNewEdge, const FGuid* InPageID = nullptr);
	bool AddNamedEdges(const TSet<Metasound::Frontend::FNamedEdge>& ConnectionsToMake, TArray<const FMetasoundFrontendEdge*>* OutEdgesCreated = nullptr, bool bReplaceExistingConnections = true, const FGuid* InPageID = nullptr);
	bool AddEdgesByNodeClassInterfaceBindings(const FGuid& InFromNodeID, const FGuid& InToNodeID, bool bReplaceExistingConnections = true, const FGuid* InPageID = nullptr);
	bool AddEdgesFromMatchingInterfaceNodeOutputsToGraphOutputs(const FGuid& InNodeID, TArray<const FMetasoundFrontendEdge*>& OutEdgesCreated, bool bReplaceExistingConnections = true, const FGuid* InPageID = nullptr);
	bool AddEdgesFromMatchingInterfaceNodeInputsToGraphInputs(const FGuid& InNodeID, TArray<const FMetasoundFrontendEdge*>& OutEdgesCreated, bool bReplaceExistingConnections = true, const FGuid* InPageID = nullptr);

	// Adds Graph Input to document, which in turn adds a referencing input node to ALL pages.  If valid PageID is provided, returns associated page's node pointer.
	// If none provided, returns node pointer to node for the builder's currently set build page ID (see 'GetBuildPageID').
	const FMetasoundFrontendNode* AddGraphInput(const FMetasoundFrontendClassInput& InClassInput, const FGuid* InPageID = nullptr);

	// Adds node to document to the page associated with the given PageID.  If no valid PageID is provided, adds and returns node pointer to node for the builder's
	// currently set build page ID (see 'GetBuildPageID').
	const FMetasoundFrontendNode* AddGraphNode(const FMetasoundFrontendGraphClass& InClass, FGuid InNodeID = FGuid::NewGuid(), const FGuid* InPageID = nullptr);

	// Adds Graph Output to document, which in turn adds a referencing output node to ALL pages.  If valid PageID is provided, returns associated page's node pointer.
	// If none provided, returns node pointer to node for the builder's currently set build page ID (see 'GetBuildPageID').
	const FMetasoundFrontendNode* AddGraphOutput(const FMetasoundFrontendClassOutput& InClassOutput, const FGuid* InPageID = nullptr);

	bool AddInterface(FName InterfaceName);

	const FMetasoundFrontendNode* AddNodeByClassName(const FMetasoundFrontendClassName& InClassName, int32 InMajorVersion = 1, FGuid InNodeID = FGuid::NewGuid(), const FGuid* InPageID = nullptr);
	
	const FMetasoundFrontendNode* AddNodeByTemplate(const Metasound::Frontend::INodeTemplate& InTemplate, FNodeTemplateGenerateInterfaceParams Params, FGuid InNodeID = FGuid::NewGuid(), const FGuid* InPageID = nullptr);

#if WITH_EDITORONLY_DATA
	// Adds a graph page to the given builder's document
	const FMetasoundFrontendGraph& AddGraphPage(const FGuid& InPageID, bool bDuplicateLastGraph, bool bSetAsBuildGraph = true);
#endif // WITH_EDITORONLY_DATA

	// Returns whether or not the given edge can be added, which requires that its input
	// is not already connected and the edge is valid (see function 'IsValidEdge').
	bool CanAddEdge(const FMetasoundFrontendEdge& InEdge, const FGuid* InPageID = nullptr) const;

	// Clears document completely of all graph page data (nodes, edges, & member metadata), dependencies,
	// interfaces, member metadata, preset state, etc. Leaves ClassMetadata intact. Reloads the builder state,
	// so external delegates must be relinked if desired.
	void ClearDocument(TSharedRef<Metasound::Frontend::FDocumentModifyDelegates> ModifyDelegates);

	UE_DEPRECATED(5.5, "Use ClearDocument instead")
	void ClearGraph() {  }

#if WITH_EDITORONLY_DATA
	bool ClearMemberMetadata(const FGuid& InMemberID);
#endif // WITH_EDITORONLY_DATA

	bool ContainsDependencyOfType(EMetasoundFrontendClassType ClassType) const;
	bool ContainsEdge(const FMetasoundFrontendEdge& InEdge, const FGuid* InPageID = nullptr) const;
	bool ContainsNode(const FGuid& InNodeID, const FGuid* InPageID = nullptr) const;

	bool ConvertFromPreset();
	bool ConvertToPreset(const FMetasoundFrontendDocument& InReferencedDocument, TSharedRef<Metasound::Frontend::FDocumentModifyDelegates> ModifyDelegates = { });

	const FMetasoundFrontendNode* DuplicateGraphInput(const FMetasoundFrontendClassInput& InClassInput, const FName InName, const FGuid* InPageID = nullptr);
	const FMetasoundFrontendNode* DuplicateGraphOutput(const FMetasoundFrontendClassOutput& InClassOutput, const FName InName, const FGuid* InPageID = nullptr);

#if WITH_EDITORONLY_DATA
	const FMetasoundFrontendEdgeStyle* FindConstEdgeStyle(const FGuid& InNodeID, FName OutputName, const FGuid* InPageID = nullptr) const;
	FMetasoundFrontendEdgeStyle* FindEdgeStyle(const FGuid& InNodeID, FName OutputName, const FGuid* InPageID = nullptr);
	FMetasoundFrontendEdgeStyle& FindOrAddEdgeStyle(const FGuid& InNodeID, FName OutputName, const FGuid* InPageID = nullptr);
	const FMetaSoundFrontendGraphComment* FindGraphComment(const FGuid& InCommentID, const FGuid* InPageID = nullptr) const;
	FMetaSoundFrontendGraphComment* FindGraphComment(const FGuid& InCommentID, const FGuid* InPageID = nullptr);
	FMetaSoundFrontendGraphComment& FindOrAddGraphComment(const FGuid& InCommentID, const FGuid* InPageID = nullptr);
	UMetaSoundFrontendMemberMetadata* FindMemberMetadata(const FGuid& InMemberID);
#endif // WITH_EDITORONLY_DATA

	static bool FindDeclaredInterfaces(const FMetasoundFrontendDocument& InDocument, TArray<const Metasound::Frontend::IInterfaceRegistryEntry*>& OutInterfaces);
	bool FindDeclaredInterfaces(TArray<const Metasound::Frontend::IInterfaceRegistryEntry*>& OutInterfaces) const;

	const FMetasoundFrontendClass* FindDependency(const FGuid& InClassID) const;
	const FMetasoundFrontendClass* FindDependency(const FMetasoundFrontendClassMetadata& InMetadata) const;
	TArray<const FMetasoundFrontendEdge*> FindEdges(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID = nullptr) const;

	const FMetasoundFrontendClassInput* FindGraphInput(FName InputName) const;
	const FMetasoundFrontendNode* FindGraphInputNode(FName InputName, const FGuid* InPageID = nullptr) const;
	const FMetasoundFrontendClassOutput* FindGraphOutput(FName OutputName) const;
	const FMetasoundFrontendNode* FindGraphOutputNode(FName OutputName, const FGuid* InPageID = nullptr) const;

	const FMetasoundFrontendVariable* FindGraphVariable(FName InVariableName, const FGuid* InPageID = nullptr) const;

	bool FindInterfaceInputNodes(FName InterfaceName, TArray<const FMetasoundFrontendNode*>& OutInputs, const FGuid* InPageID = nullptr) const;
	bool FindInterfaceOutputNodes(FName InterfaceName, TArray<const FMetasoundFrontendNode*>& OutOutputs, const FGuid* InPageID = nullptr) const;

	// Accessor for the currently set build graph.
	const FMetasoundFrontendGraph& FindConstBuildGraphChecked() const;

	const FMetasoundFrontendNode* FindNode(const FGuid& InNodeID, const FGuid* InPageID = nullptr) const;

	const FMetasoundFrontendVertex* FindNodeInput(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID = nullptr) const;
	const FMetasoundFrontendVertex* FindNodeInput(const FGuid& InNodeID, FName InVertexName, const FGuid* InPageID = nullptr) const;

	// Returns class defaults associated with the given node input (as defined in the associated node's dependency)
	const TArray<FMetasoundFrontendClassInputDefault>* FindNodeClassInputDefaults(const FGuid& InNodeID, FName InVertexName, const FGuid* InPageID = nullptr) const;

	// Returns node input's vertex default if valid and assigned.
	const FMetasoundFrontendVertexLiteral* FindNodeInputDefault(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID = nullptr) const;

	// Returns node input's vertex default if valid and assigned.
	const FMetasoundFrontendVertexLiteral* FindNodeInputDefault(const FGuid& InNodeID, FName InVertexName, const FGuid* InPageID = nullptr) const;

	TArray<const FMetasoundFrontendVertex*> FindNodeInputs(const FGuid& InNodeID, FName TypeName = FName(), const FGuid* InPageID = nullptr) const;
	TArray<const FMetasoundFrontendVertex*> FindNodeInputsConnectedToNodeOutput(const FGuid& InOutputNodeID, const FGuid& InOutputVertexID, TArray<const FMetasoundFrontendNode*>* ConnectedInputNodes = nullptr, const FGuid* InPageID = nullptr) const;

	const FMetasoundFrontendVertex* FindNodeOutput(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID = nullptr) const;
	const FMetasoundFrontendVertex* FindNodeOutput(const FGuid& InNodeID, FName InVertexName, const FGuid* InPageID = nullptr) const;
	TArray<const FMetasoundFrontendVertex*> FindNodeOutputs(const FGuid& InNodeID, FName TypeName = FName(), const FGuid* InPageID = nullptr) const;
	const FMetasoundFrontendVertex* FindNodeOutputConnectedToNodeInput(const FGuid& InInputNodeID, const FGuid& InInputVertexID, const FMetasoundFrontendNode** ConnectedOutputNode = nullptr, const FGuid* InPageID = nullptr) const;

	const FMetasoundFrontendDocument& GetConstDocumentChecked() const;
	const IMetaSoundDocumentInterface& GetConstDocumentInterfaceChecked() const;
	const FString GetDebugName() const;

	UE_DEPRECATED(5.5, "Use GetConstDocumentChecked() instead")
	const FMetasoundFrontendDocument& GetDocument() const;

	// The graph ID used when requests are made to mutate specific paged graph topology (ex. adding or removing nodes or edges)
	const FGuid& GetBuildPageID() const;

#if WITH_EDITOR
	// Gets the editor-only style of a node with the given ID.
	const FMetasoundFrontendNodeStyle* GetNodeStyle(const FGuid& InNodeID, const FGuid* InPageID = nullptr);
#endif // WITH_EDITOR

	template<typename TObjectType>
	TObjectType& CastDocumentObjectChecked() const
	{
		UObject* Owner = DocumentInterface.GetObject();
		return *CastChecked<TObjectType>(Owner);
	}

	// Generates and returns new class name for the given builder's document. Should be used with extreme caution
	// (i.e. on new assets, when migrating assets, or upon generation of transient MetaSounds), as using a persistent
	// builder registered with the DocumentBuilderRegistry may result in stale asset records keyed off of an undefined class
	// name.  In addition, this can potentially leave existing node references in an abandoned state to this class causing
	// asset validation errors.
	FMetasoundFrontendClassName GenerateNewClassName();

	Metasound::Frontend::FDocumentModifyDelegates& GetDocumentDelegates();

	UE_DEPRECATED(5.5, "Use GetConstDocumentInterfaceChecked instead")
	const IMetaSoundDocumentInterface& GetDocumentInterface() const;
	FMetasoundAssetBase& GetMetasoundAsset() const;
	
	// Get the asset referenced by this builder's preset asset, nullptr if builder is not a preset.
	FMetasoundAssetBase* GetReferencedPresetAsset() const;

	int32 GetTransactionCount() const;

	TArray<const FMetasoundFrontendNode*> GetGraphInputTemplateNodes(FName InInputName, const FGuid* InPageID = nullptr);

	EMetasoundFrontendVertexAccessType GetNodeInputAccessType(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID = nullptr) const;

	UE_DEPRECATED(5.5, "Use FindNodeInputClass overloads instead and use GetDefaults() on result (now supports page values)")
	const FMetasoundFrontendLiteral* GetNodeInputClassDefault(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID = nullptr) const;

	UE_DEPRECATED(5.5, "Use FindNodeInputDefault and returned struct Value member instead")
	const FMetasoundFrontendLiteral* GetNodeInputDefault(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID = nullptr) const;

	EMetasoundFrontendVertexAccessType GetNodeOutputAccessType(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID = nullptr) const;

#if WITH_EDITORONLY_DATA
	const bool GetIsAdvancedDisplay(const FName MemberName, const EMetasoundFrontendClassType Type) const;
#endif // WITH_EDITORONLY_DATA

	// Initializes the builder's document, using the (optional) provided document template, (optional) class name, and (optionally) whether or not to reset the existing class version.
	void InitDocument(const FMetasoundFrontendDocument* InDocumentTemplate = nullptr, const FMetasoundFrontendClassName* InNewClassName = nullptr, bool bResetVersion = true);

	// Initializes GraphClass Metadata, optionally resetting the version back to 1.0 and/or creating a unique class name if a name is not provided.
	static void InitGraphClassMetadata(FMetasoundFrontendClassMetadata& InOutMetadata, bool bResetVersion = false, const FMetasoundFrontendClassName* NewClassName = nullptr);
	void InitGraphClassMetadata(bool bResetVersion, const FMetasoundFrontendClassName* NewClassName);

	void InitNodeLocations();

	UE_DEPRECATED(5.5, "Use invalidate overload that is provided new version of modify delegates")
	void InvalidateCache() { }

	bool IsDependencyReferenced(const FGuid& InClassID) const;
	bool IsNodeInputConnected(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID = nullptr) const;
	bool IsNodeOutputConnected(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID = nullptr) const;

	bool IsInterfaceDeclared(FName InInterfaceName) const;
	bool IsInterfaceDeclared(const FMetasoundFrontendVersion& InInterfaceVersion) const;
	bool IsPreset() const;

	// Returns whether or not builder is attached to a DocumentInterface and is valid to build or act on a document.
	bool IsValid() const;

	// Returns whether or not the given edge is valid (i.e. represents an input and output that equate in data and access types) or malformed.
	// Note that this does not return whether or not the given edge exists, but rather if it could be legally applied to the given edge vertices.
	Metasound::Frontend::EInvalidEdgeReason IsValidEdge(const FMetasoundFrontendEdge& InEdge, const FGuid* InPageID = nullptr) const;

	// Iterates nodes that are filtered by only subscribing to a class with the given type (asserts if provided invalid class type).
	void IterateNodesByClassType(Metasound::Frontend::FConstClassAndNodeFunctionRef Func, EMetasoundFrontendClassType ClassType, const FGuid* InPageID = nullptr) const;

	bool ModifyInterfaces(Metasound::Frontend::FModifyInterfaceOptions&& InOptions);

	UE_DEPRECATED(5.5,
		"Cache invalidation may require new copy of delegates. In addition, re-priming is discouraged. "
		"To enforce this, new recommended pattern is to construct a new builder instead")
	void ReloadCache();

	bool RemoveDependency(const FGuid& InClassID);
	bool RemoveDependency(EMetasoundFrontendClassType ClassType, const FMetasoundFrontendClassName& InClassName, const FMetasoundFrontendVersionNumber& InClassVersionNumber);
	bool RemoveEdge(const FMetasoundFrontendEdge& EdgeToRemove, const FGuid* InPageID = nullptr);

	// Removes all edges connected to an input or output vertex associated with the node of the given ID.
	bool RemoveEdges(const FGuid& InNodeID, const FGuid* InPageID = nullptr);

	bool RemoveEdgesByNodeClassInterfaceBindings(const FGuid& InOutputNodeID, const FGuid& InInputNodeID, const FGuid* InPageID = nullptr);
	bool RemoveEdgesFromNodeOutput(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID = nullptr);
	bool RemoveEdgeToNodeInput(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID = nullptr);

#if WITH_EDITORONLY_DATA
	bool RemoveEdgeStyle(const FGuid& InNodeID, FName OutputName, const FGuid* InPageID = nullptr);
	bool RemoveGraphComment(const FGuid& InCommentID, const FGuid* InPageID = nullptr);
#endif // WITH_EDITORONLY_DATA

	bool RemoveGraphInput(FName InInputName);
	bool RemoveGraphOutput(FName InOutputName);

#if WITH_EDITORONLY_DATA
	bool RemoveGraphPage(const FGuid& InPageID);
#endif // WITH_EDITORONLY_DATA

	bool RemoveInterface(FName InName);
	bool RemoveNamedEdges(const TSet<Metasound::Frontend::FNamedEdge>& InNamedEdgesToRemove, TArray<FMetasoundFrontendEdge>* OutRemovedEdges = nullptr, const FGuid* InPageID = nullptr);
	bool RemoveNode(const FGuid& InNodeID, const FGuid* InPageID = nullptr);

#if WITH_EDITORONLY_DATA
	int32 RemoveNodeLocation(const FGuid& InNodeID, const FGuid* InLocationGuid = nullptr, const FGuid* InPageID = nullptr);
#endif // WITH_EDITOR

	void Reload(TSharedPtr<Metasound::Frontend::FDocumentModifyDelegates> Delegates = {}, bool bPrimeCache = false);

#if WITH_EDITORONLY_DATA
	bool RemoveGraphInputDefault(FName InputName, const FGuid& InPageID, bool bClearInheritsDefault = true);
#endif // WITH_EDITORONLY_DATA

	bool RemoveNodeInputDefault(const FGuid& InNodeID, const FGuid& InVertexID, const FGuid* InPageID = nullptr);
	bool RemoveUnusedDependencies();

	UE_DEPRECATED(5.5, "Use GenerateNewClassName instead")
	bool RenameRootGraphClass(const FMetasoundFrontendClassName& InName);

#if WITH_EDITORONLY_DATA
	bool ResetGraphInputDefault(FName InputName);

	// Removes all graph pages except the default.  If bClearDefaultPage is true, clears the default graph page implementation.
	void ResetGraphPages(bool bClearDefaultGraph);
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	void SetAuthor(const FString& InAuthor);

#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	// Sets the builder's targeted paged graph ID to the given ID if it exists.
	// Returns true if the builder is already targeting the given ID or if it successfully
	// found a page implementation with the given ID and was able to switch to it, false if not.
	// Swapping the targeted build graph ID clears the local cache, so swapping frequently can
	// induce cash thrashing. BroadcastDelegate should always be true unless dealing with the controller
	// API (exposed as a mechanism for mutating via controllers while deprecating.  Option will be removed
	// in a future build).
	bool SetBuildPageID(const FGuid& InBuildPageID, bool bBroadcastDelegate = true);

	// Sets the given input`s IsAdvancedDisplay state. AdvancedDisplay pins are hidden in the node by default.
	// returns true if state was changed.
	bool SetGraphInputAdvancedDisplay(const FName InputName, const bool InAdvancedDisplay);
#endif // WITH_EDITORONLY_DATA

	// Sets the given graph input's access type. If connected to other nodes and access type is not compatible,
	// associated edges/connections are removed.  Returns true if either DataType was successfully set to new
	// value or if AccessType is already the given AccessType.
	bool SetGraphInputAccessType(FName InputName, EMetasoundFrontendVertexAccessType AccessType);

	// Sets the given graph input's data type. If connected to other nodes, associated edges/connections
	// are removed.  Returns true if either DataType was successfully set to new value or if DataType is
	// already the given DataType.
	bool SetGraphInputDataType(FName InputName, FName DataType);

	bool SetGraphInputDefault(FName InputName, FMetasoundFrontendLiteral InDefaultLiteral, const FGuid* InPageID = nullptr);
	bool SetGraphInputDefaults(FName InputName, TArray<FMetasoundFrontendClassInputDefault> Defaults);

	// Sets a given graph input's name to a new name. Succeeds if the graph output exists and the new name is set (or is the same as the old name).
	bool SetGraphInputName(FName InputName, FName InName);

#if WITH_EDITORONLY_DATA
	// Sets the given output`s IsAdvancedDisplay state. AdvancedDisplay pins are hidden in the node by default.
	// returns true if state was changed.
	bool SetGraphOutputAdvancedDisplay(const FName OutputName, const bool InAdvancedDisplay);
#endif // WITH_EDITORONLY_DATA

	// Sets the given graph output's access type. If connected to other nodes and access type is not compatible,
	// associated edges/connections are removed.  Returns true if either DataType was successfully set to new
	// value or if AccessType is already the given AccessType.
	bool SetGraphOutputAccessType(FName OutputName, EMetasoundFrontendVertexAccessType AccessType);

	// Sets the given graph output's data type. If connected to other nodes, associated edges/connections
	// are removed.  Returns true if either DataType was successfully set to new value or if DataType is
	// already the given DataType.
	bool SetGraphOutputDataType(FName OutputName, FName DataType);

	// Sets a given graph output's name to a new name. Succeeds if the graph output exists and the new name is set (or is the same as the old name).
	bool SetGraphOutputName(FName InputName, FName InName);

	// Sets the given graph variable's default.
	bool SetGraphVariableDefault(FName VariableName, FMetasoundFrontendLiteral InDefaultLiteral, const FGuid* InPageID = nullptr);

#if WITH_EDITOR
	void SetDisplayName(const FText& InDisplayName);

	void SetMemberMetadata(UMetaSoundFrontendMemberMetadata& NewMetadata);

	// Sets the editor-only comment to the provided value.
	// Returns true if the node was found and the comment was updated, false if not.
	bool SetNodeComment(const FGuid& InNodeID, FString&& InNewComment, const FGuid* InPageID = nullptr);

	// Sets the editor-only comment visibility.
	// Returns true if the node was found and the visibility was set, false if not.
	bool SetNodeCommentVisible(const FGuid& InNodeID, bool bIsVisible, const FGuid* InPageID = nullptr);

	// Sets the editor-only node location of a node with the given ID to the provided location.
	// Returns true if the node was found and the location was updated, false if not.
	bool SetNodeLocation(const FGuid& InNodeID, const FVector2D& InLocation, const FGuid* InLocationGuid = nullptr, const FGuid* InPageID = nullptr);

	// Sets the editor-only Unconnected Pins Hidden for a node with the given ID.
	bool SetNodeUnconnectedPinsHidden(const FGuid& InNodeID, const bool bUnconnectedPinsHidden, const FGuid* InPageID = nullptr);
#endif // WITH_EDITOR

	bool SetNodeInputDefault(const FGuid& InNodeID, const FGuid& InVertexID, const FMetasoundFrontendLiteral& InLiteral, const FGuid* InPageID = nullptr);

	// Sets the document's version number.  Should only be called by document versioning.
	void SetVersionNumber(const FMetasoundFrontendVersionNumber& InDocumentVersionNumber);

	bool SwapGraphInput(const FMetasoundFrontendClassVertex& InExistingInputVertex, const FMetasoundFrontendClassVertex& NewInputVertex);
	bool SwapGraphOutput(const FMetasoundFrontendClassVertex& InExistingOutputVertex, const FMetasoundFrontendClassVertex& NewOutputVertex);

#if WITH_EDITOR
	UE_DEPRECATED(5.5, "Use 'UpdateDependencyRegistryData' instead and supply keys (comprised of name, version & node class type)")
	bool UpdateDependencyClassNames(const TMap<FMetasoundFrontendClassName, FMetasoundFrontendClassName>& OldToNewReferencedClassNames);

	bool UpdateDependencyRegistryData(const TMap<Metasound::Frontend::FNodeRegistryKey, Metasound::Frontend::FNodeRegistryKey>& OldToNewClassKeys);
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	// Transforms template nodes within the given builder's document, which can include swapping associated edges and/or
	// replacing nodes with other, registry-defined concrete node class instances. Returns true if any template nodes were processed.
	bool TransformTemplateNodes();

	// Versions legacy document members that contained interface information
	UE_DEPRECATED(5.5, "Moved to internally implemented versioning logic")
	bool VersionInterfaces();

	// Struct enabling property migration of data that must be applied prior to versioning logic
	struct IPropertyVersionTransform
	{
	public:
		virtual ~IPropertyVersionTransform() = default;

	protected:
		virtual bool Transform(FMetaSoundFrontendDocumentBuilder& Builder) const = 0;

		// Allows for unsafe access to a document for property migration.
		static FMetasoundFrontendDocument& GetDocumentUnsafe(const FMetaSoundFrontendDocumentBuilder& Builder);
	};
#endif // WITH_EDITORONLY_DATA

private:
	using FFinalizeNodeFunctionRef = TFunctionRef<void(FMetasoundFrontendNode&, const Metasound::Frontend::FNodeRegistryKey&)>;

	FMetasoundFrontendNode* AddNodeInternal(const FMetasoundFrontendClassMetadata& InClassMetadata, Metasound::Frontend::FFinalizeNodeFunctionRef FinalizeNode, const FGuid& InPageID, FGuid InNodeID = FGuid::NewGuid(), int32* NewNodeIndex = nullptr);
	void BeginBuilding(TSharedPtr<Metasound::Frontend::FDocumentModifyDelegates> Delegates = {}, bool bPrimeCache = false);

	// Conforms GraphOutput node's ClassID, Access & Data Type with the GraphOutput.
	// creating and removing dependencies as necessary within the document dependency array. Does *NOT*
	// modify edge data (i.e. if the DataType is changed on the given node and it has corresponding
	// edges, edges may then be invalid due to access type/DataType incompatibility).
	bool ConformGraphInputNodeToClass(const FMetasoundFrontendClassInput& GraphInput);

	// Conforms GraphOutput node's ClassID, Access & Data Type with the GraphOutput.
	// Creates and removes dependencies as necessary within the document dependency array. Does *NOT*
	// modify edge data (i.e. if the DataType is changed on the given node and it has corresponding
	// edges, edges may then be invalid due to access type/DataType incompatibility).
	bool ConformGraphOutputNodeToClass(const FMetasoundFrontendClassOutput& GraphOutput);

	FMetasoundFrontendGraph& FindBuildGraphChecked() const;

	bool FindNodeClassInterfaces(const FGuid& InNodeID, TSet<FMetasoundFrontendVersion>& OutInterfaces, const FGuid& InPageID) const;
	FMetasoundFrontendNode* FindNodeInternal(const FGuid& InNodeID, const FGuid* InPageID = nullptr);

	void IterateNodesConnectedWithVertex(const FMetasoundFrontendVertexHandle& Vertex, TFunctionRef<void(const FMetasoundFrontendEdge&, FMetasoundFrontendNode&)> NodeIndexIterFunc, const FGuid& InPageID);

	const FTopLevelAssetPath GetBuilderClassPath() const;
	FMetasoundFrontendDocument& GetDocumentChecked() const;
	IMetaSoundDocumentInterface& GetDocumentInterfaceChecked() const;

	void RemoveSwapDependencyInternal(int32 Index);

	bool SetGraphInputInheritsDefault(FName InName, bool bInputInheritsDefault);

	bool SpliceVariableNodeFromStack(const FGuid& InNodeID, const FGuid& InPageID);
	bool UnlinkVariableNode(const FGuid& InNodeID, const FGuid& InPageID);

	UPROPERTY(Transient)
	TScriptInterface<IMetaSoundDocumentInterface> DocumentInterface;

	// Page ID to apply build transaction to if no optional PageID is provided in explicit function call.
	// (Also used to support back compat for Controller API until mutable controllers are adequately deprecated).
	UPROPERTY(Transient)
	FGuid BuildPageID;

	TSharedPtr<Metasound::Frontend::IDocumentCache> DocumentCache;
	TSharedPtr<Metasound::Frontend::FDocumentModifyDelegates> DocumentDelegates;
};
