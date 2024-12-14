// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MyAutoRTFMTestObject.generated.h"

UCLASS()
class UMyAutoRTFMTestObject : public UObject
{
	GENERATED_BODY()

public:
	UMyAutoRTFMTestObject(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get()) : Value(42)
	{
		UObject* const Obj = ObjectInitializer.GetObj();
		UObject* const Outer = Obj->GetOuter();

		if (Outer->IsA<UMyAutoRTFMTestObject>())
		{
			UMyAutoRTFMTestObject* const OuterAsType = static_cast<UMyAutoRTFMTestObject*>(Outer);
			OuterAsType->Value += 13;
		}
	}

	int Value;
};
