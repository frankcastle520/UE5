// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlainPropsSave.h"
#include "PlainPropsBind.h"
#include "PlainPropsInternalBuild.h"
#include "PlainPropsInternalFormat.h"
#include "Algo/Find.h"
#include <type_traits>

namespace PlainProps
{

static uint64 GetBit(uint8 Byte, uint8 BitIdx)
{
	return (Byte >> BitIdx) & 1;
}

static uint64 SaveLeaf(const uint8* Member, FUnpackedLeafBindType Leaf)
{
#if DO_CHECK
	if (Leaf.Type == ELeafBindType::Float)
	{
		switch (Leaf.Width)
		{
			case ELeafWidth::B32: return ValueCast(*reinterpret_cast<const float*>(Member));
			case ELeafWidth::B64: return ValueCast(*reinterpret_cast<const double*>(Member));
			default: check(false); return 0;
		}

	}
#endif

	if (Leaf.Type == ELeafBindType::BitfieldBool)
	{
		return GetBit(*Member, Leaf.BitfieldIdx);
	}
	else
	{
		// Undefined behavior. If problematic, use memcpy or switch(Leaf.Type) and ValueCast(reinterpret_cast<...>(Member))  
		switch (Leaf.Width)
		{
			case ELeafWidth::B8:	return *Member;
			case ELeafWidth::B16:	return *reinterpret_cast<const uint16*>(Member);
			case ELeafWidth::B32:	return *reinterpret_cast<const uint32*>(Member);
			case ELeafWidth::B64:	return *reinterpret_cast<const uint64*>(Member);
		}
	}
	check(false);
	return uint64(0);
}

struct FLeafRangeSaver
{
	FBuiltRange* Out;
	uint8* OutIt;
	uint8* OutEnd;

	FLeafRangeSaver(FScratchAllocator& Scratch, uint64 Num, SIZE_T LeafSize)
	: Out(FBuiltRange::Create(Scratch, Num, LeafSize))
	, OutIt(Out->Data)
	, OutEnd(OutIt + Num * LeafSize)
	{}

	void Append(FExistingItemSlice Slice, uint32 Stride, SIZE_T LeafSize, const FSaveContext&)
	{
		check(OutIt + Slice.Num * LeafSize <= OutEnd);
		FMemory::Memcpy(OutIt, Slice.Data, Slice.Num * LeafSize);
		OutIt += Slice.Num * LeafSize;
	}

	FBuiltRange* Finish()
	{
		check(OutIt == OutEnd);
		return Out;
	}
};

template<SIZE_T LeafSize>
struct TStridingLeafRangeSaver : FLeafRangeSaver
{
	inline TStridingLeafRangeSaver(FScratchAllocator& Scratch, uint64 Num, SIZE_T) : FLeafRangeSaver(Scratch, Num, LeafSize) {}

	inline void Append(FExistingItemSlice Slice, uint32 Stride, SIZE_T, const FSaveContext&)
	{
		uint8* Dst = OutIt;
		for (const uint8* Src = static_cast<const uint8*>(Slice.Data), *SrcEnd = Src + Slice.Num * Stride; Src != SrcEnd; Src += Stride, Dst += LeafSize)
		{
			FMemory::Memcpy(Dst, Src, LeafSize);
		}
		OutIt = Dst;
	}

};

//////////////////////////////////////////////////////////////////////////

template<typename BuiltItemType, typename ItemSchemaType>
struct TNonLeafRangeSaver
{
	FBuiltRange* Out;
	BuiltItemType* It;

	TNonLeafRangeSaver(FScratchAllocator& Scratch, uint64 Num, ItemSchemaType)
	: Out(FBuiltRange::Create(Scratch, Num, sizeof(BuiltItemType)))
	, It(reinterpret_cast<BuiltItemType*>(Out->Data))
	{}

	void Append(FExistingItemSlice Slice, uint32 Stride, ItemSchemaType Schema, const FSaveContext& OuterCtx)
	{
		check(It + Slice.Num <= reinterpret_cast<BuiltItemType*>(Out->Data) + Out->Num);
		for (uint64 Idx = 0; Idx < Slice.Num; ++Idx)
		{
			*It++ = SaveRangeItem(Slice.At(Idx, Stride), Schema, OuterCtx);
		}
	}

	[[nodiscard]] FBuiltRange* Finish()
	{
		check(It == reinterpret_cast<BuiltItemType*>(Out->Data) + Out->Num);
		return Out;
	}
};

using FNestedRangeSaver = TNonLeafRangeSaver<FBuiltRange*, FRangeMemberBinding>;
using FStructRangeSaver = TNonLeafRangeSaver<FBuiltStruct*, FStructSchemaId>;
using FStructRangeDeltaSaver = TNonLeafRangeSaver<FBuiltStruct*, FDefaultStruct>;

//////////////////////////////////////////////////////////////////////////


template<class SaverType, typename InnerContextType>
[[nodiscard]] inline FBuiltRange* SaveRangeItems(FSaveRangeContext& ReadCtx, const IItemRangeBinding& Binding, const FSaveContext& OuterCtx, InnerContextType InnerCtx)
{
	const uint64 NumTotal = ReadCtx.Items.NumTotal;
	SaverType Saver(OuterCtx.Scratch, NumTotal, InnerCtx);
	while (true)
	{
		check(ReadCtx.Items.Slice.Num > 0);
		Saver.Append(ReadCtx.Items.Slice, ReadCtx.Items.Stride, InnerCtx, OuterCtx);
	
		ReadCtx.Request.NumRead += ReadCtx.Items.Slice.Num;
		if (ReadCtx.Request.NumRead >= NumTotal)
		{
			check(ReadCtx.Request.NumRead == NumTotal);	
			return Saver.Finish();
		}

		Binding.ReadItems(ReadCtx);	
	}
}

template<class SaverType, typename InnerContextType>
[[nodiscard]] FBuiltRange* SaveNonLeafRange(const void* Range, const IItemRangeBinding& Binding, const FSaveContext& OuterCtx, InnerContextType InnerCtx)
{
	FSaveRangeContext ReadCtx = { { Range } };
	Binding.ReadItems(ReadCtx);

	if (ReadCtx.Items.NumTotal)
	{
		return SaveRangeItems<SaverType>(ReadCtx, Binding, OuterCtx, InnerCtx);
	}
	
	return nullptr;
}

[[nodiscard]] static FBuiltRange* SaveLeafRange(const void* Range, const IItemRangeBinding& Binding, const FSaveContext& OuterCtx, ELeafWidth Width)
{
	SIZE_T LeafSize = SizeOf(Width);
	FSaveRangeContext ReadCtx = { { Range } };
	Binding.ReadItems(ReadCtx);

	if (const uint64 NumTotal = ReadCtx.Items.NumTotal)
	{
		if (ReadCtx.Items.Stride == LeafSize)
		{
			return SaveRangeItems<FLeafRangeSaver>(ReadCtx, Binding, OuterCtx, LeafSize);
		}
		else switch (Width)
		{
			case ELeafWidth::B8:	return SaveRangeItems<TStridingLeafRangeSaver<1>>(ReadCtx, Binding, OuterCtx, LeafSize);
			case ELeafWidth::B16:	return SaveRangeItems<TStridingLeafRangeSaver<2>>(ReadCtx, Binding, OuterCtx, LeafSize);
			case ELeafWidth::B32:	return SaveRangeItems<TStridingLeafRangeSaver<4>>(ReadCtx, Binding, OuterCtx, LeafSize);
			case ELeafWidth::B64:	return SaveRangeItems<TStridingLeafRangeSaver<8>>(ReadCtx, Binding, OuterCtx, LeafSize);
		}
	}

	return nullptr;
}

[[nodiscard]] static FRangeMemberBinding GetInnerRange(FRangeMemberBinding Member)
{
	check(Member.NumRanges > 1);
	check(Member.InnerTypes[0].IsRange());
	return { Member.InnerTypes + 1, Member.RangeBindings + 1, static_cast<uint16>(Member.NumRanges - 1), Member.InnermostSchema };
}

[[nodiscard]] static FBuiltRange* SaveLeafRangeBinding(FScratchAllocator& Scratch, const void* Range, const ILeafRangeBinding& Binding, FUnpackedLeafType Leaf)
{
	FLeafRangeAllocator Allocator(Scratch, Leaf);
	Binding.SaveLeaves(Range, Allocator);
	return Allocator.GetAllocatedRange();
}

[[nodiscard]] static FBuiltRange* SaveStructRange(const void* Range, const IItemRangeBinding& ItemBinding, const FSaveContext& Ctx, FStructSchemaId Id)
{
	if (const FDefaultStruct* Defaults = Algo::FindBy(Ctx.Defaults, Id, &FDefaultStruct::Id))
	{
		return SaveNonLeafRange<FStructRangeDeltaSaver>(Range, ItemBinding, Ctx, *Defaults);
	}
	else
	{
		return SaveNonLeafRange<FStructRangeSaver>(Range, ItemBinding, Ctx, Id);
	}
}

ELeafWidth GetArithmeticWidth(FLeafBindType Leaf)
{
	checkf(Leaf.Bind.Type != ELeafBindType::BitfieldBool, TEXT("Arrays of bitfields is not a thing"));
	return Leaf.Arithmetic.Width;
}

[[nodiscard]] FBuiltRange* SaveRange(const void* Range, FRangeMemberBinding Member, const FSaveContext& Ctx)
{
	FRangeBinding Binding = Member.RangeBindings[0];
	FMemberBindType InnerType = Member.InnerTypes[0];

	if (Binding.IsLeafBinding())
	{
		return SaveLeafRangeBinding(Ctx.Scratch, Range, Binding.AsLeafBinding(), UnpackNonBitfield(InnerType.AsLeaf()));
	}

	const IItemRangeBinding& ItemBinding = Binding.AsItemBinding();
	switch (InnerType.GetKind())
	{
	case EMemberKind::Leaf:		return SaveLeafRange(Range, ItemBinding, Ctx, GetArithmeticWidth(InnerType.AsLeaf()));
	case EMemberKind::Range:	return SaveNonLeafRange<FNestedRangeSaver>(Range, ItemBinding, Ctx, GetInnerRange(Member));
	case EMemberKind::Struct:	return SaveStructRange(Range, ItemBinding, Ctx, static_cast<FStructSchemaId>(Member.InnermostSchema.Get()));
	}

	check(false);
	return nullptr;
}

//////////////////////////////////////////////////////////////////////////

[[nodiscard]] static FBuiltRange* SaveRangeItem(const uint8* Range, FRangeMemberBinding Member, const FSaveContext& Ctx)
{ 
	return SaveRange(Range, Member, Ctx);
}

[[nodiscard]] static FBuiltStruct* SaveRangeItem(const uint8* Struct, FStructSchemaId Id, const FSaveContext& Ctx)
{
	return SaveStruct(Struct, Id, Ctx);
}

[[nodiscard]] static FBuiltStruct* SaveRangeItem(const uint8* Struct, FDefaultStruct Default, const FSaveContext& Ctx)
{
	return SaveStructDelta(Struct, Default.Struct, Default.Id, Ctx);
}

[[nodiscard]] static FMemberType ToMemberType(FMemberBindType In)
{
	switch (In.GetKind())
	{
	case EMemberKind::Leaf:		return FMemberType(ToLeafType(In.AsLeaf()));
	case EMemberKind::Range:	return FMemberType(In.AsRange());
	default:					return FMemberType(In.AsStruct());
	}
}

[[nodiscard]] static FMemberType* CreateInnerRangeTypes(FScratchAllocator& Scratch, uint32 NumInnerTypes, const FMemberBindType* InnerTypes)
{
	if (NumInnerTypes <= 1)
	{
		return nullptr;
	}

	FMemberType* Out = Scratch.AllocateArray<FMemberType>(NumInnerTypes);
	for (uint32 Idx = 0; Idx < NumInnerTypes; ++Idx)
	{
		Out[Idx] = ToMemberType(InnerTypes[Idx]);
	}
	return Out;
}

[[nodiscard]] static FMemberSchema CreateRangeSchema(FScratchAllocator& Scratch, FRangeMemberBinding Member)
{
	FMemberType* InnerRangeTypes = CreateInnerRangeTypes(Scratch, Member.NumRanges, Member.InnerTypes);
	return { FMemberType(Member.RangeBindings[0].GetSizeType()), ToMemberType(Member.InnerTypes[0]), Member.NumRanges, Member.InnermostSchema, InnerRangeTypes };
}

static const uint8* At(const void* Ptr, SIZE_T Offset)
{
	return static_cast<const uint8*>(Ptr) + Offset;
}

static void SaveMember(FMemberBuilder& Out, const void* Struct, FMemberId Name, const FSaveContext& Ctx, FLeafMemberBinding Member)
{
	FUnpackedLeafType Type = { ToLeafType(Member.Leaf.Type), Member.Leaf.Width };
	Out.AddLeaf(Name, Type, Member.Enum, SaveLeaf(At(Struct, Member.Offset), Member.Leaf));
}

static void SaveMember(FMemberBuilder& Out, const void* Struct, FMemberId Name, const FSaveContext& Ctx, FRangeMemberBinding Member)
{
	Out.AddRange(Name, { CreateRangeSchema(Ctx.Scratch, Member), SaveRange(At(Struct, Member.Offset), Member, Ctx) });
}

static void SaveMember(FMemberBuilder& Out, const void* Struct, FMemberId Name, const FSaveContext& Ctx, FStructMemberBinding Member)
{
	Out.AddStruct(Name, Member.Id, SaveStruct(At(Struct, Member.Offset), Member.Id, Ctx));
}

FBuiltStruct* SaveStruct(const void* Struct, FStructSchemaId BindId, const FSaveContext& Ctx)
{
	const FStructDeclaration* Declaration = nullptr;
	FMemberBuilder Out;
	if (FCustomBindingEntry Custom = Ctx.Customs.FindStructToSave(BindId))
	{
		Custom.Binding->SaveCustom(Out, Struct, nullptr, Ctx);
		Declaration = &Ctx.Declarations.Get(Custom.DeclId);	
	}
	else
	{
		const FSchemaBinding& Schema = Ctx.Schemas.GetStruct(BindId);
		Declaration = &Ctx.Declarations.Get(Schema.DeclId);	
		TConstArrayView<FMemberId> MemberOrder = Declaration->GetMemberOrder();

		for (FMemberVisitor It(Schema); It.HasMore(); )
		{
			FMemberId Name = MemberOrder[It.GetIndex()];
			switch (It.PeekKind())
			{
				case EMemberKind::Leaf:		SaveMember(Out, Struct, Name, Ctx, It.GrabLeaf());		break;
				case EMemberKind::Range:	SaveMember(Out, Struct, Name, Ctx, It.GrabRange());		break;
				case EMemberKind::Struct:	SaveMember(Out, Struct, Name, Ctx, It.GrabStruct());	break;
			}
		}
	}

	return Out.BuildAndReset(Ctx.Scratch, *Declaration, Ctx.Declarations.GetDebug());
}

////////////////////////////////////////////////////////////////////////////////////////////////

static bool DiffItem(const void* A, const void* B, const FSaveContext& Ctx, FRangeMemberBinding Range);
static bool DiffItem(const void* A, const void* B, const FSaveContext& Ctx, FStructSchemaId Struct);

static int64 DiffLeaf(const uint8* A, const uint8* B, FUnpackedLeafBindType Leaf)
{
	if (Leaf.Type == ELeafBindType::BitfieldBool)
	{
		uint32 Mask = 1u << Leaf.BitfieldIdx;
		return (*A & Mask) - (*B & Mask);
	}

	switch (Leaf.Width)
	{
	case ELeafWidth::B8:	return FMemory::Memcmp(A, B, 1);
	case ELeafWidth::B16:	return FMemory::Memcmp(A, B, 2);
	case ELeafWidth::B32:	return FMemory::Memcmp(A, B, 4);
	case ELeafWidth::B64:	return FMemory::Memcmp(A, B, 8);
	}

	check(false);
	return 0;
}

static bool DiffItemSlice(const uint8* A, const uint8* B, uint64 Num, uint32 Stride, const FSaveContext&, SIZE_T LeafSize)
{
	return !!FMemory::Memcmp(A, B, Num * Stride);
}

template<typename ItemType>
static bool DiffItemSlice(const uint8* A, const uint8* B, uint64 Num, uint32 Stride, const FSaveContext& Ctx, ItemType&& Member)
{
	for (const uint8* EndA = A + Num * Stride; A != EndA; A += Stride, B += Stride)
	{
		if (DiffItem(A, B, Ctx, Member))
		{
			return true;
		}
	}

	return false;
}

struct FItemRangeReader
{
	explicit FItemRangeReader(const void* Range, const IItemRangeBinding& Binding)
	: Ctx{{Range}}
	{
		ReadItems(Binding);
	}

	FSaveRangeContext Ctx;
	const uint8* SliceIt = nullptr;
	uint64 SliceNum = 0;

	void ReadItems(const IItemRangeBinding& Binding)
	{
		Binding.ReadItems(Ctx);
		SliceIt = static_cast<const uint8*>(Ctx.Items.Slice.Data);
		SliceNum = Ctx.Items.Slice.Num;
	}

	void RefillItems(const IItemRangeBinding& Binding)
	{
		if (SliceNum == 0)
		{
			ReadItems(Binding);
			check(SliceNum > 0);
		}
	}

	const uint8* GrabItems(uint64 Num, uint32 Stride)
	{
		const uint8* Out = SliceIt;
		SliceIt += Num * Stride; 
		SliceNum -= Num;
		return Out;
	}
};

template<class T>
bool DiffItemRange(const void* RangeA, const void* RangeB, const IItemRangeBinding& Binding, const FSaveContext& OuterCtx, T&& ItemSchema)
{
	FItemRangeReader A(RangeA, Binding);
	FItemRangeReader B(RangeB, Binding);
	if (A.Ctx.Items.NumTotal != B.Ctx.Items.NumTotal)
	{
		return true;
	}

	if (const uint64 NumTotal = A.Ctx.Items.NumTotal)
	{
		check(A.Ctx.Items.Stride == B.Ctx.Items.Stride);
		const uint32 Stride = A.Ctx.Items.Stride;
		while (true)
		{
			uint64 Num = FMath::Min(A.SliceNum, B.SliceNum);
			if (DiffItemSlice(A.GrabItems(Num, Stride), B.GrabItems(Num, Stride), Num, Stride, OuterCtx, ItemSchema))
			{
				return true;
			}
			else if (A.Ctx.Request.NumRead >= NumTotal)
			{
				check(A.Ctx.Request.NumRead == NumTotal);	
				check(B.Ctx.Request.NumRead == NumTotal);	
				return false;
			}
			
			A.RefillItems(Binding);
			B.RefillItems(Binding);
		}
	}

	return false;
}

static bool DiffItem(const uint8* A, const uint8* B, const FSaveContext& Ctx, FRangeMemberBinding Member)
{
	FRangeBinding Binding = Member.RangeBindings[0];
	FMemberBindType InnerType = Member.InnerTypes[0];

	if (Binding.IsLeafBinding())
	{
		return !!Binding.AsLeafBinding().DiffLeaves(A, B);
	}

	const IItemRangeBinding& ItemBinding = Binding.AsItemBinding();
	switch (InnerType.GetKind())
	{
	case EMemberKind::Leaf:		return DiffItemRange(A, B, ItemBinding, Ctx, SizeOf(GetArithmeticWidth(InnerType.AsLeaf())));
	case EMemberKind::Range:	return DiffItemRange(A, B, ItemBinding, Ctx, GetInnerRange(Member));
	case EMemberKind::Struct:	return DiffItemRange(A, B, ItemBinding, Ctx, static_cast<FStructSchemaId>(Member.InnermostSchema.Get()));
	}

	check(false);
	return false;
}

static bool DiffItem(const uint8* A, const uint8* B, const FSaveContext& Ctx, FStructSchemaId Id)
{
	if (const ICustomBinding* Custom = Ctx.Customs.FindStruct(Id))
	{
		return Custom->DiffCustom(A, B);
	}
	
	bool bOut = false;
	for (FMemberVisitor It(Ctx.Schemas.GetStruct(Id)); It.HasMore() && !bOut; )
	{
		uint32 Offset = It.PeekOffset();
		const uint8* ItemA = A + Offset;
		const uint8* ItemB = B + Offset;

		switch (It.PeekKind())
		{
		case EMemberKind::Leaf:		bOut = !!DiffLeaf(ItemA, ItemB, It.GrabLeaf().Leaf);	break;
		case EMemberKind::Range:	bOut = DiffItem(ItemA, ItemB, Ctx, It.GrabRange());		break;
		case EMemberKind::Struct:	bOut = DiffItem(ItemA, ItemB, Ctx, It.GrabStruct().Id);	break;
		}
	}

	return bOut;
}

////////////////////////////////////////////////////////////////////////////////////////////////

static void SaveMemberDelta(FMemberBuilder& Out, const void* Struct, const void* Default, FMemberId Name, const FSaveContext& Ctx, FLeafMemberBinding Member)
{
	if (DiffLeaf(At(Struct, Member.Offset), At(Default, Member.Offset), Member.Leaf))
	{
		SaveMember(Out, Struct, Name, Ctx, Member);
	}
}

static void SaveMemberDelta(FMemberBuilder& Out, const void* Struct, const void* Default, FMemberId Name, const FSaveContext& Ctx, FRangeMemberBinding Member)
{
	const uint8* Range = At(Struct, Member.Offset);
	if (DiffItem(Range, At(Default, Member.Offset), Ctx, Member))
	{
		Out.AddRange(Name, { CreateRangeSchema(Ctx.Scratch, Member), SaveRange(Range, Member, Ctx) });
	}
}

static void SaveMemberDelta(FMemberBuilder& Out, const void* Struct, const void* Default, FMemberId Name, const FSaveContext& Ctx, FStructMemberBinding Member)
{
	if (FBuiltStruct* Delta = SaveStructDelta(At(Struct, Member.Offset), At(Default, Member.Offset), Member.Id, Ctx))
	{
		Out.AddStruct(Name, Member.Id, MoveTemp(Delta));
	}
}

FBuiltStruct* SaveStructDelta(const void* Struct, const void* Default, FStructSchemaId BindId, const FSaveContext& Ctx)
{
	const FStructDeclaration* Declaration = nullptr;
	FMemberBuilder Out;
	if (FCustomBindingEntry Custom = Ctx.Customs.FindStructToSave(BindId))
	{
		if (Custom.Binding->DiffCustom(Struct, Default))
		{
			Declaration = &Ctx.Declarations.Get(Custom.DeclId);	
			Custom.Binding->SaveCustom(Out, Struct, Default, Ctx);
		}
	}
	else
	{
		const FSchemaBinding& Schema = Ctx.Schemas.GetStruct(BindId);
		Declaration = &Ctx.Declarations.Get(Schema.DeclId);	
		TConstArrayView<FMemberId> MemberOrder = Declaration->GetMemberOrder();

		for (FMemberVisitor It(Schema); It.HasMore(); )
		{
			FMemberId Name = MemberOrder[It.GetIndex()];
			switch (It.PeekKind())
			{
				case EMemberKind::Leaf:		SaveMemberDelta(Out, Struct, Default, Name, Ctx, It.GrabLeaf());	break;
				case EMemberKind::Range:	SaveMemberDelta(Out, Struct, Default, Name, Ctx, It.GrabRange());	break;
				case EMemberKind::Struct:	SaveMemberDelta(Out, Struct, Default, Name, Ctx, It.GrabStruct());	break;
			}
		}
	}
	
	return Out.IsEmpty() ? nullptr : Out.BuildAndReset(Ctx.Scratch, *Declaration, Ctx.Declarations.GetDebug());
}

} // namespace PlainProps