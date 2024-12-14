// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMPropertyType.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/VVMTupleType.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VPropertyType);
TGlobalTrivialEmergentTypePtr<&VPropertyType::StaticCppClassInfo> VPropertyType::GlobalTrivialEmergentType;
DEFINE_TRIVIAL_VISIT_REFERENCES(VPropertyType)

DEFINE_DERIVED_VCPPCLASSINFO(VIntPropertyType);
TGlobalTrivialEmergentTypePtr<&VIntPropertyType::StaticCppClassInfo> VIntPropertyType::GlobalTrivialEmergentType;
DEFINE_TRIVIAL_VISIT_REFERENCES(VIntPropertyType)

DEFINE_DERIVED_VCPPCLASSINFO(VFloatPropertyType);
TGlobalTrivialEmergentTypePtr<&VFloatPropertyType::StaticCppClassInfo> VFloatPropertyType::GlobalTrivialEmergentType;
DEFINE_TRIVIAL_VISIT_REFERENCES(VFloatPropertyType)

DEFINE_DERIVED_VCPPCLASSINFO(VTypePropertyType)
TGlobalTrivialEmergentTypePtr<&VTypePropertyType::StaticCppClassInfo> VTypePropertyType::GlobalTrivialEmergentType;
template <typename TVisitor>
void VTypePropertyType::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(PackageName, TEXT("PackageName"));
	Visitor.Visit(ClassName, TEXT("ClassName"));
}

DEFINE_DERIVED_VCPPCLASSINFO(VClassPropertyType)
TGlobalTrivialEmergentTypePtr<&VClassPropertyType::StaticCppClassInfo> VClassPropertyType::GlobalTrivialEmergentType;
template <typename TVisitor>
void VClassPropertyType::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(ClassValue, TEXT("ClassValue"));
}

DEFINE_DERIVED_VCPPCLASSINFO(VTuplePropertyType)
TGlobalTrivialEmergentTypePtr<&VTuplePropertyType::StaticCppClassInfo> VTuplePropertyType::GlobalTrivialEmergentType;
template <typename TVisitor>
void VTuplePropertyType::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Type, TEXT("Type"));
}

DEFINE_DERIVED_VCPPCLASSINFO(VWrappedPropertyType)
TGlobalTrivialEmergentTypePtr<&VWrappedPropertyType::StaticCppClassInfo> VWrappedPropertyType::GlobalTrivialEmergentType;
template <typename TVisitor>
void VWrappedPropertyType::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Inner, TEXT("Inner"));
}

DEFINE_DERIVED_VCPPCLASSINFO(VArrayPropertyType)
TGlobalTrivialEmergentTypePtr<&VArrayPropertyType::StaticCppClassInfo> VArrayPropertyType::GlobalTrivialEmergentType;
DEFINE_TRIVIAL_VISIT_REFERENCES(VArrayPropertyType)

DEFINE_DERIVED_VCPPCLASSINFO(VMapPropertyType)
TGlobalTrivialEmergentTypePtr<&VMapPropertyType::StaticCppClassInfo> VMapPropertyType::GlobalTrivialEmergentType;
template <typename TVisitor>
void VMapPropertyType::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Key, TEXT("Key"));
}

DEFINE_DERIVED_VCPPCLASSINFO(VInterfacePropertyType)
TGlobalTrivialEmergentTypePtr<&VInterfacePropertyType::StaticCppClassInfo> VInterfacePropertyType::GlobalTrivialEmergentType;
template <typename TVisitor>
void VInterfacePropertyType::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(InterfaceValue, TEXT("InterfaceValue"));
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
