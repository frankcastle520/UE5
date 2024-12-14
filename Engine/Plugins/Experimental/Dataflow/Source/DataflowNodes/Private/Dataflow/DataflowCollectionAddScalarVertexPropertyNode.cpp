// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowCollectionAddScalarVertexPropertyNode.h"

#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowTools.h"
#include "Misc/LazySingleton.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowCollectionAddScalarVertexPropertyNode)

#define LOCTEXT_NAMESPACE "DataflowCollectionAddScalarVertexProperty"


DataflowAddScalarVertexPropertyCallbackRegistry& DataflowAddScalarVertexPropertyCallbackRegistry::Get()
{
	return TLazySingleton<DataflowAddScalarVertexPropertyCallbackRegistry>::Get();
}

void DataflowAddScalarVertexPropertyCallbackRegistry::TearDown()
{
	TLazySingleton<DataflowAddScalarVertexPropertyCallbackRegistry>::TearDown();
}

void DataflowAddScalarVertexPropertyCallbackRegistry::RegisterCallbacks(TUniquePtr<IDataflowAddScalarVertexPropertyCallbacks>&& Callbacks)
{
	AllCallbacks.Add(Callbacks->GetName(), MoveTemp(Callbacks));
}

void DataflowAddScalarVertexPropertyCallbackRegistry::DeregisterCallbacks(const FName& CallbacksName)
{
	AllCallbacks.Remove(CallbacksName);
}

TArray<FName> DataflowAddScalarVertexPropertyCallbackRegistry::GetTargetGroupNames() const
{
	TArray<FName> UniqueNames;
	
	for (const TPair<FName, TUniquePtr<IDataflowAddScalarVertexPropertyCallbacks>>& CallbacksEntry : AllCallbacks)
	{
		for (const FName& GroupName : CallbacksEntry.Value->GetTargetGroupNames())
		{
			UniqueNames.AddUnique(GroupName);
		}
	}
	return UniqueNames;
}

TArray<UE::Dataflow::FRenderingParameter> DataflowAddScalarVertexPropertyCallbackRegistry::GetRenderingParameters() const
{
	TArray<UE::Dataflow::FRenderingParameter> UniqueParameters;

	for (const TPair<FName, TUniquePtr<IDataflowAddScalarVertexPropertyCallbacks>>& CallbacksEntry : AllCallbacks)
	{
		for (const UE::Dataflow::FRenderingParameter& RenderingParameter : CallbacksEntry.Value->GetRenderingParameters())
		{
			UniqueParameters.AddUnique(RenderingParameter);
		}
	}
	return UniqueParameters;
}


FDataflowCollectionAddScalarVertexPropertyNode::FDataflowCollectionAddScalarVertexPropertyNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterOutputConnection(&Collection, &Collection);
	RegisterOutputConnection(&AttributeKey);
}

TArray<UE::Dataflow::FRenderingParameter> FDataflowCollectionAddScalarVertexPropertyNode::GetRenderParametersImpl() const
{
	return DataflowAddScalarVertexPropertyCallbackRegistry::Get().GetRenderingParameters();
}


void FDataflowCollectionAddScalarVertexPropertyNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace UE::Dataflow;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		// Evaluate in collection
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		if (!Name.IsEmpty())
		{
			const FName InName(Name);
			const FName InGroup = TargetGroup.Name;
			TManagedArray<float>& Scalar = InCollection.AddAttribute<float>(InName, InGroup);

			const int32 MaxWeightIndex = FMath::Min(VertexWeights.Num(), Scalar.Num());
			if (VertexWeights.Num() > 0 && VertexWeights.Num() != Scalar.Num())
			{
				FDataflowTools::LogAndToastWarning(*this,
					LOCTEXT("VertexCountMismatchHeadline", "Vertex count mismatch."),
					FText::Format(LOCTEXT("VertexCountMismatchDetails", "Vertex weights in the node: {0}\n Vertices in group \"{1}\" in the Collection: {2}"),
						VertexWeights.Num(),
						FText::FromName(InGroup),
						Scalar.Num()));
			}

			for (int32 VertexID = 0; VertexID < MaxWeightIndex; ++VertexID)
			{
				Scalar[VertexID] = VertexWeights[VertexID];
			}
		}

		SetValue(Context, MoveTemp(InCollection), &Collection);
	}
	else if (Out->IsA<FCollectionAttributeKey>(&AttributeKey))
	{
		SetValue(Context, FCollectionAttributeKey(Name,"Vertices"), &AttributeKey);
	}
}

#undef LOCTEXT_NAMESPACE
