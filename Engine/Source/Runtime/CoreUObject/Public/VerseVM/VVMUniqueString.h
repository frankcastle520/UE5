// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "Containers/Set.h"
#include "Containers/StringView.h"
#include "Templates/UnrealTemplate.h"
#include "VerseVM/VVMArray.h"
#include "VerseVM/VVMCell.h"
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMGlobalHeapCensusRoot.h"
#include "VerseVM/VVMGlobalTrivialEmergentTypePtr.h"
#include "VerseVM/VVMWeakBarrier.h"

namespace Verse
{
struct VUniqueString;
class VUniqueStringSetInternPool;

template <typename T>
struct FUniqueStringSetKeyFuncs;

/// We're providing this so that we can avoid constructing `TWriteBarrier`/`TWeakBarrier`s to perform lookups in
/// the string pool, which are expensive given that they involve a TLS lookup.
template <typename T>
struct FUniqueStringSetKeyFuncsBase : BaseKeyFuncs<T, T, false>
{
	typedef FUtf8StringView KeyInitType;
	typedef VUniqueString& ElementInitType;

	static FUtf8StringView GetSetKey(VUniqueString& Element);

	static FUtf8StringView GetSetKey(const T& Element);

	static bool Matches(FUtf8StringView A, FUtf8StringView B);

	static uint32 GetKeyHash(FUtf8StringView Key);
};

template <>
struct FUniqueStringSetKeyFuncs<TWeakBarrier<VUniqueString>> : FUniqueStringSetKeyFuncsBase<TWeakBarrier<VUniqueString>>
{
};

template <>
struct FUniqueStringSetKeyFuncs<TWriteBarrier<VUniqueString>> : FUniqueStringSetKeyFuncsBase<TWriteBarrier<VUniqueString>>
{
};

class VStringInternPool final : FGlobalHeapCensusRoot
{
private:
	/// Private constructor since there should only ever be one global instance of this.
	/// There's no virtual destructor since `TLazyInitialized` is never destroyed and this is meant to be a global string pool.
	VStringInternPool() = default;

	COREUOBJECT_API VUniqueString& Intern(FAllocationContext Context, FUtf8StringView String);

	/// This gives the string intern pool the ability to conduct census on its own to clear references to the strings.
	COREUOBJECT_API virtual void ConductCensus() override;

	// The pool doesn't own the string data, the context does. So these strings are stored as weakrefs.
	// When the GC conducts a census, this string pool is also cleared of the strings that are unmarked.
	TSet<TWeakBarrier<VUniqueString>, FUniqueStringSetKeyFuncs<TWeakBarrier<VUniqueString>>> UniqueStrings;
	static UE::FMutex Mutex;

	friend struct VUniqueString;
	friend struct TLazyInitialized<VStringInternPool>;
};

/// A unique string that lives in the global string intern pool.
struct VUniqueString final : VArray
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VArray);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	/**
	 * Creates a new unique string if it does not already exist.
	 * Subsequent calls to this function with the same string will return a reference to the string that already exists in the string intern pool.
	 */
	static VUniqueString& New(FAllocationContext Context, FUtf8StringView String)
	{
		return StringPool->Intern(Context, String);
	}

	bool operator==(const VUniqueString& Other) const
	{
		return this == &Other;
	}

	// Shadow `VArrayBase::AsStringView`, avoiding type test.
	FUtf8StringView AsStringView() const
	{
		return FUtf8StringView(GetData<UTF8CHAR>(), Num());
	}

	static void SerializeImpl(VUniqueString*& This, FAllocationContext Context, FAbstractVisitor& Visitor);

private:
	static VUniqueString& Make(FAllocationContext Context, FUtf8StringView String)
	{
		return *new (Context.AllocateFastCell(sizeof(VUniqueString))) VUniqueString(Context, String);
	}

	VUniqueString(FAllocationContext Context, FUtf8StringView String)
		: VArray(Context, String, &GlobalTrivialEmergentType.Get(Context))
	{
	}

	/// Global unique string table. This has to be wrapped in a `TLazyInitialized` so that the Verse heap is first
	/// initialized before this attempts to be initialized.
	COREUOBJECT_API static TLazyInitialized<VStringInternPool> StringPool;
	friend class VStringInternPool;
};

/// Allows for `VUniqueString` to be used with Unreal hashtable containers like `TMap`/`TSet`.
inline uint32 GetTypeHash(const VUniqueString& String)
{
	return PointerHash(&String);
}

/// A unique string set. This makes use of a pool so that multiple requests for the same set of unique strings
/// returns the exact same set object in memory.
struct VUniqueStringSet : VCell
{
	using SetType = TSet<TWriteBarrier<VUniqueString>, FUniqueStringSetKeyFuncs<TWriteBarrier<VUniqueString>>>;
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	// This allows for this type to be used in range-based loops.
	class FConstIterator
	{
	public:
		FSetElementId GetId() const;
		const TWriteBarrier<VUniqueString>* operator->() const;
		const TWriteBarrier<VUniqueString>& operator*() const;
		bool operator==(const FConstIterator& Rhs) const;
		bool operator!=(const FConstIterator& Rhs) const;
		FConstIterator& operator++();

	private:
		friend struct VUniqueStringSet;
		FConstIterator(SetType::TRangedForConstIterator InCurrentIteration);
		SetType::TRangedForConstIterator CurrentIteration;
	};
	FConstIterator begin() const;
	FConstIterator end() const;

	static VUniqueStringSet& New(FAllocationContext Context, const TSet<VUniqueString*> InSet);
	static VUniqueStringSet& New(FAllocationContext Context, const std::initializer_list<FUtf8StringView>& InStrings);

	bool operator==(const VUniqueStringSet& Other) const;

	/// This is a slower, deep-equality based check. Suitable for debugging purposes.
	bool Equals(const VUniqueStringSet& Other) const;

	uint32 Num() const;

	FSetElementId FindId(const FUtf8StringView& String) const;

	bool IsValidId(const FSetElementId& Id) const;

	COREUOBJECT_API void ToStringImpl(FStringBuilderBase& Builder, FAllocationContext Context, const FCellFormatter& Formatter);

	static void SerializeImpl(VUniqueStringSet*& This, FAllocationContext Context, FAbstractVisitor& Visitor);

private:
	static SetType FormSet(FAllocationContext Context, const TSet<VUniqueString*>& InSet);
	VUniqueStringSet(FAllocationContext Context, const TSet<VUniqueString*>& InSet);

	static VUniqueStringSet& Make(FAllocationContext Context, const TSet<VUniqueString*>& InSet);
	static bool Equals(const TSet<VUniqueString*>& A, const TSet<VUniqueString*>& B);

	/// Global unique string set pool. This has to be wrapped in a `TLazyInitialized` so that the Verse heap is first
	/// initialized before this attempts to be initialized.
	COREUOBJECT_API static TLazyInitialized<VUniqueStringSetInternPool> Pool;

	/// The storage for the actual strings in this given set.
	SetType Strings;

	friend class VUniqueStringSetInternPool;
	friend struct FHashableUniqueStringSetKeyFuncs;
};

uint32 GetTypeHash(const TSet<VUniqueString*>& Set);
uint32 GetTypeHash(const VUniqueStringSet& Set);

struct FHashableUniqueStringSetKey
{
	enum class EType : uint8
	{
		Cell,
		Set,

		/// This means that the key refers to a invalid unique string set - its memory has been
		/// swept/is about to be swept and is no longer considered a valid set. This is used as
		/// a sentinel value to distinguish between actual empty string sets.
		/// When `Type` is set to this, both `Cell` and `Set` are undefined.
		Invalid
	};

	union
	{
		const VUniqueStringSet* Cell;
		const TSet<VUniqueString*>* Set;
	};

	EType Type;

	FHashableUniqueStringSetKey();
	FHashableUniqueStringSetKey(const TSet<VUniqueString*>& InSet);
	FHashableUniqueStringSetKey(const VUniqueStringSet& InCell);
	bool operator==(const FHashableUniqueStringSetKey& Other) const;
};

/// Allows for lookup into the unique string set pool without unnecessary construction of barriers.
struct FHashableUniqueStringSetKeyFuncs : BaseKeyFuncs<TWeakBarrier<VUniqueStringSet>, TWeakBarrier<VUniqueStringSet>, /*bAllowDuplicateKeys*/ false>
{
	typedef FHashableUniqueStringSetKey KeyInitType;
	typedef VUniqueStringSet& ElementInitType;

	static KeyInitType GetSetKey(ElementInitType& Element);

	static KeyInitType GetSetKey(const TWeakBarrier<VUniqueStringSet>& Element);

	static bool Matches(KeyInitType A, KeyInitType B);

	static uint32 GetKeyHash(KeyInitType Key);
};

/// A unique set string pool.
class VUniqueStringSetInternPool final : FGlobalHeapCensusRoot
{
private:
	/// Private constructor since there should only ever be one global instance of this.
	/// There's no virtual destructor since `TLazyInitialized` is never destroyed and this is meant to be a global unique string set pool.
	VUniqueStringSetInternPool() = default;

	/// Retrieves an existing string set from the set pool if it exists or creates a new one and returns it.
	COREUOBJECT_API VUniqueStringSet& Intern(FAllocationContext Context, const TSet<VUniqueString*>& InSet);

	/// This gives the pool the ability to conduct census on its own to clear references to the sets.
	COREUOBJECT_API virtual void ConductCensus() override;

	TSet<TWeakBarrier<VUniqueStringSet>, FHashableUniqueStringSetKeyFuncs> Sets;

	static UE::FMutex Mutex;

	friend struct VUniqueStringSet;
	friend struct TLazyInitialized<VUniqueStringSetInternPool>;
};

} // namespace Verse
#endif // WITH_VERSE_VM
