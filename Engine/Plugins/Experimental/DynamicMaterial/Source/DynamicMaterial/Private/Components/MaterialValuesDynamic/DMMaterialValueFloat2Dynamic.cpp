// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialValuesDynamic/DMMaterialValueFloat2Dynamic.h"

#include "Components/DMMaterialValue.h"
#include "Components/MaterialValues/DMMaterialValueFloat2.h"
#include "Materials/MaterialInstanceDynamic.h"

UDMMaterialValueFloat2Dynamic::UDMMaterialValueFloat2Dynamic()
	: UDMMaterialValueDynamic()
	, Value(FVector2D::ZeroVector)
{
}

#if WITH_EDITOR
bool UDMMaterialValueFloat2Dynamic::IsDefaultValue() const
{
	return Value == GetDefaultValue();
}

const FVector2D& UDMMaterialValueFloat2Dynamic::GetDefaultValue() const
{
	if (UDMMaterialValueFloat2* ParentValue = Cast<UDMMaterialValueFloat2>(GetParentValue()))
	{
		return ParentValue->GetValue();
	}

	return GetDefault<UDMMaterialValueFloat2>()->GetDefaultValue();
}

void UDMMaterialValueFloat2Dynamic::ApplyDefaultValue()
{
	SetValue(GetDefaultValue());
}

void UDMMaterialValueFloat2Dynamic::CopyDynamicPropertiesTo(UDMMaterialComponent* InDestinationComponent) const
{
	if (UDMMaterialValueFloat2* DestinationValueFloat2 = Cast<UDMMaterialValueFloat2>(InDestinationComponent))
	{
		DestinationValueFloat2->SetValue(GetValue());
	}
}

TSharedPtr<FJsonValue> UDMMaterialValueFloat2Dynamic::JsonSerialize() const
{
	return FDMJsonUtils::Serialize(Value);
}

bool UDMMaterialValueFloat2Dynamic::JsonDeserialize(const TSharedPtr<FJsonValue>& InJsonValue)
{
	FVector2D ValueJson;

	if (FDMJsonUtils::Deserialize(InJsonValue, ValueJson))
	{
		SetValue(ValueJson);
		return true;
	}

	return false;
}
#endif

void UDMMaterialValueFloat2Dynamic::SetValue(const FVector2D& InValue)
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

void UDMMaterialValueFloat2Dynamic::SetMIDParameter(UMaterialInstanceDynamic* InMID) const
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

	InMID->SetVectorParameterValue(
		ParentValue->GetMaterialParameterName(),
		FLinearColor(Value.X, Value.Y, 0.f, 0.f)
	);
}
