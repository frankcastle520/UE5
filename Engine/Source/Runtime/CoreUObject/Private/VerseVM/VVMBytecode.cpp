// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMBytecode.h"
#include "VerseVM/VVMAbstractVisitor.h"
#include "VerseVM/VVMBytecodeOps.h"

namespace
{
struct FOpInfo
{
	const char* Name;
};
constexpr FOpInfo Ops[] = {
#define VISIT_OP(Name) {#Name},
	VERSE_ENUM_OPS(VISIT_OP)
#undef VISIT_OP
};
} // namespace

const char* Verse::ToString(Verse::EOpcode Opcode)
{
	return Ops[static_cast<size_t>(Opcode)].Name;
}

template <>
void Verse::Visit(FAbstractVisitor& Visitor, FRegisterIndex& Value, const TCHAR* ElementName)
{
	Visitor.Visit(Value.Index, ElementName);
}

const Verse::FLocation* Verse::GetLocation(FOpLocation* First, FOpLocation* Last, uint32 OpOffset)
{
	if (First == Last)
	{
		return nullptr;
	}
	for (auto I = First + (Last - First) / 2;
		 I != First;
		 I = First + (Last - First) / 2)
	{
		if (I->Begin > OpOffset)
		{
			Last = I;
		}
		else
		{
			First = I;
		}
	}
	return &First->Location;
}

template <>
void Verse::Visit(FAbstractVisitor& Visitor, FOpLocation& Value, const TCHAR* ElementName)
{
	Visitor.VisitObject(ElementName, [&Visitor, &Value] {
		Visitor.Visit(Value.Begin, TEXT("Begin"));
		Visit(Visitor, Value.Location, TEXT("Location"));
	});
}

template <>
void Verse::Visit(FAbstractVisitor& Visitor, FRegisterName& Value, const TCHAR* ElementName)
{
	Visitor.VisitObject(ElementName, [&Visitor, &Value] {
		Visit(Visitor, Value.Index, TEXT("Index"));
		Visit(Visitor, Value.Name, TEXT("Name"));
	});
}

template <>
void Verse::Visit(FAbstractVisitor& Visitor, FNamedParam& Value, const TCHAR* ElementName)
{
	Visitor.VisitObject(ElementName, [&Visitor, &Value] {
		Visit(Visitor, Value.Index, TEXT("Index"));
		Visit(Visitor, Value.Name, TEXT("Name"));
	});
}

#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)