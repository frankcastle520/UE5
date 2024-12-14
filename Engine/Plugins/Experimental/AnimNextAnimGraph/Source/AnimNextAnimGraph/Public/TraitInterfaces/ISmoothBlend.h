// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AlphaBlend.h"
#include "TraitCore/ITraitInterface.h"
#include "TraitCore/TraitBinding.h"

class UCurveFloat;

namespace UE::AnimNext
{
	/**
	 * ISmoothBlend
	 *
	 * This interface exposes blend smoothing related information.
	 */
	struct ANIMNEXTANIMGRAPH_API ISmoothBlend : ITraitInterface
	{
		DECLARE_ANIM_TRAIT_INTERFACE(ISmoothBlend, 0x1c2c1739)

		// Returns the desired blend time for the specified child
		virtual float GetBlendTime(FExecutionContext& Context, const TTraitBinding<ISmoothBlend>& Binding, int32 ChildIndex) const;

		// Returns the desired blend type for the specified child
		virtual EAlphaBlendOption GetBlendType(FExecutionContext& Context, const TTraitBinding<ISmoothBlend>& Binding, int32 ChildIndex) const;

		// Returns the desired blend curve for the specified child
		virtual UCurveFloat* GetCustomBlendCurve(FExecutionContext& Context, const TTraitBinding<ISmoothBlend>& Binding, int32 ChildIndex) const;

#if WITH_EDITOR
		virtual const FText& GetDisplayName() const override;
		virtual const FText& GetDisplayShortName() const override;
#endif // WITH_EDITOR
	};

	/**
	 * Specialization for trait binding.
	 */
	template<>
	struct TTraitBinding<ISmoothBlend> : FTraitBinding
	{
		// @see ISmoothBlend::GetBlendTime
		float GetBlendTime(FExecutionContext& Context, int32 ChildIndex) const
		{
			return GetInterface()->GetBlendTime(Context, *this, ChildIndex);
		}

		// @see ISmoothBlend::GetBlendType
		EAlphaBlendOption GetBlendType(FExecutionContext& Context, int32 ChildIndex) const
		{
			return GetInterface()->GetBlendType(Context, *this, ChildIndex);
		}

		// @see ISmoothBlend::GetCustomBlendCurve
		UCurveFloat* GetCustomBlendCurve(FExecutionContext& Context, int32 ChildIndex) const
		{
			return GetInterface()->GetCustomBlendCurve(Context, *this, ChildIndex);
		}

	protected:
		const ISmoothBlend* GetInterface() const { return GetInterfaceTyped<ISmoothBlend>(); }
	};
}
