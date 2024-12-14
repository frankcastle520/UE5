// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMNativeFunction.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMNames.h"
#include "VerseVM/VVMPackage.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VNativeFunction);
TGlobalTrivialEmergentTypePtr<&VNativeFunction::StaticCppClassInfo> VNativeFunction::GlobalTrivialEmergentType;

template <typename TVisitor>
void VNativeFunction::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Self, TEXT("Self"));
}

void VNativeFunction::SetThunk(Verse::VPackage* Package, FUtf8StringView VerseScopePath, FUtf8StringView DecoratedName, FThunkFn NativeThunkPtr)
{
	// Function names are decorated twice: Once with the scope path they are defined in,
	// and once with the scope path of their base definition (usually these two are the same)
	Verse::VNativeFunction* Function = Package->LookupDefinition<Verse::VNativeFunction>(VerseScopePath, DecoratedName);
	if (!ensure(Function))
	{
		return;
	}

	Function->Thunk = NativeThunkPtr;
}

bool VNativeFunction::HasSelf() const
{
	return !!Self;
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
