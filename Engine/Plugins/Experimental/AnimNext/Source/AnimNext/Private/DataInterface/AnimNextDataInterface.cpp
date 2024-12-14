// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterface/AnimNextDataInterface.h"
#include "AnimNextExecuteContext.h"

#if WITH_EDITOR	
#include "Engine/ExternalAssetDependencyGatherer.h"
REGISTER_ASSETDEPENDENCY_GATHERER(FExternalAssetDependencyGatherer, UAnimNextDataInterface);
#endif // WITH_EDITOR

UAnimNextDataInterface::UAnimNextDataInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ExtendedExecuteContext.SetContextPublicDataStruct(FAnimNextExecuteContext::StaticStruct());
}

TConstArrayView<FAnimNextImplementedDataInterface> UAnimNextDataInterface::GetImplementedInterfaces() const
{
	return ImplementedInterfaces;
}

const FAnimNextImplementedDataInterface* UAnimNextDataInterface::FindImplementedInterface(const UAnimNextDataInterface* InDataInterface) const
{
	return ImplementedInterfaces.FindByPredicate([InDataInterface](const FAnimNextImplementedDataInterface& InImplementedDataInterface)
	{
		return InImplementedDataInterface.DataInterface == InDataInterface;
	});
}