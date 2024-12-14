// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


class FArchive;


/**
 * Utility functions
 */
class FDisplayClusterConfigurationUtils
{
private:
	FDisplayClusterConfigurationUtils() = default;

public:
	// Returns true if we're serializing a template or archetype. False otherwise.
	static bool IsSerializingTemplate(const FArchive& Ar);

};
