// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/InternalNetSerializers.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"

FArrayPropertyNetSerializerConfig::FArrayPropertyNetSerializerConfig()
: FNetSerializerConfig()
{
	ConfigTraits = ENetSerializerConfigTraits::NeedDestruction;
}

FArrayPropertyNetSerializerConfig::~FArrayPropertyNetSerializerConfig() = default;
