// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/SMutableInstanceViewer.h"

#include "Engine/SkeletalMesh.h"
#include "Framework/Views/TableViewMetadata.h"
#include "MuCOE/SMutableMeshViewport.h"
#include "MuCOE/SMutableSkeletonViewer.h"
#include "MuT/TypeInfo.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Views/SListView.h"
#include "MuR/SystemPrivate.h"

class ITableRow;
class STableViewBase;
class SWidget;
class USkeleton;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


/** */
namespace MutableSurfaceListColumns
{
	static const FName IdColumnID("Id");
	static const FName SharedIdID("SharedId");
	static const FName CustomColumnID("CustomId");
	static const FName ImageCountID("Images");
	static const FName VectorCountID("Vectors");
	static const FName ScalarCountID("Scalars");
	static const FName StringCountID("Strings");
};

/* **/
class SMutableInstanceSurfaceListRow : public SMultiColumnTableRow<TSharedPtr<FMutableInstanceViewerSurfaceElement>>
{
public:
	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FMutableInstanceViewerSurfaceElement>& InRowItem)
	{
		RowItem = InRowItem;
		
		SMultiColumnTableRow<TSharedPtr<FMutableInstanceViewerSurfaceElement>>::Construct(
			STableRow::FArguments()
			.ShowSelection(true)
			, InOwnerTableView
		);
	}


	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		if (InColumnName == MutableSurfaceListColumns::IdColumnID)
		{
			return SNew(SHorizontalBox) +
				SHorizontalBox::Slot()
				.Padding(4,0)
			[
				SNew(STextBlock).Text(RowItem->Id)
			];
		}

		if (InColumnName == MutableSurfaceListColumns::SharedIdID)
		{
			return SNew(SHorizontalBox) +
				SHorizontalBox::Slot()
			[
				SNew(STextBlock).Text(RowItem->SharedId)
			];
		}

		if (InColumnName == MutableSurfaceListColumns::CustomColumnID)
		{
			return SNew(SHorizontalBox) + SHorizontalBox::Slot()
			[
				SNew(STextBlock).Text(RowItem->CustomId)
			];
		}

		if (InColumnName == MutableSurfaceListColumns::ImageCountID)
		{
			return SNew(SHorizontalBox) + SHorizontalBox::Slot()
				[
					SNew(STextBlock).Text(RowItem->ImageCount)
				];
		}

		if (InColumnName == MutableSurfaceListColumns::VectorCountID)
		{
			return SNew(SHorizontalBox) + SHorizontalBox::Slot()
				[
					SNew(STextBlock).Text(RowItem->VectorCount)
				];
		}

		if (InColumnName == MutableSurfaceListColumns::ScalarCountID)
		{
			return SNew(SHorizontalBox) + SHorizontalBox::Slot()
				[
					SNew(STextBlock).Text(RowItem->ScalarCount)
				];
		}

		if (InColumnName == MutableSurfaceListColumns::StringCountID)
		{
			return SNew(SHorizontalBox) + SHorizontalBox::Slot()
				[
					SNew(STextBlock).Text(RowItem->StringCount)
				];
		}

		// Invalid column name so no widget will be produced 
		return SNullWidget::NullWidget;
	}

private:
	TSharedPtr<FMutableInstanceViewerSurfaceElement> RowItem;
};


/* **/
namespace MutableLODListColumns
{
	static const FName LODIndexColumnID("LOD Index");
	static const FName MeshIdColumnID("Mesh ID");
	static const FName SurfacesColumnID("Surfaces");
};

/* **/
class SMutableInstanceLODListRow : public SMultiColumnTableRow<TSharedPtr<FMutableInstanceViewerLODElement>>
{
public:
	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& InOwnerTableView, 
		const TSharedPtr<FMutableInstanceViewerLODElement>& InRowItem, 
		TSharedPtr<SMutableInstanceViewer> InHost)
	{
		HostMutableInstanceViewer = InHost;
		RowItem = InRowItem;

		SMultiColumnTableRow<TSharedPtr<FMutableInstanceViewerLODElement>>::Construct(
			STableRow::FArguments()
			.ShowSelection(true)
			, InOwnerTableView
		);
	}


	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		if (InColumnName == MutableLODListColumns::LODIndexColumnID)
		{
			return SNew(SHorizontalBox) +
				SHorizontalBox::Slot()
				.Padding(4, 0)
				[
					SNew(STextBlock).Text(RowItem->LODIndex)
				];
		}

		if (InColumnName == MutableLODListColumns::MeshIdColumnID)
		{
			return SNew(SHorizontalBox) +
				SHorizontalBox::Slot()
				[
					SNew(STextBlock).Text(RowItem->MeshId)
				];
		}

		if (InColumnName == MutableLODListColumns::SurfacesColumnID)
		{
			const TSharedRef<SWidget> GeneratedSurfaces = HostMutableInstanceViewer->GenerateSurfaceListView(RowItem->Surfaces);

			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				[
					GeneratedSurfaces
				];
		}

		// Invalid column name so no widget will be produced 
		return SNullWidget::NullWidget;
	}

private:
	TSharedPtr<FMutableInstanceViewerLODElement> RowItem;
	TSharedPtr<SMutableInstanceViewer> HostMutableInstanceViewer;
};


/** Namespace containing the IDs for the header on the buffers list */
namespace MutableInstanceComponentsListColumns
{
	static const FName ComponentIndexColumnID("Component Index");
	static const FName LODsColumnID("LODs");
}


class SMutableInstanceComponentListRow : public SMultiColumnTableRow<TSharedPtr<FMutableInstanceViewerComponentElement>>
{
public:
	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& InOwnerTableView,
	               const TSharedPtr<FMutableInstanceViewerComponentElement>& InRowItem, TSharedPtr<SMutableInstanceViewer> InHost)
	{
		HostMutableInstanceViewer = InHost;
		RowItem = InRowItem;
		
		SMultiColumnTableRow<TSharedPtr<FMutableInstanceViewerComponentElement>>::Construct(
			STableRow::FArguments()
			.ShowSelection(true)
			, InOwnerTableView
		);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		// Column with the index for the buffer .
		// Useful for knowing the channels on what buffer reside
		if (InColumnName == MutableInstanceComponentsListColumns::ComponentIndexColumnID)
		{
			return SNew(SBorder)
			.Content()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					SNew(STextBlock).Text(RowItem->ComponentIndex)
				]
			];
		}

		// Generate the sub table here
		if (InColumnName == MutableInstanceComponentsListColumns::LODsColumnID)
		{
			const TSharedRef<SWidget> GeneratedLODList = HostMutableInstanceViewer->GenerateLODsListView(RowItem->LODs);
			
			return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				GeneratedLODList
			];
		}

		// Invalid column name so no widget will be produced 
		return SNullWidget::NullWidget;
	}

private:
	TSharedPtr<FMutableInstanceViewerComponentElement> RowItem;
	TSharedPtr<SMutableInstanceViewer> HostMutableInstanceViewer;
};


void SMutableInstanceViewer::Construct(const FArguments& InArgs)
{
	// Splitter values
	constexpr float TablesSplitterValue = 0.5f;
	constexpr float ViewportSplitterValue = 0.5f;

	ChildSlot
	[
		SNew(SSplitter)

		+ SSplitter::Slot()
		.Value(TablesSplitterValue)
		[
			GenerateDataTableSlates()
		]

		+ SSplitter::Slot()
		.Value(ViewportSplitterValue)
		[
			GenerateViewportSlates()
		]
	];
}


void SMutableInstanceViewer::SetInstance(const mu::InstancePtrConst& InInstance, const TSharedPtr<const mu::Model>& Model, const mu::Ptr<const mu::Parameters>& Parameters, const mu::Ptr<mu::System>& System)
{
	if (InInstance != MutableInstance)
	{
		MutableInstance = InInstance;

		if (MutableInstance)
		{
			// Make sure no data is left from previous runs
			Components.Empty();

			const int32 ComponentCount = MutableInstance->GetComponentCount();
			for (int32 ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
			{
				TSharedPtr<FMutableInstanceViewerComponentElement> NewComponent = MakeShareable(new FMutableInstanceViewerComponentElement());
				Components.Add(NewComponent);

				NewComponent->ComponentIndex = FText::AsNumber(ComponentIndex);

				TSharedPtr<TArray<TSharedPtr<FMutableInstanceViewerLODElement>>> LODArray = MakeShareable(new TArray<TSharedPtr<FMutableInstanceViewerLODElement>>);
				NewComponent->LODs = LODArray;

				const int32 LODCount = MutableInstance->GetLODCount(ComponentIndex);
				for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
				{
					TSharedPtr<FMutableInstanceViewerLODElement> NewLOD = MakeShareable(new FMutableInstanceViewerLODElement());
					LODArray->Add(NewLOD);

					NewLOD->LODIndex = FText::AsNumber(LODIndex);
					mu::FResourceID MeshId = MutableInstance->GetMeshId(ComponentIndex, LODIndex);
					NewLOD->MeshId = FText::AsNumber(MeshId);

					// Generate the mesh to get some data
					{
						mu::OP::ADDRESS MeshRootAddress = mu::GetResourceIDRoot(MeshId);
						mu::Ptr<const mu::Mesh> MutableMesh = System->GetPrivate()->BuildMesh(Model, Parameters.get(), MeshRootAddress);
						if (MutableMesh && MutableMesh->IsReference())
						{
							NewLOD->MeshId = FText::Format(
								LOCTEXT("MeshReference", "Id [{0}]  Ref [{1}] "), MeshId, MutableMesh->GetReferencedMesh());
						}
					}

					TSharedPtr<TArray<TSharedPtr<FMutableInstanceViewerSurfaceElement>>> SurfaceArray = MakeShareable(new TArray<TSharedPtr<FMutableInstanceViewerSurfaceElement>>);
					NewLOD->Surfaces = SurfaceArray;

					const int32 SurfaceCount = MutableInstance->GetSurfaceCount(ComponentIndex, LODIndex);
					for (int32 SurfaceIndex = 0; SurfaceIndex < SurfaceCount; ++SurfaceIndex)
					{
						TSharedPtr<FMutableInstanceViewerSurfaceElement> NewSurface = MakeShareable(new FMutableInstanceViewerSurfaceElement());

						NewSurface->Id = FText::AsNumber(MutableInstance->GetSurfaceId(ComponentIndex, LODIndex, SurfaceIndex));
						NewSurface->SharedId = FText::AsNumber(MutableInstance->GetSharedSurfaceId(ComponentIndex, LODIndex, SurfaceIndex));
						NewSurface->CustomId = FText::AsNumber(MutableInstance->GetSurfaceCustomId(ComponentIndex, LODIndex, SurfaceIndex));
						NewSurface->ImageCount = FText::AsNumber(MutableInstance->GetImageCount(ComponentIndex, LODIndex, SurfaceIndex));
						NewSurface->VectorCount = FText::AsNumber(MutableInstance->GetVectorCount(ComponentIndex, LODIndex, SurfaceIndex));
						NewSurface->ScalarCount = FText::AsNumber(MutableInstance->GetScalarCount(ComponentIndex, LODIndex, SurfaceIndex));
						NewSurface->StringCount = FText::AsNumber(MutableInstance->GetStringCount(ComponentIndex, LODIndex, SurfaceIndex));

						SurfaceArray->Add(NewSurface);
					}
				}
			}

			// Make sure the list gets refreshed with the new contents
			ComponentsSlateView->RequestListRefresh();

			// Restore the widths of the columns each time the Instance gets changed.
			ComponentsSlateView->GetHeaderRow()->ResetColumnWidths();
		}

		// TODO: No 3D preview for instances yet
		//InstanceViewport->SetInstance(MutableInstance);
	}
}

TSharedRef<SWidget> SMutableInstanceViewer::GenerateViewportSlates()
{
	TSharedRef<SWidget> Container = SNew(SVerticalBox)

		// User warning messages 
		// + SVerticalBox::Slot()
		// .AutoHeight()
		// [
		// 	// TODO: Add a message to tell the user why no Instance is being displayed
		// 	SNew(SWarningOrErrorBox)
		// 	.MessageStyle(EMessageStyle::Warning)
		// 	.Message(FText::FromString(FString("This is just a simulation")))
		// ]

		// Instance Drawing space
		+ SVerticalBox::Slot()
		[
			SAssignNew(InstanceViewport, SMutableMeshViewport)
			// TODO: No 3D preview for instances yet
			//.Instance(MutableInstance)
		];


	return Container;
}


TSharedRef<SWidget> SMutableInstanceViewer::GenerateDataTableSlates()
{
	// Formatting
	constexpr int32 IndentationSpace = 16;
	constexpr int32 SimpleSpacing = 1;

	constexpr int32 AfterTitleSpacing = 4;
	constexpr int32 EndOfSectionSpacing = 12;

	// Naming
	const FText GeneralDataTitle = LOCTEXT("GeneralDataTitle", "General Data");
	const FText InstanceIdTitle = LOCTEXT("InstanceIdTitle", "Instance Id : ");
	const FText ComponentsTitle = LOCTEXT("ComponentsTitle", "Components");
	
	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)

			// General data ----------------------------------------------------------------
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock).
				Text(GeneralDataTitle)
			]

			+ SVerticalBox::Slot().
			  Padding(IndentationSpace, AfterTitleSpacing).
			  AutoHeight()
			[
				SNew(SVerticalBox)

				// Instace ID
				+ SVerticalBox::Slot().
				  Padding(0, SimpleSpacing).
				  AutoHeight()
				[
					SNew(SHorizontalBox)

					// Vertices title
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock).
						Text(InstanceIdTitle)
					]

					// Vertices value
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock).
						Text(this, &SMutableInstanceViewer::GetInstanceID)
					]

				]
			]

			// Components Data --------------------------------------------------------------
			+ SVerticalBox::Slot()
			  .Padding(0, EndOfSectionSpacing)
			  .AutoHeight()
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().
				AutoHeight()
				[
					SNew(STextBlock).
					Text(ComponentsTitle)
				]

				+ SVerticalBox::Slot()
				  .Padding(IndentationSpace, AfterTitleSpacing)
				  .AutoHeight()
				[
					SNew(SVerticalBox)

					// List of components ----------
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						GenerateComponentsListView()
					]

				]
				
			]
		];
}


TSharedRef<SWidget> SMutableInstanceViewer::GenerateComponentsListView()
{
	// Headers
	const FText ComponentIndexTitle = FText(LOCTEXT("ComponentIndexTitle", "Component"));
	const FText LODTitle = FText(LOCTEXT("LODsTitle", "LODs"));

	// Tooltips
	const FText ComponentIndexTooltip;
	const FText LODsTooltip;
	
	return SAssignNew(ComponentsSlateView, SListView<TSharedPtr<FMutableInstanceViewerComponentElement>>)
		.ListItemsSource(&Components)
		.OnGenerateRow(this, &SMutableInstanceViewer::OnGenerateComponentRow)
		.SelectionMode(ESelectionMode::None)
		.HeaderRow
		(
			SNew(SHeaderRow)
		
			+ SHeaderRow::Column(MutableInstanceComponentsListColumns::ComponentIndexColumnID)
				.DefaultTooltip(ComponentIndexTitle)
				.DefaultLabel(ComponentIndexTooltip)
				.FillWidth(0.1f)
		
			+ SHeaderRow::Column(MutableInstanceComponentsListColumns::LODsColumnID)
				.DefaultTooltip(LODTitle)
				.DefaultLabel(LODsTooltip)
			  	.FillWidth(0.9f)
		);
}


TSharedRef<SWidget> SMutableInstanceViewer::GenerateLODsListView( const TSharedPtr<TArray<TSharedPtr<FMutableInstanceViewerLODElement>>>& InLODs)
{
	if (!InLODs.IsValid() || InLODs->IsEmpty())
	{
		return SNew(STextBlock).
			Text(FText(LOCTEXT("NoLODs", "No LODs Found")));
	}

	// Headers
	const FText LODIndexTitle = FText(LOCTEXT("ChannelIndexTitle", "Index"));
	const FText MeshIdTitle = FText(LOCTEXT("MeshIdLabelTitle", "Mesh Id"));
	const FText SurfacesTitle = FText(LOCTEXT("SurfacesLabelTitle", "Surfaces"));
	
	return SNew(SListView<TSharedPtr<FMutableInstanceViewerLODElement>>)
		.ListItemsSource(InLODs.Get())
		.OnGenerateRow(this, &SMutableInstanceViewer::OnGenerateLODRow)
		.SelectionMode(ESelectionMode::None)
		.HeaderRow
		(
			SNew(SHeaderRow)

			+ SHeaderRow::Column(MutableLODListColumns::LODIndexColumnID)
				.DefaultLabel(LODIndexTitle)
				.FillWidth(0.14f)
			
			+ SHeaderRow::Column(MutableLODListColumns::MeshIdColumnID)
				.DefaultLabel(MeshIdTitle)
				.FillWidth(0.14f)
		
			+ SHeaderRow::Column(MutableLODListColumns::SurfacesColumnID)
			  	.DefaultLabel(SurfacesTitle)
			  	.FillWidth(0.65f)
		);
}


TSharedRef<SWidget> SMutableInstanceViewer::GenerateSurfaceListView(const TSharedPtr<TArray<TSharedPtr<FMutableInstanceViewerSurfaceElement>>>& InSurfaces)
{
	if (!InSurfaces.IsValid() || InSurfaces->IsEmpty())
	{
		return SNew(STextBlock).
			Text(FText(LOCTEXT("NoSurfaces", "No Surfaces Found")));
	}

	return SNew(SListView<TSharedPtr<FMutableInstanceViewerSurfaceElement>>)
		.ListItemsSource(InSurfaces.Get())
		.OnGenerateRow(this, &SMutableInstanceViewer::OnGenerateSurfaceRow)
		.SelectionMode(ESelectionMode::None)
		.HeaderRow
		(
			SNew(SHeaderRow)

			+ SHeaderRow::Column(MutableSurfaceListColumns::IdColumnID)
			.DefaultLabel(LOCTEXT("IdTitle", "Id"))
			.FillWidth(0.14f)

			+ SHeaderRow::Column(MutableSurfaceListColumns::SharedIdID)
			.DefaultLabel(LOCTEXT("SharedIdTitle", "SharedId"))
			.FillWidth(0.35f)

			+ SHeaderRow::Column(MutableSurfaceListColumns::CustomColumnID)
			.DefaultLabel(LOCTEXT("CustomIdTitle", "CustomId"))
			.FillWidth(0.65f)

			+ SHeaderRow::Column(MutableSurfaceListColumns::ImageCountID)
			.DefaultLabel(LOCTEXT("ImagesTitle", "Images"))
			.FillWidth(0.3f)

			+ SHeaderRow::Column(MutableSurfaceListColumns::VectorCountID)
			.DefaultLabel(LOCTEXT("VectorsTitle", "Vectors"))
			.FillWidth(0.3f)

			+ SHeaderRow::Column(MutableSurfaceListColumns::ScalarCountID)
			.DefaultLabel(LOCTEXT("ScalarsTitle", "Scalars"))
			.FillWidth(0.3f)

			+ SHeaderRow::Column(MutableSurfaceListColumns::StringCountID)
			.DefaultLabel(LOCTEXT("StringsTitle", "Strings"))
			.FillWidth(0.3f)
		);
}


FText SMutableInstanceViewer::GetInstanceID() const
{
	return FText::AsNumber(MutableInstance ? MutableInstance->GetId() : 0);
}


TSharedRef<ITableRow> SMutableInstanceViewer::OnGenerateComponentRow(TSharedPtr<FMutableInstanceViewerComponentElement> In, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SMutableInstanceComponentListRow, OwnerTable, In, SharedThis(this) );
}

TSharedRef<ITableRow> SMutableInstanceViewer::OnGenerateLODRow(TSharedPtr<FMutableInstanceViewerLODElement> In, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SMutableInstanceLODListRow, OwnerTable, In, SharedThis(this));
}

TSharedRef<ITableRow> SMutableInstanceViewer::OnGenerateSurfaceRow(TSharedPtr<FMutableInstanceViewerSurfaceElement> In, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SMutableInstanceSurfaceListRow, OwnerTable, In);
}

#undef LOCTEXT_NAMESPACE
