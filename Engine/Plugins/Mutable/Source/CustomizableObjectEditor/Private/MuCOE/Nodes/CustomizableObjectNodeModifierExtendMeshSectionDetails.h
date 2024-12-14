// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CustomizableObjectNodeModifierBaseDetails.h"
#include "MuCOE/SMutableSearchComboBox.h"
#include "IDetailCustomization.h"
#include "Types/SlateEnums.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/ITableRow.h"

namespace ESelectInfo { enum Type : int; }

class FString;
class IDetailLayoutBuilder;
class IPropertyHandle;
class UCustomizableObjectNodeObject;


class FCustomizableObjectNodeModifierExtendMeshSectionDetails : public FCustomizableObjectNodeModifierBaseDetails
{
public:
	// Makes a new instance of this detail layout class for a specific detail view requesting it 
	static TSharedRef<IDetailCustomization> MakeInstance();

	// ILayoutDetails interface
	void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;

private:

	class UCustomizableObjectNodeModifierExtendMeshSection* Node = nullptr;


	/** */
	TSharedPtr<IPropertyHandle> EnableTagsPropertyHandle;

	/** */
	TSharedPtr<SMutableTagListWidget> EnableTagListWidget;

	/** */
	virtual void OnEnableTagsPropertyChanged();


};
