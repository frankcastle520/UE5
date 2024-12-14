// Copyright Epic Games, Inc. All Rights Reserved.

#include "SActionMenu.h"

#include "GraphEditorSchemaActions.h"
#include "Framework/Application/SlateApplication.h"
#include "IDocumentation.h"
#include "SSubobjectEditor.h"
#include "AnimNextEdGraphSchema.h"
#include "RigVMHost.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "SGraphPalette.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMModel/RigVMClient.h"
#include "RigVMModel/RigVMSchema.h"
#include "Units/RigUnit.h"
#include "UncookedOnlyUtils.h"
#include "RigVMBlueprintGeneratedClass.h"

#define LOCTEXT_NAMESPACE "AnimNextEditor"

namespace UE::AnimNext::Editor
{

void SActionMenu::CollectAllAnimNextGraphActions(FGraphContextMenuBuilder& MenuBuilder) const
{
	// Disable reporting as the schema's SupportsX functions will output errors
	RigVMController->EnableReporting(false);

	for(const FRigVMFunction& Function : FRigVMRegistry::Get().GetFunctions())
	{
		if (RigVMSchema == nullptr || !RigVMSchema->SupportsUnitFunction(RigVMController, &Function))
		{
			continue;
		}

		UScriptStruct* Struct = Function.Struct;
		if (Struct == nullptr)
		{
			continue;
		}

		// skip deprecated units
		if(Function.Struct->HasMetaData(FRigVMStruct::DeprecatedMetaName))
		{
			continue;
		}

		// skip hidden units
		if(Function.Struct->HasMetaData(FRigVMStruct::HiddenMetaName))
		{
			continue;
		}

		FString CategoryMetadata, DisplayNameMetadata, MenuDescSuffixMetadata;
		Struct->GetStringMetaDataHierarchical(FRigVMStruct::CategoryMetaName, &CategoryMetadata);
		Struct->GetStringMetaDataHierarchical(FRigVMStruct::DisplayNameMetaName, &DisplayNameMetadata);
		if(DisplayNameMetadata.IsEmpty())
		{
			DisplayNameMetadata = Function.GetMethodName().ToString();
		}
		Struct->GetStringMetaDataHierarchical(FRigVMStruct::MenuDescSuffixMetaName, &MenuDescSuffixMetadata);
		if (!MenuDescSuffixMetadata.IsEmpty())
		{
			MenuDescSuffixMetadata = TEXT(" ") + MenuDescSuffixMetadata;
		}
		const FText NodeCategory = FText::FromString(CategoryMetadata);
		const FText MenuDesc = FText::FromString(DisplayNameMetadata + MenuDescSuffixMetadata);
		const FText ToolTip = Struct->GetToolTipText();

		if (MenuDesc.IsEmpty())
		{
			continue;
		}

		MenuBuilder.AddAction(MakeShared<FAnimNextSchemaAction_RigUnit>(Struct, NodeCategory, MenuDesc, ToolTip));
	}

	for (const FRigVMDispatchFactory* Factory : FRigVMRegistry::Get().GetFactories())
	{
		if (RigVMSchema == nullptr || !RigVMSchema->SupportsDispatchFactory(RigVMController, Factory))
		{
			continue;
		}

		const FRigVMTemplate* Template = Factory->GetTemplate();
		if (Template == nullptr)
		{
			continue;
		}

		// skip deprecated factories
		if(Factory->GetScriptStruct()->HasMetaData(FRigVMStruct::DeprecatedMetaName))
		{
			continue;
		}

		// skip hidden factories
		if(Factory->GetScriptStruct()->HasMetaData(FRigVMStruct::HiddenMetaName))
		{
			continue;
		}

		FText NodeCategory = FText::FromString(Factory->GetCategory());
		FText MenuDesc = FText::FromString(Factory->GetNodeTitle(FRigVMTemplateTypeMap()));
		FText ToolTip = Factory->GetNodeTooltip(FRigVMTemplateTypeMap());

		MenuBuilder.AddAction(MakeShared<FAnimNextSchemaAction_DispatchFactory>(Template->GetNotation(), NodeCategory, MenuDesc, ToolTip));
	};

	if (URigVMFunctionLibrary* LocalFunctionLibrary = RigVMClientHost->GetLocalFunctionLibrary())
	{
		const FSoftObjectPath LocalLibrarySoftPath = LocalFunctionLibrary->GetFunctionHostObjectPath();

		TArray<URigVMLibraryNode*> Functions = LocalFunctionLibrary->GetFunctions();
		for (URigVMLibraryNode* FunctionLibraryNode : Functions)
		{
			if (LocalFunctionLibrary->IsFunctionPublic(FunctionLibraryNode->GetFName()))	// Public functions will be added when processing asset registry exports
			{
				continue;
			}
			const FText NodeCategory = FText::FromString(FunctionLibraryNode->GetNodeCategory());
			const FText MenuDesc = FText::FromString(FunctionLibraryNode->GetName());
			const FText ToolTip = FunctionLibraryNode->GetToolTipText();

			MenuBuilder.AddAction(MakeShared<FAnimNextSchemaAction_Function>(FunctionLibraryNode, NodeCategory, MenuDesc, ToolTip));
		}
	}

	TMap<FAssetData, FRigVMGraphFunctionHeaderArray> FunctionExports;
	AnimNext::UncookedOnly::FUtils::GetExportedFunctionsFromAssetRegistry(UE::AnimNext::AnimNextPublicGraphFunctionsExportsRegistryTag, FunctionExports);
	AnimNext::UncookedOnly::FUtils::GetExportedFunctionsFromAssetRegistry(UE::AnimNext::ControlRigAssetPublicGraphFunctionsExportsRegistryTag, FunctionExports);

	for (const auto& Export : FunctionExports.Array())
	{
		for (const FRigVMGraphFunctionHeader& FunctionHeader : Export.Value.Headers)
		{
			if (FunctionHeader.LibraryPointer.IsValid())
			{
				const FText NodeCategory = FText::FromString(FunctionHeader.Category);
				const FText MenuDesc = FText::FromString(FunctionHeader.NodeTitle);
				const FText ToolTip = FunctionHeader.GetTooltip();

				MenuBuilder.AddAction(MakeShared<FAnimNextSchemaAction_Function>(FunctionHeader, NodeCategory, MenuDesc, ToolTip));
			}
		}
	}

	RigVMController->EnableReporting(true);
}

SActionMenu::~SActionMenu()
{
	OnClosedCallback.ExecuteIfBound();
	OnCloseReasonCallback.ExecuteIfBound(bActionExecuted, false, !DraggedFromPins.IsEmpty());
}

void SActionMenu::Construct(const FArguments& InArgs, UEdGraph* InGraph)
{
	check(InGraph);

	Graph = InGraph;
	DraggedFromPins = InArgs._DraggedFromPins;
	NewNodePosition = InArgs._NewNodePosition;
	OnClosedCallback = InArgs._OnClosedCallback;
	bAutoExpandActionMenu = InArgs._AutoExpandActionMenu;
	OnCloseReasonCallback = InArgs._OnCloseReason;

	RigVMClientHost = Graph->GetImplementingOuter<IRigVMClientHost>();
	check(RigVMClientHost);
	RigVMHost = Graph->GetTypedOuter<URigVMHost>();
	check(RigVMHost);
	RigVMController = RigVMClientHost->GetRigVMClient()->GetController(Graph);
	check(RigVMController);
	RigVMSchema = RigVMController->GetGraph()->GetSchema();
	check(RigVMSchema);

	SBorder::Construct(SBorder::FArguments()
		.BorderImage(FAppStyle::Get().GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			.WidthOverride(400)
			.HeightOverride(400)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				[
					SAssignNew(GraphActionMenu, SGraphActionMenu)
						.OnActionSelected(this, &SActionMenu::OnActionSelected)
						.OnCreateWidgetForAction(SGraphActionMenu::FOnCreateWidgetForAction::CreateSP(this, &SActionMenu::OnCreateWidgetForAction))
						.OnCollectAllActions(this, &SActionMenu::CollectAllActions)
						.OnCreateCustomRowExpander_Lambda([](const FCustomExpanderData& InActionMenuData)
						{
							// Default table row doesnt indent correctly
							return SNew(SExpanderArrow, InActionMenuData.TableRow);
						})
						.DraggedFromPins(DraggedFromPins)
						.GraphObj(Graph)
						.AlphaSortItems(true)
						.bAllowPreselectedItemActivation(true)
				]
			]
		]
	);
}

void SActionMenu::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	if (!Graph)
	{
		return;
	}

	FGraphContextMenuBuilder MenuBuilder(Graph);
	if (!DraggedFromPins.IsEmpty())
	{
		MenuBuilder.FromPin = DraggedFromPins[0];
	}

	// Cannot call GetGraphContextActions() during serialization and GC due to its use of FindObject()
	if(!GIsSavingPackage && !IsGarbageCollecting())
	{
		CollectAllAnimNextGraphActions(MenuBuilder);
	}

	OutAllActions.Append(MenuBuilder);
}

TSharedRef<SEditableTextBox> SActionMenu::GetFilterTextBox()
{
	return GraphActionMenu->GetFilterTextBox();
}

TSharedRef<SWidget> SActionMenu::OnCreateWidgetForAction(FCreateWidgetForActionData* const InCreateData)
{
	check(InCreateData);
	InCreateData->bHandleMouseButtonDown = false;

	const FSlateBrush* IconBrush = nullptr;
	FLinearColor IconColor;
	TSharedPtr<FAnimNextSchemaAction> Action = StaticCastSharedPtr<FAnimNextSchemaAction>(InCreateData->Action);
	if (Action.IsValid())
	{
		IconBrush = Action->GetIconBrush();
		IconColor = Action->GetIconColor();
	}

	TSharedPtr<SHorizontalBox> WidgetBox = SNew(SHorizontalBox);
	if (IconBrush)
	{
		WidgetBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 0, 0)
			[
				SNew(SImage)
				.ColorAndOpacity(IconColor)
				.Image(IconBrush)
			];
	}

	WidgetBox->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(IconBrush ? 4.0f : 0.0f, 0, 0, 0)
		[
			SNew(SGraphPaletteItem, InCreateData)
		];

	return WidgetBox->AsShared();
}

void SActionMenu::OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedAction, ESelectInfo::Type InSelectionType)
{
	if (!Graph)
	{
		return;
	}

	if (InSelectionType != ESelectInfo::OnMouseClick  && InSelectionType != ESelectInfo::OnKeyPress && !SelectedAction.IsEmpty())
	{
		return;
	}

	for (const TSharedPtr<FEdGraphSchemaAction>& Action : SelectedAction)
	{
		if (Action.IsValid() && Graph)
		{
			if (!bActionExecuted && (Action->GetTypeId() != FEdGraphSchemaAction_Dummy::StaticGetTypeId()))
			{
				FSlateApplication::Get().DismissAllMenus();
				bActionExecuted = true;
			}

			Action->PerformAction(Graph, DraggedFromPins, NewNodePosition);
		}
	}
}

} 

#undef LOCTEXT_NAMESPACE
