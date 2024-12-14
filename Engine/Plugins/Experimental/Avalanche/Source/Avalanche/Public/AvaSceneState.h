// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaTag.h"
#include "Containers/Array.h"
#include "Containers/Set.h"
#include "UObject/Object.h"
#include "AvaSceneState.generated.h"

class UAvaAttribute;
class UAvaSceneSettings;
struct FAvaTagHandle;

/** Object providing attribute information of the Scene */
UCLASS(MinimalAPI)
class UAvaSceneState : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UAvaSceneSettings* InSceneSettings);

	AVALANCHE_API bool AddTagAttribute(const FAvaTagHandle& InTagHandle);
	AVALANCHE_API bool RemoveTagAttribute(const FAvaTagHandle& InTagHandle);
	AVALANCHE_API bool ContainsTagAttribute(const FAvaTagHandle& InTagHandle) const;

	AVALANCHE_API bool AddNameAttribute(FName InName);
	AVALANCHE_API bool RemoveNameAttribute(FName InName);
	AVALANCHE_API bool ContainsNameAttribute(FName InName) const;

private:
	/** In-play Scene Attributes. Can be added to / removed from while in-play */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAvaAttribute>> SceneAttributes;
};
