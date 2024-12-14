// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VVMCell.h"
#include "VVMGlobalTrivialEmergentTypePtr.h"
#include "VVMUniqueString.h"

class UVerseStruct;

namespace Verse
{

struct VPackage;
struct VTupleType;

// These property types are similar to the uLang::ETypeKind enumeration but without the extra types
enum class EPropertyType : uint8
{
	False,
	True,
	Void,
	Any,
	Comparable,
	Logic,
	Int,
	Rational,
	Float,
	Char8,
	Char32,
	Range,
	Type,
	Class,
	Enumeration,
	Array,
	Generator,
	Map,
	Pointer,
	Reference,
	Option,
	Interface,
	Tuple,
	Function,
	Variable,
	Named,
	Persistable,

	// The follow exist only to simplify the code and are not used
	Unknown,
	Module,
	Path,
};

struct VPropertyType : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	static VPropertyType& New(FAllocationContext Context, EPropertyType PropertyType)
	{
		return *new (Context.AllocateFastCell(sizeof(VPropertyType))) VPropertyType(Context, PropertyType, &GlobalTrivialEmergentType.Get(Context));
	}

	// The property type is just the uLang::ETypeKind enumeration.
	EPropertyType PropertyType;

protected:
	VPropertyType(FAllocationContext Context, EPropertyType InPropertyType, const VEmergentType* EmergentType)
		: VCell(Context, EmergentType)
		, PropertyType(InPropertyType) {}
};

struct VIntPropertyType final : VPropertyType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VPropertyType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	static VIntPropertyType& New(FAllocationContext Context, int64 ClampMin, int64 ClampMax)
	{
		return *new (Context.AllocateFastCell(sizeof(VIntPropertyType))) VIntPropertyType(Context, ClampMin, ClampMax);
	}

	int64 ClampMin;
	int64 ClampMax;

protected:
	VIntPropertyType(FAllocationContext Context, int64 InClampMin, int64 InClampMax)
		: VPropertyType(Context, EPropertyType::Int, &GlobalTrivialEmergentType.Get(Context))
		, ClampMin(InClampMin)
		, ClampMax(InClampMax)
	{
	}
};

struct VFloatPropertyType final : VPropertyType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VPropertyType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	static VFloatPropertyType& New(FAllocationContext Context, double ClampMin, double ClampMax)
	{
		return *new (Context.AllocateFastCell(sizeof(VFloatPropertyType))) VFloatPropertyType(Context, ClampMin, ClampMax);
	}

	double ClampMin;
	double ClampMax;

protected:
	VFloatPropertyType(FAllocationContext Context, double InClampMin, double InClampMax)
		: VPropertyType(Context, EPropertyType::Float, &GlobalTrivialEmergentType.Get(Context))
		, ClampMin(InClampMin)
		, ClampMax(InClampMax)
	{
	}
};

struct VTypePropertyType final : VPropertyType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VPropertyType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	static VTypePropertyType& New(FAllocationContext Context, bool bIsAbstract, VUniqueString& PackageName, VUniqueString& ClassName)
	{
		return *new (Context.AllocateFastCell(sizeof(VTypePropertyType))) VTypePropertyType(Context, bIsAbstract, PackageName, ClassName);
	}

	bool bIsAbstract;
	TWriteBarrier<VUniqueString> PackageName;
	TWriteBarrier<VUniqueString> ClassName;

protected:
	VTypePropertyType(FAllocationContext Context, bool bInIsAbstract, VUniqueString& InPackageName, VUniqueString& InClassName)
		: VPropertyType(Context, EPropertyType::Type, &GlobalTrivialEmergentType.Get(Context))
		, bIsAbstract(bInIsAbstract)
		, PackageName(Context, InPackageName)
		, ClassName(Context, InClassName)
	{
	}
};

struct VClassPropertyType final : VPropertyType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VPropertyType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	static VClassPropertyType& New(FAllocationContext Context, bool bIsStruct, VValue ClassValue)
	{
		return *new (Context.AllocateFastCell(sizeof(VClassPropertyType))) VClassPropertyType(Context, bIsStruct, ClassValue);
	}

	bool bIsStruct;
	TWriteBarrier<VValue> ClassValue;

protected:
	VClassPropertyType(FAllocationContext Context, bool bInIsStruct, VValue InClassValue)
		: VPropertyType(Context, EPropertyType::Class, &GlobalTrivialEmergentType.Get(Context))
		, bIsStruct(bInIsStruct)
		, ClassValue(Context, InClassValue)
	{
	}
};

struct VTuplePropertyType final : VPropertyType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VPropertyType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	VTupleType* GetType() const { return Type.Get(); }

	static VTuplePropertyType& New(FAllocationContext Context, VTupleType* Type)
	{
		return *new (Context.AllocateFastCell(sizeof(VTuplePropertyType))) VTuplePropertyType(Context, Type);
	}

private:
	VTuplePropertyType(FAllocationContext Context, VTupleType* InType)
		: VPropertyType(Context, EPropertyType::Tuple, &GlobalTrivialEmergentType.Get(Context))
		, Type(Context, InType)
	{
	}

	TWriteBarrier<VTupleType> Type;
};

struct VWrappedPropertyType : VPropertyType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VPropertyType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	static VWrappedPropertyType& New(FAllocationContext Context, EPropertyType PropertyType, VPropertyType& Inner)
	{
		return *new (Context.AllocateFastCell(sizeof(VWrappedPropertyType))) VWrappedPropertyType(Context, PropertyType, Inner, &GlobalTrivialEmergentType.Get(Context));
	}

	TWriteBarrier<VPropertyType> Inner;

protected:
	VWrappedPropertyType(FAllocationContext Context, EPropertyType InPropertyType, VPropertyType& InInner, const VEmergentType* EmergentType)
		: VPropertyType(Context, InPropertyType, EmergentType)
		, Inner(Context, InInner)
	{
	}
};

struct VArrayPropertyType final : VWrappedPropertyType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VWrappedPropertyType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	static VArrayPropertyType& New(FAllocationContext Context, bool bIsString, VPropertyType& Inner)
	{
		return *new (Context.AllocateFastCell(sizeof(VArrayPropertyType))) VArrayPropertyType(Context, bIsString, Inner);
	}

	bool bIsString;

protected:
	VArrayPropertyType(FAllocationContext Context, bool bInIsString, VPropertyType& InInner)
		: VWrappedPropertyType(Context, EPropertyType::Array, InInner, &GlobalTrivialEmergentType.Get(Context))
		, bIsString(bInIsString)
	{
	}
};

struct VMapPropertyType final : VWrappedPropertyType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VWrappedPropertyType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	static VMapPropertyType& New(FAllocationContext Context, VPropertyType& Key, VPropertyType& Inner)
	{
		return *new (Context.AllocateFastCell(sizeof(VMapPropertyType))) VMapPropertyType(Context, Key, Inner);
	}

	TWriteBarrier<VPropertyType> Key;

protected:
	VMapPropertyType(FAllocationContext Context, VPropertyType& InKey, VPropertyType& InInner)
		: VWrappedPropertyType(Context, EPropertyType::Map, InInner, &GlobalTrivialEmergentType.Get(Context))
		, Key(Context, InKey)
	{
	}
};

struct VInterfacePropertyType final : VPropertyType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VPropertyType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	static VInterfacePropertyType& New(FAllocationContext Context, VValue InterfaceValue)
	{
		return *new (Context.AllocateFastCell(sizeof(VInterfacePropertyType))) VInterfacePropertyType(Context, InterfaceValue);
	}

	TWriteBarrier<VValue> InterfaceValue;

protected:
	VInterfacePropertyType(FAllocationContext Context, VValue InInterfaceValue)
		: VPropertyType(Context, EPropertyType::Class, &GlobalTrivialEmergentType.Get(Context))
		, InterfaceValue(Context, InInterfaceValue)
	{
	}
};

} // namespace Verse
#endif // WITH_VERSE_VM
