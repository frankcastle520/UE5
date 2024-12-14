// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraph.h"

#include "Algo/Transform.h"
#include "Analysis/MetasoundFrontendAnalyzerAddress.h"
#include "AudioParameterControllerInterface.h"
#include "Components/AudioComponent.h"
#include "EdGraph/EdGraphNode.h"
#include "Interfaces/ITargetPlatform.h"
#include "MetasoundAssetBase.h"
#include "MetasoundDocumentBuilderRegistry.h"
#include "MetasoundEditorGraphCommentNode.h"
#include "MetasoundEditorGraphInputNode.h"
#include "MetasoundEditorGraphMemberDefaults.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphValidation.h"
#include "MetasoundEditorModule.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundEditorSubsystem.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundFrontendDocumentBuilder.h"
#include "MetasoundFrontendNodeTemplateRegistry.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundLog.h"
#include "MetasoundSettings.h"
#include "MetasoundUObjectRegistry.h"
#include "MetasoundVariableNodes.h"
#include "MetasoundVertex.h"
#include "NodeTemplates/MetasoundFrontendNodeTemplateInput.h"
#include "ScopedTransaction.h"
#include "UObject/NameTypes.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetasoundEditorGraph)

#define LOCTEXT_NAMESPACE "MetaSoundEditor"

// Parameter names do not support analyzer path separator, but do support
// spaces (to be as consistent as possible with other systems such as Blueprint)
#define INVALID_PARAMETER_NAME_CHARACTERS TEXT("\"',\n\r\t") METASOUND_ANALYZER_PATH_SEPARATOR


namespace Metasound::Editor
{
	namespace GraphPrivate
	{
		FName GetUniqueTransientMemberName()
		{
			// Use unique instance ID to avoid copy/paste logic resolving invalid relationship between graphs.
			// Equality is properly resolved based on associated Frontend node's Name, TypeName, & AccessType.
			// This can bloat the name table within a editor given session, but ed graph is not serialized so
			// purely for editing.
			return *FString::Printf(TEXT("Member_%s"), *FGuid::NewGuid().ToString());
		}

		void OnLiteralChanged(UMetasoundEditorGraphMember& InMember, const FGuid* InPageID, EPropertyChangeType::Type InChangeType)
		{
			constexpr bool bPostTransaction = false;
			InMember.UpdateFrontendDefaultLiteral(bPostTransaction, InPageID);

			const bool bCommitChange = InChangeType != EPropertyChangeType::Interactive;
			if (bCommitChange)
			{
				if (UObject* MetaSound = InMember.GetOutermostObject())
				{
					FGraphBuilder::RegisterGraphWithFrontend(*MetaSound);
					if (FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(MetaSound))
					{
						MetaSoundAsset->GetModifyContext().AddMemberIDsModified({ InMember.GetMemberID() });
					}
				}
			}
		}

		// Avoids member literal setting the node literal if its not required (which in turn
		// avoids 'Reset To Default' action from being enabled when the default is equal)
		void SetOrClearIfLiteralMatchesNodeVertexDefault(FMetaSoundFrontendDocumentBuilder& InBuilder, const FMetasoundFrontendVertexHandle& VertexHandle, const FMetasoundFrontendLiteral& InDefaultLiteral)
		{
			const FMetasoundFrontendVertex* Vertex = InBuilder.FindNodeInput(VertexHandle.NodeID, VertexHandle.VertexID);
			check(Vertex);

			bool bClearLiteral = false;
			const TArray<FMetasoundFrontendClassInputDefault>* ClassDefaults = InBuilder.FindNodeClassInputDefaults(VertexHandle.NodeID, Vertex->Name);
			if (ClassDefaults)
			{
				const FGuid PageID = Engine::FDocumentBuilderRegistry::GetChecked().ResolveTargetPageID(*ClassDefaults);
				auto MatchesPageID = [&PageID](const FMetasoundFrontendClassInputDefault& InputDefault) { return InputDefault.PageID == PageID; };
				if (const FMetasoundFrontendClassInputDefault* ClassDefault = ClassDefaults->FindByPredicate(MatchesPageID))
				{
					bClearLiteral = ClassDefault->Literal.IsEqual(InDefaultLiteral);
				}
			}

			if (!bClearLiteral)
			{
				FMetasoundFrontendLiteral DefaultTypeLiteral;
				DefaultTypeLiteral.SetFromLiteral(Frontend::IDataTypeRegistry::Get().CreateDefaultLiteral(Vertex->TypeName));
				bClearLiteral = InDefaultLiteral.IsEqual(DefaultTypeLiteral);
			}

			if (bClearLiteral)
			{
				InBuilder.RemoveNodeInputDefault(VertexHandle.NodeID, VertexHandle.VertexID);
			}
			else
			{
				InBuilder.SetNodeInputDefault(VertexHandle.NodeID, VertexHandle.VertexID, InDefaultLiteral);
			}
		}

		void UpdatePreviewParameter(UMetasoundEditorGraph* MetaSoundGraph, FName MemberName, UMetasoundEditorGraphMemberDefaultLiteral& Literal)
		{
			if (GEditor)
			{
				if (MetaSoundGraph && MetaSoundGraph->IsPreviewing())
				{
					UAudioComponent* PreviewComponent = GEditor->GetPreviewAudioComponent();
					check(PreviewComponent);

					if (TScriptInterface<IAudioParameterControllerInterface> ParamInterface = PreviewComponent)
					{
						Literal.UpdatePreviewInstance(MemberName, ParamInterface);
					}
				}
			}
		}
	} // namespace GraphPrivate
} // namespace Metasound::Editor

FMetaSoundFrontendDocumentBuilder& UMetasoundEditorGraphMember::GetFrontendBuilderChecked() const
{
	using namespace Metasound::Frontend;

	const UMetasoundEditorGraph* Graph = GetOwningGraph();
	check(Graph);
	UObject& MetaSound = Graph->GetMetasoundChecked();
	return IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(&MetaSound);
}

UMetasoundEditorGraph* UMetasoundEditorGraphMember::GetOwningGraph()
{
	// Due to a prior document migration that enables ed graphs to be built from the frontend document exclusively,
	// MetaSound objects may contain more than one editor graph, so must check outer rather than accessing the
	// transient graph from the FMetaSoundAssetBase layer.
	return Cast<UMetasoundEditorGraph>(GetOuter());
}

const UMetasoundEditorGraph* UMetasoundEditorGraphMember::GetOwningGraph() const
{
	// Due to a prior document migration that enables ed graphs to be built from the frontend document exclusively,
	// MetaSound objects may contain more than one editor graph, so must check outer rather than accessing the
	// transient graph from the FMetaSoundAssetBase layer.
	return Cast<const UMetasoundEditorGraph>(GetOuter());
}

void UMetasoundEditorGraphMember::InitializeLiteral()
{
	using namespace Metasound;
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	FDataTypeRegistryInfo DataTypeInfo;
	IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
	IDataTypeRegistry::Get().GetDataTypeInfo(TypeName, DataTypeInfo);
	const EMetasoundFrontendLiteralType LiteralType = static_cast<EMetasoundFrontendLiteralType>(DataTypeInfo.PreferredLiteralType);

	TSubclassOf<UMetasoundEditorGraphMemberDefaultLiteral> LiteralClass = EditorModule.FindDefaultLiteralClass(LiteralType);
	if (!LiteralClass)
	{
		LiteralClass = UMetasoundEditorGraphMemberDefaultLiteral::StaticClass();
	}

	if (!Literal || Literal->GetClass() != LiteralClass)
	{
		FMetaSoundFrontendDocumentBuilder& Builder = GetFrontendBuilderChecked();
		const bool bIsNew = UMetaSoundEditorSubsystem::GetChecked().BindMemberMetadata(Builder, *this, LiteralClass);
		checkf(Literal, TEXT("Bind is required to initialize literal field on this member"));

		if (bIsNew)
		{
			Literal->Initialize();
		}
	}
}

FName UMetasoundEditorGraphMember::GetDataType() const
{
	return TypeName;
}

#if WITH_EDITOR
void UMetasoundEditorGraphMember::PostEditUndo()
{
	using namespace Metasound;

	Super::PostEditUndo();

	if (IsValid(this))
	{
		constexpr bool bPostTransaction = false;
		SetDataType(TypeName, bPostTransaction);
		UpdateFrontendDefaultLiteral(bPostTransaction);
	}
}
#endif // WITH_EDITOR

bool UMetasoundEditorGraphMember::Synchronize()
{
	bool bModified = false;
	if (!Literal)
	{
		bModified = true;
		InitializeLiteral();
	}

	return bModified;
}

void UMetasoundEditorGraphVertex::InitMember(FName InDataType, const FMetasoundFrontendLiteral& InDefaultLiteral, FGuid InNodeID, FMetasoundFrontendClassName&& InClassName)
{
	TypeName = InDataType;
	NodeID = InNodeID;
	ClassName = MoveTemp(InClassName);

	InitializeLiteral();

	if (ensure(Literal))
	{
		Literal->SetFromLiteral(InDefaultLiteral);
	}
}

const FMetasoundFrontendNode* UMetasoundEditorGraphVertex::GetFrontendNode() const
{
	using namespace Metasound::Engine;

	if (const UMetasoundEditorGraph* Graph = GetOwningGraph())
	{
		const FMetaSoundFrontendDocumentBuilder& Builder = FDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(&Graph->GetMetasoundChecked());
		return Builder.FindNode(NodeID);
	}

	return nullptr;
}

TArray<UMetasoundEditorGraphMemberNode*> UMetasoundEditorGraphVertex::GetNodes() const
{
	TArray<UMetasoundEditorGraphMemberNode*> Nodes;

	const UMetasoundEditorGraph* Graph = GetOwningGraph();
	if (ensure(Graph))
	{
		Graph->GetNodesOfClassEx<UMetasoundEditorGraphMemberNode>(Nodes);
		for (int32 i = Nodes.Num() -1; i >= 0; --i)
		{
			UMetasoundEditorGraphNode* Node = Nodes[i];
			if (Node && Node->GetNodeID() != NodeID)
			{
				Nodes.RemoveAtSwap(i, EAllowShrinking::No);
			}
		}
	}

	return Nodes;
}

void UMetasoundEditorGraphVertex::SetDescription(const FText& InDescription, bool bPostTransaction)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	{
		const FText TransactionLabel = FText::Format(LOCTEXT("SetGraphVertexTooltipFormat", "Set MetaSound {0}'s ToolTip"), GetGraphMemberLabel());
		const FScopedTransaction Transaction(TransactionLabel, bPostTransaction);

		if (UMetasoundEditorGraph* Graph = GetOwningGraph())
		{
			Graph->Modify();
			UObject& MetaSound = Graph->GetMetasoundChecked();
			MetaSound.Modify();

			FNodeHandle NodeHandle = GetNodeHandle();
			NodeHandle->SetDescription(InDescription);

			Graph->RegisterGraphWithFrontend();
			FGraphBuilder::GetOutermostMetaSoundChecked(*Graph).GetModifyContext().AddMemberIDsModified({ GetMemberID() });
		}
	}
}

FGuid UMetasoundEditorGraphVertex::GetMemberID() const 
{ 
	return NodeID;
}

FName UMetasoundEditorGraphVertex::GetMemberName() const
{
	using namespace Metasound::Frontend;

	if (const FMetasoundFrontendNode* FrontendNode = GetFrontendNode())
	{
		return FrontendNode->Name;
	}

	return FName();
}

void UMetasoundEditorGraphVertex::SetMemberName(const FName& InNewName, bool bPostTransaction)
{
	using namespace Metasound;
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* Graph = GetOwningGraph();
	if (!ensure(Graph))
	{
		return;
	}

	FMetaSoundFrontendDocumentBuilder& DocBuilder = GetFrontendBuilderChecked();
	FName OldName;
	if (const FMetasoundFrontendNode* Node = DocBuilder.FindNode(NodeID); ensure(Node))
	{
		if (Node->Name == InNewName)
		{
			return;
		}
		OldName = Node->Name;
	}

	const FText TransactionLabel = FText::Format(LOCTEXT("RenameGraphVertexMemberNameFormat", "Set Metasound {0} MemberName"), GetGraphMemberLabel());
	const FScopedTransaction Transaction(TransactionLabel, bPostTransaction);

	Graph->Modify();
	Graph->GetMetasoundChecked().Modify();

	RenameFrontendMemberInternal(DocBuilder, OldName, InNewName);

	const TArray<UMetasoundEditorGraphMemberNode*> Nodes = GetNodes();
	for (UMetasoundEditorGraphMemberNode* Node : Nodes)
	{
		const TArray<UEdGraphPin*>& Pins = Node->GetAllPins();
		ensure(Pins.Num() == 1);

		for (UEdGraphPin* Pin : Pins)
		{
			Pin->Modify();
			Pin->PinName = InNewName;
		}
	}

	Graph->RegisterGraphWithFrontend();
}

FText UMetasoundEditorGraphVertex::GetDisplayName() const
{
	constexpr bool bIncludeNamespace = true;
	return Metasound::Editor::FGraphBuilder::GetDisplayName(*GetConstNodeHandle(), bIncludeNamespace);
}

void UMetasoundEditorGraphVertex::SetDisplayName(const FText& InNewName, bool bPostTransaction)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* Graph = GetOwningGraph();
	if (!ensure(Graph))
	{
		return;
	}


	FNodeHandle NodeHandle = GetNodeHandle();
	if (NodeHandle->GetDisplayName().EqualTo(InNewName))
	{
		return;
	}

	const FText TransactionLabel = FText::Format(LOCTEXT("RenameGraphVertexDisplayNameFormat", "Set Metasound {0} DisplayName"), GetGraphMemberLabel());
	const FScopedTransaction Transaction(TransactionLabel, bPostTransaction);

	Graph->Modify();
	Graph->GetMetasoundChecked().Modify();

	NodeHandle->SetDisplayName(InNewName);

	const TArray<UMetasoundEditorGraphMemberNode*> Nodes = GetNodes();
	for (UMetasoundEditorGraphMemberNode* Node : Nodes)
	{
		const TArray<UEdGraphPin*>& Pins = Node->GetAllPins();
		ensure(Pins.Num() == 1);

		for (UEdGraphPin* Pin : Pins)
		{
			Pin->PinFriendlyName = InNewName;
		}
	}

	Graph->RegisterGraphWithFrontend();
	FGraphBuilder::GetOutermostMetaSoundChecked(*Graph).GetModifyContext().AddMemberIDsModified({ GetMemberID() });
}
#if WITH_EDITORONLY_DATA
bool UMetasoundEditorGraphVertex::SetIsAdvancedDisplay(const bool IsAdvancedDisplay)
{
	UMetasoundEditorGraph* Graph = GetOwningGraph();
	if (!ensure(Graph))
	{
		return false;
	}

	const FText TransactionLabel = FText::Format(LOCTEXT("SetGraphVertexIsAdvancedDisplayState", "Set Metasound {0} IsAdvancedDislay"), GetGraphMemberLabel());
	const FScopedTransaction Transaction(TransactionLabel, true);

	Graph->Modify();
	Graph->GetMetasoundChecked().Modify();
	Modify();

	bool bSucceeded = false;
	
	if (GetClassType() == EMetasoundFrontendClassType::Input)
	{
		bSucceeded = GetFrontendBuilderChecked().SetGraphInputAdvancedDisplay(GetMemberName(), IsAdvancedDisplay);
		
	}
	else if(GetClassType() == EMetasoundFrontendClassType::Output)
	{
		bSucceeded = GetFrontendBuilderChecked().SetGraphOutputAdvancedDisplay(GetMemberName(), IsAdvancedDisplay);
	}

	const FMetaSoundFrontendDocumentBuilder& Builder = GetFrontendBuilderChecked();	
	if (const FMetasoundFrontendNode* Node = Builder.FindNode(NodeID))
	{
		if (const FMetasoundFrontendClass* Class = Builder.FindDependency(Node->ClassID))
		{
			ClassName = Class->Metadata.GetClassName();
		}
	}

	Graph->RegisterGraphWithFrontend();
	return bSucceeded;
}
#endif // WITH_EDITORONLY_DATA

Metasound::Frontend::FNodeHandle UMetasoundEditorGraphVertex::GetNodeHandle()
{
	using namespace Metasound;

	UMetasoundEditorGraph* Graph = GetOwningGraph();
	check(Graph);

	UObject* Object = Graph->GetMetasound();
	if (!ensure(Object))
	{
		return Frontend::INodeController::GetInvalidHandle();
	}

	FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Object);
	check(MetasoundAsset);

	return MetasoundAsset->GetRootGraphHandle()->GetNodeWithID(NodeID);
}

Metasound::Frontend::FConstNodeHandle UMetasoundEditorGraphVertex::GetConstNodeHandle() const
{
	using namespace Metasound;

	const FMetasoundAssetBase& MetaSound = Editor::FGraphBuilder::GetOutermostConstMetaSoundChecked(*this);
	return MetaSound.GetRootGraphHandle()->GetNodeWithID(NodeID);
}

const FMetasoundFrontendVersion& UMetasoundEditorGraphVertex::GetInterfaceVersion() const
{
	return GetConstNodeHandle()->GetInterfaceVersion();
}

bool UMetasoundEditorGraphVertex::IsInterfaceMember(FMetasoundFrontendInterface* OutInterface) const
{
	return false;
}

bool UMetasoundEditorGraphVertex::NameContainsInterfaceNamespace(FMetasoundFrontendInterface* OutInterface) const
{
	using namespace Metasound::Frontend;

	const FName MemberName = GetMemberName();
	FName InterfaceNamespace;
	FName ParamName;
	Audio::FParameterPath::SplitName(MemberName, InterfaceNamespace, ParamName);

	FMetasoundFrontendInterface FoundInterface;
	if (!InterfaceNamespace.IsNone() && ISearchEngine::Get().FindInterfaceWithHighestVersion(InterfaceNamespace, FoundInterface))
	{
		if (OutInterface)
		{
			*OutInterface = MoveTemp(FoundInterface);
		}
		return true;
	}

	if (OutInterface)
	{
		*OutInterface = { };
	}
	return false;
}

bool UMetasoundEditorGraphVertex::CanRename() const
{
	Metasound::Frontend::FConstDocumentHandle DocumentHandle = GetOwningGraph()->GetDocumentHandle();
	const FMetasoundFrontendGraphClass& RootGraphClass = DocumentHandle->GetRootGraphClass();

	return !RootGraphClass.PresetOptions.bIsPreset && !IsInterfaceMember();
}

bool UMetasoundEditorGraphVertex::CanRename(const FText& InNewText, FText& OutError) const
{
	using namespace Metasound::Frontend;

	if (InNewText.IsEmptyOrWhitespace())
	{
		OutError = FText::Format(LOCTEXT("GraphVertexRenameInvalid_NameEmpty", "{0} cannot be empty string."), InNewText);
		return false;
	}

	const FString NewNameString = InNewText.ToString();
	if (!FName::IsValidXName(NewNameString, INVALID_PARAMETER_NAME_CHARACTERS, &OutError))
	{
		return false;
	}

	if (IsInterfaceMember())
	{
		const FText CurrentMemberName = FText::FromName(GetMemberName());
		OutError = FText::Format(LOCTEXT("GraphVertexRenameInvalid_GraphVertexRequired", "{0} is interface member and cannot be renamed."), CurrentMemberName);
		return false;
	}

	Metasound::Frontend::FConstDocumentHandle DocumentHandle = GetOwningGraph()->GetDocumentHandle();
	const FMetasoundFrontendGraphClass& RootGraphClass = DocumentHandle->GetRootGraphClass();

	if (RootGraphClass.PresetOptions.bIsPreset)
	{
		OutError = FText::Format(LOCTEXT("GraphVertexRenameInvalid_Preset", "{0} is a vertex in a preset graph and cannot be renamed."), InNewText);
		return false;
	}

	const FName NewName(*NewNameString);
	FName Namespace;
	FName ParameterName;
	Audio::FParameterPath::SplitName(NewName, Namespace, ParameterName);

	bool bIsNameValid = true;
	FConstNodeHandle NodeHandle = GetConstNodeHandle();
	FConstGraphHandle GraphHandle = NodeHandle->GetOwningGraph();
	GraphHandle->IterateConstNodes([&](FConstNodeHandle NodeToCompare)
	{
		if (NodeID != NodeToCompare->GetID())
		{
			FName OtherName = NodeToCompare->GetNodeName();
			if (NewName == OtherName)
			{
				bIsNameValid = false;
				OutError = FText::Format(LOCTEXT("GraphVertexRenameInvalid_NameTaken", "{0} is already in use"), InNewText);
			}
			else if (Namespace == OtherName)
			{
				bIsNameValid = false;
				OutError = FText::Format(LOCTEXT("GraphVertexRenameInvalid_NamespaceTaken", "Namespace of '{0}' cannot be the same as an existing member's name"), InNewText);
			}
			else
			{
				FName OtherNamespace;
				Audio::FParameterPath::SplitName(OtherName, OtherNamespace, OtherName);
				if (OtherNamespace == NewName)
				{
					bIsNameValid = false;
					OutError = FText::Format(LOCTEXT("GraphVertexRenameInvalid_NamespaceTaken2", "Name of '{0}' cannot be the same as an existing member's namespace"), InNewText);
				}
			}
		}
	}, GetClassType());

	return bIsNameValid;
}

bool UMetasoundEditorGraphVertex::Synchronize()
{
	bool bModified = Super::Synchronize();

	if (const FMetasoundFrontendClassVertex* Vertex = GetFrontendClassVertex(); ensure(Vertex))
	{
		if (TypeName != Vertex->TypeName)
		{
			bModified = true;
			TypeName = Vertex->TypeName;

			InitializeLiteral();
		}

		FMetaSoundFrontendDocumentBuilder& Builder = GetFrontendBuilderChecked();
		if (const FMetasoundFrontendNode* Node = Builder.FindNode(NodeID); ensure(Node))
		{
			if (const FMetasoundFrontendClass* Class = Builder.FindDependency(Node->ClassID); ensure(Class))
			{
				const FMetasoundFrontendClassName& FrontendClassName = Class->Metadata.GetClassName();
				if (ClassName != FrontendClassName)
				{
					bModified = true;
					ClassName = FrontendClassName;
				}
			}
		}
	}

	return bModified;
}

UMetasoundEditorGraphMember* UMetasoundEditorGraphMemberDefaultLiteral::FindMember() const
{
	using namespace Metasound;

	const FMetasoundAssetBase& MetaSound = Editor::FGraphBuilder::GetOutermostConstMetaSoundChecked(*this);
	if (UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(MetaSound.GetGraph()))
	{
		return Graph->FindMember(MemberID);
	}

	return nullptr;
}

void UMetasoundEditorGraphMemberDefaultLiteral::ForceRefresh()
{
}

FName UMetasoundEditorGraphMemberDefaultLiteral::GetDataType() const
{
	return { };
}

EMetasoundFrontendLiteralType UMetasoundEditorGraphMemberDefaultLiteral::GetLiteralType() const
{
	return EMetasoundFrontendLiteralType::None;
}

void UMetasoundEditorGraphMemberDefaultLiteral::InitDefault(const FGuid& InPageID)
{
}

void UMetasoundEditorGraphMemberDefaultLiteral::IterateDefaults(TFunctionRef<void(const FGuid&, FMetasoundFrontendLiteral)> Iter) const
{
	FMetasoundFrontendLiteral Literal;
	if (TryFindDefault(Literal))
	{
		Iter(Metasound::Frontend::DefaultPageID, MoveTemp(Literal));
	}
}

bool UMetasoundEditorGraphMemberDefaultLiteral::RemoveDefault(const FGuid& InPageID)
{
	return false;
}

void UMetasoundEditorGraphMemberDefaultLiteral::ResetDefaults()
{
}

void UMetasoundEditorGraphMemberDefaultLiteral::SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral, const FGuid& InPageID)
{
}

bool UMetasoundEditorGraphMemberDefaultLiteral::TryFindDefault(FMetasoundFrontendLiteral& OutLiteral, const FGuid* InPageID) const
{
	OutLiteral = { };
	return true;
}

#if WITH_EDITOR
void UMetasoundEditorGraphMemberDefaultLiteral::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (UMetasoundEditorGraphMember* Member = FindMember())
	{
		Metasound::Editor::GraphPrivate::OnLiteralChanged(*Member, nullptr, InPropertyChangedEvent.ChangeType);
	}
}

void UMetasoundEditorGraphMemberDefaultLiteral::PostEditChangeChainProperty(FPropertyChangedChainEvent& InPropertyChangedEvent)
{
	if (const FEditPropertyChain::TDoubleLinkedListNode* MemberNode = InPropertyChangedEvent.PropertyChain.GetActiveMemberNode())
	{
		if (const FProperty* ChildProperty = MemberNode->GetValue())
		{
			const FName ChildPropertyName = ChildProperty->GetFName();
			if (ChildPropertyName.IsEqual(GetDefaultsPropertyName()) || ChildPropertyName.IsEqual(GetDefaultPropertyName()))
			{
				ResolvePageDefaults();
				SortPageDefaults();
			}
		}
	}

	if (UMetasoundEditorGraphMember* Member = FindMember())
	{
		Metasound::Editor::GraphPrivate::OnLiteralChanged(*Member, nullptr, InPropertyChangedEvent.ChangeType);
	}
}

void UMetasoundEditorGraphMemberDefaultLiteral::PostEditUndo()
{
	using namespace Metasound::Editor;

	Super::PostEditUndo();

	if (!IsValid(this))
	{
		return;
	}

	constexpr bool bPostTransaction = false;
	if (UMetasoundEditorGraphMember* Member = FindMember())
	{
		Member->UpdateFrontendDefaultLiteral(bPostTransaction);
	}
}
#endif // WITH_EDITOR

bool UMetasoundEditorGraphMemberDefaultLiteral::TryGetPreviewPageID(FGuid& OutPreviewPageID) const
{
	using namespace Metasound::Engine;

	const UMetasoundEditorGraphMember* Member = FindMember();
	if (!ensure(Member))
	{
		OutPreviewPageID = Metasound::Frontend::DefaultPageID;
		return false;
	}

	const FMetaSoundFrontendDocumentBuilder& Builder = Member->GetFrontendBuilderChecked();
	if (const FMetasoundFrontendClassInput* Input = Builder.FindGraphInput(Member->GetMemberName()))
	{
		const FGuid PageID = FDocumentBuilderRegistry::GetChecked().ResolveTargetPageID(*Input);

		const UMetasoundEditorSettings* EdSettings = ::GetDefault<UMetasoundEditorSettings>();
		const UMetaSoundSettings* Settings = ::GetDefault<UMetaSoundSettings>();
		if (Settings && EdSettings)
		{
			if (const FMetaSoundPageSettings* PageSettings = Settings->FindPageSettings(EdSettings->AuditionPage))
			{
				if (PageID == PageSettings->UniqueId)
				{
					OutPreviewPageID = PageID;
					return true;
				}
			}
		}
	}

	OutPreviewPageID = Metasound::Frontend::DefaultPageID;
	return false;
}

Metasound::Editor::ENodeSection UMetasoundEditorGraphInput::GetSectionID() const 
{
	return Metasound::Editor::ENodeSection::Inputs;
}

Metasound::Frontend::FNodeHandle UMetasoundEditorGraphInput::AddNodeHandle(const FName& InName, const Metasound::Editor::FCreateNodeVertexParams& InParams)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* Graph = GetOwningGraph();
	if (!ensure(Graph))
	{
		return Metasound::Frontend::INodeController::GetInvalidHandle();
	}

	UObject& MetaSound = Graph->GetMetasoundChecked();
	FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&MetaSound);
	check(MetaSoundAsset);

	FMetasoundFrontendClassInput ClassInput = FGraphBuilder::CreateUniqueClassInput(MetaSound, InParams, { }, &InName);
	return MetaSoundAsset->GetRootGraphHandle()->AddInputVertex(ClassInput);
}

UMetasoundEditorGraphNode* UMetasoundEditorGraphInput::AddNode(Metasound::Frontend::FNodeHandle InNodeHandle, bool bInSelectNewNode)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Engine;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* Graph = GetOwningGraph();
	check(Graph);

	if (const FMetasoundFrontendNode* TemplateNode = FInputNodeTemplate::CreateNode(GetFrontendBuilderChecked(), GetMemberName()))
	{
		return FGraphBuilder::AddInputNode(Graph->GetMetasoundChecked(), TemplateNode->GetID(), bInSelectNewNode);
	}

	return nullptr;
}

const FText& UMetasoundEditorGraphInput::GetGraphMemberLabel() const
{
	static const FText Label = LOCTEXT("GraphMemberLabel_Input", "Input");
	return Label;
}

const FMetasoundFrontendClassVertex* UMetasoundEditorGraphInput::GetFrontendClassVertex() const
{
	return GetFrontendBuilderChecked().FindGraphInput(GetMemberName());
}

FText UMetasoundEditorGraphInput::GetDescription() const
{
	const FMetaSoundFrontendDocumentBuilder& Builder = GetFrontendBuilderChecked();
	if (const FMetasoundFrontendClassInput* Input = Builder.FindGraphInput(GetMemberName()))
	{
		return Input->Metadata.GetDescription();
	}

	return { };
}

int32 UMetasoundEditorGraphInput::GetSortOrderIndex() const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	const UMetasoundEditorGraph* MetaSoundGraph = GetOwningGraph();
	FConstGraphHandle GraphHandle = MetaSoundGraph->GetGraphHandle();
	FConstNodeHandle NodeHandle = GetConstNodeHandle();
	const Metasound::FVertexName& NodeName = NodeHandle->GetNodeName();
	return GraphHandle->GetSortOrderIndexForInput(NodeName);
}

TArray<UMetasoundEditorGraphMemberNode*> UMetasoundEditorGraphInput::GetNodes() const
{
	TArray<UMetasoundEditorGraphMemberNode*> Nodes;

	const UMetasoundEditorGraph* Graph = GetOwningGraph();
	if (ensure(Graph))
	{
		TArray<UMetasoundEditorGraphInputNode*> InputNodes;
		Graph->GetNodesOfClassEx<UMetasoundEditorGraphInputNode>(InputNodes);
		Algo::TransformIf(InputNodes, Nodes,
			[this](const UMetasoundEditorGraphMemberNode* Node)
			{
				return Node->GetMember() == this;
			},
			[](UMetasoundEditorGraphMemberNode* Node)
			{
				return Node;
			});
	}

	return Nodes;
}

bool UMetasoundEditorGraphInput::IsDefaultPaged() const
{
	// Triggers are special and do not show their default value, but are visible
	// to allow for interact button when auditioning.  Therefore, default paging
	// is unnecessary.
	return TypeName != Metasound::GetMetasoundDataTypeName<Metasound::FTrigger>();
}

bool UMetasoundEditorGraphInput::IsInterfaceMember(FMetasoundFrontendInterface* OutInterface) const
{
	FMetasoundFrontendInterface Interface;
	if (NameContainsInterfaceNamespace(&Interface))
	{
		// Is interface declared on this MetaSound 
		const UObject& MetaSoundObject = GetOwningGraph()->GetMetasoundChecked();
		const FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&MetaSoundObject);
		if (MetaSoundAsset && MetaSoundAsset->IsInterfaceDeclared(Interface.Version))
		{
			// Check if Input is a member of the found interface
			if (const FMetasoundFrontendNode* InputNode = GetFrontendNode())
			{
				const FMetasoundFrontendVertex& Input = InputNode->Interface.Inputs.Last();
				auto IsInput = [&Input](const FMetasoundFrontendClassInput& InterfaceInput)
				{
					return FMetasoundFrontendVertex::IsFunctionalEquivalent(Input, InterfaceInput);
				};

				if (Interface.Inputs.ContainsByPredicate(IsInput))
				{
					if (OutInterface)
					{
						*OutInterface = MoveTemp(Interface);
					}
					return true;
				}
			}
		}
	}

	if (OutInterface)
	{
		*OutInterface = { };
	}
	return false;
}

void UMetasoundEditorGraphInput::SetSortOrderIndex(int32 InSortOrderIndex)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* MetaSoundGraph = GetOwningGraph();
	check(MetaSoundGraph);

	FGraphHandle GraphHandle = MetaSoundGraph->GetGraphHandle();
	FConstNodeHandle NodeHandle = GetConstNodeHandle();
	const Metasound::FVertexName& NodeName = NodeHandle->GetNodeName();

	GraphHandle->SetSortOrderIndexForInput(NodeName, InSortOrderIndex);
	FGraphBuilder::GetOutermostMetaSoundChecked(*MetaSoundGraph).GetModifyContext().AddMemberIDsModified({ GetMemberID() });
}

void UMetasoundEditorGraphInput::ResetToClassDefault()
{
	using namespace Metasound;
	using namespace Metasound::Editor;

	if (ensure(Literal))
	{
		FMetaSoundFrontendDocumentBuilder& Builder = GetFrontendBuilderChecked();

		Builder.CastDocumentObjectChecked<UObject>().Modify();
		Literal->Modify();

		const FName MemberName = GetMemberName();
		Builder.ResetGraphInputDefault(MemberName);

		constexpr bool bPostTransaction = false;
		UpdateFrontendDefaultLiteral(bPostTransaction);

		GraphPrivate::UpdatePreviewParameter(GetOwningGraph(), MemberName, *Literal);
	}
}

void UMetasoundEditorGraphInput::SetDataType(FName InNewType, bool bPostTransaction)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (InNewType != GetDataType())
	{
		if (UMetasoundEditorGraph* Graph = GetOwningGraph())
		{
			UObject& MetaSound = Graph->GetMetasoundChecked();
			const FScopedTransaction Transaction(LOCTEXT("SetGraphInputData", "Set MetaSound Graph Input DataType"), bPostTransaction);
			MetaSound.Modify();
			Graph->Modify();
			Modify();

			FMetaSoundFrontendDocumentBuilder& Builder = GetFrontendBuilderChecked();
			const bool bSuccess = Builder.SetGraphInputDataType(GetMemberName(), InNewType);
			ensure(bSuccess);

			// Cached TypeName here must be set prior to re-initializing literal below
			TypeName = InNewType;

			if (const FMetasoundFrontendNode* Node = Builder.FindNode(NodeID); ensure(Node))
			{
				if (const FMetasoundFrontendClass* Dependency = Builder.FindDependency(Node->ClassID); ensure(Dependency))
				{
					ClassName = Dependency->Metadata.GetClassName();
				}
			}

			InitializeLiteral();

			FGraphBuilder::RegisterGraphWithFrontend(MetaSound);
		}
	}
}

void UMetasoundEditorGraphInput::SetVertexAccessType(EMetasoundFrontendVertexAccessType InNewAccessType, bool bPostTransaction)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (InNewAccessType != GetVertexAccessType())
	{
		if (UMetasoundEditorGraph* Graph = GetOwningGraph())
		{
			const FScopedTransaction Transaction(LOCTEXT("SetGraphInputVertexType", "Set MetaSound Graph Input Vertex Access Type"), bPostTransaction);
			Graph->GetMetasoundChecked().Modify();
			Graph->Modify();
			Modify();

			EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
			Graph->GetBuilderChecked().SetGraphInputAccessType(GetMemberName(), InNewAccessType, Result);
			ensure(Result == EMetaSoundBuilderResult::Succeeded);

			ClassName = GetConstNodeHandle()->GetClassMetadata().GetClassName();
			Graph->RegisterGraphWithFrontend();
		}
	}
}

void UMetasoundEditorGraphInput::SetMemberName(const FName& InNewName, bool bPostTransaction)
{
	// Renaming vertex members must stop the preview component to avoid confusion afterward
	// with newly named input not passing updated values to active previewed instance.
	if (UMetasoundEditorGraph* Graph = GetOwningGraph())
	{
		if (GEditor && Graph->IsPreviewing())
		{
			GEditor->ResetPreviewAudioComponent();
		}
	}

	Super::SetMemberName(InNewName, bPostTransaction);
}

bool UMetasoundEditorGraphInput::RenameFrontendMemberInternal(FMetaSoundFrontendDocumentBuilder& Builder, FName OldName, FName InNewName) const
{
	return Builder.SetGraphInputName(OldName, InNewName);
}

bool UMetasoundEditorGraphInput::Synchronize()
{
	bool bModified = Super::Synchronize();

	if (ensure(Literal))
	{
		bModified |= Literal->Synchronize();
	}

	return bModified;
}

void UMetasoundEditorGraphInput::UpdateFrontendDefaultLiteral(bool bPostTransaction, const FGuid* InPageID)
{
	using namespace Metasound::Editor;

	if (Literal)
	{
		FMetaSoundFrontendDocumentBuilder& Builder = GetFrontendBuilderChecked();

		const FScopedTransaction Transaction(FText::Format(LOCTEXT("Set Input Defaults", "Set MetaSound Input '{0}' Default(s)"), GetDisplayName()), bPostTransaction);
		Builder.CastDocumentObjectChecked<UObject>().Modify();
		Literal->Modify();

		const FName MemberName = GetMemberName();
		if (InPageID)
		{
			FMetasoundFrontendLiteral Default;
			if (Literal->TryFindDefault(Default, InPageID))
			{
				Builder.SetGraphInputDefault(MemberName, Default, InPageID);
			}
		}
		else
		{
			TArray<FMetasoundFrontendClassInputDefault> NewDefaults;
			Literal->IterateDefaults([&NewDefaults](const FGuid& PageID, FMetasoundFrontendLiteral Default)
			{
				NewDefaults.Add(FMetasoundFrontendClassInputDefault(PageID, MoveTemp(Default)));
			});
			Builder.SetGraphInputDefaults(MemberName, MoveTemp(NewDefaults));
		}

		GraphPrivate::UpdatePreviewParameter(GetOwningGraph(), MemberName, *Literal);
	}
}

EMetasoundFrontendVertexAccessType UMetasoundEditorGraphInput::GetVertexAccessType() const
{
	const FName MemberName = GetMemberName();
	const FMetaSoundFrontendDocumentBuilder& Builder = GetFrontendBuilderChecked();
	if (const FMetasoundFrontendClassInput* Input = Builder.FindGraphInput(MemberName))
	{
		return Input->AccessType;
	}

	return EMetasoundFrontendVertexAccessType::Reference;
}

Metasound::Frontend::FNodeHandle UMetasoundEditorGraphOutput::AddNodeHandle(const FName& InName, const Metasound::Editor::FCreateNodeVertexParams& InParams)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* Graph = GetOwningGraph();
	if (!ensure(Graph))
	{
		return Metasound::Frontend::INodeController::GetInvalidHandle();
	} 

	UObject& MetaSound = Graph->GetMetasoundChecked();

	FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&MetaSound);
	check(MetaSoundAsset);

	FMetasoundFrontendClassOutput ClassOutput = FGraphBuilder::CreateUniqueClassOutput(MetaSound, InParams, &InName);
	return MetaSoundAsset->GetRootGraphHandle()->AddOutputVertex(ClassOutput);
}

UMetasoundEditorGraphNode* UMetasoundEditorGraphOutput::AddNode(Metasound::Frontend::FNodeHandle InNodeHandle, bool bInSelectNewNode)
{
	using namespace Metasound::Editor;
	UMetasoundEditorGraph* Graph = GetOwningGraph();
	check(Graph);
	return FGraphBuilder::AddOutputNode(Graph->GetMetasoundChecked(), InNodeHandle->GetID(), bInSelectNewNode);
}

FText UMetasoundEditorGraphOutput::GetDescription() const
{
	const FMetaSoundFrontendDocumentBuilder& Builder = GetFrontendBuilderChecked();
	if (const FMetasoundFrontendClassOutput* Output = Builder.FindGraphOutput(GetMemberName()))
	{
		return Output->Metadata.GetDescription();
	}

	return { };
}

const FMetasoundFrontendClassVertex* UMetasoundEditorGraphOutput::GetFrontendClassVertex() const
{
	return GetFrontendBuilderChecked().FindGraphOutput(GetMemberName());
}

int32 UMetasoundEditorGraphOutput::GetSortOrderIndex() const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	const UMetasoundEditorGraph* MetaSoundGraph = GetOwningGraph();
	FConstGraphHandle GraphHandle = MetaSoundGraph->GetGraphHandle();
	FConstNodeHandle NodeHandle = GetConstNodeHandle();
	const Metasound::FVertexName& NodeName = NodeHandle->GetNodeName();
	return GraphHandle->GetSortOrderIndexForOutput(NodeName);
}

bool UMetasoundEditorGraphOutput::IsInterfaceMember(FMetasoundFrontendInterface* OutInterface) const
{
	using namespace Metasound::Frontend;
	
	FMetasoundFrontendInterface Interface;
	if (NameContainsInterfaceNamespace(&Interface))
	{
		// Is interface declared on this MetaSound 
		const UObject& MetaSoundObject = GetOwningGraph()->GetMetasoundChecked();
		const FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&MetaSoundObject);
		if (MetaSoundAsset && MetaSoundAsset->IsInterfaceDeclared(Interface.Version))
		{
			// Check if Output is a member of the found interface
			if (const FMetasoundFrontendNode* OutputNode = GetFrontendNode())
			{
				const FMetasoundFrontendVertex& Output = OutputNode->Interface.Outputs.Last();
				auto IsOutput = [&Output](const FMetasoundFrontendClassOutput& InterfaceOutput)
				{
					return FMetasoundFrontendVertex::IsFunctionalEquivalent(Output, InterfaceOutput);
				};

				if (Interface.Outputs.ContainsByPredicate(IsOutput))
				{
					if (OutInterface)
					{
						*OutInterface = MoveTemp(Interface);
					}
					return true;
				}
			}
		}
	}
	
	if (OutInterface)
	{
		*OutInterface = { };
	}
	return false;
}

void UMetasoundEditorGraphOutput::SetSortOrderIndex(int32 InSortOrderIndex)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* MetaSoundGraph = GetOwningGraph();
	check(MetaSoundGraph);

	FGraphHandle GraphHandle = MetaSoundGraph->GetGraphHandle();
	FConstNodeHandle NodeHandle = GetConstNodeHandle();
	const Metasound::FVertexName& NodeName = NodeHandle->GetNodeName();

	GraphHandle->SetSortOrderIndexForOutput(NodeName, InSortOrderIndex);
	FGraphBuilder::GetOutermostMetaSoundChecked(*MetaSoundGraph).GetModifyContext().AddMemberIDsModified({ GetMemberID() });
}

const FText& UMetasoundEditorGraphOutput::GetGraphMemberLabel() const
{
	static const FText Label = LOCTEXT("GraphMemberLabel_Output", "Output");
	return Label;
}

void UMetasoundEditorGraphOutput::ResetToClassDefault()
{
	using namespace Metasound;
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	FMetaSoundFrontendDocumentBuilder& Builder = GetFrontendBuilderChecked();
	TArray<UMetasoundEditorGraphMemberNode*> Nodes = GetNodes();
	for (UMetasoundEditorGraphMemberNode* Node : Nodes)
	{
		TArray<const FMetasoundFrontendVertex*> InputVertices = Builder.FindNodeInputs(Node->GetNodeID());
		if (ensure(InputVertices.Num() == 1))
		{
			Builder.RemoveNodeInputDefault(Node->GetNodeID(), InputVertices.Last()->VertexID);
		}
	}
}

void UMetasoundEditorGraphOutput::SetDataType(FName InNewType, bool bPostTransaction)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (InNewType != GetDataType())
	{
		if (UMetasoundEditorGraph* Graph = GetOwningGraph())
		{
			const FScopedTransaction Transaction(LOCTEXT("SetGraphOutputData", "Set MetaSound Graph Output DataType"), bPostTransaction);
			Graph->GetMetasoundChecked().Modify();
			Graph->Modify();
			Modify();

			EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
			Graph->GetBuilderChecked().SetGraphOutputDataType(GetMemberName(), InNewType, Result);
			ensure(Result == EMetaSoundBuilderResult::Succeeded);

			// Cached TypeName here must be set prior to re-initializing literal below
			TypeName = InNewType;
			ClassName = GetConstNodeHandle()->GetClassMetadata().GetClassName();

			InitializeLiteral();

			Graph->RegisterGraphWithFrontend();
		}
	}
}

void UMetasoundEditorGraphOutput::SetVertexAccessType(EMetasoundFrontendVertexAccessType InNewAccessType, bool bPostTransaction)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (InNewAccessType != GetVertexAccessType())
	{
		UMetasoundEditorGraph* Graph = GetOwningGraph();
		if (ensure(Graph))
		{
			const FScopedTransaction Transaction(LOCTEXT("SetGraphOutputAccessType", "Set MetaSound Graph Output Access Type"), bPostTransaction);
			Graph->GetMetasoundChecked().Modify();
			Graph->Modify();
			Modify();

			EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
			Graph->GetBuilderChecked().SetGraphOutputAccessType(GetMemberName(), InNewAccessType, Result);
			ensure(Result == EMetaSoundBuilderResult::Succeeded);

			ClassName = GetConstNodeHandle()->GetClassMetadata().GetClassName();
			Graph->RegisterGraphWithFrontend();
		}
	}
}

void UMetasoundEditorGraphOutput::UpdateFrontendDefaultLiteral(bool bPostTransaction, const FGuid* InPageID)
{
	using namespace Metasound;
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UObject* Metasound = nullptr;
	UMetasoundEditorGraph* MetaSoundGraph = GetOwningGraph();
	if (ensure(MetaSoundGraph))
	{
		Metasound = MetaSoundGraph->GetMetasound();
	}

	if (!ensure(Metasound))
	{
		return;
	}

	if (!ensure(Literal))
	{
		return;
	}

	// Use the default page ID here as output defaults do *not* support paged defaults (they exist per paged graph on the singleton output node)
	FMetasoundFrontendLiteral DefaultLiteral;
	if (ensure(Literal->TryFindDefault(DefaultLiteral)))
	{
		const FScopedTransaction Transaction(FText::Format(LOCTEXT("Set Output Default", "Set MetaSound Output '{0}' Default"), GetDisplayName()), bPostTransaction);
		Metasound->Modify();

		if (const FMetasoundFrontendNode* FrontendNode = GetFrontendNode())
		{
			FMetaSoundFrontendDocumentBuilder& Builder = GetFrontendBuilderChecked();
			const FGuid& VertexID = FrontendNode->Interface.Inputs.Last().VertexID;
			const FMetasoundFrontendVertexHandle VertexHandle { NodeID, VertexID };
			GraphPrivate::SetOrClearIfLiteralMatchesNodeVertexDefault(Builder, VertexHandle, DefaultLiteral);
		}
	}
}

EMetasoundFrontendVertexAccessType UMetasoundEditorGraphOutput::GetVertexAccessType() const
{
	const FName MemberName = GetMemberName();
	const FMetaSoundFrontendDocumentBuilder& Builder = GetFrontendBuilderChecked();
	if (const FMetasoundFrontendClassOutput* Output = Builder.FindGraphOutput(MemberName))
	{
		return Output->AccessType;
	}

	return EMetasoundFrontendVertexAccessType::Reference;
}

Metasound::Editor::ENodeSection UMetasoundEditorGraphOutput::GetSectionID() const 
{
	return Metasound::Editor::ENodeSection::Outputs;
}

bool UMetasoundEditorGraphOutput::Synchronize()
{
	using namespace Metasound;

	bool bModified = Super::Synchronize();

	FMetaSoundFrontendDocumentBuilder& Builder = GetFrontendBuilderChecked();
	const FName MemberName = GetMemberName();
	if (const FMetasoundFrontendClassOutput* ClassOutput = Builder.FindGraphOutput(MemberName))
	{
		if (ensure(Literal))
		{
			TOptional<FMetasoundFrontendLiteral> NewDefault;
			if (const FMetasoundFrontendNode* OutputNode = Builder.FindGraphOutputNode(GetMemberName()); ensure(OutputNode))
			{
				FMetasoundFrontendLiteral DefaultLiteral;
				Literal->TryFindDefault(DefaultLiteral);
				if (!OutputNode->InputLiterals.IsEmpty())
				{
					const FMetasoundFrontendVertexLiteral& VertexLiteral = OutputNode->InputLiterals.Last();
					if (!VertexLiteral.Value.IsEqual(DefaultLiteral))
					{
						NewDefault = VertexLiteral.Value;
					}
				}
				else
				{
					FMetasoundFrontendLiteral TypeDefault;
					TypeDefault.SetFromLiteral(Frontend::IDataTypeRegistry::Get().CreateDefaultLiteral(TypeName));
					if (!TypeDefault.IsEqual(DefaultLiteral))
					{
						NewDefault = TypeDefault;
					}
				}
			}

			if (NewDefault.IsSet())
			{
				bModified = true;
				Literal->ResetDefaults();
				Literal->SetFromLiteral(*NewDefault);
			}
		}
	}

	return bModified;
}

bool UMetasoundEditorGraphOutput::RenameFrontendMemberInternal(FMetaSoundFrontendDocumentBuilder& Builder, FName OldName, FName InNewName) const
{
	return Builder.SetGraphOutputName(OldName, InNewName);
}

void UMetasoundEditorGraphVariable::InitMember(FName InDataType, const FMetasoundFrontendLiteral& InDefaultLiteral, FGuid InVariableID)
{
	TypeName = InDataType;
	VariableID = InVariableID;

	InitializeLiteral();

	if (ensure(Literal))
	{
		Literal->SetFromLiteral(InDefaultLiteral);
	}
}

const FText& UMetasoundEditorGraphVariable::GetGraphMemberLabel() const
{
	static const FText Label = LOCTEXT("GraphMemberLabel_Variable", "Variable");
	return Label;
}

Metasound::Frontend::FVariableHandle UMetasoundEditorGraphVariable::GetVariableHandle()
{
	using namespace Metasound;

	FMetasoundAssetBase& MetasoundAsset = Editor::FGraphBuilder::GetOutermostMetaSoundChecked(*this);
	return MetasoundAsset.GetRootGraphHandle()->FindVariable(VariableID);
}

Metasound::Frontend::FConstVariableHandle UMetasoundEditorGraphVariable::GetConstVariableHandle() const
{
	using namespace Metasound;

	const FMetasoundAssetBase& MetaSound = Editor::FGraphBuilder::GetOutermostConstMetaSoundChecked(*this);
	return MetaSound.GetRootGraphHandle()->FindVariable(VariableID);
}

UMetasoundEditorGraphNode* UMetasoundEditorGraphVariable::AddNode(Metasound::Frontend::FNodeHandle InNodeHandle, bool bInSelectNewNode)
{
	using namespace Metasound::Editor;

	FMetasoundAssetBase& MetaSound = FGraphBuilder::GetOutermostMetaSoundChecked(*this);
	return FGraphBuilder::AddVariableNode(*MetaSound.GetOwningAsset(), InNodeHandle, bInSelectNewNode);
}

void UMetasoundEditorGraphVariable::SetMemberName(const FName& InNewName, bool bPostTransaction)
{
	using namespace Metasound::Editor;

	UMetasoundEditorGraph* Graph = GetOwningGraph();
	if (!ensure(Graph))
	{
		return;
	}

	const FText TransactionLabel = FText::Format(LOCTEXT("RenameGraphVariableMemberNameFormat", "Set Metasound {0} Name"), GetGraphMemberLabel());
	const FScopedTransaction Transaction(TransactionLabel, bPostTransaction);

	Graph->Modify();
	Graph->GetMetasoundChecked().Modify();

	GetVariableHandle()->SetName(InNewName);

	Graph->RegisterGraphWithFrontend();
	FGraphBuilder::GetOutermostMetaSoundChecked(*this).GetModifyContext().AddMemberIDsModified({ GetMemberID() });
}

FGuid UMetasoundEditorGraphVariable::GetMemberID() const 
{ 
	return VariableID;
}

FName UMetasoundEditorGraphVariable::GetMemberName() const
{
	return GetConstVariableHandle()->GetName();
}

Metasound::Editor::ENodeSection UMetasoundEditorGraphVariable::GetSectionID() const 
{ 
	return Metasound::Editor::ENodeSection::Variables;
}

FText UMetasoundEditorGraphVariable::GetDescription() const
{
	return GetConstVariableHandle()->GetDescription();
}

void UMetasoundEditorGraphVariable::SetDescription(const FText& InDescription, bool bPostTransaction)
{
	const FText TransactionLabel = FText::Format(LOCTEXT("SetGraphVariableTooltipFormat", "Set MetaSound {0}'s ToolTip"), GetGraphMemberLabel());
	const FScopedTransaction Transaction(TransactionLabel, bPostTransaction);

	if (UMetasoundEditorGraph* Graph = GetOwningGraph())
	{
		Graph->Modify();
		UObject& MetaSound = Graph->GetMetasoundChecked();
		MetaSound.Modify();

		GetVariableHandle()->SetDescription(InDescription);
	}
}

bool UMetasoundEditorGraphVariable::CanRename() const
{
	return true;
}

bool UMetasoundEditorGraphVariable::CanRename(const FText& InNewText, FText& OutError) const
{
	using namespace Metasound::Frontend;

	if (InNewText.IsEmptyOrWhitespace())
	{
		OutError = FText::Format(LOCTEXT("GraphVariableRenameInvalid_NameEmpty", "{0} cannot be empty string."), InNewText);
		return false;
	}
	
	const FString NewNameString = InNewText.ToString();
	if (!FName::IsValidXName(NewNameString, INVALID_PARAMETER_NAME_CHARACTERS, &OutError))
	{
		return false;
	}

	const FName NewName(*NewNameString);
	FName Namespace;
	FName ParameterName;
	Audio::FParameterPath::SplitName(NewName, Namespace, ParameterName);

	FConstVariableHandle VariableHandle = GetConstVariableHandle();
	TArray<FConstVariableHandle> Variables = VariableHandle->GetOwningGraph()->GetVariables();
	for (const FConstVariableHandle& OtherVariable : Variables)
	{
		if (VariableID != OtherVariable->GetID())
		{
			FName OtherName = OtherVariable->GetName();
			if (NewName == OtherName)
			{
				OutError = FText::Format(LOCTEXT("GraphVariableRenameInvalid_NameTaken", "{0} is already in use"), InNewText);
				return false;
			}

			if (Namespace == OtherName)
			{
				OutError = FText::Format(LOCTEXT("GraphVariableRenameInvalid_NamespaceTaken", "Namespace of '{0}' cannot be the same as an existing variable's name"), InNewText);
				return false;
			}

			FName OtherNamespace;
			Audio::FParameterPath::SplitName(OtherName, OtherNamespace, OtherName);
			if (OtherNamespace == NewName)
			{
				OutError = FText::Format(LOCTEXT("GraphVariableRenameInvalid_NamespaceTaken2", "Name of '{0}' cannot be the same as an existing variable's namespace"), InNewText);
				return false;
			}
		}
	}

	return true;
}

TArray<UMetasoundEditorGraphMemberNode*> UMetasoundEditorGraphVariable::GetNodes() const
{
	TArray<UMetasoundEditorGraphMemberNode*> Nodes;

	FVariableEditorNodes EditorNodes = GetVariableNodes();
	if (nullptr != EditorNodes.MutatorNode)
	{
		Nodes.Add(EditorNodes.MutatorNode);
	}
	Nodes.Append(EditorNodes.AccessorNodes);
	Nodes.Append(EditorNodes.DeferredAccessorNodes);

	return Nodes;
}

FText UMetasoundEditorGraphVariable::GetDisplayName() const
{
	return Metasound::Editor::FGraphBuilder::GetDisplayName(*GetConstVariableHandle());
}

void UMetasoundEditorGraphVariable::SetDisplayName(const FText& InNewName, bool bPostTransaction)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* Graph = GetOwningGraph();
	if (!ensure(Graph))
	{
		return;
	}

	const FText TransactionLabel = FText::Format(LOCTEXT("RenameGraphVariableDisplayNameFormat", "Set Metasound {0} DisplayName"), GetGraphMemberLabel());
	const FScopedTransaction Transaction(TransactionLabel, bPostTransaction);
	{
		Graph->Modify();
		Graph->GetMetasoundChecked().Modify();
	}

	FVariableHandle VariableHandle = GetVariableHandle();
	VariableHandle->SetDisplayName(InNewName);

	FGraphBuilder::GetOutermostMetaSoundChecked(*this).GetModifyContext().AddMemberIDsModified({ GetMemberID() });
}

void UMetasoundEditorGraphVariable::SetDataType(FName InNewType, bool bPostTransaction)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (InNewType == GetDataType())
	{
		return;
	}

	UMetasoundEditorGraph* Graph = GetOwningGraph();
	if (!ensure(Graph))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetGraphVariableType", "Set MetaSound GraphVariable Type"), bPostTransaction);
	{
		Graph->GetMetasoundChecked().Modify();
		Graph->Modify();
		Modify();

		// Changing the data type requires that the variable and the associated nodes
		// be removed and readded. Before removing, cache required info to be set after
		// readding. It is assumed that connections are discarded because connections
		// require data types to be equal between to TO and FROM pin. 
		struct FCachedData
		{
			FName MemberName;
			FText DisplayName;
			FText Description;
			FVariableNodeLocations Locations;
		} CachedData;

		FConstVariableHandle OrigVariable = GetConstVariableHandle();

		// Cache variable metadata
		CachedData.MemberName = OrigVariable->GetName();
		CachedData.DisplayName = OrigVariable->GetDisplayName();
		CachedData.Description = OrigVariable->GetDescription();
		CachedData.Locations = GetVariableNodeLocations();


		// Remove the current variable
		FGraphBuilder::DeleteMemberNodes(*this);
		FGraphHandle FrontendGraph = Graph->GetGraphHandle();
		FrontendGraph->RemoveVariable(VariableID);
		VariableID = FGuid();

		// Add variable with new type to frontend
		FVariableHandle FrontendVariable = FrontendGraph->AddVariable(InNewType);

		if (!ensure(FrontendVariable->IsValid()))
		{
			// Failed to add a new variable with the given data type. 
			return;
		}

		// Setup this object with new variable data
		VariableID = FrontendVariable->GetID();

		constexpr bool bPostSubTransaction = false;
		SetMemberName(CachedData.MemberName, bPostSubTransaction);
		SetDisplayName(CachedData.DisplayName, bPostSubTransaction);
		SetDescription(CachedData.Description, bPostSubTransaction);

		TypeName = InNewType;
		InitializeLiteral();

		{
			FMetasoundFrontendLiteral DefaultLiteral;
			DefaultLiteral.SetFromLiteral(IDataTypeRegistry::Get().CreateDefaultLiteral(GetDataType()));
			check(Literal); // Should exist from prior InitializeLiteral() call
			Literal->SetFromLiteral(DefaultLiteral);
			Literal->MemberID = VariableID;
		}

		// Add the nodes with the same identifier data but new datatype.
		UObject& Metasound = Graph->GetMetasoundChecked();
		AddVariableNodes(Metasound, FrontendGraph, CachedData.Locations);
	}

}

UMetasoundEditorGraphVariable::FVariableEditorNodes UMetasoundEditorGraphVariable::GetVariableNodes() const
{
	using namespace Metasound::Frontend;

	FVariableEditorNodes VariableNodes;
	TArray<UMetasoundEditorGraphMemberNode*> AllMetasoundNodes;

	const UMetasoundEditorGraph* Graph = GetOwningGraph();
	if (ensure(Graph))
	{
		Graph->GetNodesOfClassEx<UMetasoundEditorGraphMemberNode>(AllMetasoundNodes);
		FConstVariableHandle FrontendVariable = GetConstVariableHandle();

		// Find the mutator node if it exists.
		{
			FConstNodeHandle FrontendMutatorNode = FrontendVariable->FindMutatorNode();
			if (FrontendMutatorNode->IsValid())
			{
				const FGuid& MutatorNodeID = FrontendMutatorNode->GetID();
				auto IsNodeWithID = [&MutatorNodeID](const UMetasoundEditorGraphMemberNode* InNode)
				{
					return (nullptr != InNode) && (MutatorNodeID == InNode->GetNodeID());
				};

				if (UMetasoundEditorGraphMemberNode** FoundMutatorNode = AllMetasoundNodes.FindByPredicate(IsNodeWithID))
				{
					VariableNodes.MutatorNode = *FoundMutatorNode;
				}
			}
		}

		// Find all accessor nodes
		{
			TSet<FGuid> AccessorNodeIDs;
			for (const FConstNodeHandle& FrontendAccessorNode : FrontendVariable->FindAccessorNodes())
			{
				AccessorNodeIDs.Add(FrontendAccessorNode->GetID());
			}
			auto IsNodeInAccessorSet = [&AccessorNodeIDs](const UMetasoundEditorGraphMemberNode* InNode)
			{
				return (nullptr != InNode) && AccessorNodeIDs.Contains(InNode->GetNodeID());
			};
			VariableNodes.AccessorNodes = AllMetasoundNodes.FilterByPredicate(IsNodeInAccessorSet);
		}

		// Find all deferred accessor nodes
		{
			TSet<FGuid> DeferredAccessorNodeIDs;
			for (const FConstNodeHandle& FrontendAccessorNode : FrontendVariable->FindDeferredAccessorNodes())
			{
				DeferredAccessorNodeIDs.Add(FrontendAccessorNode->GetID());
			}
			auto IsNodeInDeferredAccessorSet = [&DeferredAccessorNodeIDs](const UMetasoundEditorGraphMemberNode* InNode)
			{
				return (nullptr != InNode) && DeferredAccessorNodeIDs.Contains(InNode->GetNodeID());
			};
			VariableNodes.DeferredAccessorNodes = AllMetasoundNodes.FilterByPredicate(IsNodeInDeferredAccessorSet);
		}
	}

	return VariableNodes;

}

UMetasoundEditorGraphVariable::FVariableNodeLocations UMetasoundEditorGraphVariable::GetVariableNodeLocations() const
{
	FVariableNodeLocations Locations;
	// Cache current node positions 
	FVariableEditorNodes EditorNodes = GetVariableNodes();
	auto GetNodeLocation = [](const UMetasoundEditorGraphMemberNode* InNode) { return FVector2D(InNode->NodePosX, InNode->NodePosY); };

	if (nullptr != EditorNodes.MutatorNode)
	{
		Locations.MutatorLocation = GetNodeLocation(EditorNodes.MutatorNode);
	}
	Algo::Transform(EditorNodes.AccessorNodes, Locations.AccessorLocations, GetNodeLocation);
	Algo::Transform(EditorNodes.DeferredAccessorNodes, Locations.DeferredAccessorLocations, GetNodeLocation);

	return Locations;
}

void UMetasoundEditorGraphVariable::AddVariableNodes(UObject& InMetasound, Metasound::Frontend::FGraphHandle& InFrontendGraph, const FVariableNodeLocations& InNodeLocs)
{
	using namespace Metasound::Frontend;
	using namespace Metasound::Editor;

	if (InNodeLocs.MutatorLocation)
	{
		bool bMutatorNodeAlreadyExists = GetConstVariableHandle()->FindMutatorNode()->IsValid();
		if (ensure(!bMutatorNodeAlreadyExists))
		{
			FConstNodeHandle MutatorFrontendNode = InFrontendGraph->FindOrAddVariableMutatorNode(VariableID);
			UMetasoundEditorGraphNode* NewGraphNode = FGraphBuilder::AddVariableNode(InMetasound, MutatorFrontendNode, false /* bInSelectNewNode */);
			NewGraphNode->UpdateFrontendNodeLocation(*InNodeLocs.MutatorLocation);
			NewGraphNode->SyncLocationFromFrontendNode();
		}
	}

	for (const FVector2D& Location : InNodeLocs.AccessorLocations)
	{
		FConstNodeHandle AccessorFrontendNode = InFrontendGraph->AddVariableAccessorNode(VariableID);
		UMetasoundEditorGraphNode* NewGraphNode = FGraphBuilder::AddVariableNode(InMetasound, AccessorFrontendNode, false /* bInSelectNewNode */);
		NewGraphNode->UpdateFrontendNodeLocation(Location);
		NewGraphNode->SyncLocationFromFrontendNode();
	}

	for (const FVector2D& Location : InNodeLocs.DeferredAccessorLocations)
	{
		FConstNodeHandle DeferredAccessorFrontendNode = InFrontendGraph->AddVariableDeferredAccessorNode(VariableID);
		UMetasoundEditorGraphNode* NewGraphNode = FGraphBuilder::AddVariableNode(InMetasound, DeferredAccessorFrontendNode, false /* bInSelectNewNode */);
		NewGraphNode->UpdateFrontendNodeLocation(Location);
		NewGraphNode->SyncLocationFromFrontendNode();
	}
}

const FGuid& UMetasoundEditorGraphVariable::GetVariableID() const
{
	return VariableID;
}

void UMetasoundEditorGraphVariable::ResetToClassDefault()
{
	using namespace Metasound;
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;
	using namespace Metasound::VariableNames;

	FMetasoundFrontendLiteral DefaultLiteral;
	DefaultLiteral.SetFromLiteral(IDataTypeRegistry::Get().CreateDefaultLiteral(GetDataType()));

	Literal->Modify();
	Literal->SetFromLiteral(DefaultLiteral);

	FVariableHandle VariableHandle = GetVariableHandle();
	VariableHandle->SetLiteral(DefaultLiteral);

	FNodeHandle MutatorNode = VariableHandle->FindMutatorNode();
	if (MutatorNode->IsValid())
	{
		FInputHandle InputHandle = MutatorNode->GetInputWithVertexName(METASOUND_GET_PARAM_NAME(InputData));
		if (ensure(InputHandle->IsValid()))
		{
			InputHandle->ClearLiteral();
		}
	}

	FGraphBuilder::GetOutermostMetaSoundChecked(*this).GetModifyContext().AddMemberIDsModified({ GetMemberID() });
}

bool UMetasoundEditorGraphVariable::Synchronize()
{
	using namespace Metasound;

	bool bModified = Super::Synchronize();
	FMetaSoundFrontendDocumentBuilder& Builder = GetFrontendBuilderChecked();
	const FMetasoundFrontendGraph& Graph = Builder.FindConstBuildGraphChecked();
	const FName MemberName = GetMemberName();
	if (const FMetasoundFrontendVariable* Variable = Builder.FindGraphVariable(MemberName))
	{
		if (TypeName != Variable->TypeName)
		{
			bModified = true;
			TypeName = Variable->TypeName;

			InitializeLiteral();
		}

		if (ensure(Literal))
		{
			TOptional<FMetasoundFrontendLiteral> NewDefault;
			FMetasoundFrontendLiteral DefaultLiteral;
			Literal->TryFindDefault(DefaultLiteral);
			if (const FMetasoundFrontendNode* MutatorNode = Builder.FindNode(Variable->MutatorNodeID))
			{
				if (!MutatorNode->InputLiterals.IsEmpty())
				{
					const FMetasoundFrontendVertexLiteral& VertexLiteral = MutatorNode->InputLiterals.Last();
					if (!VertexLiteral.Value.IsEqual(DefaultLiteral))
					{
						NewDefault = VertexLiteral.Value;
					}
				}
				else
				{
					FMetasoundFrontendLiteral TypeDefault;
					TypeDefault.SetFromLiteral(Frontend::IDataTypeRegistry::Get().CreateDefaultLiteral(TypeName));
					if (!TypeDefault.IsEqual(DefaultLiteral))
					{
						NewDefault = TypeDefault;
					}
				}
			}
			else if (!Variable->Literal.IsEqual(DefaultLiteral))
			{
				NewDefault = Variable->Literal;
			}

			if (NewDefault.IsSet())
			{
				bModified = true;
				Literal->ResetDefaults();
				Literal->SetFromLiteral(*NewDefault);
			}
		}
	}

	return bModified;
}

void UMetasoundEditorGraphVariable::UpdateFrontendDefaultLiteral(bool bPostTransaction, const FGuid* InPageID)
{
	using namespace Metasound;
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;
	using namespace Metasound::VariableNames;

	if (!ensure(Literal))
	{
		return;
	}

	FMetaSoundFrontendDocumentBuilder& Builder = GetFrontendBuilderChecked();
	const FMetasoundFrontendVariable* Variable = Builder.FindGraphVariable(GetMemberName());
	if (!ensure(Variable))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("Set Variable Default", "Set MetaSound Variable Default"), bPostTransaction);
	Builder.CastDocumentObjectChecked<UObject>().Modify();

	// Use the default page ID here as variables do *not* support paged defaults
	// (they, as well as their mutator node which has a matching default, exist in each paged graph).
	FMetasoundFrontendLiteral DefaultLiteral;
	Literal->TryFindDefault(DefaultLiteral);

	// Page ID is passed along to the builder from here because the builder needs the current BuildPageID to access the appropriate in-graph variable
	// (variables can have the same IDs/names in different paged graphs).
	Builder.SetGraphVariableDefault(GetMemberName(), DefaultLiteral, InPageID);

	if (const FMetasoundFrontendNode* MutatorNode = Builder.FindNode(Variable->MutatorNodeID, InPageID))
	{
		const FMetasoundFrontendVertex* Input = MutatorNode->Interface.Inputs.FindByPredicate([](const FMetasoundFrontendVertex& Vertex) { return Vertex.Name == METASOUND_GET_PARAM_NAME(InputData); });
		if (ensure(Input))
		{
			const FMetasoundFrontendVertexHandle VertexHandle { MutatorNode->GetID(), Input->VertexID };
			GraphPrivate::SetOrClearIfLiteralMatchesNodeVertexDefault(Builder, VertexHandle, DefaultLiteral);
		}
	}
}

UMetasoundEditorGraphInputNode* UMetasoundEditorGraph::CreateInputNode(Metasound::Frontend::FNodeHandle InNodeHandle, bool bInSelectNewNode)
{
	checkNoEntry();
	return nullptr;
}

Metasound::Frontend::FDocumentHandle UMetasoundEditorGraph::GetDocumentHandle()
{
	return GetGraphHandle()->GetOwningDocument();
}

Metasound::Frontend::FConstDocumentHandle UMetasoundEditorGraph::GetDocumentHandle() const
{
	return GetGraphHandle()->GetOwningDocument();
}

Metasound::Frontend::FGraphHandle UMetasoundEditorGraph::GetGraphHandle() 
{
	FMetasoundAssetBase* MetasoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&GetMetasoundChecked());
	check(MetasoundAsset);

	return MetasoundAsset->GetRootGraphHandle();
}

Metasound::Frontend::FConstGraphHandle UMetasoundEditorGraph::GetGraphHandle() const
{
	const FMetasoundAssetBase* MetasoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&GetMetasoundChecked());
	check(MetasoundAsset);

	return MetasoundAsset->GetRootGraphHandle();
}

void UMetasoundEditorGraph::PreSave(FObjectPreSaveContext InSaveContext)
{
	using namespace Metasound::Frontend;

	TArray<UMetasoundEditorGraphNode*> MetaSoundNodes;
	GetNodesOfClass<UMetasoundEditorGraphNode>(MetaSoundNodes);
	for (UMetasoundEditorGraphNode* Node : MetaSoundNodes)
	{
		FNodeHandle NodeHandle = Node->GetNodeHandle();
		FMetasoundFrontendNodeStyle Style = NodeHandle->GetNodeStyle();
		Style.bMessageNodeUpdated = false;
		NodeHandle->SetNodeStyle(Style);
	}

	Super::PreSave(InSaveContext);
}

UMetaSoundBuilderBase& UMetasoundEditorGraph::GetBuilderChecked() const
{
	using namespace Metasound::Engine;
	return FDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(GetMetasoundChecked());
}

UObject* UMetasoundEditorGraph::GetMetasound() const
{
	return GetOutermostObject();
}

UObject& UMetasoundEditorGraph::GetMetasoundChecked() const
{
	UObject* ParentMetasound = GetMetasound();
	check(ParentMetasound);
	return *ParentMetasound;
}

void UMetasoundEditorGraph::RegisterGraphWithFrontend()
{
	using namespace Metasound::Editor;

	if (UObject* ParentMetasound = GetOutermostObject())
	{
		FGraphBuilder::RegisterGraphWithFrontend(*ParentMetasound);
	}
}

UMetasoundEditorGraphInput* UMetasoundEditorGraph::FindInput(FGuid InNodeID) const
{
	const TObjectPtr<UMetasoundEditorGraphInput>* Input = Inputs.FindByPredicate([InNodeID](const TObjectPtr<UMetasoundEditorGraphInput>& InInput)
	{
		if (InInput)
		{
			return InInput->NodeID == InNodeID;
		}

		return false;
	});
	return Input ? Input->Get() : nullptr;
}

UMetasoundEditorGraphInput* UMetasoundEditorGraph::FindInput(FName InName) const
{
	const TObjectPtr<UMetasoundEditorGraphInput>* Input = Inputs.FindByPredicate([InName](const TObjectPtr<UMetasoundEditorGraphInput>& InInput)
	{
		if (InInput)
		{
			const FName NodeName = InInput->GetMemberName();
			return NodeName == InName;
		}

		return false;
	});
	return Input ? Input->Get() : nullptr;
}

UMetasoundEditorGraphInput* UMetasoundEditorGraph::FindOrAddInput(const FGuid& InNodeID)
{
	using namespace Metasound;

	if (TObjectPtr<UMetasoundEditorGraphInput> Input = FindInput(InNodeID))
	{
		return Input;
	}

	const FMetaSoundFrontendDocumentBuilder& Builder = GetBuilderChecked().GetConstBuilder();
	if (const FMetasoundFrontendNode* Node = Builder.FindNode(InNodeID))
	{
		if (const FMetasoundFrontendClassInput* ClassInput = Builder.FindGraphInput(Node->Name))
		{
			const FMetasoundFrontendLiteral& DefaultLiteral = ClassInput->FindConstDefaultChecked(Frontend::DefaultPageID);
			if (const FMetasoundFrontendClass* Class = Builder.FindDependency(Node->ClassID))
			{
				FMetasoundFrontendClassName ClassName = Class->Metadata.GetClassName();

				UMetasoundEditorGraphInput* NewInput = NewObject<UMetasoundEditorGraphInput>(this, Editor::GraphPrivate::GetUniqueTransientMemberName(), RF_Transactional);
				if (ensure(NewInput))
				{
					NewInput->InitMember(ClassInput->TypeName, DefaultLiteral, InNodeID, MoveTemp(ClassName));
					Inputs.Add(NewInput);
				}

				return NewInput;
			}
		}
	}

	return nullptr;
}

UMetasoundEditorGraphInput* UMetasoundEditorGraph::FindOrAddInput(Metasound::Frontend::FConstNodeHandle InNodeHandle)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	FConstGraphHandle Graph = InNodeHandle->GetOwningGraph();

	FName TypeName;
	FGuid VertexID;

	ensure(InNodeHandle->GetNumInputs() == 1);
	InNodeHandle->IterateConstInputs([InGraph = &Graph, InTypeName = &TypeName, InVertexID = &VertexID](FConstInputHandle InputHandle)
	{
		*InTypeName = InputHandle->GetDataType();
		*InVertexID = (*InGraph)->GetVertexIDForInputVertex(InputHandle->GetName());
	});

	const FGuid NodeID = InNodeHandle->GetID();
	if (TObjectPtr<UMetasoundEditorGraphInput> Input = FindInput(NodeID))
	{
		ensure(Input->TypeName == TypeName);
		return Input;
	}

	UMetasoundEditorGraphInput* NewInput = NewObject<UMetasoundEditorGraphInput>(this, GraphPrivate::GetUniqueTransientMemberName(), RF_Transactional);
	if (ensure(NewInput))
	{
		FMetasoundFrontendLiteral DefaultLiteral = Graph->GetDefaultInput(VertexID);
		FMetasoundFrontendClassName ClassName = InNodeHandle->GetClassMetadata().GetClassName();
		NewInput->InitMember(TypeName, DefaultLiteral, NodeID, MoveTemp(ClassName));
		Inputs.Add(NewInput);

		return NewInput;
	}

	return nullptr;
}

UMetasoundEditorGraphOutput* UMetasoundEditorGraph::FindOutput(FGuid InNodeID) const
{
	const TObjectPtr<UMetasoundEditorGraphOutput>* Output = Outputs.FindByPredicate([InNodeID](const TObjectPtr<UMetasoundEditorGraphOutput>& InOutput)
	{
		if (InOutput)
		{
			return InOutput->NodeID == InNodeID;
		}
		return false;
	});
	return Output ? Output->Get() : nullptr;
}

UMetasoundEditorGraphOutput* UMetasoundEditorGraph::FindOutput(FName InName) const
{
	const TObjectPtr<UMetasoundEditorGraphOutput>* Output = Outputs.FindByPredicate([&InName](const TObjectPtr<UMetasoundEditorGraphOutput>& InOutput)
	{
		if (InOutput)
		{
			return InName == InOutput->GetMemberName();
		}
		return false;
	});
	return Output ? Output->Get() : nullptr;
}

UMetasoundEditorGraphOutput* UMetasoundEditorGraph::FindOrAddOutput(const FGuid& InNodeID)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (TObjectPtr<UMetasoundEditorGraphOutput> Output = FindOutput(InNodeID))
	{
		return Output;
	}

	const FMetaSoundFrontendDocumentBuilder& Builder = GetBuilderChecked().GetConstBuilder();
	if (const FMetasoundFrontendNode* Node = Builder.FindNode(InNodeID))
	{
		if (const FMetasoundFrontendClassOutput* ClassOutput = Builder.FindGraphOutput(Node->Name))
		{
			if (const FMetasoundFrontendClass* Class = Builder.FindDependency(Node->ClassID))
			{
				FMetasoundFrontendClassName ClassName = Class->Metadata.GetClassName();

				UMetasoundEditorGraphOutput* NewOutput = NewObject<UMetasoundEditorGraphOutput>(this, GraphPrivate::GetUniqueTransientMemberName(), RF_Transactional);
				if (ensure(NewOutput))
				{
					FMetasoundFrontendLiteral DefaultLiteral;
					DefaultLiteral.SetFromLiteral(IDataTypeRegistry::Get().CreateDefaultLiteral(ClassOutput->TypeName));
					NewOutput->InitMember(ClassOutput->TypeName, DefaultLiteral, InNodeID, MoveTemp(ClassName));
					Outputs.Add(NewOutput);
				}

				return NewOutput;
			}
		}
	}

	return nullptr;
}

UMetasoundEditorGraphOutput* UMetasoundEditorGraph::FindOrAddOutput(Metasound::Frontend::FConstNodeHandle InNodeHandle)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	FConstGraphHandle Graph = InNodeHandle->GetOwningGraph();

	FName TypeName;
	FGuid VertexID;

	ensure(InNodeHandle->GetNumOutputs() == 1);
	InNodeHandle->IterateConstOutputs([InGraph = &Graph, InTypeName = &TypeName, InVertexID = &VertexID](FConstOutputHandle OutputHandle)
	{
		*InTypeName = OutputHandle->GetDataType();
		*InVertexID = (*InGraph)->GetVertexIDForInputVertex(OutputHandle->GetName());
	});

	const FGuid NodeID = InNodeHandle->GetID();
	if (TObjectPtr<UMetasoundEditorGraphOutput> Output = FindOutput(NodeID))
	{
		ensure(Output->TypeName == TypeName);
		return Output;
	}

	UMetasoundEditorGraphOutput* NewOutput = NewObject<UMetasoundEditorGraphOutput>(this, GraphPrivate::GetUniqueTransientMemberName(), RF_Transactional);
	if (ensure(NewOutput))
	{
		FMetasoundFrontendLiteral DefaultLiteral;
		DefaultLiteral.SetFromLiteral(IDataTypeRegistry::Get().CreateDefaultLiteral(TypeName));

		FMetasoundFrontendClassName ClassName = InNodeHandle->GetClassMetadata().GetClassName();
		NewOutput->InitMember(TypeName, DefaultLiteral, NodeID, MoveTemp(ClassName));
		Outputs.Add(NewOutput);

		return NewOutput;
	}

	return nullptr;
}

UMetasoundEditorGraphVariable* UMetasoundEditorGraph::FindVariable(const FGuid& InVariableID) const
{
	const TObjectPtr<UMetasoundEditorGraphVariable>* Variable = Variables.FindByPredicate([&InVariableID](const TObjectPtr<UMetasoundEditorGraphVariable>& InVariable)
	{
		if (InVariable)
		{
			return InVariable->GetVariableID() == InVariableID;
		}

		return false;
	});
	return Variable ? Variable->Get() : nullptr;
}

UMetasoundEditorGraphVariable* UMetasoundEditorGraph::FindOrAddVariable(const Metasound::Frontend::FConstVariableHandle& InVariableHandle)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	FName TypeName = InVariableHandle->GetDataType();
	const FGuid VariableID = InVariableHandle->GetID();

	if (TObjectPtr<UMetasoundEditorGraphVariable> EditorVariable = FindVariable(VariableID))
	{
		ensure(EditorVariable->TypeName == TypeName);
		return EditorVariable;
	}

	UMetasoundEditorGraphVariable* NewVariable = NewObject<UMetasoundEditorGraphVariable>(this, GraphPrivate::GetUniqueTransientMemberName(), RF_Transactional);
	if (ensure(NewVariable))
	{
		const FMetasoundFrontendLiteral DefaultLiteral = InVariableHandle->GetLiteral();
		NewVariable->InitMember(InVariableHandle->GetDataType(), DefaultLiteral, VariableID);
		Variables.Add(NewVariable);
		return NewVariable;
	}

	return nullptr;
}

UMetasoundEditorGraphMember* UMetasoundEditorGraph::FindMember(FGuid InMemberID) const
{
	if (UMetasoundEditorGraphOutput* Output = FindOutput(InMemberID))
	{
		return Output;
	}

	if (UMetasoundEditorGraphInput* Input = FindInput(InMemberID))
	{
		return Input;
	}

	return FindVariable(InMemberID);
}

UMetasoundEditorGraphMember* UMetasoundEditorGraph::FindAdjacentMember(const UMetasoundEditorGraphMember& InMember)
{
	int32 IndexInArray = Inputs.IndexOfByPredicate([&](const TObjectPtr<UMetasoundEditorGraphInput>& InputMember)
	{
		return &InMember == ToRawPtr(InputMember);
	});

	if (INDEX_NONE != IndexInArray)
	{
		if (IndexInArray < (Inputs.Num() - 1))
		{
			return Inputs[IndexInArray + 1];
		}
		else if (IndexInArray > 0)
		{
			return Inputs[IndexInArray - 1];
		}
		else
		{
			if (Outputs.Num() > 0)
			{
				return Outputs[0];
			}
		}

		return nullptr;
	}

	IndexInArray = Outputs.IndexOfByPredicate([&](const TObjectPtr<UMetasoundEditorGraphOutput>& OutputMember)
	{
		return &InMember == ToRawPtr(OutputMember);
	});

	if (INDEX_NONE != IndexInArray)
	{
		if (IndexInArray < (Outputs.Num() - 1))
		{
			return Outputs[IndexInArray + 1];
		}
		else if (IndexInArray > 0)
		{
			return Outputs[IndexInArray - 1];
		}
		else if (Inputs.Num() > 0)
		{
			return Inputs.Last();
		}

		return nullptr;
	}

	return nullptr;
}

bool UMetasoundEditorGraph::ContainsInput(const UMetasoundEditorGraphInput& InInput) const
{
	return Inputs.Contains(&InInput);
}

bool UMetasoundEditorGraph::ContainsOutput(const UMetasoundEditorGraphOutput& InOutput) const
{
	return Outputs.Contains(&InOutput);
}

bool UMetasoundEditorGraph::ContainsVariable(const UMetasoundEditorGraphVariable& InVariable) const
{
	return Variables.Contains(&InVariable);
}

void UMetasoundEditorGraph::MigrateEditorDocumentData(FMetaSoundFrontendDocumentBuilder& OutBuilder)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	// 1. Add node comments to style for non-comment nodes (comment nodes processed separately below)
	TArray<UMetasoundEditorGraphNode*> AllMetaSoundNodes;
	GetNodesOfClass(AllMetaSoundNodes);

	constexpr bool bIsVisible = true;
	for (UMetasoundEditorGraphNode* Node : AllMetaSoundNodes)
	{
		// Comment nodes are migrated in the next loop
		if (!Node->IsA<UEdGraphNode_Comment>() && !Node->NodeComment.IsEmpty())
		{
			const FGuid& NodeID = Node->GetNodeID();
			if (NodeID.IsValid())
			{
				OutBuilder.SetNodeComment(NodeID, MoveTemp(Node->NodeComment));
				OutBuilder.SetNodeCommentVisible(NodeID, Node->bCommentBubblePinned);
			}
		}
	}

	const INodeTemplate* Template = INodeTemplateRegistry::Get().FindTemplate(FInputNodeTemplate::GetRegistryKey());
	checkf(Template, TEXT("Failed to find InputNodeTemplate, which is required for migrating editor document data"));

	// 2. Move inputs to input template nodes, using connection data within the ed graph as a way to inform
	// which template node should effectively represent which input template node and own which connections.
	// Cache literals in the literal metadata map to ensure data is serialized appropriately.
	IterateInputs([this, &OutBuilder, Template](UMetasoundEditorGraphInput& Input)
	{
#if WITH_EDITOR
		if (GEditor) // Have to check if valid as its unavailable in standalone editor builds`
		{
			if (UMetaSoundEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMetaSoundEditorSubsystem>())
			{
				if (UMetasoundEditorGraphMemberDefaultLiteral* DefaultLiteral = Input.GetLiteral())
				{
					TSubclassOf<UMetasoundEditorGraphMemberDefaultLiteral> SubClass = DefaultLiteral->GetClass();

					// Migration can occur on async thread, and bind can create a new literal
					FGCScopeGuard ScopeGuard;
					EditorSubsystem->BindMemberMetadata(OutBuilder, Input, SubClass, DefaultLiteral);
					DefaultLiteral->ClearInternalFlags(EInternalObjectFlags::Async);
				}
			}
		}
#endif // WITH_EDITOR

		// Presets get rebuilt anyway and may have invalid connections (newly referenced vertices)
		// that need to be rebuilt later in asset load.
		if (OutBuilder.IsPreset())
		{
			return;
		}

		// Cache data to be used in edge swapping below, being careful to not reference the input node pointer
		// or vertex reference within the lower inner loop as the underlying node array may be reallocated by
		// template nodes being added.
		const FName InputName = Input.GetMemberName();
		const FMetasoundFrontendNode* InputNode = OutBuilder.FindGraphInputNode(InputName);

		// Potentially not used input, which is perfectly valid so early out
		if (!InputNode)
		{
			return;
		}

		const FMetasoundFrontendVertex InputNodeOutputVertex = InputNode->Interface.Outputs.Last();

		const FGuid InputNodeID = InputNode->GetID();
		FMetasoundFrontendEdge EdgeToRemove { InputNodeID, InputNodeOutputVertex.VertexID };

		TArray<UMetasoundEditorGraphMemberNode*> Nodes = Input.GetNodes();
		for (UMetasoundEditorGraphMemberNode* EdNode : Nodes)
		{
			FNodeTemplateGenerateInterfaceParams Params { { InputNodeOutputVertex.TypeName }, { } };
			const FMetasoundFrontendNode* TemplateNode = OutBuilder.AddNodeByTemplate(*Template, MoveTemp(Params));
			check(TemplateNode);

			const FGuid& TemplateNodeID = TemplateNode->GetID();
			const FGuid& TemplateInputID = TemplateNode->Interface.Inputs.Last().VertexID;
			const FGuid& TemplateOutputID = TemplateNode->Interface.Outputs.Last().VertexID;
			OutBuilder.SetNodeLocation(TemplateNodeID, FVector2D(EdNode->NodePosX, EdNode->NodePosY));

			// Transform comment to template from input node
			OutBuilder.SetNodeComment(TemplateNodeID, MoveTemp(EdNode->NodeComment));
			OutBuilder.SetNodeCommentVisible(TemplateNodeID, EdNode->bCommentBubblePinned);

			// Add edge between input node and new template node
			OutBuilder.AddEdge(FMetasoundFrontendEdge
			{
				InputNodeID,
				InputNodeOutputVertex.VertexID,
				TemplateNodeID,
				TemplateInputID
			});

			for (const UEdGraphPin* Pin : EdNode->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Output)
				{
					for (const UEdGraphPin* Linked : Pin->LinkedTo)
					{
						if (Linked)
						{
							const UMetasoundEditorGraphNode* ConnectedNode = CastChecked<const UMetasoundEditorGraphNode>(Linked->GetOwningNode());
							const FGuid ConnectedNodeID = ConnectedNode->GetNodeID();
							if (const FMetasoundFrontendVertex* ConnectedInput = OutBuilder.FindNodeInput(ConnectedNodeID, Linked->GetFName()))
							{
								// Swap connection from input node to connected node to now be from template node to connected node
								EdgeToRemove.ToNodeID = ConnectedNodeID,
								EdgeToRemove.ToVertexID = ConnectedInput->VertexID;
								OutBuilder.RemoveEdge(EdgeToRemove);

								OutBuilder.AddEdge(FMetasoundFrontendEdge
								{
									TemplateNodeID,
									TemplateOutputID,
									ConnectedNodeID,
									ConnectedInput->VertexID
								});
							}
							else
							{
								UE_LOG(LogMetaSound, Warning, TEXT("Editor graph '%s' migration failed to find node '%s' class output '%s': Ignoring connection upgrade from input '%s'"),
									*OutBuilder.GetDebugName(),
									*ConnectedNode->GetDisplayName().ToString(),
									*Linked->GetName(),
									*InputName.ToString());
							}
						}
					}
				}
			}
		}
	});

	// 4. Add comment nodes as builder graph comments to frontend document
	// (No need to propagate comments for presets)
	if (!OutBuilder.IsPreset())
	{
		TArray<UEdGraphNode_Comment*> CommentNodes;
		GetNodesOfClass(CommentNodes);
		for (UEdGraphNode_Comment* Node : CommentNodes)
		{
			FMetaSoundFrontendGraphComment& NewComment = OutBuilder.FindOrAddGraphComment(FGuid::NewGuid());
			UMetasoundEditorGraphCommentNode::ConvertToFrontendComment(*Node, NewComment);
		}
	}

	// 5. Remove input locations and ensure that all other nodes only have at most one
	// location represented in the style/editor graph (0 is acceptable as some member
	// node types (eg. variables) may not contain locations and that's ok).
	const TArray<FMetasoundFrontendNode>& GraphNodes = OutBuilder.FindConstBuildGraphChecked().Nodes;
	TMap<FGuid, const UMetasoundEditorGraphNode*> EdNodeMap;
	Algo::Transform(AllMetaSoundNodes, EdNodeMap, [](const UEdGraphNode* Node)
	{
		return TTuple<FGuid, const UMetasoundEditorGraphNode*>(Node->NodeGuid, Cast<const UMetasoundEditorGraphNode>(Node));
	});
	for (const FMetasoundFrontendNode& Node : GraphNodes)
	{
		if (const FMetasoundFrontendClass* Class = OutBuilder.FindDependency(Node.ClassID))
		{
			// Inputs no longer have locational data as input template nodes provide that
			if (Class->Metadata.GetType() == EMetasoundFrontendClassType::Input)
			{
				OutBuilder.RemoveNodeLocation(Node.GetID());
			}
			else
			{
				const TMap<FGuid, FVector2D>& Locations = Node.Style.Display.Locations;
				if (Locations.Num() > 1)
				{
					TPair<FGuid, FVector2D> DefaultLocation;
					for (const TPair<FGuid, FVector2D>& Pair : Locations)
					{
						DefaultLocation = Pair;
						if (const UMetasoundEditorGraphNode* MetaSoundNode = EdNodeMap.FindRef(Pair.Key))
						{
							if (MetaSoundNode->GetNodeID() == Node.GetID())
							{
								break;
							}
						}
					}

					// Remove first in case there are multiple locations and the editor guid may be different
					OutBuilder.RemoveNodeLocation(Node.GetID());
					OutBuilder.SetNodeLocation(Node.GetID(), DefaultLocation.Value, &DefaultLocation.Key);
				}
			}
		}
	}
}

void UMetasoundEditorGraph::SetPreviewID(uint32 InPreviewID)
{
	PreviewID = InPreviewID;
}

bool UMetasoundEditorGraph::IsPreviewing() const
{
	if (GEditor)
	{
		UAudioComponent* PreviewComponent = GEditor->GetPreviewAudioComponent();
		if (!PreviewComponent)
		{
			return false;
		}

		if (!PreviewComponent->IsPlaying())
		{
			return false;
		}

		return PreviewComponent->GetUniqueID() == PreviewID;
	}

	return false;
}

bool UMetasoundEditorGraph::IsEditable() const
{
	return GetGraphHandle()->GetGraphStyle().bIsGraphEditable;
}

void UMetasoundEditorGraph::IterateInputs(TFunctionRef<void(UMetasoundEditorGraphInput&)> InFunction) const
{
	for (UMetasoundEditorGraphInput* Input : Inputs)
	{
		if (Input)
		{
			InFunction(*Input);
		}
	}
}

void UMetasoundEditorGraph::IterateOutputs(TFunctionRef<void(UMetasoundEditorGraphOutput&)> InFunction) const
{
	for (UMetasoundEditorGraphOutput* Output : Outputs)
	{
		if (ensure(Output))
		{
			InFunction(*Output);
		}
	}
}

void UMetasoundEditorGraph::IterateVariables(TFunctionRef<void(UMetasoundEditorGraphVariable&)> InFunction) const
{
	for (UMetasoundEditorGraphVariable* Variable : Variables)
	{
		if (ensure(Variable))
		{
			InFunction(*Variable);
		}
	}
}

void UMetasoundEditorGraph::IterateMembers(TFunctionRef<void(UMetasoundEditorGraphMember&)> InFunction) const
{
	for (UMetasoundEditorGraphInput* Input : Inputs)
	{
		if (Input)
		{
			InFunction(*Input);
		}
	}

	for (UMetasoundEditorGraphOutput* Output : Outputs)
	{
		if (ensure(Output))
		{
			InFunction(*Output);
		}
	}

	for (UMetasoundEditorGraphVariable* Variable : Variables)
	{
		if (ensure(Variable))
		{
			InFunction(*Variable);
		}
	}
}

void UMetasoundEditorGraph::ValidateInternal(Metasound::Editor::FGraphValidationResults& OutResults)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	OutResults = FGraphValidationResults();
	TSet<FGuid> NodeGuids; 
	TArray<UMetasoundEditorGraphNode*> NodesToValidate;
	GetNodesOfClass<UMetasoundEditorGraphNode>(NodesToValidate);
	bool bNodeIdFound = false;
	for (UMetasoundEditorGraphNode* Node : NodesToValidate)
	{
		Node->CacheBreadcrumb();
		FGraphNodeValidationResult NodeResult(*Node);

		// Validate there is only 1 editor node per guid 
		// Input nodes are currently not 1:1 with their frontend representation
		// but when they are, they can be validated here as well 
		if (!Node->IsA<UMetasoundEditorGraphInputNode>() && !Node->IsA<UMetasoundEditorGraphVariableNode>())
		{
			NodeGuids.Add(Node->GetNodeID(), &bNodeIdFound);
			if (bNodeIdFound)
			{
				NodeResult.SetMessage(EMessageSeverity::Warning, TEXT("The internal node this represents is referenced multiple times and may have unintended behavior. Please delete and readd this node."));
			}
		}

		Node->Validate(NodeResult);

		OutResults.NodeResults.Add(MoveTemp(NodeResult));
	}
}
#undef LOCTEXT_NAMESPACE
