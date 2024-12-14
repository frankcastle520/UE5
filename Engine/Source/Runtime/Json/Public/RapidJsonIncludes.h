// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Misc/AssertionMacros.h"
#include "Misc/CoreMiscDefines.h"

#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(push)
#pragma warning(disable : 6282) // Incorrect operator: Assignment of constant in Boolean context. Consider using '==' instead
#pragma warning(disable : 6313) // Incorrect operator: Zero-valued flag cannot be tested with bitwise-and. Use an equality test to check for zero-valued flags
#endif

THIRD_PARTY_INCLUDES_START
#ifndef RAPIDJSON_ASSERT
#define RAPIDJSON_ASSERT(x) check(x)
#endif

#ifndef RAPIDJSON_ERROR_CHARTYPE
#define RAPIDJSON_ERROR_CHARTYPE TCHAR
#endif

#ifndef RAPIDJSON_ERROR_STRING
#define RAPIDJSON_ERROR_STRING(x) TEXT(x)
#endif

#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"
#include "rapidjson/encodings.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/schema.h"
// just for non-user facing logging purposes use the English error descriptions
#include "rapidjson/error/en.h"
THIRD_PARTY_INCLUDES_END

#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(pop)
#endif

