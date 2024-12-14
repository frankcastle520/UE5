// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UnrealType.h"

/*-----------------------------------------------------------------------------
	FUInt64Property.
-----------------------------------------------------------------------------*/

FUInt64Property::FUInt64Property(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
	: TProperty_Numeric(InOwner, InName, InObjectFlags)
{
}

FUInt64Property::FUInt64Property(FFieldVariant InOwner, const UECodeGen_Private::FUInt64PropertyParams& Prop)
	: Super(InOwner, (const UECodeGen_Private::FPropertyParamsBaseWithOffset&)Prop)
{
}

#if WITH_EDITORONLY_DATA
FUInt64Property::FUInt64Property(UField* InField)
	: Super(InField)
{
}
#endif // WITH_EDITORONLY_DATA
