// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMVVMPreviewSourcePanel.h"

#include "IWidgetPreviewToolkit.h"
#include "MVVMSubsystem.h"
#include "Styling/AppStyle.h"
#include "View/MVVMView.h"
#include "View/MVVMViewClass.h"
#include "WidgetPreview.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/PropertyViewer/SFieldIcon.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "SMVVMDebugSourcePanel"

namespace UE::MVVM::Private
{
	class SPreviewSourceView final : public SListView<TSharedPtr<SPreviewSourceEntry>>
	{
	};

	class SPreviewSourceEntry
	{
	public:
		SPreviewSourceEntry(UObject* InInstance, FName InName)
			: WeakInstance(InInstance)
			, Name(InName)
		{}

		UClass* GetClass() const
		{
			UObject* Instance = WeakInstance.Get();
			return Instance ? Instance->GetClass() : nullptr;
		}

		FText GetDisplayName() const
		{
			return FText::FromName(Name);
		}

		UObject* GetInstance() const
		{
			return WeakInstance.Get();
		}

	private:
		TWeakObjectPtr<UObject> WeakInstance;
		FName Name;
	};

	void SPreviewSourcePanel::Construct(const FArguments& InArgs, TSharedPtr<UMGWidgetPreview::IWidgetPreviewToolkit> PreviewEditor)
	{
		WeakPreviewEditor = PreviewEditor;
		check(PreviewEditor);

		if (UWidgetPreview* Preview = PreviewEditor->GetPreview())
		{
			HandlePreviewWidgetChanged(EWidgetPreviewWidgetChangeType::Assignment);
			OnWidgetChangedHandle = Preview->OnWidgetChanged().AddSP(this, &SPreviewSourcePanel::HandlePreviewWidgetChanged);
	#if UE_WITH_MVVM_DEBUGGING
			FDebugging::OnViewSourceValueChanged.AddSP(this, &SPreviewSourcePanel::HandleViewChanged);
	#endif
		}

		OnSelectedObjectsChangedHandle = PreviewEditor->OnSelectedObjectsChanged().AddSP(this, &SPreviewSourcePanel::HandleSelectedObjectChanged);

		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(4.f))
			[
				SAssignNew(SourceListView, Private::SPreviewSourceView)
				.ListItemsSource(&SourceList)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow(this, &SPreviewSourcePanel::GenerateWidget)
				.OnSelectionChanged(this, &SPreviewSourcePanel::HandleSourceSelectionChanged)
			]
		];

		HandlePreviewWidgetChanged(EWidgetPreviewWidgetChangeType::Reinstanced);
	}


	SPreviewSourcePanel::~SPreviewSourcePanel()
	{
		if (TSharedPtr<UMGWidgetPreview::IWidgetPreviewToolkit> PreviewEditor = WeakPreviewEditor.Pin())
		{
			if (UWidgetPreview* Preview = PreviewEditor->GetPreview())
			{
				Preview->OnWidgetChanged().Remove(OnWidgetChangedHandle);
			}

			PreviewEditor->OnSelectedObjectsChanged().Remove(OnSelectedObjectsChangedHandle);
		}
	}


	void SPreviewSourcePanel::HandlePreviewWidgetChanged(const EWidgetPreviewWidgetChangeType InChangeType)
	{
		SourceList.Reset();
		WeakView.Reset();

		// Only respond to instantiation changes
		// if (InChangeType == EWidgetPreviewWidgetChangeType::Reinstanced)
		{
			if (TSharedPtr<UMGWidgetPreview::IWidgetPreviewToolkit> PreviewEditor = WeakPreviewEditor.Pin())
			{
				if (const UUserWidget* NewWidget = PreviewEditor->GetPreview() ? PreviewEditor->GetPreview()->GetWidgetInstance() : nullptr)
				{
					if (UMVVMView* View = UMVVMSubsystem::GetViewFromUserWidget(NewWidget))
					{
						WeakView = View;
						for (const FMVVMView_Source& ViewSource : View->GetSources())
						{
							FName SourceName = View->GetViewClass()->GetSource(ViewSource.ClassKey).GetName();
							SourceList.Emplace(MakeShared<Private::SPreviewSourceEntry>(ViewSource.Source, SourceName));
						}
					}
				}
			}
		}

		if (SourceListView)
		{
			SourceListView->RequestListRefresh();
		}
	}


	void SPreviewSourcePanel::HandleSelectedObjectChanged(const TConstArrayView<TWeakObjectPtr<UObject>> InSelectedObjects)
	{
		if (bIsSelectingListItem)
		{
			return;
		}

		TGuardValue<bool> TmpInternalSelection(bIsSelectingListItem, true);
		if (TSharedPtr<UMGWidgetPreview::IWidgetPreviewToolkit> PreviewEditor = WeakPreviewEditor.Pin())
		{
			if (InSelectedObjects.Num() == 1)
			{
				TSharedPtr<Private::SPreviewSourceEntry>* Found = SourceList.FindByPredicate(
					[ToFind = InSelectedObjects[0]](const TSharedPtr<Private::SPreviewSourceEntry>& Other)
					{
						return Other->GetInstance() == ToFind;
					});

				if (Found)
				{
					SourceListView->SetSelection(*Found);
				}
				else
				{
					SourceListView->SetSelection(nullptr);
				}
			}
			else
			{
				SourceListView->SetSelection(nullptr);
			}
		}
	}


	void SPreviewSourcePanel::HandleSourceSelectionChanged(TSharedPtr<Private::SPreviewSourceEntry> Entry, ESelectInfo::Type SelectionType) const
	{
		if (bIsSelectingListItem)
		{
			return;
		}

		TGuardValue<bool> TmpInternalSelection(bIsSelectingListItem, true);
		if (TSharedPtr<UMGWidgetPreview::IWidgetPreviewToolkit> PreviewEditor = WeakPreviewEditor.Pin())
		{
			if (Entry && Entry->GetInstance())
			{
				PreviewEditor->SetSelectedObjects({ Entry->GetInstance() });
			}
			else // Nothing selected
			{
				// Effectively clears the selection
				PreviewEditor->SetSelectedObjects({ });
			}
		}
	}

	#if UE_WITH_MVVM_DEBUGGING
	void SPreviewSourcePanel::HandleViewChanged(const FDebugging::FView& View, const FDebugging::FViewSourceValueArgs& Args)
	{
		if (SourceListView)
		{
			if (View.GetView() == WeakView.Get())
			{
				SourceListView->RebuildList(); // to prevent access to invalid class, rebuild everything.
			}
		}
	}
	#endif


	TSharedRef<ITableRow> SPreviewSourcePanel::GenerateWidget(TSharedPtr<Private::SPreviewSourceEntry> Entry, const TSharedRef<STableViewBase>& OwnerTable) const
	{
		typedef STableRow<TSharedPtr<Private::SPreviewSourceEntry>> RowType;

		const TSharedRef<SWidget> FieldIcon = Entry->GetClass() ? SNew(UE::PropertyViewer::SFieldIcon, Entry->GetClass()) : SNullWidget::NullWidget;

		TSharedRef<RowType> NewRow = SNew(RowType, OwnerTable);
		NewRow->SetContent(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				FieldIcon
			]
			+ SHorizontalBox::Slot()
			.Padding(4.0f)
			[
				SNew(STextBlock)
				.Text(Entry->GetDisplayName())
			]);

		return NewRow;
	}
}

#undef LOCTEXT_NAMESPACE
