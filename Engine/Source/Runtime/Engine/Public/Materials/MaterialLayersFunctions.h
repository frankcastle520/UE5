// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MaterialTypes.h"
#include "MaterialExpression.h"

#include "MaterialLayersFunctions.generated.h"

#define LOCTEXT_NAMESPACE "MaterialLayersFunctions"

class FArchive;
class FShaderKeyGenerator;
struct FMaterialLayersFunctions;

UENUM()
enum class EMaterialLayerLinkState : uint8
{
	Uninitialized = 0u, // Saved with previous engine version
	LinkedToParent, // Layer should mirror changes from parent material
	UnlinkedFromParent, // Layer is based on parent material, but should not mirror changes
	NotFromParent, // Layer was created locally in this material, not in parent
};

/** Serializable ID structure for FMaterialLayersFunctions which allows us to deterministically recompile shaders*/
struct FMaterialLayersFunctionsID
{
	TArray<FGuid> LayerIDs;
	TArray<FGuid> BlendIDs;
	TArray<bool> LayerStates;

	#if WITH_EDITOR
	bool operator==(const FMaterialLayersFunctionsID& Reference) const;
	inline bool operator!=(const FMaterialLayersFunctionsID& Reference) const { return !operator==(Reference); }

	void SerializeForDDC(FArchive& Ar);

	friend FMaterialLayersFunctionsID& operator<<(FArchive& Ar, FMaterialLayersFunctionsID& Ref)
	{
		Ref.SerializeForDDC(Ar);
		return Ref;
	}

	void UpdateHash(FSHA1& HashState) const;

	//TODO: Investigate whether this is really required given it is only used by FMaterialShaderMapId AND that one also uses UpdateHash
	void AppendKeyString(FString& KeyString) const;
	void Append(FShaderKeyGenerator& KeyGen) const;
	#endif
};

USTRUCT()
struct FMaterialLayersFunctionsEditorOnlyData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<bool> LayerStates;

	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<FText> LayerNames;

	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<bool> RestrictToLayerRelatives;

	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<bool> RestrictToBlendRelatives;

	/** Guid that identifies each layer in this stack */
	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<FGuid> LayerGuids;

	/**
	 * State of each layer's link to parent material
	 */
	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<EMaterialLayerLinkState> LayerLinkStates;

	/**
	 * List of Guids that exist in the parent material that have been explicitly deleted
	 * This is needed to distinguish these layers from newly added layers in the parent material
	 */
	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<FGuid> DeletedParentLayerGuids;

#if WITH_EDITORONLY_DATA
	FORCEINLINE bool operator==(const FMaterialLayersFunctionsEditorOnlyData& Other) const
	{
		if (LayerStates != Other.LayerStates ||
			LayerLinkStates != Other.LayerLinkStates ||
			DeletedParentLayerGuids != Other.DeletedParentLayerGuids)
		{
			return false;
		}
		return true;
	}

	FORCEINLINE bool operator!=(const FMaterialLayersFunctionsEditorOnlyData& Other) const
	{
		return !operator==(Other);
	}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	void Empty()
	{
		LayerStates.Empty();
		LayerNames.Empty();
		RestrictToLayerRelatives.Empty();
		RestrictToBlendRelatives.Empty();
		LayerGuids.Empty();
		LayerLinkStates.Empty();
	}

	void LinkAllLayersToParent();
#endif // WITH_EDITOR
};



USTRUCT()
struct FMaterialLayersFunctionsTree
{
	GENERATED_BODY()

public:
	using FNodeId = int32;
	using FPayloadId = int32;

	static const FNodeId InvalidId = -1; // Invalid id is always <0

	struct FNode
	{
		FNodeId 		Parent = InvalidId;
		FNodeId 		NextSibling = InvalidId;
		FNodeId 		ChildrenHead = InvalidId;
		FNodeId			Spare = InvalidId;

		FORCEINLINE bool operator==(const FNode& Other) const
		{
			if (Parent != Other.Parent || NextSibling != Other.NextSibling || ChildrenHead != Other.ChildrenHead)
			{
				return false;
			}
			return true;
		}

		FORCEINLINE bool operator!=(const FNode& Other) const
		{
			return !operator==(Other);
		}

		void Clear()
		{
			Parent = NextSibling = Spare = InvalidId;
		}
	};

	struct FPayload
	{
		FPayloadId Layer = InvalidId;
		FPayloadId Blend = InvalidId;

		FORCEINLINE bool operator==(const FPayload& Other) const
		{
			if (Layer != Other.Layer || Blend != Other.Blend)
			{
				return false;
			}
			return true;
		}

		FORCEINLINE bool operator!=(const FPayload& Other) const
		{
			return !operator==(Other);
		}
	};

	TArray<FNode>		Nodes;
	TArray<FPayload>	Payloads;
	FNodeId				Root = -1; // the index of the head of the top level siblings
							   	
	FMaterialLayersFunctionsTree() = default;
	FMaterialLayersFunctionsTree(const FMaterialLayersFunctionsTree& Rhs)
		:	Nodes(Rhs.Nodes),
			Payloads(Rhs.Payloads),
			Root(Rhs.Root)
			 
	{}
	FMaterialLayersFunctionsTree(const FMaterialLayersFunctions& Rhs) = delete;

	FMaterialLayersFunctionsTree& operator=(const FMaterialLayersFunctionsTree& Rhs)
	{
		Nodes = Rhs.Nodes;
		Payloads = Rhs.Payloads;
		Root = Rhs.Root;

		return *this;
	}

	FORCEINLINE bool operator==(const FMaterialLayersFunctionsTree& Other) const
	{
		if (Nodes != Other.Nodes || Payloads != Other.Payloads || Root != Other.Root)
		{
			return false;
		}
		return true;
	}

	FORCEINLINE bool operator!=(const FMaterialLayersFunctionsTree& Other) const
	{
		return !operator==(Other);
	}

	void Empty()
	{
		Nodes.Empty();
		Payloads.Empty();
		Root = -1;
	}

	FORCEINLINE bool IsEmpty() const
	{
		return Nodes.IsEmpty();
	}

	bool Serialize( FArchive& Ar );

	friend FArchive& operator<<(FArchive& Ar, FNode& T)
	{
		return Ar << T.Parent << T.NextSibling << T.ChildrenHead << T.Spare;
	}
	friend FArchive& operator<<(FArchive& Ar, FPayload& T)
	{
		return Ar << T.Layer << T.Blend;
	}

	/// Authoring the Tree

	FORCEINLINE bool IsValidId(FNodeId Id) const
	{
		return (Id >= 0 && Id < Nodes.Num());
	}

	// Find and return the node at the specified Id or null if invalid 
	FNode* GetNode(FNodeId InNodeId) const;

	// Find and return the node payload at the specified Id or null if invalid 
	FPayload* GetPayload(FNodeId InNodeId) const;

	// Find and return the parent node at the specified Id or null if invalid 
	FNode* GetParent(FNodeId InNodeId) const;

	// Find the node which is the head of the list of siblings where InNodeID belongs
	FNodeId GetSiblingHeadId(FNodeId InNodeId) const;

	// Gather the list of children for this node
	TArray<FNodeId> GetChildrenIds(FNodeId InNodeId) const;

	// Return the depth of the node in the tree, the root is depth 0
	// the first valid node in the tree under the root is depth 1
	int32 GetDepth(FNodeId InNodeId) const;

	// Add a new node with the specified Payload under the InPArent at the specified child index
	// the new node id is returned
	FNodeId AddNode(const FPayload& InPayload, FNodeId InParent, int32 InAtChildIndex = -1);

	// Remove this node
	// detach the node from the tree hierarchy as well as all the children recursively
	// the nodes are all cleared in the array of nodes
	// the payloads are NOT cleared and still hold on their value
	// return the array of Id of the removed nodes
	TArray<FNodeId> RemoveNode(FNodeId RemovedNodeId);

	// Traverser Utility
	using FNodeVisitor = void (*)(FNodeId InNodeId, int32 InDepth, int32 InSiblingNum, const FPayload& InPayload);
	template <typename V>
	FNodeId TraverseNode(V InVisitor, FNodeId InNodeId, int32 InDepth, int32 InSiblingNum) const
	{
		// First plan for the next child node and sibling we will be visiting
		// default with the InNodeId is the root case
		FNodeId NextChildId = Root;	
		FNodeId NextSiblingId = InvalidId;

		// Traverse this node if not the root tree
		if (InNodeId != InvalidId)
		{
			if (!IsValidId(InNodeId))
				return InvalidId;

			const FNode& Node = Nodes[InNodeId];
			const FPayload& Payload = Payloads[InNodeId];
			InVisitor(InNodeId, InDepth, InSiblingNum, Payload);

			// And since we have visited a valid node, lets visit the children next and the next sibling after
			NextChildId = Node.ChildrenHead;
			NextSiblingId = Node.NextSibling;
		}

		// Traverse children
		{
			InDepth++;
			int32 SiblingNum = 0;
			while (IsValidId(NextChildId))
			{
				NextChildId = TraverseNode(InVisitor, NextChildId, InDepth, SiblingNum);
				SiblingNum++;
			}
			InDepth--;
		}

		return NextSiblingId;
	}

	template <typename V>
	void Traverse(V InVisitor, FNodeId InRootNodeId = InvalidId) const
	{
		TraverseNode(InVisitor, InRootNodeId, 0, 0);
	}

	// Logging the tree data structure, useful for debugging
	// generate a multiline string representing the tree nodes hierarchy with the payload info per node
	FString Log(FString InTab) const;

private:
	FNodeId AllocNode(const FPayload& InPayload);

};

template<>
struct TStructOpsTypeTraits<FMaterialLayersFunctionsTree>
: public TStructOpsTypeTraitsBase2<FMaterialLayersFunctionsTree>
{
	enum
	{
		WithSerializer = true,
		WithCopy = true,
		WithIdenticalViaEquality = true
	};
};

USTRUCT()
struct FMaterialLayersFunctionsRuntimeData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<TObjectPtr<class UMaterialFunctionInterface>> Layers;

	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<TObjectPtr<class UMaterialFunctionInterface>> Blends;

	// ENABLE_MATERIAL_LAYER_PROTOTYPE : UProperty describing the tree data structure
	UPROPERTY()
	FMaterialLayersFunctionsTree Tree;

	FMaterialLayersFunctionsRuntimeData() = default;
	FMaterialLayersFunctionsRuntimeData(const FMaterialLayersFunctionsRuntimeData& Rhs)
		: Layers(Rhs.Layers)
		, Blends(Rhs.Blends)
#if ENABLE_MATERIAL_LAYER_PROTOTYPE
		, Tree(Rhs.Tree)
#endif
	{}

	FMaterialLayersFunctionsRuntimeData(const FMaterialLayersFunctions& Rhs) = delete;

	FMaterialLayersFunctionsRuntimeData& operator=(const FMaterialLayersFunctionsRuntimeData& Rhs)
	{
		Layers = Rhs.Layers;
		Blends = Rhs.Blends;
#if ENABLE_MATERIAL_LAYER_PROTOTYPE
		Tree = Rhs.Tree;
#endif
		return *this;
	}

	FMaterialLayersFunctionsRuntimeData& operator=(const FMaterialLayersFunctions& Rhs) = delete;

	ENGINE_API ~FMaterialLayersFunctionsRuntimeData();

	void Empty()
	{
		Layers.Empty();
		Blends.Empty();
#if ENABLE_MATERIAL_LAYER_PROTOTYPE
		Tree.Empty();
#endif
	}

	FORCEINLINE bool operator==(const FMaterialLayersFunctionsRuntimeData& Other) const
	{
		if (Layers != Other.Layers || Blends != Other.Blends
#if ENABLE_MATERIAL_LAYER_PROTOTYPE
			|| Tree != Other.Tree
#endif
		)
		{
			return false;
		}
		return true;
	}

	FORCEINLINE bool operator!=(const FMaterialLayersFunctionsRuntimeData& Other) const
	{
		return !operator==(Other);
	}

	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);
	
	ENGINE_API void PostSerialize(const FArchive& Ar);

#if WITH_EDITOR
	const FMaterialLayersFunctionsID GetID(const FMaterialLayersFunctionsEditorOnlyData& EditorOnly) const;
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
private:
	/** FMaterialLayersFunctionsRuntimeData can be deserialized from an FMaterialLayersFunctions property, will store the editor-only portion here */
	TUniquePtr<FMaterialLayersFunctionsEditorOnlyData> LegacySerializedEditorOnlyData;



	friend struct FStaticParameterSet;
#endif // WITH_EDITORONLY_DATA
};

template<>
struct TStructOpsTypeTraits<FMaterialLayersFunctionsRuntimeData> : TStructOpsTypeTraitsBase2<FMaterialLayersFunctionsRuntimeData>
{
	enum { WithStructuredSerializeFromMismatchedTag = true };
	enum { WithPostSerialize = true };
};

USTRUCT()
struct FMaterialLayersFunctions : public FMaterialLayersFunctionsRuntimeData
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITOR
	using ID = FMaterialLayersFunctionsID;
#endif // WITH_EDITOR

	static ENGINE_API const FGuid BackgroundGuid;

	FMaterialLayersFunctions() = default;
	FMaterialLayersFunctions(const FMaterialLayersFunctionsRuntimeData&) = delete;

	FMaterialLayersFunctionsRuntimeData& GetRuntime() { return *this; }
	const FMaterialLayersFunctionsRuntimeData& GetRuntime() const { return *this; }

	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	FMaterialLayersFunctionsEditorOnlyData EditorOnly;

	void Empty()
	{
		FMaterialLayersFunctionsRuntimeData::Empty();
#if WITH_EDITOR
		EditorOnly.Empty();
#endif
	}

	inline bool IsEmpty() const { return Layers.Num() == 0; }

#if WITH_EDITOR
	ENGINE_API void AddDefaultBackgroundLayer();

	ENGINE_API int32 AppendBlendedLayer();

	ENGINE_API int32 AddLayerCopy(const FMaterialLayersFunctionsRuntimeData& Source,
		const FMaterialLayersFunctionsEditorOnlyData& SourceEditorOnly,
		int32 SourceLayerIndex,
		bool bVisible,
		EMaterialLayerLinkState LinkState);

	int32 AddLayerCopy(const FMaterialLayersFunctions& Source,
		int32 SourceLayerIndex,
		bool bVisible,
		EMaterialLayerLinkState LinkState)
	{
		return AddLayerCopy(Source, Source.EditorOnly, SourceLayerIndex, bVisible, LinkState);
	}

	ENGINE_API void InsertLayerCopy(const FMaterialLayersFunctionsRuntimeData& Source,
		const FMaterialLayersFunctionsEditorOnlyData& SourceEditorOnly,
		int32 SourceLayerIndex,
		EMaterialLayerLinkState LinkState,
		int32 LayerIndex);

	void InsertLayerCopy(const FMaterialLayersFunctions& Source,
		int32 SourceLayerIndex,
		EMaterialLayerLinkState LinkState,
		int32 LayerIndex)
	{
		return InsertLayerCopy(Source, Source.EditorOnly, SourceLayerIndex, LinkState, LayerIndex);
	}

	ENGINE_API void RemoveBlendedLayerAt(int32 Index);

	ENGINE_API void MoveBlendedLayer(int32 SrcLayerIndex, int32 DstLayerIndex);

	const ID GetID() const { return FMaterialLayersFunctionsRuntimeData::GetID(EditorOnly); }

	/** Gets a string representation of the ID */
	ENGINE_API FString GetStaticPermutationString() const;
	ENGINE_API void AppendStaticPermutationKey(FShaderKeyGenerator& KeyGen) const;

	ENGINE_API void UnlinkLayerFromParent(int32 Index);
	ENGINE_API bool IsLayerLinkedToParent(int32 Index) const;
	ENGINE_API void RelinkLayersToParent();
	ENGINE_API bool HasAnyUnlinkedLayers() const;

	void ToggleBlendedLayerVisibility(int32 Index)
	{
		if (Index < 0)
			return;
		check(EditorOnly.LayerStates.IsValidIndex(Index));
		EditorOnly.LayerStates[Index] = !EditorOnly.LayerStates[Index];
	}

	void SetBlendedLayerVisibility(int32 Index, bool InNewVisibility)
	{
		if (Index < 0)
			return;
		check(EditorOnly.LayerStates.IsValidIndex(Index));
		EditorOnly.LayerStates[Index] = InNewVisibility;
	}

	bool GetLayerVisibility(int32 Index) const
	{
		if (Index < 0)
			return true;
		check(EditorOnly.LayerStates.IsValidIndex(Index));
		return EditorOnly.LayerStates[Index];
	}

	FText GetLayerName(int32 Counter) const
	{
		FText LayerName = FText::Format(LOCTEXT("LayerPrefix", "Layer {0}"), Counter);
		if (EditorOnly.LayerNames.IsValidIndex(Counter))
		{
			LayerName = EditorOnly.LayerNames[Counter];
		}
		return LayerName;
	}

	void LinkAllLayersToParent()
	{
		EditorOnly.LinkAllLayersToParent();
	}

	static ENGINE_API bool MatchesParent(const FMaterialLayersFunctionsRuntimeData& Runtime,
		const FMaterialLayersFunctionsEditorOnlyData& EditorOnly,
		const FMaterialLayersFunctionsRuntimeData& ParentRuntime,
		const FMaterialLayersFunctionsEditorOnlyData& ParentEditorOnly);

	bool MatchesParent(const FMaterialLayersFunctions& Parent) const
	{
		return MatchesParent(GetRuntime(), EditorOnly, Parent.GetRuntime(), Parent.EditorOnly);
	}

	static ENGINE_API bool ResolveParent(const FMaterialLayersFunctionsRuntimeData& ParentRuntime,
		const FMaterialLayersFunctionsEditorOnlyData& ParentEditorOnly,
		FMaterialLayersFunctionsRuntimeData& Runtime,
		FMaterialLayersFunctionsEditorOnlyData& EditorOnly,
		TArray<int32>& OutRemapLayerIndices);

	bool ResolveParent(const FMaterialLayersFunctions& Parent, TArray<int32>& OutRemapLayerIndices)
	{
		return FMaterialLayersFunctions::ResolveParent(Parent.GetRuntime(), Parent.EditorOnly, GetRuntime(), EditorOnly, OutRemapLayerIndices);
	}


	static ENGINE_API void Validate(const FMaterialLayersFunctionsRuntimeData& Runtime, const FMaterialLayersFunctionsEditorOnlyData& EditorOnly);

	void Validate()
	{
		Validate(GetRuntime(), EditorOnly);
	}

	ENGINE_API void SerializeLegacy(FArchive& Ar);

	static ENGINE_API void CheckAndRepairPostSerializeEditorOnlyDataForRuntimeData(FMaterialLayersFunctionsRuntimeData& Runtime, FMaterialLayersFunctionsEditorOnlyData& EditorOnly);

#endif // WITH_EDITOR

	ENGINE_API void PostSerialize(const FArchive& Ar);

	FORCEINLINE bool operator==(const FMaterialLayersFunctions& Other) const
	{
		if (!FMaterialLayersFunctionsRuntimeData::operator==(Other))
		{
			return false;
		}
#if WITH_EDITORONLY_DATA
		if (EditorOnly != Other.EditorOnly)
		{
			return false;
		}
#endif // WITH_EDITORONLY_DATA
		return true;
	}

	FORCEINLINE bool operator!=(const FMaterialLayersFunctions& Other) const
	{
		return !operator==(Other);
	}

	// Define the Tree authoring of the layers
#if WITH_EDITOR
	using FLayerNodeId = FMaterialLayersFunctionsTree::FNodeId;
	using FLayerNodeIdArray = TArray<FLayerNodeId>;
	using FLayerNodePayload = FMaterialLayersFunctionsTree::FPayload; 

	ENGINE_API FLayerNodeId GetNodeParent(FLayerNodeId InNode) const;
	ENGINE_API FLayerNodeIdArray GetNodeChildren(FLayerNodeId InNode) const;
	ENGINE_API FLayerNodePayload GetNodePayload(FLayerNodeId InNodeId) const;
	ENGINE_API int32 GetNodeDepth(FLayerNodeId InNodeId) const;

	ENGINE_API int32 GetLayerFuncIndex(FLayerNodeId InNodeId) const;
	ENGINE_API int32 GetBlendFuncIndex(FLayerNodeId InNodeId) const;

	ENGINE_API bool	 CanAppendLayerNode(FLayerNodeId InParent) const;
	ENGINE_API FLayerNodeId	AppendLayerNode(FLayerNodeId InParent, int32 InSiblingIndex = -1);

	ENGINE_API bool	 CanRemoveLayerNode(FLayerNodeId InNodeId) const;
	ENGINE_API void	 RemoveLayerNodeAt(FLayerNodeId InNodeId);

	ENGINE_API void MoveLayerNode(FLayerNodeId InNodeId, FLayerNodeId DstParentId, int32 InSiblingIndex = -1);

#endif // WITH_EDITOR

private:
	UPROPERTY()
	TArray<bool> LayerStates_DEPRECATED;

	UPROPERTY()
	TArray<FText> LayerNames_DEPRECATED;

	UPROPERTY()
	TArray<bool> RestrictToLayerRelatives_DEPRECATED;

	UPROPERTY()
	TArray<bool> RestrictToBlendRelatives_DEPRECATED;

	UPROPERTY()
	TArray<FGuid> LayerGuids_DEPRECATED;

	UPROPERTY()
	TArray<EMaterialLayerLinkState> LayerLinkStates_DEPRECATED;

	UPROPERTY()
	TArray<FGuid> DeletedParentLayerGuids_DEPRECATED;


	// Don't allowing comparing a full FMaterialLayersFunctions against partial RuntimeData
	friend bool operator==(const FMaterialLayersFunctions&, const FMaterialLayersFunctionsRuntimeData&) = delete;
	friend bool operator==(const FMaterialLayersFunctionsRuntimeData&, const FMaterialLayersFunctions&) = delete;
	friend bool operator!=(const FMaterialLayersFunctions&, const FMaterialLayersFunctionsRuntimeData&) = delete;
	friend bool operator!=(const FMaterialLayersFunctionsRuntimeData&, const FMaterialLayersFunctions&) = delete;
};

template<>
struct TStructOpsTypeTraits<FMaterialLayersFunctions> : TStructOpsTypeTraitsBase2<FMaterialLayersFunctions>
{
	enum { WithPostSerialize = true };
};

#undef LOCTEXT_NAMESPACE
