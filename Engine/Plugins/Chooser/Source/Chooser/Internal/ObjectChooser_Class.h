// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IObjectChooser.h"
#include "ObjectChooser_Class.generated.h"

USTRUCT(DisplayName = "Class", Meta = (ResultType = "Class", Category = "Basic", Tooltip = "A reference to a Class.\nOnly for use in Choosers with ResultType set to Sub Class Of"))
struct CHOOSER_API FClassChooser : public FObjectChooserBase
{
	GENERATED_BODY()
	
	// FObjectChooserBase interface
	virtual UObject* ChooseObject(FChooserEvaluationContext& Context) const final override;
	virtual EIteratorStatus IterateObjects(FObjectChooserIteratorCallback Callback) const final override;
public: 
	UPROPERTY(EditAnywhere, Category = "Parameters")
	TObjectPtr<UClass> Class;
};
