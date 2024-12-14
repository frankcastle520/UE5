// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Templates/PimplPtr.h"
#include "Templates/SharedPointer.h"

class IMessageBus;

class FTraceService
{
public:
	ENGINE_API FTraceService();
	ENGINE_API FTraceService(TSharedPtr<IMessageBus> Bus);
	
private:
	TPimplPtr<class FTraceServiceImpl> Impl;
};
