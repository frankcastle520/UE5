// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMFunctionReferenceNode.h"
#include "RigVMModel/RigVMFunctionLibrary.h"
#include "RigVMCore/RigVMGraphFunctionDefinition.h"
#include "RigVMCore/RigVMGraphFunctionHost.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigVMFunctionReferenceNode)

FString URigVMFunctionReferenceNode::GetNodeTitle() const
{
	return ReferencedFunctionHeader.NodeTitle;
}

FLinearColor URigVMFunctionReferenceNode::GetNodeColor() const
{
	return ReferencedFunctionHeader.NodeColor;
}

FText URigVMFunctionReferenceNode::GetToolTipText() const
{
	return ReferencedFunctionHeader.GetTooltip();
}

FName URigVMFunctionReferenceNode::GetDisplayNameForPin(const FString& InPinPath) const
{
	if(const FString* DisplayName = ReferencedFunctionHeader.Layout.FindDisplayName(InPinPath))
	{
		if(!DisplayName->IsEmpty())
		{
			return *(*DisplayName);
		}
	}

	if(const URigVMPin* Pin = FindPin(InPinPath))
	{
		if(Pin->IsRootPin())
		{
			const FRigVMGraphFunctionArgument* Argument = ReferencedFunctionHeader.Arguments.FindByPredicate([Pin](const FRigVMGraphFunctionArgument& Argument)
			{
				return Argument.Name == Pin->GetFName();
			});

			if (Argument && !Argument->DisplayName.IsNone())
			{
				return Argument->DisplayName;
			}
		}
	}

	if(const URigVMPin* ReferencedPin = FindReferencedPin(InPinPath))
	{
		return ReferencedPin->GetDisplayName();
	}

	return Super::GetDisplayNameForPin(InPinPath);
}

FString URigVMFunctionReferenceNode::GetCategoryForPin(const FString& InPinPath) const
{
	if(const FString* Category = ReferencedFunctionHeader.Layout.FindCategory(InPinPath))
	{
		if(!Category->IsEmpty())
		{
			return *Category;
		}
	}

	if(const URigVMPin* ReferencedPin = FindReferencedPin(InPinPath))
	{
		return ReferencedPin->GetCategory();
	}

	return Super::GetCategoryForPin(InPinPath);
}

int32 URigVMFunctionReferenceNode::GetIndexInCategoryForPin(const FString& InPinPath) const
{
	if(const int32* Index = ReferencedFunctionHeader.Layout.PinIndexInCategory.Find(InPinPath))
	{
		return *Index;
	}

	if(const URigVMPin* ReferencedPin = FindReferencedPin(InPinPath))
	{
		return ReferencedPin->GetIndexInCategory();
	}

	return INDEX_NONE;
}

FString URigVMFunctionReferenceNode::GetNodeCategory() const
{
	return ReferencedFunctionHeader.Category;
}

FString URigVMFunctionReferenceNode::GetNodeKeywords() const
{
	return ReferencedFunctionHeader.Keywords;
}

bool URigVMFunctionReferenceNode::RequiresVariableRemapping() const
{
	TArray<FRigVMExternalVariable> InnerVariables;
	return RequiresVariableRemappingInternal(InnerVariables);
}

bool URigVMFunctionReferenceNode::RequiresVariableRemappingInternal(TArray<FRigVMExternalVariable>& InnerVariables) const
{
	bool bHostedInDifferencePackage = false;
	
	FRigVMGraphFunctionIdentifier LibraryPointer = ReferencedFunctionHeader.LibraryPointer;
	const FString& LibraryPackagePath = LibraryPointer.GetNodeSoftPath().GetLongPackageName();
	const FString& ThisPacakgePath = GetPackage()->GetPathName();
	bHostedInDifferencePackage = LibraryPackagePath != ThisPacakgePath;
		
	if(bHostedInDifferencePackage)
	{
		InnerVariables = GetExternalVariables(false);
		if(InnerVariables.Num() == 0)
		{
			return false;
		}
	}

	return bHostedInDifferencePackage;
}

bool URigVMFunctionReferenceNode::IsFullyRemapped() const
{
	TArray<FRigVMExternalVariable> InnerVariables;
	if(!RequiresVariableRemappingInternal(InnerVariables))
	{
		return true;
	}

	for(const FRigVMExternalVariable& InnerVariable : InnerVariables)
	{
		const FName InnerVariableName = InnerVariable.Name;
		const FName* OuterVariableName = VariableMap.Find(InnerVariableName);
		if(OuterVariableName == nullptr)
		{
			return false;
		}
		check(!OuterVariableName->IsNone());
	}

	return true;
}

TArray<FRigVMExternalVariable> URigVMFunctionReferenceNode::GetExternalVariables() const
{
	return GetExternalVariables(true);
}

TArray<FRigVMExternalVariable> URigVMFunctionReferenceNode::GetExternalVariables(bool bRemapped) const
{
	TArray<FRigVMExternalVariable> Variables;
	
	if(!bRemapped)
	{
		if (const FRigVMGraphFunctionData* FunctionData = GetReferencedFunctionData(false))
		{
			Variables = FunctionData->Header.ExternalVariables;
		}
		else
		{
			Variables = GetReferencedFunctionHeader().ExternalVariables;
		}
	}
	else
	{
		if(RequiresVariableRemappingInternal(Variables))
		{
			for(FRigVMExternalVariable& Variable : Variables)
			{
				const FName* OuterVariableName = VariableMap.Find(Variable.Name);
				if(OuterVariableName != nullptr)
				{
					check(!OuterVariableName->IsNone());
					Variable.Name = *OuterVariableName;
				}
			}
		}
	}
	
	return Variables; 
}

FName URigVMFunctionReferenceNode::GetOuterVariableName(const FName& InInnerVariableName) const
{
	if(const FName* OuterVariableName = VariableMap.Find(InInnerVariableName))
	{
		return *OuterVariableName;
	}
	return NAME_None;
}

uint32 URigVMFunctionReferenceNode::GetStructureHash() const
{
	const FRigVMRegistry& Registry = FRigVMRegistry::Get();
	
	uint32 Hash = Super::GetStructureHash();

	Hash = HashCombine(Hash, GetTypeHash(ReferencedFunctionHeader.Name.ToString()));
	Hash = HashCombine(Hash, GetTypeHash(ReferencedFunctionHeader.NodeTitle));
	Hash = HashCombine(Hash, GetTypeHash(ReferencedFunctionHeader.LibraryPointer.GetLibraryNodePath()));
	Hash = HashCombine(Hash, GetTypeHash(ReferencedFunctionHeader.Keywords));
	Hash = HashCombine(Hash, GetTypeHash(ReferencedFunctionHeader.Description));
	Hash = HashCombine(Hash, GetTypeHash(ReferencedFunctionHeader.NodeColor));

	for(const FRigVMGraphFunctionArgument& Argument : ReferencedFunctionHeader.Arguments)
	{
		Hash = HashCombine(Hash, GetTypeHash(Argument.Name.ToString()));
		Hash = HashCombine(Hash, GetTypeHash((int32)Argument.Direction));
		const TRigVMTypeIndex TypeIndex = Registry.GetTypeIndexFromCPPType(Argument.CPPType.ToString());
		Hash = HashCombine(Hash, Registry.GetHashForType(TypeIndex));

		for(const TPair<FString, FText>& Pair : Argument.PathToTooltip)
		{
			Hash = HashCombine(Hash, GetTypeHash(Pair.Key));
			Hash = HashCombine(Hash, GetTypeHash(Pair.Value.ToString()));
		}
	}

	for(const FRigVMExternalVariable& ExternalVariable : ReferencedFunctionHeader.ExternalVariables)
	{
		Hash = HashCombine(Hash, GetTypeHash(ExternalVariable.Name.ToString()));
		const TRigVMTypeIndex TypeIndex = Registry.GetTypeIndexFromCPPType(ExternalVariable.TypeName.ToString());
		Hash = HashCombine(Hash, Registry.GetHashForType(TypeIndex));
	}

	return Hash;
}

void URigVMFunctionReferenceNode::UpdateFunctionHeaderFromHost()
{
	if (const FRigVMGraphFunctionData* Data = GetReferencedFunctionData())
	{
		ReferencedFunctionHeader = Data->Header;
		InvalidateCache();
	}
}

const FRigVMGraphFunctionData* URigVMFunctionReferenceNode::GetReferencedFunctionData(bool bLoadIfNecessary) const
{
	if (IRigVMGraphFunctionHost* Host = ReferencedFunctionHeader.GetFunctionHost(bLoadIfNecessary))
	{
		return Host->GetRigVMGraphFunctionStore()->FindFunction(ReferencedFunctionHeader.LibraryPointer);
	}
	return nullptr;
}

TArray<FRigVMTag> URigVMFunctionReferenceNode::GetVariantTags() const
{
	if (const FRigVMGraphFunctionData* Data = GetReferencedFunctionData(false))
	{
		return Data->Header.Variant.Tags;
	}
	return GetReferencedFunctionHeader().Variant.Tags;
}

FString URigVMFunctionReferenceNode::GetOriginalDefaultValueForRootPin(const URigVMPin* InRootPin) const
{
	if(InRootPin->CanProvideDefaultValue())
	{
		const FRigVMGraphFunctionHeader& Header = GetReferencedFunctionHeader();
		if(!Header.IsValid())
		{
			// we don't have a valid header yet
			// maybe still waiting for the function reference to resolve?
			return FString();
		}
		const FRigVMGraphFunctionArgument* Argument = Header.Arguments.FindByPredicate([InRootPin](const FRigVMGraphFunctionArgument& InArgument) -> bool
		{
			return InArgument.Name == InRootPin->GetFName();
		});
		if(Argument)
		{
			return Argument->DefaultValue;
		}
	}
	return Super::GetOriginalDefaultValueForRootPin(InRootPin);
}

FText URigVMFunctionReferenceNode::GetToolTipTextForPin(const URigVMPin* InPin) const
{
	check(InPin);

	URigVMPin* RootPin = InPin->GetRootPin();
	const FRigVMGraphFunctionArgument* Argument = ReferencedFunctionHeader.Arguments.FindByPredicate([RootPin](const FRigVMGraphFunctionArgument& Argument)
	{
		return Argument.Name == RootPin->GetFName();
	});

	if (Argument)
	{
		if (const FText* Tooltip = Argument->PathToTooltip.Find(InPin->GetSegmentPath(false)))
		{
			return *Tooltip;
		}
	}

	if(const URigVMPin* ReferencedPin = FindReferencedPin(InPin))
	{
		return ReferencedPin->GetToolTipText();
	}

	return Super::GetToolTipTextForPin(InPin);
}

TArray<FString> URigVMFunctionReferenceNode::GetPinCategories() const
{
	FRigVMNodeLayout ReferenceLayout;
	const FRigVMNodeLayout* Layout = &ReferencedFunctionHeader.Layout;
	if(IsReferencedNodeLoaded())
	{
		ReferenceLayout = LoadReferencedNode()->GetNodeLayout();
		Layout = &ReferenceLayout;
	}
	TArray<FString> TransientPinCategories;
	for(const FRigVMPinCategory& Category : Layout->Categories)
	{
		TransientPinCategories.Add(Category.Path);
	}
	return TransientPinCategories;
}

FRigVMNodeLayout URigVMFunctionReferenceNode::GetNodeLayout(bool bIncludeEmptyCategories) const
{
	if(IsReferencedNodeLoaded())
	{
		return LoadReferencedNode()->GetNodeLayout(bIncludeEmptyCategories);
	}
	return ReferencedFunctionHeader.Layout;
}

FRigVMGraphFunctionIdentifier URigVMFunctionReferenceNode::GetFunctionIdentifier() const
{
	return GetReferencedFunctionHeader().LibraryPointer;
}

bool URigVMFunctionReferenceNode::IsReferencedFunctionHostLoaded() const
{
	return ReferencedFunctionHeader.LibraryPointer.HostObject.ResolveObject() != nullptr;
}

bool URigVMFunctionReferenceNode::IsReferencedNodeLoaded() const
{
	return ReferencedFunctionHeader.LibraryPointer.GetNodeSoftPath().ResolveObject() != nullptr;
}

URigVMLibraryNode* URigVMFunctionReferenceNode::LoadReferencedNode() const
{
	FSoftObjectPath SoftObjectPath = ReferencedFunctionHeader.LibraryPointer.GetNodeSoftPath();
	UObject* LibraryNode = SoftObjectPath.ResolveObject();
	if (!LibraryNode)
	{
		LibraryNode = SoftObjectPath.TryLoad();
	}
	return Cast<URigVMLibraryNode>(LibraryNode);
	
}

TArray<int32> URigVMFunctionReferenceNode::GetInstructionsForVMImpl(const FRigVMExtendedExecuteContext& Context, URigVM* InVM, const FRigVMASTProxy& InProxy) const
{
	TArray<int32> Instructions = URigVMNode::GetInstructionsForVMImpl(Context, InVM, InProxy);

	// if the base cannot find any matching instructions, fall back to the library node's implementation
	if(Instructions.IsEmpty())
	{
		Instructions = Super::GetInstructionsForVMImpl(Context, InVM, InProxy);
	}
	
	return Instructions;
}

const URigVMPin* URigVMFunctionReferenceNode::FindReferencedPin(const URigVMPin* InPin) const
{
	return FindReferencedPin(InPin->GetSegmentPath(true));
}

const URigVMPin* URigVMFunctionReferenceNode::FindReferencedPin(const FString& InPinPath) const
{
	if(const URigVMLibraryNode* LibraryNode = LoadReferencedNode())
	{
		return LibraryNode->FindPin(InPinPath);
	}
	return nullptr;
}
