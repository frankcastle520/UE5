// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowSettings)

namespace UE::Dataflow::Private
{
	static const FLinearColor CManagedArrayCollectionPinTypeColor = FLinearColor(0.353393f, 0.454175f, 1.0f, 1.0f);
	static const FLinearColor CArrayPinTypeColor = FLinearColor(1.0f, 0.172585f, 0.0f, 1.0f);
	static const FLinearColor CBoxPinTypeColor = FLinearColor(0.013575f, 0.770000f, 0.429609f, 1.0f);
	static const FLinearColor CSpherePinTypeColor = FLinearColor(0.2f, 0.6f, 1.f, 1.0f);
	static const FLinearColor CDataflowAnyTypePinTypeColor = FLinearColor(0.3f, 0.3f, 0.3f, 1.0f);
} // namespace UE::Dataflow::Private

UDataflowSettings::UDataflowSettings(const FObjectInitializer& ObjectInitlaizer)
	: Super(ObjectInitlaizer)
{
	ManagedArrayCollectionPinTypeColor = UE::Dataflow::Private::CManagedArrayCollectionPinTypeColor;
	ArrayPinTypeColor = UE::Dataflow::Private::CArrayPinTypeColor;
	BoxPinTypeColor = UE::Dataflow::Private::CBoxPinTypeColor;
	SpherePinTypeColor = UE::Dataflow::Private::CSpherePinTypeColor;
	DataflowAnyTypePinTypeColor = UE::Dataflow::Private::CDataflowAnyTypePinTypeColor;
}

FName UDataflowSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR

FText UDataflowSettings::GetSectionText() const
{
	return NSLOCTEXT("DataflowPlugin", "DataflowSettingsSection", "Dataflow");
}

void UDataflowSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property != nullptr)
	{
		OnDataflowSettingsChangedDelegate.Broadcast(NodeColorsMap);
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

FNodeColors UDataflowSettings::RegisterColors(const FName& Category, const FNodeColors& Colors)
{
	if (!NodeColorsMap.Contains(Category))
	{
		NodeColorsMap.Add(Category, Colors);
	}
	return NodeColorsMap[Category];
}




