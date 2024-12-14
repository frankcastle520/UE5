// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeModifierMorphMeshSectionDetails.h"

#include "MuCOE/CustomizableObjectEditorUtilities.h"
#include "MuCOE/CustomizableObjectEditorStyle.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/Nodes/CustomizableObjectNodeObject.h"
#include "MuCOE/Nodes/CustomizableObjectNodeObjectGroup.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterial.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierBase.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierMorphMeshSection.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierExtendMeshSection.h"
#include "MuCOE/Nodes/CustomizableObjectNodeSkeletalMesh.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "DetailLayoutBuilder.h"
#include "IDetailsView.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/Attribute.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectDetails"


TSharedRef<IDetailCustomization> FCustomizableObjectNodeModifierMorphMeshSectionDetails::MakeInstance()
{
	return MakeShareable( new FCustomizableObjectNodeModifierMorphMeshSectionDetails );
}


void FCustomizableObjectNodeModifierMorphMeshSectionDetails::CustomizeDetails( IDetailLayoutBuilder& DetailBuilder )
{
	FCustomizableObjectNodeModifierBaseDetails::CustomizeDetails(DetailBuilder);

	const IDetailsView* DetailsView = DetailBuilder.GetDetailsView();
	if ( DetailsView->GetSelectedObjects().Num() )
	{
		Node = Cast<UCustomizableObjectNodeModifierMorphMeshSection>( DetailsView->GetSelectedObjects()[0].Get() );
	}

	if (!Node)
	{
		return;
	}

	// This property is not relevant for this node
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UCustomizableObjectNodeModifierWithMaterial, ReferenceMaterial), UCustomizableObjectNodeModifierWithMaterial::StaticClass());
	
	// Add a morph selection widget.
	IDetailCategoryBuilder& MorphCategory = DetailBuilder.EditCategory( "Morph" );

	// Scan for hint morph names
	RefreshMorphOptions();

	TSharedRef<IPropertyHandle> MorphTargetNameProperty = DetailBuilder.GetProperty("MorphTargetName");
	MorphCategory.AddCustomRow(LOCTEXT("MorphMaterialDetails_Target", "Target"))
		[
			SNew(SProperty, MorphTargetNameProperty)
				.ShouldDisplayName(false)
				.CustomWidget()
				[
					SNew(SBorder)
						.BorderImage(UE_MUTABLE_GET_BRUSH("NoBorder"))
						.Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.FillWidth(10.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
									.Text(LOCTEXT("MorphMaterialDetails_MorphTarget", "Morph Target"))
									.Font(IDetailLayoutBuilder::GetDetailFont())
							]

							+ SHorizontalBox::Slot()
							.FillWidth(10.0f)
							.HAlign(HAlign_Fill)
							.VAlign(VAlign_Center)
							[
								SAssignNew(this->MorphCombo , SMutableSearchComboBox)
								.ButtonStyle(FAppStyle::Get(), "NoBorder")
								.OptionsSource(&MorphOptionsSource)
								.OnSelectionChanged(this, &FCustomizableObjectNodeModifierMorphMeshSectionDetails::OnMorphTargetComboBoxSelectionChanged)
								.Content()
								[
									SNew(SEditableTextBox)
									.Font(IDetailLayoutBuilder::GetDetailFont())
									.Text_Lambda([&]() { return Node ? FText::FromString(Node->MorphTargetName) : FText();})
									.OnTextChanged(this, &FCustomizableObjectNodeModifierMorphMeshSectionDetails::OnMorphTargetComboBoxSelectionChanged)
								]
							]
						]
				]
		];
}


void FCustomizableObjectNodeModifierMorphMeshSectionDetails::OnMorphTargetComboBoxSelectionChanged(const FText& NewText)
{
	if (Node && Node->MorphTargetName != NewText.ToString())
	{
		Node->MorphTargetName = NewText.ToString();
		Node->Modify();
	}
}


void FCustomizableObjectNodeModifierMorphMeshSectionDetails::OnRequiredTagsPropertyChanged()
{
	FCustomizableObjectNodeModifierBaseDetails::OnRequiredTagsPropertyChanged();
	RefreshMorphOptions();
}


void FCustomizableObjectNodeModifierMorphMeshSectionDetails::RefreshMorphOptions()
{
	MorphOptionsSource.SetNum(0, EAllowShrinking::No);

	TArray<UCustomizableObjectNode*> CandidateNodes;
	Node->GetPossiblyModifiedNodes(CandidateNodes);

	TMap<UEdGraphNode*, TSharedPtr<SMutableSearchComboBox::FFilteredOption>> AddedOptions;

	for (UCustomizableObjectNode* Candidate : CandidateNodes)
	{
		AddMorphsFromNode(Candidate, AddedOptions);
	}

	// Add all morphs if no candidate is found
	if (MorphOptionsSource.IsEmpty())
	{
		UCustomizableObject* ThisNodeObject = GetRootObject(*Node);
		UCustomizableObject* RootObject = GraphTraversal::GetRootObject(ThisNodeObject);

		TSet<UCustomizableObject*> AllCustomizableObject;
		GetAllObjectsInGraph(RootObject, AllCustomizableObject);

		for (const UCustomizableObject* CustObject : AllCustomizableObject)
		{
			if (!CustObject)
			{
				continue;
			}

			for (const TObjectPtr<UEdGraphNode>& Candidate : CustObject->GetPrivate()->GetSource()->Nodes)
			{
				AddMorphsFromNode(Candidate, AddedOptions);
			}
		}
	}
}


TSharedPtr<SMutableSearchComboBox::FFilteredOption> FCustomizableObjectNodeModifierMorphMeshSectionDetails::AddNodeHierarchyOptions(UEdGraphNode* InNode, TMap<UEdGraphNode*, TSharedPtr<SMutableSearchComboBox::FFilteredOption>>& AddedOptions)
{
	TSharedPtr<SMutableSearchComboBox::FFilteredOption> Option;
	if (TSharedPtr<SMutableSearchComboBox::FFilteredOption>* FoundCached = AddedOptions.Find(InNode))
	{
		Option = *FoundCached;
	}

	if (InNode && !Option)
	{
		// Add parents
		TSharedPtr<SMutableSearchComboBox::FFilteredOption> ParentOption;
		{
			// Add this node as placeholder in the cache to prevent infinite loops because of graph loops.
			AddedOptions.Add(InNode, Option);

			// Pin traversal
			for (const UEdGraphPin* Pin : InNode->Pins)
			{
				if (Pin && Pin->Direction == EEdGraphPinDirection::EGPD_Output
					&& !Pin->LinkedTo.IsEmpty() && Pin->LinkedTo[0])
				{
					UEdGraphNode* ParentNode = Pin->LinkedTo[0]->GetOwningNode();

					ParentOption = AddNodeHierarchyOptions(ParentNode, AddedOptions);

					// We are ok with just one parent
					if (ParentOption)
					{
						break;
					}
				}
			}

			// Node internal references
			if (!ParentOption)
			{
				// Is it an object referencing an external group?
				if (UCustomizableObjectNodeObject* ObjectNode = Cast<UCustomizableObjectNodeObject>(InNode))
				{
					if (ObjectNode->ParentObject)
					{
						UEdGraphNode* ExternalParentNode = GetCustomizableObjectExternalNode<UEdGraphNode>(ObjectNode->ParentObject, ObjectNode->ParentObjectGroupId);
						ParentOption = AddNodeHierarchyOptions(ExternalParentNode, AddedOptions);
					}
				}
			}

			// TODO: Support import/export nodes
		}

		// Is it a relevant type that we want to show in the hierarchy?
		if (UCustomizableObjectNodeMaterial* MeshSectionNode = Cast<UCustomizableObjectNodeMaterial>(InNode))
		{
			Option = MakeShared<SMutableSearchComboBox::FFilteredOption>();
			Option->Parent = ParentOption;
			UMaterialInterface* Material = MeshSectionNode->GetMaterial();
			Option->DisplayOption = FString::Printf(TEXT("Mesh Section [%s]"), Material ? *Material->GetName() : TEXT("no-material"));
			MorphOptionsSource.Add(Option.ToSharedRef());
		}

		else if (UCustomizableObjectNodeObject* ObjectNode = Cast<UCustomizableObjectNodeObject>(InNode))
		{
			Option = MakeShared<SMutableSearchComboBox::FFilteredOption>();
			Option->Parent = ParentOption;
			Option->DisplayOption = ObjectNode->ObjectName;
			if (Option->DisplayOption.IsEmpty())
			{
				Option->DisplayOption = "Unnamed Object";
			}
			MorphOptionsSource.Add(Option.ToSharedRef());
		}

		else if (UCustomizableObjectNodeObjectGroup* GroupNode = Cast<UCustomizableObjectNodeObjectGroup>(InNode))
		{
			Option = MakeShared<SMutableSearchComboBox::FFilteredOption>();
			Option->Parent = ParentOption;
			Option->DisplayOption = GroupNode->GroupName;
			if (Option->DisplayOption.IsEmpty())
			{
				Option->DisplayOption = "Unnamed Group";
			}
			MorphOptionsSource.Add(Option.ToSharedRef());
		}

		else if (UCustomizableObjectNodeModifierBase* ModifierNode = Cast<UCustomizableObjectNodeModifierBase>(InNode))
		{
			Option = MakeShared<SMutableSearchComboBox::FFilteredOption>();
			Option->Parent = ParentOption;
			Option->DisplayOption = ModifierNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
			MorphOptionsSource.Add(Option.ToSharedRef());
		}

		// Overwrite in to cache, to prevent loops
		AddedOptions.Add(InNode, Option);

		// If this node wasn't of interest, maybe its parent was.
		if (!Option)
		{
			Option = ParentOption;
		}
	}

	return Option;
}


void FCustomizableObjectNodeModifierMorphMeshSectionDetails::AddMorphsFromNode( UEdGraphNode* Candidate, TMap<UEdGraphNode*, TSharedPtr<SMutableSearchComboBox::FFilteredOption>>& AddedOptions)
{
	USkeletalMesh* SkeletalMesh = nullptr;

	if (UCustomizableObjectNodeMaterialBase* MaterialNode = Cast<UCustomizableObjectNodeMaterialBase>(Candidate))
	{
		if (MaterialNode->OutputPin())
		{
			const UEdGraphPin* SourceMeshPin = FindMeshBaseSource(*MaterialNode->OutputPin(), false);
			if (SourceMeshPin)
			{
				UCustomizableObjectNodeSkeletalMesh* SkeletalNode = Cast<UCustomizableObjectNodeSkeletalMesh>(SourceMeshPin->GetOwningNode());
				if (SkeletalNode)
				{
					SkeletalMesh = SkeletalNode->SkeletalMesh;
				}
			}
		}
	}

	else if (UCustomizableObjectNodeModifierExtendMeshSection* ExtendNode = Cast<UCustomizableObjectNodeModifierExtendMeshSection>(Candidate))
	{
		if (ExtendNode->OutputPin())
		{
			const UEdGraphPin* SourceMeshPin = FindMeshBaseSource(*ExtendNode->OutputPin(), false);
			if (SourceMeshPin)
			{
				UCustomizableObjectNodeSkeletalMesh* SkeletalNode = Cast<UCustomizableObjectNodeSkeletalMesh>(SourceMeshPin->GetOwningNode());
				if (SkeletalNode)
				{
					SkeletalMesh = SkeletalNode->SkeletalMesh;
				}
			}
		}
	}

	if (SkeletalMesh)
	{
		const TArray<TObjectPtr<UMorphTarget>>& Morphs = SkeletalMesh->GetMorphTargets();
		for (TObjectPtr<UMorphTarget> Morph : Morphs)
		{
			if (Morph)
			{
				TSharedPtr<SMutableSearchComboBox::FFilteredOption> NodeOption = AddNodeHierarchyOptions(Candidate, AddedOptions);

				FString MorphTargetName = Morph->GetName();

				TSharedRef<SMutableSearchComboBox::FFilteredOption> Option = MakeShared<SMutableSearchComboBox::FFilteredOption>();
				Option->ActualOption = MorphTargetName;
				Option->DisplayOption = MorphTargetName;
				Option->Parent = NodeOption;
				MorphOptionsSource.Add(Option);
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
