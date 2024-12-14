// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CustomizableObjectNodeModifierBaseDetails.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UCustomizableObjectNodeModifierClipWithMesh;


class FCustomizableObjectNodeModifierClipWithMeshDetails : public FCustomizableObjectNodeModifierBaseDetails
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** ILayoutDetails interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;

private:

	/** The node for which details are being customized */
	UCustomizableObjectNodeModifierClipWithMesh* Node = nullptr;

	/** Pointer to the builder passed by CustomizeDetails method */
	IDetailLayoutBuilder* DetailBuilderPtr = nullptr;

};
