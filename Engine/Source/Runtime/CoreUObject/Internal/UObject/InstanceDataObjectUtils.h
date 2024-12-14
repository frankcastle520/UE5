// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#if WITH_EDITORONLY_DATA

#define UE_API COREUOBJECT_API

class FFieldVariant;
class FProperty;
class UClass;
class UObject;
class UStruct;

namespace UE { class FPropertyPathNameTree; }
namespace UE { class FUnknownEnumNames; }

namespace UE
{

/** Query if InstanceDataObject support is available generally. */
bool IsInstanceDataObjectSupportEnabled();
/** Query if InstanceDataObject support is enabled for a specific object. */
bool IsInstanceDataObjectSupportEnabled(const UObject* Object);
bool StructContainsLooseProperties(const UStruct* Struct);

/** Query if placeholder support is enabled for a specific import class type. */
bool CanCreatePropertyBagPlaceholderTypeForImportClass(const UClass* ImportType);

/** Helper to check if a class is an IDO class. */
bool IsClassOfInstanceDataObjectClass(UStruct* Class);

/** Generate a UClass that contains the union of the properties of PropertyTree and OwnerClass. */
UClass* CreateInstanceDataObjectClass(const FPropertyPathNameTree* PropertyTree, const FUnknownEnumNames* EnumNames, UClass* OwnerClass, UObject* Outer);

/** Notify that a property in an struct was set when the struct was deserialized. */
void MarkPropertyValueSerialized(const UStruct* Struct, void* StructData, const FProperty* Property, int32 ArrayIndex = 0);
/** Query whether a property in the struct was set when the struct was deserialized. */
bool WasPropertyValueSerialized(const UStruct* Struct, const void* StructData, const FProperty* Property, int32 ArrayIndex = 0);
/** Copy whether each property was set by serialization from one IDO to another. */
void CopyPropertyValueSerializedData(const FFieldVariant& OldField, void* OldDataPtr, const FFieldVariant& NewField, void* NewDataPtr);

/** Query whether the property value is initialized for a property in the struct. */
UE_API bool IsPropertyValueInitialized(const UStruct* Struct, void* StructData, const FProperty* Property, int32 ArrayIndex = 0);
/** Set the flag that the property value is initialized for a property in the struct. */
UE_API void SetPropertyValueInitialized(const UStruct* Struct, void* StructData, const FProperty* Property, int32 ArrayIndex = 0);
/** Clear the flag that the property value is initialized for a property in the struct. */
UE_API void ClearPropertyValueInitialized(const UStruct* Struct, void* StructData, const FProperty* Property, int32 ArrayIndex = 0);
/** Reset the property value initialized flags for every property in the struct. */
UE_API void ResetPropertyValueInitialized(const UStruct* Struct, void* StructData);

} // UE

#undef UE_API

#endif // WITH_EDITORONLY_DATA
