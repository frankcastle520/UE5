// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

namespace UE
{
COREPRECISEFP_API bool PreciseFPEqual(float Lhs, float Rhs);
COREPRECISEFP_API bool PreciseFPEqual(double Lhs, double Rhs);

COREPRECISEFP_API uint32 PreciseFPHash(float);
COREPRECISEFP_API uint32 PreciseFPHash(double);
} // namespace UE