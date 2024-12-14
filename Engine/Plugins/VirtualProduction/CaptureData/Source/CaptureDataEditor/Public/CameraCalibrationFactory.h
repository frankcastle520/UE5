// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"

#include "CameraCalibrationFactory.generated.h"

/**
 * Implements a factory for UCameraCalibrationFactory objects from file
 */
UCLASS(hidecategories=Object)
class UCameraCalibrationFactory
	: public UFactory
{
	GENERATED_BODY()

public:
	UCameraCalibrationFactory();

	//~ UFactory interface
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InWarn) override;
};
