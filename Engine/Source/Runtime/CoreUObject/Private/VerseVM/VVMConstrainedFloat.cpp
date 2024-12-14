// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMConstrainedFloat.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/VVMValue.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VConstrainedFloat);
TGlobalTrivialEmergentTypePtr<&VConstrainedFloat::StaticCppClassInfo> VConstrainedFloat::GlobalTrivialEmergentType;

template <typename TVisitor>
inline void VConstrainedFloat::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.VisitConstrainedFloat([this, &Visitor] {
		Visitor.Visit(Min, TEXT("Min"));
		Visitor.Visit(Max, TEXT("Max"));
	});
}

bool VConstrainedFloat::SubsumesImpl(FAllocationContext Context, VValue Value)
{
	if (!Value.IsFloat())
	{
		return false;
	}

	VFloat Float = Value.AsFloat();
	return GetMin() <= Float && GetMax() >= Float;
}

} // namespace Verse

#endif