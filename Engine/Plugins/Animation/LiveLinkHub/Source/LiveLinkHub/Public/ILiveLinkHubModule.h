// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

class LIVELINKHUB_API ILiveLinkHubModule : public IModuleInterface
{
public:
	virtual void PreinitializeLiveLinkHub() = 0;

	/** Launch the slate application hosting the live link hub. */
	virtual void StartLiveLinkHub(bool bLauncherDistribution = false) = 0;

	virtual void ShutdownLiveLinkHub() = 0;
};

