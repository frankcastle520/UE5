// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlainPropsBuildSchema.h"
#include "PlainPropsInternalBuild.h"
#include "PlainPropsInternalFormat.h"
#include "Algo/Compare.h"
#include "Algo/Find.h"
#include "Containers/Map.h"

namespace PlainProps
{

static const TCHAR* ToString(FLeafType Leaf)
{
	const TCHAR* Leaves[8][4] = {
		{TEXT("bool"),		TEXT("ERR_b16"),	TEXT("ERR_b32"),	TEXT("ERR_b64")},
		{TEXT("int8"),		TEXT("int16"),		TEXT("int32"),		TEXT("int64")},
		{TEXT("uint8"),		TEXT("uint16"),		TEXT("uint32"),		TEXT("uint64")},
		{TEXT("ERR_fp8"),	TEXT("ERR_fp16"),	TEXT("float"),		TEXT("double")},
		{TEXT("hex8"),		TEXT("hex16"),		TEXT("hex32"),		TEXT("hex64")},
		{TEXT("enum8"),		TEXT("enum16"),		TEXT("enum32"),		TEXT("enum64")},
		{TEXT("utf8"),		TEXT("utf16"),		TEXT("utf32"),		TEXT("ERR_utf64")},
		{TEXT("ERR_oob"),	TEXT("ERR_oob"),	TEXT("ERR_oob"),	TEXT("ERR_oob")}};

	return Leaves[(uint8)Leaf.Type][(uint8)Leaf.Width];
}


static FString PrintMemberSchema(FMemberType Type, FOptionalSchemaId InnerSchema, TConstArrayView<FMemberType> InnerRangeTypes)
{
	switch (Type.GetKind())
	{
	case EMemberKind::Leaf:		return ToString(Type.AsLeaf());
	case EMemberKind::Struct:	return FString::Printf(TEXT("Struct [%d]%s%s"), InnerSchema.Get().Idx, Type.AsStruct().IsSuper ? TEXT(" (super)") : TEXT(""), Type.AsStruct().IsDynamic ? TEXT(" (dynamic)") : TEXT(""));
	case EMemberKind::Range:	return TEXT("Range of ") + PrintMemberSchema(InnerRangeTypes[0], InnerSchema, InnerRangeTypes.RightChop(1));
	}
	return "Illegal member kind";
}

static FString PrintMemberSchema(FMemberSchema Schema)
{
	return PrintMemberSchema(Schema.Type, Schema.InnerSchema, Schema.GetInnerRangeTypes());
}

//////////////////////////////////////////////////////////////////////////

struct FStructSchemaBuilder
{
	FStructSchemaBuilder(const FStructDeclaration& InDecl, FSchemasBuilder& InSchemas);

	const FStructDeclaration&					Declaration;
	FSchemasBuilder&							AllSchemas;
	bool										bMissingMemberNoted = false;
	FOptionalMemberId*							MemberOrder;
	FMemberSchema*								NotedSchemas;
	TBitArray<>									NotedMembers;

	void										NoteMembersRecursively(const FBuiltStruct& Struct);
	void										NoteRangeRecursively(ERangeSizeType NumType, TConstArrayView<FMemberType> Types, void* InnermostSchemaBuilder, const FBuiltRange& Range);
	FBuiltStructSchema							Build() const;
};

struct FEnumSchemaBuilder
{
	const FEnumDeclaration&						Declaration;
	FEnumSchemaId								Id;
	TSet<uint64>								NotedConstants;

	void										NoteValue(uint64 Value);
	FBuiltEnumSchema							Build() const;
};

//////////////////////////////////////////////////////////////////////////

FSchemasBuilder::FSchemasBuilder(const FDeclarations& Types, const IStructBindIds& InBindIds, FScratchAllocator& InScratch)
: FSchemasBuilder(Types.GetStructs(), Types.GetEnums(), InBindIds, Types.GetDebug(), InScratch)
{}

FSchemasBuilder::FSchemasBuilder(FStructDeclarations InStructs, FEnumDeclarations InEnums, const IStructBindIds& InBindIds, const FDebugIds& InDebug, FScratchAllocator& InScratch)
: DeclaredStructs(InStructs)
, DeclaredEnums(InEnums)
, BindIds(InBindIds)
, Scratch(InScratch)
, Debug(InDebug)
{
	StructIndices.Init(INDEX_NONE, InStructs.Num());
	EnumIndices.Init(INDEX_NONE, InEnums.Num());
}

FSchemasBuilder::~FSchemasBuilder() {}

template<class T, typename ...Ts>
FORCEINLINE T& GetOrEmplace(int32& Index, TPagedArray<T, 4096>& Things, Ts&&... EmplaceArgs)
{
	if (Index == INDEX_NONE)
	{
		Index = Things.Num();
		Things.Emplace(Forward<Ts>(EmplaceArgs)...);
	}

	return Things[Index];
}
	
FORCEINLINE FEnumSchemaBuilder&	FSchemasBuilder::NoteEnum(FEnumSchemaId Id)
{
	checkf(!bBuilt, TEXT("Noted new members after building"));
	checkf(Id.Idx < uint32(DeclaredEnums.Num()) && DeclaredEnums[Id.Idx], TEXT("Undeclared enum '%s' noted"), *Debug.Print(Id));
	return GetOrEmplace(EnumIndices[Id.Idx], Enums, *DeclaredEnums[Id.Idx], Id);
}

FORCEINLINE FStructSchemaBuilder& FSchemasBuilder::NoteStruct(FStructSchemaId BindId)
{
	checkf(!bBuilt, TEXT("Noted new members after building"));

	FStructSchemaId DeclId	= BindId.Idx < uint32(DeclaredStructs.Num()) && DeclaredStructs[BindId.Idx] 
							? BindId : BindIds.GetDeclId(BindId);

	checkf(DeclId.Idx < uint32(DeclaredStructs.Num()) && DeclaredStructs[DeclId.Idx], TEXT("Undeclared struct '%s' noted"), *Debug.Print(DeclId));
	return GetOrEmplace(StructIndices[DeclId.Idx], Structs, *DeclaredStructs[DeclId.Idx], *this);
}

void FSchemasBuilder::NoteStructAndMembers(FStructSchemaId BindId, const FBuiltStruct& Struct)
{
	NoteStruct(BindId).NoteMembersRecursively(Struct);
}

FBuiltSchemas FSchemasBuilder::Build()
{
	checkf(!bBuilt, TEXT("Already built"));
	bBuilt = true;

	NoteInheritanceChains();

	FBuiltSchemas Out;
	Out.Structs.Reserve(Structs.Num());
	Out.Enums.Reserve(Enums.Num());

	for (FStructSchemaBuilder& Struct : Structs)
	{
		Out.Structs.Emplace(Struct.Build());
	}
	for (FEnumSchemaBuilder& Enum : Enums)
	{
		Out.Enums.Emplace(Enum.Build());
	}

	return Out;
}

void FSchemasBuilder::NoteInheritanceChains()
{
	for (int Idx = 0, Num = Structs.Num(); Idx < Num; ++Idx)
	{
		for (FOptionalStructSchemaId Super = Structs[Idx].Declaration.Super; Super; Super = DeclaredStructs[Super.Get().Idx]->Super)
		{
			uint32 SuperIdx = Super.Get().Idx;
			GetOrEmplace(StructIndices[SuperIdx], Structs, *DeclaredStructs[SuperIdx], *this);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

FStructSchemaBuilder::FStructSchemaBuilder(const FStructDeclaration& Decl, FSchemasBuilder& Schemas)
: Declaration(Decl)
, AllSchemas(Schemas)
, NotedMembers(false, Declaration.NumMembers + int32(!!Declaration.Super))
{
	MemberOrder = Schemas.GetScratch().AllocateArray<FOptionalMemberId>(NotedMembers.Num());
	NotedSchemas = Schemas.GetScratch().AllocateArray<FMemberSchema>(NotedMembers.Num());

	check(NotedMembers.Num());
	MemberOrder[0] = NoId;
	TConstArrayView<FMemberId> Order = Declaration.GetMemberOrder();
	FMemory::Memcpy(MemberOrder + int32(!!Declaration.Super), Order.GetData(), Order.NumBytes());
}

static bool RequiresDynamicStructSchema(const FMemberSchema& A, const FMemberSchema& B)
{
	if (A.InnerSchema != B.InnerSchema && A.Type.GetKind() == B.Type.GetKind())
	{
		if (A.Type.IsStruct())
		{
			return true;
		}
		else if (A.Type.IsRange() && A.GetInnermostType().IsStruct() && B.GetInnermostType().IsStruct())
		{
			// Same range size and nested range sizes
			return	A.Type == B.Type &&	Algo::Compare(	A.GetInnerRangeTypes().LeftChop(1),
														B.GetInnerRangeTypes().LeftChop(1));
		}
	}

	return false;
}

static void SetIsDynamic(FMemberType& InOut)
{
	FStructType Type = InOut.AsStruct();
	Type.IsDynamic = true;
	InOut = FMemberType(Type);
}

static void* NoteStructOrEnum(FSchemasBuilder& AllSchemas, bool bStruct, FSchemaId Id)
{
	return bStruct ? static_cast<void*>(&AllSchemas.NoteStruct(static_cast<FStructSchemaId>(Id))) : &AllSchemas.NoteEnum(static_cast<FEnumSchemaId>(Id));
}

void FStructSchemaBuilder::NoteMembersRecursively(const FBuiltStruct& Struct)
{
	check(Declaration.Occupancy != EMemberPresence::RequireAll || Struct.NumMembers == Declaration.NumMembers);
	bMissingMemberNoted |= Struct.NumMembers < NotedMembers.Num();
	
	if (Struct.NumMembers == 0)
	{
		return;
	}


	const int32 NumNoted = NotedMembers.Num();
	int32 NoteIdx = 0;
	for (const FBuiltMember& Member : MakeArrayView(Struct.Members, Struct.NumMembers))
	{
		while (MemberOrder[NoteIdx] != Member.Name)
		{
			++NoteIdx;
			check(NoteIdx < NumNoted);
		}

		if (NotedMembers[NoteIdx])
		{
			FMemberSchema& NotedSchema = NotedSchemas[NoteIdx];
			if (RequiresDynamicStructSchema(NotedSchema, Member.Schema))
			{
				if (!NotedSchema.GetInnermostType().AsStruct().IsDynamic)
				{
					SetIsDynamic(NotedSchema.EditInnermostType(AllSchemas.GetScratch()));
					NotedSchema.InnerSchema = NoId;
				}
				check(NotedSchema.InnerSchema == NoId);
				
			}
			else
			{
				checkf(NotedSchema == Member.Schema, TEXT("Member '%s' in '%s' first added as %s and later as %s."),
					*AllSchemas.GetDebug().Print(Member.Name), *AllSchemas.GetDebug().Print(Declaration.Type), *PrintMemberSchema(NotedSchema), *PrintMemberSchema(Member.Schema));
			}
		}
		else
		{
			NotedMembers[NoteIdx] = true;
			NotedSchemas[NoteIdx] = Member.Schema;
		}

		++NoteIdx;
		
		const FMemberSchema& Schema = Member.Schema;
		if (Schema.InnerSchema)
		{
			checkSlow(IsStructOrEnum(Schema.GetInnermostType()));
			FSchemaId InnerSchema = Schema.InnerSchema.Get();

			switch (Schema.Type.GetKind())
			{
			case EMemberKind::Leaf:
				AllSchemas.NoteEnum(static_cast<FEnumSchemaId>(InnerSchema)).NoteValue(Member.Value.Leaf);
				break;

			case EMemberKind::Struct:
				AllSchemas.NoteStruct(static_cast<FStructSchemaId>(InnerSchema)).NoteMembersRecursively(*Member.Value.Struct);
				break;
		
			case EMemberKind::Range:
				void* InnerSchemaBuilder = NoteStructOrEnum(AllSchemas, Schema.GetInnermostType().IsStruct(), InnerSchema);
				if (Member.Value.Range)
				{
					NoteRangeRecursively(Schema.Type.AsRange().MaxSize, Schema.GetInnerRangeTypes(), InnerSchemaBuilder, *Member.Value.Range);			
				}
				break;
			}
		}
	}
}

template<typename IntType>
void NoteEnumValues(FEnumSchemaBuilder& Schema, const IntType* Values, uint64 Num)
{
	for (IntType Value : TConstArrayView64<IntType>(Values, Num))
	{
		Schema.NoteValue(Value);
	}
}

static void NoteEnumRange(FEnumSchemaBuilder& Out, FLeafType Leaf, const FBuiltRange& Range)
{
	check(Leaf.Type == ELeafType::Enum);
	switch (Leaf.Width)
	{
	case ELeafWidth::B8:	NoteEnumValues(Out, reinterpret_cast<const uint8* >(Range.Data), Range.Num); break;
	case ELeafWidth::B16:	NoteEnumValues(Out, reinterpret_cast<const uint16*>(Range.Data), Range.Num); break;
	case ELeafWidth::B32:	NoteEnumValues(Out, reinterpret_cast<const uint32*>(Range.Data), Range.Num); break;
	case ELeafWidth::B64:	NoteEnumValues(Out, reinterpret_cast<const uint64*>(Range.Data), Range.Num); break;
	}
}

void FStructSchemaBuilder::NoteRangeRecursively(ERangeSizeType NumType, TConstArrayView<FMemberType> Types, void* InnermostSchema, const FBuiltRange& Range)
{
	FMemberType Type = Types[0];
	switch (Type.GetKind())
	{
	case EMemberKind::Struct:
		for (const FBuiltStruct* Struct : Range.AsStructs())
		{
			static_cast<FStructSchemaBuilder*>(InnermostSchema)->NoteMembersRecursively(*Struct);
		}
		break;
	case EMemberKind::Range:
		for (const FBuiltRange* InnerRange : Range.AsRanges())
		{
			if (InnerRange)
			{
				NoteRangeRecursively(Type.AsRange().MaxSize, Types.RightChop(1), InnermostSchema, *InnerRange);
			}
		}
		break;
	case EMemberKind::Leaf:
		NoteEnumRange(/* out */ *static_cast<FEnumSchemaBuilder*>(InnermostSchema), Type.AsLeaf(), Range);
		break;
	}
}

FBuiltStructSchema FStructSchemaBuilder::Build() const
{
	FBuiltStructSchema Out = { Declaration.Type, Declaration.Id, Declaration.Super };
	Out.bDense = Declaration.Occupancy == EMemberPresence::RequireAll || !bMissingMemberNoted;
	
	if (int32 Num = NotedMembers.CountSetBits())
	{
		Out.MemberNames.Reserve(Num);
		Out.MemberSchemas.Reserve(Num);
		for (int32 NoteIdx = 0, NoteNum = NotedMembers.Num(); NoteIdx < NoteNum; ++NoteIdx)
		{
			if (NotedMembers[NoteIdx])
			{
				if (FOptionalMemberId Name = MemberOrder[NoteIdx])
				{
					Out.MemberNames.Add(Name.Get());
				}
				Out.MemberSchemas.Add(&NotedSchemas[NoteIdx]);
			}
		}

		check(Num == Out.MemberSchemas.Num());
	}

	return Out;
}

//////////////////////////////////////////////////////////////////////////

FBuiltEnumSchema FEnumSchemaBuilder::Build() const
{
	FBuiltEnumSchema Out = { Declaration.Type, Id };
	Out.Mode = Declaration.Mode;
	Out.Width = Declaration.Width;

	if (int32 Num = NotedConstants.Num())
	{
		Out.Names.Reserve(Num);
		Out.Constants.Reserve(Num);
		for (FEnumerator Enumerator : Declaration.GetEnumerators())
		{
			if (NotedConstants.Contains(Enumerator.Constant))
			{
				Out.Names.Add(Enumerator.Name);
				Out.Constants.Add(Enumerator.Constant);
			}
		}
	}
	
	check(	NotedConstants.Num() == Out.Constants.Num() || 
			NotedConstants.Num() == Out.Constants.Num() + (Out.Mode == EEnumMode::Flag) );
	return Out;
}

void FEnumSchemaBuilder::NoteValue(uint64 Value)
{
	if (Declaration.Mode == EEnumMode::Flag)
	{
		if (Value == 0)
		{
			// Don't validate 0 flag is declared, it isn't
			NotedConstants.Add(Value);
		}
		else
		{
			const int32 NumValidated = NotedConstants.Num();
			while (Value != 0)
			{
				uint64 HiBit = uint64(1) << FMath::FloorLog2_64(Value);
				NotedConstants.Add(HiBit);
				Value &= ~HiBit;
			}

			for (int32 Idx = NumValidated, Num = NotedConstants.Num(); Idx < Num; ++Idx)
			{
				uint64 Flag = NotedConstants.Get(FSetElementId::FromInteger(Idx));
				checkf(Algo::FindBy(Declaration.GetEnumerators(), Flag, &FEnumerator::Constant), TEXT("Enum flag %d is undeclared"), Flag);
			}
		}
	}
	else
	{
		bool bValidated;
		NotedConstants.FindOrAdd(Value, /* out */ &bValidated);
		if (!bValidated)
		{
			checkf(Algo::FindBy(Declaration.GetEnumerators(), Value, &FEnumerator::Constant), TEXT("Enum value %d is undeclared"), Value);
		}
	}
}

} // namespace PlainProps
