// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDObjectPool.h"

bool FChaosVDObjectPoolCVars::bUseObjectPool = true;
FAutoConsoleVariableRef FChaosVDObjectPoolCVars::CVarUseObjectPool(TEXT("p.Chaos.VD.Tool.UseObjectPool"),FChaosVDObjectPoolCVars::bUseObjectPool, TEXT("Set to false to disable the use of a pool system for uobjects."));
