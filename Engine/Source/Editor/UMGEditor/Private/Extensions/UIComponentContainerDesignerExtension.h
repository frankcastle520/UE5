// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DesignerExtension.h"
#include "IHasDesignerExtensibility.h"

class FUIComponentContainerDesignerExtensionFactory : public IDesignerExtensionFactory, public TSharedFromThis<FUIComponentContainerDesignerExtensionFactory>
{
public:
	FUIComponentContainerDesignerExtensionFactory() = default;
	virtual ~FUIComponentContainerDesignerExtensionFactory() = default;

	virtual TSharedRef<FDesignerExtension> CreateDesignerExtension() const override;
};

class FUIComponentContainerDesignerExtension : public FDesignerExtension
{
public:
	virtual void PreviewContentChanged(TSharedRef<SWidget> NewContent) override;
};
