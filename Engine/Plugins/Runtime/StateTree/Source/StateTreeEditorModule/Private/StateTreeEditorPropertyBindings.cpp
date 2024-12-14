// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditorPropertyBindings.h"
#include "StateTreePropertyBindingCompiler.h"
#include "Misc/EnumerateRange.h"
#include "PropertyPathHelpers.h"
#include "StateTreeNodeBase.h"
#include "StateTreePropertyFunctionBase.h"
#include "StateTreeEditorNode.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeEditorPropertyBindings)

#define LOCTEXT_NAMESPACE "StateTreeEditor"

UStateTreeEditorPropertyBindingsOwner::UStateTreeEditorPropertyBindingsOwner(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

//////////////////////////////////////////////////////////////////////////

void FStateTreeEditorPropertyBindings::SetBindingsOwner(gsl::not_null<TScriptInterface<IStateTreeEditorPropertyBindingsOwner>> InBindingsOwner)
{
	BindingsOwner = InBindingsOwner;
}

void FStateTreeEditorPropertyBindings::AddPropertyBinding(const FStateTreePropertyPath& SourcePath, const FStateTreePropertyPath& TargetPath)
{
	RemovePropertyBindings(TargetPath, ESearchMode::Exact);

	FStateTreePropertyPathBinding Binding(SourcePath, TargetPath);

	// If we have bindings owner, update property path segments to capture property IDs, etc.
	if (BindingsOwner)
	{
		FStateTreeDataView SourceDataView;
		if (BindingsOwner->GetDataViewByID(Binding.GetSourcePath().GetStructID(), SourceDataView))
		{
			Binding.GetMutableSourcePath().UpdateSegmentsFromValue(SourceDataView);
		}
		FStateTreeDataView TargetDataView;
		if (BindingsOwner->GetDataViewByID(Binding.GetTargetPath().GetStructID(), TargetDataView))
		{
			Binding.GetMutableTargetPath().UpdateSegmentsFromValue(TargetDataView);
		}
	}
	
	PropertyBindings.Add(Binding);
}

FStateTreePropertyPath FStateTreeEditorPropertyBindings::AddFunctionPropertyBinding(const UScriptStruct* PropertyFunctionNodeStruct, TConstArrayView<FStateTreePropertyPathSegment> SourcePathSegments, const FStateTreePropertyPath& TargetPath)
{
	check(PropertyFunctionNodeStruct->IsChildOf<FStateTreePropertyFunctionBase>());

	FInstancedStruct PropertyFunctionNode(FStateTreeEditorNode::StaticStruct());
	FStateTreeEditorNode& PropertyFunction = PropertyFunctionNode.GetMutable<FStateTreeEditorNode>();
	const FGuid NodeID = FGuid::NewGuid();
	PropertyFunction.ID = NodeID;
	PropertyFunction.Node.InitializeAs(PropertyFunctionNodeStruct);
	const FStateTreePropertyFunctionBase& Function = PropertyFunction.Node.Get<FStateTreePropertyFunctionBase>();
	if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Function.GetInstanceDataType()))
	{
		PropertyFunction.Instance.InitializeAs(InstanceType);
	}

	RemovePropertyBindings(TargetPath, ESearchMode::Exact);
	FStateTreePropertyPath SourcePath = FStateTreePropertyPath(NodeID, SourcePathSegments);
	PropertyBindings.Emplace(MoveTemp(PropertyFunctionNode), SourcePath, TargetPath);
	return SourcePath;
}

void FStateTreeEditorPropertyBindings::AddPropertyBinding(const FStateTreePropertyPathBinding& Binding)
{
	RemovePropertyBindings(Binding.GetTargetPath(), ESearchMode::Exact);
	PropertyBindings.Add(Binding);
}

void FStateTreeEditorPropertyBindings::RemovePropertyBindings(const FStateTreePropertyPath& TargetPath, ESearchMode SearchMode)
{
	if (SearchMode == ESearchMode::Exact)
	{
		PropertyBindings.RemoveAllSwap([&TargetPath](const FStateTreePropertyPathBinding& Binding)
			{
				return Binding.GetTargetPath() == TargetPath;
			});
	}
	else
	{
		PropertyBindings.RemoveAllSwap([&TargetPath](const FStateTreePropertyPathBinding& Binding)
			{
				return Binding.GetTargetPath().Includes(TargetPath);
			});
	}
}

void FStateTreeEditorPropertyBindings::CopyBindings(const FGuid FromStructID, const FGuid ToStructID)
{
	// Copy all bindings that target "FromStructID" and retarget them to "ToStructID".
	TArray<FStateTreePropertyPathBinding> NewBindings;
	for (const FStateTreePropertyPathBinding& Binding : PropertyBindings)
	{
		if (Binding.GetTargetPath().GetStructID() == FromStructID)
		{
			NewBindings.Emplace(Binding.GetSourcePath(), FStateTreePropertyPath(ToStructID, Binding.GetTargetPath().GetSegments()));
		}
	}

	PropertyBindings.Append(NewBindings);
}

bool FStateTreeEditorPropertyBindings::HasPropertyBinding(const FStateTreePropertyPath& TargetPath, ESearchMode RemoveMode) const
{
	if (RemoveMode == ESearchMode::Exact)
	{
		return PropertyBindings.ContainsByPredicate([&TargetPath](const FStateTreePropertyPathBinding& Binding)
			{
				return Binding.GetTargetPath() == TargetPath;
			});
	}
	else
	{
		return PropertyBindings.ContainsByPredicate([&TargetPath](const FStateTreePropertyPathBinding& Binding)
			{
				return Binding.GetTargetPath().Includes(TargetPath);
			});
	}
}

const FStateTreePropertyPathBinding* FStateTreeEditorPropertyBindings::FindPropertyBinding(const FStateTreePropertyPath& TargetPath, ESearchMode RemoveMode) const
{
	if (RemoveMode == ESearchMode::Exact)
	{
		return PropertyBindings.FindByPredicate([&TargetPath](const FStateTreePropertyPathBinding& Binding)
			{
				return Binding.GetTargetPath() == TargetPath;
			});
	}
	else
	{
		return PropertyBindings.FindByPredicate([&TargetPath](const FStateTreePropertyPathBinding& Binding)
			{
				return Binding.GetTargetPath().Includes(TargetPath);
			});
	}
}

const FStateTreePropertyPath* FStateTreeEditorPropertyBindings::GetPropertyBindingSource(const FStateTreePropertyPath& TargetPath) const
{
	const FStateTreePropertyPathBinding* Binding = PropertyBindings.FindByPredicate([&TargetPath](const FStateTreePropertyPathBinding& Binding)
		{
			return Binding.GetTargetPath() == TargetPath;
		});
	return Binding ? &Binding->GetSourcePath() : nullptr;
}

void FStateTreeEditorPropertyBindings::GetPropertyBindingsFor(const FGuid StructID, TArray<const FStateTreePropertyPathBinding*>& OutBindings) const
{
	for (const FStateTreePropertyPathBinding& Binding : PropertyBindings)
	{
		if (Binding.GetSourcePath().GetStructID().IsValid() && Binding.GetTargetPath().GetStructID() == StructID)
		{
			OutBindings.Add(&Binding);
		}
	}
}

void FStateTreeEditorPropertyBindings::RemoveUnusedBindings(const TMap<FGuid, const FStateTreeDataView>& ValidStructs)
{
	PropertyBindings.RemoveAllSwap([ValidStructs](FStateTreePropertyPathBinding& Binding)
		{
			// Remove binding if it's target struct has been removed
			if (!ValidStructs.Contains(Binding.GetTargetPath().GetStructID()))
			{
				// Remove
				return true;
			}

			// Target path should always have at least one segment (copy bind directly on a target struct/object). 
			if (Binding.GetTargetPath().IsPathEmpty())
			{
				return true;
			}

			// Remove binding if path containing instanced indirections (e.g. instance struct or object) cannot be resolved.
			// TODO: Try to use core redirect to find new name.
			{
				const FStateTreeDataView* SourceValue = ValidStructs.Find(Binding.GetSourcePath().GetStructID());
				if (SourceValue && SourceValue->IsValid())
				{
					FString Error;
					TArray<FStateTreePropertyPathIndirection> Indirections;
					if (!Binding.GetSourcePath().ResolveIndirectionsWithValue(*SourceValue, Indirections, &Error))
					{
						UE_LOG(LogStateTree, Verbose, TEXT("Removing binding to %s because binding source path cannot be resolved: %s"),
							*Binding.GetSourcePath().ToString(), *Error); // Error contains the target path.

						// Remove
						return true;
					}
				}
			}
			
			{
				const FStateTreeDataView TargetValue = ValidStructs.FindChecked(Binding.GetTargetPath().GetStructID());
				FString Error;
				TArray<FStateTreePropertyPathIndirection> Indirections;
				if (!Binding.GetTargetPath().ResolveIndirectionsWithValue(TargetValue, Indirections, &Error))
				{
					UE_LOG(LogStateTree, Verbose, TEXT("Removing binding to %s because binding target path cannot be resolved: %s"),
						*Binding.GetSourcePath().ToString(), *Error); // Error contains the target path.

					// Remove
					return true;
				}
			}

			return false;
		});
}

bool FStateTreeEditorPropertyBindings::ContainsAnyStruct(const TSet<const UStruct*>& Structs)
{
	auto PathContainsStruct = [&Structs](const FStateTreePropertyPath& PropertyPath)
	{
		for (const FStateTreePropertyPathSegment& Segment : PropertyPath.GetSegments())
		{
			if (Structs.Contains(Segment.GetInstanceStruct()))
			{
				return true;
			}
		}
		return false;
	};
	
	for (FStateTreePropertyPathBinding& PropertyPathBinding : PropertyBindings)
	{
		if (PathContainsStruct(PropertyPathBinding.GetSourcePath()))
		{
			return true;
		}
		if (PathContainsStruct(PropertyPathBinding.GetTargetPath()))
		{
			return true;
		}
	}
	
	return false;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void FStateTreeEditorPropertyBindings::AddPropertyBinding(const FStateTreeEditorPropertyPath& SourcePath, const FStateTreeEditorPropertyPath& TargetPath)
{
	AddPropertyBinding(UE::StateTree::Private::ConvertEditorPath(SourcePath), UE::StateTree::Private::ConvertEditorPath(TargetPath));
}

void FStateTreeEditorPropertyBindings::RemovePropertyBindings(const FStateTreeEditorPropertyPath& TargetPath)
{
	RemovePropertyBindings(UE::StateTree::Private::ConvertEditorPath(TargetPath), ESearchMode::Exact);
}

bool FStateTreeEditorPropertyBindings::HasPropertyBinding(const FStateTreeEditorPropertyPath& TargetPath) const
{
	return HasPropertyBinding(UE::StateTree::Private::ConvertEditorPath(TargetPath), ESearchMode::Exact);
}

const FStateTreeEditorPropertyPath* FStateTreeEditorPropertyBindings::GetPropertyBindingSource(const FStateTreeEditorPropertyPath& TargetPath) const
{
	static FStateTreeEditorPropertyPath Dummy;
	const FStateTreePropertyPath* SourcePath = GetPropertyBindingSource(UE::StateTree::Private::ConvertEditorPath(TargetPath));
	if (SourcePath != nullptr)
	{
		Dummy = UE::StateTree::Private::ConvertEditorPath(*SourcePath);
		return &Dummy;
	}
	return nullptr;
}

void FStateTreeEditorPropertyBindings::GetPropertyBindingsFor(const FGuid StructID, TArray<FStateTreePropertyPathBinding>& OutBindings) const
{
	TArray<const FStateTreePropertyPathBinding*> NodeBindings;
	GetPropertyBindingsFor(StructID, NodeBindings);
	Algo::Transform(NodeBindings, OutBindings, [](const FStateTreePropertyPathBinding* BindingPtr) { return *BindingPtr; });
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FStateTreeEditorPropertyBindings::RemoveUnusedBindings(const TMap<FGuid, const UStruct*>& ValidStructs)
{
	PropertyBindings.RemoveAllSwap([ValidStructs](const FStateTreePropertyPathBinding& Binding)
		{
			// Remove binding if it's target struct has been removed
			if (!ValidStructs.Contains(Binding.GetTargetPath().GetStructID()))
			{
				return true;
			}

			// Target path should always have at least one segment (copy bind directly on a target struct/object). 
			if (Binding.GetTargetPath().IsPathEmpty())
			{
				return true;
			}
		
			// Remove binding if path containing instanced indirections (e.g. instance struct or object) cannot be resolved.
			// TODO: Try to use core redirect to find new name.
			const UStruct* Struct = ValidStructs.FindChecked(Binding.GetTargetPath().GetStructID());
			TArray<FStateTreePropertyPathIndirection> Indirections;
			if (!Binding.GetTargetPath().ResolveIndirections(Struct, Indirections))
			{
				// Remove
				return true;
			}

			return false;
		});
}


//////////////////////////////////////////////////////////////////////////

FStateTreeBindingLookup::FStateTreeBindingLookup(const IStateTreeEditorPropertyBindingsOwner* InBindingOwner)
	: BindingOwner(InBindingOwner)
{
}

const FStateTreePropertyPath* FStateTreeBindingLookup::GetPropertyBindingSource(const FStateTreePropertyPath& InTargetPath) const
{
	check(BindingOwner);
	const FStateTreeEditorPropertyBindings* EditorBindings = BindingOwner->GetPropertyEditorBindings();
	check(EditorBindings);

	return EditorBindings->GetPropertyBindingSource(InTargetPath);
}

FText FStateTreeBindingLookup::GetPropertyPathDisplayName(const FStateTreePropertyPath& InPath, EStateTreeNodeFormatting Formatting) const
{
	check(BindingOwner);
	const FStateTreeEditorPropertyBindings* EditorBindings = BindingOwner->GetPropertyEditorBindings();
	check(EditorBindings);

	FString StructName;
	int32 FirstSegmentToStringify = 0;

	// If path's struct is a PropertyFunction, let it override a display name.
	{
		const FStateTreePropertyPathBinding* BindingToPath = EditorBindings->GetBindings().FindByPredicate([&InPath](const FStateTreePropertyPathBinding& Binding)
		{
			return Binding.GetSourcePath() == InPath;
		});

		if (BindingToPath && BindingToPath->GetPropertyFunctionNode().IsValid())
		{
			const FConstStructView PropertyFuncEditorNodeView = BindingToPath->GetPropertyFunctionNode();
			const FStateTreeEditorNode& EditorNode = PropertyFuncEditorNodeView.Get<const FStateTreeEditorNode>();

			if (!EditorNode.Node.IsValid())
			{
				return LOCTEXT("Unlinked", "???");
			}

			const FStateTreeNodeBase& Node = EditorNode.Node.Get<FStateTreeNodeBase>();

			// Skipping an output property if there's only one of them.
			if (UE::StateTree::GetStructSingleOutputProperty(*Node.GetInstanceDataType()))
			{
				FirstSegmentToStringify = 1;
			}

			const FText Description = Node.GetDescription(BindingToPath->GetSourcePath().GetStructID(), EditorNode.GetInstance(), *this, Formatting);
			if (!Description.IsEmpty())
			{
				StructName = Description.ToString();
			}
		}
	
		if (StructName.IsEmpty())
		{
			FStateTreeBindableStructDesc Struct;
			if (BindingOwner->GetStructByID(InPath.GetStructID(), Struct))
			{
				StructName = Struct.Name.ToString();
			}
		}
	}

	FString Result = MoveTemp(StructName);
	if (InPath.NumSegments() > FirstSegmentToStringify)
	{
		Result += TEXT(".") + InPath.ToString(/*HighlightedSegment*/ INDEX_NONE, /*HighlightPrefix*/ nullptr, /*HighlightPostfix*/ nullptr, /*bOutputInstances*/ false, FirstSegmentToStringify);
	}

	return FText::FromString(Result);
}

FText FStateTreeBindingLookup::GetBindingSourceDisplayName(const FStateTreePropertyPath& InTargetPath, EStateTreeNodeFormatting Formatting) const
{
	check(BindingOwner);
	const FStateTreeEditorPropertyBindings* EditorBindings = BindingOwner->GetPropertyEditorBindings();
	check(EditorBindings);

	// Check if the target property is bound, if so, return binding description.
	if (const FStateTreePropertyPath* SourcePath = GetPropertyBindingSource(InTargetPath))
	{
		return GetPropertyPathDisplayName(*SourcePath, Formatting);
	}

	// Check if it's bound to context data.
	const UStruct* TargetStruct = nullptr;
	const FProperty* TargetProperty = nullptr;
	EStateTreePropertyUsage Usage = EStateTreePropertyUsage::Invalid;
	
	FStateTreeBindableStructDesc TargetStructDesc;
	if (BindingOwner->GetStructByID(InTargetPath.GetStructID(), TargetStructDesc))
	{
		TArray<FStateTreePropertyPathIndirection> Indirection;
		if (InTargetPath.ResolveIndirections(TargetStructDesc.Struct, Indirection)
			&& Indirection.Num() > 0)
		{
			const FStateTreePropertyPathIndirection& Leaf = Indirection.Last(); 
			TargetProperty = Leaf.GetProperty();
			if (TargetProperty)
			{
				Usage = UE::StateTree::GetUsageFromMetaData(TargetProperty);
			}
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(TargetProperty))
			{
				TargetStruct = StructProperty->Struct;
			}
			if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(TargetProperty))
			{
				TargetStruct = ObjectProperty->PropertyClass;
			}
		}
	}

	if (Usage == EStateTreePropertyUsage::Context)
	{
		if (TargetStruct)
		{
			const FStateTreeBindableStructDesc Desc = BindingOwner->FindContextData(TargetStruct, TargetProperty->GetName());
			if (Desc.IsValid())
			{
				// Connected
				return FText::FromName(Desc.Name);
			}
		}
		return LOCTEXT("Unlinked", "???");
	}

	// Not a binding nor context data.
	return FText::GetEmpty();
}

const FProperty* FStateTreeBindingLookup::GetPropertyPathLeafProperty(const FStateTreePropertyPath& InPath) const
{
	check(BindingOwner);
	const FStateTreeEditorPropertyBindings* EditorBindings = BindingOwner->GetPropertyEditorBindings();
	check(EditorBindings);

	const FProperty* Result = nullptr;
	FStateTreeBindableStructDesc Struct;
	if (BindingOwner->GetStructByID(InPath.GetStructID(), Struct))
	{
		TArray<FStateTreePropertyPathIndirection> Indirection;
		if (InPath.ResolveIndirections(Struct.Struct, Indirection) && Indirection.Num() > 0)
		{
			return Indirection.Last().GetProperty();
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE