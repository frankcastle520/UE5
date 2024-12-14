// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "IDetailCustomization.h"
#include "PropertyHandle.h"

class IDetailCategoryBuilder;

class FColorCorrectWindowDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	/** Recreate the color grading property struct's children as groups or root properties of the color grading category */
	void MoveColorGradingPropertiesToCategory(TSharedRef<IPropertyHandle> StructHandle, IDetailCategoryBuilder& RootCategory);
};
