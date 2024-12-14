// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlainPropsDeclare.h"
#include "PlainPropsTypes.h"
#include "Algo/Compare.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Memory/MemoryView.h"
#include "Templates/UniquePtr.h"

namespace PlainProps
{

struct FBuiltMember;
struct FBuiltStruct;
struct FBuiltRange;
class FDebugIds;
struct FUnpackedLeafType;

//////////////////////////////////////////////////////////////////////////

/// Single-threaded scratch allocator for intermediate built representation
class FScratchAllocator
{
	struct FPage
	{
		FPage*			PrevPage;
		uint8			Data[0];
	};

	static constexpr uint32 PageSize = 65536;
	static constexpr uint32 DataSize = PageSize - offsetof(FPage, Data);
	
	uint8*					Cursor = nullptr;
	uint8*					PageEnd = nullptr;
	FPage*					LastPage = nullptr;

	PLAINPROPS_API uint8* AllocateInNewPage(SIZE_T Size, uint32 Alignment);

public:
	UE_NONCOPYABLE(FScratchAllocator);
	FScratchAllocator() = default;
	PLAINPROPS_API ~FScratchAllocator();

	inline void* Allocate(SIZE_T Size, uint32 Alignment)
	{
		uint8* Out = Align(Cursor, Alignment);
		if (Out + Size <= PageEnd)
		{
			Cursor = Out + Size;
			return Out;
		}

		return AllocateInNewPage(Size, Alignment);
	}

	inline void* AllocateZeroed(SIZE_T Size, uint32 Alignment)
	{
		void* Out = Allocate(Size, Alignment);
		FMemory::Memzero(Out, Size);
		return Out;
	}

	template<typename T>
	inline T* AllocateArray(uint64 Num)
	{
		T* Out = static_cast<T*>(Allocate(Num * sizeof(T), alignof(T)));
		for (uint64 Idx = 0; Idx < Num; ++Idx)
		{
			new (Out + Idx) T;
		}
		return Out;
	}
};

//////////////////////////////////////////////////////////////////////////

struct FMemberSchema
{
	FMemberType						Type;
	FMemberType						InnerRangeType;
	uint16							NumInnerRanges;
	FOptionalSchemaId				InnerSchema;
	const FMemberType*				NestedRangeTypes;

	TConstArrayView<FMemberType> GetInnerRangeTypes() const
	{
		return MakeArrayView(NestedRangeTypes ? NestedRangeTypes : &InnerRangeType, NumInnerRanges);
	}

	FMemberType GetInnermostType() const
	{
		return NumInnerRanges ? GetInnerRangeTypes().Last() : Type;
	}

	[[nodiscard]] PLAINPROPS_API FMemberType& EditInnermostType(FScratchAllocator& Scratch);

	void CheckInvariants()
	{
		check(Type.IsRange() == !!NumInnerRanges);
		check(!!NestedRangeTypes == (NumInnerRanges > 1));
	}
};

inline bool operator==(FMemberSchema A, FMemberSchema B)
{
	if (FMemory::Memcmp(&A, &B, sizeof(FMemberSchema)) == 0)
	{
		return true;
	}

	return A.Type == B.Type && A.InnerSchema == B.InnerSchema && Algo::Compare(A.GetInnerRangeTypes(), B.GetInnerRangeTypes());
}
//////////////////////////////////////////////////////////////////////////

inline uint64 ValueCast(bool Value)			{ return static_cast<uint64>(Value); }
inline uint64 ValueCast(int8 Value)			{ return static_cast<uint8>(Value); }
inline uint64 ValueCast(int16 Value)		{ return static_cast<uint16>(Value); }
inline uint64 ValueCast(int32 Value)		{ return static_cast<uint32>(Value); }
inline uint64 ValueCast(int64 Value)		{ return static_cast<uint64>(Value); }
inline uint64 ValueCast(uint8 Value)		{ return Value; }
inline uint64 ValueCast(uint16 Value)		{ return Value; }
inline uint64 ValueCast(uint32 Value)		{ return Value; }
inline uint64 ValueCast(uint64 Value)		{ return Value; }
uint64 ValueCast(float Value);
uint64 ValueCast(double Value);
inline uint64 ValueCast(char8_t Value)		{ return static_cast<uint8>(Value); }
inline uint64 ValueCast(char16_t Value)		{ return static_cast<uint16>(Value); }
inline uint64 ValueCast(char32_t Value)		{ return static_cast<uint32>(Value); }

//////////////////////////////////////////////////////////////////////////

// Todo: Turn into smart pointer where dtor call FBuiltRange::Delete if Values not detached to FBuiltValue
struct FTypedRange
{
	FMemberSchema Schema;
	FBuiltRange* Values = nullptr;
};

template<Arithmetic T, typename SizeType>
FMemberSchema MakeLeafRangeSchema()
{
	return { FMemberType(RangeSizeOf(SizeType{})), ReflectArithmetic<T>.Pack(), 1, NoId, nullptr };
}

template<Enumeration T, typename SizeType>
FMemberSchema MakeEnumRangeSchema(FEnumSchemaId Schema)
{
	return { FMemberType(RangeSizeOf(SizeType{})), ReflectEnum<T>.Pack(), 1, Schema, nullptr };
}

inline constexpr FMemberType DefaultStructType = FMemberType(FStructType{EMemberKind::Struct, /* IsDynamic */ 0, /* IsSuper */ 0});
inline constexpr FMemberType SuperStructType =	 FMemberType(FStructType{EMemberKind::Struct, /* IsDynamic */ 0, /* IsSuper */ 1});


inline FMemberSchema MakeStructRangeSchema(ERangeSizeType SizeType, FStructSchemaId Schema)
{
	return { FMemberType(SizeType), DefaultStructType, 1, Schema, nullptr };
}

PLAINPROPS_API FMemberSchema MakeNestedRangeSchema(FScratchAllocator& Scratch, ERangeSizeType SizeType, FMemberSchema InnerRangeSchema);

// @param InnerTypes must outlive FMemberSchema
template<uint16 N>
inline FMemberSchema MakeNestedRangeSchema(ERangeSizeType SizeType, const FMemberType (&InnerTypes)[N], FOptionalSchemaId InnermostSchema)
{
	return { FMemberType(SizeType), InnerTypes[0], N, InnermostSchema, N > 1 ? InnerTypes : nullptr };
}

//////////////////////////////////////////////////////////////////////////

[[nodiscard]] PLAINPROPS_API FBuiltRange* CloneLeaves(FScratchAllocator& Scratch, uint64 Num, const void* Data, SIZE_T LeafSize);

template<LeafType T, typename SizeType>
[[nodiscard]] FTypedRange BuildLeafRange(FScratchAllocator& Scratch, const T* Values, SizeType Num)
{
	// todo: detect invalid floats
	return { MakeLeafRangeSchema<T, SizeType>(), CloneLeaves(Scratch, Num, Values, sizeof(T)) };
}

template<LeafType T, typename SizeType>
[[nodiscard]] FTypedRange BuildLeafRange(FScratchAllocator& Scratch, TConstArrayView<T, SizeType> Values)
{
	// todo: detect invalid floats
	return { MakeLeafRangeSchema<T, SizeType>(), CloneLeaves(Scratch, Values.Num(), Values.GetData(), sizeof(T)) };
}

template<Enumeration T, typename SizeType>
[[nodiscard]] FTypedRange BuildEnumRange(FScratchAllocator& Scratch, FEnumSchemaId Enum, TConstArrayView<T, SizeType> Values)
{
	return { MakeEnumRangeSchema<T, SizeType>(Enum), CloneLeaves(Scratch, Values.Num(), Values.GetData(), sizeof(T)) };
}

[[nodiscard]] inline FTypedRange MakeStructRange(FStructSchemaId Schema, ERangeSizeType SizeType, FBuiltRange* Values )
{
	return { MakeStructRangeSchema(SizeType, Schema), Values };
}

//////////////////////////////////////////////////////////////////////////

union FBuiltValue
{
	uint64			Leaf;
	FBuiltStruct*	Struct;
	FBuiltRange*	Range;
};

struct FBuiltMember
{
	FBuiltMember(FMemberId Name, FUnpackedLeafType Leaf, FOptionalEnumSchemaId Schema, uint64 Value);
	FBuiltMember(FMemberId Name, FTypedRange Range);
	FBuiltMember(FMemberId Name, FStructSchemaId Schema, FBuiltStruct* Value);
	FBuiltMember(FOptionalMemberId N, FMemberSchema S, FBuiltValue V) : Name(N), Schema(MoveTemp(S)), Value(V) {}
	static FBuiltMember MakeSuper(FStructSchemaId Schema, FBuiltStruct* Value);

	FOptionalMemberId		Name;
	FMemberSchema			Schema;
	FBuiltValue				Value;
};

//////////////////////////////////////////////////////////////////////////

// Builds an ordered list of properties to be saved
class FMemberBuilder
{
public:
	template<Arithmetic T>
	void Add(FMemberId Name, T Value)
	{
		AddLeaf(Name, ReflectArithmetic<T>, {}, ValueCast(Value));
	}

	template<Enumeration T>
	void AddEnum(FMemberId Name, FEnumSchemaId Schema, T Value)
	{
		AddLeaf(Name, ReflectEnum<T>, ToOptional(Schema), ValueCast(Value));
	}
	
	void AddEnum8(FMemberId Name, FEnumSchemaId Schema, uint8 Value)	{ AddLeaf(Name, {ELeafType::Enum, ELeafWidth::B8},  ToOptional(Schema), Value); }
	void AddEnum16(FMemberId Name, FEnumSchemaId Schema, uint16 Value)	{ AddLeaf(Name, {ELeafType::Enum, ELeafWidth::B16}, ToOptional(Schema), Value); }
	void AddEnum32(FMemberId Name, FEnumSchemaId Schema, uint32 Value)	{ AddLeaf(Name, {ELeafType::Enum, ELeafWidth::B32}, ToOptional(Schema), Value); }
	void AddEnum64(FMemberId Name, FEnumSchemaId Schema, uint64 Value)	{ AddLeaf(Name, {ELeafType::Enum, ELeafWidth::B64}, ToOptional(Schema), Value); }

	void AddLeaf(FMemberId Name, FUnpackedLeafType Leaf, FOptionalEnumSchemaId Enum, uint64 Value)	{ Members.Emplace(Name, Leaf, Enum, Value); }
	void AddRange(FMemberId Name, FTypedRange Range)												{ Members.Emplace(Name, Range); }
	void AddStruct(FMemberId Name, FStructSchemaId Schema, FBuiltStruct* Struct)					{ Members.Emplace(Name, Schema, Struct); }
	
	// Build members into a single nested super struct member, no-op if no non-super members has been added
	PLAINPROPS_API void BuildSuperStruct(FScratchAllocator&	Scratch, const FStructDeclaration& Super, const FDebugIds& Debug);

	[[nodiscard]] PLAINPROPS_API FBuiltStruct* BuildAndReset(FScratchAllocator& Scratch, const FStructDeclaration& Declared, const FDebugIds& Debug);

	bool IsEmpty() const { return Members.IsEmpty(); }

private:
	using FBuiltMemberArray = TArray<FBuiltMember, TInlineAllocator<16>>;
	
	FBuiltMemberArray		Members;
	
	//template<typename T>
	//void NormalizeLeafRange(T*, uint64) {}
	//PLAINPROPS_API void NormalizeLeafRange(float*, uint64 Num);
	//PLAINPROPS_API void NormalizeLeafRange(double*, uint64 Num);
};

// Rough API draft
struct FDenseMemberBuilder
{
	FScratchAllocator& Scratch;
	const FDebugIds& Debug;

	template<typename T, typename... Ts>
	[[nodiscard]] FBuiltStruct* BuildHomogeneous(const FStructDeclaration& Declaration, T Head, Ts... Tail) const
	{
		// Todo: Handle enums, ranges and structs
		FBuiltValue Values[] = { {.Leaf = ValueCast(Head)}, {.Leaf = (ValueCast(Tail))}...  };
		return BuildHomo(Declaration, FMemberType(ReflectArithmetic<T>.Pack()), Values);
	}

private:
	[[nodiscard]] PLAINPROPS_API FBuiltStruct* BuildHomo(const FStructDeclaration& Declaration, FMemberType Leaf, TConstArrayView<FBuiltValue> Values) const;
};

// Helper class for building struct ranges
class FStructRangeBuilder
{
public:
	FStructRangeBuilder(uint64 Num, ERangeSizeType InSizeType)
	: SizeType(InSizeType)
	{
		Structs.SetNum(Num);
	}

	template<typename IntType>
	explicit FStructRangeBuilder(IntType Num)
	: FStructRangeBuilder(static_cast<uint64>(Num), RangeSizeOf(Num))
	{}
	 
	FMemberBuilder& operator[](uint64 Idx) { return Structs[Idx]; }

	FTypedRange BuildAndReset(FScratchAllocator& Scratch, const FStructDeclaration& Declared, const FDebugIds& Debug);

private:
	TArray64<FMemberBuilder> Structs; 
	ERangeSizeType SizeType;
};


// Helper class for building nested ranges
class FNestedRangeBuilder
{
public:
	FNestedRangeBuilder(FMemberSchema InSchema, int64 InitialReserve)
	: Schema(InSchema)
	{
		Ranges.Reserve(InitialReserve);
	}

	~FNestedRangeBuilder();

	void Add(FTypedRange Range)
	{
		check(Range.Values == nullptr || Range.Schema == Schema);
		Ranges.Add(Range.Values);
	}

	[[nodiscard]] FTypedRange BuildAndReset(FScratchAllocator& Scratch, ERangeSizeType SizeType);

private:
	TArray64<FBuiltRange*> Ranges; 
	FMemberSchema Schema;
};

} // namespace PlainProps