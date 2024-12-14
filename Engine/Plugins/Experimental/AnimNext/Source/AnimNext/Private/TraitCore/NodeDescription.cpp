// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraitCore/NodeDescription.h"

#include "Serialization/Archive.h"
#include "TraitCore/TraitRegistry.h"
#include "TraitCore/NodeTemplate.h"
#include "TraitCore/NodeTemplateRegistry.h"

namespace UE::AnimNext
{
	void FNodeDescription::Serialize(FArchive& Ar)
	{
		const FNodeTemplateRegistry& NodeTemplateRegistry = FNodeTemplateRegistry::Get();

		Ar << NodeID;

		if (Ar.IsSaving())
		{
			const FNodeTemplate* NodeTemplate = NodeTemplateRegistry.Find(TemplateHandle);

			uint32 TemplateUID = NodeTemplate->GetUID();
			Ar << TemplateUID;
		}
		else if (Ar.IsLoading())
		{
			uint32 TemplateUID = 0;
			Ar << TemplateUID;

			TemplateHandle = NodeTemplateRegistry.Find(TemplateUID);
		}
		else
		{
			// Counting, etc
			int32 TemplateOffset = TemplateHandle.GetTemplateOffset();
			Ar << TemplateOffset;
		}

		// Use our template to serialize our traits
		const FNodeTemplate* NodeTemplate = NodeTemplateRegistry.Find(TemplateHandle);

		const uint32 NumTraits = NodeTemplate->GetNumTraits();
		const FTraitTemplate* TraitTemplates = NodeTemplate->GetTraits();
		for (uint32 TraitIndex = 0; TraitIndex < NumTraits; ++TraitIndex)
		{
			const FTraitRegistryHandle TraitHandle = TraitTemplates[TraitIndex].GetRegistryHandle();
			const FTrait* Trait = FTraitRegistry::Get().Find(TraitHandle);

			FAnimNextTraitSharedData* SharedData = TraitTemplates[TraitIndex].GetTraitDescription(*this);

			Trait->SerializeTraitSharedData(Ar, *SharedData);
		}
	}
}
