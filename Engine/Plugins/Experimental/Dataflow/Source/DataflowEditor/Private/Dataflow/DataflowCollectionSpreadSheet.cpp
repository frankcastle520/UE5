// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowCollectionSpreadSheet.h"
#include "Dataflow/DataflowObjectInterface.h"
#include "Dataflow/DataflowCollectionSpreadSheetWidget.h"
#include "Dataflow/DataflowContent.h"
#include "Dataflow/DataflowSelection.h"
#include "Templates/EnableIf.h"

//#include "Dataflow/DataflowEdNode.h"


FDataflowCollectionSpreadSheet::FDataflowCollectionSpreadSheet(TObjectPtr<UDataflowBaseContent> InContent)
	: FDataflowNodeView(InContent)
{

}

void FDataflowCollectionSpreadSheet::SetSupportedOutputTypes()
{
	GetSupportedOutputTypes().Empty();

	GetSupportedOutputTypes().Add("FManagedArrayCollection");
}

void FDataflowCollectionSpreadSheet::UpdateViewData()
{
	if (CollectionSpreadSheet)
	{
		CollectionSpreadSheet->GetCollectionTable()->GetCollectionInfoMap().Empty();

		if (GetSelectedNode())
		{
			if (GetSelectedNode()->IsBound())
			{
				if (TSharedPtr<FDataflowNode> DataflowNode = GetSelectedNode()->DataflowGraph->FindBaseNode(GetSelectedNode()->DataflowNodeGuid))
				{
					TArray<FDataflowOutput*> Outputs = DataflowNode->GetOutputs();
					if (const TObjectPtr<UDataflowBaseContent> Content = GetEditorContent())
					{
						if (TSharedPtr<UE::Dataflow::FEngineContext> Context = Content->GetDataflowContext())
						{
							for (FDataflowOutput* Output : Outputs)
							{
								FName Name = Output->GetName();
								FName Type = Output->GetType();

								if (Output->GetType() == "FManagedArrayCollection" && !Output->IsAnyType())
								{
									const FManagedArrayCollection& Value = Output->GetValue<FManagedArrayCollection>(*Context, FManagedArrayCollection());

									CollectionSpreadSheet->GetCollectionTable()->GetCollectionInfoMap().Add(Name.ToString(), { Value });
								}
							}
						}
					}
				}
			}

			CollectionSpreadSheet->SetData(GetSelectedNode()->GetName());
		}
		else
		{
			CollectionSpreadSheet->SetData(FString());
		}

		CollectionSpreadSheet->RefreshWidget();
	}
}


void FDataflowCollectionSpreadSheet::SetCollectionSpreadSheet(TSharedPtr<SCollectionSpreadSheetWidget>& InCollectionSpreadSheet)
{
	ensure(!CollectionSpreadSheet);

	CollectionSpreadSheet = InCollectionSpreadSheet;

	if (CollectionSpreadSheet)
	{
		OnPinnedDownChangedDelegateHandle = CollectionSpreadSheet->GetOnPinnedDownChangedDelegate().AddRaw(this, &FDataflowCollectionSpreadSheet::OnPinnedDownChanged);
		OnRefreshLockedChangedDelegateHandle = CollectionSpreadSheet->GetOnRefreshLockedChangedDelegate().AddRaw(this, &FDataflowCollectionSpreadSheet::OnRefreshLockedChanged);
	}
}


FDataflowCollectionSpreadSheet::~FDataflowCollectionSpreadSheet()
{
	if (CollectionSpreadSheet)
	{
		CollectionSpreadSheet->GetOnPinnedDownChangedDelegate().Remove(OnPinnedDownChangedDelegateHandle);
		CollectionSpreadSheet->GetOnRefreshLockedChangedDelegate().Remove(OnRefreshLockedChangedDelegateHandle);
	}
}
