// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavMesh/LinkGenerationConfig.h"
#include "BaseGeneratedNavLinksProxy.h"
#include "NavAreas/NavArea_Default.h"

#if WITH_RECAST
#include "Detour/DetourNavLinkBuilderConfig.h"
#endif //WITH_RECAST

FNavLinkGenerationJumpDownConfig::FNavLinkGenerationJumpDownConfig()
{
	AreaClass = UNavArea_Default::StaticClass();
}

#if WITH_RECAST

void FNavLinkGenerationJumpDownConfig::CopyToDetourConfig(dtNavLinkBuilderJumpDownConfig& OutDetourConfig) const
{
	OutDetourConfig.enabled = bEnabled;
	OutDetourConfig.jumpLength = JumpLength;
	OutDetourConfig.jumpDistanceFromEdge = JumpDistanceFromEdge;
	OutDetourConfig.jumpMaxDepth = JumpMaxDepth;
	OutDetourConfig.jumpHeight = JumpHeight;
	OutDetourConfig.jumpEndsHeightTolerance	= JumpEndsHeightTolerance;
	OutDetourConfig.samplingSeparationFactor = SamplingSeparationFactor;
	OutDetourConfig.filterDistanceThreshold = FilterDistanceThreshold;
	OutDetourConfig.linkBuilderFlags = LinkBuilderFlags;

	if (LinkProxy)
	{
		OutDetourConfig.linkUserId = LinkProxy->GetId().GetId();	
	}
}

#endif //WITH_RECAST
