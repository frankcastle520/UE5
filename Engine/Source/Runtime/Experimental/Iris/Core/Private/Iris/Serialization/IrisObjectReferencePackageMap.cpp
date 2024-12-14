// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/IrisObjectReferencePackageMap.h"
#include "HAL/IConsoleManager.h"

namespace UE
{
	namespace Net
	{
		bool bEnableIrisPackageMapNameExports = true;
		static FAutoConsoleVariableRef CVarEnableIrisPackageMapNameExports(TEXT("net.iris.EnableIrisPackageMapNameExports"), bEnableIrisPackageMapNameExports, TEXT("If enabled, iris captures and exports fnames when calling into old serialziation code instead of serializing a strings."));
	}
}

bool UIrisObjectReferencePackageMap::SerializeObject(FArchive& Ar, UClass* InClass, UObject*& Obj, FNetworkGUID* OutNetGUID)
{
	if (!PackageMapExports)
	{
		return false;
	}

	constexpr uint8 MaxNumReferences = 255U;
	UE::Net::FIrisPackageMapExports::FObjectReferenceArray* References = &PackageMapExports->References;

	if (Ar.IsSaving())
	{
		int32 Index = MaxNumReferences;
		if (!References->Find(Obj, Index) && References->Num() < MaxNumReferences)
		{
			Index = References->Add(Obj);
		}

		if (References->IsValidIndex(Index))
		{
			uint8 IndexByte = static_cast<uint8>(Index);
			Ar << IndexByte;
		}
		else
		{
			ensureMsgf(false, TEXT("UIrisObjectReferencePackageMap::SerializeObject, failed to serialize object reference with Index %u (%s). A Maximum of %u references are currently supported by this PackageMap"),
				Index, *GetNameSafe(Obj), MaxNumReferences);
			uint8 IndexByte = MaxNumReferences;
			Ar << IndexByte;
			return false;
		}
	}
	else
	{
		uint8 IndexByte = MaxNumReferences;
		Ar << IndexByte;
		if (References->IsValidIndex(IndexByte) && IndexByte < MaxNumReferences)
		{
			Obj = (*References)[IndexByte];
		}
		else
		{
			ensureMsgf(false, TEXT("UIrisObjectReferencePackageMap::SerializeObject, failed to read object reference index %u is out of bounds. Current ObjectReference num: %u"), IndexByte, References->Num());
			return false;
		}
	}

	return true;
}

bool UIrisObjectReferencePackageMap::SerializeName(FArchive& Ar, FName& InName)
{
	using namespace UE::Net;

	if (!(PackageMapExports && bEnableIrisPackageMapNameExports))
	{
		return Super::SerializeName(Ar, InName);
	}

	FIrisPackageMapExports::FNameArray* Names = &PackageMapExports->Names;
	constexpr uint8 MaxNumNames = 255U;

	if (Ar.IsSaving())
	{
		int32 Index = MaxNumNames;
		if (!Names->Find(InName, Index) && Names->Num() < MaxNumNames)
		{
			Index = Names->Add(InName);
		}

		if (Names->IsValidIndex(Index))
		{
			uint8 IndexByte = static_cast<uint8>(Index);
			Ar << IndexByte;
		}
		else
		{
			ensureMsgf(false, TEXT("UIrisObjectReferencePackageMap::SerializeName, failed to serialize name with Index %u (%s). A Maximum of %u name are currently supported by this PackageMap"),
				Index, *InName.ToString(), MaxNumNames);
			uint8 IndexByte = MaxNumNames;
			Ar << IndexByte;
			return false;
		}
	}
	else
	{
		uint8 IndexByte = MaxNumNames;
		Ar << IndexByte;
		if (Names->IsValidIndex(IndexByte) && IndexByte < MaxNumNames)
		{
			InName = (*Names)[IndexByte];
		}
		else
		{
			ensureMsgf(false, TEXT("UIrisObjectReferencePackageMap::SerializeName, failed to read name index %u is out of bounds. Current Name num: %u"), IndexByte, Names->Num());
			return false;
		}
	}

	return true;
}

void UIrisObjectReferencePackageMap::InitForRead(const UE::Net::FIrisPackageMapExports* InPackageMapExports, const UE::Net::FNetTokenResolveContext& InNetTokenResolveContext)
{ 
	PackageMapExports = const_cast<UE::Net::FIrisPackageMapExports*>(InPackageMapExports);
	NetTokenResolveContext = InNetTokenResolveContext;
}

void UIrisObjectReferencePackageMap::InitForWrite(UE::Net::FIrisPackageMapExports* InPackageMapExports)
{
	PackageMapExports = InPackageMapExports;
	if (ensureMsgf(PackageMapExports, TEXT("UIrisObjectReferencePackageMap requires valid PackageMapExports to capture exports.")))
	{
		PackageMapExports->Reset();
	}
}

