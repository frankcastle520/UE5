// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimModule/SuspensionBaseInterface.h"

namespace Chaos
{

bool FSuspensionBaseInterface::IsBehaviourType(eSimModuleTypeFlags InType) const
{ 
	return (InType & Raycast); 
}

void FSuspensionBaseInterface::SetTargetPoint(const FSuspensionTargetPoint& InTargetPoint)
{
	TargetPoint = InTargetPoint;
}

} // namespace Chaos
