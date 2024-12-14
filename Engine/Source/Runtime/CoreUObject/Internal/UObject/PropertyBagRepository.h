// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Map.h"
#include "HAL/CriticalSection.h"
#include "UObject/GCObject.h"
#include "UObject/PropertyTypeName.h"
#include "Templates/FunctionFwd.h"

class UObject;
class FMemoryArchive;

namespace UE
{

#if WITH_EDITORONLY_DATA

class FPropertyPathNameTree;
class FUnknownEnumNames;

// Singleton class tracking property bag association with objects
class FPropertyBagRepository : public FGCObject
{
	struct FPropertyBagAssociationData
	{
		void Destroy();

		FPropertyPathNameTree* Tree = nullptr;
		FUnknownEnumNames* EnumNames = nullptr;
		TObjectPtr<UObject> InstanceDataObject = nullptr;
		bool bNeedsFixup = false;
	};
	// TODO: Make private throughout and extend access permissions here or in wrapper classes? Don't want engine code modifying bags outside of serializers and details panels.
	//friend UObjectBase;
	//friend UStruct;
	friend struct FScopedInstanceDataObjectLoad;

private:
	friend class FPropertyBagRepositoryLock;
	mutable FCriticalSection CriticalSection;

	// Lifetimes/ownership:
	// Managed within UObjectBase and synced with object lifetime. The repo tracks pointers to bags, not the bags themselves.

	/** Map of objects/subobjects to their top level property bag. */
	// TODO: Currently will only exist in editor world, but could tracking per world make some sense for teardown in future? We're relying on object destruction to occur properly to free these up. 
	TMap<const UObject*, FPropertyBagAssociationData> AssociatedData;
	
	TMap<const UObject*, const UObject*> InstanceDataObjectToOwner;

	// used to make sure IDOs don't have name overlap
	TMap<const UObject*, TObjectPtr<UObject>> Namespaces;

	FPropertyBagRepository() = default;

public:
	FPropertyBagRepository(const FPropertyBagRepository &) = delete;
	FPropertyBagRepository& operator=(const FPropertyBagRepository&) = delete;
	
	// Singleton accessor
	static COREUOBJECT_API FPropertyBagRepository& Get();

	// Reclaim space - TODO: Hook up to GC.
	void ShrinkMaps();

	/**
	 * Finds or creates a property path name tree to collect unknown property paths within the owner.
	 */
	FPropertyPathNameTree* FindOrCreateUnknownPropertyTree(const UObject* Owner);

	/**
	 * Adds an unknown enum name to the names tracked for an object.
	 *
	 * @param Owner			The owner to associate the unknown name with.
	 * @param Enum			The enum associated with the property being serialized. May be null or the wrong type.
	 * @param EnumTypeName	The type name of the enum containing the unknown name.
	 * @param EnumValueName	The unknown name to track.
	 */
	void AddUnknownEnumName(const UObject* Owner, const UEnum* Enum, FPropertyTypeName EnumTypeName, FName EnumValueName);

	/**
	 * Finds tracked unknown enum names associated with the object.
	 *
	 * @param Owner			The owner associated with the unknown names to find.
	 * @param EnumTypeName	The type name of the enum containing the unknown names to find.
	 * @param OutNames		Array to assign the unknown names to. Empty on return if no names are found.
	 * @param bOutHasFlags	Assigned to true if the enum is known to have flags, otherwise false.
	 */
	void FindUnknownEnumNames(const UObject* Owner, FPropertyTypeName EnumTypeName, TArray<FName>& OutNames, bool& bOutHasFlags) const;

	/**
	 * Finds tracked unknown enum names associated with the object, otherwise null.
	 */
	const FUnknownEnumNames* FindUnknownEnumNames(const UObject* Owner) const;

	/**
	 * Resets tracked unknown enum names associated with the object.
	 */
	void ResetUnknownEnumNames(const UObject* Owner);

	// Future version for reworked InstanceDataObjects - track InstanceDataObject rather than bag (directly):
	/**
	 * Instantiate an InstanceDataObject object representing all fields within the bag, tracked against the owner object.
	 * @param Owner			- Associated in world object.
	 * @param Archive		- used to read value of new archive from. Leave this set to nullptr to use the object's linker or copy Owner
	 * @return				- Custom InstanceDataObject object, UClass derived from associated bag.
	 */
	COREUOBJECT_API UObject* CreateInstanceDataObject(UObject* Owner, FArchive* Archive = nullptr);
	COREUOBJECT_API UObject* DuplicateInstanceDataObject(UObject* SourceOwner, UObject* DestOwner);

	// called at the end of postload to copy data from Owner to its IDO
	COREUOBJECT_API void PostLoadInstanceDataObject(const UObject* Owner);

	// TODO: Restrict property bag  destruction to within UObject::BeginDestroy() & FPropertyBagProperty destructor.
	// Removes bag, InstanceDataObject, and all associated data for this object.
	void DestroyOuterBag(const UObject* Owner);

	/**
	 * ReassociateObjects
	 * @param ReplacedObjects - old/new owner object pairs. Reassigns InstanceDataObjects/bags to the new owner.
	 */
	COREUOBJECT_API void ReassociateObjects(const TMap<UObject*, UObject*>& ReplacedObjects);

	static void PostEditChangeChainProperty(const UObject* Object, FPropertyChangedChainEvent& PropertyChangedEvent);

	/**
	 * RequiresFixup - test if InstanceDataObject properties perfectly match object instance properties. This is necessary for the object to be published in UEFN.    
	 * @param Object		- Object to test.
	 * @param bIncludeOuter - Include the outer objects in the check.
	 * @return				- Does the object's InstanceDataObject, or optionally any of its outer objects, contain any loose properties requiring user fixup before the object may be published?
	 */
	COREUOBJECT_API bool RequiresFixup(const UObject* Object, bool bIncludeOuter = false) const;
	// set the bNeedsFixup flag for this object's IDO to false
	COREUOBJECT_API void MarkAsFixedUp(const UObject* Object = nullptr);

	// Accessors
	COREUOBJECT_API bool HasInstanceDataObject(const UObject* Owner) const;
	COREUOBJECT_API UObject* FindInstanceDataObject(const UObject* Owner);
	COREUOBJECT_API const UObject* FindInstanceDataObject(const UObject* Owner) const;
	COREUOBJECT_API void FindNestedInstanceDataObject(const UObject* Owner, bool bRequiresFixupOnly, TFunctionRef<void(UObject*)> Callback);
	UE_INTERNAL void AddReferencedInstanceDataObject(const UObject* Owner, FReferenceCollector& Collector);

	COREUOBJECT_API const UObject* FindInstanceForDataObject(const UObject* InstanceDataObject) const;

	// query whether a property in Struct was set when the struct was deserialized
	COREUOBJECT_API static bool WasPropertyValueSerialized(const UStruct* Struct, const void* StructData, const FProperty* Property, int32 ArrayIndex = 0);

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
	// End FGCObject interface

	// query for whether or not the given struct/class is a placeholder type
	static COREUOBJECT_API bool IsPropertyBagPlaceholderType(const UStruct* Type);
	// query for whether or not the given object was created as a placeholder type
	static COREUOBJECT_API bool IsPropertyBagPlaceholderObject(const UObject* Object);
	// query for whether or not creating property bag placeholder objects should be allowed
	static COREUOBJECT_API bool IsPropertyBagPlaceholderObjectSupportEnabled();

	/**
	 * Create a new placeholder type object to swap in for a missing class/struct. An object of
	 * this type will be associated with a property bag when serialized so it doesn't lose data.
	 * 
	 * @param Outer			Scope at which to create the placeholder type object (e.g. UPackage).
	 * @param Class			Type object class (or derivative type). For example, UClass::StaticClass().
	 * @param Name			Optional object name. If not specified, a unique object name will be created.
	 * @param Flags			Additional object flags. These will be appended to the default set of type object flags.
	 *						(Note: All placeholder types are transient by definition and internally default to 'RF_Transient'.)
	 * @param SuperStruct	Optional super type. By default, placeholder types are derivatives of UObject (NULL implies default).
	 * 
	 * @return A reference to a new placeholder type object.
	 */ 
	static COREUOBJECT_API UStruct* CreatePropertyBagPlaceholderType(UObject* Outer, UClass* Class, FName Name = NAME_None, EObjectFlags Flags = RF_NoFlags, UStruct* SuperStruct = nullptr);
	template<typename T = UObject>
	static UClass* CreatePropertyBagPlaceholderClass(UObject* Outer, UClass* Class, FName Name = NAME_None, EObjectFlags Flags = RF_NoFlags)
	{
		return Cast<UClass>(CreatePropertyBagPlaceholderType(Outer, Class, Name, Flags, T::StaticClass()));
	}

	/**
	 * Remove a placeholder type object from the internal registry.
	 * 
	 * @param PlaceholderType	Placeholder type object to remove.
	 */
	static COREUOBJECT_API void RemovePropertyBagPlaceholderType(UStruct* PlaceholderType);

private:
	void Lock() const { CriticalSection.Lock(); }
	void Unlock() const { CriticalSection.Unlock(); }

	// Internal functions requiring the repository to be locked before being called

	// Delete owner reference and disassociate all data. Returns success.
	bool RemoveAssociationUnsafe(const UObject* Owner);

	// Instantiate InstanceDataObject within BagData. Returns InstanceDataObject object. 
	void CreateInstanceDataObjectUnsafe(UObject* Owner, FPropertyBagAssociationData& BagData, FArchive* Archive = nullptr);
};

class FUnknownEnumNames
{
public:
	void Add(const UEnum* Enum, FPropertyTypeName EnumTypeName, FName EnumValueName);
	void Find(FPropertyTypeName EnumTypeName, TArray<FName>& OutNames, bool& bOutHasFlags) const;

private:
	struct FInfo
	{
		TSet<FName> Names;
		bool bHasFlags = false;
	};

	TMap<FPropertyTypeName, FInfo> Enums;
};

#endif // WITH_EDITORONLY_DATA

// construct this context in the same scope as an object is being serialized to support InstanceDataObjects.
// if saving, this will simply set flags. If loading, an IDO will be constructed at the end of the scope when needed
struct FScopedIDOSerializationContext
{
#if WITH_EDITORONLY_DATA
	COREUOBJECT_API FScopedIDOSerializationContext(UObject* InObject, FArchive& Archive);
	COREUOBJECT_API FScopedIDOSerializationContext(UObject* InObject, bool bImpersonate); // assumes save
	COREUOBJECT_API explicit FScopedIDOSerializationContext(bool bImpersonate); // assumes save
	COREUOBJECT_API ~FScopedIDOSerializationContext();

	FArchive* Archive = nullptr;
	UObject* const Object = nullptr;
	const int64 PreSerializeOffset = 0;
	UObject* SavedSerializedObject;
	bool bSavedTrackSerializedPropertyPath;
	bool bSavedTrackInitializedProperties;
	bool bSavedTrackSerializedProperties;
	bool bSavedTrackUnknownProperties;
	bool bSavedTrackUnknownEnumNames;
	bool bSavedImpersonateProperties;
	bool bCreateIDO = false;

private:
	void SaveSerializeContext(FUObjectSerializeContext* SerializeContext);
	void RestoreSerializeContext(FUObjectSerializeContext* SerializeContext) const;
	// if we're loading and IDO should be created, this will be called when the context falls out of scope
	void FinishCreatingInstanceDataObject() const;
#else
	inline FScopedIDOSerializationContext(UObject* InObject, FArchive& Archive) {}
	inline FScopedIDOSerializationContext(UObject* InObject, bool bImpersonate) {}
	inline explicit FScopedIDOSerializationContext(bool bImpersonate) {}
#endif

	FScopedIDOSerializationContext(const FScopedIDOSerializationContext&) = delete;
	FScopedIDOSerializationContext& operator=(const FScopedIDOSerializationContext&) = delete;
};

} // UE
