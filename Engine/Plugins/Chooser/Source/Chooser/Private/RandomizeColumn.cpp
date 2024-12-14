// Copyright Epic Games, Inc. All Rights Reserved.
#include "RandomizeColumn.h"
#include "ChooserIndexArray.h"
#include "ChooserPropertyAccess.h"
#if WITH_EDITOR
#include "StructUtils/PropertyBag.h"
#endif

bool FRandomizeContextProperty::GetValue(FChooserEvaluationContext& Context, const FChooserRandomizationContext*& OutResult) const
{
	return Binding.GetValuePtr(Context,OutResult);
}

FRandomizeColumn::FRandomizeColumn()
{
	InputValue.InitializeAs(FRandomizeContextProperty::StaticStruct());
}

void FRandomizeColumn::Filter(FChooserEvaluationContext& Context, const FChooserIndexArray& IndexListIn, FChooserIndexArray& IndexListOut) const
{
	int Count = IndexListIn.Num();
	int Selection = 0;

	const FChooserRandomizationContext* RandomizationContext = nullptr;
	if (InputValue.IsValid())
	{
		InputValue.Get<FChooserParameterRandomizeBase>().GetValue(Context,RandomizationContext);
	}
	
	if (Count > 1)
	{
		int LastSelectedIndex = -1;
		if (RandomizationContext)
		{
			if (const FRandomizationState* State = RandomizationContext->StateMap.Find(this))
			{
				LastSelectedIndex = State->LastSelectedRow;
			}
		}

		if (IndexListIn.HasCosts())
		{
			// find the lowest cost row
			float LowestCost = UE_MAX_FLT;
			uint32 LowestCostIndex = 0;
			for (const FChooserIndexArray::FIndexData& IndexData : IndexListIn)
			{
				if (IndexData.Cost < LowestCost)
				{
					LowestCost = IndexData.Cost;
					LowestCostIndex = IndexData.Index;
				}
			}

			// compute the sum of all weights/probabilities - only considering rows with cost nearly equal to the lowest cost
			float TotalWeight = 0;
			int MinCount = 0;
			for (const FChooserIndexArray::FIndexData& IndexData : IndexListIn)
			{
				if (FMath::IsNearlyEqual(LowestCost, IndexData.Cost, EqualCostThreshold))
				{
					float RowWeight = 1.0f;
					if (RowValues.Num() > static_cast<int32>(IndexData.Index))
					{
						RowWeight = RowValues[IndexData.Index];
					}
			
					if (IndexData.Index == LastSelectedIndex)
					{
						RowWeight *= RepeatProbabilityMultiplier;
					}
			
					TotalWeight += RowWeight;
					MinCount++;
				}
			}

			if (MinCount == 1)
			{
				// only one entry with lowest cost, we can't randomize
				IndexListOut.Push({LowestCostIndex, LowestCost});
				return;
			}
				

			// pick a random float from 0-total weight
			float RandomNumber = FMath::FRandRange(0.0f, TotalWeight);
			float Weight = 0;

			// add up the weights again, and select the index where our sum clears the random float
			for (; Selection < Count - 1; Selection++)
			{
				const FChooserIndexArray::FIndexData& IndexData = IndexListIn[Selection];
				if (FMath::IsNearlyEqual(LowestCost, IndexData.Cost, EqualCostThreshold))
				{
					float RowWeight = 1.0f;
				
					if (RowValues.IsValidIndex(IndexData.Index))
					{
						RowWeight = RowValues[IndexData.Index];
					}
				
					if (IndexData.Index == LastSelectedIndex)
					{
						RowWeight *= RepeatProbabilityMultiplier;
					}
				
					Weight += RowWeight;
					if (Weight > RandomNumber)
					{
						break;
					}
				}
			}	
		}
		else
		{
			// compute the sum of all weights/probabilities
			float TotalWeight = 0;
			for (const FChooserIndexArray::FIndexData& IndexData : IndexListIn)
			{
				float RowWeight = 1.0f;
				if (RowValues.Num() > static_cast<int32>(IndexData.Index))
				{
					RowWeight = RowValues[IndexData.Index];
				}
			
				if (IndexData.Index == LastSelectedIndex)
				{
					RowWeight *= RepeatProbabilityMultiplier;
				}
			
				TotalWeight += RowWeight;
			}

			// pick a random float from 0-total weight
			float RandomNumber = FMath::FRandRange(0.0f, TotalWeight);
			float Weight = 0;

			// add up the weights again, and select the index where our sum clears the random float
			for (; Selection < Count - 1; Selection++)
			{
				const FChooserIndexArray::FIndexData& IndexData = IndexListIn[Selection];
				float RowWeight = 1.0f;
			
				if (RowValues.IsValidIndex(IndexData.Index))
				{
					RowWeight = RowValues[IndexData.Index];
				}
			
				if (IndexData.Index == LastSelectedIndex)
				{
					RowWeight *= RepeatProbabilityMultiplier;
				}
			
				Weight += RowWeight;
				if (Weight > RandomNumber)
				{
					break;
				}
			}
		}
	}

	if (Selection < Count)
	{
		IndexListOut.Push(IndexListIn[Selection]);
	}
}

void FRandomizeColumn::SetOutputs(FChooserEvaluationContext& Context, int RowIndex) const
{
	if (InputValue.IsValid() && RowValues.IsValidIndex((RowIndex)))
	{
		const FChooserRandomizationContext* ConstRandomizationContext = nullptr;
		InputValue.Get<FChooserParameterRandomizeBase>().GetValue(Context,ConstRandomizationContext);
		if(ConstRandomizationContext)
		{
			FChooserRandomizationContext* RandomizationContext = const_cast<FChooserRandomizationContext*>(ConstRandomizationContext);
			FRandomizationState& State = RandomizationContext->StateMap.FindOrAdd(this, FRandomizationState());
			State.LastSelectedRow = RowIndex;
		}
	}
}
	#if WITH_EDITOR

	void FRandomizeColumn::AddToDetails(FInstancedPropertyBag& PropertyBag, int32 ColumnIndex, int32 RowIndex)
	{
		if (RowValues.IsValidIndex(RowIndex))
		{
			FName PropertyName("RowData",ColumnIndex);
			FPropertyBagPropertyDesc PropertyDesc(PropertyName, EPropertyBagPropertyType::Float);
			PropertyDesc.MetaData.Add(FPropertyBagPropertyDescMetaData("DisplayName", "Randomize"));
			PropertyBag.AddProperties({PropertyDesc});
			PropertyBag.SetValueFloat(PropertyName, RowValues[RowIndex]);
		}
	}

	void FRandomizeColumn::SetFromDetails(FInstancedPropertyBag& PropertyBag, int32 ColumnIndex, int32 RowIndex)
	{
		FName PropertyName("RowData", ColumnIndex);
		
		TValueOrError<float, EPropertyBagResult> Result = PropertyBag.GetValueFloat(PropertyName);
		if (float* Value = Result.TryGetValue())
		{
			RowValues[RowIndex] = *Value;
		}
	}

    #endif