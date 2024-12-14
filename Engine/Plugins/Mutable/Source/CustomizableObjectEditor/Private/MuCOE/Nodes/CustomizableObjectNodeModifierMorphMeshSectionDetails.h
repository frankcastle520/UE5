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


class FCustomizableObjectNodeModifierMorphMeshSectionDetails : public FCustomizableObjectNodeModifierBaseDetails
{
public:
	// Makes a new instance of this detail layout class for a specific detail view requesting it 
	static TSharedRef<IDetailCustomization> MakeInstance();

	// ILayoutDetails interface
	void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;

protected:

	// FCustomizableObjectNodeModifierBaseDetails interface
	virtual void OnRequiredTagsPropertyChanged() override;

private:

	class UCustomizableObjectNodeModifierMorphMeshSection* Node = nullptr;

	/** */
	TSharedPtr<SMutableSearchComboBox> MorphCombo;
	TArray< TSharedRef<SMutableSearchComboBox::FFilteredOption> > MorphOptionsSource;
	void OnMorphTargetComboBoxSelectionChanged(const FText& NewText);

	/** */
	void AddMorphsFromNode(UEdGraphNode* Candidate, TMap<UEdGraphNode*, TSharedPtr<SMutableSearchComboBox::FFilteredOption>>& AddedOptions);
		
	/** */
	TSharedPtr<SMutableSearchComboBox::FFilteredOption> AddNodeHierarchyOptions(UEdGraphNode* Node, TMap<UEdGraphNode*, TSharedPtr<SMutableSearchComboBox::FFilteredOption>>& AddedOptions);

	/** */
	void RefreshMorphOptions();

};
