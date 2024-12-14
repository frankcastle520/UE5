// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailPropertyExtensionHandler.h"

namespace UE::Chaos::ClothAsset
{
	struct FClothSimulationNodeDetailExtender : public IDetailPropertyExtensionHandler
	{
		virtual bool IsPropertyExtendable(const UClass* InObjectClass, const IPropertyHandle& PropertyHandle) const override;
		virtual void ExtendWidgetRow(FDetailWidgetRow& InWidgetRow, const IDetailLayoutBuilder& InDetailBuilder, const UClass* InObjectClass, TSharedPtr<IPropertyHandle> PropertyHandle) override;
	};
} // namespace UE::Chaos::ClothAsset
