// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Declares.h"
#include "Chaos/ImplicitFwd.h"
#include "ChaosDebugDraw/ChaosDDTypes.h"
#include "Math/MathFwd.h"

#if CHAOS_DEBUG_DRAW

namespace Chaos
{
	struct CHAOS_API FChaosDDImplicitObject
	{
		static void Draw(
			const FConstImplicitObjectPtr& Implicit,
			const FRigidTransform3& Transform, 
			const FColor& Color, 
			float LineThickness, 
			float Duration);
	};
}

namespace Chaos::Private
{
	void ChaosDDRenderImplicitObject(
		ChaosDD::Private::IChaosDDRenderer& Renderer, 
		const FConstImplicitObjectPtr& Implicit, 
		const FRigidTransform3& Transform,
		const FColor& Color,
		float LineThickness, 
		float Duration);
}

#endif