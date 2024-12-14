// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHICoreResourceCollection.h"
#include "VulkanResources.h"

#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
using FVulkanResourceCollection = UE::RHICore::FGenericResourceCollection;

template<>
struct TVulkanResourceTraits<FRHIResourceCollection>
{
	using TConcreteType = FVulkanResourceCollection;
};
#endif