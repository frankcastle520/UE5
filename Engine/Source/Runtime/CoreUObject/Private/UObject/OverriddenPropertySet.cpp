// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/OverriddenPropertySet.h"

#include "UObject/OverridableManager.h"
#include "Serialization/ArchiveSerializedPropertyChain.h"
#include "UObject/PropertyOptional.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectArchetypeHelper.h"
#include "UObject/UObjectThreadContext.h"

/*
 *************************************************************************************
 * Overridable serialization is experimental, not supported and use at your own risk *
 *************************************************************************************
 */

DEFINE_LOG_CATEGORY(LogOverridableObject);

//----------------------------------------------------------------------//
// FOverridableSerializationLogic
//----------------------------------------------------------------------//
thread_local bool FOverridableSerializationLogic::bUseOverridableSerialization = false;
thread_local FOverriddenPropertySet* FOverridableSerializationLogic::OverriddenProperties = nullptr;

EOverriddenPropertyOperation FOverridableSerializationLogic::GetOverriddenPropertyOperation(const FArchive& Ar, FProperty* Property /*= nullptr*/, uint8* DataPtr /*= nullptr*/, uint8* DefaultValue)
{
	checkf(bUseOverridableSerialization, TEXT("Nobody should use this method if it is not setup to use overridable serialization"));

	const FArchiveSerializedPropertyChain* CurrentPropertyChain = Ar.GetSerializedPropertyChain();
	const FProperty* CurrentProperty = Property ? Property : (CurrentPropertyChain ? CurrentPropertyChain->GetPropertyFromStack(0) : nullptr);
	if (CurrentProperty && CurrentProperty->HasAnyPropertyFlags(CPF_ExperimentalNeverOverriden))
	{
		return EOverriddenPropertyOperation::None;
	}

	const EOverriddenPropertyOperation OverriddenOperation = OverriddenProperties ? OverriddenProperties->GetOverriddenPropertyOperation(CurrentPropertyChain, Property) : EOverriddenPropertyOperation::None;
	if (OverriddenOperation != EOverriddenPropertyOperation::None)
	{
		return OverriddenOperation;
	}

	// It does not mean that if we have no record of an overriden operation that a subobject might have one, need to traverse all possible subobjects. 
	if (CurrentProperty)
	{
		if (CurrentProperty->HasAnyPropertyFlags(CPF_ExperimentalAlwaysOverriden))
		{
			return EOverriddenPropertyOperation::Replace;
		}

		// In the case of a CDO owning default value, we might need to serialize it to keep its value.
		if (OverriddenProperties && OverriddenProperties->IsCDOOwningProperty(*CurrentProperty))
		{
			// Only need serialize this value if it is different from the default property value
			if (!CurrentProperty->Identical(DataPtr, DefaultValue, Ar.GetPortFlags()))
			{
				return 	EOverriddenPropertyOperation::Replace;
			}
		}
	}
	
	return EOverriddenPropertyOperation::None;
}

//----------------------------------------------------------------------//
// FOverridableSerializationScope
//----------------------------------------------------------------------//
FEnableOverridableSerializationScope::FEnableOverridableSerializationScope(bool bEnableOverridableSerialization, FOverriddenPropertySet* OverriddenProperties)
{
	if (bEnableOverridableSerialization)
	{
		if (FOverridableSerializationLogic::IsEnabled())
		{
			bWasOverridableSerializationEnabled = true;
			SavedOverriddenProperties = FOverridableSerializationLogic::GetOverriddenProperties();
			FOverridableSerializationLogic::Disable();
		}
		FOverridableSerializationLogic::Enable(OverriddenProperties);
		bOverridableSerializationEnabled = true;
	}
}

FEnableOverridableSerializationScope::~FEnableOverridableSerializationScope()
{
	if (bOverridableSerializationEnabled)
	{
		FOverridableSerializationLogic::Disable();
		if (bWasOverridableSerializationEnabled)
		{
			FOverridableSerializationLogic::Enable(SavedOverriddenProperties);
		}
	}
}

FOverriddenPropertyNodeID::FOverriddenPropertyNodeID(const FProperty* Property)
	: Object(nullptr)
{
	if (Property)
	{
		// append typename to the end of the property ID
		UE::FPropertyTypeNameBuilder TypeNameBuilder;
#if WITH_EDITORONLY_DATA
		{
			// use property impersonation for SaveTypeName so that keys don't change when classes die
			FUObjectSerializeContext* SerializeContext = FUObjectThreadContext::Get().GetSerializeContext();
            TGuardValue<bool> ScopedImpersonateProperties(SerializeContext->bImpersonateProperties, true);
            Property->SaveTypeName(TypeNameBuilder);
		}
#endif
		UE::FPropertyTypeName TypeName = TypeNameBuilder.Build();
		TStringBuilder<256> StringBuilder;
		StringBuilder << Property->GetFName();
		StringBuilder << " - ";
		StringBuilder << TypeName;
		Path = FName(StringBuilder.ToView());
	}
}

FOverriddenPropertyNodeID FOverriddenPropertyNodeID::RootNodeId()
{
	FOverriddenPropertyNodeID Result;
	Result.Path = FName(TEXT("root"));
	return Result;
}

FOverriddenPropertyNodeID FOverriddenPropertyNodeID::FromMapKey(const FProperty* KeyProperty, const void* KeyData)
{
	if (const FObjectPropertyBase* KeyObjectProperty = CastField<FObjectPropertyBase>(KeyProperty))
	{
		if (const UObject* Object = KeyObjectProperty->GetObjectPropertyValue(KeyData))
		{
			return FOverriddenPropertyNodeID(*Object);
		}
	}
	else
	{
		FString KeyString;
		KeyProperty->ExportTextItem_Direct(KeyString, KeyData, /*DefaultValue*/nullptr, /*Parent*/nullptr, PPF_None);
		FOverriddenPropertyNodeID Result;
		Result.Path = FName(KeyString);
		return Result;
	}
		
	checkf(false, TEXT("This case is not handled"))
	return FOverriddenPropertyNodeID();
}

int32 FOverriddenPropertyNodeID::ToMapInternalIndex(FScriptMapHelper& MapHelper) const
{
	// Special case for object we didn't use the pointer to create the key
	if (const FObjectPropertyBase* KeyObjectProperty = CastField<FObjectPropertyBase>(MapHelper.KeyProp))
	{
		for (FScriptMapHelper::FIterator It(MapHelper); It; ++It) 
		{
			if (UObject* CurrentObject = KeyObjectProperty->GetObjectPropertyValue(MapHelper.GetKeyPtr(It)))
			{
				if ((*this) == FOverriddenPropertyNodeID(*CurrentObject))
				{
					return It.GetInternalIndex();
				}
			}
		}
	}
	else
	{
		// Default case, just import the text as key value for comparison
		void* TempKeyValueStorage = FMemory_Alloca(MapHelper.MapLayout.SetLayout.Size);
		MapHelper.KeyProp->InitializeValue(TempKeyValueStorage);

		FString KeyToFind(ToString());
		MapHelper.KeyProp->ImportText_Direct(*KeyToFind, TempKeyValueStorage, nullptr, PPF_None);

		const int32 InternalIndex = MapHelper.FindMapPairIndexFromHash(TempKeyValueStorage);

		MapHelper.KeyProp->DestroyValue(TempKeyValueStorage);

		return InternalIndex;
	}
	return INDEX_NONE;
}

//----------------------------------------------------------------------//
// FOverriddenPropertyNodeID
//----------------------------------------------------------------------//
void FOverriddenPropertyNodeID::HandleObjectsReInstantiated(const TMap<UObject*, UObject*>& Map)
{
	if (!Object)
	{
		return;
	}

	if (UObject*const* ReplacedObject = Map.Find(Object))
	{
		Object = *ReplacedObject;
	}
}


//----------------------------------------------------------------------//
// FOverriddenPropertySet
//----------------------------------------------------------------------//
FOverriddenPropertyNode& FOverriddenPropertySet::FindOrAddNode(FOverriddenPropertyNode& ParentNode, FOverriddenPropertyNodeID NodeID)
{
	FOverriddenPropertyNodeID& SubNodeID = ParentNode.SubPropertyNodeKeys.FindOrAdd(NodeID, FOverriddenPropertyNodeID());
	if (SubNodeID.IsValid())
	{
		FOverriddenPropertyNode* FoundNode = OverriddenPropertyNodes.Find(SubNodeID);
		checkf(FoundNode, TEXT("Expecting a node"));
		return *FoundNode;
	}

	// We can safely assume that the parent node is at least modified from now on
	if (ParentNode.Operation == EOverriddenPropertyOperation::None)
	{
		ParentNode.Operation = EOverriddenPropertyOperation::Modified;
	}

	// Not found add the node
	FStringBuilderBase SubPropertyKeyBuilder;
	SubPropertyKeyBuilder = *ParentNode.NodeID.ToString();
	SubPropertyKeyBuilder.Append(TEXT("."));
	SubPropertyKeyBuilder.Append(*NodeID.ToString());
	SubNodeID.Path = FName(SubPropertyKeyBuilder.ToString());
	SubNodeID.Object = NodeID.Object;
	const FSetElementId NewID = OverriddenPropertyNodes.Emplace(SubNodeID);
	return OverriddenPropertyNodes.Get(NewID);
}

EOverriddenPropertyOperation FOverriddenPropertySet::GetOverriddenPropertyOperation(const FOverriddenPropertyNode& ParentPropertyNode, FPropertyVisitorPath::Iterator PropertyIterator, bool* bOutInheritedOperation, const void* Data) const
{
	FOverridableManager& OverridableManager = FOverridableManager::Get();

	const void* SubValuePtr = Data;
	const FOverriddenPropertyNode* OverriddenPropertyNode = &ParentPropertyNode;
	int32 ArrayIndex = INDEX_NONE;
	while (PropertyIterator && (!OverriddenPropertyNode || OverriddenPropertyNode->Operation != EOverriddenPropertyOperation::Replace))
	{
		ArrayIndex = INDEX_NONE;

		const FProperty* CurrentProperty = PropertyIterator->Property;
		SubValuePtr = CurrentProperty->ContainerPtrToValuePtr<void>(SubValuePtr, 0); //@todo support static arrays

		const FOverriddenPropertyNode* CurrentOverriddenPropertyNode = nullptr;
		if (OverriddenPropertyNode)
		{
			if (const FOverriddenPropertyNodeID* CurrentPropKey = OverriddenPropertyNode->SubPropertyNodeKeys.Find(CurrentProperty))
			{
				CurrentOverriddenPropertyNode = OverriddenPropertyNodes.Find(*CurrentPropKey);
				checkf(CurrentOverriddenPropertyNode, TEXT("Expecting a node"));
			}
		}

		FPropertyVisitorPath::Iterator NextPropertyIterator = PropertyIterator+1;
		// Special handling for instanced subobjects 
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(CurrentProperty))
		{
			if (NextPropertyIterator)
			{
				// Forward any sub queries to the subobject
				if (UObject* SubObject = ObjectProperty->GetObjectPropertyValue(SubValuePtr))
				{
					// This should not be needed in the property grid, as it should already been called on the subobject.
					return OverridableManager.GetOverriddenPropertyOperation(*SubObject, NextPropertyIterator, bOutInheritedOperation);
				}
			}
		}
		// Special handling for array of instanced subobjects 
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(CurrentProperty))
		{
			ArrayIndex = PropertyIterator->Index;
			checkf(ArrayIndex == INDEX_NONE || PropertyIterator->PropertyInfo == EPropertyVisitorInfoType::ContainerIndex, TEXT("Expecting a container index"));

			// Only special case is instanced subobjects, otherwise we fallback to full array override
			checkf(ArrayProperty->Inner, TEXT("Expecting an inner type for Arrays"));
			if (const FObjectPropertyBase* InnerObjectProperty = ArrayProperty->Inner->HasAnyPropertyFlags(CPF_PersistentInstance) ? CastField<FObjectPropertyBase>(ArrayProperty->Inner) : nullptr)
			{
				FScriptArrayHelper ArrayHelper(ArrayProperty, SubValuePtr);
				if(ArrayHelper.IsValidIndex(ArrayIndex))
				{
					if (UObject* SubObject = InnerObjectProperty->GetObjectPropertyValue(ArrayHelper.GetElementPtr(ArrayIndex)))
					{
						if (NextPropertyIterator)
						{
							// Forward any sub queries to the subobject
							return OverridableManager.GetOverriddenPropertyOperation(*SubObject, NextPropertyIterator, bOutInheritedOperation);
						}
						else if(CurrentOverriddenPropertyNode)
						{
							// Caller wants to know about any override state on the reference of the subobject itself
							const FOverriddenPropertyNodeID  SubObjectID(*SubObject);
							if (const FOverriddenPropertyNodeID* CurrentPropKey = CurrentOverriddenPropertyNode->SubPropertyNodeKeys.Find(SubObjectID))
							{
								const FOverriddenPropertyNode* SubObjectOverriddenPropertyNode = OverriddenPropertyNodes.Find(*CurrentPropKey);
								checkf(SubObjectOverriddenPropertyNode, TEXT("Expecting a node"));
								if (bOutInheritedOperation)
								{
									*bOutInheritedOperation = false;
								}
								return SubObjectOverriddenPropertyNode->Operation;
							}
						}
					}
				}
			}
		}
		// Special handling for maps and values of instance subobjects
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(CurrentProperty))
		{
			ArrayIndex = PropertyIterator->Index;
			checkf(ArrayIndex == INDEX_NONE || PropertyIterator->PropertyInfo == EPropertyVisitorInfoType::ContainerIndex, TEXT("Expecting a container index"));

			checkf(MapProperty->ValueProp, TEXT("Expecting a value type for Maps"));
			FScriptMapHelper MapHelper(MapProperty, SubValuePtr);

			const int32 InternalMapIndex = ArrayIndex != INDEX_NONE ? MapHelper.FindInternalIndex(ArrayIndex) : INDEX_NONE;
			if(MapHelper.IsValidIndex(InternalMapIndex))
			{
				if (NextPropertyIterator)
				{
					// Forward any sub queries to the subobject
					if (const FObjectPropertyBase* ValueInstancedObjectProperty = MapProperty->ValueProp->HasAnyPropertyFlags(CPF_PersistentInstance) ? CastField<FObjectPropertyBase>(MapProperty->ValueProp) : nullptr)
					{
						if (UObject* ValueSubObject = ValueInstancedObjectProperty->GetObjectPropertyValue(MapHelper.GetValuePtr(InternalMapIndex)))
						{
							return OverridableManager.GetOverriddenPropertyOperation(*ValueSubObject, NextPropertyIterator, bOutInheritedOperation);
						}
					}
				}
				else if(CurrentOverriddenPropertyNode)
				{
					// Caller wants to know about any override state on the reference of the map pair itself
					checkf(MapProperty->KeyProp, TEXT("Expecting a key type for Maps"));
					FOverriddenPropertyNodeID OverriddenKeyID = FOverriddenPropertyNodeID::FromMapKey(MapProperty->KeyProp, MapHelper.GetKeyPtr(InternalMapIndex));

					if (const FOverriddenPropertyNodeID* CurrentPropKey = CurrentOverriddenPropertyNode->SubPropertyNodeKeys.Find(OverriddenKeyID))
					{
						const FOverriddenPropertyNode* SubObjectOverriddenPropertyNode = OverriddenPropertyNodes.Find(*CurrentPropKey);
						checkf(SubObjectOverriddenPropertyNode, TEXT("Expecting a node"));
						if (bOutInheritedOperation)
						{
							*bOutInheritedOperation = false;
						}
						return SubObjectOverriddenPropertyNode->Operation;
					}
				}
			}
		}

		OverriddenPropertyNode = CurrentOverriddenPropertyNode;
		++PropertyIterator;
	}

	if (bOutInheritedOperation)
	{
		*bOutInheritedOperation = PropertyIterator || ArrayIndex != INDEX_NONE;
	}
	return OverriddenPropertyNode ? OverriddenPropertyNode->Operation : EOverriddenPropertyOperation::None;
}

bool FOverriddenPropertySet::ClearOverriddenProperty(FOverriddenPropertyNode& ParentPropertyNode, FPropertyVisitorPath::Iterator PropertyIterator, const void* Data)
{
	FOverridableManager& OverridableManager = FOverridableManager::Get();

	bool bClearedOverrides = false;
	const void* SubValuePtr = Data;
	FOverriddenPropertyNode* OverriddenPropertyNode = &ParentPropertyNode;
	int32 ArrayIndex = INDEX_NONE;
	TArray<FOverriddenPropertyNodeID> PropertyNodeIDPath;
	PropertyNodeIDPath.Push(OverriddenPropertyNode->NodeID);
	while (PropertyIterator && (!OverriddenPropertyNode || OverriddenPropertyNode->Operation != EOverriddenPropertyOperation::Replace))
	{
		ArrayIndex = INDEX_NONE;

		const FProperty* CurrentProperty = PropertyIterator->Property;
		SubValuePtr = CurrentProperty->ContainerPtrToValuePtr<void>(SubValuePtr, 0); //@todo support static arrays

		FOverriddenPropertyNode* CurrentOverriddenPropertyNode = nullptr;
		if (OverriddenPropertyNode)
		{
			if (const FOverriddenPropertyNodeID* CurrentPropKey = OverriddenPropertyNode->SubPropertyNodeKeys.Find(CurrentProperty))
			{
				CurrentOverriddenPropertyNode = OverriddenPropertyNodes.Find(*CurrentPropKey);
				checkf(CurrentOverriddenPropertyNode, TEXT("Expecting a node"));
				PropertyNodeIDPath.Push(CurrentOverriddenPropertyNode->NodeID);
			}
		}

		// Special handling for instanced subobjects 
		FPropertyVisitorPath::Iterator NextPropertyIterator = PropertyIterator+1;
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(CurrentProperty))
		{
			if (UObject* SubObject = ObjectProperty->GetObjectPropertyValue(SubValuePtr))
			{
				if (NextPropertyIterator)
				{
					return OverridableManager.ClearOverriddenProperty(*SubObject, NextPropertyIterator);
				}
				else
				{
					OverridableManager.ClearOverrides(*SubObject);
					bClearedOverrides = true;
				}
			}
		}
		// Special handling for array of instanced subobjects 
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(CurrentProperty))
		{
			ArrayIndex = PropertyIterator->Index;
			checkf(ArrayIndex == INDEX_NONE || PropertyIterator->PropertyInfo == EPropertyVisitorInfoType::ContainerIndex, TEXT("Expecting a container index"));

			// Only special case is instanced subobjects, otherwise we fallback to full array override
			if (FObjectPropertyBase* InnerObjectProperty = CastField<FObjectPropertyBase>(ArrayProperty->Inner))
			{
				if (InnerObjectProperty->HasAnyPropertyFlags(CPF_PersistentInstance))
				{
					FScriptArrayHelper ArrayHelper(ArrayProperty, SubValuePtr);

					if(ArrayIndex == INDEX_NONE)
					{
						// This is a case of the entire array needs to be cleared
						// Need to loop through every sub object and clear them
						for (int i = 0; i < ArrayHelper.Num(); ++i)
						{
							if (UObject* SubObject = InnerObjectProperty->GetObjectPropertyValue(ArrayHelper.GetElementPtr(i)))
							{
								OverridableManager.ClearInstancedSubObjectOverrides(*Owner, *SubObject);
							}
						}
						bClearedOverrides = true;
					}
					else if(ArrayHelper.IsValidIndex(ArrayIndex))
					{
						if (UObject* SubObject = InnerObjectProperty->GetObjectPropertyValue(ArrayHelper.GetElementPtr(ArrayIndex)))
						{
							if (NextPropertyIterator)
							{
								return OverridableManager.ClearOverriddenProperty(*SubObject, NextPropertyIterator);
							}
							else if(CurrentOverriddenPropertyNode)
							{
								const FOverriddenPropertyNodeID  SubObjectID(*SubObject);
								FOverriddenPropertyNodeID CurrentPropKey;
								if (CurrentOverriddenPropertyNode->SubPropertyNodeKeys.RemoveAndCopyValue(SubObjectID, CurrentPropKey))
								{
									verifyf(OverriddenPropertyNodes.Remove(CurrentPropKey), TEXT("Expecting a node to be removed"));
									OverridableManager.ClearInstancedSubObjectOverrides(*Owner, *SubObject);
									return true;
								}
							}
						}
					}
				}
			}
		}
		// Special handling for maps and values of instance subobjects 
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(CurrentProperty))
		{
			ArrayIndex = PropertyIterator->Index;
			checkf(ArrayIndex == INDEX_NONE || PropertyIterator->PropertyInfo == EPropertyVisitorInfoType::ContainerIndex, TEXT("Expecting a container index"));

			FScriptMapHelper MapHelper(MapProperty, SubValuePtr);

			const int32 InternalMapIndex = ArrayIndex != INDEX_NONE ? MapHelper.FindInternalIndex(ArrayIndex) : INDEX_NONE;
			const FObjectPropertyBase* ValueInstancedObjectProperty = MapProperty->ValueProp->HasAnyPropertyFlags(CPF_PersistentInstance) ? CastField<FObjectPropertyBase>(MapProperty->ValueProp) : nullptr;

			// If there is a next node, it is probably because the map value is holding a instanced subobject and the user is changing value on it.
			// So forward the call to the instanced subobject
			if (NextPropertyIterator)
			{
				if(MapHelper.IsValidIndex(InternalMapIndex))
				{
					checkf(MapProperty->ValueProp, TEXT("Expecting a value type for Maps"));
					if (UObject* ValueSubObject = ValueInstancedObjectProperty ? ValueInstancedObjectProperty->GetObjectPropertyValue(MapHelper.GetValuePtr(InternalMapIndex)) : nullptr)
					{
						return OverridableManager.ClearOverriddenProperty(*ValueSubObject, NextPropertyIterator);
					}
				}
			}
			else if(InternalMapIndex == INDEX_NONE)
			{
				// Users want to clear all of the overrides on the array, but in the case of instanced subobject, we need to clear the overrides on them as well.
				if (ValueInstancedObjectProperty)
				{
					// This is a case of the entire array needs to be cleared
					// Need to loop through every sub object and clear them
					for (FScriptMapHelper::FIterator It(MapHelper); It; ++It)
					{
						if (UObject* ValueSubObject = ValueInstancedObjectProperty->GetObjectPropertyValue(MapHelper.GetValuePtr(It.GetInternalIndex())))
						{
							OverridableManager.ClearInstancedSubObjectOverrides(*Owner, *ValueSubObject);
						}
					}
				}
				bClearedOverrides = true;
			}
			else if (MapHelper.IsValidIndex(InternalMapIndex) && CurrentOverriddenPropertyNode)
			{
				checkf(MapProperty->KeyProp, TEXT("Expecting a key type for Maps"));
				FOverriddenPropertyNodeID OverriddenKeyID = FOverriddenPropertyNodeID::FromMapKey(MapProperty->KeyProp, MapHelper.GetKeyPtr(InternalMapIndex));

				FOverriddenPropertyNodeID CurrentPropKey;
				if (CurrentOverriddenPropertyNode->SubPropertyNodeKeys.RemoveAndCopyValue(OverriddenKeyID, CurrentPropKey))
				{
					verifyf(OverriddenPropertyNodes.Remove(CurrentPropKey), TEXT("Expecting a node to be removed"));

					if (UObject* ValueSubObject = ValueInstancedObjectProperty ? ValueInstancedObjectProperty->GetObjectPropertyValue(MapHelper.GetValuePtr(InternalMapIndex)) : nullptr)
					{
						// In the case of a instanced subobject, clear all the overrides on the subobject as well
						OverridableManager.ClearInstancedSubObjectOverrides(*Owner, *ValueSubObject);
					}

					return true;
				}
			}
		}

		OverriddenPropertyNode = CurrentOverriddenPropertyNode;
		++PropertyIterator;
	}

	auto CleanupClearedNodes = [this, &PropertyNodeIDPath]()
	{
		// Need to cleanup up the chain of property nodes if they endup empty
		FOverriddenPropertyNodeID ChildPropertyNodeID;
		while (FOverriddenPropertyNode* CurrentPropertyNode = !PropertyNodeIDPath.IsEmpty() ? OverriddenPropertyNodes.Find(PropertyNodeIDPath.Top()) : nullptr)
		{
			PropertyNodeIDPath.Pop();
			if (CurrentPropertyNode->SubPropertyNodeKeys.Num() > 1)
			{
				// Now need to remove the child from this node
				if (ChildPropertyNodeID.IsValid())
				{
					const FOverriddenPropertyNodeID* NodeToRemove = CurrentPropertyNode->SubPropertyNodeKeys.FindKey(ChildPropertyNodeID);
					checkf(NodeToRemove, TEXT("Expecting a node"));
					CurrentPropertyNode->SubPropertyNodeKeys.Remove(*NodeToRemove);

					verifyf(OverriddenPropertyNodes.Remove(ChildPropertyNodeID), TEXT("Expecting the node to be removed"));
				}
				break;
			}

			RemoveOverriddenSubProperties(*CurrentPropertyNode);
			ChildPropertyNodeID = CurrentPropertyNode->NodeID;
		}
	};

	if (PropertyIterator || OverriddenPropertyNode == nullptr)
	{
		if (bClearedOverrides)
		{
			CleanupClearedNodes();
		}

		return bClearedOverrides;
	}

	if (ArrayIndex != INDEX_NONE)
	{
		return false;
	}

	CleanupClearedNodes();
	return true;
}

void FOverriddenPropertySet::NotifyPropertyChange(FOverriddenPropertyNode* ParentPropertyNode, const EPropertyNotificationType Notification, FPropertyVisitorPath::Iterator PropertyIterator, const EPropertyChangeType::Type ChangeType, const void* Data, bool& bNeedsCleanup)
{
	checkf(IsValid(Owner), TEXT("Expecting a valid overridable owner"));

	FOverridableManager& OverridableManager = FOverridableManager::Get();
	if (!PropertyIterator)
	{
		if (ParentPropertyNode && Notification == EPropertyNotificationType::PostEdit)
		{
			// Sub-property overrides are not needed from now on, so clear them
			RemoveOverriddenSubProperties(*ParentPropertyNode);

			// Replacing this entire property
			ParentPropertyNode->Operation = EOverriddenPropertyOperation::Replace;

			// If we are overriding the root node, need to propagate the overrides to all instanced sub object
			const FOverriddenPropertyNode* RootNode = OverriddenPropertyNodes.Find(RootNodeID);
			checkf(RootNode, TEXT("Expecting to always have a "));
			if (RootNode == ParentPropertyNode)
			{
				OverridableManager.PropagateOverrideToInstancedSubObjects(*Owner);
			}
		}
		return;
	}

	const FProperty* Property = PropertyIterator->Property;
	checkf(Property, TEXT("Expecting a valid property"));

	const void* SubValuePtr = Property->ContainerPtrToValuePtr<void>(Data, 0); //@todo support static arrays

	FOverriddenPropertyNode* SubPropertyNode = nullptr;
	if (ParentPropertyNode)
	{
		FOverriddenPropertyNode& SubPropertyNodeRef = FindOrAddNode(*ParentPropertyNode, Property);
		SubPropertyNode = SubPropertyNodeRef.Operation != EOverriddenPropertyOperation::Replace ? &SubPropertyNodeRef : nullptr;
	}

	ON_SCOPE_EXIT
	{
		if (bNeedsCleanup && ParentPropertyNode && SubPropertyNode && SubPropertyNode->SubPropertyNodeKeys.IsEmpty())
		{
			FOverriddenPropertyNodeID RemovedNodeID;
			if (ParentPropertyNode->SubPropertyNodeKeys.RemoveAndCopyValue(Property, RemovedNodeID))
			{
				verifyf(OverriddenPropertyNodes.Remove(RemovedNodeID), TEXT("Expecting the node to be removed"));
			}
			if (ParentPropertyNode->Operation == EOverriddenPropertyOperation::Modified && ParentPropertyNode->SubPropertyNodeKeys.IsEmpty())
			{
				ParentPropertyNode->Operation = EOverriddenPropertyOperation::None;
			}
		}
	};

	FPropertyVisitorPath::Iterator NextPropertyIterator = PropertyIterator+1;
	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		// Only special case is instanced subobjects, otherwise we fallback to full array override
		if (FObjectPropertyBase* InnerObjectProperty = CastField<FObjectPropertyBase>(ArrayProperty->Inner))
		{
			if (InnerObjectProperty->HasAnyPropertyFlags(CPF_PersistentInstance))
			{
				FScriptArrayHelper ArrayHelper(ArrayProperty, SubValuePtr);
				int32 ArrayIndex = PropertyIterator->Index;
				checkf(ArrayIndex == INDEX_NONE || PropertyIterator->PropertyInfo == EPropertyVisitorInfoType::ContainerIndex, TEXT("Expecting a container index"));

				if (!NextPropertyIterator)
				{
					checkf(ArrayProperty->Inner, TEXT("Expecting an inner type for Arrays"));

					static TArray<uint8> SavedPreEditSubObjects;
					FScriptArrayHelper PreEditSubObjectsArrayHelper(ArrayProperty, &SavedPreEditSubObjects);
					if (Notification == EPropertyNotificationType::PreEdit)
					{
						PreEditSubObjectsArrayHelper.EmptyAndAddValues(ArrayHelper.Num());
						for(int32 i = 0; i < ArrayHelper.Num(); i++)
						{
							InnerObjectProperty->SetObjectPropertyValue(PreEditSubObjectsArrayHelper.GetElementPtr(i), InnerObjectProperty->GetObjectPropertyValue(ArrayHelper.GetElementPtr(i)));
						}
						return;
					}

					auto ArrayReplace = [&]
					{
						if (SubPropertyNode)
						{
							// Overriding all entry in the array
							SubPropertyNode->Operation = EOverriddenPropertyOperation::Replace;
						}

						// This is a case of the entire array is overridden
						// Need to loop through every sub object and setup them up as overridden
						for (int i = 0; i < ArrayHelper.Num(); ++i)
						{
							if (UObject* SubObject = InnerObjectProperty->GetObjectPropertyValue(ArrayHelper.GetElementPtr(i)))
							{
								if(SubPropertyNode)
								{
									const FOverriddenPropertyNodeID SubObjectID(*SubObject);
									FOverriddenPropertyNode& SubObjectNode = FindOrAddNode(*SubPropertyNode, SubObjectID);
									SubObjectNode.Operation = EOverriddenPropertyOperation::Replace;
								}

								OverridableManager.OverrideInstancedSubObject(*Owner, *SubObject);
							}
						}
					};

					auto ArrayAddImpl = [&]()
					{
						checkf(ArrayHelper.IsValidIndex(ArrayIndex), TEXT("ArrayAdd change type expected to have an valid index"));
						if (UObject* AddedSubObject = InnerObjectProperty->GetObjectPropertyValue(ArrayHelper.GetElementPtr(ArrayIndex)))
						{
							if(SubPropertyNode)
							{
								const FOverriddenPropertyNodeID  AddedSubObjectID(*AddedSubObject);
								FOverriddenPropertyNode& AddedSubObjectNode = FindOrAddNode(*SubPropertyNode, AddedSubObjectID);
								AddedSubObjectNode.Operation = EOverriddenPropertyOperation::Add;

								// Check if this could be a readd
								UObject* RemovedSubObjectArchetype = AddedSubObject->GetArchetype();
								if(RemovedSubObjectArchetype && !RemovedSubObjectArchetype->HasAnyFlags(RF_ClassDefaultObject))
								{
									const FOverriddenPropertyNodeID RemovedSubObjectID(*RemovedSubObjectArchetype);
									FOverriddenPropertyNodeID RemovedNodeID;
									if (SubPropertyNode->SubPropertyNodeKeys.RemoveAndCopyValue(RemovedSubObjectID, RemovedNodeID))
									{
										verifyf(OverriddenPropertyNodes.Remove(RemovedNodeID), TEXT("Expecting the node to be removed"));
										bNeedsCleanup = true;
									}
								}
							}
						}
					};

					auto ArrayRemoveImpl = [&]()
					{
						checkf(PreEditSubObjectsArrayHelper.IsValidIndex(ArrayIndex), TEXT("ArrayRemove change type expected to have an valid index"));
						if (UObject* RemovedSubObject = InnerObjectProperty->GetObjectPropertyValue(PreEditSubObjectsArrayHelper.GetElementPtr(ArrayIndex)))
						{
							if(SubPropertyNode)
							{
								UObject* RemovedSubObjectArchetype = RemovedSubObject->GetArchetype();
								const FOverriddenPropertyNodeID RemovedSubObjectID (!RemovedSubObjectArchetype || RemovedSubObjectArchetype->HasAnyFlags(RF_ClassDefaultObject) ? *RemovedSubObject : *RemovedSubObjectArchetype);
								FOverriddenPropertyNode& RemovedSubObjectNode = FindOrAddNode(*SubPropertyNode, RemovedSubObjectID);

								if (RemovedSubObjectNode.Operation == EOverriddenPropertyOperation::Add)
								{
									// An add then a remove becomes no opt
									FOverriddenPropertyNodeID RemovedNodeID;
									if (SubPropertyNode->SubPropertyNodeKeys.RemoveAndCopyValue(RemovedSubObjectID, RemovedNodeID))
									{
										verifyf(OverriddenPropertyNodes.Remove(RemovedNodeID), TEXT("Expecting the node to be removed"));
										bNeedsCleanup = true;
									}
								}
								else
								{
									RemovedSubObjectNode.Operation = EOverriddenPropertyOperation::Remove;
								}
							}
						}
					};

					// Only arrays flagged overridable logic can record deltas, for now just override entire array
					if (!ArrayProperty->HasAnyPropertyFlags(CPF_ExperimentalOverridableLogic))
					{
						if(ChangeType == EPropertyChangeType::Unspecified && ArrayIndex == INDEX_NONE)
						{
							// Overriding all entry in the array + override instanced sub obejects
							ArrayReplace();
						}
						else if (SubPropertyNode)
						{
							// Overriding all entry in the array
							SubPropertyNode->Operation = EOverriddenPropertyOperation::Replace;
						}
						return;
					}

					switch(ChangeType)
					{
					case EPropertyChangeType::ValueSet:
						checkf(ArrayIndex != INDEX_NONE, TEXT("ValueSet change type should have associated indexes"));
						// Intentional fall thru
					case EPropertyChangeType::Unspecified:
						{
							if (ArrayIndex != INDEX_NONE)
							{
								// Overriding a single entry in the array
								ArrayRemoveImpl();
								ArrayAddImpl();
							}
							else
							{
								ArrayReplace();
							}
							return;
						}
					case EPropertyChangeType::ArrayAdd:
						{
							ArrayAddImpl();
							return;
						}
					case EPropertyChangeType::ArrayRemove:
						{
							ArrayRemoveImpl();
							return;
						}
					case EPropertyChangeType::ArrayClear:
						{
							checkf(ArrayIndex == INDEX_NONE, TEXT("ArrayClear change type should not have associated indexes"));

							for (int i = 0; i < PreEditSubObjectsArrayHelper.Num(); ++i)
							{
								ArrayIndex = i;
								ArrayRemoveImpl();
							}
							return;
						}
					case EPropertyChangeType::ArrayMove:
						{
							UE_LOG(LogOverridableObject, Warning, TEXT("ArrayMove change type is not going to change anything as ordering of object isn't supported yet"));
							return;
						}
					default:
						{
							UE_LOG(LogOverridableObject, Warning, TEXT("Property change type is not supported will default to full array override"));
							break;
						}
					}
				}
				// Can only forward to subobject if we have a valid index
				else if (ArrayHelper.IsValidIndex(ArrayIndex))
				{
					if (UObject* SubObject = InnerObjectProperty->GetObjectPropertyValue(ArrayHelper.GetElementPtr(ArrayIndex)))
					{
						// This should not be needed in the property grid, as it should already been called on the subobject itself.
						OverridableManager.NotifyPropertyChange(Notification, *SubObject, NextPropertyIterator, ChangeType);
						return;
					}
				}
			}
		}
	}
	// @todo support set in the overridable serialization
	//else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	//{
	//	
	//}
	else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		// Special handling of instanced subobjects
		checkf(MapProperty->KeyProp, TEXT("Expecting a key type for Maps"));
		FObjectPropertyBase* KeyObjectProperty = CastField<FObjectPropertyBase>(MapProperty->KeyProp);

		// SubObjects
		checkf(!KeyObjectProperty || !MapProperty->KeyProp->HasAnyPropertyFlags(CPF_PersistentInstance) || CastField<FClassProperty>(MapProperty->KeyProp), TEXT("Keys as a instanced subobject is not supported yet"));

		checkf(MapProperty->ValueProp, TEXT("Expecting a value type for Maps"));
		FObjectPropertyBase* ValueInstancedObjectProperty = MapProperty->ValueProp->HasAnyPropertyFlags(CPF_PersistentInstance) ? CastField<FObjectPropertyBase>(MapProperty->ValueProp) : nullptr;

		FScriptMapHelper MapHelper(MapProperty, SubValuePtr);
		int32 LogicalMapIndex = PropertyIterator->Index;
		checkf(LogicalMapIndex == INDEX_NONE || PropertyIterator->PropertyInfo == EPropertyVisitorInfoType::ContainerIndex, TEXT("Expecting a container index type"));

		int32 InternalMapIndex = LogicalMapIndex != INDEX_NONE ? MapHelper.FindInternalIndex(LogicalMapIndex) : INDEX_NONE;
		if (!NextPropertyIterator)
		{
			static const FProperty* SavedProp = nullptr;
			static uint8* SavedPreEditMap = nullptr;

			auto FreePreEditMap = []()
			{
				if (SavedPreEditMap)
				{
					checkf(SavedProp, TEXT("Expecting a matching property to the allocated memory"));
					SavedProp->DestroyValue(SavedPreEditMap);
					FMemory::Free(SavedPreEditMap);
					SavedPreEditMap = nullptr;
					SavedProp = nullptr;
				}
			};

			if (Notification == EPropertyNotificationType::PreEdit)
			{
				FreePreEditMap();

				SavedPreEditMap = (uint8*)FMemory::Malloc(MapProperty->GetSize(), MapProperty->GetMinAlignment());
				MapProperty->InitializeValue(SavedPreEditMap);
				SavedProp = MapProperty;

				FScriptMapHelper PreEditMapHelper(MapProperty, SavedPreEditMap);
				PreEditMapHelper.EmptyValues();
				for (FScriptMapHelper::FIterator It(MapHelper); It; ++It)
				{
					PreEditMapHelper.AddPair(MapHelper.GetKeyPtr(It.GetInternalIndex()), MapHelper.GetValuePtr(It.GetInternalIndex()));
				}
				return;
			}

			checkf(SavedProp == MapProperty, TEXT("Expecting the same property as the pre edit flow"));
			FScriptMapHelper PreEditMapHelper(MapProperty, SavedPreEditMap);
			// The logical should map directly to the pre edit map internal index as we skipped all of the invalid entries
			int32 InternalPreEditMapIndex = LogicalMapIndex;

			ON_SCOPE_EXIT
			{
				FreePreEditMap();
			};

			auto MapReplace = [&]()
			{
				// Overriding a all entries in the map
				if (SubPropertyNode)
				{
					SubPropertyNode->Operation = EOverriddenPropertyOperation::Replace;
				}

				// This is a case of the entire array is overridden
				// Need to loop through every sub object and setup them up as overridden
				for (FScriptMapHelper::FIterator It(MapHelper); It; ++It)
				{
					if(SubPropertyNode)
					{
						FOverriddenPropertyNodeID OverriddenKeyID = FOverriddenPropertyNodeID::FromMapKey(MapProperty->KeyProp, MapHelper.GetKeyPtr(It.GetInternalIndex()));
						FOverriddenPropertyNode& OverriddenKeyNode = FindOrAddNode(*SubPropertyNode, OverriddenKeyID);
						OverriddenKeyNode.Operation = EOverriddenPropertyOperation::Replace;
					}

					// @todo support instanced object as a key in maps
					//if (UObject* KeySubObject = KeyInstancedObjectProperty ? KeyInstancedObjectProperty->GetObjectPropertyValue(MapHelper.GetKeyPtr(*It)) : nullptr)
					//{
					//	OverridableManager.OverrideInstancedSubObject(*Owner, *KeySubObject);
					//}
					if (UObject* ValueSubObject = ValueInstancedObjectProperty ? ValueInstancedObjectProperty->GetObjectPropertyValue(MapHelper.GetValuePtr(It.GetInternalIndex())) : nullptr)
					{
						OverridableManager.OverrideInstancedSubObject(*Owner, *ValueSubObject);
					}
				}
			};

			auto MapAddImpl = [&]()
			{
				checkf(MapHelper.IsValidIndex(InternalMapIndex), TEXT("ArrayAdd change type expected to have an valid index"));

				if(SubPropertyNode)
				{
					FOverriddenPropertyNodeID AddedKeyID = FOverriddenPropertyNodeID::FromMapKey(MapProperty->KeyProp, MapHelper.GetKeyPtr(InternalMapIndex));
					FOverriddenPropertyNode& AddedKeyNode = FindOrAddNode(*SubPropertyNode, AddedKeyID);
					AddedKeyNode.Operation = EOverriddenPropertyOperation::Add;
				}
			};

			auto MapRemoveImpl = [&]()
			{
				checkf(PreEditMapHelper.IsValidIndex(InternalPreEditMapIndex), TEXT("ArrayRemove change type expected to have an valid index"));

				if(SubPropertyNode)
				{
					FOverriddenPropertyNodeID RemovedKeyID = FOverriddenPropertyNodeID::FromMapKey(MapProperty->KeyProp, PreEditMapHelper.GetKeyPtr(InternalPreEditMapIndex));
					FOverriddenPropertyNode& RemovedKeyNode = FindOrAddNode(*SubPropertyNode, RemovedKeyID);
					if (RemovedKeyNode.Operation == EOverriddenPropertyOperation::Add)
					{
						// @Todo support remove/add/remove
						FOverriddenPropertyNodeID RemovedNodeID;
						if (SubPropertyNode->SubPropertyNodeKeys.RemoveAndCopyValue(RemovedKeyID, RemovedNodeID))
						{
							verifyf(OverriddenPropertyNodes.Remove(RemovedNodeID), TEXT("Expecting the node to be removed"));
							bNeedsCleanup = true;
						}
					}
					else
					{
						RemovedKeyNode.Operation = EOverriddenPropertyOperation::Remove;
					}
				}
			};

			// Only maps flagged overridable logic can be handled here
			if (!MapProperty->HasAnyPropertyFlags(CPF_ExperimentalOverridableLogic))
			{
				if (ChangeType == EPropertyChangeType::Unspecified && InternalMapIndex == INDEX_NONE)
				{
					// Overriding all entry in the array + override instanced sub obejects
					MapReplace();
				}
				else if(SubPropertyNode)
				{
					// Overriding all entry in the array
					SubPropertyNode->Operation = EOverriddenPropertyOperation::Replace;
				}
				return;
			}

			switch (ChangeType)
			{
			case EPropertyChangeType::ValueSet:
				checkf(LogicalMapIndex != INDEX_NONE, TEXT("ValueSet change type should have associated indexes"));
				// Intentional fall thru
			case EPropertyChangeType::Unspecified:
				{
					if(LogicalMapIndex != INDEX_NONE)
					{
						// Overriding a single entry in the map
						MapRemoveImpl();
						MapAddImpl();
					}
					else
					{
						MapReplace();
					}
					return;
				}
			case EPropertyChangeType::ArrayAdd:
				{
					MapAddImpl();
					return;
				}
			case EPropertyChangeType::ArrayRemove:
				{
					MapRemoveImpl();
					return;
				}
			case EPropertyChangeType::ArrayClear:
				{
					checkf(InternalPreEditMapIndex == INDEX_NONE, TEXT("ArrayClear change type should not have associated indexes"));

					for (FScriptMapHelper::FIterator It(PreEditMapHelper); It; ++It)
					{
						InternalPreEditMapIndex = It.GetInternalIndex();
						MapRemoveImpl();
					}
					return;
				}
			case EPropertyChangeType::ArrayMove:
				{
					UE_LOG(LogOverridableObject, Warning, TEXT("ArrayMove change type is not going to change anything as ordering of object isn't supported yet"));
					return;
				}
			default:
				{
					UE_LOG(LogOverridableObject, Warning, TEXT("Property change type is not supported will default to full array override"));
					break;
				}
			}
		}
		// Can only forward to subobject if we have a valid index
		else if (MapHelper.IsValidIndex(InternalMapIndex))
		{
			// @todo support instanced object as a key in maps
			//if (UObject* SubObject = KeyInstancedObjectProperty ? KeyInstancedObjectProperty->GetObjectPropertyValue(MapHelper.GetValuePtr(InternalMapIndex)) : nullptr)
			//{
			//	// This should not be needed in the property grid, as it should already been called on the subobject.
			//	OverridableManager.NotifyPropertyChange(Notification, *SubObject, NextPropertyIterator, ChangeType);
			//	return;
			//}

			if (UObject* SubObject = ValueInstancedObjectProperty ? ValueInstancedObjectProperty->GetObjectPropertyValue(MapHelper.GetValuePtr(InternalMapIndex)) : nullptr)
			{
				// This should not be needed in the property grid, as it should already been called on the subobject.
				OverridableManager.NotifyPropertyChange(Notification, *SubObject, NextPropertyIterator, ChangeType);
				return;
			}
		}
	}
	else if (Property->IsA<FStructProperty>())
	{
		if (!NextPropertyIterator)
		{
			if (Notification == EPropertyNotificationType::PostEdit && SubPropertyNode)
			{
				SubPropertyNode->Operation = EOverriddenPropertyOperation::Replace;
			}
		}
		else
		{
			NotifyPropertyChange(SubPropertyNode, Notification, NextPropertyIterator, ChangeType, SubValuePtr, bNeedsCleanup);
		}
		return;
	}
	else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		if (!NextPropertyIterator)
		{
			if (Notification == EPropertyNotificationType::PostEdit && SubPropertyNode)
			{
				SubPropertyNode->Operation = EOverriddenPropertyOperation::Replace;
			}
		}
		else if (UObject* SubObject = ObjectProperty->GetObjectPropertyValue(SubValuePtr))
		{
			// This should not be needed in the property grid, as it should already been called on the subobject.
			OverridableManager.NotifyPropertyChange(Notification, *SubObject, NextPropertyIterator, ChangeType);
		}
		return;
	}
	else if (const FOptionalProperty* OptionalProperty = CastField<FOptionalProperty>(Property))
	{
		if (!NextPropertyIterator)
		{
			if (Notification == EPropertyNotificationType::PostEdit && SubPropertyNode)
			{
				SubPropertyNode->Operation = EOverriddenPropertyOperation::Replace;
			}
		}
		else if (OptionalProperty->IsSet(Data))
		{
			NotifyPropertyChange(SubPropertyNode, Notification, NextPropertyIterator, ChangeType, OptionalProperty->GetValuePointerForRead(SubValuePtr), bNeedsCleanup);
		}
		return;
	}

	UE_CLOG(NextPropertyIterator, LogOverridableObject, Warning, TEXT("Unsupported property type(%s), fallback to overriding entire property"), *Property->GetName());
	if (Notification == EPropertyNotificationType::PostEdit)
	{
		if (SubPropertyNode)
		{
			// Replacing this entire property
			SubPropertyNode->Operation = EOverriddenPropertyOperation::Replace;
		}
	}
}

void FOverriddenPropertySet::RemoveOverriddenSubProperties(FOverriddenPropertyNode& PropertyNode)
{
	for (const auto& Pair : PropertyNode.SubPropertyNodeKeys)
	{
		FOverriddenPropertyNode* RemovedPropertyNode = OverriddenPropertyNodes.Find(Pair.Value);
		checkf(RemovedPropertyNode, TEXT("Expecting a node"));
		RemoveOverriddenSubProperties(*RemovedPropertyNode);
		verifyf(OverriddenPropertyNodes.Remove(Pair.Value), TEXT("Expecting the node to be removed"));
	}
	PropertyNode.Operation = EOverriddenPropertyOperation::None;
	PropertyNode.SubPropertyNodeKeys.Empty();
}

EOverriddenPropertyOperation FOverriddenPropertySet::GetOverriddenPropertyOperation(FPropertyVisitorPath::Iterator PropertyIterator, bool* bOutInheritedOperation /*= nullptr*/) const
{
	if (const FOverriddenPropertyNode* RootNode = OverriddenPropertyNodes.Find(RootNodeID))
	{
		return GetOverriddenPropertyOperation(*RootNode, PropertyIterator, bOutInheritedOperation, Owner);
	}
	return EOverriddenPropertyOperation::None;
}

bool FOverriddenPropertySet::ClearOverriddenProperty(FPropertyVisitorPath::Iterator PropertyIterator)
{
	if (FOverriddenPropertyNode* RootNode = OverriddenPropertyNodes.Find(RootNodeID))
	{
		return ClearOverriddenProperty(*RootNode, PropertyIterator, Owner);
	}
	return true;
}

void FOverriddenPropertySet::OverrideProperty(FPropertyVisitorPath::Iterator PropertyIterator, const void* Data)
{
	FOverriddenPropertyNode& RootPropertyNode = OverriddenPropertyNodes.FindOrAdd(RootNodeID);
	bool bNeedsCleanup = false;
	NotifyPropertyChange(&RootPropertyNode, EPropertyNotificationType::PreEdit, PropertyIterator, EPropertyChangeType::Unspecified, Data, bNeedsCleanup);
	NotifyPropertyChange(&RootPropertyNode, EPropertyNotificationType::PostEdit, PropertyIterator, EPropertyChangeType::Unspecified, Data, bNeedsCleanup);
}

void FOverriddenPropertySet::NotifyPropertyChange(const EPropertyNotificationType Notification, FPropertyVisitorPath::Iterator PropertyIterator, const EPropertyChangeType::Type ChangeType, const void* Data)
{
	bool bNeedsCleanup = false;
	NotifyPropertyChange(&OverriddenPropertyNodes.FindOrAdd(RootNodeID), Notification, PropertyIterator, ChangeType, Data, bNeedsCleanup);
}

EOverriddenPropertyOperation FOverriddenPropertySet::GetOverriddenPropertyOperation(const FArchiveSerializedPropertyChain* CurrentPropertyChain, FProperty* Property) const
{
	if (const FOverriddenPropertyNode* RootNode = OverriddenPropertyNodes.Find(RootNodeID))
	{
		return GetOverriddenPropertyOperation(*RootNode, CurrentPropertyChain, Property);
	}
	return EOverriddenPropertyOperation::None;
}

FOverriddenPropertyNode* FOverriddenPropertySet::SetOverriddenPropertyOperation(EOverriddenPropertyOperation Operation, const FArchiveSerializedPropertyChain* CurrentPropertyChain, FProperty* Property)
{
	return SetOverriddenPropertyOperation(Operation, OverriddenPropertyNodes.FindOrAdd(RootNodeID), CurrentPropertyChain, Property);
}

const FOverriddenPropertyNode* FOverriddenPropertySet::GetOverriddenPropertyNode(const FArchiveSerializedPropertyChain* CurrentPropertyChain) const
{
	if (const FOverriddenPropertyNode* RootNode = OverriddenPropertyNodes.Find(RootNodeID))
	{
		return GetOverriddenPropertyNode(*RootNode, CurrentPropertyChain);
	}
	return nullptr;
}

EOverriddenPropertyOperation FOverriddenPropertySet::GetOverriddenPropertyOperation(const FOverriddenPropertyNode& ParentPropertyNode, const FArchiveSerializedPropertyChain* CurrentPropertyChain, FProperty* Property) const
{
	// No need to look further
	// if it is the entire property is replaced or
	// if it is the FOverriddenPropertySet struct which is always Overridden
	if (ParentPropertyNode.Operation == EOverriddenPropertyOperation::Replace)
	{
		return EOverriddenPropertyOperation::Replace;
	}

	// @Todo optimize find a way to not have to copy the property chain here.
	FArchiveSerializedPropertyChain PropertyChain(CurrentPropertyChain ? *CurrentPropertyChain : FArchiveSerializedPropertyChain());
	if(Property)
	{
		PropertyChain.PushProperty(Property, Property->IsEditorOnlyProperty());
	}

	TArray<class FProperty*, TInlineAllocator<8>>::TConstIterator PropertyIterator = PropertyChain.GetRootIterator();
	const FOverriddenPropertyNode* OverriddenPropertyNode = &ParentPropertyNode;
	while (PropertyIterator && OverriddenPropertyNode && OverriddenPropertyNode->Operation != EOverriddenPropertyOperation::Replace)
	{
		const FProperty* CurrentProperty = (*PropertyIterator);
		if (const FOverriddenPropertyNodeID* CurrentPropKey = OverriddenPropertyNode->SubPropertyNodeKeys.Find(CurrentProperty))
		{
			OverriddenPropertyNode = OverriddenPropertyNodes.Find(*CurrentPropKey);
			checkf(OverriddenPropertyNode, TEXT("Expecting a node"));
		}
		else
		{
			OverriddenPropertyNode = nullptr;
			break;
		}
		++PropertyIterator;
	}

	return OverriddenPropertyNode ? OverriddenPropertyNode->Operation : EOverriddenPropertyOperation::None;
}

FOverriddenPropertyNode* FOverriddenPropertySet::SetOverriddenPropertyOperation(EOverriddenPropertyOperation Operation, FOverriddenPropertyNode& ParentPropertyNode, const FArchiveSerializedPropertyChain* CurrentPropertyChain, FProperty* Property)
{
	// No need to look further
	// if it is the entire property is replaced or
	// if it is the FOverriddenPropertySet struct which is always Overridden
	if (ParentPropertyNode.Operation == EOverriddenPropertyOperation::Replace)
	{
		return nullptr;
	}

	// @Todo optimize find a way to not have to copy the property chain here.
	FArchiveSerializedPropertyChain PropertyChain(CurrentPropertyChain ? *CurrentPropertyChain : FArchiveSerializedPropertyChain());
	if (Property)
	{
		PropertyChain.PushProperty(Property, Property->IsEditorOnlyProperty());
	}

	TArray<class FProperty*, TInlineAllocator<8>>::TConstIterator PropertyIterator = PropertyChain.GetRootIterator();
	FOverriddenPropertyNode* OverriddenPropertyNode = &ParentPropertyNode;
	while (PropertyIterator && OverriddenPropertyNode->Operation != EOverriddenPropertyOperation::Replace)
	{
		const FProperty* CurrentProperty = (*PropertyIterator);
		OverriddenPropertyNode = &FindOrAddNode(*OverriddenPropertyNode, CurrentProperty);
		++PropertyIterator;
	}

	// Might have stop before as one of the parent property was completely replaced.
	if (!PropertyIterator)
	{
		OverriddenPropertyNode->Operation = Operation;
		return OverriddenPropertyNode;
	}

	return nullptr;
}

EOverriddenPropertyOperation FOverriddenPropertySet::GetSubPropertyOperation(FOverriddenPropertyNodeID NodeID) const
{
	const FOverriddenPropertyNode* OverriddenPropertyNode = OverriddenPropertyNodes.Find(NodeID);
	return OverriddenPropertyNode ? OverriddenPropertyNode->Operation : EOverriddenPropertyOperation::None;
}

FOverriddenPropertyNode* FOverriddenPropertySet::SetSubPropertyOperation(EOverriddenPropertyOperation Operation, FOverriddenPropertyNode& Node, FOverriddenPropertyNodeID NodeID)
{
	FOverriddenPropertyNode& OverriddenPropertyNode = FindOrAddNode(Node, NodeID);
	OverriddenPropertyNode.Operation = Operation;
	return &OverriddenPropertyNode;
}

bool FOverriddenPropertySet::IsCDOOwningProperty(const FProperty& Property) const
{
	checkf(Owner, TEXT("Expecting a valid overridable owner"));
	if (!Owner->HasAnyFlags(RF_ClassDefaultObject))
	{
		return false;
	}

	// We need to serialize only if the property owner is the current CDO class
	// Otherwise on derived class, this is done in parent CDO or it should be explicitly overridden if it is different than the parent value
	// This is sort of like saying it overrides the default property initialization value.
	return Property.GetOwnerClass() == Owner->GetClass();
}

void FOverriddenPropertySet::Reset()
{
	OverriddenPropertyNodes.Reset();
}

void FOverriddenPropertySet::HandleObjectsReInstantiated(const TMap<UObject*, UObject*>& Map)
{
#if WITH_EDITOR
	// When their is a cached archetype, it is an indicator this object is about to be replaced
	// So no need to replace any ptr, otherwise we might not be able to reconstitute the right information
	if (FEditorCacheArchetypeManager::Get().GetCachedArchetype(Owner))
	{
		return;
	}
#endif // WITH_EDITOR

	for (FOverriddenPropertyNode& Node : OverriddenPropertyNodes)
	{
		Node.NodeID.HandleObjectsReInstantiated(Map);
		for (auto& Pair : Node.SubPropertyNodeKeys)
		{
			Pair.Key.HandleObjectsReInstantiated(Map);
			Pair.Value.HandleObjectsReInstantiated(Map);
		}
	}
}

const FOverriddenPropertyNode* FOverriddenPropertySet::GetOverriddenPropertyNode(const FOverriddenPropertyNode& ParentPropertyNode, const FArchiveSerializedPropertyChain* CurrentPropertyChain) const
{
	if (!CurrentPropertyChain)
	{
		return &ParentPropertyNode;
	}

	TArray<class FProperty*, TInlineAllocator<8>>::TConstIterator PropertyIterator = CurrentPropertyChain->GetRootIterator();
	const FOverriddenPropertyNode* OverriddenPropertyNode = &ParentPropertyNode;
	while (PropertyIterator && OverriddenPropertyNode)
	{
		const FProperty* CurrentProperty = (*PropertyIterator);
		if (const FOverriddenPropertyNodeID* CurrentPropKey = OverriddenPropertyNode->SubPropertyNodeKeys.Find(CurrentProperty))
		{
			OverriddenPropertyNode = OverriddenPropertyNodes.Find(*CurrentPropKey);
			checkf(OverriddenPropertyNode, TEXT("Expecting a node"));
		}
		else
		{
			OverriddenPropertyNode = nullptr;
			break;
		}
		++PropertyIterator;
	}

	return OverriddenPropertyNode;
}