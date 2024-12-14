// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialValuesDynamic/DMMaterialValueRenderTargetDynamic.h"

UDMMaterialValueRenderTargetDynamic::UDMMaterialValueRenderTargetDynamic()
{
}

#if WITH_EDITOR
TSharedPtr<FJsonValue> UDMMaterialValueRenderTargetDynamic::JsonSerialize() const
{
	return nullptr;
}

bool UDMMaterialValueRenderTargetDynamic::JsonDeserialize(const TSharedPtr<FJsonValue>& InJsonValue)
{
	return false;
}

bool UDMMaterialValueRenderTargetDynamic::IsDefaultValue() const
{
	return true;
}

void UDMMaterialValueRenderTargetDynamic::ApplyDefaultValue()
{
}

void UDMMaterialValueRenderTargetDynamic::CopyDynamicPropertiesTo(UDMMaterialComponent* InDestinationComponent) const
{	
}
#endif

void UDMMaterialValueRenderTargetDynamic::SetMIDParameter(UMaterialInstanceDynamic* InMID) const
{
}
