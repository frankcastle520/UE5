// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Templates/Requires.h"
#include "Templates/UnrealTypeTraits.h"

#include <type_traits>

class AActor;

#if WITH_EDITOR
template <typename ActorType UE_REQUIRES(std::is_base_of_v<AActor, ActorType>)>
struct FWorldPartitionActorDescType
{};

#define DEFINE_ACTORDESC_TYPE(ActorType, ActorDescType) \
template <>												\
struct FWorldPartitionActorDescType<ActorType>			\
{														\
	typedef class ActorDescType Type;					\
};
#else
#define DEFINE_ACTORDESC_TYPE(ActorType, ActorDescType)
#endif
