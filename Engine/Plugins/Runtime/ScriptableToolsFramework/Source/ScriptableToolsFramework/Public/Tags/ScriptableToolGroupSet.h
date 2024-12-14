// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Templates/SubclassOf.h"
#include "Tags/ScriptableToolGroupTag.h"

#include "ScriptableToolGroupSet.generated.h"

USTRUCT()
struct SCRIPTABLETOOLSFRAMEWORK_API FScriptableToolGroupSet
{
	GENERATED_BODY()

	/**
	 * Note: This type needs to be specified explicitly for Groups because we can't use a typedef for a UPROPERTY.
	 */
	typedef TSet<TSubclassOf<UScriptableToolGroupTag>> FGroupSet;

public:

	bool Matches(const FScriptableToolGroupSet& OtherSet) const
	{
		return !GetGroups().Intersect(OtherSet.GetGroups()).IsEmpty();
	}

	void SetGroups(const FGroupSet& GroupsIn)
	{
		Groups = GroupsIn;
		SanitizeGroups();
	}

	const FGroupSet& GetGroups() const
	{
		return Groups;
	}

	FGroupSet& GetGroups()
	{
		return Groups;
	}

private:

	void  SanitizeGroups();

	UPROPERTY()
	TSet<TSubclassOf<UScriptableToolGroupTag>> Groups;
};