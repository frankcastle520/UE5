// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMFunction.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/Inline/VVMValueObjectInline.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMProcedure.h"
#include "VerseVM/VVMValuePrinting.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VFunction);
TGlobalTrivialEmergentTypePtr<&VFunction::StaticCppClassInfo> VFunction::GlobalTrivialEmergentType;

template <typename TVisitor>
void VFunction::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.VisitFunction(Procedure->Name->AsStringView(), [this, &Visitor] {
		Visitor.Visit(Procedure, TEXT("Procedure"));
		Visitor.Visit(Self, TEXT("Self"));
		Visitor.Visit(ParentScope, TEXT("ParentScope"));
	});
}

void VFunction::ToStringImpl(FStringBuilderBase& Builder, FAllocationContext Context, const FCellFormatter& Formatter)
{
	Builder.Append(TEXT("Procedure="));
	Formatter.Append(Builder, Context, *Procedure);
	if (Self)
	{
		Builder.Append(TEXT(", Self="));
		// NOTE: (yiliang.siew) `Self` should always be a class object instance, which should be a `VValueObject` or a `UObject`.
		// If no `Self` is present, it should be a `VFalse`.
		VValue SelfValue = Self.Get();
		if (SelfValue.IsCell())
		{
			Formatter.Append(Builder, Context, SelfValue.AsCell());
		}
		else if (SelfValue.IsUObject())
		{
			Builder.Appendf(TEXT("%s"), *SelfValue.AsUObject()->GetName());
		}
		else
		{
			V_DIE("Invalid type of `Self` object encountered!");
		}
	}
	if (ParentScope)
	{
		Builder.Append(TEXT(", ParentScope="));
		Formatter.Append(Builder, Context, *ParentScope);
	}
}

bool VFunction::HasSelf() const
{
	return !Self.Get().IsUninitialized();
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
