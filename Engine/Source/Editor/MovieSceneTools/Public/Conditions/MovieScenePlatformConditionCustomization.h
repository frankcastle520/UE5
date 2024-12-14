// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "Templates/SharedPointer.h"
#include "PropertyHandle.h"

class IDetailLayoutBuilder;
class SCheckBoxList;

class MOVIESCENETOOLS_API FMovieScenePlatformConditionCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:

	void OnPlatformCheckChanged(int32 Index);
	TArray<FName> GetCurrentValidPlatformNames();

	TSharedPtr<IPropertyHandle> ValidPlatformsPropertyHandle;
	TSharedPtr<SCheckBoxList> CheckBoxList;
};
