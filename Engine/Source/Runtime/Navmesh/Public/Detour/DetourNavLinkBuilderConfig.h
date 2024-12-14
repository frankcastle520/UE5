// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"

enum dtNavLinkBuilderFlags : unsigned short
{
	DT_NAVLINK_CREATE_CENTER_POINT_LINK = 1 << 0,
	DT_NAVLINK_CREATE_EXTREMITY_LINKS	= 1 << 1,
};

/** Configuration for generated jump down links. */
struct dtNavLinkBuilderJumpDownConfig
{
	/// Should this config be used to generate links.
	bool enabled = true;

	/// Horizontal length of the jump. How far from the starting point we will look for ground. [Limit: > 0] [Units: wu]
	float jumpLength = 150.f;

	/// How far from the edge is the jump started. [Limit: > 0] [Units: wu]
	float jumpDistanceFromEdge = 10.f;

	/// How far below the starting height we want to look for landing ground. [Limit: > 0] [Units: wu]
	float jumpMaxDepth = 150.f;

	/// Peak height relative to the height of the starting point. [Limit: >= 0] [Units: wu]
	float jumpHeight = 50.f;

	/// Tolerance at both ends of the jump to find ground. [Limit: > 0] [Units: wu]
	float jumpEndsHeightTolerance = 50.f;

	/// Value multiplied by CellSize to find the distance between sampling trajectories. Default is 1.
	/// Larger values improve generation speed but might introduce sampling errors. [Limit: >= 1]
	float samplingSeparationFactor = 1.f;

	/// When filtering similar links, distance used to compare between segment endpoints to match similar links.
	/// Use greater distance for more filtering (0 to deactivate filtering). [Limit: > 0] [Units: wu]
	float filterDistanceThreshold = 80.f;

	/// Flags used to indicate how links will be added.
	unsigned short linkBuilderFlags = DT_NAVLINK_CREATE_CENTER_POINT_LINK;

	/// Cached parabola constant fitting the configuration parameters. 
	float cachedParabolaConstant = 0;

	/// Cached value used when computing jump trajectory.
	float cachedDownRatio = 0;
	
	/// User id used to handle links made from this configuration.
	unsigned long long linkUserId = 0;

	/// User defined flags assigned to the off-mesh connections
	unsigned short polyFlag = 0;

	/// User defined area id assigned to the off-mesh connections
	unsigned char area = 0;

	/// Initialize the configuration by computing cached values.
	NAVMESH_API void init();
};

/** Configuration for generated jump over links. */
struct dtNavLinkBuilderJumpOverConfig
{
	/// Should this config be used to generate links.
	bool enabled = false;	// @todo: Disabled for now since this configuration not currently exposed.

	/// Maximum jumpable gap size used when matching edges to jump over. [Limit: > 0] [Units: wu]
	float jumpGapWidth = 200.f;

	/// Vertical tolerance used when matching edges to jump over. [Limit: > 0] [Units: wu]
	float jumpGapHeightTolerance = 100.f; 

	/// How far from the center of the gap is the jump started. [Limit: > 0] [Units: wu]
	float jumpDistanceFromGapCenter = 100.f; 

	/// Height at the top of the jump trajectory. [Limit: > 0] [Units: wu]
	float jumpHeight = 100.f; 

	/// Tolerance at both ends of the jump to find ground. [Limit: > 0] [Units: wu]
	float jumpEndsHeightTolerance = 50.f;

	/// Value multiplied by CellSize to find the distance between sampling trajectories. Default is 1.
	/// Larger values improve generation speed but might introduce sampling errors. [Limit: >= 1]
	float samplingSeparationFactor = 1.f;

	/// When filtering similar links, distance used to compare between segment endpoints to match similar links.
	/// Use greater distance for more filtering (0 to deactivate filtering). [Limit: > 0] [Units: wu]
	float filterDistanceThreshold = 80.f;
};
