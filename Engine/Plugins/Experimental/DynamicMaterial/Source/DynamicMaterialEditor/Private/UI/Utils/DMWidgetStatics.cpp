// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Utils/DMWidgetStatics.h"

#include "Components/MaterialValues/DMMaterialValueFloat.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

const FLazyName FDMWidgetStatics::PropertyValueWidget = TEXT("SPropertyValueWidget");

FDMWidgetStatics& FDMWidgetStatics::Get()
{
	 static FDMWidgetStatics Instance;
	 return Instance;
}

bool FDMWidgetStatics::GetExpansionState(UObject* InOwner, FName InName, bool& bOutExpanded)
{
	const FExpansionItem ExpansionItem = {InOwner, InName};

	if (const bool* State = ExpansionStates.Find(ExpansionItem))
	{
		bOutExpanded = *State;
		return true;
	}

	return false;
}

void FDMWidgetStatics::SetExpansionState(UObject* InOwner, FName InName, bool bInIsExpanded)
{
	const FExpansionItem ExpansionItem = {InOwner, InName};
	ExpansionStates.FindOrAdd(ExpansionItem) = bInIsExpanded;
}

FDMPropertyHandle FDMWidgetStatics::GetPropertyHandle(const SWidget* InOwningWidget, UObject* InObject, FName InPropertyName)
{
	TArray<FDMPropertyHandle>& PropertyHandles = PropertyHandleMap.FindOrAdd(InOwningWidget);

	for (const FDMPropertyHandle& ExistingHandle : PropertyHandles)
	{
		if (ExistingHandle.PropertyHandle && ExistingHandle.PropertyHandle->GetProperty()->GetFName() == InPropertyName)
		{
			TArray<UObject*> Outers;
			ExistingHandle.PropertyHandle->GetOuterObjects(Outers);

			if (Outers.IsEmpty() == false && Outers[0] == InObject)
			{
				return ExistingHandle;
			}
		}
	}

	if (TSharedPtr<IPropertyRowGenerator> PropertyRowGenerator = SearchForGenerator(PropertyHandles, InObject))
	{
		FDMPropertyHandle PropertyHandle;
		PropertyHandle.PropertyRowGenerator = PropertyRowGenerator;

		if (TSharedPtr<IDetailTreeNode> DetailTreeNode = SearchGeneratorForNode(PropertyRowGenerator.ToSharedRef(), InPropertyName))
		{
			PropertyHandle.DetailTreeNode = DetailTreeNode;
			PropertyHandle.PropertyHandle = DetailTreeNode->CreatePropertyHandle();

			AddPropertyMetaData(InObject, InPropertyName, PropertyHandle);

			return PropertyHandle;
		}

		return PropertyHandle;
	}

	FDMPropertyHandle NewHandle = CreatePropertyHandle(InOwningWidget, InObject, InPropertyName);

	if (!NewHandle.PropertyHandle.IsValid() && NewHandle.DetailTreeNode.IsValid())
	{
		NewHandle.PropertyHandle = NewHandle.DetailTreeNode->CreatePropertyHandle();
	}

	AddPropertyMetaData(InObject, InPropertyName, NewHandle);

	PropertyHandles.Add(NewHandle);

	return NewHandle;
}

void FDMWidgetStatics::ClearPropertyHandles(const SWidget* InOwningWidget)
{
	PropertyHandleMap.Remove(InOwningWidget);
}

TSharedPtr<SWidget> FDMWidgetStatics::FindWidgetInHierarchy(const TSharedRef<SWidget>& InParent, const FName& InName)
{
	if (InParent->GetType() == InName)
	{
		return InParent;
	}

	FChildren* Children = InParent->GetChildren();

	if (!Children)
	{
		return nullptr;
	}

	const int32 ChildNum = Children->Num();

	for (int32 Index = 0; Index < ChildNum; ++Index)
	{
		const TSharedRef<SWidget>& Widget = Children->GetChildAt(Index);

		if (Widget->GetType() == InName)
		{
			return Widget;
		}
	}

	for (int32 Index = 0; Index < ChildNum; ++Index)
	{
		if (TSharedPtr<SWidget> FoundChild = FindWidgetInHierarchy(Children->GetChildAt(Index), InName))
		{
			return FoundChild;
		}
	}

	return nullptr;
}

TSharedPtr<SWidget> FDMWidgetStatics::GetInnerPropertyValueWidget(const TSharedRef<SWidget>& InWidget)
{
	if (FChildren* Children = InWidget->GetChildren())
	{
		if (Children->Num() > 0)
		{
			return Children->GetChildAt(0);
		}
	}

	return nullptr;
}

void FDMWidgetStatics::ClearData()
{
	ExpansionStates.Empty();
	PropertyHandleMap.Empty();
}

FDMPropertyHandle FDMWidgetStatics::CreatePropertyHandle(const void* InOwningWidget, UObject* InObject, FName InPropertyName)
{
	FDMPropertyHandle PropertyHandle;

	FPropertyEditorModule& PropertyEditor = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	FPropertyRowGeneratorArgs RowGeneratorArgs;

	if (UDMMaterialComponent* Component = Cast<UDMMaterialComponent>(InObject))
	{
		RowGeneratorArgs.NotifyHook = Component;
	}

	PropertyHandle.PropertyRowGenerator = PropertyEditor.CreatePropertyRowGenerator(RowGeneratorArgs);
	PropertyHandle.PropertyRowGenerator->SetObjects({InObject});

	if (const TSharedPtr<IDetailTreeNode> FoundTreeNode = SearchGeneratorForNode(
		PropertyHandle.PropertyRowGenerator.ToSharedRef(), InPropertyName))
	{
		PropertyHandle.DetailTreeNode = FoundTreeNode;
		PropertyHandle.PropertyHandle = FoundTreeNode->CreatePropertyHandle();
		return PropertyHandle;
	}

	return PropertyHandle;
}

TSharedPtr<IDetailTreeNode> FDMWidgetStatics::SearchNodesForProperty(const TArray<TSharedRef<IDetailTreeNode>>& InNodes, FName InPropertyName)
{
	for (const TSharedRef<IDetailTreeNode>& ChildNode : InNodes)
	{
		switch (ChildNode->GetNodeType())
		{
			case EDetailNodeType::Category:
			{
				TArray<TSharedRef<IDetailTreeNode>> CategoryChildNodes;
				ChildNode->GetChildren(CategoryChildNodes);

				if (TSharedPtr<IDetailTreeNode> FoundNode = SearchNodesForProperty(CategoryChildNodes, InPropertyName))
				{
					return FoundNode;
				}

				break;
			}

			case EDetailNodeType::Item:
				if (ChildNode->GetNodeName() == InPropertyName)
				{
					return ChildNode;
				}
				break;

			default:
				// Do nothing
				break;
		}
	}

	return nullptr;
}

TSharedPtr<IDetailTreeNode> FDMWidgetStatics::SearchGeneratorForNode(const TSharedRef<IPropertyRowGenerator>& InGenerator, FName InPropertyName)
{
	return SearchNodesForProperty(InGenerator->GetRootTreeNodes(), InPropertyName);
}

TSharedPtr<IPropertyRowGenerator> FDMWidgetStatics::SearchForGenerator(const TArray<FDMPropertyHandle>& InPropertyHandles, UObject* InObject)
{
	if (!InObject)
	{
		return nullptr;
	}

	for (const FDMPropertyHandle& PropertyHandle : InPropertyHandles)
	{
		if (PropertyHandle.PropertyRowGenerator.IsValid())
		{
			for (const TWeakObjectPtr<UObject>& WeakObject : PropertyHandle.PropertyRowGenerator->GetSelectedObjects())
			{
				if (WeakObject.Get() == InObject)
				{
					return PropertyHandle.PropertyRowGenerator;
				}
			}
		}
	}

	return nullptr;
}

void FDMWidgetStatics::AddPropertyMetaData(UObject* InObject, FName InPropertyName, FDMPropertyHandle& InPropertyHandle)
{
	FProperty* Property = nullptr;

	if (InPropertyHandle.PropertyHandle.IsValid())
	{
		TSharedRef<IPropertyHandle> PropertyHandleRef = InPropertyHandle.PropertyHandle.ToSharedRef();

		InPropertyHandle.Priority = GetPriority(PropertyHandleRef);
		InPropertyHandle.bKeyframeable = IsKeyframeable(PropertyHandleRef);

		Property = InPropertyHandle.PropertyHandle->GetProperty();

		if (UDMMaterialValueFloat* FloatValue = Cast<UDMMaterialValueFloat>(InObject))
		{
			if (FloatValue->HasValueRange())
			{
				const FName UIMin = FName("UIMin");
				const FName UIMax = FName("UIMax");
				const FName ClampMin = FName("ClampMin");
				const FName ClampMax = FName("ClampMax");

				InPropertyHandle.PropertyHandle->SetInstanceMetaData(UIMin, FString::SanitizeFloat(FloatValue->GetValueRange().Min));
				InPropertyHandle.PropertyHandle->SetInstanceMetaData(ClampMin, FString::SanitizeFloat(FloatValue->GetValueRange().Min));
				InPropertyHandle.PropertyHandle->SetInstanceMetaData(UIMax, FString::SanitizeFloat(FloatValue->GetValueRange().Max));
				InPropertyHandle.PropertyHandle->SetInstanceMetaData(ClampMax, FString::SanitizeFloat(FloatValue->GetValueRange().Max));
			}
		}
	}
	else if (IsValid(InObject))
	{
		Property = InObject->GetClass()->FindPropertyByName(InPropertyName);
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		uint8 ComponentCount = 1;

		if (StructProperty->Struct == TBaseStructure<FVector2D>::Get()
			|| StructProperty->Struct == TVariantStructure<FVector2f>::Get())
		{
			ComponentCount = 2;
		}

		if (StructProperty->Struct == TBaseStructure<FVector>::Get()
			|| StructProperty->Struct == TVariantStructure<FVector3f>::Get()
			|| StructProperty->Struct == TBaseStructure<FRotator>::Get())
		{
			ComponentCount = 3;
		}

		// FLinearColor doesn't need the extra space
		if (StructProperty->Struct == TBaseStructure<FVector4>::Get()
			|| StructProperty->Struct == TVariantStructure<FVector4f>::Get())
		{
			ComponentCount = 4;
		}

		switch (ComponentCount)
		{
			case 0:
			case 1:
				break;

			case 2:
				InPropertyHandle.MaxWidth = 200.f;
				break;

				// 3 and above
			default:
				InPropertyHandle.MaxWidth = 275.f;
				break;
		}
	}
}

EDMPropertyHandlePriority FDMWidgetStatics::GetPriority(const TSharedRef<IPropertyHandle>& InPropertyHandle)
{
	if (InPropertyHandle->HasMetaData("HighPriority"))
	{
		return EDMPropertyHandlePriority::High;
	}

	if (InPropertyHandle->HasMetaData("LowPriority"))
	{
		return EDMPropertyHandlePriority::Low;
	}

	return EDMPropertyHandlePriority::Normal;
}

bool FDMWidgetStatics::IsKeyframeable(const TSharedRef<IPropertyHandle>& InPropertyHandle)
{
	return !InPropertyHandle->HasMetaData("NotKeyframeable");
}
