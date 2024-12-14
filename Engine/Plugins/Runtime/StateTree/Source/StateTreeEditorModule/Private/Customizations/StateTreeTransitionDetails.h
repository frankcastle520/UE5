// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"
#include "Input/Reply.h"

class IPropertyHandle;
class IPropertyUtilities;
class IDetailChildrenBuilder;
class FDetailWidgetRow;
class UStateTreeEditorData;
enum class EStateTreeTransitionTrigger : uint8;
class SBorder;
class SWidget;

/**
 * Type customization for FStateTreeTransition.
 */

class FStateTreeTransitionDetails : public IPropertyTypeCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:

	UStateTreeEditorData* GetEditorData() const;

	FText GetDescription() const;

	EStateTreeTransitionTrigger GetTrigger() const;
	bool GetDelayTransition() const;

	void OnCopyTransition() const;
	void OnPasteTransition() const;

	FReply OnRowMouseDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	FReply OnRowMouseUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	TSharedRef<SWidget> GenerateOptionsMenu();
	void OnDeleteTransition() const;
	void OnDeleteAllTransitions() const;
	void OnDuplicateTransition() const;
	
	TSharedPtr<IPropertyHandle> TriggerProperty;
	TSharedPtr<IPropertyHandle> PriorityProperty;
	TSharedPtr<IPropertyHandle> RequiredEventProperty;
	TSharedPtr<IPropertyHandle> StateProperty;
	TSharedPtr<IPropertyHandle> DelayTransitionProperty;
	TSharedPtr<IPropertyHandle> DelayDurationProperty;
	TSharedPtr<IPropertyHandle> DelayRandomVarianceProperty;
	TSharedPtr<IPropertyHandle> ConditionsProperty;
	TSharedPtr<IPropertyHandle> IDProperty;
	
	TSharedPtr<SBorder> RowBorder; 

	TSharedPtr<IPropertyUtilities> PropUtils;
	TSharedPtr<IPropertyHandle> StructProperty;
};
