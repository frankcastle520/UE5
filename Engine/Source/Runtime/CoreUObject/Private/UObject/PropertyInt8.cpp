// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UnrealType.h"

/*-----------------------------------------------------------------------------
	FInt8Property.
-----------------------------------------------------------------------------*/

FInt8Property::FInt8Property(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
	: Super(InOwner, InName, InObjectFlags)
{
}

FInt8Property::FInt8Property(FFieldVariant InOwner, const UECodeGen_Private::FInt8PropertyParams& Prop)
	: Super(InOwner, (const UECodeGen_Private::FPropertyParamsBaseWithOffset&)Prop)
{
}

#if WITH_EDITORONLY_DATA
FInt8Property::FInt8Property(UField* InField)
	: Super(InField)
{
}
#endif // WITH_EDITORONLY_DATA
