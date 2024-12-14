// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMGlobalTrivialEmergentTypePtr.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"

namespace Verse
{

void FGlobalTrivialEmergentTypePtrRoot::Visit(FAbstractVisitor& Visitor)
{
	VisitImpl(Visitor);
}

void FGlobalTrivialEmergentTypePtrRoot::Visit(FMarkStackVisitor& Visitor)
{
	VisitImpl(Visitor);
}

template <typename TVisitor>
void FGlobalTrivialEmergentTypePtrRoot::VisitImpl(TVisitor& Visitor)
{
	Visitor.Visit(EmergentType, TEXT("EmergentType"));
}

VEmergentType& FGlobalTrivialEmergentTypePtr::Create(FAllocationContext Context, VCppClassInfo* ClassInfo)
{
	VEmergentType* Object = VEmergentType::New(Context, VTrivialType::Singleton.Get(), ClassInfo);
	VEmergentType* Expected = nullptr;
	EmergentType.compare_exchange_strong(Expected, Object);
	VEmergentType* Result;
	if (Expected)
	{
		Result = Expected;
	}
	else
	{
		Result = Object;
		new FGlobalTrivialEmergentTypePtrRoot(Context, Object);
	}
	V_DIE_UNLESS(EmergentType.load() == Result);
	return *Result;
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
