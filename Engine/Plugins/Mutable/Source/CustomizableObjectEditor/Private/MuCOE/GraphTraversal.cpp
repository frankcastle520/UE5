// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/GraphTraversal.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "MuCOE/CustomizableObjectPin.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/Nodes/CustomizableObjectNodeEnumParameter.h"
#include "MuCOE/Nodes/CustomizableObjectNodeExposePin.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierExtendMeshSection.h"
#include "MuCOE/Nodes/CustomizableObjectNodeExternalPin.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterial.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterialVariation.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMeshGeometryOperation.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMeshMorph.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMeshMorphStackApplication.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMeshMorphStackDefinition.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMeshReshape.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMeshSwitch.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMeshVariation.h"
#include "MuCOE/Nodes/CustomizableObjectNodeObject.h"
#include "MuCOE/Nodes/CustomizableObjectNodeSkeletalMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeStaticMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTable.h"
#include "MuCOE/Nodes/CustomizableObjectNodeAnimationPose.h"
#include "MuCOE/Nodes/CustomizableObjectNodeObjectGroup.h"
#include "MuCOE/Nodes/CustomizableObjectNodeReroute.h"
#include "UObject/UObjectIterator.h"


TArray<UEdGraphPin*> FollowPinArray(const UEdGraphPin& Pin, bool bIgnoreOrphan, bool* bOutCycleDetected)
{
	bool bCycleDetected = false;
		
	TArray<UEdGraphPin*> Result;
	
	TSet<const UEdGraphPin*> Visited;

	TArray<const UEdGraphPin*> PinsToVisit;
	PinsToVisit.Add(&Pin);
	while (PinsToVisit.Num())
	{
		const UEdGraphPin& CurrentPin = *PinsToVisit.Pop();

		if (!bIgnoreOrphan && IsPinOrphan(CurrentPin))
		{
			continue;
		}

		Visited.FindOrAdd(&CurrentPin, &bCycleDetected);
		if (bCycleDetected)
		{
			continue;
		}

		for (UEdGraphPin* LinkedPin : CurrentPin.LinkedTo)
		{
			if (!bIgnoreOrphan && IsPinOrphan(*LinkedPin))
			{
				continue;
			}
			
			if (const UCustomizableObjectNodeExposePin* ExposePinNode = Cast<UCustomizableObjectNodeExposePin>(LinkedPin->GetOwningNodeUnchecked()))
			{
				check(Pin.Direction == EGPD_Output);

				for (TObjectIterator<UCustomizableObjectNodeExternalPin> It; It; ++It)
				{
					const UCustomizableObjectNodeExternalPin* LinkedNode = *It;
					
					if (IsValid(LinkedNode) &&
						!LinkedNode->IsTemplate() &&
						LinkedNode->GetNodeExposePin() == ExposePinNode)
					{
						const UEdGraphPin* ExternalPin = LinkedNode->GetExternalPin();
						check(ExternalPin);
						PinsToVisit.Add(ExternalPin);
					}
				}
			}
			else if (const UCustomizableObjectNodeExternalPin* ExternalPinNode = Cast<UCustomizableObjectNodeExternalPin>(LinkedPin->GetOwningNodeUnchecked()))
			{
				check(Pin.Direction == EGPD_Input);
				
				if (const UCustomizableObjectNodeExposePin* LinkedNode = ExternalPinNode->GetNodeExposePin())
				{
					const UEdGraphPin* ExposePin = LinkedNode->InputPin();
					check(ExposePin);
					PinsToVisit.Add(ExposePin);
				}
			}
			else if (const UCustomizableObjectNodeReroute* NodeReroute = Cast<UCustomizableObjectNodeReroute>(LinkedPin->GetOwningNodeUnchecked()))
			{
				PinsToVisit.Add(Pin.Direction == EGPD_Input ? NodeReroute->GetInputPin() : NodeReroute->GetOutputPin());
			}
			else
			{
				Result.Add(LinkedPin);
			}
		}
	}

	if (bOutCycleDetected)
	{
		*bOutCycleDetected = bCycleDetected;	
	}

	return Result;
}


TArray<UEdGraphPin*> FollowInputPinArray(const UEdGraphPin& Pin, bool* bOutCycleDetected)
{
	check(Pin.Direction == EGPD_Input);
	return FollowPinArray(Pin, true, bOutCycleDetected);
}


UEdGraphPin* FollowInputPin(const UEdGraphPin& Pin, bool* CycleDetected)
{
	TArray<UEdGraphPin*> Result = FollowInputPinArray(Pin, CycleDetected);
	check(Result.Num() <= 1); // Use FollowPinImmersive if the pin can have more than one input.

	if (!Result.IsEmpty())
	{
		return Result[0];
	}
	else
	{
		return nullptr;
	}
}


TArray<UEdGraphPin*> FollowOutputPinArray(const UEdGraphPin& Pin, bool* bOutCycleDetected)
{
	check(Pin.Direction == EGPD_Output);
	return FollowPinArray(Pin, true, bOutCycleDetected);
}


UEdGraphPin* FollowOutputPin(const UEdGraphPin& Pin, bool* CycleDetected)
{
	TArray<UEdGraphPin*> Result = FollowOutputPinArray(Pin, CycleDetected);
	check(Result.Num() <= 1); // Use FollowPinImmersive if the pin can have more than one input.

	if (!Result.IsEmpty())
	{
		return Result[0];
	}
	else
	{
		return nullptr;
	}
}


TArray<UEdGraphPin*> ReverseFollowPinArray(const UEdGraphPin& Pin, bool bIgnoreOrphan, bool* bOutCycleDetected)
{
	bool bCycleDetected = false;
		
	TArray<UEdGraphPin*> Result;
	
	TSet<const UEdGraphPin*> Visited;

	TArray<UEdGraphPin*> PinsToVisit;
	PinsToVisit.Add(const_cast<UEdGraphPin*>(&Pin));
	while (PinsToVisit.Num())
	{
		UEdGraphPin& CurrentPin = *PinsToVisit.Pop();

		if (!bIgnoreOrphan && IsPinOrphan(CurrentPin))
		{
			continue;
		}

		Visited.FindOrAdd(&CurrentPin, &bCycleDetected);
		if (bCycleDetected)
		{
			continue;
		}
		
		if (const UCustomizableObjectNodeExposePin* ExposePinNode = Cast<UCustomizableObjectNodeExposePin>(CurrentPin.GetOwningNodeUnchecked()))
		{
			check(Pin.Direction == EGPD_Input);

			for (TObjectIterator<UCustomizableObjectNodeExternalPin> It; It; ++It)
			{
				const UCustomizableObjectNodeExternalPin* LinkedNode = *It;

				if (IsValid(LinkedNode) &&
					!LinkedNode->IsTemplate() &&
					LinkedNode->GetNodeExposePin() == ExposePinNode)
				{
					const UEdGraphPin* ExternalPin = LinkedNode->GetExternalPin();
					check(ExternalPin);

					for (UEdGraphPin* LinkedPin : ExternalPin->LinkedTo)
					{
						PinsToVisit.Add(LinkedPin);
					}
				}
			}
		}
		else if (const UCustomizableObjectNodeExternalPin* ExternalPinNode = Cast<UCustomizableObjectNodeExternalPin>(CurrentPin.GetOwningNodeUnchecked()))
		{
			check(Pin.Direction == EGPD_Output);
				
			if (const UCustomizableObjectNodeExposePin* LinkedNode = ExternalPinNode->GetNodeExposePin())
			{
				const UEdGraphPin* ExposePin = LinkedNode->InputPin();
				check(ExposePin);

				for (UEdGraphPin* LinkedPin : ExposePin->LinkedTo)
				{
					PinsToVisit.Add(LinkedPin);
				}
			}
		}
		else if (const UCustomizableObjectNodeReroute* NodeReroute = Cast<UCustomizableObjectNodeReroute>(CurrentPin.GetOwningNodeUnchecked()))
		{
			UEdGraphPin* ReroutePin = Pin.Direction == EGPD_Output ? NodeReroute->GetInputPin() : NodeReroute->GetOutputPin();

			for (UEdGraphPin* LinkedPin : ReroutePin->LinkedTo)
			{
				PinsToVisit.Add(LinkedPin);
			}
		}
		else
		{
			if (bIgnoreOrphan || !IsPinOrphan(CurrentPin))
			{
				Result.Add(&CurrentPin);
			}
		}
	}
	
	if (bOutCycleDetected)
	{
		*bOutCycleDetected = bCycleDetected;	
	}
	
	return Result;
}


UCustomizableObjectNodeObject* GetRootNode(const UCustomizableObject* Object, bool& bOutMultipleBaseObjectsFound)
{
	// Look for the base object node
	UCustomizableObjectNodeObject* Root = nullptr;
	TArray<UCustomizableObjectNodeObject*> ObjectNodes;
	Object->GetPrivate()->GetSource()->GetNodesOfClass<UCustomizableObjectNodeObject>(ObjectNodes);

	bOutMultipleBaseObjectsFound = false;

	for (TArray<UCustomizableObjectNodeObject*>::TIterator It(ObjectNodes); It; ++It)
	{
		if ((*It)->bIsBase)
		{
			if (Root)
			{
				bOutMultipleBaseObjectsFound = true;
				break;
			}
			else
			{
				Root = *It;
			}
		}
	}

	return Root;
}


bool GetParentsUntilRoot(const UCustomizableObject* Object, TArray<UCustomizableObjectNodeObject*>& ArrayNodeObject, TArray<const  UCustomizableObject*>& ArrayCustomizableObject)
{
	bool MultipleBaseObjectsFound;
	UCustomizableObjectNodeObject* Root = GetRootNode(Object, MultipleBaseObjectsFound);

	bool bSuccess = true;

	if (!MultipleBaseObjectsFound && (Root != nullptr))
	{
		if (!ArrayCustomizableObject.Contains(Object))
		{
			ArrayNodeObject.Add(Root);
			ArrayCustomizableObject.Add(Object);
		}
		else
		{
			// This object has already been visted which means that there is a Cycle between Customizable Objects
			return false;
		}

		if (Root->ParentObject != nullptr)
		{
			bSuccess = GetParentsUntilRoot(Root->ParentObject, ArrayNodeObject, ArrayCustomizableObject);
		}
	}

	return bSuccess;
}


bool HasCandidateAsParent(UCustomizableObjectNodeObject* Node, UCustomizableObject* ParentCandidate)
{
	if (Node->ParentObject == ParentCandidate)
	{
		return true;
	}

	if (Node->ParentObject != nullptr)
	{
		bool MultipleBaseObjectsFound;
		UCustomizableObjectNodeObject* ParentNodeObject = GetRootNode(Node->ParentObject, MultipleBaseObjectsFound);

		if ((ParentNodeObject->ParentObject == nullptr) || MultipleBaseObjectsFound)
		{
			return false;
		}
		else
		{
			return HasCandidateAsParent(ParentNodeObject, ParentCandidate);
		}
	}

	return false;
}


UCustomizableObject* GetFullGraphRootObject(const UCustomizableObjectNodeObject* Node, TArray<UCustomizableObject*>& VisitedObjects)
{
	if (Node->ParentObject != nullptr)
	{
		VisitedObjects.Add(Node->ParentObject);

		bool MultipleBaseObjectsFound;
		UCustomizableObjectNodeObject* Root = GetRootNode(Node->ParentObject, MultipleBaseObjectsFound);

		if (Root->ParentObject == nullptr)
		{
			if (MultipleBaseObjectsFound)
			{
				return nullptr;
			}
			else
			{
				return Node->ParentObject;
			}
		}
		else
		{
			if (VisitedObjects.Contains(Root->ParentObject))
			{
				//There is a cycle
				return nullptr;
			}
			else
			{
				return GetFullGraphRootObject(Root, VisitedObjects);
			}
		}
	}

	return nullptr;
}


UCustomizableObject* GetRootObject(const UCustomizableObjectNode& Node)
{
	return CastChecked<UCustomizableObject>(Node.GetGraph()->GetOuter());
}


UCustomizableObject* GraphTraversal::GetRootObject(UCustomizableObject* ChildObject)
{
	const UCustomizableObject* ConstChildObject = ChildObject;
	return const_cast<UCustomizableObject*>(GetRootObject(ConstChildObject));
}


const UCustomizableObject* GraphTraversal::GetRootObject(const UCustomizableObject* ChildObject)
{
	// Grab a node to start the search -> Get the root since it should be always present
	bool bMultipleBaseObjectsFound = false;
	UCustomizableObjectNodeObject* ObjectRootNode = GetRootNode(ChildObject, bMultipleBaseObjectsFound);

	if (ObjectRootNode && ObjectRootNode->ParentObject)
	{
		TArray<UCustomizableObject*> VisitedNodes;
		return GetFullGraphRootObject(ObjectRootNode, VisitedNodes);
	}

	// No parent object found, return input as the parent of the graph
	// This can also mean the ObjectRootNode does not exist because it has not been opened yet (so no nodes have been generated)
	return ChildObject;
}


void GraphTraversal::VisitNodes(UCustomizableObjectNode& StartNode, const TMultiMap<FGuid, UCustomizableObjectNodeObject*>& ObjectGroupMap, const TFunction<void(UCustomizableObjectNode&)>& VisitFunction)
{
	TSet<UCustomizableObjectNode*> VisitedNodes;
	
	TArray<UCustomizableObjectNode*> NodesToVisit;
	NodesToVisit.Add(&StartNode);

	while (!NodesToVisit.IsEmpty())
	{
		UCustomizableObjectNode* CurrentNode = NodesToVisit.Pop();

		if (VisitedNodes.Contains(CurrentNode))
		{
			continue;
		}

		VisitedNodes.Add(CurrentNode);

		VisitFunction(*CurrentNode);

		for (UEdGraphPin* Pin : CurrentNode->GetAllNonOrphanPins())
		{
			if (Pin->Direction != EGPD_Input)
			{
				continue;
			}

			for (UEdGraphPin* ConnectedPin : FollowInputPinArray(*Pin))
			{
				UEdGraphNode* ConnectedNode = ConnectedPin->GetOwningNode();
				
				if (UCustomizableObjectNode* Node = Cast<UCustomizableObjectNode>(ConnectedNode))
				{
					NodesToVisit.Add(Node);
				}

				if (UCustomizableObjectNodeObjectGroup* ObjectGroupNode = Cast<UCustomizableObjectNodeObjectGroup>(ConnectedNode))
				{
					TArray<UCustomizableObjectNodeObject*> ChildObjectNodes;
					ObjectGroupMap.MultiFind(ObjectGroupNode->NodeGuid, ChildObjectNodes);
						
					for (UCustomizableObjectNodeObject* ChildObjectNode : ChildObjectNodes)
					{
						NodesToVisit.Add(ChildObjectNode);
					}
				}
			}
		}
	}
}


UCustomizableObjectNodeObject* GetFullGraphRootNodeObject(const UCustomizableObjectNodeObject* Node, TArray<const UCustomizableObject*>& VisitedObjects)
{
	if (Node->ParentObject != nullptr)
	{
		VisitedObjects.Add(Node->ParentObject);

		bool MultipleBaseObjectsFound;
		UCustomizableObjectNodeObject* Root = GetRootNode(Node->ParentObject, MultipleBaseObjectsFound);

		if (Root->ParentObject == nullptr)
		{
			if (MultipleBaseObjectsFound)
			{
				return nullptr;
			}
			else
			{
				return Root;
			}
		}
		else
		{
			if (VisitedObjects.Contains(Root->ParentObject))
			{
				//There is a cycle
				return nullptr;
			}
			else
			{
				return GetFullGraphRootNodeObject(Root, VisitedObjects);
			}
		}
	}

	return nullptr;
}


const UEdGraphPin* FindMeshBaseSource(const UEdGraphPin& Pin, const bool bOnlyLookForStaticMesh)
{
	check(Pin.Direction == EGPD_Output);
	check(Pin.PinType.PinCategory == UEdGraphSchema_CustomizableObject::PC_Mesh 
		||
		Pin.PinType.PinCategory == UEdGraphSchema_CustomizableObject::PC_PassThroughMesh
		||
		Pin.PinType.PinCategory == UEdGraphSchema_CustomizableObject::PC_Material
		||
		Pin.PinType.PinCategory == UEdGraphSchema_CustomizableObject::PC_Modifier
	);
	
	const UEdGraphNode* Node = Pin.GetOwningNode();
	check(Node);

	if (Cast<UCustomizableObjectNodeSkeletalMesh>(Node))
	{
		if (!bOnlyLookForStaticMesh)
		{
			return &Pin;
		}
	}

	else if (Cast<UCustomizableObjectNodeStaticMesh>(Node))
	{
		return &Pin;
	}

	else if (const UCustomizableObjectNodeMeshGeometryOperation* TypedNodeGeom = Cast<UCustomizableObjectNodeMeshGeometryOperation>(Node))
	{
		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeGeom->MeshAPin()))
		{
			return FindMeshBaseSource(*ConnectedPin, bOnlyLookForStaticMesh);
		}
	}

	else if (const UCustomizableObjectNodeMeshReshape* TypedNodeReshape = Cast<UCustomizableObjectNodeMeshReshape>(Node))
	{
		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeReshape->BaseMeshPin()))
		{
			return FindMeshBaseSource(*ConnectedPin, bOnlyLookForStaticMesh);
		}
	}

	else if (const UCustomizableObjectNodeMeshMorph* TypedNodeMorph = Cast<UCustomizableObjectNodeMeshMorph>(Node))
	{
		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeMorph->MeshPin()))
		{
			return FindMeshBaseSource(*ConnectedPin, bOnlyLookForStaticMesh);
		}
	}

	else if (const UCustomizableObjectNodeMeshSwitch* TypedNodeSwitch = Cast<UCustomizableObjectNodeMeshSwitch>(Node))
	{
		if (const UEdGraphPin* EnumParameterPin = FollowInputPin(*TypedNodeSwitch->SwitchParameter()))
		{
			if (const UCustomizableObjectNodeEnumParameter* EnumNode = Cast<UCustomizableObjectNodeEnumParameter>(EnumParameterPin->GetOwningNode()))
			{
				if (const UEdGraphPin* DefaultPin = TypedNodeSwitch->GetElementPin(EnumNode->DefaultIndex))
				{
					if (const UEdGraphPin* ConnectedPin = FollowInputPin(*DefaultPin))
					{
						return FindMeshBaseSource(*ConnectedPin, bOnlyLookForStaticMesh);
					}
				}
			}
		}
	}

	else if (const UCustomizableObjectNodeMeshVariation* TypedNodeMeshVar = Cast<UCustomizableObjectNodeMeshVariation>(Node))
	{
		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeMeshVar->DefaultPin()))
		{
			return FindMeshBaseSource(*ConnectedPin, bOnlyLookForStaticMesh);
		}

		for (int32 i = 0; i < TypedNodeMeshVar->GetNumVariations(); ++i)
		{
			if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeMeshVar->VariationPin(i)))
			{
				return FindMeshBaseSource(*ConnectedPin, bOnlyLookForStaticMesh);
			}
		}
	}

	else if (const UCustomizableObjectNodeMaterialBase* TypedNodeMat = Cast<UCustomizableObjectNodeMaterialBase>(Node))
	{
		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeMat->GetMeshPin()))
		{
			return FindMeshBaseSource(*ConnectedPin, bOnlyLookForStaticMesh);
		}
	}

	else if (const UCustomizableObjectNodeMaterialVariation* TypedNodeMatVar = Cast<UCustomizableObjectNodeMaterialVariation>(Node))
	{
		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeMatVar->DefaultPin()))
		{
			return FindMeshBaseSource(*ConnectedPin, bOnlyLookForStaticMesh);
		}
	}

	else if (const UCustomizableObjectNodeModifierExtendMeshSection* TypedNodeExtend = Cast<UCustomizableObjectNodeModifierExtendMeshSection>(Node))
	{
		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeExtend->AddMeshPin()))
		{
			return FindMeshBaseSource(*ConnectedPin, bOnlyLookForStaticMesh);
		}
	}
	
	else if (const UCustomizableObjectNodeMeshMorphStackDefinition* TypedNodeMorphStackDef = Cast<UCustomizableObjectNodeMeshMorphStackDefinition>(Node))
	{
		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeMorphStackDef->GetMeshPin()))
		{
			return FindMeshBaseSource(*ConnectedPin, bOnlyLookForStaticMesh);
		}
	}

	else if (const UCustomizableObjectNodeMeshMorphStackApplication* TypedNodeMorphStackApp = Cast<UCustomizableObjectNodeMeshMorphStackApplication>(Node))
	{
		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeMorphStackApp->GetMeshPin()))
		{
			return FindMeshBaseSource(*ConnectedPin, bOnlyLookForStaticMesh);
		}
	}

	else if (const UCustomizableObjectNodeMeshReshape* NodeMeshReshape = Cast<UCustomizableObjectNodeMeshReshape>(Node))
	{
		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*NodeMeshReshape->BaseMeshPin()))
		{
			return FindMeshBaseSource(*ConnectedPin, bOnlyLookForStaticMesh);
		}
	}
	
	else if (Cast<UCustomizableObjectNodeTable>(Node))
	{
		if (!bOnlyLookForStaticMesh)
		{
			return &Pin;
		}
	}

	else if (const UCustomizableObjectNodeAnimationPose* NodeMeshPose = Cast<UCustomizableObjectNodeAnimationPose>(Node))
	{
		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*NodeMeshPose->GetInputMeshPin()))
		{
			return FindMeshBaseSource(*ConnectedPin, bOnlyLookForStaticMesh);
		}
	}

	else
	{
		unimplemented(); // Case missing.
	}
	
	return nullptr;
}


void GetNodeGroupObjectNodeMappingImmersive(UCustomizableObject* Object, FAssetRegistryModule& AssetRegistryModule, TSet<UCustomizableObject*>& Visited, TMultiMap<FGuid, UCustomizableObjectNodeObject*>& Mapping)
{
	Visited.Add(Object);

	TArray<FName> ArrayReferenceNames;
	AssetRegistryModule.Get().GetReferencers(*Object->GetOuter()->GetPathName(), ArrayReferenceNames, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Hard);

	FARFilter Filter;
	for (const FName& ReferenceName : ArrayReferenceNames)
	{
		if (!ReferenceName.ToString().StartsWith(TEXT("/TempAutosave")))
		{
			Filter.PackageNames.Add(ReferenceName);
		}
	}

	Filter.bIncludeOnlyOnDiskAssets = false;
	
	TArray<FAssetData> ArrayAssetData;
	AssetRegistryModule.Get().GetAssets(Filter, ArrayAssetData);

	for (FAssetData& AssetData : ArrayAssetData)
	{
		UCustomizableObject* ChildObject = Cast<UCustomizableObject>(AssetData.GetAsset());
		if (!ChildObject)
		{
			continue;
		}

		if (ChildObject != Object && !ChildObject->HasAnyFlags(RF_Transient))
		{
			bool bMultipleBaseObjectsFound = false;
			UCustomizableObjectNodeObject* ChildRoot = GetRootNode(ChildObject, bMultipleBaseObjectsFound);

			if (ChildRoot && !bMultipleBaseObjectsFound)
			{
				if (ChildRoot->ParentObject == Object)
				{
					Mapping.Add(ChildRoot->ParentObjectGroupId, ChildRoot);
				}
			}
		}

		if (!Visited.Contains(ChildObject))
		{
			GetNodeGroupObjectNodeMappingImmersive(ChildObject, AssetRegistryModule, Visited, Mapping);
		}
	}
}


TMultiMap<FGuid, UCustomizableObjectNodeObject*> GetNodeGroupObjectNodeMapping(UCustomizableObject* Object)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	TSet<UCustomizableObject*> Visited;
	TMultiMap<FGuid, UCustomizableObjectNodeObject*> Mapping;

	GetNodeGroupObjectNodeMappingImmersive(Object, AssetRegistryModule, Visited, Mapping);
	
	return Mapping;
}


void GetAllObjectsInGraph(UCustomizableObject* Object, TSet<UCustomizableObject*>& OutObjects)
{
	if (!Object)
	{
		return;
	}

	// Search the root of the CO's graph
	UCustomizableObject* RootObject = GraphTraversal::GetRootObject(Object);
	TMultiMap<FGuid, UCustomizableObjectNodeObject*> DummyMap;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	GetNodeGroupObjectNodeMappingImmersive(RootObject, AssetRegistryModule, OutObjects, DummyMap);
}


namespace GraphTraversal
{
	bool IsRootObject(const UCustomizableObject& Object)
	{
		const TObjectPtr<UEdGraph> Source = Object.GetPrivate()->GetSource();
		if (!Source || !Source->Nodes.Num())
		{
			// Conservative approach.
			return true;
		}

		TArray<UCustomizableObjectNodeObject*> ObjectNodes;
		Source->GetNodesOfClass<UCustomizableObjectNodeObject>(ObjectNodes);

		// Look for the base object node
		const UCustomizableObjectNodeObject* Root = nullptr;
		for (TArray<UCustomizableObjectNodeObject*>::TIterator It(ObjectNodes); It; ++It)
		{
			if ((*It)->bIsBase)
			{
				Root = *It;
			}
		}

		return Root && !Root->ParentObject;
	}
}


void NodePinConnectionListChanged(const TArray<UEdGraphPin*>& Pins)
{
	TMap<UEdGraphNode*, TSet<UEdGraphPin*>> SortedPins;
	for (UEdGraphPin* Pin : Pins) 
	{
		if (UEdGraphNode* Node = Pin->GetOwningNodeUnchecked())
		{
			SortedPins.FindOrAdd(Node).Add(Pin);
		}
	}

	for (TTuple<UEdGraphNode*, TSet<UEdGraphPin*>> Pair : SortedPins)
	{
		for (UEdGraphPin* ConnectedPin : Pair.Value)
		{
			Pair.Key->PinConnectionListChanged(ConnectedPin);
		}
		
		Pair.Key->NodeConnectionListChanged();
	}
}
