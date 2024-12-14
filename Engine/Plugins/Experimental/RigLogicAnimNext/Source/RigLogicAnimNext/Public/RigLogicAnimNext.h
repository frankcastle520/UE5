// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "RigLogicInstanceDataPool.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRigLogicAnimNext, Log, All);

class FRigLogicAnimNextModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	UE::AnimNext::FRigLogicInstanceDataPool DataPool;
};