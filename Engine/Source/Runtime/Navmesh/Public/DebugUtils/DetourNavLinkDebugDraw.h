// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Detour/DetourNavLinkBuilder.h"

//@UE BEGIN
struct duDebugDraw;
class dtNavLinkBuilder;

enum duNavLinkBuilderDrawFlags
{
	DRAW_WALKABLE_SURFACE =	1 << 0,
	DRAW_BORDERS =			1 << 1,
	DRAW_SELECTED_EDGE =	1 << 2,
	DRAW_TRAJECTORY =		1 << 3,
	DRAW_LAND_SAMPLES =		1 << 4,
	DRAW_COLLISION_SLICES =	1 << 5,
	DRAW_LINKS =			1 << 6,
	DRAW_FILTERED_LINKS =	1 << 7,
};

NAVMESH_API void duDebugDrawNavLinkBuilder(duDebugDraw* dd, const dtNavLinkBuilder& linkBuilder, unsigned int drawFlags, const struct dtNavLinkBuilder::EdgeSampler* es);
//@UE END
