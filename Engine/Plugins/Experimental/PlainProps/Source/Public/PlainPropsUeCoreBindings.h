// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Set.h"
#include "Math/Quat.h"
#include "Math/Vector.h"
#include "Math/Transform.h"
#include "Misc/Optional.h"
#include "PlainPropsBind.h"
#include "PlainPropsBuild.h" // FMemberSchema for TSetDeltaBinding::SaveSet
#include "PlainPropsIndex.h"
#include "PlainPropsLoad.h"
#include "PlainPropsRead.h"
#include "PlainPropsStringUtil.h"
#include "UObject/NameTypes.h"


PP_REFLECT_STRUCT_TEMPLATE(, TTuple, void, Key, Value); // Todo handle TTuple and higher arities

namespace UE::Math
{
PP_REFLECT_STRUCT(, FVector, void, X, Y, Z);
PP_REFLECT_STRUCT(, FVector4, void, X, Y, Z, W);
PP_REFLECT_STRUCT(, FQuat, void, X, Y, Z, W);
}

template <typename T>
struct TIsContiguousContainer<PlainProps::TRangeView<T>>
{
	static inline constexpr bool Value = true;
};

namespace PlainProps::UE
{

//class FReflection
//{
//	TIdIndexer<FName>	Names;
//	FRuntime			Types;
//
//public:
//	TIdIndexer<FName>&		GetIds() { return Names; }

	//template<typename Ctti>
	//FStructSchemaId			BindStruct();

	//template<typename Ctti>
	//FStructSchemaId			BindStructInterlaced(TConstArrayView<FMemberBinding> NonCttiMembers);
	//FStructSchemaId			BindStruct(FStructSchemaId Id, const ICustomBinding& Custom);
	//FStructSchemaId			BindStruct(FTypeId Type, FOptionalSchemaId Super, TConstArrayView<FNamedMemberBinding> Members, EMemberPresence Occupancy);
	//void					DropStruct(FStructSchemaId Id) { Types.DropStruct(Id); }

	//template<typename Ctti>
	//FEnumSchemaId			BindEnum();
	//FEnumSchemaId			BindEnum(FTypeId Type, EEnumMode Mode, ELeafWidth Width, TConstArrayView<FEnumerator> Enumerators);
	//void					DropEnum(FEnumSchemaId Id) { Types.DropEnum(Id); }
//};
//
//PLAINPROPS_API FReflection GReflection;
//
//struct FIds
//{
//	static FMemberId		IndexMember(FAnsiStringView Name)			{ return GReflection.GetIds().NameMember(FName(Name)); }
//	static FTypenameId		IndexTypename(FAnsiStringView Name)			{ return GReflection.GetIds().MakeTypename(FName(Name)); }
//	static FScopeId			IndexCurrentModule()						{ return GReflection.GetIds().MakeScope(FName(UE_MODULE_NAME)); }
//	static FTypeId			IndexNativeType(FAnsiStringView Typename)	{ return {IndexCurrentModule(), IndexTypename(Typename)}; }
//	static FEnumSchemaId	IndexEnum(FTypeId Name)						;
//	static FEnumSchemaId	IndexEnum(FAnsiStringView Name)				;
//	static FStructSchemaId	IndexStruct(FTypeId Name)					;
//	static FStructSchemaId	IndexStruct(FAnsiStringView Name)			;
//	
//};

// todo: use generic cached instance template?
//template<class Ids>
//FScopeId GetModuleScope()
//{
//	static FScopeId Id = Ids::IndexScope(UE_MODULE_NAME);
//	return Id;
//}

//template<class Ctti>
//class TBindRtti
//{
//	FSchemaId Id;
//public:
//	TBindRtti() : Id(BindRtti<Ctti, FIds>(GReflection.GetTypes()))
//	{}
//
//	~TBindRtti()
//	{
//		if constexpr (std::is_enum_v<Ctti::Type>)
//		{
//			GReflection.DropEnum(static_cast<FEnumSchemaId>(Id));
//		}
//		else
//		{
//			GReflection.DropStruct(static_cast<FStructSchemaId>(Id));
//		}
//	}
//};

} // namespace PlainProps::UE

//#define UEPP_BIND_STRUCT(T) 
	
//////////////////////////////////////////////////////////////////////////
// Below container bindings should be moved to some suitable header
//////////////////////////////////////////////////////////////////////////

#include "Containers/Array.h"
#include "Containers/Set.h"
#include "Templates/UniquePtr.h"

namespace PlainProps::UE
{

template <typename T, class Allocator>
struct TArrayBinding : public IItemRangeBinding
{
	using SizeType = int32;
	using ItemType = T;
	using ArrayType = TArray<T, Allocator>;

	virtual void MakeItems(FLoadRangeContext& Ctx) const override
	{
		ArrayType& Array = Ctx.Request.GetRange<ArrayType>();
		if constexpr (std::is_default_constructible_v<T>)
		{
			Array.SetNum(Ctx.Request.NumTotal());
		}
		else
		{
			Array.SetNumUninitialized(Ctx.Request.NumTotal());
			Ctx.Items.SetUnconstructed();
		}
		
		Ctx.Items.Set(Array.GetData(), Ctx.Request.NumTotal());
	}

	virtual void ReadItems(FSaveRangeContext& Ctx) const override
	{
		const ArrayType& Array = Ctx.Request.GetRange<ArrayType>();
		Ctx.Items.SetAll(Array.GetData(), static_cast<uint64>(Array.Num()));
	}
};

//////////////////////////////////////////////////////////////////////////

struct FStringBinding : public ILeafRangeBinding
{
	using SizeType = int32;
	using ItemType = char8_t;

	virtual void SaveLeaves(const void* Range, FLeafRangeAllocator& Out) const override
	{
		const TArray<TCHAR>& Src = static_cast<const FString*>(Range)->GetCharArray();
		int32 SrcLen = Src.Num() - 1;
		if (SrcLen <= 0)
		{
		}
		else if constexpr (sizeof(TCHAR) == sizeof(char8_t))
		{
			char8_t* Utf8 = Out.AllocateRange<char8_t>(SrcLen);
			FMemory::Memcpy(Utf8, Src.GetData(), SrcLen);
		}
		else
		{
			int32 Utf8Len = FPlatformString::ConvertedLength<UTF8CHAR>(Src.GetData(), SrcLen);
			char8_t* Utf8 = Out.AllocateRange<char8_t>(Utf8Len);
			UTF8CHAR* Utf8End = FPlatformString::Convert(reinterpret_cast<UTF8CHAR*>(Utf8), Utf8Len, Src.GetData(), SrcLen);	
			check((char8_t*)Utf8End - Utf8 == Utf8Len);
		}
	}

	virtual void LoadLeaves(void* Range, FLeafRangeLoadView Items) const override
	{
		TArray<TCHAR>& Dst = static_cast<FString*>(Range)->GetCharArray();
		TRangeView<char8_t> Utf8 = Items.As<char8_t>();
		const UTF8CHAR* Src = reinterpret_cast<const UTF8CHAR*>(Utf8.begin());
		int32 SrcLen = static_cast<int32>(Utf8.Num());
		if (SrcLen == 0)
		{
			Dst.Reset();
		}
		else if constexpr (sizeof(TCHAR) == sizeof(char8_t))
		{
			Dst.SetNum(SrcLen + 1);
			FMemory::Memcpy(Dst.GetData(), Src, SrcLen);
			Dst[SrcLen] = '\0';	
		}
		else
		{
			int32 DstLen = FPlatformString::ConvertedLength<TCHAR>(Src, SrcLen);
			Dst.SetNum(DstLen + 1);
			TCHAR* DstEnd = FPlatformString::Convert(Dst.GetData(), DstLen, Src, SrcLen);
			check(DstEnd - Dst.GetData() == DstLen);
			*DstEnd = '\0';
		}
	}

	virtual int64 DiffLeaves(const void* RangeA, const void* RangeB) const override
	{
		const FString& A = *static_cast<const FString*>(RangeA);
		const FString& B = *static_cast<const FString*>(RangeB);
		int32 ALen = A.Len();
		int32 BLen = B.Len();

		if (int32 LenDiff = ALen - BLen)
		{
			return LenDiff;
		}

		// Case-sensitive comparison
		return ALen ? FMemory::Memcmp(A.GetCharArray().GetData(), B.GetCharArray().GetData(), ALen * sizeof(TCHAR)) : 0;
	}
};

//////////////////////////////////////////////////////////////////////////

template <typename T>
struct TUniquePtrBinding : public IItemRangeBinding
{
	using SizeType = bool;
	using ItemType = T;

	virtual void MakeItems(FLoadRangeContext& Ctx) const override
	{
		TUniquePtr<T>& Ptr = Ctx.Request.GetRange<TUniquePtr<T>>();
		
		if (Ctx.Request.NumTotal() == 0)
		{
			Ptr.Reset();
			return;
		}
		
		if (!Ptr)
		{
			if constexpr (std::is_default_constructible_v<T>)
			{
				Ptr.Reset(new T);
			}
			else
			{
				Ptr.Reset(reinterpret_cast<T*>(FMemory::Malloc(sizeof(T), alignof(T))));
				Ctx.Items.SetUnconstructed();
			}
		}
		
		Ctx.Items.Set(Ptr.Get(), 1);
	}

	virtual void ReadItems(FSaveRangeContext& Ctx) const override
	{
		const TUniquePtr<T>& Ptr = Ctx.Request.GetRange<TUniquePtr<T>>();
		Ctx.Items.SetAll(Ptr.Get(), Ptr ? 1 : 0);
	}
};

//////////////////////////////////////////////////////////////////////////

template <typename T>
struct TOptionalBinding : public IItemRangeBinding
{
	using SizeType = bool;
	using ItemType = T;

	virtual void MakeItems(FLoadRangeContext& Ctx) const override
	{
		TOptional<T>& Opt = Ctx.Request.GetRange<TOptional<T>>();
		
		if (Ctx.Request.NumTotal() == 0)
		{
			Opt.Reset();
		}
		else if constexpr (std::is_default_constructible_v<T>)
		{
			if (!Opt)
			{
				Opt.Emplace();
			}
			Ctx.Items.Set(reinterpret_cast<T*>(&Opt), 1);
		}
		else if (Opt)
		{
			Ctx.Items.Set(reinterpret_cast<T*>(&Opt), 1);
		}
		else
		{
			if (Ctx.Request.IsFirstCall())
			{
				Ctx.Items.SetUnconstructed();
				Ctx.Items.RequestFinalCall();
				Ctx.Items.Set(reinterpret_cast<T*>(&Opt), 1);	
			}
			else
			{
				// Move-construct from self reference
				Opt.Emplace(reinterpret_cast<T&&>(Opt));
			}
		}
	}

	virtual void ReadItems(FSaveRangeContext& Ctx) const override
	{
		const TOptional<T>& Opt = Ctx.Request.GetRange<TOptional<T>>();
		check(!Opt || reinterpret_cast<const T*>(&Opt) == &Opt.GetValue());
		Ctx.Items.SetAll(reinterpret_cast<const T*>(Opt ? &Opt : nullptr), Opt ? 1 : 0);
	}
};

//////////////////////////////////////////////////////////////////////////

template <typename T, typename KeyFuncs, typename SetAllocator>
struct TSetBinding : public IItemRangeBinding
{
	using SizeType = int32;
	using ItemType = T;
	using SetType = TSet<T, KeyFuncs, SetAllocator>;

	virtual void MakeItems(FLoadRangeContext& Ctx) const override
	{
		SetType& Set = Ctx.Request.GetRange<SetType>();
		SizeType Num = static_cast<SizeType>(Ctx.Request.NumTotal());

		static constexpr bool bAllocate = sizeof(T) > sizeof(FLoadRangeContext::Scratch);
		static constexpr uint64 MaxItems = bAllocate ? 1 : sizeof(FLoadRangeContext::Scratch) / SIZE_T(sizeof(T));
		
		if (Ctx.Request.IsFirstCall())
		{
			Set.Reset();

			if (uint64 NumRequested = Ctx.Request.NumTotal())
			{
				Set.Reserve(NumRequested);

				// Create temporary buffer
				uint64 NumTmp = FMath::Min(MaxItems, NumRequested);
				void* Tmp = bAllocate ? FMemory::Malloc(sizeof(T)) : Ctx.Scratch;
				Ctx.Items.Set(Tmp, NumTmp, sizeof(T));
				if constexpr (std::is_default_constructible_v<T>)
				{
					for (T* It = static_cast<T*>(Tmp), *End = It + NumTmp; It != End; ++It)
					{
						::new (It) T;
					}
				}
				else
				{
					Ctx.Items.SetUnconstructed();
				}

				Ctx.Items.RequestFinalCall();
			}
		}
		else
		{
			// Add items that have been loaded
			TArrayView<T> Tmp = Ctx.Items.Get<T>();
			for (T& Item : Tmp)
			{
				Set.Emplace(MoveTemp(Item));
			}

			if (Ctx.Request.IsFinalCall())
			{
				// Destroy and free temporaries
				uint64 NumTmp = FMath::Min(MaxItems, Ctx.Request.NumTotal());
				for (T& Item : MakeArrayView(Tmp.GetData(), NumTmp))
				{
					Item.~T();
				}
				if constexpr (bAllocate)
				{
					FMemory::Free(Tmp.GetData());
				}	
			}
			else
			{
				Ctx.Items.Set(Tmp.GetData(), FMath::Min(static_cast<uint64>(Tmp.Num()), Ctx.Request.NumMore()));
				check(Ctx.Items.Get<T>().Num());
			}
		}
	}

	virtual void ReadItems(FSaveRangeContext& Ctx) const override
	{
		static_assert(offsetof(TSetElement<T>, Value) == 0);
		const TSparseArray<TSetElement<T>>& Elems = Ctx.Request.GetRange<TSparseArray<TSetElement<T>>>();

		if (FExistingItemSlice LastRead = Ctx.Items.Slice)
		{
			// Continue partial response
			const TSetElement<T>* NextElem = static_cast<const TSetElement<T>*>(LastRead.Data) + LastRead.Num + /* skip known invalid */ 1;
			Ctx.Items.Slice = GetContiguousSlice(Elems.PointerToIndex(NextElem), Elems);
		}
		else if (Elems.IsCompact())
		{
			int32 Num = Elems.Num();
			Ctx.Items.SetAll(Num ? &Elems[0] : nullptr, Num);
		}
		else
		{
			// Start partial response
			Ctx.Items.NumTotal = Elems.Num();
			Ctx.Items.Stride = sizeof(TSetElement<T>);
			Ctx.Items.Slice = GetContiguousSlice(0, Elems);
		}
	}

	static FExistingItemSlice GetContiguousSlice(int32 Idx, const TSparseArray<TSetElement<T>>& Elems)
	{
		int32 Num = 1;
		for (;!Elems.IsValidIndex(Idx); ++Idx) {}
		for (; Elems.IsValidIndex(Idx + Num); ++Num) {}
		return { &Elems[Idx], static_cast<uint64>(Num) };
	}
};

//////////////////////////////////////////////////////////////////////////

template <typename K, typename V, typename SetAllocator, typename KeyFuncs>
struct TMapBinding : public TSetBinding<TPair<K, V>, KeyFuncs, SetAllocator>
{};

//////////////////////////////////////////////////////////////////////////

//TODO: Consider macroifying parts of this, e.g PP_CUSTOM_BIND(PLAINPROPS_API, FTransform, Transform, Translate, Rotate, Scale)
struct FTransformBinding : ICustomBinding
{
	using Type = FTransform;
	inline static constexpr EMemberPresence Occupancy = EMemberPresence::AllowSparse;
	enum class EMember : uint8 { Translate, Rotate, Scale };
	const FMemberId MemberIds[3];
	const FStructSchemaId VectorId;
	const FStructSchemaId QuatId;

	template<class Ids>
	FTransformBinding(TCustomInit<Ids>/*, const FDeclarations& Declared*/)
	: MemberIds{Ids::IndexMember("Translate"), Ids::IndexMember("Rotate"), Ids::IndexMember("Scale")}
	, VectorId(GetStructDeclId<Ids, FVector>())
	, QuatId(GetStructDeclId<Ids, FQuat>())
	{}

	PLAINPROPS_API void	Save(FMemberBuilder& Dst, const FTransform& Src, const FTransform* Default, const FSaveContext& Context) const;
	PLAINPROPS_API void	Load(FTransform& Dst, FStructView Src, ECustomLoadMethod Method, const FLoadBatch& Batch) const;
	inline static bool	Diff(const FTransform& A, const FTransform& B) { return !A.Equals(B, 0.0); }
};

//////////////////////////////////////////////////////////////////////////

struct FSetDeltaIds
{
	enum class EMember : uint8 { Del, Add };
	const FMemberId MemberIds[2];
	
	template<class Ids>	
	FSetDeltaIds(TCustomInit<Ids>)
	: MemberIds{Ids::IndexMember("Del"), Ids::IndexMember("Add")}
	{}

	template<class Ids>	
	static FSetDeltaIds Cache()
	{
		static FSetDeltaIds Out(TCustomInit<Ids>{});
		return Out;
	}
};

template <typename T, typename KeyFuncs, typename SetAllocator>
struct TSetDeltaBinding : ICustomBinding, FSetDeltaIds
{
	using Type = TSet<T, KeyFuncs, SetAllocator>;
	using FSet = Type;
	using FSetBinding = TSetBinding<T, KeyFuncs, SetAllocator>;
	using FRangeMemberHelper = TRangeMemberHelper<FSetBinding>;
	static constexpr EMemberPresence Occupancy = EMemberPresence::AllowSparse;
	static constexpr uint16 NumInnerRanges = FRangeMemberHelper::NumRanges - 1;

	struct FCustomTypename
	{
		inline static constexpr std::string_view DeclName = "SetDelta";
		inline static constexpr std::string_view BindName = Concat<DeclName, ShortTypename<KeyFuncs>, ShortTypename<SetAllocator>>;
		inline static constexpr std::string_view Namespace;
		using Parameters = std::tuple<T>;
	};

	FRangeMemberHelper InnerRange;
	
	template<class Ids>	
	TSetDeltaBinding(TCustomInit<Ids>)
	: FSetDeltaIds(FSetDeltaIds::Cache<Ids>())
	{
		InnerRange.template Init<Ids>();
	}
	
	void Save(FMemberBuilder& Dst, const FSet& Src, const FSet* Default, const FSaveContext& Context) const
	{
		if (Default && !Default->IsEmpty())
		{
			// Inefficient, mimic FSetProperty::SerializeItem for better perf
			SaveSet(Dst, MemberIds[(uint8)EMember::Del], Default->Difference(Src), Context);
			SaveSet(Dst, MemberIds[(uint8)EMember::Add], Src.Difference(*Default), Context);
		}
		else if (Src.Num())
		{
			SaveSet(Dst, MemberIds[(uint8)EMember::Add], Src, Context);
		}
	}

	void SaveSet(FMemberBuilder& Dst, FMemberId Name, const FSet& Set, const FSaveContext& Ctx) const
	{
		if (int32 Num = Set.Num())
		{
			// Start of a more optimized version that saves typed values directly w/o leaning on TSetBinding
			//
			//FBuiltRange* Range;
			//if constexpr (LeafType<T>)
			//{
			//	Range = FBuiltRange::Create(Ctx.Scratch, Num, sizeof(T));
			//	T* OutIt = static_cast<T*>(Range->Data);
			//	//const TSparseArray<TSetElement<T>& Elements = reinterpret_cast<const TSparseArray<TSetElement<T>>&>(Values);
			//	//if (Elements.IsCompact())
			//	//{
			//	//	FMemory::Memcpy(Range->Data, Elements.GetData(), sizeof(T) * Num);
			//	//}
			//	//else 
			//	for (T Leaf : Set)
			//	{
			//		*OutIt++ = Leaf;
			//	}
			//}
			//else 
			//{
			//	Range = SaveRange(&Set, InnerRange.MakeBinding(/* offset*/ 0), Ctx);
			//}

			// Lean on TSetBinding for now, less efficient but simpler
			FBuiltRange* Range = SaveRange(&Set, InnerRange.MakeBinding(/* offset*/ 0), Ctx);
			FMemberSchema Schema = MakeNestedRangeSchema(InnerRange.MaxSize, InnerRange.InnerSchemaTypes, InnerRange.InnermostSchema);
			Dst.AddRange(Name, { Schema, Range });	
		}

	}

	inline void Load(FSet& Dst, FStructView Src, ECustomLoadMethod Method, const FLoadBatch& Batch) const
	{
		FMemberReader Members(Src);

		if (Method == ECustomLoadMethod::Construct)
		{
			::new (&Dst) FSet;
		}
				
		if (Members.HasMore())
		{
			if (Members.PeekName() == MemberIds[(uint8)EMember::Add])
			{
				ApplyItems<EMember::Add>(Dst, Members.GrabRange(), Batch);
			}
			else
			{
				check(Members.PeekName() == MemberIds[(uint8)EMember::Del]);
				ApplyItems<EMember::Del>(Dst, Members.GrabRange(), Batch);
		
				if (Members.HasMore())
				{
					check(Members.PeekName() == MemberIds[(uint8)EMember::Add]);
					ApplyItems<EMember::Add>(Dst, Members.GrabRange(), Batch);
				}
			}

			check(!Members.HasMore());
		}
	}

	template<EMember Op>
	void ApplyItems(FSet& Out, FRangeView Items, const FLoadBatch& Batch) const
	{
		check(!Items.IsEmpty());

		if constexpr (Op == EMember::Add && !LeafType<T>)
		{
			Out.Reserve(static_cast<int32>(Items.Num()));
		}

		if constexpr (LeafType<T>)
		{
			ApplyLeaves<Op>(Out, Items.AsLeaves().As<T>());
		}
		else if constexpr (NumInnerRanges)
		{
			ApplyRanges<Op>(Out, Items.AsRanges(), Batch);
		}
		else 
		{
			ApplyStructs<Op>(Out, Items.AsStructs(), Batch);
		}
	}
	
	template<EMember Op>
	void ApplyLeaves(FSet& Out, TRangeView<T> Items) const
	{
		if constexpr (Op == EMember::Add)
		{
			Out.Append(MakeArrayView(Items));
		}
		else
		{
			for (T Item : Items)
			{
				Out.Remove(Item);
			}
		}
	}

	template<EMember Op>
	void ApplyRanges(FSet& Out, FNestedRangeView Items, const FLoadBatch& Batch) const
	{
		static_assert(std::is_default_constructible_v<T>, TEXT("Ranges must be default-constructible"));

		TConstArrayView<FRangeBinding> InnerRangeBindings(InnerRange.RangeBindings + 1, NumInnerRanges);
		for (FRangeView Item : Items)
		{
			T Tmp;
			LoadRange(&Tmp, Item, InnerRange.MaxSize, InnerRangeBindings, Batch);
			ApplyItem<Op>(Out, MoveTemp(Tmp));
		}
	}
	
	template<EMember Op>
	void ApplyStructs(FSet& Out, FStructRangeView Items, const FLoadBatch& Batch) const
	{
		for (FStructView Item : Items)
		{
			if constexpr (std::is_default_constructible_v<T>)
			{
				T Tmp;
				LoadStruct(&Tmp, Item, Batch);
				ApplyItem<Op>(Out, MoveTemp(Tmp));
			}
			else
			{
				alignas(T) uint8 Buffer[sizeof(T)];
				ConstructAndLoadStruct(Buffer, Item, Batch);
				T& Tmp = *reinterpret_cast<T*>(Buffer);
				ApplyItem<Op>(Out, MoveTemp(Tmp));
				Tmp.~T();
			}
		}
	}
	
	template<EMember Op>
	void ApplyItem(FSet& Out, T&& Item) const
	{
		if constexpr (Op == EMember::Add)
		{
			Out.Emplace(MoveTemp(Item));
		}
		else
		{
			Out.Remove(Item);
		}
	}

	inline static bool Diff(const FSet& A, const FSet& B)
	{
		if (A.Num() != B.Num())
		{
			return true;
		}

		for (const T& AKey : A)
		{
			if (!B.Contains(AKey))
			{
				return true;
			}
		}

		return false;
	}
};

//	template <typename T, typename VariantType>
//	struct TVariantConstructFromMember
//	{
//		/** Default construct the type and load it from the FArchive */
//		static void Construct(VariantType& Dst, FMemberReader Src)
//		{
//			if constexpr (std::is_arithmetic_v<T>)
//			{
//				Dst.template Emplace<T>(Src.GrabLeaf().As<T>());
//			}
//			else constexpr 
//
//		}
//	};
//
//template <typename... Ts>
//struct TVariantBinding : ICustomBinding
//{
//	using VariantType = TVariant<Ts...>;
//
//	const FStructDeclaration& Declaration;
//
//	static constexpr void(*Loaders[])(FMemberReader&, VariantType&) = { &TLoader<Ts, VariantType>::Load... };
//
//	virtual void LoadStruct(void* Dst, FStructView Src, ECustomLoadMethod Method, const FLoadBatch& Batch) const override
//	{
//		VariantType& Variant = *reinterpret_cast<VariantType*>(Dst);
//
//		if (Method == ECustomLoadMethod::Assign)
//		{
//			Variant.~VariantType();
//		}
//
//		FMemberReader Members(Src);
//		const FMemberId* DeclaredName = Algo::Find(Declaration.Names, Members.PeekName());
//		check(DeclaredName);
//		int64 Idx = DeclaredName - &Declaration.Names[0];
//
//		check(TypeIndex < UE_ARRAY_COUNT(Loaders));
//		Loaders[TypeIndex](Ar, OutVariant);		
//		
//	}
//
//	template<typename T>
//	void Load(TVariant<Ts...>& Dst, FCustomMemberLoader& Src, ECustomLoadMethod Method)
//	{
//
//		
//		{
//			Dst.Emplace(MoveTemp(*reinterpret_cast<T*>(Temp)));
//		}
//		else
//		{
//			new (Dst) TVariant<Ts...>(MoveTemp(*reinterpret_cast<T*>(Temp)));
//		}
//	}
//
//	virtual FBuiltStruct*	SaveStruct(const void* Src, const FDebugIds& Debug) const override
//	{
//		...
//	}
//};

} // namespace PlainProps::UE

namespace PlainProps
{

template <>
struct TTypename<FName>
{
	inline static constexpr std::string_view DeclName = "Name";
	inline static constexpr std::string_view Namespace;
};

template <>
struct TTypename<FTransform>
{
	inline static constexpr std::string_view DeclName = "Transform";
	inline static constexpr std::string_view Namespace;
};

template <typename K, typename V>
struct TTypename<TPair<K,V>>
{
	inline static constexpr std::string_view DeclName = "Pair";
	inline static constexpr std::string_view Namespace;
	using Parameters = std::tuple<K, V>;
};

template <>
struct TTypename<FString>
{
	inline static constexpr std::string_view RangeBindName = "String";
	inline static constexpr std::string_view Namespace;
};

inline static constexpr std::string_view UeArrayName = "Array";
inline static constexpr std::string_view UeSetName = "Set";
inline static constexpr std::string_view UeMapName = "Map";

template<typename T, typename Allocator>
struct TTypename<TArray<T, Allocator>>
{
	inline static constexpr std::string_view RangeBindName = Concat<UeArrayName, ShortTypename<Allocator>>;
	inline static constexpr std::string_view Namespace;
};

template<>
struct TShortTypename<FDefaultAllocator> : FOmitTypename {};
template<>
struct TShortTypename<FDefaultSetAllocator> : FOmitTypename {};
template<typename T>
struct TShortTypename<DefaultKeyFuncs<T, false>> : FOmitTypename {};
template<typename K, typename V>
struct TShortTypename<TDefaultMapHashableKeyFuncs<K, V, false>> : FOmitTypename {};

inline constexpr std::string_view InlineAllocatorPrefix = "InlX";
template<int N>
struct TShortTypename<TInlineAllocator<N>>
{
	inline static constexpr std::string_view Value = Concat<InlineAllocatorPrefix, HexString<N>>;
};

template<int N>
struct TShortTypename<TInlineSetAllocator<N>> : TShortTypename<TInlineAllocator<N>> {};

template <typename T, typename KeyFuncs, typename SetAllocator>
struct TTypename<TSet<T, KeyFuncs, SetAllocator>>
{
	inline static constexpr std::string_view RangeBindName = Concat<UeSetName, ShortTypename<KeyFuncs>, ShortTypename<SetAllocator>>;
	inline static constexpr std::string_view Namespace;
};

template <typename K, typename V, typename SetAllocator, typename KeyFuncs>
struct TTypename<TMap<K, V, SetAllocator, KeyFuncs>>
{
	inline static constexpr std::string_view RangeBindName = Concat<UeMapName, ShortTypename<SetAllocator>, ShortTypename<KeyFuncs>>;
	inline static constexpr std::string_view Namespace;
};

template<>
PLAINPROPS_API void AppendString(FString& Out, const FName& Name);

template<typename T, typename Allocator>
struct TRangeBind<TArray<T, Allocator>>
{
	using Type = UE::TArrayBinding<T, Allocator>;
};

template<>
struct TRangeBind<FString>
{
	using Type = UE::FStringBinding;
};

template<typename T>
struct TRangeBind<TUniquePtr<T>>
{
	using Type = UE::TUniquePtrBinding<T>;
};

template <typename T, typename KeyFuncs, typename SetAllocator>
struct TRangeBind<TSet<T, KeyFuncs, SetAllocator>>
{
	using Type = UE::TSetBinding<T, KeyFuncs, SetAllocator>;
};

template <typename K, typename V, typename SetAllocator, typename KeyFuncs>
struct TRangeBind<TMap<K, V, SetAllocator, KeyFuncs>>
{
	using Type = UE::TMapBinding<K, V, SetAllocator, KeyFuncs>;
};

template<typename T>
struct TRangeBind<TOptional<T>>
{
	using Type = UE::TOptionalBinding<T>;
};

template<>
struct TCustomBind<FTransform>
{
	using Type = UE::FTransformBinding;
};

} // namespace PlainProps