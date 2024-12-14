// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMScope.h"
#include "VerseVM/Inline/VVMCellInline.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VScope)
TGlobalTrivialEmergentTypePtr<&VScope::StaticCppClassInfo> VScope::GlobalTrivialEmergentType;

template <typename TVisitor>
void VScope::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(SuperClass, TEXT("SuperClass"));
}

} // namespace Verse

#endif
