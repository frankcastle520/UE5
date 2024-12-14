// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMTupleType.h"
#include "VerseVM/VVMEngineEnvironment.h"
#include "VerseVM/VVMPackage.h"
#include "VerseVM/VVMVerse.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VTupleType);
TGlobalTrivialEmergentTypePtr<&VTupleType::StaticCppClassInfo> VTupleType::GlobalTrivialEmergentType;

template <typename TVisitor>
void VTupleType::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(UEMangledName, TEXT("UEMangledName"));

	TWriteBarrier<VPropertyType>* ElementTypes = GetElementTypes();
	if constexpr (TVisitor::bIsAbstractVisitor)
	{
		uint64 NumElementsScratch = NumElements;
		Visitor.BeginArray(TEXT("ElementTypes"), NumElementsScratch);
		for (uint32 Index = 0; Index < NumElements; ++Index)
		{
			Visitor.Visit(ElementTypes[Index], TEXT("ElementType"));
		}
		Visitor.EndArray();

		uint64 NumAssociatedUStructs = AssociatedUStructs.Num();
		Visitor.BeginMap(TEXT("AssociatedUStructs"), NumAssociatedUStructs);
		for (auto It = AssociatedUStructs.CreateIterator(); It; ++It)
		{
			Visitor.VisitPair([&Visitor, &It] {
				if (Visitor.IsMarked(It->Key.Get(), TEXT("Key")))
				{
					Visitor.Visit(It->Value, TEXT("Value"));
				}
			});
		}
		Visitor.EndMap();
	}
	else
	{
		for (uint32 Index = 0; Index < NumElements; ++Index)
		{
			Visitor.Visit(ElementTypes[Index], TEXT("ElementType"));
		}

		for (auto It = AssociatedUStructs.CreateIterator(); It; ++It)
		{
			Visitor.Visit(It->Key, TEXT("Key"));
			Visitor.Visit(It->Value, TEXT("Value"));
		}
	}
}

void VTupleType::CreateUStruct(FAllocationContext Context, VPackage* Scope, TWriteBarrier<VValue>& Result)
{
	IEngineEnvironment* Environment = VerseVM::GetEngineEnvironment();
	check(Environment);
	Environment->CreateUStruct(Context, this, Scope, Result);
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
