// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"

struct FTypedElementWidgetConstructor;

/*
 * All Teds widgets will be contained inside STedsWidget which acts like a container widget
 * so we can have guaranteed access to the contents inside to dynamically update them if required.
 * This widget is created and returned for any Teds widget requested for a row, regardless of if
 * the actual internal widget exists or not.
 * 
 * Currently this is simply an SCompoundWidget
 */
class STedsWidget : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(STedsWidget)
		: _UiRowHandle(UE::Editor::DataStorage::InvalidRowHandle)
		, _Content()
	{
	}

	// The UI Row this widget will be assigned to
	SLATE_ARGUMENT(UE::Editor::DataStorage::RowHandle, UiRowHandle)
	
	/** The actual widget content */
	SLATE_DEFAULT_SLOT(FArguments, Content)
	
	SLATE_END_ARGS()
	
	TYPEDELEMENTFRAMEWORK_API STedsWidget();

	TYPEDELEMENTFRAMEWORK_API void Construct( const FArguments& InArgs );
	
	TYPEDELEMENTFRAMEWORK_API void SetContent(const TSharedRef< SWidget >& InContent);

	TYPEDELEMENTFRAMEWORK_API UE::Editor::DataStorage::RowHandle GetRowHandle() const;

private:

	void RegisterTedsWidget(const TSharedPtr<SWidget>& InContentWidget);
	
	static IEditorDataStorageProvider* GetStorageIfAvailable();

private:
	
	UE::Editor::DataStorage::RowHandle UiRowHandle;
};
