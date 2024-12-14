// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/ChaosFleshGetFleshAssetNode.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosFleshGetFleshAssetNode)

void FGetFleshAssetDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Output))
	{
		FManagedArrayCollection Collection;
		SetValue(Context, MoveTemp(Collection), &Output);

		const UFleshAsset* FleshAssetValue = FleshAsset;
		if (!FleshAssetValue)
		{
			if (const UE::Dataflow::FEngineContext* EngineContext = Context.AsType<UE::Dataflow::FEngineContext>())
			{
				FleshAssetValue = Cast<UFleshAsset>(EngineContext->Owner);
			}
		}

		if (FleshAssetValue)
		{
			if (const FFleshCollection* AssetCollection = FleshAssetValue->GetCollection())
			{
				SetValue(Context, (const FManagedArrayCollection&)(*AssetCollection), &Output);
			}
		}
	}
}
