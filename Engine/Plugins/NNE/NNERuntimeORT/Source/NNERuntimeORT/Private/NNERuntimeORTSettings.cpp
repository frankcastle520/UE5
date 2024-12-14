// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeORTSettings.h"

UNNERuntimeORTSettings::UNNERuntimeORTSettings(const FObjectInitializer& ObjectInitlaizer)
	: Super(ObjectInitlaizer)
{

}

FName UNNERuntimeORTSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText UNNERuntimeORTSettings::GetSectionText() const
{
	return NSLOCTEXT("NNERuntimeORTPlugin", "NNERuntimeORTSettingsSection", "NNERuntimeORT");
}
#endif