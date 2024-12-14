// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "HAL/PlatformMemory.h"
#include "Memory/MemoryView.h"
#include "Misc/AssertionMacros.h"
#include "Serialization/VarInt.h"
#include "PlainPropsTypes.h"

namespace PlainProps
{

struct FEnumSchema;
struct FSchemaBatch;
struct FStructSchema;

//////////////////////////////////////////////////////////////////////////

// Represents a batch currently being read from
class FReadBatchId
{
	friend class FReadSchemaRegistry;
	uint16 Idx = 0;
};

// @param Schemas must outlive read batch
PLAINPROPS_API const FSchemaBatch*		ValidateSchemas(FMemoryView Schemas);
PLAINPROPS_API FReadBatchId				MountReadSchemas(const FSchemaBatch* Schemas);
PLAINPROPS_API const FSchemaBatch*		UnmountReadSchemas(FReadBatchId Batch);

PLAINPROPS_API uint32					NumStructSchemas(FReadBatchId Batch);
PLAINPROPS_API const FStructSchema&		ResolveStructSchema(FReadBatchId Batch, FStructSchemaId Id);
PLAINPROPS_API const FEnumSchema&		ResolveEnumSchema(FReadBatchId Batch, FEnumSchemaId Id);
PLAINPROPS_API FNestedScope				ResolveUntranslatedNestedScope(FReadBatchId Batch, FNestedScopeId Id);
PLAINPROPS_API FParametricTypeView		ResolveUntranslatedParametricType(FReadBatchId Batch, FParametricTypeId Id);

//////////////////////////////////////////////////////////////////////////

class FByteReader
{
	const uint8* It = nullptr;
#if DO_CHECK
	const uint8* End = nullptr;
#endif
public:
	FByteReader() = default;
	explicit FByteReader(FMemoryView View) : FByteReader(static_cast<const uint8*>(View.GetData()), View.GetSize()) {}
	FByteReader(const uint8* Data, uint64 NumBytes) : FByteReader(Data, Data + NumBytes) {}
	FByteReader(const uint8* InBegin, const uint8* InEnd)
	: It(InBegin)
#if DO_CHECK
	, End(InEnd)
#endif
	{}

	[[nodiscard]] const uint8* GrabBytes(uint64 NumBytes)
	{
		check(It + NumBytes <= End);
		const uint8* Out = It;
		It += NumBytes;
		return Out;
	}

	[[nodiscard]] inline FMemoryView GrabSlice(uint64 NumBytes)
	{
		return FMemoryView(GrabBytes(NumBytes), NumBytes);
	}

	[[nodiscard]] inline FMemoryView GrabSkippableSlice()
	{
		return GrabSlice(GrabVarIntU());
	}

	[[nodiscard]] inline uint8 GrabByte()
	{
		return *GrabBytes(1);
	}

	template<typename T>
	[[nodiscard]] inline T Grab()
	{
		return FPlatformMemory::ReadUnaligned<T>(GrabBytes(sizeof(T)));
	}
	
	[[nodiscard]] uint64 GrabVarIntU()
	{
		check(It < End);
		uint32 NumBytesRead;
		uint64 Out = ReadVarUInt(It, NumBytesRead);
		It += NumBytesRead;
		checkSlow(It <= End);
		return Out;
	}

	[[nodiscard]] const uint8* Peek() const { return It; }
	
	[[nodiscard]] FMemoryView PeekSkippableSlice() const { return FByteReader(*this).GrabSkippableSlice(); }

	template<typename T>
	void SkipAlignmentPadding()
	{
		for (; !IsAligned(It, alignof(T)); ++It)
		{
			check(*It == 0);
		}
	}
	
	void CheckEmpty() const { check(It == End); }
	void CheckNonEmpty() const { check(It != End); }
	void CheckSize(int64 ExpectedSize) const { check(End - It == ExpectedSize); }
};

// Helper that consumes 8 bits from the byte value stream
class FBitCacheReader
{
	uint8 Bits = 0;
	uint8 BitIt = 0;
public:
	[[nodiscard]] FORCEINLINE bool GrabNext(FByteReader& Bytes)
	{
		BitIt <<= 1; // Shift up til overflow

		if (BitIt == 0)
		{
			Bits = Bytes.GrabByte();
			BitIt = 1;
		}

		return !!(Bits & BitIt);
	}

	FORCENOINLINE void Skip(uint32 Num, FByteReader& Bytes)
	{
		uint32 NumCached = 1 + FMath::CountLeadingZeros8(BitIt);

		if (NumCached > Num)
		{
			BitIt <<= Num;
		}
		else
		{
			uint32 NumUncached = Num - NumCached;

			// Grab new bytes, keep the last byte and bit within it
			uint32 NumBytes = Align(NumUncached + 1, 8) / 8;
			Bits = Bytes.GrabBytes(NumBytes)[NumBytes - 1];
			BitIt = 1 << (NumUncached % 8);
		}
	}
};

//////////////////////////////////////////////////////////////////////////

struct FStructSchemaHandle
{
	FStructSchemaId			Id;
	FReadBatchId			Batch;

	const FStructSchema&	Resolve() const { return ResolveStructSchema(Batch, Id); }
	const FStructSchema&	ResolveSuper() const;
};

struct FStructView
{
	FStructSchemaHandle		Schema;
	FByteReader				Values;
};

//////////////////////////////////////////////////////////////////////////

union FMemberValue
{
	const uint8*			Ptr;	// From byte stream
	bool					bValue; // From bit cache
};

struct FLeafView
{
	FUnpackedLeafType		Leaf;
	FReadBatchId			Batch;
	FEnumSchemaId			Enum;
	FMemberValue			Value;

	FORCEINLINE bool		AsBool() const		{ return Value.bValue; }
	FORCEINLINE int8		AsS8() const		{ return As<int8>(); }
	FORCEINLINE uint8		AsU8() const		{ return As<uint8>(); }
	FORCEINLINE int16		AsS16() const		{ return As<int16>(); }
	FORCEINLINE uint16		AsU16() const		{ return As<uint16>(); }
	FORCEINLINE int32		AsS32() const		{ return As<int32>(); }
	FORCEINLINE uint32		AsU32() const		{ return As<uint32>(); }
	FORCEINLINE int64		AsS64() const		{ return As<int64>(); }
	FORCEINLINE uint64		AsU64() const		{ return As<uint64>(); }
	FORCEINLINE double		AsDouble() const	{ return As<double>(); }
	FORCEINLINE float		AsFloat() const		{ return As<float>(); }
	FORCEINLINE char8_t		AsChar8() const		{ return As<char8_t>(); }
	FORCEINLINE char16_t	AsChar16() const	{ return As<char16_t>(); }
	FORCEINLINE char32_t	AsChar32() const	{ return As<char32_t>(); }
	FORCEINLINE uint8		AsEnum8() const		{ return As<uint8,	FUnpackedLeafType{ELeafType::Enum, ELeafWidth::B8}>(); }
	FORCEINLINE uint16		AsEnum16() const	{ return As<uint16,	FUnpackedLeafType{ELeafType::Enum, ELeafWidth::B16}>(); }
	FORCEINLINE uint32		AsEnum32() const	{ return As<uint32,	FUnpackedLeafType{ELeafType::Enum, ELeafWidth::B32}>(); }
	FORCEINLINE uint64		AsEnum64() const	{ return As<uint64,	FUnpackedLeafType{ELeafType::Enum, ELeafWidth::B64}>(); }

private:
	template<Arithmetic T, FUnpackedLeafType ExpectedLeaf = ReflectArithmetic<T>>
	FORCEINLINE T As() const
	{
		check(ExpectedLeaf == Leaf);
		return *reinterpret_cast<const T*>(Value.Ptr);
	}

	template<Enumeration T, FUnpackedLeafType ExpectedLeaf = ReflectEnum<T>>
	FORCEINLINE T As() const
	{
		check(ExpectedLeaf == Leaf);
		return *reinterpret_cast<const T*>(Value.Ptr);
	}
};

//////////////////////////////////////////////////////////////////////////

//struct FEnumSchemaFlatView
//{
//	const FEnumSchema& Schema;
//	
//	TOptional<uint64>		ToValue(FMemberId Id) const;
//	TOptional<FMemberId>	ToId(uint64 Value) const;
//	bool					IsSet(FMemberId Id, uint64 Value) const;
//};
//
//struct FEnumSchemaFlagView
//{
//	const FEnumSchema& Schema;
//
//	FEnumSchemaFlagView(const FEnumSchema& InSchema); // @pre sizeof(T) matches Schema.Width and all schema values use single bit
//	
//	TOptional<uint64>						ToFlags(TConstArrayView<FMemberId> Ids) const;
//	TOptional<TConstArrayView<FMemberId>>	ToIds(uint64 Flags) const;
//	bool									HasAny(FMemberId Id, uint64 Flags) const;
//	bool									HasAll(FMemberId Id, uint64 Flags) const;
//};
//
//struct FEnumFlatView
//{
//	const FEnumSchemaFlatView* Schema;
//	uint64 Value;
//
//	bool Equals(FMemberId Id) const;
//};
//
//struct FEnumFlagView
//{
//	const FEnumSchemaFlagView* Schema;
//	uint64 Value;
//};

//////////////////////////////////////////////////////////////////////////

class FMemberReader;
class FRangeLoader;
class FStructRangeIterator;
class FNestedRangeIterator;

template<typename IteratorType>
class TStructuralRangeView;

class FLeafRangeView;
using FStructRangeView = TStructuralRangeView<FStructRangeIterator>;
using FNestedRangeView = TStructuralRangeView<FNestedRangeIterator>;

struct FRangeSchema
{
	FMemberType				ItemType;
	FReadBatchId			Batch; // Needed to resolve inner schema
	FOptionalSchemaId		InnermostSchema;
	const FMemberType*		NestedItemTypes; // For nested ranges, can be out-of-bounds otherwise
};

class FRangeView
{
public:
	uint64							Num() const				{ return NumItems;}
	bool							IsEmpty() const			{ return NumItems == 0; }
	bool							IsLeafRange() const		{ return Schema.ItemType.GetKind() == EMemberKind::Leaf; }
	bool							IsStructRange() const	{ return Schema.ItemType.GetKind() == EMemberKind::Struct; }
	bool							IsNestedRange() const	{ return Schema.ItemType.GetKind() == EMemberKind::Range; }

	PLAINPROPS_API FLeafRangeView	AsLeaves() const;		// @pre IsLeafRange()
	PLAINPROPS_API FStructRangeView	AsStructs() const;		// @pre IsStructRange()
	PLAINPROPS_API FNestedRangeView	AsRanges() const;		// @pre IsNestedRange()

private:
	friend FMemberReader;
	friend FNestedRangeIterator;
	friend FRangeLoader;

	FRangeSchema					Schema;
	uint64							NumItems;
	FMemoryView						Values;
};

//////////////////////////////////////////////////////////////////////////

class FBoolRangeIterator
{
public:
	FBoolRangeIterator(const uint8* Data, uint64 Idx)
	: Byte(Data + Idx / 8)
	, Mask(1u << (Idx % 8))
	{}

	bool operator*() const
	{
		return !!((*Byte) & Mask);
	}

	FBoolRangeIterator& operator++()
	{
		Mask <<= 1;
		if (Mask == 0x100)
		{
			++Byte;
			Mask = 1;
		}
		return *this;
	}

	bool operator!=(FBoolRangeIterator Rhs)
	{
		return Byte != Rhs.Byte || Mask != Rhs.Mask;
	}

private:
	const uint8* Byte;
	uint32 Mask;
};

class FBoolRangeView
{
public:
	FBoolRangeView(const uint8* InData, uint64 InNum)
	: Data(InData)
	, NumBits(InNum)
	{}

	uint64 Num() const					{ return NumBits;}
	bool operator[](uint64 Idx) const	{ return *FBoolRangeIterator(Data, Idx); }
	FBoolRangeIterator begin() const	{ return FBoolRangeIterator(Data, 0); }
	FBoolRangeIterator end() const		{ return FBoolRangeIterator(Data, NumBits); }

private:
	const uint8* Data;
	uint64 NumBits;

	friend inline uint64 GetNum(const FBoolRangeView& Range) { return Range.NumBits; }
};

//////////////////////////////////////////////////////////////////////////

template<typename T>
class TRangeView
{
public:
	TRangeView(const T* InData, uint64 InNum)
	: Data(InData)
	, NumItems(InNum)
	{}

	uint64 Num() const					{ return NumItems;}
	T operator[](uint64 Idx) const		{ check(Idx < NumItems); return Data[Idx]; }
	const T* begin() const				{ return Data; }
	const T* end() const				{ return Data + NumItems; }
private:
	const T* Data;
	uint64 NumItems;
	
	friend inline uint64 GetNum(const TRangeView& Range) { return Range.NumItems; }
	friend inline const T* GetData(const TRangeView& Range) { return Range.Data; }
};

//  Works with FBoolRangeView, which lack GetData(), TRangeView, TArrayView, std::intializer_list and T[]
template<class RangeTypeA, class RangeTypeB>
bool EqualItems(RangeTypeA&& A, RangeTypeB&& B)
{
	if (static_cast<uint64>(GetNum(A)) != static_cast<uint64>(GetNum(B)))
	{
		return false;
	}

	auto BIt = std::begin(B);
	for (auto bA : A)
	{
		if (bA != *BIt++)
		{
			return false;
		}
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////

class FLeafRangeView
{
public:
	ELeafType				GetType() const;
	SIZE_T					GetSizeOf() const;
	uint64					Num() const			{ return NumItems; }
	uint64					NumBytes() const	{ return NumItems * GetSizeOf(); }
	
	// These ranges hide the internal representations so we can change format in the future, 
	// e.g. store zeroes or 1.0f in some compact fashion or even var int encodings
	//
	// They could also provide various conversion helpers

	FBoolRangeView			AsBools() const
	{
		check(Type == ELeafType::Bool);
		return FBoolRangeView(Values, NumItems);
	}
	
	TRangeView< int8>		AsS8s() const		{ return As< int8>(); }
	TRangeView<uint8>		AsU8s() const		{ return As<uint8>(); }
	TRangeView< int16>		AsS16s() const		{ return As< int16>(); }
	TRangeView<uint16>		AsU16s() const		{ return As<uint16>(); }
	TRangeView< int32>		AsS32s() const		{ return As< int32>(); }
	TRangeView<uint32>		AsU32s() const		{ return As<uint32>(); }
	TRangeView< int64>		AsS64s() const		{ return As< int64>(); }
	TRangeView<uint64>		AsU64s() const		{ return As<uint64>(); }
	TRangeView<float>		AsFloats() const	{ return As<float>(); }
	TRangeView<double>		AsDoubles() const	{ return As<double>(); }
	TRangeView<char8_t>		AsUtf8() const		{ return As<char8_t>(); }
	TRangeView<char16_t>	AsUtf16() const		{ return As<char16_t>(); }
	TRangeView<char32_t>	AsUtf32() const		{ return As<char32_t>(); }
	
	template<Arithmetic T>
	TRangeView<T> As() const
	{
		static_assert(!std::is_same_v<T, bool>);
		check(FUnpackedLeafType(Type, Width) == ReflectArithmetic<T>);
		return TRangeView<T>(reinterpret_cast<const T*>(Values), NumItems);
	}

	template<Enumeration T>
	TRangeView<T> As() const
	{
		check(FUnpackedLeafType(Type, Width) == ReflectEnum<T>);
		return TRangeView<T>(reinterpret_cast<const T*>(Values), NumItems);
	}

	//FEnumFlatRangeView		AsFlatEnums();	// @pre GetType() == ELeafType::Enum
	//FEnumFlagRangeView		AsFlagEnums();	// @pre GetType() == ELeafType::Enum && 

	//// Option B Memcopying + copy conversion apis
	//
	//void					ConvertBits(TArrayView<bool> Out);
	//void					ConvertBits(TArrayView<uint8> Out);
	//void					ConvertBits(TArrayView<uint16> Out);
	//void					ConvertBits(TArrayView<uint32> Out);
	//void					ConvertBits(TArrayView<uint64> Out);

	//template<typename T>
	//void					CopyExact(TArrayView<T> Out);

	//// E.g. IntCastChecked, double -> float ok but not float -> double if it loses precision
	////						uint64 -> uint32 ok if all 
	//template<typename T>
	//bool					TryConvertLossless(TArrayView<uint64> Out);

	////  
	//template<typename T>
	//bool					TryConvert(TArrayView<T> Out);

	//template<typename T>
	//void					CopyStaticCast(TArrayView<T> Out);


private:
	friend class FRangeView;

	ELeafType						Type;
	ELeafWidth						Width;
	FReadBatchId					Batch;
	FOptionalEnumSchemaId			Enum;
	uint64							NumItems;
	const uint8*					Values;
};

//////////////////////////////////////////////////////////////////////////

class FNestedRangeIterator
{
	friend FNestedRangeView;
	FRangeSchema			Schema;
	FByteReader				ByteIt;
	FBitCacheReader			BitIt;

public:
	FNestedRangeIterator(const FRangeSchema& InSchema, FMemoryView Data)
	: Schema(InSchema) 
	, ByteIt(Data)
	{}

	PLAINPROPS_API FRangeView	operator*() const;
	bool						operator!=(const FNestedRangeIterator& Rhs) const { return ByteIt.Peek() != Rhs.ByteIt.Peek(); };
	PLAINPROPS_API void			operator++();
};

class FStructRangeIterator
{
	friend FStructRangeView;
	FStructSchemaHandle		Schema;
	FByteReader				ByteIt;

public:
	FStructRangeIterator(const FStructSchemaHandle& InSchema, FMemoryView Data)
	: Schema(InSchema) 
	, ByteIt(Data)
	{}

	FStructView operator*() const { return { Schema, FByteReader(ByteIt.PeekSkippableSlice()) }; }
	bool operator!=(const FStructRangeIterator& Rhs) const { return ByteIt.Peek() != Rhs.ByteIt.Peek(); };
	void operator++() { (void)ByteIt.GrabSkippableSlice(); }
};

template<typename IteratorType>
class TStructuralRangeView
{
public:
	using SchemaType = decltype(IteratorType::Schema);

	uint64			Num() const		{ return NumItems; }
	SchemaType		GetSchema() const { return Schema; }

	IteratorType	begin() const	{ return IteratorType(Schema, Data); }
	IteratorType	end() const		{ return IteratorType(Schema, FMemoryView(Data.GetDataEnd(), 0)); }

private:
	friend class FRangeView;

	uint64				NumItems;
	FMemoryView			Data;
	SchemaType			Schema;
};

//////////////////////////////////////////////////////////////////////////

//class FAnyMemberView
//{
//public:
//	EMemberKind				GetKind() const			{ return Type.Kind; };
//
//	bool					IsLeaf() const			{ return Type.Kind == EMemberKind::Leaf; }
//	bool					IsStruct() const		{ return Type.Kind == EMemberKind::Struct; }
//	bool					IsRange() const			{ return Type.Kind == EMemberKind::Range; }
//
//	FStructView				AsStruct() const;		// @pre bool() && IsStruct()
//	FLeafView				AsLeaf() const;			// @pre bool() && IsLeaf()
//	FRangeView				AsRange() const;		// @pre bool() && IsRange()
//
//private:
//	friend class FMemberReader;
//
//	FMemberType			Type;
//	uint32				BatchId; // To resolve struct schema
//	FMemberValue		Value;
//};

// Iterates over struct members
class FMemberReader
{
public:
	explicit FMemberReader(FStructView Struct) : FMemberReader(Struct.Schema.Resolve(), Struct.Values, Struct.Schema.Batch) {}

	bool					HasMore() const			{ return MemberIdx < NumMembers; }
	
	FOptionalMemberId		PeekName() const;		// @pre HasMore()
	FOptionalMemberId		PeekNameUnchecked() const; // @pre HasMore()
	EMemberKind				PeekKind() const;		// @pre HasMore()
	FMemberType				PeekType() const;		// @pre HasMore()

	FLeafView				GrabLeaf();				// @pre PeekKind() == EMemberKind::Leaf
	FRangeView				GrabRange();			// @pre PeekKind() == EMemberKind::Range
	FStructView				GrabStruct();			// @pre PeekKind() == EMemberKind::Struct
	//FAnyMemberView		GrabAny();				// @pre HasMore()

	// Experimental 
	// @pre Has N more contiguous members of the expected leaf type
	template<Arithmetic T>
	void					GrabLeaves(T* Out, uint32 N);

	// Experimental
	// @pre Has N more contiguous members of the expected leaf type
	template<Enumeration T>
	void					GrabEnums(T* Out, uint32 N);

protected: // for unit tests
	const FMemberType*		Footer;
	const FReadBatchId		Batch;					// Needed to resolve schemas
	const bool				IsSparse : 1;
	const bool				HasSuper : 1;
	const uint32			NumMembers;
	const uint32			NumRangeTypes;			// Number of ranges and nested ranges

	uint32					MemberIdx = 0;
	uint32					RangeTypeIdx = 0;		// Types of [nested] ranges
	uint32					InnerSchemaIdx = 0;		// Types of static structs and enums
	FBitCacheReader			Bits;
	FByteReader				ValueIt;

#if DO_CHECK
	const uint32			NumInnerSchemas;		// Number of static structs and enums
#endif

	FMemberReader(const FStructSchema& Schema, FByteReader Values, FReadBatchId InBatch);

	const FMemberType*		GetMemberTypes() const;
	const FMemberType*		GetRangeTypes() const;
	const FSchemaId*		GetInnerSchemas() const;
	const FMemberId*		GetMemberNames() const;
	
	void					AdvanceToNextMember();
	void					AdvanceToLaterMember(uint32 Num);
	void					SkipMissingSparseMembers();
	void					SkipSchema(FMemberType InnermostType);

	using FMemberTypeRange = TConstArrayView<FMemberType>;

	FMemberTypeRange		GrabRangeTypes();
	FSchemaId				GrabInnerSchema();
	FStructSchemaId			GrabStructSchema(FStructType Type);
	FOptionalSchemaId		GrabRangeSchema(FMemberType InnermostType);
	inline FEnumSchemaId	GrabEnumSchema()		{ return static_cast<FEnumSchemaId&&>(GrabInnerSchema()); }
	inline bool				GrabBit()				{ return Bits.GrabNext(/* in-out */ ValueIt); }
	inline uint64			GrabSkipLength()		{ return ValueIt.GrabVarIntU(); }

	void					GrabBools(void* Out, uint32 Num);
	void					GrabEnums(void* Out, uint32 Num, SIZE_T NumBytes);
	void					GrabLeaves(void* Out, uint32 Num, SIZE_T NumBytes);
};

template<Arithmetic T>
void FMemberReader::GrabLeaves(T* Out, uint32 N)
{
	if (N)
	{
		if constexpr (std::is_same_v<bool, T>)
		{
			GrabBools(Out, N);
		}
		else
		{
			GrabLeaves(Out, N, sizeof(T));
		}
	}
}

template<Enumeration T>
void FMemberReader::GrabEnums(T* Out, uint32 N)
{
	if (N)
	{
		GrabLeaves(Out, N, sizeof(T));
		InnerSchemaIdx += N;
	}
}

//////////////////////////////////////////////////////////////////////////

// Hides the inheritance chain and iterates over super members first
class FFlatMemberReader
{
public:
	FFlatMemberReader(FStructView Struct);

	bool					HasMore() const			{ return It->HasMore(); }

	FMemberId				PeekName() const		{ return It->PeekName().Get(); }
	EMemberKind				PeekKind() const		{ return It->PeekKind(); }
	FTypeId					PeekOwner() const		{ return It->Owner; }

	FLeafView				GrabLeaf()				{ return Grabbed(It->GrabLeaf()); }
	FRangeView				GrabRange()				{ return Grabbed(It->GrabRange()); }
	FStructView				GrabStruct()			{ return Grabbed(It->GrabStruct()); }

private:
	struct FReader : public FMemberReader
	{
		FReader(FStructView Struct);
		FTypeId Owner;
	};
	TArray<FReader, TInlineAllocator<8>> Lineage;
	FReader* It = nullptr;

	template<class ViewType>
	ViewType Grabbed(ViewType Out)
	{
		It -= (It != &Lineage[0] && !It->HasMore());
		return Out;
	}
};

} // namespace PlainProps