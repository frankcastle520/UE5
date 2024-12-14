// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAnimNextStateTreeModule.h"

namespace UE::AnimNext::StateTree
{

class FAnimNextStateTreeModule : public IAnimNextStateTreeModule
{
public:
	virtual void ShutdownModule() override;
	virtual void StartupModule() override;
};

}