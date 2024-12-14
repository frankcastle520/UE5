// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMObject.h"
#include "VerseVM/Inline/VVMObjectInline.h"
#include "VerseVM/VVMCppClassInfo.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VObject);

template <typename TVisitor>
void VObject::VisitReferencesImpl(TVisitor& Visitor)
{
	// The actual visiting is done by the subclasses of this class
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
