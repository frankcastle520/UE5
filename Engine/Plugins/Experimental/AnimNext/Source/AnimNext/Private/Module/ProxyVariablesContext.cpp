// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module/ProxyVariablesContext.h"
#include "Module/AnimNextModuleInstance.h"

namespace UE::AnimNext
{

FAnimNextPublicVariablesProxy& FProxyVariablesContext::GetPublicVariablesProxy() const
{
	return ModuleInstance.PublicVariablesProxy;
}

}