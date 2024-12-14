// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CustomizableObjectNodeDetails.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"

class UCustomizableObjectNodeComponentMesh;
class SWidget;


/** Copy Material node details panel. Hides all properties from the inheret Material node. */
class FCustomizableObjectNodeComponentMeshDetails : public FCustomizableObjectNodeDetails
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	// ILayoutDetails interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;

	// Own interface
	
	TSharedRef<SWidget> OnGenerateLODMenuForPicker();

	FText GetCurrentLODName() const;

	TSharedRef<SWidget>OnGenerateLODComboBoxForPicker();

	void OnSelectedLODChanged(int32 NewLODIndex);

private:
	TWeakObjectPtr<UCustomizableObjectNodeComponentMesh> NodeComponentMesh;
	
	TWeakPtr<IDetailLayoutBuilder> WeakDetailsBuilder;
};