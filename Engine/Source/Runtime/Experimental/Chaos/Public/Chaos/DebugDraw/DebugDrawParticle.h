// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Declares.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/ShapeInstanceFwd.h"

#if CHAOS_DEBUG_DRAW

namespace Chaos
{
	struct CHAOS_API FChaosDDParticle
	{
		// Draw a particle's shapes with auto-coloring. SpaceTransform is simulation space to world transform (for RBAN)
		static void DrawShapes(
			const FRigidTransform3& SpaceTransform,
			const FGeometryParticleHandle* Particle);

		// Draw a particle's shapes with auto-coloring
		static void DrawShapes(
			const FGeometryParticleHandle* Particle);

		// Draw a particle's shapes with manual coloring
		static void DrawShapes(
			const FGeometryParticleHandle* Particle,
			const FColor& Color);

		// Draw a particle's optimized shapes with auto-coloring if it has any
		static bool DrawOptimizedShapes(
			const FGeometryParticleHandle* Particle);
	};
}

#endif