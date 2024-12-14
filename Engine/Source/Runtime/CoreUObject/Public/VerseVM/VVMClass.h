// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "Templates/SharedPointer.h"
#include "UObject/Class.h" // For UScriptStruct::ICppStructOps which can not be fwd-declared
#include "VerseVM/VVMArray.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMProcedure.h"
#include "VerseVM/VVMPropertyType.h"
#include "VerseVM/VVMShape.h"
#include "VerseVM/VVMType.h"

class UObject;
class UVerseVMClass;
class FVerseVMEngineEnvironment;

namespace Verse
{
struct FAbstractVisitor;
struct VObject;
struct VValueObject;
struct VNativeStruct;
struct VProcedure;
struct VPackage;
struct VFunction;

/// This provides a custom comparison that allows us to do pointer-based compares of each unique string set, rather than hash-based comparisons.
struct FEmergentTypesCacheKeyFuncs : TDefaultMapKeyFuncs<TWriteBarrier<VUniqueStringSet>, TWriteBarrier<VEmergentType>, /*bInAllowDuplicateKeys*/ false>
{
public:
	static bool Matches(KeyInitType A, KeyInitType B);
	static bool Matches(KeyInitType A, const VUniqueStringSet& B);
	static uint32 GetKeyHash(KeyInitType Key);
	static uint32 GetKeyHash(const VUniqueStringSet& Key);
};

/// A sequence of fields and blocks in a class body.
/// May represent either a single class, or the flattened combination of a subclass and its superclasses.
struct VConstructor : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	struct VEntry
	{
		/// When non-null, the name of this field. When null, this entry represents a block.
		TWriteBarrier<VUniqueString> Name;

		/// If the entry represents something defined in C++.
		bool bNative;

		/// For data members, the declared type.
		/// TODO: Can we just use VType for this?
		TWriteBarrier<VPropertyType> Type;

		/// When `bDynamic` is `true`, `Value` should be a `VFunction` for a default initializer or block, or nothing for an
		/// uninitialized field. Otherwise, `Value` should be a constant `VValue` representing a default field value.
		/// (This may be a `VFunction` without the `Self` member for methods, since they bind `Self` lazily).
		TWriteBarrier<VValue> Value;
		bool bDynamic;

		static VEntry Constant(FAllocationContext Context, FUtf8StringView InField, bool bInNative, VPropertyType* InPropertyType, VValue InValue);
		static VEntry Constant(FAllocationContext Context, VUniqueString& InField, bool bInNative, VPropertyType* InPropertyType, VValue InValue);
		static VEntry Field(FAllocationContext Context, VUniqueString& InField, bool bInNative, VPropertyType* InType = nullptr);
		static VEntry FieldInitializer(FAllocationContext Context, FUtf8StringView InField, bool bInNative, VPropertyType* InPropertyType, VProcedure& InCode);
		static VEntry FieldInitializer(FAllocationContext Context, VUniqueString& InField, bool bInNative, VPropertyType* InPropertyType, VProcedure& InCode);
		static VEntry Block(FAllocationContext Context, VProcedure& Code);

		/// Checks if the entry refers to a method that is unbound (i.e. has no `Self`), or if the entry is referring to a function at all.
		COREUOBJECT_API bool IsMethod() const;

		VFunction* Initializer() const;
	};

	const uint32 NumEntries;
	VEntry Entries[];

	static VConstructor& New(FAllocationContext Context, const TArray<VEntry>& InEntries)
	{
		size_t NumBytes = offsetof(VConstructor, Entries) + InEntries.Num() * sizeof(Entries[0]);
		return *new (Context.AllocateFastCell(NumBytes)) VConstructor(Context, InEntries);
	}

	COREUOBJECT_API void ToStringImpl(FStringBuilderBase& Builder, FAllocationContext Context, const FCellFormatter& Formatter);

	static void SerializeImpl(VConstructor*& This, FAllocationContext Context, FAbstractVisitor& Visitor);

	/// This loads the method that matches the given name. If it does not exist, returns `nullptr`.
	COREUOBJECT_API VFunction* LoadFunction(FAllocationContext Context, VUniqueString& FieldName, VValue SelfObject);

private:
	static VConstructor& NewUninitialized(FAllocationContext Context, uint32 InNumEntries)
	{
		size_t NumBytes = offsetof(VConstructor, Entries) + InNumEntries * sizeof(Entries[0]);
		return *new (Context.AllocateFastCell(NumBytes)) VConstructor(Context, InNumEntries);
	}

	VConstructor(FAllocationContext Context, const TArray<VEntry>& InEntries)
		: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
		, NumEntries(InEntries.Num())
	{
		for (uint32 Index = 0; Index < NumEntries; ++Index)
		{
			new (&Entries[Index]) VEntry(InEntries[Index]);
		}
	}

	VConstructor(FAllocationContext Context, uint32 InNumEntries)
		: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
		, NumEntries(InNumEntries)
	{
	}
};

struct VClass : VType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	enum class EKind : uint8
	{
		Class,
		Struct,
		Interface
	};

	FUtf8StringView GetName() const { return ClassName.Get() != nullptr ? ClassName->AsStringView() : FUtf8StringView(); }
	FUtf8StringView GetUEMangledName() const { return UEMangledName.Get() != nullptr ? UEMangledName->AsStringView() : FUtf8StringView(); }
	COREUOBJECT_API FUtf8StringView ExtractClassName() const;
	VPackage* GetScope() const { return Scope.Get(); }
	EKind GetKind() const { return Kind; }
	bool IsStruct() const { return GetKind() == EKind::Struct; }
	bool IsNative() const { return bNative; }
	bool IsNativeStruct() const { return IsNative() && IsStruct(); }

	/// Allocate a new VValueObject. Also returns a sequence of VProcedures to invoke to finish the object's construction.
	/// `ArchetypeValues` should match the order of IDs in `ArchetypeFields`.
	COREUOBJECT_API VValueObject& NewVObject(FAllocationContext Context, VUniqueStringSet& ArchetypeFields, const TArray<VValue>& ArchetypeValues, TArray<VFunction*>& OutInitializers);

	FOpResult NewNativeStruct(FAllocationContext Context, VUniqueStringSet& ArchetypeFields, const TArray<VValue>& ArchetypeValues, TArray<VFunction*>& OutInitializers);

	/// Allocate a new VNativeStruct and move an existing struct into it
	template <class CppStructType>
	VNativeStruct& NewNativeStruct(FAllocationContext Context, CppStructType&& Struct);

	/// Allocate a new UObject. Also returns a sequence of VProcedures to invoke to finish the object's construction.
	/// `ArchetypeValues` should match the order of IDs in `ArchetypeFields`.
	COREUOBJECT_API UObject* NewUObject(FAllocationContext Context, VUniqueStringSet& ArchetypeFields, const TArray<VValue>& ArchetypeValues, TArray<VFunction*>& OutInitializers);

private:
	// Helper to find initializer procedures after archetype fields have been set on an object
	void GatherInitializers(VUniqueStringSet& ArchetypeFields, TArray<VFunction*>& OutInitializers);

public:
	/// Vends an emergent type based on requested fields to override in the class archetype instantiation.
	COREUOBJECT_API VEmergentType& GetOrCreateEmergentTypeForArchetype(FAllocationContext Context, VUniqueStringSet& ArchetypeFieldNames, VCppClassInfo* CppClassInfo);
	VEmergentType& GetOrCreateEmergentTypeForNativeStruct(FAllocationContext Context);

	template <class SubTypeOfUStruct>
	COREUOBJECT_API SubTypeOfUStruct* GetUStruct() const; // Fails if it's not there
	template <class SubTypeOfUStruct>
	COREUOBJECT_API SubTypeOfUStruct* GetOrCreateUStruct(FAllocationContext Context);

	/// Returns the constructor for the class.
	COREUOBJECT_API VConstructor& GetConstructor() const;

	/**
	 * Creates a new class.
	 *
	 * @param Scope         Containing package or null.
	 * @param Name          Name or null.
	 * @param UEMangledName Name to be used when creating the UE version of the class or the UE package.  Can be null.
	 * @param ImportStruct  The Unreal class/struct that is being reflected by this Verse VM type.
	 * @param bNative       `true` if this represents a native class (i.e. defined in C++).
	 * @param Kind          Class, Struct or Interface.
	 * @param Inherited     An array of base classes, in order of inheritance.
	 * @param Constructor   The sequence of fields and blocks in the class body.
	 */
	COREUOBJECT_API static VClass& New(FAllocationContext Context, VPackage* Scope, VArray* Name, VArray* UEMangledName, UStruct* ImportStruct, bool bNative, EKind Kind, const TArray<VClass*>& Inherited, VConstructor& Constructor);

private:
	friend class ::FVerseVMEngineEnvironment;

	VClass(FAllocationContext Context, VPackage* InScope, VArray* InName, VArray* InUEMangledName, UStruct* InImportStruct, bool bInNative, EKind InKind, const TArray<VClass*>& InInherited, VConstructor& InConstructor);

	/// Append to `Entries` those elements of `Base` which are not already overridden, indicated by `Fields`.
	COREUOBJECT_API static void Extend(TSet<VUniqueString*>& Fields, TArray<VConstructor::VEntry>& Entries, const VConstructor& Base);

	/// Creates an associated UClass or UScriptStruct for this VClass
	COREUOBJECT_API UStruct* CreateUStruct(FAllocationContext Context);

	COREUOBJECT_API VEmergentType& GetOrCreateEmergentTypeForImportedNativeStruct(FAllocationContext Context);

	/// Initialize an instance using the constructor
	COREUOBJECT_API FOpResult InitInstance(FAllocationContext Context, VShape& Shape, void* Data) const;

	COREUOBJECT_API bool SubsumesImpl(FAllocationContext, VValue);

	/// The package this class is in
	TWriteBarrier<VPackage> Scope;

	TWriteBarrier<VArray> ClassName;

	TWriteBarrier<VArray> UEMangledName;
	/// An associated UClass/UScriptStruct allows this VClass to create UObject/VNativeStruct instances
	TWriteBarrier<VValue> AssociatedUStruct;
	bool bNative;

	EKind Kind;

	// Super classes and interfaces. The single superclass is always first.
	uint32 NumInherited;

	/// The combined sequence of initializers and blocks in this class and its superclasses, in execution order.
	/// Actual object construction may further override some elements of this sequence.
	TWriteBarrier<VConstructor> Constructor;

	// TODO: (yiliang.siew) This should be a weak map when we can support it in the GC. https://jira.it.epicgames.com/browse/SOL-5312
	/// This is a cache that allows for fast vending of emergent types based on the fields being overridden.
	TMap<TWriteBarrier<VUniqueStringSet>, TWriteBarrier<VEmergentType>, FDefaultSetAllocator, FEmergentTypesCacheKeyFuncs> EmergentTypesCache;

	TWriteBarrier<VClass> Inherited[];
};

template <class SubTypeOfUStruct>
inline SubTypeOfUStruct* VClass::GetUStruct() const
{
	return CastChecked<SubTypeOfUStruct>(AssociatedUStruct.Get().AsUObject());
}
template <class SubTypeOfUStruct>
inline SubTypeOfUStruct* VClass::GetOrCreateUStruct(FAllocationContext Context)
{
	return AssociatedUStruct ? GetUStruct<SubTypeOfUStruct>() : CastChecked<SubTypeOfUStruct>(CreateUStruct(Context));
}
};     // namespace Verse
#endif // WITH_VERSE_VM
