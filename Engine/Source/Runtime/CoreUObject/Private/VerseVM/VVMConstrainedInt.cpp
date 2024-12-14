// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMConstrainedInt.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VConstrainedInt);
TGlobalTrivialEmergentTypePtr<&VConstrainedInt::StaticCppClassInfo> VConstrainedInt::GlobalTrivialEmergentType;

template <typename TVisitor>
inline void VConstrainedInt::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.VisitConstrainedInt([this, &Visitor] {
		Visitor.Visit(Min, TEXT("Min"));
		Visitor.Visit(Max, TEXT("Max"));
	});
}

bool VConstrainedInt::SubsumesImpl(FAllocationContext Context, VValue Value)
{
	if (!Value.IsInt())
	{
		return false;
	}

	VInt Int = Value.AsInt();
	return (!GetMin() || VInt::Lte(Context, GetMin(), Int))
		&& (!GetMax() || VInt::Gte(Context, GetMax(), Int));
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
