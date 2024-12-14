// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IChooserColumn.h"
#include "IChooserParameterRandomize.h"
#include "ChooserPropertyAccess.h"
#include "StructUtils/InstancedStruct.h"
#include "RandomizeColumn.generated.h"

USTRUCT(DisplayName = "Randomize Property Binding")
struct CHOOSER_API FRandomizeContextProperty :  public FChooserParameterRandomizeBase
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Meta = (BindingType = "FChooserRandomizationContext", BindingAllowFunctions = "false", BindingColor = "StructPinTypeColor"), Category = "Binding")
	FChooserPropertyBinding Binding;
	
	virtual bool GetValue(FChooserEvaluationContext& Context, const FChooserRandomizationContext*& OutResult) const override;
	virtual bool IsBound() const override
	{
		return Binding.IsBoundToRoot || !Binding.PropertyBindingChain.IsEmpty();
	}

	CHOOSER_PARAMETER_BOILERPLATE();
};

USTRUCT(DisplayName = "Randomize", Meta = (Category = "Random", Tooltip = "The Randomize column will randomly select between whatever values have passed all filters.\n The value specified in each cell is a probability weighting for the row.\n  A row with a value twice as likely as another will be twice as likely to be selected.\n Using the optional RandomizationContext binding, it can track the most recent selection, and reduce the probability of randomly picking the same entry twice"))
struct CHOOSER_API FRandomizeColumn : public FChooserColumnBase
{
	GENERATED_BODY()
	public:
	FRandomizeColumn();
	
	UPROPERTY(EditAnywhere, NoClear, Meta = (ExcludeBaseStruct, BaseStruct = "/Script/Chooser.RandomizeContextProperty", ToolTip="Optional reference to a ChooserRandomizationContext struct. If bound, this is used to store the most recent selection (for each Choosers referencing it - you only need to create one variable per Character or context), for use with RepateProbabilityMultiplier to reduce the chance of selecting the same entry twice."), Category = "Data")
	FInstancedStruct InputValue;
	
	
	UPROPERTY(EditAnywhere, Category= "Data", meta=(ClampMin="0.0",Tooltip="Multiplies the weight of the previous chosen result (set to 0 to never pick the same result twice in a row)"));
	float RepeatProbabilityMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, Category= "Data", meta=(ClampMin="0.0",Tooltip="When columns with scoring are used, randomize will pick from among all rows that have a cost nearly equal to the minumum cost, using this threshold"));
	float EqualCostThreshold = 0.001f;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category=Data, meta=(ToolTip="DefaultRowValue will be assigned to cells when new rows are created"));
	float DefaultRowValue = 1.0f;
#endif
	
	UPROPERTY()
	TArray<float> RowValues; 
	
	virtual void Filter(FChooserEvaluationContext& Context, const FChooserIndexArray& IndexListIn, FChooserIndexArray& IndexListOut) const override;
	virtual void SetOutputs(FChooserEvaluationContext& Context, int RowIndex) const override;
	
	virtual bool HasFilters() const override { return true; }
	virtual bool HasOutputs() const override { return true; }

	#if WITH_EDITOR
    	virtual bool EditorTestFilter(int32 RowIndex) const override
    	{
    		return true;
    	}

		virtual void AddToDetails(FInstancedPropertyBag& PropertyBag, int32 ColumnIndex, int32 RowIndex) override;
		virtual void SetFromDetails(FInstancedPropertyBag& PropertyBag, int32 ColumnIndex, int32 RowIndex) override;
    #endif

	virtual void PostLoad() override
	{
		if (InputValue.IsValid())
		{
			InputValue.GetMutable<FChooserParameterBase>().PostLoad();
		}
	}

	virtual void Compile(IHasContextClass* Owner, bool bForce) override
	{
		if (FChooserParameterRandomizeBase* Input = InputValue.GetMutablePtr<FChooserParameterRandomizeBase>())
		{
			// binding on randomize columns is optional, so don't call compile unless it's bound, to avoid error messages
			if (Input->IsBound())
			{
				Input->Compile(Owner, bForce);
			}
		}
	};

	CHOOSER_COLUMN_BOILERPLATE(FChooserParameterRandomizeBase);

#if WITH_EDITOR
	virtual bool IsRandomizeColumn() const override { return true; }
#endif
};