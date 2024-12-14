// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMFailureContext.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMFrame.h"
#include "VerseVM/VVMTask.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VFailureContext);
TGlobalTrivialEmergentTypePtr<&VFailureContext::StaticCppClassInfo> VFailureContext::GlobalTrivialEmergentType;

template <typename TVisitor>
void VFailureContext::VisitReferencesImpl(TVisitor& Visitor)
{
	TIntrusiveTree<VFailureContext>::VisitReferencesImpl(Visitor);
	Visitor.Visit(Task, TEXT("Task"));
	Visitor.Visit(Frame, TEXT("Frame"));
	Visitor.Visit(IncomingEffectToken, TEXT("IncomingEffectToken"));
	Visitor.Visit(BeforeThenEffectToken, TEXT("BeforeThenEffectToken"));
	Visitor.Visit(DoneEffectToken, TEXT("DoneEffectToken"));
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)