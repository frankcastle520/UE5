// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraitCore/TraitUID.h"

namespace UE::AnimNext
{
	struct FNodeTemplate;

	/**
	  * FNodeTemplateBuilder
	  * 
	  * Utility to help construct node templates.
	  */
	struct ANIMNEXT_API FNodeTemplateBuilder
	{
		FNodeTemplateBuilder() = default;

		// Adds the specified trait type to the node template
		void AddTrait(FTraitUID TraitUID);

		// Returns a node template for the provided list of traits
		// The node template will be built into the provided buffer and a pointer to it is returned
		FNodeTemplate* BuildNodeTemplate(TArray<uint8>& NodeTemplateBuffer) const;

		// Returns a node template for the provided list of traits
		// The node template will be built into the provided buffer and a pointer to it is returned
		static FNodeTemplate* BuildNodeTemplate(const TArray<FTraitUID>& InTraitUIDs, TArray<uint8>& NodeTemplateBuffer);

		// Resets the node template builder
		void Reset();

	private:
		static uint32 GetNodeTemplateUID(const TArray<FTraitUID>& InTraitUIDs);
		static void AppendTemplateTrait(
			const TArray<FTraitUID>& InTraitUIDs, int32 TraitIndex,
			TArray<uint8>& NodeTemplateBuffer);

		TArray<FTraitUID> TraitUIDs;	// The list of traits to use when building the node template
	};
}
