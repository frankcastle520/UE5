// Copyright Epic Games, Inc. All Rights Reserved.


#include "Widgets/STedsTableViewerRow.h"

#include "TedsTableViewerColumn.h"
#include "Widgets/STedsTableViewer.h"

namespace UE::Editor::DataStorage
{
	void STedsTableViewerRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView, const TSharedRef<FTedsTableViewerModel>& InTableViewerModel)
	{
		Item = InArgs._Item;
		TableViewerModel = InTableViewerModel;

		const auto Args = FSuperRowType::FArguments()
		.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("SceneOutliner.TableViewRow"));

		SMultiColumnTableRow<TableViewerItemPtr>::Construct(Args, OwnerTableView);
	}

	TSharedRef<SWidget> STedsTableViewerRow::GenerateWidgetForColumn(const FName& ColumnName)
	{
		if(TSharedPtr<FTedsTableViewerColumn> Column = TableViewerModel->GetColumn(ColumnName))
		{
			if(TSharedPtr<SWidget> RowWidget = Column->ConstructRowWidget(Item))
			{
				return SNew(SBox)
						.MinDesiredHeight(20.0f)
						.VAlign(VAlign_Center)
						[
							RowWidget.ToSharedRef()
						];
			}
		}

		return SNullWidget::NullWidget;
	}
} // namespace UE::Editor::DataStorage
