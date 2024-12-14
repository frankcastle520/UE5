// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "Containers/Map.h"
#include "VVMCell.h"
#include "VVMUniqueString.h"
#include "VVMWriteBarrier.h"

class FVerseVMEngineEnvironment;
class FProperty;

namespace Verse
{
struct FAccessContext;
struct VUniqueString;
struct VEmergentType;

template <Verse::VCppClassInfo* ClassInfo>
struct TGlobalTrivialEmergentTypePtr;

enum class EFieldType : int8
{
	// The field's value is stored in a VObject.
	// e.g. `c := class{ X:int }` or `c := class{ var X:int = 0 }`
	Offset,

	// The field's value is a native type stored in a UObject or VNativeStruct.
	FProperty,
	// The field's value is a mutable native type stored in a UObject or VNativeStruct.
	FPropertyVar,
	// The field's value is a VRestValue stored in a UObject or VNativeStruct.
	FVerseProperty,

	// The field's value is stored in the shape.
	// This is used for fields default-initialized to a constant, such as methods.
	// e.g. `a := class{ X:int = 3, Y:int = 5, F(Z:int):int = X + Y + Z }; A := a{ X := 8 }
	// Here, `A.Y` and `A.F` are stored in the shape, while `A.X` is stored in the object.
	Constant,
};

/// Maps fully qualified names to offsets/constants.
struct VShape : VCell
{
	struct VEntry
	{
		union
		{
			/// The zero-based offset for the given entry that can be used to index into the object.
			uint64 Index;

			/// For shapes of UObjects, this points to the FProperty associated with this field
			/// The caller must guarantee that the property lives as long as this shape
			FProperty* UProperty;

			/// The constant value for the given entry.
			TWriteBarrier<VValue> Value;
		};

		EFieldType Type;

		// Must have a copy/move constructor in order to be used with `TMap` as the value type.
		VEntry(const VEntry& Other);
		VEntry(VEntry&& Other)
			: VEntry(Other) {}

		static VEntry Offset() { return {}; }
		static VEntry Property(FProperty* InProperty = nullptr) { return {InProperty, EFieldType::FProperty}; }
		static VEntry PropertyVar(FProperty* InProperty = nullptr) { return {InProperty, EFieldType::FPropertyVar}; }
		static VEntry Constant(FAccessContext Context, VValue InConstant) { return {Context, InConstant}; }

		bool operator==(const VEntry& Other) const;

		bool IsProperty() const { return Type == EFieldType::FProperty || Type == EFieldType::FPropertyVar || Type == EFieldType::FVerseProperty; }

	private:
		VEntry();
		VEntry(FProperty* InProperty, EFieldType InType);
		VEntry(FAccessContext Context, VValue InConstant);
	};

	/// We're providing this in order to be able to lookup into the fields map without having to
	/// construct a write barrier around the unique string representing the field name each time.
	struct FFieldsMapKeyFuncs : TDefaultMapKeyFuncs<TWriteBarrier<VUniqueString>, VEntry, /*bInAllowDuplicateKeys*/ false>
	{
		static bool Matches(KeyInitType A, KeyInitType B);
		static bool Matches(KeyInitType A, const VUniqueString& B);
		static uint32 GetKeyHash(KeyInitType Key);
		static uint32 GetKeyHash(const VUniqueString& Key);
	};

	using FieldsMap = TMap<TWriteBarrier<VUniqueString>, VEntry, FDefaultSetAllocator, FFieldsMapKeyFuncs>;

	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	/// Create a new shape. Note that indices for offset-based fields will be discarded and the fields given re-ordered
	/// indices as part of the new shape created.
	COREUOBJECT_API static VShape* New(FAllocationContext Context, FieldsMap&& InFields);

	const VEntry* GetField(const VUniqueString& Name) const;

	uint64 GetNumFields() const;

	bool operator==(const VShape& Other) const;

	friend uint32 GetTypeHash(const VShape& Shape);

	VShape& CopyToMeltedShape(FAllocationContext);

	FieldsMap::TIterator CreateFieldsIterator() { return Fields.CreateIterator(); }

private:
	VShape(FAllocationContext Context, FieldsMap&& InFields);

	/// Mapping of the field names to their data in the layout.
	/// This should not be mutated after initialization; if this needs to be modified, you
	/// should create a new shape and emergent type instead.
	/// We can't mark this map as `const` because we need to be able to mark the entries and names to hold strong references to them.
	FieldsMap Fields;

	uint64 NumIndexedFields;

	friend class ::FVerseVMEngineEnvironment;
	friend struct VClass;
	friend struct VObject;
	friend struct VValueObject;
	friend struct VEmergentType;
};

} // namespace Verse
#endif // WITH_VERSE_VM
