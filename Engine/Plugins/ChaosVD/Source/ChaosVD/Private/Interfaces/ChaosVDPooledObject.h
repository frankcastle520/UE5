// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"
#include "ChaosVDPooledObject.generated.h"

UINTERFACE()
class UChaosVDPooledObject : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface used by any object that can be pooled
 */
class CHAOSVD_API IChaosVDPooledObject
{
	GENERATED_BODY()

public:
	virtual void OnAcquired() PURE_VIRTUAL(IChaosVDPooledObject::OnAcquired)
	virtual void OnDisposed() PURE_VIRTUAL(IChaosVDPooledObject::OnDisposed)
};
