// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeBindingExtension.h"
#include "EdGraphSchema_K2.h"
#include "Features/IModularFeatures.h"
#include "IPropertyAccessEditor.h"
#include "StateTreeAnyEnum.h"
#include "StateTreeCompiler.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeNodeBase.h"
#include "Styling/AppStyle.h"
#include "UObject/EnumProperty.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "StateTreePropertyRef.h"
#include "StateTreePropertyRefHelpers.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "IDetailChildrenBuilder.h"
#include "IStructureDataProvider.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyBagDetails.h"
#include "ScopedTransaction.h"
#include "StateTreeEditorNodeUtils.h"
#include "StateTreeEditorModule.h"
#include "StateTreeNodeClassCache.h"
#include "StateTreePropertyFunctionBase.h"
#include "StateTreeEditorData.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

namespace UE::StateTree::PropertyBinding
{

/** Information for the types gathered from a FStateTreePropertyRef property meta-data */
struct FRefTypeInfo
{
	/** Display Name Text of the Ref Type */
	FText TypeNameText;

	/** Ref Type expressed as a Pin Type */
	FEdGraphPinType PinType;
};

const FName StateTreeNodeIDName(TEXT("StateTreeNodeID"));
const FName AllowAnyBindingName(TEXT("AllowAnyBinding"));

UObject* FindEditorBindingsOwner(UObject* InObject)
{
	UObject* Result = nullptr;

	for (UObject* Outer = InObject; Outer; Outer = Outer->GetOuter())
	{
		IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(Outer);
		if (BindingOwner)
		{
			Result = Outer;
			break;
		}
	}
	return Result;
}

UStruct* ResolveLeafValueStructType(FStateTreeDataView ValueView, TConstArrayView<FBindingChainElement> InBindingChain)
{
	if (ValueView.GetMemory() == nullptr)
	{
		return nullptr;
	}
	
	FStateTreePropertyPath Path;

	for (const FBindingChainElement& Element : InBindingChain)
	{
		if (const FProperty* Property = Element.Field.Get<FProperty>())
		{
			Path.AddPathSegment(Property->GetFName(), Element.ArrayIndex);
		}
		else if (const UFunction* Function = Element.Field.Get<UFunction>())
		{
			// Cannot handle function calls
			return nullptr;
		}
	}

	TArray<FStateTreePropertyPathIndirection> Indirections;
	if (!Path.ResolveIndirectionsWithValue(ValueView, Indirections)
		|| Indirections.IsEmpty())
	{
		return nullptr;
	}

	// Last indirection points to the value of the leaf property, check the type.
	const FStateTreePropertyPathIndirection& LastIndirection = Indirections.Last();

	UStruct* Result = nullptr;

	if (LastIndirection.GetContainerAddress())
	{
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(LastIndirection.GetProperty()))
		{
			// Get the type of the instanced struct's value.
			if (StructProperty->Struct == TBaseStructure<FInstancedStruct>::Get())
			{
				const FInstancedStruct& InstancedStruct = *reinterpret_cast<const FInstancedStruct*>(LastIndirection.GetPropertyAddress());
				Result = const_cast<UScriptStruct*>(InstancedStruct.GetScriptStruct());
			}
		}
		else if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(LastIndirection.GetProperty()))
		{
			// Get type of the instanced object.
			if (const UObject* Object = *reinterpret_cast<UObject* const*>(LastIndirection.GetPropertyAddress()))
			{
				Result = Object->GetClass();
			}
		}
	}

	return Result;
}

void MakeStructPropertyPathFromBindingChain(const FGuid StructID, TConstArrayView<FBindingChainElement> InBindingChain, FStateTreeDataView DataView, FStateTreePropertyPath& OutPath)
{
	OutPath.Reset();
	OutPath.SetStructID(StructID);
	
	for (const FBindingChainElement& Element : InBindingChain)
	{
		if (const FProperty* Property = Element.Field.Get<FProperty>())
		{
			OutPath.AddPathSegment(Property->GetFName(), Element.ArrayIndex);
		}
		else if (const UFunction* Function = Element.Field.Get<UFunction>())
		{
			OutPath.AddPathSegment(Function->GetFName());
		}
	}

	OutPath.UpdateSegmentsFromValue(DataView);
}

EStateTreePropertyUsage MakeStructPropertyPathFromPropertyHandle(TSharedPtr<const IPropertyHandle> InPropertyHandle, FStateTreePropertyPath& OutPath)
{
	OutPath.Reset();

	FGuid StructID;
	TArray<FStateTreePropertyPathSegment> PathSegments;
	EStateTreePropertyUsage ResultUsage = EStateTreePropertyUsage::Invalid; 

	TSharedPtr<const IPropertyHandle> CurrentPropertyHandle = InPropertyHandle;
	while (CurrentPropertyHandle.IsValid())
	{
		const FProperty* Property = CurrentPropertyHandle->GetProperty();
		if (Property)
		{
			FStateTreePropertyPathSegment& Segment = PathSegments.InsertDefaulted_GetRef(0); // Traversing from leaf to root, insert in reverse.

			// Store path up to the property which has ID.
			Segment.SetName(Property->GetFName());
			Segment.SetArrayIndex(CurrentPropertyHandle->GetIndexInArray());

			// Store type of the object (e.g. for instanced objects or instanced structs).
			if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
			{
				if (ObjectProperty->HasAnyPropertyFlags(CPF_PersistentInstance | CPF_InstancedReference))
				{
					const UObject* Object = nullptr;
					if (CurrentPropertyHandle->GetValue(Object) == FPropertyAccess::Success)
					{
						if (Object)
						{
							Segment.SetInstanceStruct(Object->GetClass());
						}
					}
				}
			}
			else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (StructProperty->Struct == TBaseStructure<FInstancedStruct>::Get())
				{
					void* Address = nullptr;
					if (CurrentPropertyHandle->GetValueData(Address) == FPropertyAccess::Success)
					{
						if (Address)
						{
							FInstancedStruct& Struct = *static_cast<FInstancedStruct*>(Address);
							Segment.SetInstanceStruct(Struct.GetScriptStruct());
						}
					}
				}
			}

			// Array access is represented as: "Array, PropertyInArray[Index]", we're traversing from leaf to root, skip the node without index.
			// Advancing the node before ID test, since the array is on the instance data, the ID will be on the Array node.
			if (Segment.GetArrayIndex() != INDEX_NONE)
			{
				TSharedPtr<const IPropertyHandle> ParentPropertyHandle = CurrentPropertyHandle->GetParentHandle();
				if (ParentPropertyHandle.IsValid())
				{
					const FProperty* ParentProperty = ParentPropertyHandle->GetProperty();
					if (ParentProperty
						&& ParentProperty->IsA<FArrayProperty>()
						&& Property->GetFName() == ParentProperty->GetFName())
					{
						CurrentPropertyHandle = ParentPropertyHandle;
					}
				}
			}

			// Bindable property must have node ID
			if (const FString* IDString = CurrentPropertyHandle->GetInstanceMetaData(UE::StateTree::PropertyBinding::StateTreeNodeIDName))
			{
				LexFromString(StructID, **IDString);
				ResultUsage = UE::StateTree::GetUsageFromMetaData(Property);
				break;
			}
		}
		
		CurrentPropertyHandle = CurrentPropertyHandle->GetParentHandle();
	}

	if (!StructID.IsValid())
	{
		ResultUsage = EStateTreePropertyUsage::Invalid;
	}
	else
	{
		OutPath = FStateTreePropertyPath(StructID, PathSegments);
	}

	return ResultUsage;
}

// @todo: there's a similar function in StateTreeNodeDetails.cpp, merge.
FText GetPropertyTypeText(const FProperty* Property)
{
	FEdGraphPinType PinType;
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	Schema->ConvertPropertyToPinType(Property, PinType);
				
	const FName PinSubCategory = PinType.PinSubCategory;
	const UObject* PinSubCategoryObject = PinType.PinSubCategoryObject.Get();
	if (PinSubCategory != UEdGraphSchema_K2::PSC_Bitmask && PinSubCategoryObject)
	{
		if (const UField* Field = Cast<const UField>(PinSubCategoryObject))
		{
			return Field->GetDisplayNameText();
		}
		return FText::FromString(PinSubCategoryObject->GetName());
	}

	return UEdGraphSchema_K2::GetCategoryText(PinType.PinCategory, NAME_None, true);
}

TSharedRef<SWidget> MakeContextStructWidget(const FStateTreeBindableStructDesc& InContextStruct)
{
	FEdGraphPinType PinType;

	UStruct* Struct = const_cast<UStruct*>(InContextStruct.Struct.Get());

	if (UClass* Class = Cast<UClass>(Struct))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategory = NAME_None;
		PinType.PinSubCategoryObject = Class;
	}
	else if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Struct))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategory = NAME_None;
		PinType.PinSubCategoryObject = ScriptStruct;
	}

	const FSlateBrush* Icon = FBlueprintEditorUtils::GetIconFromPin(PinType, true);
	const FLinearColor IconColor = GetDefault<UEdGraphSchema_K2>()->GetPinTypeColor(PinType);

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SSpacer)
			.Size(FVector2D(18.0f, 0.0f))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(1.0f, 0.0f)
		[
			SNew(SImage)
			.Image(Icon)
			.ColorAndOpacity(IconColor)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromName(InContextStruct.Name))
		];
}

TSharedRef<SWidget> MakeBindingPropertyInfoWidget(const FText& InDisplayText, const FEdGraphPinType& InPinType)
{
	const FSlateBrush* Icon = FBlueprintEditorUtils::GetIconFromPin(InPinType, /*bIsLarge*/true);
	const FLinearColor IconColor = GetDefault<UEdGraphSchema_K2>()->GetPinTypeColor(InPinType);

	return SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SSpacer)
			.Size(FVector2D(18.0f, 0.0f))
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(1.0f, 0.0f)
		[
			SNew(SImage)
			.Image(Icon)
			.ColorAndOpacity(IconColor)
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(InDisplayText)
		];
}
	
/** Helper struct to Begin/End Sections */
struct FSectionHelper
{
	FSectionHelper(FMenuBuilder& MenuBuilder)
		: MenuBuilder(MenuBuilder)
	{
	}
	~FSectionHelper()
	{
		if (bSectionOpened)
		{
			MenuBuilder.EndSection();
		}
	}
	void SetSection(const FText& InSection)
	{
		if (!InSection.IdenticalTo(CurrentSection))
		{
			if (bSectionOpened)
			{
				MenuBuilder.EndSection();
			}
			CurrentSection = InSection;
			MenuBuilder.BeginSection(NAME_None, CurrentSection);
			bSectionOpened = true;
		}
	}
private:
	FText CurrentSection;
	FMenuBuilder& MenuBuilder;
	bool bSectionOpened = false;
};

FOnStateTreePropertyBindingChanged STATETREEEDITORMODULE_API OnStateTreePropertyBindingChanged;

struct FCachedBindingData : public TSharedFromThis<FCachedBindingData>
{
	FCachedBindingData(UObject* InOwnerObject, const FStateTreePropertyPath& InTargetPath, const TSharedPtr<const IPropertyHandle>& InPropertyHandle, TArrayView<FStateTreeBindableStructDesc> InAccessibleStructs)
		: WeakOwnerObject(InOwnerObject)
		, TargetPath(InTargetPath)
		, PropertyHandle(InPropertyHandle)
		, AccessibleStructs(InAccessibleStructs)
	{
	}

	void AddBinding(TConstArrayView<FBindingChainElement> InBindingChain)
	{
		if (InBindingChain.IsEmpty())
		{
			return;
		}
		
		if (!TargetPath.GetStructID().IsValid())
		{
			return;
		}

		UObject* OwnerObject = WeakOwnerObject.Get();
		if (!OwnerObject)
		{
			return;
		}
		
		IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(OwnerObject);
		if (!BindingOwner)
		{
			return;
		}

		FStateTreeEditorPropertyBindings* EditorBindings = BindingOwner->GetPropertyEditorBindings();
		if (!EditorBindings)
		{
			return;
		}

		// First item in the binding chain is the index in AccessibleStructs.
		const int32 SourceStructIndex = InBindingChain[0].ArrayIndex;
		check(SourceStructIndex >= 0 && SourceStructIndex < AccessibleStructs.Num());

		const FStateTreeBindableStructDesc& BindableStruct = AccessibleStructs[SourceStructIndex];
				
		TConstArrayView<FBindingChainElement> SourceBindingChain(InBindingChain.begin() + 1, InBindingChain.Num() - 1); // remove struct index.

		FStateTreeDataView DataView;
		BindingOwner->GetDataViewByID(BindableStruct.ID, DataView);

		// If SourceBindingChain is empty at this stage, it means that the binding points to the source struct itself.
		FStateTreePropertyPath SourcePath;
		UE::StateTree::PropertyBinding::MakeStructPropertyPathFromBindingChain(BindableStruct.ID, SourceBindingChain, DataView, SourcePath);

		OwnerObject->Modify();

		if (BindableStruct.DataSource == EStateTreeBindableStructSource::PropertyFunction)
		{
			const UScriptStruct* PropertyFunctionNodeStruct = nullptr;

			BindingOwner->EnumerateBindablePropertyFunctionNodes([ID = BindableStruct.ID, &PropertyFunctionNodeStruct](const UScriptStruct* NodeStruct, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
			{
				if (Desc.ID == ID)
				{
					PropertyFunctionNodeStruct = NodeStruct;
					return EStateTreeVisitor::Break;
				}

				return EStateTreeVisitor::Continue;
			});

			if (ensure(PropertyFunctionNodeStruct))
			{
				// If there are no segments, bindings leads directly into source struct's single output property. It's path has to be recovered.
				if (SourcePath.NumSegments() == 0)
				{
					const FProperty* SingleOutputProperty = UE::StateTree::GetStructSingleOutputProperty(*BindableStruct.Struct);
					check(SingleOutputProperty);

					FStateTreePropertyPathSegment SingleOutputPropertySegment = FStateTreePropertyPathSegment(SingleOutputProperty->GetFName());
					SourcePath = EditorBindings->AddFunctionPropertyBinding(PropertyFunctionNodeStruct, {SingleOutputPropertySegment}, TargetPath);
				}
				else
				{
					SourcePath = EditorBindings->AddFunctionPropertyBinding(PropertyFunctionNodeStruct, SourcePath.GetSegments(), TargetPath);
				}
			}
		}
		else
		{
			EditorBindings->AddPropertyBinding(SourcePath, TargetPath);
		}

		UpdateData();

		UE::StateTree::PropertyBinding::OnStateTreePropertyBindingChanged.Broadcast(SourcePath, TargetPath);
	}

	bool HasBinding(FStateTreeEditorPropertyBindings::ESearchMode SearchMode) const
	{
		UObject* OwnerObject = WeakOwnerObject.Get();
		if (!OwnerObject)
		{
			return false;
		}

		IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(OwnerObject);
		if (!BindingOwner)
		{
			return false;
		}

		FStateTreeEditorPropertyBindings* EditorBindings = BindingOwner->GetPropertyEditorBindings();
		if (!EditorBindings)
		{
			return false;
		}

		return EditorBindings && EditorBindings->HasPropertyBinding(TargetPath, SearchMode);
	}

	void RemoveBinding(FStateTreeEditorPropertyBindings::ESearchMode RemoveMode)
	{
		UObject* OwnerObject = WeakOwnerObject.Get();
		if (!OwnerObject)
		{
			return;
		}
		
		IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(OwnerObject);
		if (!BindingOwner)
		{
			return;
		}

		FStateTreeEditorPropertyBindings* EditorBindings = BindingOwner->GetPropertyEditorBindings();
		if (!EditorBindings)
		{
			return;
		}

		OwnerObject->Modify();
		EditorBindings->RemovePropertyBindings(TargetPath, RemoveMode);

		UpdateData();
		
		const FStateTreePropertyPath SourcePath; // Null path
		UE::StateTree::PropertyBinding::OnStateTreePropertyBindingChanged.Broadcast(SourcePath, TargetPath);
	}

	bool CanCreateParameter(const FStateTreeBindableStructDesc& InStructDesc, TArray<TSharedPtr<const FRefTypeInfo>>& OutRefTypeInfos) const
	{
		const FProperty* Property = PropertyHandle->GetProperty();
		if (!Property)
		{
			return false;
		}

		IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(WeakOwnerObject.Get());
		if (!BindingOwner)
		{
			return false;
		}

		if (!BindingOwner->CanCreateParameter(InStructDesc.ID))
		{
			return false;
		}

		// Add the PropertyRef property type with its RefTypes
		const FStructProperty* StructProperty = CastField<const FStructProperty>(Property);
		if (StructProperty && StructProperty->Struct && StructProperty->Struct->IsChildOf(FStateTreePropertyRef::StaticStruct()))
		{
			TArray<FEdGraphPinType, TInlineAllocator<1>> PinTypes;

			const bool bCanTargetRefArray = PropertyHandle->HasMetaData(PropertyRefHelpers::CanRefToArrayName);

			if (StructProperty->Struct->IsChildOf(FStateTreeBlueprintPropertyRef::StaticStruct()))
			{
				void* PropertyRefAddress = nullptr;
				if (PropertyHandle->GetValueData(PropertyRefAddress) == FPropertyAccess::Result::Success)
				{
					check(PropertyRefAddress);
					PinTypes.Add(PropertyRefHelpers::GetBlueprintPropertyRefInternalTypeAsPin(*static_cast<const FStateTreeBlueprintPropertyRef*>(PropertyRefAddress)));
				}
			}
			else
			{
				PinTypes = PropertyRefHelpers::GetPropertyRefInternalTypesAsPins(*Property);
			}

			// If Property supports Arrays, add the Array version of these pin types
			if (PropertyHandle->HasMetaData(PropertyRefHelpers::CanRefToArrayName))
			{
				const int32 PinTypeNum = PinTypes.Num();
				for (int32 Index = 0; Index < PinTypeNum; ++Index)
				{
					const FEdGraphPinType& SourcePinType = PinTypes[Index];
					if (!SourcePinType.IsArray())
					{
						FEdGraphPinType& PinType = PinTypes.Emplace_GetRef(SourcePinType);
						PinType.ContainerType = EPinContainerType::Array;
					}
				}
			}

			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

			for (const FEdGraphPinType& PinType : PinTypes)
			{
				TSharedRef<FRefTypeInfo> RefTypeInfo = MakeShared<FRefTypeInfo>();
				RefTypeInfo->PinType = PinType;

				FString TypeName;
				if(UObject* SubCategoryObject = PinType.PinSubCategoryObject.Get()) 
				{
					TypeName = SubCategoryObject->GetName();
				}
				else
				{
					TypeName = PinType.PinCategory.ToString() + TEXT(" ") + PinType.PinSubCategory.ToString();
				}

				RefTypeInfo->TypeNameText = FText::FromString(TypeName);
				OutRefTypeInfos.Emplace(MoveTemp(RefTypeInfo));
			}
		}

		return true;
	}

	void PromoteToParameter(FName InPropertyName, FStateTreeBindableStructDesc InStructDesc, TSharedPtr<const FRefTypeInfo> InPropertyInfoOverride)
	{
		if (!TargetPath.GetStructID().IsValid())
		{
			return;
		}
		
		UObject* OwnerObject = WeakOwnerObject.Get();
		if (!OwnerObject)
		{
			return;
		}

		IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(OwnerObject);
		if (!BindingOwner)
		{
			return;
		}

		const FProperty* Property = PropertyHandle->GetProperty();
		if (!Property)
		{
			return;
		}

		const FProperty* TargetProperty = nullptr;
		const void* TargetContainerAddress = nullptr;

		FStateTreeDataView TargetDataView;
		if (BindingOwner->GetDataViewByID(TargetPath.GetStructID(), TargetDataView) && TargetDataView.IsValid())
		{
			TArray<FStateTreePropertyPathIndirection> TargetIndirections;
			if (ensure(TargetPath.ResolveIndirectionsWithValue(TargetDataView, TargetIndirections)))
			{
				const FStateTreePropertyPathIndirection& LastIndirection = TargetIndirections.Last();
				TargetProperty = LastIndirection.GetProperty();
				TargetContainerAddress = LastIndirection.GetContainerAddress();
			}
		}

		FStateTreeEditorPropertyBindings* EditorBindings = BindingOwner->GetPropertyEditorBindings();
		if (!EditorBindings)
		{
			return;
		}

		const FGuid StructID = InStructDesc.ID;

		const FScopedTransaction Transaction(LOCTEXT("PromoteToParameter", "Promote to Parameter"));

		TArray<FStateTreeEditorPropertyCreationDesc, TFixedAllocator<1>> PropertyCreationDescs;
		{
			FStateTreeEditorPropertyCreationDesc& PropertyCreationDesc = PropertyCreationDescs.AddDefaulted_GetRef();

			if (InPropertyInfoOverride)
			{
				PropertyCreationDesc.PropertyDesc.Name = InPropertyName;
				UE::StructUtils::SetPropertyDescFromPin(PropertyCreationDesc.PropertyDesc, InPropertyInfoOverride->PinType);
			}
			else
			{
				PropertyCreationDesc.PropertyDesc = FPropertyBagPropertyDesc(InPropertyName, Property);
			}

			// Create desc based on the Target Property, but without the meta-data.
			// This functionality mirrors the user action of adding a new property from the UI, where meta-data is not available.
			// Additionally, meta-data like EditCondition is not desirable here
			PropertyCreationDesc.PropertyDesc.MetaClass = nullptr;
			PropertyCreationDesc.PropertyDesc.MetaData.Reset();

			// Set the Property & Container Address to copy
			if (TargetProperty && TargetContainerAddress)
			{
				PropertyCreationDesc.SourceProperty = TargetProperty;
				PropertyCreationDesc.SourceContainerAddress = TargetContainerAddress;
			}
		}

		OwnerObject->Modify();
		BindingOwner->CreateParameters(StructID, /*InOut*/PropertyCreationDescs);

		// Use the name in PropertyDescs, as it might contain a different name than the desired InPropertyName (for uniquness)
		FStateTreePropertyPath SourcePath(StructID, PropertyCreationDescs[0].PropertyDesc.Name);
		EditorBindings->AddPropertyBinding(SourcePath, TargetPath);

		UpdateData();
		UE::StateTree::PropertyBinding::OnStateTreePropertyBindingChanged.Broadcast(SourcePath, TargetPath);
	}

	void UpdateData()
	{
		static FName PropertyIcon(TEXT("Kismet.Tabs.Variables"));

		SourceStructName = FText::GetEmpty();
		FormatableText = FText::GetEmpty();
		FormatableTooltipText = FText::GetEmpty();
		Color = FLinearColor::White;
		Image = nullptr;

		if (!PropertyHandle.IsValid())
		{
			return;
		}

		const FProperty* Property = PropertyHandle->GetProperty();
		if (!Property)
		{
			return;
		}

		UObject* OwnerObject = WeakOwnerObject.Get();
		if (!OwnerObject)
		{
			return;
		}
		
		IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(OwnerObject);
		if (!BindingOwner)
		{
			return;
		}

		FStateTreeEditorPropertyBindings* EditorBindings = BindingOwner->GetPropertyEditorBindings();
		if (!EditorBindings)
		{
			return;
		}

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		check(Schema);

		FStateTreeDataView TargetDataView;
		BindingOwner->GetDataViewByID(TargetPath.GetStructID(), TargetDataView);

		FEdGraphPinType PinType;
		const bool bIsPropertyRef = UE::StateTree::PropertyRefHelpers::IsPropertyRef(*Property);
		if (bIsPropertyRef && TargetDataView.IsValid())
		{
			// Use internal type to construct PinType if it's property of PropertyRef type.
			TArray<FStateTreePropertyPathIndirection> TargetIndirections;
			if (ensure(TargetPath.ResolveIndirectionsWithValue(TargetDataView, TargetIndirections)))
			{
				const uint8* PropertyRef = TargetIndirections.Last().GetPropertyAddress();
				PinType = UE::StateTree::PropertyRefHelpers::GetPropertyRefInternalTypeAsPin(*Property, PropertyRef);
			}
		}
		else
		{
			Schema->ConvertPropertyToPinType(Property, PinType);
		}


		FTextBuilder TooltipBuilder;
		const FStateTreePropertyPathBinding* CurrentBinding = EditorBindings->GetBindings().FindByPredicate([this](const FStateTreePropertyPathBinding& Binding)
		{
			return Binding.GetTargetPath() == TargetPath;
		});

		if (CurrentBinding)
		{
			const FStateTreePropertyPath& SourcePath = CurrentBinding->GetSourcePath();
			FString SourcePropertyPathAsString = SourcePath.ToString();

			// If source is a bound PropertyFunction, it will not be present in AccessibleStructs thus it has to be accessed through bindings owner.
			FStateTreeBindableStructDesc SourceDesc;
			if (BindingOwner->GetStructByID(SourcePath.GetStructID(), SourceDesc))
			{
				// Making first segment of the path invisible for the user if it's property function's single output property.
				if (SourceDesc.DataSource == EStateTreeBindableStructSource::PropertyFunction && UE::StateTree::GetStructSingleOutputProperty(*SourceDesc.Struct))
				{
					SourcePropertyPathAsString = SourcePath.ToString(/*HighlightedSegment*/ INDEX_NONE, /*HighlightPrefix*/ nullptr, /*HighlightPostfix*/ nullptr, /*bOutputInstances*/ false, 1);
				}

				// Check that the binding is valid.
				bool bIsValidBinding = false;
				FStateTreeDataView SourceDataView;
				const FProperty* SourceLeafProperty = nullptr;
				const UStruct* SourceStruct = nullptr;
				if (BindingOwner->GetDataViewByID(SourcePath.GetStructID(), SourceDataView)
					&& TargetDataView.IsValid())
				{
					TArray<FStateTreePropertyPathIndirection> SourceIndirections;
					TArray<FStateTreePropertyPathIndirection> TargetIndirections;

					// Resolve source and target properties.
					// Source path can be empty, when the binding binds directly to a context struct/class.
					// Target path must always point to a valid property (at least one indirection).
					if (SourcePath.ResolveIndirectionsWithValue(SourceDataView, SourceIndirections)
						&& TargetPath.ResolveIndirectionsWithValue(TargetDataView, TargetIndirections)
						&& !TargetIndirections.IsEmpty())
					{
						const FStateTreePropertyPathIndirection TargetLeafIndirection = TargetIndirections.Last();
						if (SourceIndirections.Num() > 0)
						{
							// Binding to a source property.
							const FStateTreePropertyPathIndirection SourceLeafIndirection = SourceIndirections.Last();
							SourceLeafProperty = SourceLeafIndirection.GetProperty();
							bIsValidBinding = ArePropertiesCompatible(SourceLeafProperty, TargetLeafIndirection.GetProperty(), SourceLeafIndirection.GetPropertyAddress(), TargetLeafIndirection.GetPropertyAddress());
						}
						else
						{
							// Binding to a source context struct.
							SourceStruct = SourceDataView.GetStruct();
							bIsValidBinding = ArePropertyAndContextStructCompatible(SourceStruct, TargetLeafIndirection.GetProperty());
						}
					}
				}

				FormatableText = FText::FormatNamed(LOCTEXT("ValidSourcePath", "{SourceStruct}{PropertyPath}"), TEXT("PropertyPath"), SourcePropertyPathAsString.IsEmpty() ? FText() : FText::FromString(TEXT(".") + SourcePropertyPathAsString));
				SourceStructName = FText::FromString(SourceDesc.Name.ToString());

				if (bIsValidBinding)
				{
					if (SourcePropertyPathAsString.IsEmpty())
					{
						if (CurrentBinding->GetPropertyFunctionNode().IsValid())
						{
							TooltipBuilder.AppendLine(LOCTEXT("ExistingBindingToFunctionTooltip", "Property is bound to function {SourceStruct}."));
						}
						else
						{
							TooltipBuilder.AppendLine(LOCTEXT("ExistingBindingTooltip", "Property is bound to {SourceStruct}."));
						}
					}
					else
					{
						if (CurrentBinding->GetPropertyFunctionNode().IsValid())
						{
							TooltipBuilder.AppendLineFormat(LOCTEXT("ExistingBindingToFunctionWithPropertyTooltip", "Property is bound to function {SourceStruct} property {PropertyPath}."), {{TEXT("PropertyPath"), FText::FromString(SourcePropertyPathAsString)}});
						}
						else
						{
							TooltipBuilder.AppendLineFormat(LOCTEXT("ExistingBindingWithPropertyTooltip", "Property is bound to {SourceStruct} property {PropertyPath}."), {{TEXT("PropertyPath"), FText::FromString(SourcePropertyPathAsString)}});
						}
					}

					if (bIsPropertyRef) // Update the pin type with source property so that property ref that can binds to multiple types display the binded one.
					{
						Schema->ConvertPropertyToPinType(SourceLeafProperty, PinType);
					}

					Image = FAppStyle::GetBrush(PropertyIcon);
					Color = Schema->GetPinTypeColor(PinType);
				}
				else
				{
					FText SourceType;
					if (SourceLeafProperty)
					{
						SourceType = UE::StateTree::PropertyBinding::GetPropertyTypeText(SourceLeafProperty);
					}
					else if (SourceStruct)
					{
						SourceType = SourceStruct->GetDisplayNameText();
					}
					FText TargetType = UE::StateTree::PropertyBinding::GetPropertyTypeText(Property);

					if (SourcePath.IsPathEmpty())
					{
						TooltipBuilder.AppendLineFormat(LOCTEXT("MismatchingBindingTooltip", "Property is bound to {SourceStruct}, but binding source type '{SourceType}' does not match property type '{TargetType}'."),
							{
								{TEXT("SourceType"), SourceType},
								{TEXT("TargetType"), TargetType}
							});
					}
					else
					{
						TooltipBuilder.AppendLineFormat(LOCTEXT("MismatchingBindingTooltipWithProperty", "Property is bound to {SourceStruct} property {PropertyPath}, but binding source type '{SourceType}' does not match property type '{TargetType}'."),
							{
								{TEXT("PropertyPath"), FText::FromString(SourcePropertyPathAsString)},
								{TEXT("SourceType"), SourceType},
								{TEXT("TargetType"), TargetType}
							});
					}

					Image = FCoreStyle::Get().GetBrush("Icons.ErrorWithColor");
					Color = FLinearColor::White;
				}
			}
			else
			{
				// Missing source
				FormatableText = FText::Format(LOCTEXT("MissingSource", "???.{0}"), FText::FromString(SourcePropertyPathAsString));
				TooltipBuilder.AppendLineFormat(LOCTEXT("MissingBindingTooltip", "Missing binding source for property path '{0}'."), FText::FromString(SourcePropertyPathAsString));
				Image = FCoreStyle::Get().GetBrush("Icons.ErrorWithColor");
				Color = FLinearColor::White;
			}

			CachedSourcePath = SourcePath;
		}
		else
		{
			// No bindings
			FormatableText = FText::GetEmpty();
			TooltipBuilder.AppendLineFormat(LOCTEXT("BindTooltip", "Bind {0} to value from another property."), UE::StateTree::PropertyBinding::GetPropertyTypeText(Property));

			Image = FAppStyle::GetBrush(PropertyIcon);
			Color = Schema->GetPinTypeColor(PinType);

			CachedSourcePath.Reset();
		}

		if (bIsPropertyRef)
		{
			if (Property->HasMetaData(UE::StateTree::PropertyRefHelpers::IsRefToArrayName))
			{
				TooltipBuilder.AppendLineFormat(LOCTEXT("PropertyRefBindingTooltipArray", "Supported types are Array of {0}"), FText::FromString(Property->GetMetaData(UE::StateTree::PropertyRefHelpers::RefTypeName)));
			}
			else
			{
				TooltipBuilder.AppendLineFormat(LOCTEXT("PropertyRefBindingTooltip", "Supported types are {0}"), FText::FromString(Property->GetMetaData(UE::StateTree::PropertyRefHelpers::RefTypeName)));
				if (Property->HasMetaData(UE::StateTree::PropertyRefHelpers::CanRefToArrayName))
				{
					TooltipBuilder.AppendLine(LOCTEXT("PropertyRefBindingTooltipCanSupportArray", "Supports Arrays"));
				}
			}
		}

		FormatableTooltipText = TooltipBuilder.ToText();

		bIsDataCached = true;
	}

	bool CanBindToContextStruct(const UStruct* InStruct, int32 InStructIndex)
	{
		ConditionallyUpdateData();

		// Do not allow to bind directly StateTree nodes
		// @todo: find a way to more specifically call out the context structs, e.g. pass the property path to the callback.
		if (InStruct != nullptr)
		{
			const bool bIsStateTreeNode = AccessibleStructs.ContainsByPredicate([InStruct](const FStateTreeBindableStructDesc& AccessibleStruct)
			{
				return AccessibleStruct.DataSource != EStateTreeBindableStructSource::Context
					&& AccessibleStruct.DataSource != EStateTreeBindableStructSource::Parameter
					&& AccessibleStruct.DataSource != EStateTreeBindableStructSource::TransitionEvent
					&& AccessibleStruct.DataSource != EStateTreeBindableStructSource::StateEvent
					&& AccessibleStruct.DataSource != EStateTreeBindableStructSource::PropertyFunction
					&& AccessibleStruct.Struct == InStruct;
			});

			if (bIsStateTreeNode)
			{
				return false;
			}
		}

		check(AccessibleStructs.IsValidIndex(InStructIndex));
		// Binding directly into PropertyFunction's struct is allowed if it contains a compatible single output property.
		if (AccessibleStructs[InStructIndex].DataSource == EStateTreeBindableStructSource::PropertyFunction)
		{
			IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(WeakOwnerObject.Get());
			FStateTreeDataView DataView;
			// If DataView exists, struct is an instance of already bound function.
			if (BindingOwner == nullptr || BindingOwner->GetDataViewByID(AccessibleStructs[InStructIndex].ID, DataView))
			{
				return false;
			}

			if (const FProperty* SingleOutputProperty = UE::StateTree::GetStructSingleOutputProperty(*AccessibleStructs[InStructIndex].Struct))
			{
				return CanBindToProperty(SingleOutputProperty, {FBindingChainElement(nullptr, InStructIndex), FBindingChainElement(const_cast<FProperty*>(SingleOutputProperty))});
			}
		}

		return ArePropertyAndContextStructCompatible(InStruct, PropertyHandle->GetProperty());
	}
			
	bool CanBindToProperty(const FProperty* SourceProperty, TConstArrayView<FBindingChainElement> InBindingChain)
	{
		ConditionallyUpdateData();

		// Special case for binding widget calling OnCanBindProperty with Args.Property (i.e. self).
		if (PropertyHandle->GetProperty() == SourceProperty)
		{
			return true;
		}

		UObject* OwnerObject = WeakOwnerObject.Get();
		if (!OwnerObject)
		{
			return false;
		}

		IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(OwnerObject);
		if (!BindingOwner)
		{
			return false;
		}

		const int32 SourceStructIndex = InBindingChain[0].ArrayIndex;
		check(AccessibleStructs.IsValidIndex(SourceStructIndex));

		FStateTreeDataView SourceDataView;
		if (AccessibleStructs[SourceStructIndex].DataSource == EStateTreeBindableStructSource::PropertyFunction)
		{
			SourceDataView = FStateTreeDataView(AccessibleStructs[SourceStructIndex].Struct, nullptr);
		}
		else
		{
			BindingOwner->GetDataViewByID(AccessibleStructs[SourceStructIndex].ID, SourceDataView);
		}

		FStateTreePropertyPath SourcePath;
		UE::StateTree::PropertyBinding::MakeStructPropertyPathFromBindingChain(AccessibleStructs[SourceStructIndex].ID, InBindingChain, SourceDataView, SourcePath);

		TArray<FStateTreePropertyPathIndirection> SourceIndirections;
		void* TargetValueAddress = nullptr;
		if (PropertyHandle->GetValueData(TargetValueAddress) == FPropertyAccess::Success && SourcePath.ResolveIndirectionsWithValue(SourceDataView, SourceIndirections))
		{
			return ArePropertiesCompatible(SourceProperty, PropertyHandle->GetProperty(), SourceIndirections.Last().GetPropertyAddress(), TargetValueAddress);
		}
		
		return false;
	}

	bool CanAcceptPropertyOrChildren(const FProperty* SourceProperty, TConstArrayView<FBindingChainElement> InBindingChain)
	{
		if (!SourceProperty)
		{
			return false;
		}

		ConditionallyUpdateData();

		if (!PropertyHandle.IsValid() || PropertyHandle->GetProperty() == nullptr)
		{
			return false;
		}

		const int32 SourceStructIndex = InBindingChain[0].ArrayIndex;
		check(AccessibleStructs.IsValidIndex(SourceStructIndex));
		const FStateTreeBindableStructDesc& StructDesc = AccessibleStructs[SourceStructIndex];

		if (StructDesc.DataSource == EStateTreeBindableStructSource::PropertyFunction)
		{
			IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(WeakOwnerObject.Get());
			FStateTreeDataView DataView;
			// If DataView exists, struct is an instance of already bound function.
			if (BindingOwner == nullptr || BindingOwner->GetDataViewByID(AccessibleStructs[SourceStructIndex].ID, DataView))
			{
				return false;
			}

			// To avoid duplicates, PropertyFunction struct's children are not allowed to be bound if it contains a compatible single output property.
			if (const FProperty* SingleOutputProperty = UE::StateTree::GetStructSingleOutputProperty(*StructDesc.Struct))
			{
				if (CanBindToProperty(SingleOutputProperty, {FBindingChainElement(nullptr, SourceStructIndex), FBindingChainElement(const_cast<FProperty*>(SingleOutputProperty))}))
				{
					return false;
				}
			}

			// Binding to non-output PropertyFunctions properties is not allowed.
			if (InBindingChain.Num() == 1 && UE::StateTree::GetUsageFromMetaData(SourceProperty) != EStateTreePropertyUsage::Output)
			{
				return false;
			}
		}

		if (UE::StateTree::PropertyRefHelpers::IsPropertyRef(*PropertyHandle->GetProperty()) && !UE::StateTree::PropertyRefHelpers::IsPropertyAccessibleForPropertyRef(*SourceProperty, InBindingChain, StructDesc))
		{
			if (!UE::StateTree::PropertyRefHelpers::IsPropertyAccessibleForPropertyRef(*SourceProperty, InBindingChain, StructDesc))
			{
				return false;
			}
		}

		return IsPropertyBindable(*SourceProperty);
	}

	static bool ArePropertyAndContextStructCompatible(const UStruct* SourceStruct, const FProperty* TargetProperty)
	{
		if (const FStructProperty* TargetStructProperty = CastField<FStructProperty>(TargetProperty))
		{
			return TargetStructProperty->Struct == SourceStruct;
		}
		if (const FObjectProperty* TargetObjectProperty = CastField<FObjectProperty>(TargetProperty))
		{
			return SourceStruct != nullptr && SourceStruct->IsChildOf(TargetObjectProperty->PropertyClass);
		}
		
		return false;
	}

	static bool ArePropertiesCompatible(const FProperty* SourceProperty, const FProperty* TargetProperty, const void* SourcePropertyValue, const void* TargetPropertyValue)
	{
		// @TODO: Refactor FStateTreePropertyBindings::ResolveCopyType() so that we can use it directly here.
		
		bool bCanBind = false;

		const FStructProperty* TargetStructProperty = CastField<FStructProperty>(TargetProperty);
		
		// AnyEnums need special handling.
		// It is a struct property but we want to treat it as an enum. We need to do this here, instead of 
		// FStateTreePropertyBindingCompiler::GetPropertyCompatibility() because the treatment depends on the value too.
		// Note: AnyEnums will need special handling before they can be used for binding.
		if (TargetStructProperty && TargetStructProperty->Struct == FStateTreeAnyEnum::StaticStruct())
		{
			// If the AnyEnum has AllowAnyBinding, allow to bind to any enum.
			const bool bAllowAnyBinding = TargetProperty->HasMetaData(UE::StateTree::PropertyBinding::AllowAnyBindingName);

			check(TargetPropertyValue);
			const FStateTreeAnyEnum* TargetAnyEnum = static_cast<const FStateTreeAnyEnum*>(TargetPropertyValue);

			// If the enum class is not specified, allow to bind to any enum, if the class is specified allow only that enum.
			if (const FByteProperty* SourceByteProperty = CastField<FByteProperty>(SourceProperty))
			{
				if (UEnum* Enum = SourceByteProperty->GetIntPropertyEnum())
				{
					bCanBind = bAllowAnyBinding || TargetAnyEnum->Enum == Enum;
				}
			}
			else if (const FEnumProperty* SourceEnumProperty = CastField<FEnumProperty>(SourceProperty))
			{
				bCanBind = bAllowAnyBinding || TargetAnyEnum->Enum == SourceEnumProperty->GetEnum();
			}
		}
		else if (TargetStructProperty && TargetStructProperty->Struct == FStateTreeStructRef::StaticStruct())
		{
			FString BaseStructName;
			const UScriptStruct* TargetStructRefBaseStruct = UE::StateTree::Compiler::GetBaseStructFromMetaData(TargetProperty, BaseStructName);

			if (const FStructProperty* SourceStructProperty = CastField<FStructProperty>(SourceProperty))
			{
				if (SourceStructProperty->Struct == TBaseStructure<FStateTreeStructRef>::Get())
				{
					FString SourceBaseStructName;
					const UScriptStruct* SourceStructRefBaseStruct = UE::StateTree::Compiler::GetBaseStructFromMetaData(SourceStructProperty, SourceBaseStructName);
					bCanBind = SourceStructRefBaseStruct && SourceStructRefBaseStruct->IsChildOf(TargetStructRefBaseStruct);
				}
				else
				{
					bCanBind = SourceStructProperty->Struct && SourceStructProperty->Struct->IsChildOf(TargetStructRefBaseStruct);
				}
			}
		}
		else if (TargetStructProperty && UE::StateTree::PropertyRefHelpers::IsPropertyRef(*TargetStructProperty))
		{
			check(TargetPropertyValue);
			bCanBind = UE::StateTree::PropertyRefHelpers::IsPropertyRefCompatibleWithProperty(*TargetStructProperty, *SourceProperty, TargetPropertyValue, SourcePropertyValue);
		}
		else
		{
			// Note: We support type promotion here
			bCanBind = FStateTreePropertyBindings::GetPropertyCompatibility(SourceProperty, TargetProperty) != EStateTreePropertyAccessCompatibility::Incompatible;
		}

		return bCanBind;
	}

	UStruct* ResolveIndirection(TConstArrayView<FBindingChainElement> InBindingChain)
	{
		UObject* OwnerObject = WeakOwnerObject.Get();
		if (!OwnerObject)
		{
			return nullptr;
		}
		
		IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(OwnerObject);
		if (!BindingOwner)
		{
			return nullptr;
		}

		const int32 SourceStructIndex = InBindingChain[0].ArrayIndex;
		check(SourceStructIndex >= 0 && SourceStructIndex < AccessibleStructs.Num());

		FStateTreeDataView DataView;
		if (BindingOwner->GetDataViewByID(AccessibleStructs[SourceStructIndex].ID, DataView))
		{
			return UE::StateTree::PropertyBinding::ResolveLeafValueStructType(DataView, InBindingChain);
		}

		return nullptr;
	}

	FText GetText()
	{
		ConditionallyUpdateData();

		// Bound PropertyFunction is allowed to override it's display name.
		if (IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(WeakOwnerObject.Get()))
		{
			if(FStateTreeEditorPropertyBindings* EditorBindings = BindingOwner->GetPropertyEditorBindings())
			{
				const FStateTreePropertyPathBinding* CurrentBinding = EditorBindings->GetBindings().FindByPredicate([this](const FStateTreePropertyPathBinding& Binding)
				{
					return Binding.GetTargetPath() == TargetPath;
				});

				if (CurrentBinding)
				{
					const FConstStructView PropertyFunctionEditorNodeView = CurrentBinding->GetPropertyFunctionNode();
					if(PropertyFunctionEditorNodeView.IsValid())
					{
						const FStateTreeEditorNode& EditorNode = PropertyFunctionEditorNodeView.Get<const FStateTreeEditorNode>();
						if (const FStateTreeNodeBase* Node = EditorNode.Node.GetPtr<const FStateTreeNodeBase>())
						{
							const FText Description = Node->GetDescription(CachedSourcePath.GetStructID(), EditorNode.GetInstance(), FStateTreeBindingLookup(BindingOwner), EStateTreeNodeFormatting::Text);
							if (!Description.IsEmpty())
							{
								return FText::FormatNamed(FormatableText, TEXT("SourceStruct"), Description);
							}
						}
					}
				}
			}
		}

		return FText::FormatNamed(FormatableText, TEXT("SourceStruct"), SourceStructName);
	}
	
	FText GetTooltipText()
	{
		ConditionallyUpdateData();

		// If the source property is a PropertyFunction and it overrides it's display name, it's been used in the tooltip text.
		if (IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(WeakOwnerObject.Get()))
		{
			if(FStateTreeEditorPropertyBindings* EditorBindings = BindingOwner->GetPropertyEditorBindings())
			{
				const FStateTreePropertyPathBinding* CurrentBinding = EditorBindings->GetBindings().FindByPredicate([this](const FStateTreePropertyPathBinding& Binding)
				{
					return Binding.GetTargetPath() == TargetPath;
				});

				if (CurrentBinding)
				{
					const FConstStructView PropertyFunctionEditorNodeView = CurrentBinding->GetPropertyFunctionNode();
					if(PropertyFunctionEditorNodeView.IsValid())
					{
						const FStateTreeEditorNode& EditorNode = PropertyFunctionEditorNodeView.Get<const FStateTreeEditorNode>();
						if(const FStateTreeNodeBase* Node = EditorNode.Node.GetPtr<const FStateTreeNodeBase>())
						{
							const FText Description = Node->GetDescription(CachedSourcePath.GetStructID(), EditorNode.GetInstance(), FStateTreeBindingLookup(BindingOwner), EStateTreeNodeFormatting::Text);
							if (!Description.IsEmpty())
							{
								return FText::FormatNamed(FormatableTooltipText, TEXT("SourceStruct"), Description);
							}
						}
					}
				}
			}
		}

		return FText::FormatNamed(FormatableTooltipText, TEXT("SourceStruct"), SourceStructName);
	}
	
	FLinearColor GetColor()
	{
		ConditionallyUpdateData();

		// Bound PropertyFunction is allowed to override it's icon color if the binding leads directly into it's single output property.
		if (CachedSourcePath.NumSegments() == 1)
		{
			if (IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(WeakOwnerObject.Get()))
			{
				if(FStateTreeEditorPropertyBindings* EditorBindings = BindingOwner->GetPropertyEditorBindings())
				{
					const FStateTreePropertyPathBinding* CurrentBinding = EditorBindings->GetBindings().FindByPredicate([this](const FStateTreePropertyPathBinding& Binding)
					{
						return Binding.GetTargetPath() == TargetPath;
					});

					if (CurrentBinding)
					{
						const FConstStructView PropertyFunctionEditorNodeView = CurrentBinding->GetPropertyFunctionNode();
						if (PropertyFunctionEditorNodeView.IsValid())
						{
							const FStateTreeEditorNode& EditorNode = PropertyFunctionEditorNodeView.Get<const FStateTreeEditorNode>();
							if (const FStateTreeNodeBase* Node = EditorNode.Node.GetPtr<const FStateTreeNodeBase>())
							{
								if (Node && UE::StateTree::GetStructSingleOutputProperty(*Node->GetInstanceDataType()))
								{
									return Node->GetIconColor();
								}
					
							}
						}
					}
				}
			}
		}

		return Color;
	}
	
	const FSlateBrush* GetImage()
	{
		ConditionallyUpdateData();

		// Bound PropertyFunction is allowed to override it's icon.
		if (IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(WeakOwnerObject.Get()))
		{
				if(FStateTreeEditorPropertyBindings* EditorBindings = BindingOwner->GetPropertyEditorBindings())
				{
					const FStateTreePropertyPathBinding* CurrentBinding = EditorBindings->GetBindings().FindByPredicate([this](const FStateTreePropertyPathBinding& Binding)
					{
						return Binding.GetTargetPath() == TargetPath;
					});

					if (CurrentBinding)
					{
						const FConstStructView PropertyFunctionEditorNodeView = CurrentBinding->GetPropertyFunctionNode();
						if (PropertyFunctionEditorNodeView.IsValid())
						{
							const FStateTreeEditorNode& EditorNode = PropertyFunctionEditorNodeView.Get<const FStateTreeEditorNode>();
							if (const FStateTreeNodeBase* Node = EditorNode.Node.GetPtr<const FStateTreeNodeBase>())
							{
								return UE::StateTreeEditor::EditorNodeUtils::ParseIcon(Node->GetIconName()).GetIcon();
							}
						}
					}
				}
		}

		return Image;
	}
	
private:

	void ConditionallyUpdateData()
	{
		UObject* OwnerObject = WeakOwnerObject.Get();
		if (!OwnerObject)
		{
			return;
		}
		
		IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(OwnerObject);
		if (!BindingOwner)
		{
			return;
		}

		FStateTreeEditorPropertyBindings* EditorBindings = BindingOwner->GetPropertyEditorBindings();
		if (!EditorBindings)
		{
			return;
		}

		const FStateTreePropertyPath* CurrentSourcePath = EditorBindings->GetPropertyBindingSource(TargetPath);
		bool bPathsIdentical = false;
		if (CurrentSourcePath)
		{
			bPathsIdentical = CachedSourcePath == *CurrentSourcePath;
		}
		else
		{
			bPathsIdentical = CachedSourcePath.IsPathEmpty();
		}

		if (!bIsDataCached || !bPathsIdentical)
		{
			UpdateData();
		}
	}
	
	TWeakObjectPtr<UObject> WeakOwnerObject = nullptr;
	FStateTreePropertyPath CachedSourcePath;
	FStateTreePropertyPath TargetPath;
	TSharedPtr<const IPropertyHandle> PropertyHandle;
	TArray<FStateTreeBindableStructDesc> AccessibleStructs;

	/* Default name of the source struct. */
	FText SourceStructName;

	/* Binding's display name text. Expects it's source struct name to be injected before use. */
	FText FormatableText;

	/* Binding's tooltip text. Expects it's source struct name to be injected before use. */
	FText FormatableTooltipText;

	FLinearColor Color = FLinearColor::White;
	const FSlateBrush* Image = nullptr;
	
	bool bIsDataCached = false;
};

bool IsPropertyBindable(const FProperty& Property)
{
	const bool bIsUserEditable = Property.HasAnyPropertyFlags(CPF_Edit);
	if (!bIsUserEditable)
	{
		UE_LOG(LogStateTreeEditor, Verbose, TEXT("Property %s is not bindable because it's not user-settable in the editor"),
			*Property.GetName());
		return false;
	}

	const bool bPrivateOrProtected = !Property.HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected);
	const bool bPrivateButBlueprintAccessible = Property.GetBoolMetaData(FBlueprintMetadata::MD_AllowPrivateAccess);
	if (!bPrivateOrProtected && !bPrivateButBlueprintAccessible)
	{
		UE_LOG(LogStateTreeEditor, Verbose, TEXT("Property %s is not bindable because it's either private or protected and not private-accessible to blueprints"),
			*Property.GetName());
		return false;
	}

	return true;
}

/* Provides PropertyFunctionNode instance for a property node. */
class FStateTreePropertyFunctionNodeProvider : public IStructureDataProvider
{
public:
	FStateTreePropertyFunctionNodeProvider(IStateTreeEditorPropertyBindingsOwner& InBindingsOwner, FStateTreePropertyPath InTargetPath)
		: BindingsOwner(Cast<UObject>(&InBindingsOwner))
		, TargetPath(MoveTemp(InTargetPath))
	{}
	
	virtual bool IsValid() const override
	{
		return GetPropertyFunctionEditorNodeView(BindingsOwner.Get(), TargetPath).IsValid();
	};

	virtual const UStruct* GetBaseStructure() const override
	{
		return FStateTreeEditorNode::StaticStruct();
	}
	
	virtual void GetInstances(TArray<TSharedPtr<FStructOnScope>>& OutInstances, const UStruct* ExpectedBaseStructure) const override
	{
		if (ExpectedBaseStructure)
		{
			const FStructView Node = GetPropertyFunctionEditorNodeView(BindingsOwner.Get(), TargetPath);

			if (Node.IsValid() && Node.GetScriptStruct()->IsChildOf(ExpectedBaseStructure))
			{
				OutInstances.Add(MakeShared<FStructOnScope>(Node.GetScriptStruct(), Node.GetMemory()));
			}
		}
	}

	static bool IsBoundToValidPropertyFunction(UObject& InBindingsOwner, const FStateTreePropertyPath& InTargetPath)
	{
		return GetPropertyFunctionEditorNodeView(&InBindingsOwner, InTargetPath).IsValid();
	}
	
private:
	static FStructView GetPropertyFunctionEditorNodeView(UObject* RawBindingsOwner, const FStateTreePropertyPath& InTargetPath)
	{
		if(IStateTreeEditorPropertyBindingsOwner* Owner = Cast<IStateTreeEditorPropertyBindingsOwner>(RawBindingsOwner))
		{
			FStateTreeEditorPropertyBindings* EditorBindings = Owner->GetPropertyEditorBindings();
			FStateTreePropertyPathBinding* FoundBinding = EditorBindings->GetMutableBindings().FindByPredicate([&InTargetPath](const FStateTreePropertyPathBinding& Binding)
			{
				return Binding.GetTargetPath() == InTargetPath;
			});

			
			if (FoundBinding)
			{
				const FStructView EditorNodeView = FoundBinding->GetMutablePropertyFunctionNode();
				if (EditorNodeView.IsValid())
				{
					const FStateTreeEditorNode& EditorNode = EditorNodeView.Get<FStateTreeEditorNode>();
					if (EditorNode.Node.IsValid() && EditorNode.Instance.IsValid())
					{
						return EditorNodeView;
					}
				}
			}
		}

		return FStructView();
	}

	TWeakObjectPtr<UObject> BindingsOwner;
	FStateTreePropertyPath TargetPath;
};

} // UE::StateTree::PropertyBinding

bool FStateTreeBindingExtension::IsPropertyExtendable(const UClass* InObjectClass, const IPropertyHandle& PropertyHandle) const
{
	const FProperty* Property = PropertyHandle.GetProperty();
	if (Property == nullptr || Property->HasAnyPropertyFlags(CPF_PersistentInstance | CPF_EditorOnly | CPF_Config | CPF_Deprecated))
	{
		return false;
	}
	
	FStateTreePropertyPath TargetPath;
	// Figure out the structs we're editing, and property path relative to current property.
	const EStateTreePropertyUsage Usage = UE::StateTree::PropertyBinding::MakeStructPropertyPathFromPropertyHandle(PropertyHandle.AsShared(), TargetPath);

	if (Usage == EStateTreePropertyUsage::Input || Usage == EStateTreePropertyUsage::Context)
	{
		// Allow to bind only to the main level on input and context properties.
		return TargetPath.GetSegments().Num() == 1;
	}
	if (Usage == EStateTreePropertyUsage::Parameter)
	{
		return true;
	}
	
	return false;
}


void FStateTreeBindingExtension::ExtendWidgetRow(FDetailWidgetRow& InWidgetRow, const IDetailLayoutBuilder& InDetailBuilder, const UClass* InObjectClass, TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable("PropertyAccessEditor"))
	{
		return;
	}

	IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");

	UObject* OwnerObject = nullptr;
	FStateTreeEditorPropertyBindings* EditorBindings = nullptr;

	// Array of structs we can bind to.
	TArray<FBindingContextStruct> BindingContextStructs;
	TArray<FStateTreeBindableStructDesc> AccessibleStructs;

	// The struct and property where we're binding.
	FStateTreePropertyPath TargetPath;

	IStateTreeEditorPropertyBindingsOwner* BindingOwner = nullptr;

	TArray<UObject*> OuterObjects;
	InPropertyHandle->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num() == 1)
	{
		// Only allow to binding when one object is selected.
		OwnerObject = UE::StateTree::PropertyBinding::FindEditorBindingsOwner(OuterObjects[0]);

		// Figure out the structs we're editing, and property path relative to current property.
		UE::StateTree::PropertyBinding::MakeStructPropertyPathFromPropertyHandle(InPropertyHandle, TargetPath);

		BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(OwnerObject);
		if (BindingOwner)
		{
			EditorBindings = BindingOwner->GetPropertyEditorBindings();
			BindingOwner->GetAccessibleStructs(TargetPath.GetStructID(), AccessibleStructs);

			BindingOwner->EnumerateBindablePropertyFunctionNodes([&AccessibleStructs](const UScriptStruct* NodeStruct, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
			{
				AccessibleStructs.Add(Desc);
				return EStateTreeVisitor::Continue;
			});

			TMap<FString, FText> SectionNames;
			for (FStateTreeBindableStructDesc& StructDesc : AccessibleStructs)
			{
				const UStruct* Struct = StructDesc.Struct;

				FBindingContextStruct& ContextStruct = BindingContextStructs.AddDefaulted_GetRef();
				ContextStruct.DisplayText = FText::FromString(StructDesc.Name.ToString());
				ContextStruct.Struct = const_cast<UStruct*>(Struct);
				ContextStruct.Category = StructDesc.Category;

				// Mare sure same section names get exact same FText representation (binding widget uses IsIdentical() to compare the section names).
				if (const FText* SectionText = SectionNames.Find(StructDesc.StatePath))
				{
					ContextStruct.Section = *SectionText;
				}
				else
				{
					ContextStruct.Section = SectionNames.Add(StructDesc.StatePath, FText::FromString(StructDesc.StatePath));
				}
				
				// PropertyFunction overrides it's struct's icon color.
				if (StructDesc.DataSource == EStateTreeBindableStructSource::PropertyFunction)
				{
					if (const FProperty* OutputProperty = UE::StateTree::GetStructSingleOutputProperty(*StructDesc.Struct))
					{
						const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
						check(Schema);

						FEdGraphPinType PinType;
						if (Schema->ConvertPropertyToPinType(OutputProperty, PinType))
						{
							ContextStruct.Color = Schema->GetPinTypeColor(PinType);
						}
					}
				}
			}
		}
	}

	TSharedPtr<UE::StateTree::PropertyBinding::FCachedBindingData> CachedBindingData = MakeShared<UE::StateTree::PropertyBinding::FCachedBindingData>(OwnerObject, TargetPath, InPropertyHandle, AccessibleStructs);

	// Wrap value widget 
	{
		auto IsValueVisible = TAttribute<EVisibility>::Create([CachedBindingData]() -> EVisibility
			{
				return CachedBindingData->HasBinding(FStateTreeEditorPropertyBindings::ESearchMode::Exact) ? EVisibility::Collapsed : EVisibility::Visible;
			});

		TSharedPtr<SWidget> ValueWidget = InWidgetRow.ValueContent().Widget;
		InWidgetRow.ValueContent()
			[
				SNew(SBox)
				.Visibility(IsValueVisible)
				[
					ValueWidget.ToSharedRef()
				]
			];
	}
	
	FPropertyBindingWidgetArgs Args;
	Args.Property = InPropertyHandle->GetProperty();

	Args.OnCanBindPropertyWithBindingChain = FOnCanBindPropertyWithBindingChain::CreateLambda([CachedBindingData](FProperty* InProperty, TConstArrayView<FBindingChainElement> InBindingChain)
		{
			return CachedBindingData->CanBindToProperty(InProperty, InBindingChain);
		});

	Args.OnCanBindToContextStructWithIndex = FOnCanBindToContextStructWithIndex::CreateLambda([CachedBindingData](const UStruct* InStruct, int32 InStructIndex)
		{
			return CachedBindingData->CanBindToContextStruct(InStruct, InStructIndex);
		});

	Args.OnCanAcceptPropertyOrChildrenWithBindingChain = FOnCanAcceptPropertyOrChildrenWithBindingChain::CreateLambda([CachedBindingData](FProperty* InProperty, TConstArrayView<FBindingChainElement> InBindingChain)
		{
			return CachedBindingData->CanAcceptPropertyOrChildren(InProperty, InBindingChain);
		});

	Args.OnCanBindToClass = FOnCanBindToClass::CreateLambda([](UClass* InClass)
		{
			return true;
		});

	Args.OnAddBinding = FOnAddBinding::CreateLambda([CachedBindingData, &InDetailBuilder](FName InPropertyName, TConstArrayView<FBindingChainElement> InBindingChain)
		{
			CachedBindingData->AddBinding(InBindingChain);
			InDetailBuilder.GetPropertyUtilities()->RequestForceRefresh();
		});

	Args.OnRemoveBinding = FOnRemoveBinding::CreateLambda([CachedBindingData, &InDetailBuilder](FName InPropertyName)
		{
			CachedBindingData->RemoveBinding(FStateTreeEditorPropertyBindings::ESearchMode::Exact);
			InDetailBuilder.GetPropertyUtilities()->RequestForceRefresh();
		});

	Args.OnCanRemoveBinding = FOnCanRemoveBinding::CreateLambda([CachedBindingData](FName InPropertyName)
		{
			return CachedBindingData->HasBinding(FStateTreeEditorPropertyBindings::ESearchMode::Exact);
		});

	Args.CurrentBindingText = MakeAttributeLambda([CachedBindingData]()
		{
			return CachedBindingData->GetText();
		});

	Args.CurrentBindingToolTipText = MakeAttributeLambda([CachedBindingData]()
		{
			return CachedBindingData->GetTooltipText();
		});

	Args.CurrentBindingImage = MakeAttributeLambda([CachedBindingData]() -> const FSlateBrush*
		{
			return CachedBindingData->GetImage();
		});

	Args.CurrentBindingColor = MakeAttributeLambda([CachedBindingData]() -> FLinearColor
		{
			return CachedBindingData->GetColor();
		});

	if (BindingOwner)
	{
		Args.OnResolveIndirection = FOnResolveIndirection::CreateLambda([CachedBindingData](TConstArrayView<FBindingChainElement> InBindingChain)
		{
			return CachedBindingData->ResolveIndirection(InBindingChain);
		});
	}

	Args.BindButtonStyle = &FAppStyle::Get().GetWidgetStyle<FButtonStyle>("HoverHintOnly");
	Args.bAllowNewBindings = false;
	Args.bAllowArrayElementBindings = false;
	Args.bAllowUObjectFunctions = false;

	if (CanPromoteToParameter(InPropertyHandle))
	{
		Args.MenuExtender = MakeShared<FExtender>();
		Args.MenuExtender->AddMenuExtension(
			TEXT("BindingActions"),
			EExtensionHook::After,
			nullptr,
			FMenuExtensionDelegate::CreateLambda([CachedBindingData, AccessibleStructs = MoveTemp(AccessibleStructs), InPropertyHandle](FMenuBuilder& MenuBuilder)
			{
				MenuBuilder.AddSubMenu(
					LOCTEXT("PromoteToParameter", "Promote to Parameter"),
					LOCTEXT("PromoteToParameterTooltip", "Create a new parameter of the same type as the property, copy value over, and bind the property to the new parameter."),
					FNewMenuDelegate::CreateLambda([&CachedBindingData, &AccessibleStructs, &InPropertyHandle](FMenuBuilder& InMenuBuilder)
					{
						using namespace UE::StateTree::PropertyBinding;

						const FProperty* Property = InPropertyHandle->GetProperty();
						check(Property);
						const FName PropertyName = Property->GetFName();

						TSharedRef<FCachedBindingData> CachedBindingDataRef = CachedBindingData.ToSharedRef();

						FSectionHelper SectionHelper(InMenuBuilder);
						for (const FStateTreeBindableStructDesc& ContextStruct : AccessibleStructs)
						{
							TArray<TSharedPtr<const FRefTypeInfo>> RefTypeInfos;
							if (CachedBindingData->CanCreateParameter(ContextStruct, /*out*/RefTypeInfos))
							{
								SectionHelper.SetSection(FText::FromString(ContextStruct.StatePath));

								if (RefTypeInfos.IsEmpty())
								{
									InMenuBuilder.AddMenuEntry(FExecuteAction::CreateSP(CachedBindingDataRef, &FCachedBindingData::PromoteToParameter, PropertyName, ContextStruct, TSharedPtr<const FRefTypeInfo>()),
										MakeContextStructWidget(ContextStruct));
								}
								else
								{
									InMenuBuilder.AddSubMenu(MakeContextStructWidget(ContextStruct),
										FNewMenuDelegate::CreateLambda([CachedBindingDataRef, PropertyName, &ContextStruct, RefTypeInfos = MoveTemp(RefTypeInfos)](FMenuBuilder& InSubMenuBuilder)
										{
											FSectionHelper SectionHelper(InSubMenuBuilder);
											SectionHelper.SetSection(LOCTEXT("RefTypeParams", "Reference Types"));
											for (const TSharedPtr<const FRefTypeInfo>& RefTypeInfo : RefTypeInfos)
											{
												InSubMenuBuilder.AddMenuEntry(FExecuteAction::CreateSP(CachedBindingDataRef, &FCachedBindingData::PromoteToParameter, PropertyName, ContextStruct, RefTypeInfo),
													MakeBindingPropertyInfoWidget(RefTypeInfo->TypeNameText, RefTypeInfo->PinType));
											}
										}));
								}
							}
						}
					})
				);
			})
		);
	}

	// ResetToDefault
	{
		InWidgetRow.CustomResetToDefault = FResetToDefaultOverride::Create(
			MakeAttributeLambda([CachedBindingData, InPropertyHandle]()
			{
				return InPropertyHandle->CanResetToDefault() || CachedBindingData->HasBinding(FStateTreeEditorPropertyBindings::ESearchMode::Includes);
			}),
			FSimpleDelegate::CreateLambda([CachedBindingData, &InDetailBuilder, InPropertyHandle]()
				{
					if (CachedBindingData->HasBinding(FStateTreeEditorPropertyBindings::ESearchMode::Includes))
					{
						CachedBindingData->RemoveBinding(FStateTreeEditorPropertyBindings::ESearchMode::Includes);
						InDetailBuilder.GetPropertyUtilities()->RequestForceRefresh();
					}
					if (InPropertyHandle->CanResetToDefault())
					{
						InPropertyHandle->ResetToDefault();
					}
				}),
			false);
	}

	InWidgetRow.ExtensionContent()
	[
		PropertyAccessEditor.MakePropertyBindingWidget(BindingContextStructs, Args)
	];
}

bool FStateTreeBindingExtension::CanPromoteToParameter(const TSharedPtr<IPropertyHandle>& InPropertyHandle) const
{
	const FProperty* Property = InPropertyHandle->GetProperty();
	if (!Property)
	{
		return false;
	}

	// Property Bag picker only detects Blueprint Types, so only allow properties that are blueprint types
	// FPropertyBagInstanceDataDetails::OnPropertyNameContent uses SPinTypeSelector to generate the property type picker.
	// UEdGraphSchema_K2::GetVariableTypeTree (GatherPinsImpl: FindEnums, FindStructs, FindObjectsAndInterfaces) is used there which only allows bp types.
	// The below behavior mirrors the behavior in the pin gathering but for properties

	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(EnumProperty->GetEnum()))
		{
			return false;
		}
	}
	else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		// Support Property Refs as even though these aren't bp types, the actual types that would be added are the ones in the meta-data RefType
		if (StructProperty->Struct && StructProperty->Struct->IsChildOf(FStateTreePropertyRef::StaticStruct()))
		{
			return true;
		}

		if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(StructProperty->Struct))
		{
			return false;
		}
	}
	else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(ObjectProperty->PropertyClass))
		{
			return false;
		}
	}
	else if (const FInterfaceProperty* InterfaceProperty = CastField<FInterfaceProperty>(Property))
	{
		if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(InterfaceProperty->InterfaceClass))
		{
			return false;
		}
	}

	return true;
}

void FStateTreeBindingsChildrenCustomization::CustomizeChildren(IDetailChildrenBuilder& ChildrenBuilder, TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	TArray<UObject*> OuterObjects;
	InPropertyHandle->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num() == 1)
	{
		FStateTreePropertyPath TargetPath;
		UE::StateTree::PropertyBinding::MakeStructPropertyPathFromPropertyHandle(InPropertyHandle, TargetPath);

		using FStateTreePropertyFunctionNodeProvider = UE::StateTree::PropertyBinding::FStateTreePropertyFunctionNodeProvider;
		UObject* BindingsOwner = UE::StateTree::PropertyBinding::FindEditorBindingsOwner(OuterObjects[0]);
		if (BindingsOwner && FStateTreePropertyFunctionNodeProvider::IsBoundToValidPropertyFunction(*BindingsOwner, TargetPath))
		{
			// Bound PropertyFunction takes control over property's children composition.
			const TSharedPtr<FStateTreePropertyFunctionNodeProvider> StructProvider = MakeShared<FStateTreePropertyFunctionNodeProvider>(*CastChecked<IStateTreeEditorPropertyBindingsOwner>(BindingsOwner), MoveTemp(TargetPath));
			// Create unique name to persists expansion state.
			const FName UniqueName = FName(LexToString(TargetPath.GetStructID()) + TargetPath.ToString());
			ChildrenBuilder.AddChildStructure(InPropertyHandle.ToSharedRef(), StructProvider, UniqueName);
		}
	}
}

bool FStateTreeBindingsChildrenCustomization::ShouldCustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle)
{
	TArray<UObject*> OuterObjects;
	InPropertyHandle->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num() == 1)
	{
		// Bound property's children composition gets overridden.
		FStateTreePropertyPath TargetPath;
		UE::StateTree::PropertyBinding::MakeStructPropertyPathFromPropertyHandle(InPropertyHandle, TargetPath);
		IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(UE::StateTree::PropertyBinding::FindEditorBindingsOwner(OuterObjects[0]));
		if (!TargetPath.IsPathEmpty() && BindingOwner)
		{
			if (FStateTreeEditorPropertyBindings* EditorBindings = BindingOwner->GetPropertyEditorBindings())
			{
				return EditorBindings->HasPropertyBinding(TargetPath);
			}
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
