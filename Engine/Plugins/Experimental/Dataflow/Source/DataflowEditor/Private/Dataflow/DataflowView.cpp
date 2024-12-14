// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowView.h"

#include "Dataflow/DataflowEditor.h"
#include "Dataflow/DataflowSelection.h"
#include "Templates/EnableIf.h"

#define LOCTEXT_NAMESPACE "DataflowView"

FDataflowNodeView::FDataflowNodeView(TObjectPtr<UDataflowBaseContent> InContent)
	: FGCObject()
	, EditorContent(InContent)
{
}


FDataflowNodeView::~FDataflowNodeView()
{
	if (SelectedNode)
	{
		if (TSharedPtr<FDataflowNode> DataflowNode = SelectedNode->GetDataflowNode())
		{
			if (DataflowNode->GetOnNodeInvalidatedDelegate().IsBound() && OnNodeInvalidatedDelegateHandle.IsValid())
			{
				DataflowNode->GetOnNodeInvalidatedDelegate().Remove(OnNodeInvalidatedDelegateHandle);
			}
		}
	}
}

TObjectPtr<UDataflowBaseContent> FDataflowNodeView::GetEditorContent()
{
	if (ensure(EditorContent))
	{
		return EditorContent;
	}
	return nullptr;
}

bool FDataflowNodeView::SelectedNodeHaveSupportedOutputTypes(UDataflowEdNode* InNode)
{
	SetSupportedOutputTypes();

	if (InNode->IsBound())
	{
		if (TSharedPtr<FDataflowNode> DataflowNode = InNode->DataflowGraph->FindBaseNode(InNode->DataflowNodeGuid))
		{
			TArray<FDataflowOutput*> Outputs = DataflowNode->GetOutputs();

			for (FDataflowOutput* Output : Outputs)
			{
				for (const FString& OutputType : SupportedOutputTypes)
				{
					if (Output->GetType() == FName(*OutputType))
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}



void FDataflowNodeView::OnConstructionViewSelectionChanged(const TArray<UPrimitiveComponent*>& InComponents)
{
	ConstructionViewSelectionChanged(InComponents);
}


void FDataflowNodeView::OnSelectedNodeChanged(UDataflowEdNode* InNode)
{
	if (!bIsPinnedDown)
	{
		//
		// Remove from broadcast
		//
		if (SelectedNode)
		{
			if (TSharedPtr<FDataflowNode> DataflowNode = SelectedNode->GetDataflowNode())
			{
				if (DataflowNode->GetOnNodeInvalidatedDelegate().IsBound() && OnNodeInvalidatedDelegateHandle.IsValid())
				{
					DataflowNode->GetOnNodeInvalidatedDelegate().Remove(OnNodeInvalidatedDelegateHandle);
				}
			}
		}

		SelectedNode = nullptr;

		if (InNode)  // nullptr is valid
		{
			if (SelectedNodeHaveSupportedOutputTypes(InNode))
			{
				SelectedNode = InNode;
			}

			// 
			// Bind OnNodeInvalidated() to new SelectedNode
			// 
			if (SelectedNode)
			{
				if (TSharedPtr<FDataflowNode> DataflowNode = SelectedNode->GetDataflowNode())
				{
					OnNodeInvalidatedDelegateHandle = DataflowNode->GetOnNodeInvalidatedDelegate().AddRaw(this, &FDataflowNodeView::OnNodeInvalidated);
				}
			}
		}

		UpdateViewData();
	}
}

void FDataflowNodeView::OnNodeInvalidated(FDataflowNode* InvalidatedNode)
{
	if (!bIsRefreshLocked)
	{
		if (TSharedPtr<FDataflowNode> DataflowNode = SelectedNode->GetDataflowNode())
		{
			if (InvalidatedNode == DataflowNode.Get())
			{
				UpdateViewData();
			}
		}
	}
}

void FDataflowNodeView::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(SelectedNode);
	if (EditorContent)
	{
		Collector.AddReferencedObject(EditorContent);
	}
}


#undef LOCTEXT_NAMESPACE
