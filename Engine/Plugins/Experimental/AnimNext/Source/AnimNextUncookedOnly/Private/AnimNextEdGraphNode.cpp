// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextEdGraphNode.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "TraitCore/TraitHandle.h"
#include "Graph/RigUnit_AnimNextTraitStack.h"
#include "Graph/RigDecorator_AnimNextCppTrait.h"
#include "RigVMModel/RigVMController.h"
#include "TraitCore/TraitRegistry.h"
#include "ToolMenu.h"
#include "UncookedOnlyUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Graph/RigUnit_AnimNextRunAnimationGraph.h"
#include "Graph/RigVMTrait_AnimNextPublicVariables.h"

#define LOCTEXT_NAMESPACE "AnimNextEdGraphNode"

namespace UE::AnimNext::Editor::Private
{
	static const FLazyName VariablesTraitBaseName = TEXT("Variables");
}

void UAnimNextEdGraphNode::GetNodeContextMenuActions(UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const
{
	using namespace UE::AnimNext::Editor::Private;
	
	URigVMEdGraphNode::GetNodeContextMenuActions(Menu, Context);

	UAnimNextEdGraphNode* NonConstThis = const_cast<UAnimNextEdGraphNode*>(this);

	if (IsTraitStack())
	{
		FToolMenuSection& Section = Menu->AddSection("AnimNextTraitNodeActions", LOCTEXT("AnimNextTraitNodeActionsMenuHeader", "Traits"));

		Section.AddSubMenu(
			TEXT("AddTraitMenu"),
			LOCTEXT("AddTraitMenu", "Add Trait"),
			LOCTEXT("AddTraitMenuTooltip", "Add the chosen trait to currently selected node"),
			FNewToolMenuDelegate::CreateUObject(NonConstThis, &UAnimNextEdGraphNode::BuildAddTraitContextMenu));
	}
	else if(IsRunGraphNode())
	{
		FToolMenuSection& Section = Menu->AddSection("AnimNextRunAnimGraphNodeActions", LOCTEXT("AnimNextAnimGraphNodeActionsMenuHeader", "Animation Graph"));

		URigVMController* VMController = GetController();
		URigVMNode* VMNode = GetModelNode();
		URigVMPin* VMPin = Context->Pin != nullptr ? FindModelPinFromGraphPin(Context->Pin) : nullptr;

		if(VMPin != nullptr && VMNode->FindTrait(VMPin))
		{
			Section.AddMenuEntry(
				TEXT("RemoveExposedVariables"),
				LOCTEXT("RemoveExposedVariablesMenu", "Remove Exposed Variables"),
				LOCTEXT("RemoveExposeVariablesMenuTooltip", "Remove the exposed variable trait from this node"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([VMController, VMNode, Name = VMPin->GetFName()]()
					{
						VMController->RemoveTrait(VMNode, Name);
					})
				));
		}
		else
		{
			Section.AddSubMenu(
				TEXT("ExposeVariables"),
				LOCTEXT("ExposeVariablesMenu", "Expose Variables"),
				LOCTEXT("ExposeVariablesMenuTooltip", "Expose the variables of a selected animation graph as pins on this node"),
				FNewToolMenuDelegate::CreateUObject(NonConstThis, &UAnimNextEdGraphNode::BuildExposeVariablesContextMenu));
		}
	}
}

void UAnimNextEdGraphNode::ConfigurePin(UEdGraphPin* EdGraphPin, const URigVMPin* ModelPin) const
{
	Super::ConfigurePin(EdGraphPin, ModelPin);

	// Trait handles always remain as a RigVM input pins so that we can still link things to them even if they are hidden
	// We handle visibility for those explicitly here
	const bool bIsInputPin = ModelPin->GetDirection() == ERigVMPinDirection::Input;
	const bool bIsTraitHandle = ModelPin->GetCPPTypeObject() == FAnimNextTraitHandle::StaticStruct();
	if (bIsInputPin && bIsTraitHandle)
	{
		if (const URigVMPin* DecoratorPin = ModelPin->GetParentPin())
		{
			if (DecoratorPin->IsTraitPin())
			{
				check(DecoratorPin->GetScriptStruct() == FRigDecorator_AnimNextCppDecorator::StaticStruct());

				TSharedPtr<FStructOnScope> DecoratorScope = DecoratorPin->GetTraitInstance();
				const FRigDecorator_AnimNextCppDecorator* VMDecorator = (const FRigDecorator_AnimNextCppDecorator*)DecoratorScope->GetStructMemory();

				const UScriptStruct* TraitStruct = VMDecorator->GetTraitSharedDataStruct();
				check(TraitStruct != nullptr);

				const FProperty* PinProperty = TraitStruct->FindPropertyByName(ModelPin->GetFName());
				EdGraphPin->bHidden = PinProperty->HasMetaData(FRigVMStruct::HiddenMetaName);
			}
		}
	}
}

bool UAnimNextEdGraphNode::IsTraitStack() const
{
	if (const URigVMUnitNode* VMNode = Cast<URigVMUnitNode>(GetModelNode()))
	{
		const UScriptStruct* ScriptStruct = VMNode->GetScriptStruct();
		return ScriptStruct == FRigUnit_AnimNextTraitStack::StaticStruct();
	}

	return false;
}

bool UAnimNextEdGraphNode::IsRunGraphNode() const
{
	if (const URigVMUnitNode* VMNode = Cast<URigVMUnitNode>(GetModelNode()))
	{
		const UScriptStruct* ScriptStruct = VMNode->GetScriptStruct();
		return ScriptStruct == FRigUnit_AnimNextRunAnimationGraph::StaticStruct();
	}

	return false;
}

void UAnimNextEdGraphNode::BuildAddTraitContextMenu(UToolMenu* SubMenu)
{
	using namespace UE::AnimNext;

	const FTraitRegistry& TraitRegistry = FTraitRegistry::Get();
	TArray<const FTrait*> Traits = TraitRegistry.GetTraits();

	URigVMController* VMController = GetController();
	URigVMNode* VMNode = GetModelNode();

	const UScriptStruct* CppDecoratorStruct = FRigDecorator_AnimNextCppDecorator::StaticStruct();

	for (const FTrait* Trait : Traits)
	{
		UScriptStruct* ScriptStruct = Trait->GetTraitSharedDataStruct();

		FString DefaultValue;
		{
			const FRigDecorator_AnimNextCppDecorator DefaultCppDecoratorStructInstance;
			FRigDecorator_AnimNextCppDecorator CppDecoratorStructInstance;
			CppDecoratorStructInstance.DecoratorSharedDataStruct = ScriptStruct;

			if (!CppDecoratorStructInstance.CanBeAddedToNode(VMNode, nullptr))
			{
				continue;	// This trait isn't supported on this node
			}

			const FProperty* Prop = FAnimNextCppDecoratorWrapper::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(FAnimNextCppDecoratorWrapper, CppDecorator));
			Prop->ExportText_Direct(DefaultValue, &CppDecoratorStructInstance, &DefaultCppDecoratorStructInstance, nullptr, PPF_SerializedAsImportText);
		}

		FString DisplayNameMetadata;
		ScriptStruct->GetStringMetaDataHierarchical(FRigVMStruct::DisplayNameMetaName, &DisplayNameMetadata);
		const FString DisplayName = DisplayNameMetadata.IsEmpty() ? Trait->GetTraitName() : DisplayNameMetadata;

		const FText ToolTip = ScriptStruct->GetToolTipText();

		FToolMenuEntry TraitEntry = FToolMenuEntry::InitMenuEntry(
			*Trait->GetTraitName(),
			FText::FromString(DisplayName),
			ToolTip,
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda(
				[this, Trait, VMController, VMNode, CppDecoratorStruct, DefaultValue, DisplayName]()
				{
					VMController->AddTrait(
						VMNode->GetFName(),
						*CppDecoratorStruct->GetPathName(),
						*DisplayName,
						DefaultValue, INDEX_NONE, true, true);
				})
			)
		);

		SubMenu->AddMenuEntry(NAME_None, TraitEntry);
	}
}

void UAnimNextEdGraphNode::BuildExposeVariablesContextMenu(UToolMenu* SubMenu)
{
	using namespace UE::AnimNext::Editor::Private;

	URigVMController* VMController = GetController();
	URigVMNode* VMNode = GetModelNode();

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.Filter.ClassPaths.Add(UAnimNextDataInterface::StaticClass()->GetClassPathName());
	AssetPickerConfig.Filter.bRecursiveClasses = true;
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.AssetShowWarningText = LOCTEXT("NoAssetsWithPublicVariablesMessage", "No animation graphs with public variables found");
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([VMController, VMNode](const FAssetData& InAssetData)
	{
		FSlateApplication::Get().DismissAllMenus();

		FString DefaultValue;
		FRigVMTrait_AnimNextPublicVariables DefaultTrait;
		FRigVMTrait_AnimNextPublicVariables NewTrait;
		UAnimNextDataInterface* Asset = CastChecked<UAnimNextDataInterface>(InAssetData.GetAsset());
		NewTrait.Asset = Asset;
		const FInstancedPropertyBag& PublicVariableDefaults = Asset->GetPublicVariableDefaults();
		TConstArrayView<FPropertyBagPropertyDesc> Descs = PublicVariableDefaults.GetPropertyBagStruct()->GetPropertyDescs();
		NewTrait.VariableNames.Reserve(Descs.Num());
		for(const FPropertyBagPropertyDesc& Desc : Descs)
		{
			NewTrait.VariableNames.Add(Desc.Name);
		}
		FRigVMTrait_AnimNextPublicVariables::StaticStruct()->ExportText(DefaultValue, &NewTrait, &DefaultTrait, nullptr, PPF_SerializedAsImportText, nullptr);

		const FName ValidTraitName = URigVMSchema::GetUniqueName(VariablesTraitBaseName, [VMNode](const FName& InName) {
			return VMNode->FindPin(InName.ToString()) == nullptr;
		}, false, false);
		VMController->AddTrait(VMNode, FRigVMTrait_AnimNextPublicVariables::StaticStruct(), ValidTraitName, DefaultValue);
	});

	AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateLambda([this](const FAssetData& InAssetData)
	{
		// Filter to only show assets with public variables
		FAnimNextAssetRegistryExports Exports;
		if(UE::AnimNext::UncookedOnly::FUtils::GetExportedVariablesForAsset(InAssetData, Exports))
		{
			for(const FAnimNextAssetRegistryExportedVariable& Export : Exports.Variables)
			{
				if((Export.GetFlags() & EAnimNextExportedVariableFlags::Public) != EAnimNextExportedVariableFlags::NoFlags)
				{
					return false;
				}
			}
		}
		return true;
	});

	FToolMenuEntry Entry = FToolMenuEntry::InitWidget(
		TEXT("AnimationGraphPicker"),
		SNew(SBox)
		.WidthOverride(300.0f)
		.HeightOverride(400.0f)
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		],
		FText::GetEmpty(),
		true,
		false,
		false,
		LOCTEXT("AnimationGraphPickerTooltip", "Choose an animation graph with public variables to expose")
	);
	
	SubMenu->AddMenuEntry(NAME_None, Entry);
}

#undef LOCTEXT_NAMESPACE
