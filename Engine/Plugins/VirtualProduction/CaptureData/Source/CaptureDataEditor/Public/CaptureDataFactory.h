// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"

#include "CaptureDataFactory.generated.h"


//////////////////////////////////////////////////////////////////////////
// UMeshCaptureDataFactory

UCLASS(hidecategories=Object)
class UMeshCaptureDataFactory
	: public UFactory
{
	GENERATED_BODY()

public:
	//~ UFactory Interface
	UMeshCaptureDataFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InWarn) override;
};

//////////////////////////////////////////////////////////////////////////
// UFootageCaptureDataFactory

UCLASS(hideCategories=Object)
class UFootageCaptureDataFactory
	: public UFactory
{
	GENERATED_BODY()

public:
	//~ UFactory Interface
	UFootageCaptureDataFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InWarn) override;
};