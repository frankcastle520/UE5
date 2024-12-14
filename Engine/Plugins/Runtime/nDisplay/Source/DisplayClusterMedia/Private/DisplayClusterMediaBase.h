// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "OpenColorIOColorSpace.h"


/**
 * Base media adapter class
 */
class FDisplayClusterMediaBase
{
public:
	FDisplayClusterMediaBase(const FString& InMediaId, const FString& InClusterNodeId, bool bInLateOCIO = false)
		: MediaId(InMediaId)
		, ClusterNodeId(InClusterNodeId)
		, bLateOCIO(bInLateOCIO)
	{ }

	virtual ~FDisplayClusterMediaBase() = default;

public:

	/** Returns ID of this media adapter */
	const FString& GetMediaId() const
	{
		return MediaId;
	}

	/** Returns current cluster node ID */
	const FString& GetClusterNodeId() const
	{
		return ClusterNodeId;
	}

	/** Returns current late OCIO configuration/state */
	bool IsLateOCIO() const
	{
		return bLateOCIO;
	}

private:

	/** ID of this media adapter */
	const FString MediaId;

	/** Cluster node ID we're running on */
	const FString ClusterNodeId;

	/** Is OCIO expected to be applied late on the receiver side */
	const bool bLateOCIO = false;
};
