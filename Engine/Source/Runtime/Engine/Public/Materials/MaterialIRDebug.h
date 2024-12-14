// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Materials/MaterialIRCommon.h"

#if WITH_EDITOR

namespace UE::MIR
{

//
void DebugDumpIRUseGraph(const FMaterialIRModule& Module);

} // namespace UE::MIR

#endif // #if WITH_EDITOR
