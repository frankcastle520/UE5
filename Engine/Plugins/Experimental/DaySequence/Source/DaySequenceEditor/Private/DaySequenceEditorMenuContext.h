// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Package.h"
#include "DaySequenceEditorMenuContext.generated.h"

class FDaySequenceEditorToolkit;

UCLASS()
class UDaySequenceEditorMenuContext : public UObject
{
	GENERATED_BODY()
public:
	TWeakPtr<FDaySequenceEditorToolkit> Toolkit;
};
