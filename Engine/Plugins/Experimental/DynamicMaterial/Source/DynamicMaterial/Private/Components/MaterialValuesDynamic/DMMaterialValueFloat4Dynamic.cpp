// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialValuesDynamic/DMMaterialValueFloat4Dynamic.h"

#include "Components/DMMaterialValue.h"
#include "Components/MaterialValues/DMMaterialValueFloat4.h"
#include "Materials/MaterialInstanceDynamic.h"

UDMMaterialValueFloat4Dynamic::UDMMaterialValueFloat4Dynamic()
	: UDMMaterialValueDynamic()
	, Value(FLinearColor::Black)
{
}

#if WITH_EDITOR
bool UDMMaterialValueFloat4Dynamic::IsDefaultValue() const
{
	return Value == GetDefaultValue();
}

const FLinearColor& UDMMaterialValueFloat4Dynamic::GetDefaultValue() const
{
	if (UDMMaterialValueFloat4* ParentValue = Cast<UDMMaterialValueFloat4>(GetParentValue()))
	{
		return ParentValue->GetValue();
	}

	return GetDefault<UDMMaterialValueFloat4>()->GetDefaultValue();
}

void UDMMaterialValueFloat4Dynamic::ApplyDefaultValue()
{
	SetValue(GetDefaultValue());
}

void UDMMaterialValueFloat4Dynamic::CopyDynamicPropertiesTo(UDMMaterialComponent* InDestinationComponent) const
{
	if (UDMMaterialValueFloat4* DestinationValueFloat4 = Cast<UDMMaterialValueFloat4>(InDestinationComponent))
	{
		DestinationValueFloat4->SetValue(GetValue());
	}
}

TSharedPtr<FJsonValue> UDMMaterialValueFloat4Dynamic::JsonSerialize() const
{
	return FDMJsonUtils::Serialize(Value);
}

bool UDMMaterialValueFloat4Dynamic::JsonDeserialize(const TSharedPtr<FJsonValue>& InJsonValue)
{
	FLinearColor ValueJson;

	if (FDMJsonUtils::Deserialize(InJsonValue, ValueJson))
	{
		SetValue(ValueJson);
		return true;
	}

	return false;
}
#endif

void UDMMaterialValueFloat4Dynamic::SetValue(const FLinearColor& InValue)
{
	if (!IsComponentValid())
	{
		return;
	}

	if (Value.Equals(InValue))
	{
		return;
	}

	Value = InValue;

	OnValueChanged();
}

void UDMMaterialValueFloat4Dynamic::SetMIDParameter(UMaterialInstanceDynamic* InMID) const
{
	if (!IsComponentValid())
	{
		return;
	}

	UDMMaterialValue* ParentValue = GetParentValue();

	if (!ParentValue)
	{
		return;
	}

	check(InMID);

	InMID->SetVectorParameterValue(ParentValue->GetMaterialParameterName(), Value);
}
