// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Set.h"

class AActor;
class FPrimitiveComponentId;

namespace UE::AvaMedia::PlayableUtils
{
	void AddPrimitiveComponentIds(const AActor* InActor, TSet<FPrimitiveComponentId>& InComponentIds);
}