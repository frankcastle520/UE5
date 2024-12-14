// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlainPropsLoad.h"
#include "PlainPropsBind.h"
#include "PlainPropsInternalFormat.h"
#include "PlainPropsInternalRead.h"
#include "Containers/Set.h"
#include "Misc/Optional.h"
#include <type_traits>

namespace PlainProps
{

struct FLoadStructMemcpy
{
	uint32			Size;
	uint32			Offset;
};

// Describes how to load a saved struct into the matching in-memory representation
class FLoadStructPlan
{
public:
	FLoadStructPlan() = default;

	explicit FLoadStructPlan(FLoadStructMemcpy Memcpy)
	: Handle((uint64(Memcpy.Size) << 32) | (uint64(Memcpy.Offset) << 2) | MemcpyMask)
	{
		check(Memcpy.Offset == AsMemcpy().Offset && Memcpy.Size == AsMemcpy().Size);
	}

	explicit FLoadStructPlan(const ICustomBinding& Custom)
	: Handle(uint64(&Custom) | CustomMask)
	{
		check(&Custom == &AsCustom());
	}

	// @param OffsetWidth Usage unimplemented, store size and offsets as 8/16/32/64-bit 
	explicit FLoadStructPlan(const FSchemaBinding& Schema, ELeafWidth OffsetWidth, bool bSparse)
	: Handle(uint64(&Schema) | SchemaBit | (bSparse ? SparseBit : 0) | (uint64(OffsetWidth) << SchemaOffsetShift))
	{
		static_assert(alignof(FSchemaBinding) >= 8);
		check(&Schema == &AsSchema());
		check(bSparse == IsSparseSchema());
		check(OffsetWidth == GetOffsetWidth());
	}

	bool						IsSchema() const		{ return (Handle & SchemaBit) == SchemaBit; }
	bool						IsSparseSchema() const	{ return (Handle & SparseSchemaMask) == SparseSchemaMask; }
	bool						IsMemcpy() const		{ return (Handle & LoMask) == MemcpyMask; }
	bool						IsCustom() const		{ return (Handle & LoMask) == CustomMask; }
	FLoadStructMemcpy			AsMemcpy() const		{ check(IsMemcpy()); return { static_cast<uint32>(Handle >> 32), static_cast<uint32>(Handle) >> 1 }; }
	const ICustomBinding&		AsCustom() const		{ check(IsCustom()); return *AsPtr<ICustomBinding>(); }
	const FSchemaBinding&		AsSchema() const		{ check(IsSchema()); return *AsPtr<FSchemaBinding>(); }

	ELeafWidth					GetOffsetWidth() const	{ check(IsSchema()); return static_cast<ELeafWidth>((Handle & SchemaOffsetMask) >> SchemaOffsetShift); }

private:
	static constexpr uint64 SparseBit			= uint64(1) << FPlatformMemory::KernelAddressBit;
	static constexpr uint64 PtrMask				= ~(SparseBit | 0b111);
	static constexpr uint64 LoMask				= 0b11;
	static constexpr uint64 MemcpyMask			= 0b00;
	static constexpr uint64 CustomMask			= 0b10;
	static constexpr uint64 SchemaBit			= 0b01;
	static constexpr uint64 SparseSchemaMask	= SchemaBit | SparseBit;
	static constexpr uint64 SchemaOffsetShift	= 1;
	static constexpr uint64 SchemaOffsetMask	= 0b110;
	
	template<typename T>
	const T* AsPtr() const
	{
		check(Handle & PtrMask);
		return reinterpret_cast<T*>(Handle & PtrMask);
	}

	uint64						Handle = 0;
};

////////////////////////////////////////////////////////////////////////////

static uint16 CountEnums(const FStructSchema& Schema)
{
	if (Schema.NumInnerSchemas == 0)
	{
		return 0;
	}
	
	uint16 Num = 0;
	TConstArrayView<FMemberType> RangeTypes = Schema.GetRangeTypes(); 
	if (RangeTypes.IsEmpty())
	{
		for (FMemberType Member : Schema.GetMemberTypes())
		{
			Num += IsEnum(Member);
		}
		return Num;
	}
	
	uint16 RangeTypeIdx = 0;
	for (FMemberType Member : Schema.GetMemberTypes())
	{
		if (Member.IsRange())
		{
			FMemberType InnermostType = GrabInnerRangeTypes(RangeTypes, /* in-out */ RangeTypeIdx).Last();
			Num += IsEnum(InnermostType);
		}
		else
		{
			Num += IsEnum(Member);
		}
	}
	check(RangeTypeIdx == Schema.NumRangeTypes);
	return Num;
}

static bool HasDifferentSupers(const FStructSchema& From, const FSchemaBinding& To, TConstArrayView<FStructSchemaId> ToStructIds)
{
	if (From.Inheritance == ESuper::No)
	{
		return To.HasSuper();
	}
	else if (To.HasSuper())
	{
		FStructSchemaId FromSuper = ToStructIds[From.GetSuperSchema().Get().Idx];
		FStructSchemaId ToSuper = static_cast<FStructSchemaId>(To.GetInnerSchemas()[0]);
		return FromSuper == ToSuper;
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////

// Used to create an additional load plan, beyond the saved struct schema ids
struct FLoadIdMapping
{
	FStructSchemaId ReadId; // ~ Batch decl id, index into saved schemas and load plans
	FStructSchemaId LoadId; // ~ Batch bind id, index into load plans
	FStructSchemaId BindId; // Runtime bind id
};

// Helps load type-erased structs with ExplicitBindName by allocating new load-time struct ids
class FLoadIdBinder
{
public:
	FLoadIdBinder(TConstArrayView<FStructSchemaId> RuntimeDeclIds)
	: DeclIds(RuntimeDeclIds)
	, NextLoadIdx(static_cast<uint32>(RuntimeDeclIds.Num()))
	{}

	FStructSchemaId					BindLoadId(FStructSchemaId ReadId, FStructSchemaId BindId)
	{
		FStructSchemaId DeclId = DeclIds[ReadId.Idx];
		return BindId == DeclId ? ReadId : MapLoadId(ReadId, BindId);
	}
	FLoadIdMapping					GetMapping(int32 Idx) const { return Mappings[Idx]; }
	int32							NumMappings() const { return Mappings.Num(); }

private:
	TConstArrayView<FStructSchemaId>	DeclIds;
	uint32								NextLoadIdx;
	TArray<FLoadIdMapping>				Mappings;

	FStructSchemaId MapLoadId(FStructSchemaId ReadId, FStructSchemaId BindId)
	{
		for (FLoadIdMapping Mapping : Mappings)
		{
			if (Mapping.BindId == BindId)
			{
				check(Mapping.ReadId == ReadId);
				return Mapping.LoadId;
			}
		}

		FLoadIdMapping& Mapping = Mappings.AddDefaulted_GetRef();
		Mapping.ReadId = ReadId;
		Mapping.LoadId = { NextLoadIdx++ };
		Mapping.BindId = BindId;
		return Mapping.LoadId;
	}
};

////////////////////////////////////////////////////////////////////////////

struct FLoadBatch
{
	FReadBatchId			ReadId; // Needed to access schemas for custom struct loading
	uint32					NumReadSchemas;
	uint32					NumPlans;
	FLoadStructPlan			Plans[0];

	FLoadStructPlan			operator[](FStructSchemaId LoadId) const
	{
		check(LoadId.Idx < NumPlans);
		return Plans[LoadId.Idx];
	}

	FStructSchemaId			GetReadId(FStructSchemaId LoadId) const
	{
		check(LoadId.Idx < NumPlans);
		static_assert(alignof(FLoadStructPlan) >= alignof(FStructSchemaId));
		const FStructSchemaId* SaveIds = reinterpret_cast<const FStructSchemaId*>(Plans + NumPlans);
		return LoadId.Idx < NumReadSchemas ? LoadId : SaveIds[LoadId.Idx - NumReadSchemas];
	}
};

void FLoadBatchDeleter::operator()(FLoadBatch* Batch) const
{
	FMemory::Free(Batch);
}

using SubsetByteArray = TArray<uint8, TInlineAllocator<1024>>;

static void CopyMemberBinding(FLeafMemberBinding Binding, /* in-out */ const FSchemaId*& InnerSchemaIt, FMemberBinder& Out)
{
	InnerSchemaIt += (Binding.Leaf.Type == ELeafBindType::Enum); // Skip enum schema
	Out.AddMember(Binding.Leaf.Pack(), static_cast<uint32>(Binding.Offset));
}

// TODO: Make all below free functions FLoadPlanner member functions and make LoadIds a member variable

static void CopyMemberBinding(FStructMemberBinding Binding, /* in-out */ const FSchemaId*& InnerSchemaIt, FLoadIdBinder& LoadIds, FMemberBinder& Out)
{
	FStructSchemaId LoadId = LoadIds.BindLoadId(static_cast<FStructSchemaId>(*InnerSchemaIt), Binding.Id);
	Out.AddMember(FMemberBindType(Binding.Type), static_cast<uint32>(Binding.Offset));
	Out.AddInnerSchema(LoadId);
	++InnerSchemaIt;
}

static void CopyMemberBinding(FRangeMemberBinding Binding, /* in-out */ const FSchemaId*& InnerSchemaIt, FLoadIdBinder& LoadIds, FMemberBinder& Out)
{
	FMemberBindType InnermostType = Binding.InnerTypes[Binding.NumRanges - 1];
	Out.AddRange(MakeArrayView(Binding.RangeBindings, Binding.NumRanges), InnermostType, static_cast<uint32>(Binding.Offset));
	if (InnermostType.IsStruct())
	{
		FStructSchemaId LoadId = LoadIds.BindLoadId(static_cast<FStructSchemaId>(*InnerSchemaIt), static_cast<FStructSchemaId>(Binding.InnermostSchema.Get()));
		Out.AddInnerSchema(LoadId);
		++InnerSchemaIt;
	}
	else
	{
		InnerSchemaIt += (InnermostType.AsLeaf().Bind.Type == ELeafBindType::Enum); // Skip enum schema
	}
}

static void CopyMemberBinding(/* in-out */ FMemberVisitor& BindIt, /* in-out */ const FSchemaId*& InnerSchemaIt, FLoadIdBinder& LoadIds, FMemberBinder& Out)
{
	switch (BindIt.PeekKind())
	{
		case EMemberKind::Leaf:		CopyMemberBinding(BindIt.GrabLeaf(), InnerSchemaIt, Out);				break;
		case EMemberKind::Range:	CopyMemberBinding(BindIt.GrabRange(), InnerSchemaIt, LoadIds, Out);		break;
		case EMemberKind::Struct:	CopyMemberBinding(BindIt.GrabStruct(), InnerSchemaIt, LoadIds, Out);	break;
		default:					check(false);															break;
	}
}

static void CreateSubsetBindingWithoutEnumIds(const FStructSchema& From, const FSchemaBinding& To, TConstArrayView<FMemberId> ToNames, uint16 NumEnums, FLoadIdBinder& LoadIds, SubsetByteArray& Out)
{
	check(To.NumMembers == ToNames.Num());
	check(To.NumMembers >= From.NumMembers);
	check(IsAligned(Out.Num(), alignof(FSchemaBinding)));

	int32 OutPos = Out.Num();
	
	// Allocate and init header 
	FSchemaBinding Header = { To.DeclId, From.NumMembers, From.NumInnerSchemas - NumEnums, From.NumRangeTypes };
	Out.AddUninitialized(Header.CalculateSize());
	FSchemaBinding* Schema = new (&Out[OutPos]) FSchemaBinding {Header};
	
	// Copy subset of member bindings
	FMemberVisitor ToIt(To);
	FMemberBinder Footer(*Schema);
	const FSchemaId* InnerSchemaIt = From.GetInnerSchemas();
	for (FMemberId FromName : From.GetMemberNames())
	{
		while (FromName != ToNames[ToIt.GetIndex()])
		{
			ToIt.SkipMember();
		}

		CopyMemberBinding(/* in-out */ ToIt, /* in-out */ InnerSchemaIt, /* in-out */ LoadIds,  /* out */ Footer);	
	}
	check(InnerSchemaIt == From.GetInnerSchemas() + From.NumInnerSchemas);
}

static void CloneBindingWithReplacedStructIds(const FSchemaId* FromIds, const FSchemaBinding& To, FLoadIdBinder& LoadIds, SubsetByteArray& Out)
{
	check(IsAligned(Out.Num(), alignof(FSchemaBinding)));

	// Clone runtime FSchemaBinding including inner schema bind ids
	uint32 Size = To.CalculateSize();
	Out.AddUninitialized(Size);
	FSchemaBinding* Schema = reinterpret_cast<FSchemaBinding*>(&Out[Out.Num() - Size]);
	FMemory::Memcpy(Schema, &To, Size);

	// Replace inner schema bind ids with batch load ids
	const FStructSchemaId* SaveIdIt = static_cast<const FStructSchemaId*>(FromIds);
	FStructSchemaId* OutIds = static_cast<FStructSchemaId*>(const_cast<FSchemaId*>(Schema->GetInnerSchemas()));
	for (FStructSchemaId& OutId : MakeArrayView(OutIds, To.NumInnerSchemas))
	{
		FStructSchemaId MemcopiedBindId = OutId;
		OutId = LoadIds.BindLoadId(*SaveIdIt++, MemcopiedBindId);
	}
}

[[nodiscard]] static FLoadStructPlan MakeSchemaLoadPlan(const FStructSchema& From, const FSchemaBinding& To, TConstArrayView<FMemberId> ToMemberIds, TConstArrayView<FStructSchemaId> ToStructIds, FLoadIdBinder& LoadIds, SubsetByteArray& OutSubsetSchemas)
{
	uint16 NumEnums = CountEnums(From);
	if (From.NumMembers < To.NumMembers || NumEnums || HasDifferentSupers(From, To, ToStructIds))
	{
		CreateSubsetBindingWithoutEnumIds(From, To, ToMemberIds, NumEnums, LoadIds, OutSubsetSchemas);
	}
	else
	{
		check(From.NumMembers == To.NumMembers);
		check(From.NumInnerSchemas == To.NumInnerSchemas);
		check(From.NumRangeTypes == To.NumInnerRanges);

		if (From.NumInnerSchemas > 0)
		{
			CloneBindingWithReplacedStructIds(From.GetInnerSchemas(), To, LoadIds, OutSubsetSchemas);
		}
		// else reuse existing bindings
	}

	// Pointer to created subset load schema will be remapped later
	return FLoadStructPlan(To, ELeafWidth::B32, !From.IsDense);
}

[[nodiscard]] static TOptional<FLoadStructMemcpy> TryMakeMemcpyPlan(const FStructSchema& From, const FSchemaBinding& To, TConstArrayView<FMemberId> ToMemberIds)
{
	// todo
	return NullOpt;
}

[[nodiscard]] static FLoadStructPlan MakeLoadPlan(const FStructSchema& From, const FSchemaBinding& To, TConstArrayView<FMemberId> ToMemberIds, TConstArrayView<FStructSchemaId> ToStructIds, FLoadIdBinder& LoadIds, SubsetByteArray& OutSubsetSchemas)
{
	TOptional<FLoadStructMemcpy> Memcpy = TryMakeMemcpyPlan(From, To, ToMemberIds);
	return Memcpy ? FLoadStructPlan(Memcpy.GetValue()) : MakeSchemaLoadPlan(From, To, ToMemberIds, ToStructIds, LoadIds, OutSubsetSchemas);
}

struct FMissingBinding : ICustomBinding
{
	FMissingBinding(const TCHAR* InType) : Type(InType) {}
	const TCHAR* Type;
	virtual void SaveCustom(FMemberBuilder& Dst, const void* Src, const void* Default, const FSaveContext& Ctx) override { checkf(false, TEXT("Can't save %s"), Type); }
	virtual void LoadCustom(void* Dst, FStructView Src, ECustomLoadMethod Method, const FLoadBatch& Batch) const override { checkf(false, TEXT("Can't load %s"), Type); }
	virtual bool DiffCustom(const void* StructA, const void* StructB) const override { checkf(false, TEXT("Can't diff %s"), Type); return false; }
};
static FMissingBinding GMemberlessBinding(TEXT("Memberless struct binding"));
static FMissingBinding GUnboundBinding{TEXT("Type-erased struct binding")};

struct FLoadPlanner
{
	// Constructor parameters
	const FReadBatchId								ReadId;
	const FDeclarations&							Declarations;
	const FCustomBindings&							Customs;
	const FSchemaBindings&							Schemas;
	const TConstArrayView<FStructSchemaId>			RuntimeIds;

	// Temporary data structures	
	TArray<FLoadStructPlan, TInlineAllocator<256>>	Plans;
	TArray<uint32, TInlineAllocator<256>>			SubsetSchemaSizes;
	SubsetByteArray									SubsetSchemaData;
	TSet<FStructSchemaId>							UnboundSaveIds;
	
	FLoadBatchPtr CreatePlans()
	{
		check(NumStructSchemas(ReadId) == RuntimeIds.Num());

		// Make load plans for saved schemas
		const uint32 NumPlans = RuntimeIds.Num();
		Plans.SetNumUninitialized(NumPlans);
		SubsetSchemaSizes.SetNumUninitialized(NumPlans);
		FLoadIdBinder LoadIds(RuntimeIds);
		for (uint32 Idx = 0; Idx < NumPlans; ++Idx)
		{
			FLoadIdMapping Mapping;
			Mapping.ReadId = { Idx };
			Mapping.LoadId = { Idx };
			Mapping.BindId = RuntimeIds[Idx];
			CreatePlan(Mapping, LoadIds);
		}

		// Make load plans for type-erased/ExplicitBindName structs needed by already created load plans
		if (LoadIds.NumMappings() > 0)
		{
			Plans.Reserve(NumPlans + LoadIds.NumMappings());
			SubsetSchemaSizes.Reserve(NumPlans + LoadIds.NumMappings());
			for (int32 Idx = 0; Idx < LoadIds.NumMappings(); ++Idx)
			{
				check(LoadIds.GetMapping(Idx).LoadId.Idx == static_cast<uint32>(Plans.Num()));
				Plans.AddUninitialized();
				SubsetSchemaSizes.AddUninitialized();
				CreatePlan(LoadIds.GetMapping(Idx), /* might grow */ LoadIds);
			}

			// Verify that all UnboundSaveIds were bound by some load plan
			for (int32 Idx = 0; !UnboundSaveIds.IsEmpty() && Idx < LoadIds.NumMappings(); ++Idx)
			{
				UnboundSaveIds.Remove(LoadIds.GetMapping(Idx).ReadId);
			}
		}

		for (FStructSchemaId UnboundSaveId : UnboundSaveIds)
		{
			checkf(false, TEXT("Unbound struct '%s' can't be loaded"), *Declarations.GetDebug().Print(RuntimeIds[UnboundSaveId.Idx]));
		}

		// Allocate load batch, copy plans and subset schemas, and fixup subset schema plans
		return CreateBatch(LoadIds);
	}
private:

	FLoadBatchPtr CreateBatch(const FLoadIdBinder& LoadIds)
	{
		const uint32 NumPlans = Plans.Num();
		const uint32 NumMappings = LoadIds.NumMappings();
		const uint32 NumReadSchemas = RuntimeIds.Num();
		check(NumPlans == NumReadSchemas + NumMappings);

		// Allocate plan, init header and copy plans
		SIZE_T Bytes = sizeof(FLoadBatch) + sizeof(FLoadStructPlan) * NumPlans
					 + Align(sizeof(FStructSchemaId) * NumMappings, alignof(FSchemaBinding))
					 + SubsetSchemaData.Num();
		FLoadBatch Header = { ReadId, NumReadSchemas, NumPlans };
		FLoadBatch* Out = new (FMemory::Malloc(Bytes)) FLoadBatch{Header};
		FMemory::Memcpy(Out->Plans, Plans.GetData(), sizeof(FLoadStructPlan) * NumPlans);

		// Copy LoadId -> ReadId mapping so custom-bound mapped plans can form FReadSchemaHandle and FStructView
		FStructSchemaId* OutReadId = reinterpret_cast<FStructSchemaId*>(Out->Plans + NumPlans);
		for (uint32 Idx = 0; Idx < NumMappings; ++Idx)
		{
			*OutReadId++ = LoadIds.GetMapping(Idx).ReadId;
			check(OutReadId[-1].Idx < NumReadSchemas);
		}

		// Copy cloned subset schemas and patch up plan -> schema pointers
		if (SubsetSchemaData.Num() > 0)
		{
			void* OutSubsetData = Align(OutReadId, alignof(FSchemaBinding));
			FMemory::Memcpy(OutSubsetData, SubsetSchemaData.GetData(), SubsetSchemaData.Num());
		
			// Update plans with actual subset schema pointers
			const uint8* It = static_cast<const uint8*>(OutSubsetData);
			for (uint32 Idx = 0; Idx < NumPlans; ++Idx)
			{
				if (int32 Size = SubsetSchemaSizes[Idx])
				{
					check(IsAligned(Size, alignof(FSchemaBinding)));
					check(Plans[Idx].IsSchema());
					bool bSparse = Plans[Idx].IsSparseSchema();
					Out->Plans[Idx] = FLoadStructPlan(*reinterpret_cast<const FSchemaBinding*>(It), ELeafWidth::B32, bSparse);
					It += Size;
				}
			}
			check(It == (uint8*)OutSubsetData + SubsetSchemaData.Num());
			check(It == (uint8*)Out + Bytes);
		}

		return FLoadBatchPtr(Out);
	}

	void CreatePlan(FLoadIdMapping Mapping, FLoadIdBinder& LoadIds)
	{
		int32 SubsetSchemaOffset = SubsetSchemaData.Num();	
		Plans[Mapping.LoadId.Idx] = CreatePlanInner(Mapping, LoadIds);
		SubsetSchemaSizes[Mapping.LoadId.Idx] = static_cast<uint32>(SubsetSchemaData.Num() - SubsetSchemaOffset);
	}

	FLoadStructPlan CreatePlanInner(FLoadIdMapping Mapping, FLoadIdBinder& LoadIds)
	{
		if (const ICustomBinding* Custom = Customs.FindStruct(Mapping.BindId))
		{
			return FLoadStructPlan(*Custom);
		}

		const FStructSchema& From = ResolveStructSchema(ReadId, Mapping.ReadId);
		if (From.NumMembers)
		{
			if (const FSchemaBinding* To = Schemas.FindStruct(Mapping.BindId))
			{
				// Possible optimization - some simple memcpy cases doesn't need to resolve the declaration
				TConstArrayView<FMemberId> ToMemberIds = Declarations.Get(To->DeclId).GetMemberOrder();
				return MakeLoadPlan(From, *To, ToMemberIds, RuntimeIds, LoadIds, /* out */ SubsetSchemaData);	
			}
			
			// Type-erased structs
			UnboundSaveIds.Add(Mapping.ReadId);
			return FLoadStructPlan(GUnboundBinding);
		}

//		checkf(!From.Type.Scope && From.Type.Name.NumParameters == 2, TEXT("Unexpected bind id typename '%s' saved as '%s'. Only range-bound template parameters are memberless. "
//				"They're always anonymous two-parameter types and uninstantiable as structs, bound via MakeAnonymousParametricType(). "),
//				*Declarations.GetDebug().Print(From.Type),
//				*Declarations.GetDebug().Print(Mapping.SaveId));
		return FLoadStructPlan(GMemberlessBinding);
	}
};

FLoadBatchPtr CreateLoadPlans(FReadBatchId ReadId, const FDeclarations& Declarations, const FCustomBindings& Customs, const FSchemaBindings& Schemas, TConstArrayView<FStructSchemaId> RuntimeIds)
{
	return FLoadPlanner{ReadId, Declarations, Customs, Schemas, RuntimeIds}.CreatePlans();
}


////////////////////////////////////////////////////////////////////////////

inline static void SetBit(uint8& Out, uint8 Idx, bool bValue)
{
	uint8 Mask = IntCastChecked<uint8>(1 << Idx);
	if (bValue)
	{
		Out |= Mask;
	}
	else
	{
		Out &= Mask;
	}	
}

struct FLoadRangePlan
{
	ERangeSizeType MaxSize;
	FOptionalStructSchemaId InnermostStruct;
	TConstArrayView<FMemberBindType> InnerTypes;
	const FRangeBinding* Bindings = nullptr;

	FLoadRangePlan Tail() const
	{
		return { InnerTypes[0].AsRange().MaxSize, InnermostStruct, InnerTypes.RightChop(1), Bindings + 1 };
	}
};

inline static FMemberBindType ToBindType(FMemberType Member)
{
	switch (Member.GetKind())
	{
	case EMemberKind::Leaf:		return FMemberBindType(Member.AsLeaf());
	case EMemberKind::Range:	return FMemberBindType(Member.AsRange());
	default:					return FMemberBindType(Member.AsStruct());	
	}
}

class FRangeLoader
{
public:
	static void LoadRangeView(uint8* Member, FRangeView Src, ERangeSizeType MaxSize, TConstArrayView<FRangeBinding> Bindings, const FLoadBatch& Batch)
	{
		TArray<FMemberBindType, TFixedAllocator<16>> InnerTypes;
		InnerTypes.Add(ToBindType(Src.Schema.ItemType));
		if (Src.Schema.ItemType.IsRange())
		{
			for (const FMemberType* It = Src.Schema.NestedItemTypes; It; It = It->IsRange() ? (It + 1) : nullptr)
			{
				InnerTypes.Add(ToBindType(*It));
			}
		}
		check(Bindings.Num() == InnerTypes.Num());
		
		FOptionalStructSchemaId StructSchema = InnerTypes.Last().IsStruct() ? static_cast<FOptionalStructSchemaId>(Src.Schema.InnermostSchema) : NoId;
		
		FLoadRangePlan Plan = { MaxSize, StructSchema, InnerTypes, Bindings.GetData() };
		
		FByteReader ByteIt(Src.Values);
		FBitCacheReader BitIt;
		LoadRangePlan(Member, Src.NumItems, ByteIt, BitIt, Batch, Plan);
		ByteIt.CheckEmpty();
	}

	static void LoadRangePlan(uint8* Member, uint64 Num, FByteReader& ByteIt, FBitCacheReader& BitIt, const FLoadBatch& Batch, const FLoadRangePlan& Range)
	{
		FRangeBinding Binding = Range.Bindings[0];
		FMemberBindType InnerType = Range.InnerTypes[0];
		
		if (Binding.IsLeafBinding())
		{
			LoadLeafRange(Member, Num, Binding.AsLeafBinding(), ByteIt, UnpackNonBitfield(InnerType.AsLeaf()));
		}
		else if (Num)
		{
			const IItemRangeBinding& ItemBinding = Binding.AsItemBinding();
			switch (InnerType.GetKind())
			{
				case EMemberKind::Leaf:		LoadRangeValues(Member, Num, ItemBinding, ByteIt, Batch, UnpackNonBitfield(InnerType.AsLeaf())); break;
				case EMemberKind::Range:	LoadRangeValues(Member, Num, ItemBinding, ByteIt, Batch, Range.Tail()); break;
				case EMemberKind::Struct:	LoadRangeValues(Member, Num, ItemBinding, ByteIt, Batch, Range.InnermostStruct.Get()); break;
			}
		}
		else
		{
			FLoadRangeContext NoItemsCtx{.Request = {Member, 0}};
			(Binding.AsItemBinding().MakeItems)(NoItemsCtx);
		}
	}	
	
	static void LoadLeafRange(uint8* Member, uint64 Num, const ILeafRangeBinding& Binding, FByteReader& ByteIt, FUnpackedLeafType Leaf)
	{
		FMemoryView Values = Num ? GrabRangeValues(ByteIt, Num, Leaf) : FMemoryView();
		Binding.LoadLeaves(Member, FLeafRangeLoadView(Values.GetData(), Num, Leaf));
	}

	template<class SchemaType>
	static void LoadRangeValues(uint8* Member, uint64 Num, const IItemRangeBinding& Binding, FByteReader& ByteIt, const FLoadBatch& Batch, SchemaType&& Schema)
	{
		FByteReader ValueIt(GrabRangeValues(ByteIt, Num, Schema));
		FBitCacheReader BitIt; // Only used by ranges of ERangeSizeType::Uni ranges
		FLoadRangeContext Ctx{.Request = {Member, Num}};
		
		while (Ctx.Request.Index < Num)
		{
			(Binding.MakeItems)(Ctx);
			CopyRangeValues(Ctx.Items, ValueIt, BitIt, Batch, Schema);
			Ctx.Request.Index += Ctx.Items.Num;
		}
		ValueIt.CheckEmpty();

		if (Ctx.Items.bNeedFinalize)
		{
			(Binding.MakeItems)(Ctx);
		}
	}

	static FMemoryView GrabRangeValues(FByteReader& ByteIt, uint64 Num, FUnpackedLeafType Leaf)
	{
		check(Num > 0);
		return ByteIt.GrabSlice(GetLeafRangeSize(Num, Leaf));
	}

	template<class SchemaType>
	static FMemoryView GrabRangeValues(FByteReader& ByteIt, uint64, SchemaType&&)
	{
		return ByteIt.GrabSkippableSlice();
	}
		
	static void CopyRangeValues(const FConstructedItems& Items, FByteReader& ByteIt, FBitCacheReader&, const FLoadBatch& Batch, FUnpackedLeafType Leaf)
	{
		check(Items.Size == SizeOf(Leaf.Width));
		if (Leaf.Type != ELeafType::Bool)
		{
			FMemory::Memcpy(Items.Data, ByteIt.GrabBytes(Items.NumBytes()), Items.NumBytes());
		}
		else
		{
			FBoolRangeView Bits(ByteIt.GrabBytes(Align(Items.Num, 8)/8), Items.Num);
			uint8* It = Items.Data;
			for (bool bBit : Bits)
			{
				reinterpret_cast<bool&>(*It++) = bBit;
			}
		}
	}
	
	static void CopyRangeValues(const FConstructedItems& Items, FByteReader& ByteIt, FBitCacheReader&, const FLoadBatch& Batch, FStructSchemaId Id)
	{
		uint64 ItemSize = Items.Size;
		for (uint8* It = Items.Data, *End = It + Items.NumBytes(); It != End; It += ItemSize)
		{
			PlainProps::LoadStruct(It, FByteReader(ByteIt.GrabSkippableSlice()), Id, Batch);
		}
	}
	
	static void CopyRangeValues(const FConstructedItems& Items, FByteReader& ByteIt, FBitCacheReader& BitIt, const FLoadBatch& Batch, const FLoadRangePlan& Plan)
	{
		uint64 ItemSize = Items.Size;
		for (uint8* It = Items.Data, *End = It + Items.NumBytes(); It != End; It += ItemSize)
		{
			uint64 Num = GrabRangeNum(Plan.MaxSize, ByteIt, BitIt);
			LoadRangePlan(It, Num, ByteIt, BitIt, Batch, Plan);
		}
	}
};

////////////////////////////////////////////////////////////////////////////

template<bool bSparse, typename OffsetType>
class TMemberLoader : public FRangeLoader
{
public:
	TMemberLoader(FByteReader Values, const FSchemaBinding& Schema, const FLoadBatch& InBatch)
	: Types(Schema.Members, Schema.NumMembers)
	, Offsets(Schema.GetOffsets())
	, InnerStructSchemas(static_cast<const FStructSchemaId*>(Schema.GetInnerSchemas()), Schema.NumInnerSchemas)
	, InnerRangeTypes(Schema.GetInnerRangeTypes(), Schema.NumInnerRanges)
	, RangeBindings(Schema.GetRangeBindings())
	, Batch(InBatch)
	, ByteIt(Values)
	{}

	void Load(void* Struct)
	{
		SkipMissingSparseMembers();
	
		while (MemberIdx < Types.Num())
		{
			LoadMember(Struct);
			++MemberIdx;
			SkipMissingSparseMembers();
		}
	}

private:
	const TConstArrayView<FMemberBindType>		Types;
	const OffsetType* const						Offsets;
	const TConstArrayView<FStructSchemaId>		InnerStructSchemas;
	const TConstArrayView<FMemberBindType>		InnerRangeTypes;
	const FRangeBinding*						RangeBindings;
	const FLoadBatch&							Batch;
	
	FByteReader ByteIt;
	FBitCacheReader BitIt;
	int32 MemberIdx = 0;
	int32 InnerRangeIdx = 0;
	int32 InnerStructIdx = 0;	

	void SkipMissingSparseMembers()
	{
		// Make code changes in FMemberReader::SkipMissingSparseMembers() too
		while (bSparse && MemberIdx < Types.Num() && BitIt.GrabNext(ByteIt))
		{
			FMemberBindType Type = Types[MemberIdx];
			FMemberBindType InnermostType = Type.IsRange() ? GrabInnerRanges(Type.AsRange()).InnerTypes.Last() : Type;
			if (InnermostType.IsStruct())
			{
				(void)GrabInnerStruct(InnermostType.AsStruct());
			}
			++MemberIdx;
		}
	}

	void LoadMember(void* Struct)
	{
		FMemberBindType Type = Types[MemberIdx];
		uint8* Member = static_cast<uint8*>(Struct) + Offsets[MemberIdx];

		switch (Type.GetKind())
		{
			case EMemberKind::Leaf:		LoadMemberLeaf(Member, Type.AsLeaf()); break;
			case EMemberKind::Range:	LoadMemberRange(Member, GrabInnerRanges(Type.AsRange())); break;
			case EMemberKind::Struct:	LoadMemberStruct(Member, GrabInnerStruct(Type.AsStruct())); break;
		}
	}

	FStructSchemaId GrabInnerStruct(FStructBindType Type)
	{
		return Type.IsDynamic ? FStructSchemaId { ByteIt.Grab<uint32>() } : InnerStructSchemas[InnerStructIdx++];
	}

	FLoadRangePlan GrabInnerRanges(FRangeBindType Type)
	{
		const FRangeBinding* Bindings = RangeBindings + InnerRangeIdx;
		TConstArrayView<FMemberBindType> InnerTypes = GrabInnerRangeTypes(InnerRangeTypes, /* in-out */ InnerRangeIdx);	
		FOptionalStructSchemaId InnermostStruct = InnerTypes.Last().IsStruct() ? ToOptional(GrabInnerStruct(InnerTypes.Last().AsStruct())) : NoId;
		return { Type.MaxSize, InnermostStruct, InnerTypes, Bindings };
	}
	
	void LoadMemberLeaf(uint8* Member, FLeafBindType Leaf)
	{
		switch (Leaf.Bind.Type)
		{
			case ELeafBindType::Bool:
				reinterpret_cast<bool&>(*Member) = BitIt.GrabNext(ByteIt);
				break;
			case ELeafBindType::BitfieldBool:
				SetBit(*Member, Leaf.Bitfield.Idx, BitIt.GrabNext(ByteIt));
				break;
			default:
				switch (Leaf.Arithmetic.Width)
				{
					case ELeafWidth::B8:	FMemory::Memcpy(Member, ByteIt.GrabBytes(1), 1); break;
					case ELeafWidth::B16:	FMemory::Memcpy(Member, ByteIt.GrabBytes(2), 2); break;
					case ELeafWidth::B32:	FMemory::Memcpy(Member, ByteIt.GrabBytes(4), 4); break;
					case ELeafWidth::B64:	FMemory::Memcpy(Member, ByteIt.GrabBytes(8), 8); break;
				}
				break;
		}
	}

	void LoadMemberStruct(uint8* Member, FStructSchemaId Id)
	{
		PlainProps::LoadStruct(Member, FByteReader(ByteIt.GrabSkippableSlice()), Id, Batch);
	}

	void LoadMemberRange(uint8* Member, const FLoadRangePlan& Plan)
	{
		uint64 Num = GrabRangeNum(Plan.MaxSize, ByteIt, BitIt);
		LoadRangePlan(Member, Num, ByteIt, BitIt, Batch, Plan);
	}
};

////////////////////////////////////////////////////////////////////////////

void LoadStruct(void* Dst, FByteReader Src, FStructSchemaId LoadId, const FLoadBatch& Batch)
{
	FLoadStructPlan Plan = Batch[LoadId];
	if (Plan.IsSchema())
	{
		if (Plan.IsSparseSchema())
		{
			TMemberLoader< true, uint32>(Src, Plan.AsSchema(), Batch).Load(Dst);
		}
		else
		{
			TMemberLoader<false, uint32>(Src, Plan.AsSchema(), Batch).Load(Dst);
		}
	}
	else if (Plan.IsMemcpy())
	{
		Src.CheckSize(Plan.AsMemcpy().Size);
		FMemory::Memcpy(static_cast<uint8*>(Dst) + Plan.AsMemcpy().Offset, Src.Peek(), Plan.AsMemcpy().Size);
	}
	else
	{
		FStructSchemaHandle ReadSchema{Batch.GetReadId(LoadId), Batch.ReadId};
		Plan.AsCustom().LoadCustom(Dst, { ReadSchema, Src }, ECustomLoadMethod::Assign, Batch);
	}
}

void LoadStruct(void* Dst, FStructView Src, const FLoadBatch& Batch)
{
	LoadStruct(Dst, Src.Values, Src.Schema.Id, Batch);
}

void ConstructAndLoadStruct(void* Dst, FByteReader Src, FStructSchemaId Id, const FLoadBatch& Batch)
{
	FLoadStructPlan Plan = Batch[Id];
	checkf(!Plan.IsSchema(), TEXT("Non-default constructible types requires ICustomBinding or in rare cases memcpying"));

	if (Plan.IsMemcpy())
	{
		Src.CheckSize(Plan.AsMemcpy().Size);
		FMemory::Memcpy(static_cast<uint8*>(Dst) + Plan.AsMemcpy().Offset, Src.Peek(), Plan.AsMemcpy().Size);
	}
	else
	{
		FStructSchemaHandle ReadSchema{Id, Batch.ReadId};
		Plan.AsCustom().LoadCustom(Dst, { ReadSchema, Src }, ECustomLoadMethod::Construct, Batch);
	}
}

void LoadRange(void* Dst, FRangeView Src, ERangeSizeType MaxSize, TConstArrayView<FRangeBinding> Bindings, const FLoadBatch& Batch)
{
	FRangeLoader::LoadRangeView(static_cast<uint8*>(Dst), Src, MaxSize, Bindings, Batch);
}


} // namespace PlainProps