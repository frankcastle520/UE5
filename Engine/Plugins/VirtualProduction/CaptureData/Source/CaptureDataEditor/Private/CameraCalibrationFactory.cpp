// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraCalibrationFactory.h"
#include "CameraCalibration.h"


/* UCameraCalibrationFactory
 *******************************/

UCameraCalibrationFactory::UCameraCalibrationFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UCameraCalibration::StaticClass();
}


UObject* UCameraCalibrationFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InWarn)
{
	return NewObject<UCameraCalibration>(InParent, InClass, InName, InFlags);
};