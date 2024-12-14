// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpComponent.h"

#include "DatasmithSketchUpExportContext.h"
#include "DatasmithSketchUpMaterial.h"
#include "DatasmithSketchUpMetadata.h"
#include "DatasmithSketchUpString.h"
#include "DatasmithSketchUpSummary.h"
#include "DatasmithSketchUpUtils.h"

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/model/component_definition.h"
#include "SketchUpAPI/model/drawing_element.h"
#include "SketchUpAPI/model/entities.h"
#include <SketchUpAPI/model/entity.h>
#include "SketchUpAPI/model/group.h"
#include "SketchUpAPI/model/layer.h"
#include "SketchUpAPI/model/location.h"
#include "SketchUpAPI/model/model.h"
#include "SketchUpAPI/model/component_instance.h"
#include "SketchUpAPI/geometry/transformation.h"
#include "SketchUpAPI/geometry/vector3d.h"

#if !defined(SKP_SDK_2019) && !defined(SKP_SDK_2020)
#include "SketchUpAPI/model/layer_folder.h"
#endif

#include "DatasmithSceneExporter.h"
#include "DatasmithSketchUpSDKCeases.h"

#include "IDatasmithSceneElements.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"
#include "Algo/Compare.h"

#include "Misc/Paths.h"


#define REMOVE_MESHES_WHEN_INVISIBLE

using namespace DatasmithSketchUp;

void FDefinition::ParseNode(FExportContext& Context, FNodeOccurence& Node)
{
	// Process child nodes
	
	// Convert the SketchUp normal component instances into sub-hierarchies of Datasmith actors.
	for (SUComponentInstanceRef SComponentInstanceRef : GetEntities().GetComponentInstances())
	{
		TSharedPtr<FComponentInstance> ComponentInstance = Context.ComponentInstances.AddComponentInstance(*this, SComponentInstanceRef);
		if (ComponentInstance.IsValid())
		{
			FNodeOccurence& ChildNode = ComponentInstance->CreateNodeOccurrence(Context, Node);
			ComponentInstance->ParseNode(Context, ChildNode);
		}
	}

	// Convert the SketchUp group component instances into sub-hierarchies of Datasmith actors.
	for (SUGroupRef SGroupRef : GetEntities().GetGroups())
	{
		SUComponentInstanceRef SComponentInstanceRef = SUGroupToComponentInstance(SGroupRef);

		TSharedPtr<FComponentInstance> ComponentInstance = Context.ComponentInstances.AddComponentInstance(*this, SComponentInstanceRef);
		if (ComponentInstance.IsValid())
		{
			FNodeOccurence& ChildNode = ComponentInstance->CreateNodeOccurrence(Context, Node);
			ComponentInstance->ParseNode(Context, ChildNode);
		}
	}

	for (SUImageRef ImageRef : GetEntities().GetImages())
	{
		TSharedPtr<FImage> Image = Context.Images.AddImage(*this, ImageRef);
		if (Image.IsValid())
		{
			Image->CreateNodeOccurrence(Context, Node);
		}
	}
}

// Update mesh actors of an entiry with Entities(that is model or component instance)
void FEntityWithEntities::UpdateOccurrenceMeshActors(FExportContext& Context, FNodeOccurence& Node)
{
	FDefinition* EntityDefinition = GetDefinition();
	DatasmithSketchUp::FEntitiesGeometry* EntitiesGeometry = EntityDefinition->GetEntities().EntitiesGeometry.Get();

	if (!EntitiesGeometry)
	{
		return;
	}

	FEntitiesGeometry::FExportedGeometry& ExportedGeometry = EntitiesGeometry->GetOccurrenceExportedGeometry(Node);

	Node.MeshActors.Reset(ExportedGeometry.GetMeshCount());

	FString ComponentActorName = Node.GetActorName();

	
	for (int32 MeshIndex = 0; MeshIndex < ExportedGeometry.GetMeshCount(); ++MeshIndex)
	{
		FString MeshActorName = FString::Printf(TEXT("%ls_%d"), *ComponentActorName, MeshIndex + 1); // Count meshes/mesh actors from 1

		// Create a Datasmith mesh actor for the Datasmith mesh element.
		TSharedPtr<IDatasmithMeshActorElement> DMeshActorPtr = FDatasmithSceneFactory::CreateMeshActor(*MeshActorName);

		Node.MeshActors.Add(DMeshActorPtr);

		// Add the Datasmith actor component depth tag.
		// We use component depth + 1 to factor in the added Datasmith scene root once imported in Unreal.
		FString ComponentDepthTag = FString::Printf(TEXT("SU.DEPTH.%d"), Node.Depth + 1);
		DMeshActorPtr->AddTag(*ComponentDepthTag);

		// Add the Datasmith actor component definition GUID tag.
		FString DefinitionGUIDTag = FString::Printf(TEXT("SU.GUID.%ls"), *EntityDefinition->GetSketchupSourceGUID());
		DMeshActorPtr->AddTag(*DefinitionGUIDTag);

		// Add the Datasmith actor component instance path tag.
		FString InstancePathTag = ComponentActorName.Replace(TEXT("SU"), TEXT("SU.PATH.0")).Replace(TEXT("_"), TEXT("."));
		DMeshActorPtr->AddTag(*InstancePathTag);

		// ADD_TRACE_LINE(TEXT("Actor %ls: %ls %ls %ls"), *MeshActorLabel, *ComponentDepthTag, *DefinitionGUIDTag, *InstancePathTag);

		// Set the Datasmith mesh element used by the mesh actor.
		DMeshActorPtr->SetStaticMeshPathName(ExportedGeometry.GetMeshElementName(MeshIndex));
	}
}

void FNodeOccurence::UpdateVisibility(FExportContext& Context)
{
	if (bPropertiesInvalidated)
	{
		Entity.UpdateOccurrenceLayer(Context, *this);
	}

	if (bVisibilityInvalidated || bPropertiesInvalidated)
	{
		Entity.UpdateOccurrenceVisibility(Context, *this);
		bVisibilityInvalidated = false;
	}

	for (FNodeOccurence* ChildNode : Children)
	{
		ChildNode->UpdateVisibility(Context);
	}
}

void FNodeOccurence::UpdateTransformations(FExportContext& Context)
{
	if (bPropertiesInvalidated)
	{
		Entity.UpdateOccurrenceTransformation(Context, *this);
	}

	for (FNodeOccurence* ChildNode : Children)
	{
		ChildNode->UpdateTransformations(Context);
	}
}

void FNodeOccurence::Update(FExportContext& Context)
{
	// todo: Is it possible not to traverse whole scene when only part of it changes?
	// - one way is to collect all nodes that need to be updated
	// - the other - only topmost invalidated nodes. and them traverse from them only, not from the top. 
	//   E.g. when a node is invalidated - traverse its subtree to invalidate all the nodes below. Also when a node is invalidated check  
	//   its parent - if its not invalidated this means any ancestor is not invalidated. This way complexity would be O(n) where n is number of nodes that need update, not number of all nodes

	if (bMeshActorsInvalidated)
	{
		Entity.ResetOccurrenceActors(Context, *this);

		if (bVisible)
		{
			Entity.UpdateOccurrenceMeshActors(Context, *this);
		}
		bMeshActorsInvalidated = false;
	}

	if (bPropertiesInvalidated)
	{
		if (bVisible)
		{
			Entity.UpdateOccurrence(Context, *this);
		}

		bPropertiesInvalidated = false;
	}

	for (FNodeOccurence* ChildNode : Children)
	{
		ChildNode->Update(Context);
	}
}

void FNodeOccurence::InvalidateProperties()
{
	if (bPropertiesInvalidated)
	{
		// if node is invalidated no need to traverse further - it's already done
		return;
	}

	bPropertiesInvalidated = true;

	// todo: register invalidated?

	for (FNodeOccurence* Child : Children)
	{
		Child->InvalidateProperties();
	}
}

void FNodeOccurence::InvalidateMeshActors()
{
	bMeshActorsInvalidated = true;
}

FString FNodeOccurence::GetActorName()
{
	return DatasmithActorName;
}

FString FNodeOccurence::GetActorLabel()
{
	return DatasmithActorLabel;
}

void FNodeOccurence::RemoveOccurrence(FExportContext& Context)
{
	// RemoveOccurrence is called from Entity only(i.e. it doesn't remove occurrence from the Entity itself, it's done there)

	if (MaterialOverride)
	{
		MaterialOverride->UnregisterInstance(Context, this);
	}

	if (ParentNode)
	{
		ParentNode->Children.Remove(this);
	}

	// Usually child component instances are removed in proper order - children first. Nut grouping entities
	// has this weird behavior that containing component removed without cleaning its children. Test case:
	// Group an instance and some other entity(e.g. face) and then convert group to component
	// then converted group is removed without other events for its children
	// Probably this is because those 'children' are actually entities in the group's definition
	// And Group entity itself is just an instance of its definition(just like ComponentInstance)
	// So that Group definition content is not changed and that definition just receives another instance
	for (FNodeOccurence* Child : Children.Array())
	{
		Child->RemoveOccurrence(Context);
	}

	Entity.ResetOccurrenceActors(Context, *this);
	Entity.DeleteOccurrence(Context, this);
}

void FNodeOccurence::ResetMetadataElement(FExportContext& Context)
{
	// Create a Datasmith metadata element for the SketckUp component instance metadata definition.
	FString MetadataElementName = FString::Printf(TEXT("%ls_DATA"), DatasmithActorElement->GetName());
	
	if (!DatasmithMetadataElement.IsValid())
	{
		DatasmithMetadataElement = FDatasmithSceneFactory::CreateMetaData(*MetadataElementName);
		DatasmithMetadataElement->SetAssociatedElement(DatasmithActorElement);
		Context.DatasmithScene->AddMetaData(DatasmithMetadataElement);
	}
	else
	{
		DatasmithMetadataElement->SetName(*MetadataElementName);
	}
	DatasmithMetadataElement->SetLabel(*GetActorLabel());
	DatasmithMetadataElement->ResetProperties();
}

bool FNodeOccurence::SetVisibility(bool bValue)
{
	bool  bChanged = bVisible != bValue;
	bVisible = bValue;
	return bChanged;
}

void FNodeOccurence::RemoveDatasmithActorHierarchy(FExportContext& Context)
{
	if (!DatasmithActorElement)
	{
		// Hierarchy already removed(or wasn't created)
		return;
	}

	// Remove depth-first
	for (FNodeOccurence* ChildNode : Children)
	{
		ChildNode->RemoveDatasmithActorHierarchy(Context);
	}

	Entity.ResetOccurrenceActors(Context, *this);
}

void FNodeOccurence::ResetNodeActors(FExportContext& Context)
{
	FNodeOccurence& Node = *this;

	// Remove old mesh actors
	// todo: reuse old mesh actors (also can keep instances when removing due to say hidden)
	if (Node.DatasmithActorElement)
	{
		// Check if component used an actor to combine mesh and child nodes under it
		// todo: just add flag for code clearness?
		bool bHasActor = Node.MeshActors.IsEmpty() || (Node.DatasmithActorElement != Node.MeshActors[0]);

		if (bHasActor)  
		{
			// In this case detach all the children before removing actor from the parent/scene
			// note: DatasmithScene::RemoveActor has only two ways to remove children - relocating then to Scene root or deleting hierarchy

			int32 ChildCount = Node.DatasmithActorElement->GetChildrenCount();
			// Remove last child each time to optimize array elements relocation
			for(int32 ChildIndex = ChildCount-1; ChildIndex >= 0; --ChildIndex)
			{
				Node.DatasmithActorElement->RemoveChild(Node.DatasmithActorElement->GetChild(ChildIndex));
			}
		}

		if (const TSharedPtr<IDatasmithActorElement>& ParentActor = Node.DatasmithActorElement->GetParentActor())
		{
			ParentActor->RemoveChild(Node.DatasmithActorElement);
		}
		else
		{
			Context.DatasmithScene->RemoveActor(Node.DatasmithActorElement, EDatasmithActorRemovalRule::RemoveChildren);
		}
		Node.DatasmithActorElement.Reset();

		if (DatasmithMetadataElement)
		{
			Context.DatasmithScene->RemoveMetaData(DatasmithMetadataElement);
		}
		DatasmithMetadataElement.Reset();

	}
	Node.MeshActors.Reset();
}

FModelDefinition::FModelDefinition(SUModelRef InModel) : Model(InModel)
{
}

void FModelDefinition::Parse(FExportContext& Context)
{
	SUEntitiesRef EntitiesRef = SU_INVALID;
	// Retrieve the SketchUp model entities.
	SUModelGetEntities(Model, &EntitiesRef);
	Entities = Context.EntitiesObjects.AddEntities(*this, EntitiesRef);
}

void FModelDefinition::UpdateGeometry(FExportContext& Context)
{
	Entities->UpdateGeometry(Context, {Context.RootNode.Get()}, {});
}

void FModelDefinition::UpdateMetadata(FExportContext& Context)
{

}

void FModelDefinition::InvalidateInstancesGeometry(FExportContext& Context)
{
	Context.Model->InvalidateEntityGeometry();
}

void FModelDefinition::InvalidateInstancesMetadata(FExportContext& Context)
{
}

void FModelDefinition::FillOccurrenceActorMetadata(FNodeOccurence& Node)
{
}

FString FModelDefinition::GetSketchupSourceName()
{
	FString SketchupSourceName = SuGetString(SUModelGetName, Model);
	if (SketchupSourceName.IsEmpty())
	{
		SketchupSourceName = TEXT("SketchUp_Model");
	}
	return SketchupSourceName;
}

FString FModelDefinition::GetSketchupSourceId()
{
	return GetSketchupSourceGUID();
}

bool FModelDefinition::UpdateModel(FExportContext& Context)
{
	// SketchUp API has no notification of Geolocation change so retrieve it every time and compare to check if we need to set DirectLink update
	FVector GeolocationDatasmith = Context.DatasmithScene->GetGeolocation();

	FVector2d Geolocation(GeolocationDatasmith);
	FVector2d GeolocationNew = Geolocation;

	SULocationRef Location = SU_INVALID;
	if (SU_ERROR_NONE == SUModelGetLocation(Model, &Location))
	{
		double Latitude = 0;
		double Longitude = 0;
		SULocationGetLatLong(Location, &Latitude, &Longitude);

		GeolocationNew = FVector2d(Latitude, Longitude);
	}

	if ((Geolocation - GeolocationNew).IsNearlyZero(1e-10))
	{
		return false;
	}

	Context.DatasmithScene->SetGeolocationLatitude(GeolocationNew.X);
	Context.DatasmithScene->SetGeolocationLongitude(GeolocationNew.Y);
	return true;
}

FString FModelDefinition::GetSketchupSourceGUID()
{
	return TEXT("MODEL");
}

void FModelDefinition::AddInstance(FExportContext& Context, TSharedPtr<FComponentInstance> Instance)
{
	FNodeOccurence& ChildNode = Instance->CreateNodeOccurrence(Context, *Context.RootNode);
	Instance->ParseNode(Context, ChildNode);
}

void FModelDefinition::AddImage(FExportContext& Context, TSharedPtr<FImage> Image)
{
	Image->CreateNodeOccurrence(Context, *Context.RootNode);
}


FComponentDefinition::FComponentDefinition(
	SUComponentDefinitionRef InComponentDefinitionRef)
	: ComponentDefinitionRef(InComponentDefinitionRef)
{
}

FComponentDefinition::~FComponentDefinition()
{
}

void FComponentDefinition::Parse(FExportContext& Context)
{
	SUEntitiesRef EntitiesRef = SU_INVALID;
	// Retrieve the SketchUp component definition entities.
	SUComponentDefinitionGetEntities(ComponentDefinitionRef, &EntitiesRef); // we can ignore the returned SU_RESULT

	Entities = Context.EntitiesObjects.AddEntities(*this, EntitiesRef);

	// Get the component ID of the SketckUp component definition.
	SketchupSourceID = DatasmithSketchUpUtils::GetComponentID(ComponentDefinitionRef);

	// Retrieve the SketchUp component definition behavior in the rendering scene.
	SUComponentBehavior SComponentBehavior;
	SUComponentDefinitionGetBehavior(ComponentDefinitionRef, &SComponentBehavior); // we can ignore the returned SU_RESULT

	// Get whether or not the source SketchUp component behaves like a billboard.
	bSketchupSourceFaceCamera = SComponentBehavior.component_always_face_camera;
}

void FComponentInstance::SetupActor(FExportContext& Context, FNodeOccurence& Node)
{
	FComponentDefinition& EntityDefinition = *(FComponentDefinition *)GetDefinition();

	// Add the Datasmith actor component depth tag.
	// We use component depth + 1 to factor in the added Datasmith scene root once imported in Unreal.
	FString ComponentDepthTag = FString::Printf(TEXT("SU.DEPTH.%d"), Node.Depth);
	Node.DatasmithActorElement->AddTag(*ComponentDepthTag);

	// Add the Datasmith actor component definition GUID tag.
	FString DefinitionGUIDTag = FString::Printf(TEXT("SU.GUID.%ls"), *GetDefinition()->GetSketchupSourceGUID());
	Node.DatasmithActorElement->AddTag(*DefinitionGUIDTag);

	// Add the Datasmith actor component instance path tag.
	FString InstancePathTag = Node.GetActorName().Replace(TEXT("SU"), TEXT("SU.PATH.0")).Replace(TEXT("_"), TEXT("."));
	Node.DatasmithActorElement->AddTag(*InstancePathTag);

	// Add the Datasmith actor component instance face camera tag when required.
	if (EntityDefinition.bSketchupSourceFaceCamera)
	{
		Node.DatasmithActorElement->AddTag(TEXT("SU.BEHAVIOR.FaceCamera"));
	}

	if (Node.ParentNode->DatasmithActorElement)
	{
		Node.ParentNode->DatasmithActorElement->AddChild(Node.DatasmithActorElement);
	}
	else
	{
		Context.DatasmithScene->AddActor(Node.DatasmithActorElement);
	}
}

void FComponentDefinition::UpdateGeometry(FExportContext& Context)
{
	// Some occurrences geometry should have its transformation baked into exported mesh, when that transformation can't be converted to UE(i.e. shear transform)
	TArray<FNodeOccurence*> NodesToInstance;
	TArray<FNodeOccurence*> NodesToBake;

	for (FComponentInstance* Instance: Instances)
	{
		// todo: might add check to ComponentInstance visibility
		for (FNodeOccurence* NodeOccurence: Instance->Occurrences)
		{
			if (NodeOccurence->bVisible)
			{
				if (NodeOccurence->bTransformSupportedByUE)
				{
					NodesToInstance.Add(NodeOccurence);
				}
				else
				{
					NodesToBake.Add(NodeOccurence);
				}
			}
		}
	}

	Entities->UpdateGeometry(Context, NodesToInstance, NodesToBake);
}

void FComponentDefinition::UpdateMetadata(FExportContext& Context)
{
	ParsedMetadata = MakeUnique<FMetadata>(SUComponentDefinitionToEntity(ComponentDefinitionRef));;
}

void FComponentInstance::BuildNodeNames(FNodeOccurence& Node)
{
	// Get the SketckUp component instance persistent ID.
	int64 SketchupPersistentID = Node.Entity.GetPersistentId();
	Node.DatasmithActorName = FString::Printf(TEXT("%ls_%lld"), *Node.ParentNode->GetActorName(), SketchupPersistentID);

	FString EntityName = Node.Entity.GetEntityName();
	Node.DatasmithActorLabel = FDatasmithUtils::SanitizeObjectName(EntityName.IsEmpty() ? GetDefinition()->GetSketchupSourceName() : EntityName);
}

void FComponentDefinition::InvalidateInstancesGeometry(FExportContext& Context)
{
	// todo: keep all instances or encapsulate enumeration(duplicated) of FComponentInstance
	size_t InstanceCount = 0;
	SUComponentDefinitionGetNumInstances(ComponentDefinitionRef, &InstanceCount);

	TArray<SUComponentInstanceRef> InstanceRefs;
	InstanceRefs.Init(SU_INVALID, InstanceCount);
	SUComponentDefinitionGetInstances(ComponentDefinitionRef, InstanceCount, InstanceRefs.GetData(), &InstanceCount);
	InstanceRefs.SetNum(InstanceCount);

	for (const SUComponentInstanceRef& InstanceRef : InstanceRefs)
	{
		Context.ComponentInstances.InvalidateComponentInstanceGeometry(DatasmithSketchUpUtils::GetComponentInstanceID(InstanceRef));
	}
}

void FComponentDefinition::InvalidateInstancesMetadata(FExportContext& Context)
{
	// todo: keep all instances or incapsulate enumeration(duplicated) of FComponentInstance
	size_t InstanceCount = 0;
	SUComponentDefinitionGetNumInstances(ComponentDefinitionRef, &InstanceCount);

	TArray<SUComponentInstanceRef> InstanceRefs;
	InstanceRefs.Init(SU_INVALID, InstanceCount);
	SUComponentDefinitionGetInstances(ComponentDefinitionRef, InstanceCount, InstanceRefs.GetData(), &InstanceCount);
	InstanceRefs.SetNum(InstanceCount);

	for (const SUComponentInstanceRef& InstanceRef : InstanceRefs)
	{
		Context.ComponentInstances.InvalidateComponentInstanceMetadata(DatasmithSketchUpUtils::GetComponentInstanceID(InstanceRef));
	}
}

void FComponentDefinition::FillOccurrenceActorMetadata(FNodeOccurence& Node)
{
	if (ParsedMetadata)
	{
		ParsedMetadata->AddMetadata(Node.DatasmithMetadataElement);
	}
}

FString FComponentDefinition::GetSketchupSourceName()
{
	// Retrieve the SketchUp component definition name.
	return SuGetString(SUComponentDefinitionGetName, ComponentDefinitionRef);
}

FString FComponentDefinition::GetSketchupSourceId()
{
// Although SUEntityGetPersistentID implemented since SU 2017 it returns valid Id for ComponentDefinitions
// only since SU 2020.1 (even though SUEntityGetPersistentID docs states SUComponentDefinitionRef 'supported' from 2017)
// see https://github.com/SketchUp/api-issue-tracker/issues/314
#ifndef SKP_SDK_2019
	// Use Entity PersistentID - this one is persistent(between sessions)  for model file and doesn't change when definition is modified(e.g. geometry edited)
	int64_t EntityPid = 0;
	if (SUEntityGetPersistentID(SUComponentDefinitionToEntity(ComponentDefinitionRef), &EntityPid) == SU_ERROR_NONE)
	{
		if (ensure(EntityPid != 0))
		{
			return FString::Printf(TEXT("%llx"), EntityPid);
		}
	}
#endif

	return FMD5::HashAnsiString(*GetSketchupSourceGUID());
}

FString FComponentDefinition::GetSketchupSourceGUID()
{
	// Retrieve the SketchUp component definition IFC GUID.
	return SuGetString(SUComponentDefinitionGetGuid, ComponentDefinitionRef);
}

void FComponentDefinition::LinkComponentInstance(FComponentInstance* ComponentInstance)
{
	Instances.Add(ComponentInstance);
}

void FComponentDefinition::UnlinkComponentInstance(FComponentInstance* ComponentInstance)
{
	Instances.Remove(ComponentInstance);
}

void FComponentDefinition::RemoveComponentDefinition(FExportContext& Context)
{
	// Remove ComponentDefinition that doesn't have tracked instances 
	ensure(!Instances.Num());
	
	// todo: might better keep in the Definition's Entities all ComponentInstanceIDs of the tracked entities
	// this way we don't need to check whether we are tracking them (inside RemoveComponentInstance) 
	for (SUComponentInstanceRef ComponentInstanceRef : GetEntities().GetComponentInstances())
	{
		Context.ComponentInstances.RemoveComponentInstance(
			DatasmithSketchUpUtils::GetComponentID(ComponentDefinitionRef), 
			DatasmithSketchUpUtils::GetComponentInstanceID(ComponentInstanceRef));
	}

	for (SUGroupRef GroupRef : GetEntities().GetGroups())
	{
		Context.ComponentInstances.RemoveComponentInstance(
			DatasmithSketchUpUtils::GetComponentID(ComponentDefinitionRef),
			DatasmithSketchUpUtils::GetGroupID(GroupRef));
	}

	Context.Materials.UnregisterGeometry(GetEntities().EntitiesGeometry.Get());
	Context.EntitiesObjects.UnregisterEntities(GetEntities());
}

void FComponentDefinition::AddInstance(FExportContext& Context, TSharedPtr<FComponentInstance> Instance)
{
	for (FComponentInstance* ParentInstance : Instances)
	{
		for (FNodeOccurence* ParentOccurrence : ParentInstance->Occurrences)
		{
			FNodeOccurence& ChildNode = Instance->CreateNodeOccurrence(Context, *ParentOccurrence);
			Instance->ParseNode(Context, ChildNode);
		}
	}
}

void FComponentDefinition::AddImage(FExportContext& Context, TSharedPtr<FImage> Image)
{
	for (FComponentInstance* ParentInstance : Instances)
	{
		for (FNodeOccurence* ParentOccurrence : ParentInstance->Occurrences)
		{
			// todo: remove djupcacation with FComponentDefinition::AddInstance
			Image->CreateNodeOccurrence(Context, *ParentOccurrence);
		}
	}
}

void FEntityWithEntities::UpdateOccurrence(FExportContext& Context, FNodeOccurence& Node)
{
	if (Node.MaterialOverride)
	{
		Node.MaterialOverride->UnregisterInstance(Context, &Node);
	}

	FString EffectiveLayerName = SuGetString(SULayerGetName, Node.EffectiveLayerRef);

	FDefinition* EntityDefinition = GetDefinition();
	DatasmithSketchUp::FEntitiesGeometry& EntitiesGeometry = *EntityDefinition->GetEntities().EntitiesGeometry;

	// Set the effective inherited material ID.
	if (!GetAssignedMaterial(Node.InheritedMaterialID))
	{
		Node.InheritedMaterialID = Node.ParentNode->InheritedMaterialID;
	}

	FEntitiesGeometry::FExportedGeometry& ExportedGeometry = EntitiesGeometry.GetOccurrenceExportedGeometry(Node);

	FString MeshActorLabel = Node.GetActorLabel();
	// Update Datasmith Mesh Actors
	for (int32 MeshIndex = 0; MeshIndex < Node.MeshActors.Num(); ++MeshIndex)
	{
		const TSharedPtr<IDatasmithMeshActorElement>& MeshActor = Node.MeshActors[MeshIndex];
		MeshActor->SetLabel(*MeshActorLabel);
		MeshActor->SetLayer(*FDatasmithUtils::SanitizeObjectName(EffectiveLayerName));

		// Update Override(Inherited)  Material
		// todo: set inherited material only on mesh actors that have faces with default material, right now setting on every mesh, hot harmful but excessive
		if (ExportedGeometry.IsMeshUsingInheritedMaterial(MeshIndex))
		{
			Context.Materials.SetMeshActorOverrideMaterial(Node, EntitiesGeometry, MeshActor);
		}
	}
}

FNodeOccurence& FEntity::CreateNodeOccurrence(FExportContext& Context, FNodeOccurence& ParentNode)
{
	FNodeOccurence* Occurrence = new FNodeOccurence(&ParentNode, *this);
	ParentNode.Children.Add(Occurrence);
	Occurrences.Add(Occurrence);
	return *Occurrence;
}

void FEntity::DeleteOccurrence(FExportContext& Context, FNodeOccurence* Node)
{
	EntityOccurrenceVisible(Node, false);
	Occurrences.Remove(Node);
	delete Node;
}

void FEntity::RemoveOccurrences(FExportContext& Context)
{
	TArray<FNodeOccurence*> OccurencesCopy = Occurrences; // Copy RemoveOccurrence modifies the array
	for (FNodeOccurence* Occurrence : OccurencesCopy)
	{
		Occurrence->RemoveOccurrence(Context);
	}
}

void FEntity::UpdateEntityGeometry(FExportContext& Context)
{
	if (bGeometryInvalidated)
	{
		InvalidateOccurrencesGeometry(Context);
		bGeometryInvalidated = false;
	}
}

void FEntity::UpdateEntityProperties(FExportContext& Context)
{
	if (bPropertiesInvalidated)
	{
		// We can't just update Occurrence properties
		// When transform changes each node needs its parent transform to be already calculated 
		// So we postpone occurrence nodes updates until we do update with respect to hierarchy(top first)
		InvalidateOccurrencesProperties(Context);
		UpdateMetadata(Context);
		
		bPropertiesInvalidated = false;
	}
}

void FEntity::EntityOccurrenceVisible(FNodeOccurence* Node, bool bVisible)
{
	if (bVisible)
	{
		VisibleNodes.Add(Node);
	}
	else
	{
		if (VisibleNodes.Contains(Node))
		{
			VisibleNodes.Remove(Node);
		}
	}
}

void FEntity::SetParentDefinition(FExportContext& Context, FDefinition* InParent)
{
	if (!IsParentDefinition(InParent)) // Changing parent
	{
		// If we are re-parenting(i.e. entity was previously owned by another Definition - this happens
		// when say a ComponentInstance was selected in UI and "Make Group" was performed.
		if (Parent)
		{
			RemoveOccurrences(Context);
			Occurrences.Reset();  // Clear occurrences - RemoveOccurrences doesn't do it(not needed during ComponentInstance removal)
		}

		Parent = InParent;
	}
}

void FEntityWithEntities::EntityOccurrenceVisible(FNodeOccurence* Node, bool bVisible)
{
	Super::EntityOccurrenceVisible(Node, bVisible);

	GetDefinition()->EntityVisible(this, VisibleNodes.Num() > 0);
}

FComponentInstance::FComponentInstance(SUEntityRef InEntityRef, FComponentDefinition& InDefinition)
	: Super(InEntityRef)
	, Definition(InDefinition)
{}

FComponentInstance::~FComponentInstance()
{
}

FDefinition* FComponentInstance::GetDefinition()
{
	return &Definition;
}

bool FComponentInstance::GetAssignedMaterial(FMaterialIDType& MaterialId)
{
	SUComponentInstanceRef ComponentInstanceRef = GetComponentInstanceRef();
	SUMaterialRef MaterialRef = DatasmithSketchUpUtils::GetMaterial(ComponentInstanceRef);

	// Set the effective inherited material ID.
	if (SUIsValid(MaterialRef))
	{
		// Get the material ID of the SketckUp component instance material.
		MaterialId = DatasmithSketchUpUtils::GetMaterialID(MaterialRef);
		return true;
	}
	return false;
}

void FComponentInstance::UpdateOccurrenceTransformation(FExportContext& Context, FNodeOccurence& Node)
{
	// Compute the world transform of the SketchUp component instance.
	SUTransformation LocalTransform;
	SUComponentInstanceGetTransform(GetComponentInstanceRef(), &LocalTransform);

	SUTransformation WorldTransform;
	SUTransformationMultiply(&Node.ParentNode->WorldTransformSource, &LocalTransform, &WorldTransform);

	bool bTransformChanged = !DatasmithSketchUpUtils::CompareSUTransformations(WorldTransform, Node.WorldTransformSource);

	Node.WorldTransformSource = WorldTransform;
	Node.WorldTransform = WorldTransform;


	auto GetNodePath = [&Node]()
	{
		TArray<FString> NamePath;

		for (const FNodeOccurence* N = &Node; N; N = N->ParentNode)
		{
			NamePath.Insert(N->Entity.GetEntityLabel(), 0);
		}

		return FString::Join(NamePath, TEXT("_"));
	};

	FVector Translation{};
	FQuat Rotation{};
	FVector Scale{};
	FVector Shear{};

	bool bTransformSupportedByUE = true;
	if (DatasmithSketchUpUtils::DecomposeTransform(LocalTransform, Translation, Rotation, Scale, Shear))
	{
		if (!Shear.IsNearlyZero())
		{
			bTransformSupportedByUE = false;
			DatasmithSketchUpUtils::ToRuby::LogWarn(
				FString::Printf(TEXT("Entity '%s' has shear in local transform"), *GetNodePath()));
		}
	}
	else
	{
		DatasmithSketchUpUtils::ToRuby::LogWarn(
			FString::Printf(TEXT("Entity %s has zero scaling"), *GetNodePath()));
	}

	if (DatasmithSketchUpUtils::DecomposeTransform(Node.WorldTransform, Translation, Rotation, Scale, Shear))
	{
		
		if (!Shear.IsNearlyZero())
		{
			bTransformSupportedByUE = false;
			DatasmithSketchUpUtils::ToRuby::LogWarn(
				FString::Printf(TEXT("Entity %s has shear in world transform"), *GetNodePath()));
		}
		//  Non-uniform with children not supported as children might be rotated and this would skew them
		// todo: worth checking down the subtree for actual rotation present to support these edge cases without extra mesh export!
		else if (!Scale.IsUniform() && !Node.Children.IsEmpty())
		{
			bTransformSupportedByUE = false;
			DatasmithSketchUpUtils::ToRuby::LogWarn(
				FString::Printf(TEXT("Entity %s has non-uniform scaling in world transform"), *GetNodePath()));
		}
	}
	else
	{
		DatasmithSketchUpUtils::ToRuby::LogWarn(
			FString::Printf(TEXT("Entity %s has zero scaling"), *GetNodePath()));
	}

	bTransformSupportedByUE = bTransformSupportedByUE && Node.ParentNode->bTransformSupportedByUE;

	if (!bTransformSupportedByUE)
	{

		SUTransformation ActorTransform;
		SUTransformation MeshActorWorldTransform;
		SUTransformation BakeTransform;

		DatasmithSketchUpUtils::SplitTransform(Node.WorldTransformSource, ActorTransform, MeshActorWorldTransform, BakeTransform);

		Node.WorldTransform = ActorTransform;
		Node.MeshActorWorldTransform = MeshActorWorldTransform;
		Node.BakeTransform = BakeTransform;
	}

	// If node's transform is not supported by UE(therefore it was baked/needs baking into mesh)
	// and transform itself was changed this means that geometry should be re-exported(as exported geometry was baked with old transform)
	bool bNeedInvalidateBakedGeometry = 
		(static_cast<bool>(Node.bTransformSupportedByUE) != bTransformSupportedByUE)
		|| (!Node.bTransformSupportedByUE && bTransformChanged);

	Node.bTransformSupportedByUE = bTransformSupportedByUE;

	if (bNeedInvalidateBakedGeometry)
	{
		GetDefinition()->InvalidateDefinitionGeometry();
	}
}

void FComponentInstance::UpdateOccurrence(FExportContext& Context, FNodeOccurence& Node)
{
	if (FDefinition* EntityDefinition = GetDefinition())
	{
		BuildNodeNames(Node);
	}

	// Set the actor label used in the Unreal UI.
	Node.DatasmithActorElement->SetLabel(*Node.GetActorLabel());

	// Retrieve the SketchUp component instance effective layer name.
	FString SEffectiveLayerName;
	SEffectiveLayerName = SuGetString(SULayerGetName, Node.EffectiveLayerRef);

	// Set the Datasmith actor layer name.
	Node.DatasmithActorElement->SetLayer(*FDatasmithUtils::SanitizeObjectName(SEffectiveLayerName));

	// Set the Datasmith actor world transform.
	DatasmithSketchUpUtils::SetActorTransform(Node.DatasmithActorElement, Node.WorldTransform);

	Node.ResetMetadataElement(Context);// todo: can enable/disable metadata export by toggling this code
	FillOccurrenceActorMetadata(Node);
	
	// Update Datasmith Mesh Actors
	for (int32 MeshIndex = 0; MeshIndex < Node.MeshActors.Num(); ++MeshIndex)
	{
		const TSharedPtr<IDatasmithMeshActorElement>& MeshActor = Node.MeshActors[MeshIndex];

		if (Node.bTransformSupportedByUE)
		{
			// Set mesh actor transform after node transform
			MeshActor->SetScale(Node.DatasmithActorElement->GetScale());
			MeshActor->SetRotation(Node.DatasmithActorElement->GetRotation());
			MeshActor->SetTranslation(Node.DatasmithActorElement->GetTranslation());
		}
		else
		{
			DatasmithSketchUpUtils::SetActorTransform(MeshActor, Node.MeshActorWorldTransform);
		}
	}

	Super::UpdateOccurrence(Context, Node);
}

int64 FComponentInstance::GetPersistentId()
{
	SUComponentInstanceRef ComponentInstanceRef = GetComponentInstanceRef();
	return DatasmithSketchUpUtils::GetComponentPID(ComponentInstanceRef);
}

FString FComponentInstance::GetEntityName()
{
	SUComponentInstanceRef InSComponentInstanceRef = GetComponentInstanceRef();
	FString SComponentInstanceName;
	return SuGetString(SUComponentInstanceGetName, InSComponentInstanceRef);
}

FString FComponentInstance::GetEntityLabel()
{
	FString EntityName = GetEntityName();
	return EntityName.IsEmpty() ? GetDefinition()->GetSketchupSourceName() : EntityName;
}

void FComponentInstance::UpdateMetadata(FExportContext& Context)
{
	ParsedMetadata = MakeUnique<FMetadata>(SUComponentInstanceToEntity(GetComponentInstanceRef()));
}

void FComponentInstance::UpdateEntityProperties(FExportContext& Context)
{
	if (bPropertiesInvalidated)
	{
		// todo: update metadata here
	}
	
	FEntity::UpdateEntityProperties(Context);
}

void FComponentInstance::UpdateOccurrenceMeshActors(FExportContext& Context, FNodeOccurence& Node)
{
	BuildNodeNames(Node);

	FEntityWithEntities::UpdateOccurrenceMeshActors(Context, Node);

	if (Node.Children.IsEmpty() && !Node.MeshActors.IsEmpty()) // Don't make extra actor when geometry node has no children 
	{
		TSharedPtr<IDatasmithMeshActorElement> MeshActor = Node.MeshActors[0];

		Node.DatasmithActorElement = MeshActor;

		SetupActor(Context, Node);
	}
	else
	{
		Node.DatasmithActorElement = FDatasmithSceneFactory::CreateActor(*Node.GetActorName());
		SetupActor(Context, Node);

		for (TSharedPtr<IDatasmithMeshActorElement> MeshActor : Node.MeshActors)
		{
			Node.DatasmithActorElement->AddChild(MeshActor);
		}

		for (FNodeOccurence* Child: Node.Children)
		{
			if (Child->DatasmithActorElement)
			{
				Node.DatasmithActorElement->AddChild(Child->DatasmithActorElement);
			}
		}
	}

}

void FComponentInstance::ResetOccurrenceActors(FExportContext& Context, FNodeOccurence& Node)
{
	Node.ResetNodeActors(Context);
}

void FComponentInstance::ParseNode(FExportContext& Context, FNodeOccurence& Node)
{
	FDefinition* EntityDefinition = GetDefinition();

	if (!EntityDefinition)
	{
		return;
	}

	EntityDefinition->ParseNode(Context, Node);
}

void FComponentInstance::InvalidateOccurrencesGeometry(FExportContext& Context)
{
	for (FNodeOccurence* Node : Occurrences)
	{
		Node->InvalidateMeshActors();

		// Should invalidate transform to trigger transform update for mesh actors 
		// todo: can simplify this
		// - separate Transform invalidation from other properties? If it should give any improvement?
		// - or just update mesh actors transforms? we can't do it here though as transform can be invalidated by ancestors change later when occurrences are updated
		// - add another flag to invalidate just mesh actors properties and update them separately
		Node->InvalidateProperties();
	}
}

void FComponentInstance::InvalidateOccurrencesProperties(FExportContext& Context)
{
	// When ComponentInstance is modified we need to determine if its visibility might have changed foremost
	// because this determines whether corresponding node would exist in the Datasmith scene 
	// Two things affect this - Hidden instance flag and layer(tag):

	bool bNewHidden = false;
	SUDrawingElementRef DrawingElementRef = SUComponentInstanceToDrawingElement(GetComponentInstanceRef());
	SUDrawingElementGetHidden(DrawingElementRef, &bNewHidden);

	SUDrawingElementGetLayer(DrawingElementRef, &LayerRef);
	bool bNewLayerVisible = Context.Layers.IsLayerVisible(LayerRef);

	if (bHidden != bNewHidden || bLayerVisible != bNewLayerVisible)
	{
		bHidden = bNewHidden;
		bLayerVisible = bNewLayerVisible;
		for (FNodeOccurence* Node : Occurrences)
		{
			Node->bVisibilityInvalidated = true;
		}
	}

	for (FNodeOccurence* Node : Occurrences)
	{
		Node->InvalidateProperties();
	}
}

FComponentInstanceIDType FComponentInstance::GetComponentInstanceId()
{
	return DatasmithSketchUpUtils::GetComponentInstanceID(GetComponentInstanceRef());
}

SUComponentInstanceRef FComponentInstance::GetComponentInstanceRef()
{
	return SUComponentInstanceFromEntity(EntityRef);
}

void FComponentInstance::FillOccurrenceActorMetadata(FNodeOccurence& Node)
{
	if (!Node.DatasmithMetadataElement)
	{
		return;
	}

	//SUTransformation SComponentInstanceWorldTransform = DatasmithSketchUpUtils::GetComponentInstanceTransform(GetComponentInstanceRef(), Node.ParentNode->WorldTransform);
	//double Volume;
	//SUComponentInstanceComputeVolume(GetComponentInstanceRef(), &SComponentInstanceWorldTransform, &Volume);

	// Add original instance/component names to metadata
	TSharedPtr<IDatasmithKeyValueProperty> EntityName = FDatasmithSceneFactory::CreateKeyValueProperty(TEXT("Instance"));
	EntityName->SetValue(*GetEntityName());
	Node.DatasmithMetadataElement->AddProperty(EntityName);

	TSharedPtr<IDatasmithKeyValueProperty> DefinitionName = FDatasmithSceneFactory::CreateKeyValueProperty(TEXT("Definition"));
	DefinitionName->SetValue(*GetDefinition()->GetSketchupSourceName());
	Node.DatasmithMetadataElement->AddProperty(DefinitionName);

	TSharedPtr<IDatasmithKeyValueProperty> DefinitionIdName = FDatasmithSceneFactory::CreateKeyValueProperty(TEXT("DefinitionIdName"));
	DefinitionIdName->SetValue(*GetDefinition()->GetSketchupSourceId());
	Node.DatasmithMetadataElement->AddProperty(DefinitionIdName);

	// Add instance metadata
	if (ParsedMetadata)
	{
		ParsedMetadata->AddMetadata(Node.DatasmithMetadataElement);
	}

	// Add definition metadata
	GetDefinition()->FillOccurrenceActorMetadata(Node);
}


void FImageCollection::LayerModified(FEntityIDType LayerId)
{
	for (const TPair<FEntityIDType, TSharedPtr<FImage>>& IdImage : Images)
	{
		TSharedPtr<FImage> Image = IdImage.Value;
		if (SUIsValid(Image->LayerRef) && (LayerId == DatasmithSketchUpUtils::GetEntityID(SULayerToEntity(Image->LayerRef))))
		{
			Image->InvalidateEntityProperties();
		}
	}
}

void FComponentInstance::UpdateOccurrenceLayer(FExportContext& Context, FNodeOccurence& Node)
{
	Node.EffectiveLayerRef = DatasmithSketchUpUtils::GetEffectiveLayer(GetComponentInstanceRef(), Node.ParentNode->EffectiveLayerRef);
}

void FComponentInstance::UpdateOccurrenceVisibility(FExportContext& Context, FNodeOccurence& Node)
{
	bool bEffectiveLayerVisible = Context.Layers.IsLayerVisible(Node.EffectiveLayerRef);

	// Parent node, component instance and layer - all should be visible to have node visible
	bool bVisibilityChanged = Node.SetVisibility(Node.ParentNode->bVisible && !bHidden && bEffectiveLayerVisible);

	EntityOccurrenceVisible(&Node, Node.bVisible);

	if (Node.bVisible)
	{
		Node.InvalidateProperties();
		Node.InvalidateMeshActors();
	}
	else
	{
		// Making component instance occurrence invisible  needs to invalidate geometry export
		// for different reasons: this occurrence could have its own baked mesh, it could be a single used of an instanced mesh
		GetDefinition()->InvalidateDefinitionGeometry();
		Node.RemoveDatasmithActorHierarchy(Context);
	}

	for (FNodeOccurence* ChildNode : Node.Children)
	{
		// Invalidate Visibility for child nodes when parent's was changed
		// as visibility is hierarchical so children should update even
		// if they weren't invalidated directly
		ChildNode->bVisibilityInvalidated |= bVisibilityChanged;
	}
}

void FComponentInstance::RemoveComponentInstance(FExportContext& Context)
{
	Definition.EntityVisible(this, false);
	Definition.UnlinkComponentInstance(this);
	RemoveOccurrences(Context);

	// If there's no Instances of this removed ComponentInstance we need to stop tracking Definition's Entities
	// Details:
	// SketchUp api doesn't fire event for those child Entities although they are effectively removed from Model 
	// Sketchup.active_model.definitions.purge_unused will deallocate those dangling Entities leaving references invalid
	// Although SU API tries to notify about this but fails e.g. DefinitionObserver.onComponentInstanceRemoved/onEraseEntity passes already deleted Entity making this callback useless
	if (!Definition.Instances.Num())
	{
		Definition.RemoveComponentDefinition(Context);
	}
}

FModel::FModel(FModelDefinition& InDefinition) : Super(SU_INVALID), Definition(InDefinition)
{

}

FDefinition* FModel::GetDefinition()
{
	return &Definition;
}

bool FModel::GetAssignedMaterial(FMaterialIDType& MaterialId)
{
	MaterialId = FMaterial::INHERITED_MATERIAL_ID;
	return true;
}

int64 FModel::GetPersistentId()
{
	return 0;
}

FString FModel::GetEntityName()
{
	return "";
}
   
FString FModel::GetEntityLabel()
{
	return "";
}

void FModel::UpdateOccurrenceLayer(FExportContext& Context, FNodeOccurence&)
{
}

void FModel::InvalidateOccurrencesGeometry(FExportContext& Context)
{
	Context.RootNode->InvalidateMeshActors();
	Context.RootNode->InvalidateProperties();
}

void FModel::InvalidateOccurrencesProperties(FExportContext& Context)
{
	Context.RootNode->InvalidateProperties();
}

void FModel::UpdateOccurrenceVisibility(FExportContext& Context, FNodeOccurence& Node)
{
	Node.SetVisibility(true);

	EntityOccurrenceVisible(&Node, true);
}

void FModel::UpdateMetadata(FExportContext& Context)
{
}

void FModel::UpdateOccurrenceMeshActors(FExportContext& Context, FNodeOccurence& Node)
{
	FEntityWithEntities::UpdateOccurrenceMeshActors(Context, Node);

	for (TSharedPtr<IDatasmithMeshActorElement> MeshActor : Node.MeshActors)
	{
		Context.DatasmithScene->AddActor(MeshActor);
	}
}

void FModel::ResetOccurrenceActors(FExportContext& Context, FNodeOccurence& Node)
{
	// Model actors are MeshActors added to DatasmithScene root
	for (TSharedPtr<IDatasmithMeshActorElement> MeshActor : Node.MeshActors)
	{
		Context.DatasmithScene->RemoveActor(MeshActor, EDatasmithActorRemovalRule::RemoveChildren);
	}
}

FDefinition::FDefinition(): bMeshesAdded(false)
                            , bGeometryInvalidated(true)
                            , bPropertiesInvalidated(true)
{}

FDefinition::~FDefinition()
{}

void FDefinition::EntityVisible(FEntity* Entity, bool bVisible)
{
	if (bVisible)
	{
		VisibleEntities.Add(Entity);
	}
	else
	{
		if (VisibleEntities.Contains(Entity))
		{
			VisibleEntities.Remove(Entity);
		}
	}
}

void FDefinition::UpdateDefinition(FExportContext& Context)
{
	if (VisibleEntities.Num())
	{
		if (bGeometryInvalidated)
		{
			UpdateGeometry(Context);
			InvalidateInstancesGeometry(Context); // Make sure instances keep up with definition changes
			bMeshesAdded = false;

			bGeometryInvalidated = false;
		}

		if (bPropertiesInvalidated)
		{
			// Currently SketchUp has no Observer for Component Definition attributes.
			// So this code is only executed on export
			// todo: implement attributes sync once api is available
			UpdateMetadata(Context);
			InvalidateInstancesMetadata(Context); // Make sure instances keep up with definition changes

			bPropertiesInvalidated = false;
		}

		if (!bMeshesAdded)
		{
			GetEntities().AddMeshesToDatasmithScene(Context);
			bMeshesAdded = true;
		}
	}
	else
	{
		if (bMeshesAdded)
		{
			// Without references meshes will be cleaned from datasmith scene
			// bMeshesAdded = false; // todo: SceneCleanUp - do maintenance myself?
			GetEntities().RemoveMeshesFromDatasmithScene(Context);
			bMeshesAdded = false;
		}
	}
}
