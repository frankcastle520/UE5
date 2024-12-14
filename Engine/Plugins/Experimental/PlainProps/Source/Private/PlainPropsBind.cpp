// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlainPropsBind.h"
#include "PlainPropsIndex.h"
#include "PlainPropsInternalBuild.h"
#include "PlainPropsInternalFormat.h"
#include "PlainPropsInternalRead.h"

namespace PlainProps
{

static_assert(sizeof(ELeafBindType) == 1);
static_assert((uint8)ELeafType::Bool		== (uint8)ELeafBindType::Bool);
static_assert((uint8)ELeafType::IntS		== (uint8)ELeafBindType::IntS);
static_assert((uint8)ELeafType::IntU		== (uint8)ELeafBindType::IntU);
static_assert((uint8)ELeafType::Float		== (uint8)ELeafBindType::Float);
static_assert((uint8)ELeafType::Hex			== (uint8)ELeafBindType::Hex);
static_assert((uint8)ELeafType::Enum		== (uint8)ELeafBindType::Enum);
static_assert((uint8)ELeafType::Unicode		== (uint8)ELeafBindType::Unicode);

////////////////////////////////////////////////////////////////////////////////////////////////

uint32 FSchemaBinding::CalculateSize() const
{
	uint32 Out = sizeof(FSchemaBinding) + (NumMembers + NumInnerRanges) * sizeof(FMemberBindType);
	Out = Align(Out + NumMembers * sizeof(uint32), sizeof(uint32));
	Out = Align(Out + NumInnerSchemas * sizeof(FSchemaId), sizeof(FSchemaId));
	Out = Align(Out + NumInnerRanges * sizeof(FRangeBinding), sizeof(FRangeBinding));
	return Out;
}

////////////////////////////////////////////////////////////////////////////////////////////////

FMemberVisitor::FMemberVisitor(const FSchemaBinding& InSchema)
: Schema(InSchema)
, NumMembers(InSchema.NumMembers)
{}

EMemberKind FMemberVisitor::PeekKind() const
{
	return PeekType().GetKind();
}

FMemberBindType	FMemberVisitor::PeekType() const
{
	check(HasMore());
	return Schema.Members[MemberIdx];
}

uint32 FMemberVisitor::PeekOffset() const
{
	check(HasMore());
	return Schema.GetOffsets()[MemberIdx];
}

uint64 FMemberVisitor::GrabMemberOffset()
{
	return Schema.GetOffsets()[MemberIdx++];
}

FLeafMemberBinding FMemberVisitor::GrabLeaf()
{
	FUnpackedLeafBindType Leaf = PeekType().AsLeaf();
	FOptionalEnumSchemaId Enum = Leaf.Type == ELeafBindType::Enum ? ToOptional(GrabEnumSchema()) : NoId;
	uint64 Offset = GrabMemberOffset();

	return {Leaf, Enum, Offset};
}

FStructMemberBinding FMemberVisitor::GrabStruct()
{
	checkf(!PeekType().AsStruct().IsDynamic, TEXT("Bound structs can't be dynamic"));
	return { PeekType().AsStruct(), static_cast<FStructSchemaId>(GrabInnerSchema()), GrabMemberOffset() };
}

static bool HasSchema(FMemberBindType Type)
{
	return Type.IsStruct() || Type.AsLeaf().Bind.Type == ELeafBindType::Enum;
}

TConstArrayView<FMemberBindType> FMemberVisitor::GrabInnerTypes()
{
	const int32 Idx = InnerRangeIdx;
	const TConstArrayView<FMemberBindType> All(Schema.GetInnerRangeTypes(), Schema.NumInnerRanges);
	while (All[InnerRangeIdx++].IsRange()) {}
	return All.Slice(Idx, InnerRangeIdx - Idx);
}

FRangeMemberBinding FMemberVisitor::GrabRange()
{
	ERangeSizeType MaxSize = PeekType().AsRange().MaxSize;
	const FRangeBinding* RangeBindings = Schema.GetRangeBindings() + InnerRangeIdx;
	check(MaxSize == RangeBindings[0].GetSizeType());
	FMemberBindTypeRange InnerTypes = GrabInnerTypes();
	FOptionalSchemaId InnermostSchema = HasSchema(InnerTypes.Last()) ? ToOptional(GrabInnerSchema()) : NoId; 
	uint64 Offset = GrabMemberOffset();
		
	return { &InnerTypes[0], RangeBindings, static_cast<uint16>(InnerTypes.Num()), InnermostSchema, Offset};
}

void FMemberVisitor::SkipMember()
{
	FMemberBindType Type = PeekType();
	if (Type.IsRange())
	{
		FMemberBindTypeRange InnerTypes = GrabInnerTypes();
		InnerSchemaIdx += HasSchema(InnerTypes.Last());
	}
	else
	{
		InnerSchemaIdx += HasSchema(Type);
	}
	
	++MemberIdx;
}

FSchemaId FMemberVisitor::GrabInnerSchema()
{
	check(InnerSchemaIdx < Schema.NumInnerSchemas);
	return Schema.GetInnerSchemas()[InnerSchemaIdx++];
}

////////////////////////////////////////////////////////////////////////////////////////////////

FRangeBinding::FRangeBinding(const IItemRangeBinding& Binding, ERangeSizeType SizeType)
: Handle(uint64(&Binding) | uint8(SizeType))
{
	check(&Binding == &AsItemBinding());
	check(SizeType == GetSizeType());
}

FRangeBinding::FRangeBinding(const ILeafRangeBinding& Binding, ERangeSizeType SizeType)
: Handle(uint64(&Binding) | uint8(SizeType) | LeafMask)
{
	check(&Binding == &AsLeafBinding());
	check(SizeType == GetSizeType());
}

////////////////////////////////////////////////////////////////////////////////////////////////

void* FLeafRangeAllocator::Allocate(FUnpackedLeafType Leaf, uint64 Num)
{
	check(!Range);
	Range = FBuiltRange::Create(Scratch, Num, SizeOf(Leaf.Width));
	return Range->Data;
}

////////////////////////////////////////////////////////////////////////////////////////////////

void FCustomBindings::BindStruct(FStructSchemaId BindId, FStructSchemaId DeclId, ICustomBinding& Binding)
{
	checkf(!Find(BindId), TEXT("'%s' already bound"), *Debug.Print(BindId));
	Entries.Emplace(BindId, DeclId, &Binding);
}

void FCustomBindings::DropStruct(FStructSchemaId BindId)
{
	for (FCustomBindingEntry& Entry : Entries)
	{
		if (Entry.BindId == BindId)
		{
			Entries.RemoveAtSwap(&Entry - Entries.GetData(), EAllowShrinking::No);
			return;
		}
	}
	
	checkf(false, TEXT("'%s' unbound"), *Debug.Print(BindId));
}

FOptionalStructSchemaId	FCustomBindings::FindStructDeclId(FStructSchemaId BindId) const
{ 
	 FCustomBindingEntry Entry = Find(BindId);
	 return Entry ? ToOptional(Entry.DeclId) : NoId;
}

FCustomBindingEntry	FCustomBindings::Find(FStructSchemaId BindId) const
{
	for (FCustomBindingEntry Entry : Entries)
	{
		if (Entry.BindId == BindId)
		{
			return Entry;
		}
	}

	return Base ? Base->Find(BindId) : FCustomBindingEntry{};
}

////////////////////////////////////////////////////////////////////////////////////////////////

FSchemaBindings::~FSchemaBindings()
{}

static uint16 CountInnerSchemas(TConstArrayView<FMemberBinding> Members)
{
	uint32 Out = 0;
	for (const FMemberBinding& Member : Members)
	{
		Out += !!Member.InnermostSchema;
	}
	return IntCastChecked<uint16>(Out);
}

static uint16 CountRanges(TConstArrayView<FMemberBinding> Members)
{
	int32 Out = 0;
	for (const FMemberBinding& Member : Members)
	{
		Out += Member.RangeBindings.Num();
	}
	return IntCastChecked<uint16>(Out);
}

void FSchemaBindings::BindStruct(FStructSchemaId BindId, FStructSchemaId DeclId, TConstArrayView<FMemberBinding> Members)
{
	// Make header, allocate and copy header
	FSchemaBinding Header = { DeclId, IntCastChecked<uint16>(Members.Num()), CountInnerSchemas(Members), CountRanges(Members) };
	FSchemaBinding* Schema = new (FMemory::MallocZeroed(Header.CalculateSize())) FSchemaBinding {Header};

	// Write footer
	FMemberBinder Footer(*Schema);
	for (const FMemberBinding& Member : Members)
	{
		TConstArrayView<FRangeBinding> Ranges = Member.RangeBindings;
		if (Ranges.IsEmpty())
		{
			Footer.AddMember(Member.InnermostType, IntCastChecked<uint32>(Member.Offset));
		}
		else
		{
			Footer.AddRange(Ranges, Member.InnermostType, IntCastChecked<uint32>(Member.Offset));
		}

		if (Member.InnermostSchema)
		{
			Footer.AddInnerSchema(Member.InnermostSchema.Get());
		}
	}

	// Bind
	if (BindId.Idx >= static_cast<uint32>(Bindings.Num()))
	{
		Bindings.SetNum(BindId.Idx + 1);
	}
	checkf(!Bindings[BindId.Idx], TEXT("'%s' already bound"), *Debug.Print(BindId));
	Bindings[BindId.Idx].Reset(Schema);
}

const FSchemaBinding* FSchemaBindings::FindStruct(FStructSchemaId BindId) const
{
	return BindId.Idx < (uint32)Bindings.Num() ? Bindings[BindId.Idx].Get() : nullptr;
}

const FSchemaBinding& FSchemaBindings::GetStruct(FStructSchemaId BindId) const
{
	checkf(BindId.Idx < (uint32)Bindings.Num() && Bindings[BindId.Idx], TEXT("'%s' is unbound"), *Debug.Print(BindId));
	return *Bindings[BindId.Idx].Get();
}

void FSchemaBindings::DropStruct(FStructSchemaId BindId)
{
	checkf(BindId.Idx < (uint32)Bindings.Num() && Bindings[BindId.Idx], TEXT("'%s' is unbound"), *Debug.Print(BindId));
	Bindings[BindId.Idx].Reset();
}

FStructSchemaId FSchemaBindings::GetDeclId(FStructSchemaId BindId) const
{
	return GetStruct(BindId).DeclId;
}

//////////////////////////////////////////////////////////////////////////

FStructSchemaId FStructBindIds::GetDeclId(FStructSchemaId BindId) const
{
	FOptionalStructSchemaId CustomId = Customs.FindStructDeclId(BindId);
	return CustomId ? CustomId.Get() : Schemas.GetDeclId(BindId);
}

//////////////////////////////////////////////////////////////////////////

TArray<FStructSchemaId> IndexInMemoryNames(const FSchemaBatch& Schemas, FIdIndexerBase& Indexer)
{
	const uint8* Base = reinterpret_cast<const uint8*>(&Schemas);
	const uint32* Offsets = Schemas.SchemaOffsets;

	TArray<FStructSchemaId> Out;
	Out.SetNumUninitialized(Schemas.NumStructSchemas);
	for (int32 Idx = 0; Idx < Out.Num(); ++Idx)
	{
		const FStructSchema& Schema = *reinterpret_cast<const FStructSchema*>(Base + Offsets[Idx]);
		Out[Idx] = Indexer.IndexStruct(Schema.Type);
	}

	return Out;
}

uint32 FIdTranslatorBase::CalculateTranslationSize(int32 NumSavedNames, const FSchemaBatch& Batch)
{
	static_assert(sizeof(FNameId) == sizeof(FNestedScopeId));
	static_assert(sizeof(FNameId) == sizeof(FParametricTypeId));
	static_assert(sizeof(FNameId) == sizeof(FSchemaId));
	return sizeof(FNameId) * (NumSavedNames + Batch.NumNestedScopes + Batch.NumParametricTypes + Batch.NumSchemas);
}

FFlatScopeId Translate(FFlatScopeId From, TConstArrayView<FNameId> ToNames)
{
	return { ToNames[From.Name.Idx] };
}

static void TranslateScopeIds(TArrayView<FNestedScopeId> Out, FIdIndexerBase& Indexer, TConstArrayView<FNameId> ToNames, TConstArrayView<FNestedScope> From)
{
	uint32 OutIdx = 0;
	for (FNestedScope Scope : From)
	{
		check(Scope.Outer.IsFlat() || Scope.Outer.AsNested().Idx < OutIdx);
		FScopeId Outer = Scope.Outer.IsFlat() ? FScopeId(Translate(Scope.Outer.AsFlat(), ToNames)) : FScopeId(Out[Scope.Outer.AsNested().Idx]);
		FFlatScopeId Inner = Translate(Scope.Inner, ToNames);
		Out[OutIdx++] = Indexer.NestScope(Outer, Inner).AsNested();
	}
}

static void TranslateParametricTypeIds(TArrayView<FParametricTypeId> Out, FIdIndexerBase& Indexer, const FIdBinding& To, TConstArrayView<FParametricType> From, const FTypeId* FromParameters)
{
	TArray<FTypeId, TInlineAllocator<8>> Params;
	uint32 OutIdx = 0;
	for (FParametricType Parametric : From)
	{
		Params.Reset();
		for (FTypeId FromParameter : MakeArrayView(FromParameters + Parametric.Parameters.Idx, Parametric.Parameters.NumParameters))
		{
			Params.Add(To.Remap(FromParameter));
		}
		Out[OutIdx++] = Indexer.MakeParametricTypeId(To.Remap(Parametric.Name), Params);
	}
}

static void TranslateSchemaIds(TArrayView<FSchemaId> Out, FIdIndexerBase& Indexer, const FIdBinding& To, const FSchemaBatch& From)
{
	uint32 OutIdx = 0;
	for (const FStructSchema& FromSchema : GetStructSchemas(From))
	{
		FTypeId ToType = To.Remap(FromSchema.Type);
		checkSlow(ToType.Name.NumParameters == FromSchema.Type.Name.NumParameters);
		Out[OutIdx++] = Indexer.IndexStruct(ToType);
	}
	
	for (const FEnumSchema& FromSchema : GetEnumSchemas(From))
	{
		FTypeId ToType = To.Remap(FromSchema.Type);
		Out[OutIdx++] = Indexer.IndexEnum(ToType);
	}
}

FIdBinding FIdTranslatorBase::TranslateIds(FMutableMemoryView To, FIdIndexerBase& Indexer, TConstArrayView<FNameId> ToNames, const FSchemaBatch& From)
{
	TArrayView<FNestedScopeId> ToScopes(static_cast<FNestedScopeId*>(To.GetData()), From.NumNestedScopes);
	TArrayView<FParametricTypeId> ToParametricTypes(reinterpret_cast<FParametricTypeId*>(ToScopes.end()), From.NumParametricTypes);
	TArrayView<FSchemaId> ToSchemas(reinterpret_cast<FSchemaId*>(ToParametricTypes.end()), From.NumSchemas);
	FIdBinding Out = {ToNames, ToScopes, ToParametricTypes, ToSchemas};
	check(uintptr_t(To.GetDataEnd()) == uintptr_t(ToSchemas.end()));

	TranslateScopeIds(ToScopes, Indexer, ToNames, From.GetNestedScopes());
	TranslateParametricTypeIds(ToParametricTypes, Indexer, Out, From.GetParametricTypes(), From.GetFirstParameter());
	TranslateSchemaIds(ToSchemas, Indexer, Out, From);

	return Out;
}

//////////////////////////////////////////////////////////////////////////

template<class IdType>
void RemapAll(TArrayView<IdType> Ids, FIdBinding NewIds)
{
	for (IdType& Id : Ids)
	{
		Id = NewIds.Remap(Id);
	}
}

FSchemaBatch* CreateTranslatedSchemas(const FSchemaBatch& In, FIdBinding NewIds)
{
	const FMemoryView InSchemas = GetSchemaData(In);
	const uint32 Num = In.NumSchemas;
	const uint64 Size = sizeof(FSchemaBatch) + /* offsets */ sizeof(uint32) * Num + InSchemas.GetSize();

	// Allocate and copy header
	FSchemaBatch* Out = new (FMemory::Malloc(Size)) FSchemaBatch {In};
	Out->NumNestedScopes = 0;
	Out->NestedScopesOffset = 0;
	Out->NumParametricTypes = 0;

	// Initialize schema offsets
	const uint32 DroppedBytes = IntCastChecked<uint32>(uintptr_t(InSchemas.GetData()) - uintptr_t(In.SchemaOffsets + Num));
	for (uint32 Idx = 0; Idx < Num; ++Idx)
	{
		Out->SchemaOffsets[Idx] = In.SchemaOffsets[Idx] - DroppedBytes;
	}

	// Copy schemas and remap type ids if needed
	FMemory::Memcpy(reinterpret_cast<uint8*>(Out) + Out->GetSchemaOffsets()[0], InSchemas.GetData(), InSchemas.GetSize());
	for (FStructSchema& Schema : GetStructSchemas(*Out))
	{
		Schema.Type = NewIds.Remap(Schema.Type);
		RemapAll(Schema.EditMemberNames(), NewIds);
	}
	for (FEnumSchema& Schema : GetEnumSchemas(*Out))
	{
		Schema.Type = NewIds.Remap(Schema.Type);
		RemapAll(MakeArrayView(Schema.Footer, Schema.Num), NewIds);
	}

	return Out;
}

void DestroyTranslatedSchemas(const FSchemaBatch* Schemas)
{
	FMemory::Free(const_cast<FSchemaBatch*>(Schemas));
}

} // namespace PlainProps