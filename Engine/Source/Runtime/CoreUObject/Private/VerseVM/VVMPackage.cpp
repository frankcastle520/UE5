// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMPackage.h"
#include "Containers/AnsiString.h"
#include "Containers/UnrealString.h"
#include "Containers/Utf8String.h"
#include "Templates/Casts.h"
#include "UObject/Package.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMEngineEnvironment.h"
#include "VerseVM/VVMTupleType.h"
#include "VerseVM/VVMVerse.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VPackage);
TGlobalTrivialEmergentTypePtr<&VPackage::StaticCppClassInfo> VPackage::GlobalTrivialEmergentType;

UPackage* VPackage::GetUPackage(const TCHAR* UEPackageName) const
{
	return GetUPackageInternal(StringCast<UTF8CHAR>(UEPackageName));
}

UPackage* VPackage::GetOrCreateUPackage(FAllocationContext Context, const TCHAR* UEPackageName)
{
	auto Utf8PackageName = StringCast<UTF8CHAR>(UEPackageName);
	UPackage* Package = GetUPackageInternal(Utf8PackageName);
	if (Package == nullptr)
	{
		IEngineEnvironment* Environment = VerseVM::GetEngineEnvironment();
		ensure(Environment);
		FString ScratchSpace;
		const TCHAR* AdornedPackageName = Environment->AdornPackageName(UEPackageName, PackageStage, ScratchSpace);
		Package = Environment->CreateUPackage(Context, AdornedPackageName);
		UPackageMap.AddValue(Context, Utf8PackageName, VValue(Package));
	}
	return Package;
}

void VPackage::NotifyUsedTupleType(FAllocationContext Context, VTupleType* TupleType)
{
	if (!UsedTupleTypes)
	{
		UsedTupleTypes.Set(Context, VWeakCellMap::New(Context));
	}
	UsedTupleTypes->Add(Context, TupleType, TupleType);
}

void VPackage::SetStage(EPackageStage InPackageStage)
{
	if (PackageStage == InPackageStage)
	{
		return;
	}
	PackageStage = InPackageStage;
	IEngineEnvironment* Environment = VerseVM::GetEngineEnvironment();
	ensure(Environment);
	for (uint32 Index = UPackageMap.Num(); Index-- > 0;)
	{
		const VArray& Utf8PackageName = UPackageMap.GetName(Index);
		VValue PackageValue = UPackageMap.GetValue(Index);
		if (PackageValue.IsUObject())
		{
			UPackage* Package = Cast<UPackage>(PackageValue.AsUObject());
			FString UPackageName = Utf8PackageName.AsString();
			FString ScratchSpace;
			const TCHAR* AdornedPackageName = Environment->AdornPackageName(*UPackageName, PackageStage, ScratchSpace);
			Package->Rename(AdornedPackageName);
		}
	}
}

UPackage* VPackage::GetUPackageInternal(FUtf8StringView UEPackageName) const
{
	VValue PackageValue = UPackageMap.Lookup(UEPackageName);
	return PackageValue.IsUObject() ? Cast<UPackage>(PackageValue.AsUObject()) : nullptr;
}

template <typename TVisitor>
void VPackage::VisitReferencesImpl(TVisitor& Visitor)
{
	Map.Visit(Visitor, TEXT("DefinitionMap"));
	if (FVersionedDigest* DigestVariant = DigestVariants[(int)EDigestVariant::PublicAndEpicInternal].GetPtrOrNull())
	{
		Visitor.Visit(DigestVariant->Code, TEXT("PublicAndEpicInternalDigest.Code"));
	}
	if (FVersionedDigest* DigestVariant = DigestVariants[(int)EDigestVariant::PublicOnly].GetPtrOrNull())
	{
		Visitor.Visit(DigestVariant->Code, TEXT("PublicOnlyDigest.Code"));
	}
	Visitor.Visit(PackageName, TEXT("PackageName"));
	Visitor.Visit(UsedTupleTypes, TEXT("UsedTupleTypes"));
	UPackageMap.Visit(Visitor, TEXT("UPackageMap"));
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
