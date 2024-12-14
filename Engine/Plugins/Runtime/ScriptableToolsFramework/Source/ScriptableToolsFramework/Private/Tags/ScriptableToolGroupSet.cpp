// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tags/ScriptableToolGroupSet.h"

void FScriptableToolGroupSet::SanitizeGroups()
{
	FGroupSet GroupSetCopy = Groups;
	GroupSetCopy.Remove(nullptr);
	Groups = GroupSetCopy;
}

