// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextPool.h"

struct FAnimNextModuleInstance;

namespace UE::AnimNext
{
	template<typename T> struct TPoolHandle;
}

namespace UE::AnimNext
{

// Opaque handle representing a module instance
using FModuleHandle = TPoolHandle<FAnimNextModuleInstance>;

}