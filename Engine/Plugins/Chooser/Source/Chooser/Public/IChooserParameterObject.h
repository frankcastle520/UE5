// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IChooserParameterBase.h"
#include "IChooserParameterObject.generated.h"

class UObject;

USTRUCT()
struct FChooserParameterObjectBase : public FChooserParameterBase
{
	GENERATED_BODY()

	virtual bool GetValue(FChooserEvaluationContext& Context, FSoftObjectPath& OutResult) const { return false; }
	virtual bool GetValue(FChooserEvaluationContext& Context, UObject*& OutResult) const { return false; }
	virtual bool SetValue(FChooserEvaluationContext& Context, UObject* OutResult) const { return false; }

#if WITH_EDITOR
	virtual UClass* GetAllowedClass() const { return UObject::StaticClass(); }
#endif
};
