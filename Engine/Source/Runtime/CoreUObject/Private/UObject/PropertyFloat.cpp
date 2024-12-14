// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Math/PreciseFP.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UnrealType.h"

/*-----------------------------------------------------------------------------
	FFloatProperty.
-----------------------------------------------------------------------------*/

FFloatProperty::FFloatProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
	: Super(InOwner, InName, InObjectFlags)
{
	PropertyFlags |= CPF_HasGetValueTypeHash;
}

FFloatProperty::FFloatProperty(FFieldVariant InOwner, const UECodeGen_Private::FFloatPropertyParams& Prop)
	: Super(InOwner, (const UECodeGen_Private::FPropertyParamsBaseWithOffset&)Prop)
{
	PropertyFlags |= CPF_HasGetValueTypeHash;
}

#if WITH_EDITORONLY_DATA
FFloatProperty::FFloatProperty(UField* InField)
	: Super(InField)
{
}
#endif // WITH_EDITORONLY_DATA

bool FFloatProperty::Identical(const void* A, const void* B, uint32 PortFlags) const
{
	return UE::PreciseFPEqual(*static_cast<const float*>(A), B ? *static_cast<const float*>(B) : 0.0f);
}

uint32 FFloatProperty::GetValueTypeHashInternal(const void* Src) const
{
	return UE::PreciseFPHash(*static_cast<const float*>(Src));
}
