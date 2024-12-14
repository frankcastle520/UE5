// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMProgram.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/VVMCppClassInfo.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VProgram);
TGlobalTrivialEmergentTypePtr<&VProgram::StaticCppClassInfo> VProgram::GlobalTrivialEmergentType;

template <typename TVisitor>
void VProgram::VisitReferencesImpl(TVisitor& Visitor)
{
	PackageMap.Visit(Visitor, TEXT("PackageMap"));
	TupleTypeMap.Visit(Visitor, TEXT("TupleTypeMap"));
	Visitor.Visit(Intrinsics, TEXT("Intrinsics"));
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
