// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DesignerExtension.h"
#include "IHasDesignerExtensibility.h"

namespace UE::MVVM
{
class FBlueprintViewDesignerExtensionFactory : public IDesignerExtensionFactory, public TSharedFromThis<FBlueprintViewDesignerExtensionFactory>
{
public:
	FBlueprintViewDesignerExtensionFactory() = default;
	virtual ~FBlueprintViewDesignerExtensionFactory() = default;

	virtual TSharedRef<FDesignerExtension> CreateDesignerExtension() const override;
};

class FBlueprintViewDesignerExtension : public FDesignerExtension
{
public:
	virtual void PreviewContentChanged(TSharedRef<SWidget> NewContent) override;
};
}
