// Copyright Epic Games, Inc. All Rights Reserved.

#include "Layout/Containers/ColumnWrappingContainerTemplates.h"

#include "ColumnWrappingContainer.h"

TSharedRef<FColumnWrappingContainer> FColumnWrappingContainerTemplates::GetBestFitColumnsWithSmallCells()
{
	// TODO: extract this out into a size type
	return MakeShared<FColumnWrappingContainer>( FColumnWrappingContainerArgs{ 32 } );			
}

FColumnWrappingContainerTemplates& FColumnWrappingContainerTemplates::Get()
{
	static FColumnWrappingContainerTemplates Presets;
	return Presets;
}