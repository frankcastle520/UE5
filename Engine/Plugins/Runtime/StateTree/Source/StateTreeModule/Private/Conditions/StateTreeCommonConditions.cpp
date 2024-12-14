// Copyright Epic Games, Inc. All Rights Reserved.

#include "Conditions/StateTreeCommonConditions.h"
#include "StateTreeExecutionContext.h"
#include "UObject/EnumProperty.h"
#include "StateTreeNodeDescriptionHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeCommonConditions)

#define LOCTEXT_NAMESPACE "StateTree"

namespace UE::StateTree::Conditions
{

template<typename T>
bool CompareNumbers(const T Left, const T Right, const EGenericAICheck Operator)
{
	switch (Operator)
	{
	case EGenericAICheck::Equal:
		return Left == Right;
		break;
	case EGenericAICheck::NotEqual:
		return Left != Right;
		break;
	case EGenericAICheck::Less:
		return Left < Right;
		break;
	case EGenericAICheck::LessOrEqual:
		return Left <= Right;
		break;
	case EGenericAICheck::Greater:
		return Left > Right;
		break;
	case EGenericAICheck::GreaterOrEqual:
		return Left >= Right;
		break;
	default:
		ensureMsgf(false, TEXT("Unhandled operator %d"), Operator);
		return false;
		break;
	}
}

} // UE::StateTree::Conditions


//----------------------------------------------------------------------//
//  FStateTreeCompareIntCondition
//----------------------------------------------------------------------//

bool FStateTreeCompareIntCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	const bool bResult = UE::StateTree::Conditions::CompareNumbers<int32>(InstanceData.Left, InstanceData.Right, Operator);
	return bResult ^ bInvert;
}

#if WITH_EDITOR
FText FStateTreeCompareIntCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	FText LeftValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, Left)), Formatting);
	if (LeftValue.IsEmpty())
	{
		LeftValue = FText::AsNumber(InstanceData->Left);
	}

	FText RightValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, Right)), Formatting);
	if (RightValue.IsEmpty())
	{
		RightValue = FText::AsNumber(InstanceData->Right);
	}

	const FText InvertText = UE::StateTree::DescHelpers::GetInvertText(bInvert, Formatting);
	const FText OperatorText = UE::StateTree::DescHelpers::GetOperatorText(Operator, Formatting);

	return FText::FormatNamed(LOCTEXT("CompareInt", "{EmptyOrNot}{Left} {Op} {Right}"),
		TEXT("EmptyOrNot"), InvertText,
		TEXT("Left"),LeftValue,
		TEXT("Op"), OperatorText,
		TEXT("Right"),RightValue);
}
#endif

//----------------------------------------------------------------------//
//  FStateTreeCompareFloatCondition
//----------------------------------------------------------------------//

bool FStateTreeCompareFloatCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	const bool bResult = UE::StateTree::Conditions::CompareNumbers<double>(InstanceData.Left, InstanceData.Right, Operator);
	return bResult ^ bInvert;
}

#if WITH_EDITOR
FText FStateTreeCompareFloatCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	FNumberFormattingOptions Options;
	Options.MinimumFractionalDigits = 1;
	Options.MaximumFractionalDigits = 3;

	FText LeftValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, Left)), Formatting);
	if (LeftValue.IsEmpty())
	{
		LeftValue = FText::AsNumber(InstanceData->Left, &Options);
	}

	FText RightValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, Right)), Formatting);
	if (RightValue.IsEmpty())
	{
		RightValue = FText::AsNumber(InstanceData->Right, &Options);
	}

	const FText InvertText = UE::StateTree::DescHelpers::GetInvertText(bInvert, Formatting);
	const FText OperatorText = UE::StateTree::DescHelpers::GetOperatorText(Operator, Formatting);

	return FText::FormatNamed(LOCTEXT("CompareFloat", "{EmptyOrNot}{Left} {Op} {Right}"),
		TEXT("EmptyOrNot"), InvertText,
		TEXT("Left"),LeftValue,
		TEXT("Op"), OperatorText,
		TEXT("Right"),RightValue);
}
#endif

//----------------------------------------------------------------------//
//  FStateTreeCompareBoolCondition
//----------------------------------------------------------------------//

bool FStateTreeCompareBoolCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	
	return (InstanceData.bLeft == InstanceData.bRight) ^ bInvert;
}

#if WITH_EDITOR
FText FStateTreeCompareBoolCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	FText LeftValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, bLeft)), Formatting);
	if (LeftValue.IsEmpty())
	{
		LeftValue = UE::StateTree::DescHelpers::GetBoolText(InstanceData->bLeft, Formatting);
	}

	FText RightValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, bRight)), Formatting);
	if (RightValue.IsEmpty())
	{
		RightValue = UE::StateTree::DescHelpers::GetBoolText(InstanceData->bRight, Formatting);
	}

	const FText InvertText = UE::StateTree::DescHelpers::GetInvertText(bInvert, Formatting);

	const FText Format = (Formatting == EStateTreeNodeFormatting::RichText)
		? LOCTEXT("CompareBoolRich", "{EmptyOrNot}{Left} <s>is</> {Right}")
		: LOCTEXT("CompareBool", "{EmptyOrNot}{Left} is {Right}");

	return FText::FormatNamed(Format,
		TEXT("EmptyOrNot"), InvertText,
		TEXT("Left"),LeftValue,
		TEXT("Right"),RightValue);
}
#endif

//----------------------------------------------------------------------//
//  FStateTreeCompareEnumCondition
//----------------------------------------------------------------------//

bool FStateTreeCompareEnumCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	return (InstanceData.Left == InstanceData.Right) ^ bInvert;
}

#if WITH_EDITOR
FText FStateTreeCompareEnumCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	FText LeftValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, Left)), Formatting);
	if (LeftValue.IsEmpty())
	{
		if (InstanceData->Left.Enum)
		{
			LeftValue = InstanceData->Left.Enum->GetDisplayNameTextByValue(InstanceData->Left.Value);
		}
		else
		{
			LeftValue = LOCTEXT("None", "None");
		}
	}

	FText RightValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, Right)), Formatting);
	if (RightValue.IsEmpty())
	{
		if (InstanceData->Left.Enum)
		{
			RightValue = InstanceData->Right.Enum->GetDisplayNameTextByValue(InstanceData->Right.Value);
		}
		else
		{
			RightValue = LOCTEXT("None", "None");
		}
	}

	const FText InvertText = UE::StateTree::DescHelpers::GetInvertText(bInvert, Formatting);

	const FText Format = (Formatting == EStateTreeNodeFormatting::RichText)
		? LOCTEXT("CompareEnumRich", "{EmptyOrNot}{Left} <s>is</> {Right}")
		: LOCTEXT("CompareEnum", "{EmptyOrNot}{Left} is {Right}");

	return FText::FormatNamed(Format,
		TEXT("EmptyOrNot"), InvertText,
		TEXT("Left"),LeftValue,
		TEXT("Right"),RightValue);
}

void FStateTreeCompareEnumCondition::OnBindingChanged(const FGuid& ID, FStateTreeDataView InstanceData, const FStateTreePropertyPath& SourcePath, const FStateTreePropertyPath& TargetPath, const IStateTreeBindingLookup& BindingLookup)
{
	if (!TargetPath.GetStructID().IsValid())
	{
		return;
	}

	FInstanceDataType& Instance = InstanceData.GetMutable<FInstanceDataType>();

	// Left has changed, update enums from the leaf property.
	if (!TargetPath.IsPathEmpty()
		&& TargetPath.GetSegments().Last().GetName() == GET_MEMBER_NAME_CHECKED(FInstanceDataType, Left))
	{
		if (const FProperty* LeafProperty = BindingLookup.GetPropertyPathLeafProperty(SourcePath))
		{
			// Handle both old stype namespace enums and new class enum properties.
			UEnum* NewEnum = nullptr;
			if (const FByteProperty* ByteProperty = CastField<FByteProperty>(LeafProperty))
			{
				NewEnum = ByteProperty->GetIntPropertyEnum();
			}
			else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(LeafProperty))
			{
				NewEnum = EnumProperty->GetEnum();
			}

			if (Instance.Left.Enum != NewEnum)
			{
				Instance.Left.Initialize(NewEnum);
			}
		}
		else
		{
			Instance.Left.Initialize(nullptr);
		}

		if (Instance.Right.Enum != Instance.Left.Enum)
		{
			Instance.Right.Initialize(Instance.Left.Enum);
		}
	}
}

#endif


//----------------------------------------------------------------------//
//  FStateTreeCompareDistanceCondition
//----------------------------------------------------------------------//

bool FStateTreeCompareDistanceCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	const FVector::FReal Left = FVector::DistSquared(InstanceData.Source, InstanceData.Target);
	const FVector::FReal Right = FMath::Square(InstanceData.Distance);
	const bool bResult = UE::StateTree::Conditions::CompareNumbers<FVector::FReal>(Left, Right, Operator);
	return bResult ^ bInvert;
}

#if WITH_EDITOR
FText FStateTreeCompareDistanceCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	FNumberFormattingOptions Options;
	Options.MinimumFractionalDigits = 1;
	Options.MaximumFractionalDigits = 3;

	FText SourceValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, Source)), Formatting);
	if (SourceValue.IsEmpty())
	{
		SourceValue = InstanceData->Source.ToText();
	}

	FText TargetValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, Target)), Formatting);
	if (TargetValue.IsEmpty())
	{
		TargetValue = InstanceData->Target.ToText();
	}

	FText DistanceValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, Distance)), Formatting);
	if (DistanceValue.IsEmpty())
	{
		DistanceValue = FText::AsNumber(InstanceData->Distance, &Options);
	}

	const FText OperatorText = UE::StateTree::DescHelpers::GetOperatorText(Operator, Formatting);
	const FText InvertText = UE::StateTree::DescHelpers::GetInvertText(bInvert, Formatting);

	const FText Format = (Formatting == EStateTreeNodeFormatting::RichText)
		? LOCTEXT("CompareDistanceRich", "{EmptyOrNot}<s>Distance from</> {Source} <s>to</> {Target} {Op} {Distance}")
		: LOCTEXT("CompareDistance", "{EmptyOrNot}Distance from {Source} to {Target} {Op} {Distance}");

	return FText::FormatNamed(Format,
		TEXT("EmptyOrNot"), InvertText,
		TEXT("Source"), SourceValue,
		TEXT("Target"), TargetValue,
		TEXT("Op"), OperatorText,
		TEXT("Distance"), DistanceValue);
}
#endif

//----------------------------------------------------------------------//
//  FStateTreeRandomCondition
//----------------------------------------------------------------------//

bool FStateTreeRandomCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	return FMath::FRandRange(0.0f, 1.0f) < InstanceData.Threshold;
}

#if WITH_EDITOR
FText FStateTreeRandomCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	FNumberFormattingOptions Options;
	Options.MinimumFractionalDigits = 1;
	Options.MaximumFractionalDigits = 3;

	FText ThresholdValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, Threshold)), Formatting);
	if (ThresholdValue.IsEmpty())
	{
		ThresholdValue = FText::AsNumber(InstanceData->Threshold, &Options);
	}

	const FText Format = (Formatting == EStateTreeNodeFormatting::RichText)
		? LOCTEXT("RandomRich", "<s>Random [0..1] &lt;</> {Threshold}")
		: LOCTEXT("Random", "Random [0..1] &lt; {Threshold}");

	return FText::FormatNamed(Format,
		TEXT("Threshold"), ThresholdValue);
}
#endif

#undef LOCTEXT_NAMESPACE

