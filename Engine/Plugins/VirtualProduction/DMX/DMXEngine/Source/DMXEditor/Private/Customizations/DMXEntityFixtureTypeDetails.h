// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "Layout/Visibility.h"

class IPropertyHandle;
class IPropertyUtilities;


/** Details customization for the 'FixtureType FunctionProperties' details view */
class FDMXEntityFixtureTypeDetails
	: public IDetailCustomization
{
public:
	/** Creates an instance of this details customization */
	static TSharedRef<IDetailCustomization> MakeInstance();

protected:
	//~ Begin IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End IDetailCustomization interface

private:
	/** Called when the GDTFSource property changed */
	void OnGDTFSourceChanged();

	/** Returns the visibility of the bExportGeneratedGDTF property */
	EVisibility GetExportGeneratedGDTFPropertyVisibility() const;

	/** Handle to the GDTFSource property */
	TSharedPtr<IPropertyHandle> GDTFSourceHandle;

	/** Property utilities for this customization */
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};
