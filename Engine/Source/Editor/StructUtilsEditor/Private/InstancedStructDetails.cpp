// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstancedStructDetails.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "UObject/Package.h"
#include "Widgets/Images/SImage.h"
#include "Modules/ModuleManager.h"
#include "StructUtils/UserDefinedStruct.h"
#include "StructUtils/InstancedStruct.h"
#include "IStructureDataProvider.h"
#include "Misc/ConfigCacheIni.h"
#include "StructUtilsDelegates.h"
#include "SInstancedStructPicker.h"

#define LOCTEXT_NAMESPACE "StructUtilsEditor"

////////////////////////////////////

class FInstancedStructProvider : public IStructureDataProvider
{
public:
	FInstancedStructProvider() = default;
	
	explicit FInstancedStructProvider(const TSharedPtr<IPropertyHandle>& InStructProperty)
		: StructProperty(InStructProperty)
	{
	}
	
	virtual ~FInstancedStructProvider() override {}

	void Reset()
	{
		StructProperty = nullptr;
	}
	
	virtual bool IsValid() const override
	{
		bool bHasValidData = false;
		EnumerateInstances([&bHasValidData](const UScriptStruct* ScriptStruct, uint8* Memory, UPackage* Package)
		{
			if (ScriptStruct && Memory)
			{
				bHasValidData = true;
				return false; // Stop
			}
			return true; // Continue
		});

		return bHasValidData;
	}
	
	virtual const UStruct* GetBaseStructure() const override
	{
		// Taken from UClass::FindCommonBase
		auto FindCommonBaseStruct = [](const UScriptStruct* StructA, const UScriptStruct* StructB)
		{
			const UScriptStruct* CommonBaseStruct = StructA;
			while (CommonBaseStruct && StructB && !StructB->IsChildOf(CommonBaseStruct))
			{
				CommonBaseStruct = Cast<UScriptStruct>(CommonBaseStruct->GetSuperStruct());
			}
			return CommonBaseStruct;
		};

		const UScriptStruct* CommonStruct = nullptr;
		EnumerateInstances([&CommonStruct, &FindCommonBaseStruct](const UScriptStruct* ScriptStruct, uint8* Memory, UPackage* Package)
		{
			if (ScriptStruct)
			{
				CommonStruct = FindCommonBaseStruct(ScriptStruct, CommonStruct);
			}
			return true; // Continue
		});

		return CommonStruct;
	}

	virtual void GetInstances(TArray<TSharedPtr<FStructOnScope>>& OutInstances, const UStruct* ExpectedBaseStructure) const override
	{
		// The returned instances need to be compatible with base structure.
		// This function returns empty instances in case they are not compatible, with the idea that we have as many instances as we have outer objects.
		EnumerateInstances([&OutInstances, ExpectedBaseStructure](const UScriptStruct* ScriptStruct, uint8* Memory, UPackage* Package)
		{
			TSharedPtr<FStructOnScope> Result;
			
			if (ExpectedBaseStructure && ScriptStruct && ScriptStruct->IsChildOf(ExpectedBaseStructure))
			{
				Result = MakeShared<FStructOnScope>(ScriptStruct, Memory);
				Result->SetPackage(Package);
			}

			OutInstances.Add(Result);

			return true; // Continue
		});
	}

	virtual bool IsPropertyIndirection() const override
	{
		return true;
	}

	virtual uint8* GetValueBaseAddress(uint8* ParentValueAddress, const UStruct* ExpectedBaseStructure) const override
	{
		if (!ParentValueAddress)
		{
			return nullptr;
		}

		FInstancedStruct& InstancedStruct = *reinterpret_cast<FInstancedStruct*>(ParentValueAddress);
		if (ExpectedBaseStructure && InstancedStruct.GetScriptStruct() && InstancedStruct.GetScriptStruct()->IsChildOf(ExpectedBaseStructure))
		{
			return InstancedStruct.GetMutableMemory();
		}
		
		return nullptr;
	}
	
protected:

	void EnumerateInstances(TFunctionRef<bool(const UScriptStruct* ScriptStruct, uint8* Memory, UPackage* Package)> InFunc) const
	{
		if (!StructProperty.IsValid())
		{
			return;
		}
		
		TArray<UPackage*> Packages;
		StructProperty->GetOuterPackages(Packages);

		StructProperty->EnumerateRawData([&InFunc, &Packages](void* RawData, const int32 DataIndex, const int32 /*NumDatas*/)
		{
			const UScriptStruct* ScriptStruct = nullptr;
			uint8* Memory = nullptr;
			UPackage* Package = nullptr;
			if (FInstancedStruct* InstancedStruct = static_cast<FInstancedStruct*>(RawData))
			{
				ScriptStruct = InstancedStruct->GetScriptStruct();
				Memory = InstancedStruct->GetMutableMemory();
				if (ensureMsgf(Packages.IsValidIndex(DataIndex), TEXT("Expecting packges and raw data to match.")))
				{
					Package = Packages[DataIndex];
				}
			}

			return InFunc(ScriptStruct, Memory, Package);
		});
	}
	
	TSharedPtr<IPropertyHandle> StructProperty;
};

////////////////////////////////////

FInstancedStructDataDetails::FInstancedStructDataDetails(TSharedPtr<IPropertyHandle> InStructProperty)
{
#if DO_CHECK
	FStructProperty* StructProp = CastFieldChecked<FStructProperty>(InStructProperty->GetProperty());
	check(StructProp);
	check(StructProp->Struct == FInstancedStruct::StaticStruct());
#endif

	StructProperty = InStructProperty;
}

FInstancedStructDataDetails::~FInstancedStructDataDetails()
{
	UE::StructUtils::Delegates::OnUserDefinedStructReinstanced.Remove(UserDefinedStructReinstancedHandle);
}

void FInstancedStructDataDetails::OnUserDefinedStructReinstancedHandle(const UUserDefinedStruct& Struct)
{
	OnStructLayoutChanges();
}

void FInstancedStructDataDetails::SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren)
{
	OnRegenerateChildren = InOnRegenerateChildren;
}

TArray<TWeakObjectPtr<const UStruct>> FInstancedStructDataDetails::GetInstanceTypes() const
{
	TArray<TWeakObjectPtr<const UStruct>> Result;
	
	StructProperty->EnumerateConstRawData([&Result](const void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/)
	{
		TWeakObjectPtr<const UStruct>& Type = Result.AddDefaulted_GetRef();
		if (const FInstancedStruct* InstancedStruct = static_cast<const FInstancedStruct*>(RawData))
		{
			Result.Add(InstancedStruct->GetScriptStruct());
		}
		else
		{
			Result.Add(nullptr);
		}
		return true;
	});

	return Result;
}

void FInstancedStructDataDetails::GetPropertyGroups(const TArray<TSharedPtr<IPropertyHandle>>& InProperties, IDetailChildrenBuilder& InChildBuilder, TMap<TSharedPtr<IPropertyHandle>, IDetailGroup*>& OutPropertyToGroup) const
{
	static const FName CategoryName = FName(TEXT("Category"));
	static const FName EnableCategoriesName = FName(TEXT("EnableCategories"));

	// Temporarily store a mapping of category -> group while groups are being built
	TMap<FString, IDetailGroup*> CategoryToGroup;
	
	for (const TSharedPtr<IPropertyHandle>& PropertyHandle : InProperties)
	{
		// The property needs the "EnableCategories" metadata in order to be added under a group. Grouping is opt-in.
		if (!PropertyHandle->HasMetaData(EnableCategoriesName))
		{
			continue;
		}
		
		const FString& PropertyCategory = PropertyHandle->GetMetaData(CategoryName).TrimStartAndEnd();
		if (PropertyCategory.IsEmpty())
		{
			continue;
		}

		constexpr bool bCullEmpty = true;
		TArray<FString> CategoriesToAdd;
		PropertyCategory.ParseIntoArray(CategoriesToAdd, TEXT("|"), bCullEmpty);

		if (CategoriesToAdd.IsEmpty())
		{
			continue;
		}

		// Tracks the category name as it is being built up (eg, Foo -> Foo|Bar -> Foo|Bar|Baz)
		FString CompleteCategory;

		// For this property, add all of the groups needed for its category (eg, Foo, Foo|Bar, and Foo|Bar|Baz)
		IDetailGroup* CurrentGroup = nullptr;
		for (FString& CategoryToAdd : CategoriesToAdd)
		{
			CategoryToAdd.TrimStartAndEndInline();

			// Cover the edge case where there's a category like "Foo|", which is invalid
			CategoryToAdd.TrimCharInline(TEXT('|'), nullptr);

			CompleteCategory = CompleteCategory.IsEmpty()
				? CategoryToAdd
				: FString::Join(TArray({CompleteCategory, CategoryToAdd}), TEXT("|"));

			// Create the category's group if it has not yet been created
			if (!CategoryToGroup.Contains(CompleteCategory))
			{
				if (CurrentGroup)
				{
					// Add the group to the previous group if this is a nested category (eg, if this is the Foo|Bar group, add to the Foo group)
					CurrentGroup = &CurrentGroup->AddGroup(FName(CompleteCategory), FText::FromString(CategoryToAdd));
				}
				else
				{
					// Otherwise, add the group as a normal group via the builder
					CurrentGroup = &InChildBuilder.AddGroup(FName(CompleteCategory), FText::FromString(CategoryToAdd));
				}

				CategoryToGroup.Add(CompleteCategory, CurrentGroup);
			}
			else
			{
				CurrentGroup = CategoryToGroup[CompleteCategory];
			}
		}

		check(CurrentGroup);
		OutPropertyToGroup.Add(PropertyHandle, CurrentGroup);
	}
}

void FInstancedStructDataDetails::OnStructLayoutChanges()
{
	if (StructProvider.IsValid())
	{
		// Reset the struct provider immediately, some update functions might get called with the old struct.
		StructProvider->Reset();
	}
	OnRegenerateChildren.ExecuteIfBound();
}

void FInstancedStructDataDetails::OnStructHandlePostChange()
{
	if (StructProvider.IsValid())
	{
		TArray<TWeakObjectPtr<const UStruct>> InstanceTypes = GetInstanceTypes();
		if (InstanceTypes != CachedInstanceTypes)
		{
			OnRegenerateChildren.ExecuteIfBound();
		}
	}
}

void FInstancedStructDataDetails::GenerateHeaderRowContent(FDetailWidgetRow& NodeRow)
{
	StructProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FInstancedStructDataDetails::OnStructHandlePostChange));
	if (!UserDefinedStructReinstancedHandle.IsValid())
	{
		UserDefinedStructReinstancedHandle = UE::StructUtils::Delegates::OnUserDefinedStructReinstanced.AddSP(this, &FInstancedStructDataDetails::OnUserDefinedStructReinstancedHandle);
	}
}

void FInstancedStructDataDetails::GenerateChildContent(IDetailChildrenBuilder& ChildBuilder)
{
	// Add the rows for the struct
	TSharedRef<FInstancedStructProvider> NewStructProvider = MakeShared<FInstancedStructProvider>(StructProperty);

	bool bCustomizedProperty = false;

	const UStruct* BaseStruct = NewStructProvider->GetBaseStructure();
	if (BaseStruct)
	{
		static const FName EditModuleName("PropertyEditor");
		if (const FPropertyEditorModule* EditModule = FModuleManager::GetModulePtr<FPropertyEditorModule>(EditModuleName))
		{
			if (EditModule->IsCustomizedStruct(BaseStruct, FCustomPropertyTypeLayoutMap()))
			{
				bCustomizedProperty = true;
			}
		}
	}

	if (bCustomizedProperty)
	{
		// Use the struct name instead of the fully-qualified property name
		const FText Label = BaseStruct->GetDisplayNameText();
		const FName PropertyName = StructProperty->GetProperty()->GetFName();

		// If the struct has a property customization, then we'll route through AddChildStructure, as it supports
		// IPropertyTypeCustomization. The other branch is mostly kept as-is for legacy support purposes.
		IDetailPropertyRow* PropertyRow = ChildBuilder.AddChildStructure(
			StructProperty.ToSharedRef(), NewStructProvider, PropertyName, Label
		);

		// Expansion state is not properly persisted for these structures, so let's expanded it by default for now
		if (PropertyRow)
		{
			PropertyRow->ShouldAutoExpand(true);
		}
	}
	else
	{
		TArray<TSharedPtr<IPropertyHandle>> ChildProperties = StructProperty->AddChildStructure(NewStructProvider);
		
		// Properties may have Category metadata. If that's the case, they should be added under groups.
		TMap<TSharedPtr<IPropertyHandle>, IDetailGroup*> PropertyToGroup;
		GetPropertyGroups(ChildProperties, ChildBuilder, PropertyToGroup);
		
		for (TSharedPtr<IPropertyHandle> ChildHandle : ChildProperties)
		{
			// If the property has a group, add it under the group. Otherwise, just add it normally via the builder.
			IDetailGroup** PropertyGroup = PropertyToGroup.Find(ChildHandle);
			if (PropertyGroup && *PropertyGroup)
			{
				IDetailPropertyRow& Row = (*PropertyGroup)->AddPropertyRow(ChildHandle.ToSharedRef());
				OnChildRowAdded(Row);
			}
			else
			{
				IDetailPropertyRow& Row = ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
				OnChildRowAdded(Row);
			}
		}
	}

	StructProvider = NewStructProvider;

	CachedInstanceTypes = GetInstanceTypes();
}

void FInstancedStructDataDetails::Tick(float DeltaTime)
{
	// If the instance types change (e.g. due to selecting new struct type), we'll need to update the layout.
	TArray<TWeakObjectPtr<const UStruct>> InstanceTypes = GetInstanceTypes();
	if (InstanceTypes != CachedInstanceTypes)
	{
		OnRegenerateChildren.ExecuteIfBound();
	}
}

FName FInstancedStructDataDetails::GetName() const
{
	static const FName Name("InstancedStructDataDetails");
	return Name;
}


////////////////////////////////////

TSharedRef<IPropertyTypeCustomization> FInstancedStructDetails::MakeInstance()
{
	return MakeShared<FInstancedStructDetails>();
}

FInstancedStructDetails::~FInstancedStructDetails()
{
	if (OnObjectsReinstancedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectsReinstanced.Remove(OnObjectsReinstancedHandle);
	}
}

void FInstancedStructDetails::CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities();

	OnObjectsReinstancedHandle = FCoreUObjectDelegates::OnObjectsReinstanced.AddSP(this, &FInstancedStructDetails::OnObjectsReinstanced);

	HeaderRow
		.ShouldAutoExpand(true)
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		.VAlign(VAlign_Center)
		[
			SAssignNew(StructPicker, SInstancedStructPicker, StructProperty, PropUtils)
		]
		.IsEnabled(StructProperty->IsEditable());
}

void FInstancedStructDetails::OnObjectsReinstanced(const FReplacementObjectMap& ObjectMap)
{
	// Force update the details when BP is compiled, since we may cached hold references to the old object or class.
	if (!ObjectMap.IsEmpty() && PropUtils.IsValid())
	{
		PropUtils->RequestRefresh();
	}
}

void FInstancedStructDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	TSharedRef<FInstancedStructDataDetails> DataDetails = MakeShared<FInstancedStructDataDetails>(StructProperty);
	StructBuilder.AddCustomBuilder(DataDetails);
}

#undef LOCTEXT_NAMESPACE
