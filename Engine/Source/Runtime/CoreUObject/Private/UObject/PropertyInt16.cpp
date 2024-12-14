// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UnrealType.h"

/*-----------------------------------------------------------------------------
	FInt16Property.
-----------------------------------------------------------------------------*/

FInt16Property::FInt16Property(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
	: Super(InOwner, InName, InObjectFlags)
{
}

FInt16Property::FInt16Property(FFieldVariant InOwner, const UECodeGen_Private::FInt16PropertyParams& Prop)
	: Super(InOwner, (const UECodeGen_Private::FPropertyParamsBaseWithOffset&)Prop)
{
}

#if WITH_EDITORONLY_DATA
FInt16Property::FInt16Property(UField* InField)
	: Super(InField)
{
}
#endif // WITH_EDITORONLY_DATA
