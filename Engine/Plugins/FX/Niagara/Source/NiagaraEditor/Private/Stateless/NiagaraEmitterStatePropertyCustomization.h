// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PropertyHandle.h"
#include "IPropertyTypeCustomization.h"

struct FNiagaraEmitterStateData;

class FNiagaraEmitterStatePropertyCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	const FNiagaraEmitterStateData* GetEmitterState() const;

	EVisibility GetEnableDistanceCullingVisibility() const;
	EVisibility GetMinDistanceVisibility() const;
	EVisibility GetMaxDistanceVisibility() const;

private:
	TSharedPtr<IPropertyHandle> EmitterStatePropertyHandle;
	TWeakObjectPtr<UObject>		EmitterStateOwnerObject;
};
