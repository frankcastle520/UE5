// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlainPropsDeclare.h"
#include "Algo/Compare.h"
#include "Containers/Set.h"

namespace PlainProps
{

static void ValidateDeclaration(const FEnumDeclaration& Enum)
{
	if (Enum.Mode == EEnumMode::Flag)
	{
		for (FEnumerator E : Enum.GetEnumerators())
		{
			checkf(FMath::CountBits(E.Constant) == 1, TEXT("Flag enums must use one bit per enumerator"));
		}
	}

	TSet<uint32, DefaultKeyFuncs<uint32>, TInlineSetAllocator<64>> Names;
	TSet<uint64, DefaultKeyFuncs<uint64>,TInlineSetAllocator<64>> Constants;
	uint64 LastConstant = 0;
	for (FEnumerator E : Enum.GetEnumerators())
	{
		checkf(FMath::FloorLog2_64(E.Constant) < 8 * SizeOf(Enum.Width), TEXT("Enumerator constant larger than declared width"));

		bool bDeclared;
		Names.FindOrAdd(E.Name.Idx, /* out*/ &bDeclared);
		checkf(!bDeclared, TEXT("Enumerator name declared twice"));
		Constants.FindOrAdd(E.Constant, /* out*/ &bDeclared);
		checkf(!bDeclared, TEXT("Enumerator constant declared twice"));

		checkf(LastConstant <= E.Constant, TEXT("Enumerator constants must be declared in ascending order"));
		LastConstant = E.Constant;
	}
}

template<typename T>
void CopyItems(T* It, TConstArrayView<T> Items)
{
	for (T Item : Items)
	{
		(*It++)  = Item;
	}
}

void FDeclarations::DeclareStruct(FStructSchemaId DeclId, FTypeId Type, TConstArrayView<FMemberId> MemberOrder, EMemberPresence Occupancy, FOptionalStructSchemaId Super)
{
	if (static_cast<int32>(DeclId.Idx) >= DeclaredStructs.Num())
	{
		DeclaredStructs.SetNum(DeclId.Idx + 1);
	}

	TUniquePtr<FStructDeclaration>& Ptr = DeclaredStructs[DeclId.Idx];
	if (Ptr)
	{
		check(DeclId == Ptr->Id);
		check(Type == Ptr->Type);
		check(Super == Ptr->Super);
		check(Occupancy == Ptr->Occupancy);
		check(Algo::Compare(MemberOrder, Ptr->GetMemberOrder()));

		Ptr->RefCount++;
	}
	else
	{
		FStructDeclaration Header{1, DeclId, Type, Super, Occupancy, IntCastChecked<uint16>(MemberOrder.Num())};
		void* Data = FMemory::Malloc(sizeof(FStructDeclaration) + MemberOrder.Num() * MemberOrder.GetTypeSize());
		Ptr.Reset(new (Data) FStructDeclaration(Header));
		CopyItems(Ptr->MemberOrder, MemberOrder);
	}
}

void FDeclarations::DeclareEnum(FEnumSchemaId Id, FTypeId Type, EEnumMode Mode, ELeafWidth Width, TConstArrayView<FEnumerator> Enumerators)
{
	if (static_cast<int32>(Id.Idx) >= DeclaredEnums.Num())
	{
		DeclaredEnums.SetNum(Id.Idx + 1);
	}
	
	TUniquePtr<FEnumDeclaration>& Ptr = DeclaredEnums[Id.Idx];
	checkf(!Ptr, TEXT("'%s' is already declared"), *Debug.Print(Id));

	FEnumDeclaration Header{Type, Mode, Width, IntCastChecked<uint16>(Enumerators.Num())};
	void* Data = FMemory::Malloc(sizeof(FEnumDeclaration) + Enumerators.Num() * Enumerators.GetTypeSize());
	Ptr.Reset(new (Data) FEnumDeclaration(Header));
	CopyItems(Ptr->Enumerators, Enumerators);

	ValidateDeclaration(*Ptr);
}

void FDeclarations::DropStructRef(FStructSchemaId DeclId)
{
	Check(DeclId);
	
	TUniquePtr<FStructDeclaration>& Ptr = DeclaredStructs[DeclId.Idx];
	Ptr->RefCount--;
	if (Ptr->RefCount == 0)
	{
		Ptr.Reset();
	}
}

#if DO_CHECK
void FDeclarations::Check(FEnumSchemaId Id) const
{
	checkf(Id.Idx < (uint32)DeclaredEnums.Num() && DeclaredEnums[Id.Idx], TEXT("'%s' is undeclared"), *Debug.Print(Id));
}

void FDeclarations::Check(FStructSchemaId Id) const
{
	checkf(Id.Idx < (uint32)DeclaredStructs.Num() && DeclaredStructs[Id.Idx], TEXT("'%s' is undeclared"), *Debug.Print(Id));
}
#endif

} // namespace PlainProps