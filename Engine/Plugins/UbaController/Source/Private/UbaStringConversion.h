// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Containers/StringConv.h"
#include "UbaExports.h"

using FStringToUbaStringConversion = TStringConversion<TStringConvert<TCHAR, uba::tchar>>;
using FUbaStringToStringConversion = TStringConversion<TStringConvert<uba::tchar, TCHAR>>;

#define TCHAR_TO_UBASTRING(STR) FStringToUbaStringConversion(STR).Get()
#define UBASTRING_TO_TCHAR(STR) FUbaStringToStringConversion(STR).Get()

