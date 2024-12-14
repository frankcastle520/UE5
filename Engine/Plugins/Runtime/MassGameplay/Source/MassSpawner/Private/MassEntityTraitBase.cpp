// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityTraitBase.h"

//----------------------------------------------------------------------//
//  UMassEntityTraitBase
//----------------------------------------------------------------------//
void UMassEntityTraitBase::DestroyTemplate() const 
{

}

bool UMassEntityTraitBase::ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World, FAdditionalTraitRequirements& OutTraitRequirements) const
{
	return true;
}

#if WITH_EDITOR
UMassEntityTraitBase::FOnNewTraitType UMassEntityTraitBase::OnNewTraitTypeEvent;

void UMassEntityTraitBase::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		OnNewTraitTypeEvent.Broadcast(*this);
	}
}

#endif // WITH_EDITOR

//-----------------------------------------------------------------------------
// DEPRECATED
//-----------------------------------------------------------------------------
bool UMassEntityTraitBase::ValidateTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	TArray<const UStruct*> AdditionalRequirements;
	FAdditionalTraitRequirements AdditionalTraitRequirementsWrapper(AdditionalRequirements);
	return ValidateTemplate(BuildContext, World, AdditionalTraitRequirementsWrapper);
}