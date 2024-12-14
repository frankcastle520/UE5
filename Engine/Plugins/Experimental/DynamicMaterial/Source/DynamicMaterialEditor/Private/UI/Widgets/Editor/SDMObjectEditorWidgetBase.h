// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorUndoClient.h"
#include "Widgets/SCompoundWidget.h"

#include "Containers/Array.h"
#include "Templates/SharedPointer.h"
#include "UI/Utils/DMWidgetSlot.h"
#include "UObject/WeakObjectPtr.h"

class FCustomDetailsViewItemId;
class FName;
class ICustomDetailsView;
class ICustomDetailsViewItem;
class IDetailKeyframeHandler;
class SDMMaterialEditor;
class UObject;
enum class ECustomDetailsTreeInsertPosition : uint8;
struct FDMPropertyHandle;

/** Base class for the object editor. Provides the methods and layout for producing a Custom Details View. */
class SDMObjectEditorWidgetBase : public SCompoundWidget, public FSelfRegisteringEditorUndoClient
{
	SLATE_DECLARE_WIDGET(SDMObjectEditorWidgetBase, SCompoundWidget)

	SLATE_BEGIN_ARGS(SDMObjectEditorWidgetBase) {}
	SLATE_END_ARGS()

public:
	virtual ~SDMObjectEditorWidgetBase() override;

	void Construct(const FArguments& InArgs, const TSharedRef<SDMMaterialEditor>& InEditorWidget, UObject* InObject);

	UObject* GetObject() const { return ObjectWeak.Get(); }

	TSharedPtr<SDMMaterialEditor> GetEditorWidget() const { return EditorWidgetWeak.Pin(); }

	void Validate();

	//~ Begin FUndoClient
	virtual void PostUndo(bool bInSuccess) override { OnUndo(); }
	virtual void PostRedo(bool bInSuccess) override { OnUndo(); }
	//~ End FUndoClient

protected:
	static constexpr const TCHAR* DefaultCategoryName = TEXT("General");

	TWeakPtr<SDMMaterialEditor> EditorWidgetWeak;
	TWeakObjectPtr<UObject> ObjectWeak;

	TDMWidgetSlot<SWidget> ContentSlot;

	TSharedPtr<IDetailKeyframeHandler> KeyframeHandler;
	bool bConstructing;
	TArray<FName> Categories;
	TSharedPtr<ICustomDetailsViewItem> DefaultCategoryItem;

	TSharedRef<SWidget> CreateWidget();

	virtual TSharedRef<ICustomDetailsViewItem> GetDefaultCategory(const TSharedRef<ICustomDetailsView>& InDetailsView,
		const FCustomDetailsViewItemId& InRootId);

	TSharedRef<ICustomDetailsViewItem> GetCategoryForRow(const TSharedRef<ICustomDetailsView>& InDetailsView, 
		const FCustomDetailsViewItemId& InRootId, const FDMPropertyHandle& InPropertyRow);

	void AddDetailTreeRow(const TSharedRef<ICustomDetailsView>& InDetailsView, const FCustomDetailsViewItemId& InParentId,
		ECustomDetailsTreeInsertPosition InPosition, const FDMPropertyHandle& InPropertyRow);

	void AddCustomRow(const TSharedRef<ICustomDetailsView>& InDetailsView, const FCustomDetailsViewItemId& InParentId, 
		ECustomDetailsTreeInsertPosition InPosition, const FDMPropertyHandle& InPropertyRow);

	void OnExpansionStateChanged(const TSharedRef<ICustomDetailsViewItem>& InItem, bool bInExpansionState);

	virtual TArray<FDMPropertyHandle> GetPropertyRows() = 0;

	virtual void OnUndo() = 0;
};
