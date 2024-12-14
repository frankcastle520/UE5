// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UnrealType.h"

/*-----------------------------------------------------------------------------
	FInt64Property.
-----------------------------------------------------------------------------*/

FInt64Property::FInt64Property(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
	: Super(InOwner, InName, InObjectFlags)
{
}

FInt64Property::FInt64Property(FFieldVariant InOwner, const UECodeGen_Private::FInt64PropertyParams& Prop)
	: Super(InOwner, (const UECodeGen_Private::FPropertyParamsBaseWithOffset&)Prop)
{
}

#if WITH_EDITORONLY_DATA
FInt64Property::FInt64Property(UField* InField)
	: Super(InField)
{
}
#endif // WITH_EDITORONLY_DATA
