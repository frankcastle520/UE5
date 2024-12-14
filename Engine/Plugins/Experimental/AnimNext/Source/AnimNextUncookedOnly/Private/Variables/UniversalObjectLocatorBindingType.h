// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Variables/IVariableBindingType.h"

namespace UE::AnimNext::UncookedOnly
{
	struct FClassProxy;
}

namespace UE::AnimNext::UncookedOnly
{

// Provides information about object proxy parameter sources
class FUniversalObjectLocatorBindingType : public IVariableBindingType
{
public:
	FUniversalObjectLocatorBindingType();
	virtual ~FUniversalObjectLocatorBindingType() override;

private:
	// IVariableBindingType interface
	virtual TSharedRef<SWidget> CreateEditWidget(const TSharedRef<IPropertyHandle>& InPropertyHandle, const FAnimNextParamType& InType) const override;
	virtual FText GetDisplayText(TConstStructView<FAnimNextVariableBindingData> InBindingData) const override;
	virtual FText GetTooltipText(TConstStructView<FAnimNextVariableBindingData> InBindingData) const override;
	virtual void BuildBindingGraphFragment(const FRigVMCompileSettings& InSettings, const FBindingGraphFragmentArgs& InArgs, URigVMPin*& OutExecTail, FVector2D& OutLocation) const override;

	static const UClass* GetClass(TConstStructView<FAnimNextVariableBindingData> InBindingData);

	TSharedRef<FClassProxy> FindOrCreateClassProxy(const UClass* InClass) const; 

	// Cached map of classes -> proxy
	mutable TMap<TObjectKey<UClass>, TSharedPtr<FClassProxy>> ClassMap;

	// Hook for object reinstancing
	FDelegateHandle OnObjectsReinstancedHandle;
};

}
