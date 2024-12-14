// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/CoreNet.h"
#include "Net/Core/NetToken/NetToken.h"
#include "IrisObjectReferencePackageMap.generated.h"

// Forward declarations
class FNetworkGUID;

namespace UE::Net
{
	// In order to properly capture exported data when calling in to old style NetSerialize methods
	// we need to capture and inject certain types.
	struct FIrisPackageMapExports
	{
		typedef TArray<TObjectPtr<UObject>, TInlineAllocator<4>> FObjectReferenceArray;
		typedef TArray<FName, TInlineAllocator<4>> FNameArray;

		void Reset()
		{
			References.Reset();
			Names.Reset();
		}

		FObjectReferenceArray References;
		FNameArray Names;
	};
}

/**
 * Custom packagemap implementation used to be able to capture exports such as UObject* references, names and NetTokens from external serialization.
 * Exports written when using this packagemap will be captured in an array and serialized as an index.
 * When reading using this packagemap exports will be read as an index and resolved by picking the corresponding entry from the provided array containing the data associated with the export.
 */
UCLASS(transient, MinimalAPI)
class UIrisObjectReferencePackageMap : public UPackageMap
{
public:
	GENERATED_BODY()

	// We override SerializeObject in order to be able to capture object references
	virtual bool SerializeObject(FArchive& Ar, UClass* InClass, UObject*& Obj, FNetworkGUID* OutNetGUID) override;

	// Override SerializeName in order to be able to capture name and serialize them with iris instead.
	virtual bool SerializeName(FArchive& Ar, FName& InName);

	// Init for read, we need to set the exports from which we are going to read our data.
	IRISCORE_API void InitForRead(const UE::Net::FIrisPackageMapExports* PackageMapExports, const UE::Net::FNetTokenResolveContext& InNetTokenResolveContext);

	// Init for write, all captured exports will be serialized as in index and added to the PackageMapExports for later export using iris.
	IRISCORE_API void InitForWrite(UE::Net::FIrisPackageMapExports* PackageMapExports);

	virtual const UE::Net::FNetTokenResolveContext* GetNetTokenResolveContext() const  override { return &NetTokenResolveContext; }

protected:
	UE::Net::FIrisPackageMapExports* PackageMapExports = nullptr;
	UE::Net::FNetTokenResolveContext NetTokenResolveContext;
};
