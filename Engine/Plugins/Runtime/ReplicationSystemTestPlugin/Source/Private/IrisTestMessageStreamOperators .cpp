// Copyright Epic Games, Inc. All Rights Reserved.

#include "IrisTestMessageStreamOperators.h"
#include "Iris/ReplicationSystem/Filtering/NetObjectFilter.h"

namespace UE::Net
{

FTestMessage& operator<<(FTestMessage& Message, ENetFilterStatus Status)
{
	return Message << (Status == ENetFilterStatus::Disallow ? TEXT("ENetFilterStatus::Disallow") : TEXT("ENetFilterStatus::Allow"));
}

}
