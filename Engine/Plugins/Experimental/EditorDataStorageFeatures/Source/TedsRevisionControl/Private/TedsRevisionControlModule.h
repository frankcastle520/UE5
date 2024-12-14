// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "UObject/GCObject.h"

class FReferenceCollector;

class FTedsRevisionControlModule : public IModuleInterface, public FGCObject
{
public:
	~FTedsRevisionControlModule() override = default;

	//
	// IModuleInterface
	//

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	
	//
	// FGCObject
	//
	
	void AddReferencedObjects(FReferenceCollector& Collector) override;
	FString GetReferencerName() const override;
};
