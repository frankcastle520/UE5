// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialValues/DMMaterialValueColorAtlas.h"
#include "DMDefs.h"
#include "Materials/MaterialInstanceDynamic.h"

#if WITH_EDITOR
#include "Components/MaterialValuesDynamic/DMMaterialValueColorAtlasDynamic.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveLinearColorAtlas.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionCurveAtlasRowParameter.h"
#include "Model/IDMMaterialBuildStateInterface.h"
#include "Model/IDMMaterialBuildUtilsInterface.h"
#include "Utils/DMUtils.h"
#endif

#define LOCTEXT_NAMESPACE "DMMaterialValueColorAtlas"

UDMMaterialValueColorAtlas::UDMMaterialValueColorAtlas()
	: UDMMaterialValue(EDMValueType::VT_ColorAtlas)
	, Value(0)
#if WITH_EDITORONLY_DATA
	, DefaultValue(0)
	, Atlas(nullptr)
	, Curve(nullptr)
#endif
{
#if WITH_EDITORONLY_DATA
	EditableProperties.Add(GET_MEMBER_NAME_CHECKED(UDMMaterialValueColorAtlas, Atlas));
	EditableProperties.Add(GET_MEMBER_NAME_CHECKED(UDMMaterialValueColorAtlas, Curve));
#endif
}

void UDMMaterialValueColorAtlas::SetValue(float InValue)
{
	if (!IsComponentValid())
	{
		return;
	}

	InValue = FMath::Clamp(InValue, 0.f, 1.f);

	if (FMath::IsNearlyEqual(Value, InValue))
	{
		return;
	}

	Value = InValue;

	OnValueChanged(EDMUpdateType::Value | EDMUpdateType::AllowParentUpdate);
}

void UDMMaterialValueColorAtlas::SetMIDParameter(UMaterialInstanceDynamic* InMID) const
{
	if (!IsComponentValid())
	{
		return;
	}

	check(InMID);

	InMID->SetScalarParameterValue(GetMaterialParameterName(), Value);
}

#if WITH_EDITOR
void UDMMaterialValueColorAtlas::SetAtlas(UCurveLinearColorAtlas* InAtlas)
{
	if (!IsComponentValid())
	{
		return;
	}

	if (Atlas == InAtlas)
	{
		return;
	}

	Atlas = InAtlas;

	OnValueChanged(EDMUpdateType::Structure | EDMUpdateType::AllowParentUpdate);
}

void UDMMaterialValueColorAtlas::SetCurve(UCurveLinearColor* InCurve)
{
	if (!IsComponentValid())
	{
		return;
	}

	if (Curve == InCurve)
	{
		return;
	}

	Curve = InCurve;

	OnValueChanged(EDMUpdateType::Structure | EDMUpdateType::AllowParentUpdate);
}

void UDMMaterialValueColorAtlas::GenerateExpression(const TSharedRef<IDMMaterialBuildStateInterface>& InBuildState) const
{
	if (!IsComponentValid())
	{
		return;
	}

	if (InBuildState->HasValue(this))
	{
		return;
	}

	UMaterialExpressionScalarParameter* AlphaParameter = InBuildState->GetBuildUtils().CreateExpressionParameter<UMaterialExpressionScalarParameter>(
		GetMaterialParameterName(), 
		GetParameterGroup(), 
		UE_DM_NodeComment_Default
	);

	check(AlphaParameter);

	// This is a parameter, but we're treating it as a standard node.
	UMaterialExpressionCurveAtlasRowParameter* AtlasExpression = InBuildState->GetBuildUtils().CreateExpression<UMaterialExpressionCurveAtlasRowParameter>(UE_DM_NodeComment_Default);
	check(AtlasExpression);

	AtlasExpression->Atlas = Atlas;
	AtlasExpression->Curve = Curve;
	AtlasExpression->DefaultValue = Value;
	AtlasExpression->InputTime.Connect(0, AlphaParameter);

	// Connect the RGB and A channels back together.
	UMaterialExpressionAppendVector* AppendExpression = InBuildState->GetBuildUtils().CreateExpressionAppend(AtlasExpression, 0, AtlasExpression, 4);

	InBuildState->AddValueExpressions(this, {AlphaParameter, AtlasExpression, AppendExpression});
}

void UDMMaterialValueColorAtlas::ApplyDefaultValue()
{
	SetValue(DefaultValue);
}

void UDMMaterialValueColorAtlas::ResetDefaultValue()
{
	DefaultValue = 0.f;
}

UDMMaterialValueDynamic* UDMMaterialValueColorAtlas::ToDynamic(UDynamicMaterialModelDynamic* InMaterialModelDynamic)
{
	UDMMaterialValueColorAtlasDynamic* ValueDynamic = UDMMaterialValueDynamic::CreateValueDynamic<UDMMaterialValueColorAtlasDynamic>(InMaterialModelDynamic, this);
	ValueDynamic->SetValue(Value);

	return ValueDynamic;
}

FString UDMMaterialValueColorAtlas::GetComponentPathComponent() const
{
	return TEXT("ColorAtlasAlpha");
}

FText UDMMaterialValueColorAtlas::GetComponentDescription() const
{
	return LOCTEXT("ColorAtlas", "Color Atlas");
}

TSharedPtr<FJsonValue> UDMMaterialValueColorAtlas::JsonSerialize() const
{
	return FDMJsonUtils::Serialize({
		{GET_MEMBER_NAME_STRING_CHECKED(ThisClass, Value), FDMJsonUtils::Serialize(Value)},
		{GET_MEMBER_NAME_STRING_CHECKED(ThisClass, Atlas), FDMJsonUtils::Serialize(Atlas)},
		{GET_MEMBER_NAME_STRING_CHECKED(ThisClass, Curve), FDMJsonUtils::Serialize(Curve)}
	});
}

bool UDMMaterialValueColorAtlas::JsonDeserialize(const TSharedPtr<FJsonValue>& InJsonValue)
{
	TMap<FString, TSharedPtr<FJsonValue>> Data;

	if (!FDMJsonUtils::Deserialize(InJsonValue, Data))
	{
		return false;
	}

	bool bSuccess = false;
	EDMUpdateType UpdateType = EDMUpdateType::Value;

	if (const TSharedPtr<FJsonValue>* JsonValue = Data.Find(GET_MEMBER_NAME_STRING_CHECKED(ThisClass, Atlas)))
	{
		UCurveLinearColorAtlas* AtlasJson = nullptr;

		if (FDMJsonUtils::Deserialize(*JsonValue, AtlasJson))
		{
			const FDMUpdateGuard Guard;
			SetAtlas(AtlasJson);
			UpdateType = EDMUpdateType::Structure;
			bSuccess = true;
		}
	}

	if (const TSharedPtr<FJsonValue>* JsonValue = Data.Find(GET_MEMBER_NAME_STRING_CHECKED(ThisClass, Curve)))
	{
		UCurveLinearColor* CurveJson = nullptr;

		if (FDMJsonUtils::Deserialize(*JsonValue, CurveJson))
		{
			const FDMUpdateGuard Guard;
			SetCurve(CurveJson);
			UpdateType = EDMUpdateType::Structure;
			bSuccess = true;
		}
	}

	if (const TSharedPtr<FJsonValue>* JsonValue = Data.Find(GET_MEMBER_NAME_STRING_CHECKED(ThisClass, Value)))
	{
		float ValueJson;

		if (FDMJsonUtils::Deserialize(*JsonValue, ValueJson))
		{
			const FDMUpdateGuard Guard;
			SetValue(ValueJson);
			bSuccess = true;
		}
	}

	if (bSuccess)
	{
		OnValueChanged(UpdateType | EDMUpdateType::AllowParentUpdate);
	}

	return bSuccess;
}

void UDMMaterialValueColorAtlas::SetDefaultValue(float InDefaultValue)
{
	DefaultValue = InDefaultValue;
}

bool UDMMaterialValueColorAtlas::IsDefaultValue() const
{
	return FMath::IsNearlyEqual(Value, DefaultValue);
}
#endif

#undef LOCTEXT_NAMESPACE
