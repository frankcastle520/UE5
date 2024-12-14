// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/ScalarVertexPropertyGroupCustomization.h"
#include "Dataflow/DataflowCollectionAddScalarVertexPropertyNode.h"

namespace UE::Dataflow
{
	TSharedRef<IPropertyTypeCustomization> FScalarVertexPropertyGroupCustomization::MakeInstance()
	{
		return MakeShareable(new FScalarVertexPropertyGroupCustomization);
	}

	TArray<FName> FScalarVertexPropertyGroupCustomization::GetTargetGroupNames(const FManagedArrayCollection& Collection) const
	{
		return DataflowAddScalarVertexPropertyCallbackRegistry::Get().GetTargetGroupNames();
	}

	FName FScalarVertexPropertyGroupCustomization::GetCollectionPropertyName() const
	{
		return GET_MEMBER_NAME_CHECKED(FDataflowCollectionAddScalarVertexPropertyNode, Collection);
	}
}  // End namespace UE::Chaos::ClothAsset
