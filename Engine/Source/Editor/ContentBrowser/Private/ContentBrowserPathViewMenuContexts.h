// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"
#include "UObject/Object.h"

#include "ContentBrowserPathViewMenuContexts.generated.h"

class SPathView;

/**
 * Context for the PathView setting combo button
 */
UCLASS()
class UContentBrowserPathViewContextMenuContext : public UObject
{
	GENERATED_BODY()

public:
	FName OwningContentBrowserName;
	TWeakPtr<SPathView> PathView;
};
