// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Include StrProperty.h.inl's includes before defining the macros, in case the macros 'poison' other headers or there are re-entrant includes.
#include "UObject/StrPropertyIncludes.h.inl"

#include "Containers/UnrealString.h"

#define UE_STRPROPERTY_CLASS                FStrProperty
#define UE_STRPROPERTY_STRINGTYPE           FString
#define UE_STRPROPERTY_CASTCLASSFLAG        CASTCLASS_FStrProperty
#define UE_STRPROPERTY_PROPERTYPARAMSSTRUCT FStrPropertyParams
	#include "UObject/StrProperty.h.inl"
#undef UE_STRPROPERTY_PROPERTYPARAMSSTRUCT
#undef UE_STRPROPERTY_CASTCLASSFLAG
#undef UE_STRPROPERTY_STRINGTYPE
#undef UE_STRPROPERTY_CLASS
