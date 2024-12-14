// Copyright Epic Games, Inc. All Rights Reserved.

#include "Extensions/UIComponentContainer.h"

#include "Blueprint/WidgetTree.h"
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Extensions/UIComponent.h"
#include "Slate/SObjectWidget.h"

FUIComponentTarget::FUIComponentTarget()
{}

FUIComponentTarget::FUIComponentTarget(UUIComponent* InComponent, FName InChildName)
{
	TargetName = InChildName;
	Component = InComponent;
}

UWidget* FUIComponentTarget::Resolve(const UWidgetTree* WidgetTree)
{
	if (!TargetName.IsNone() && WidgetTree)
	{
		return WidgetTree->FindWidget(TargetName);
	}

	return nullptr;
}

void FUIComponentTarget::SetTargetName(FName NewName)
{
	TargetName = NewName;
}

void UUIComponentContainer::Initialize()
{
	if (UUserWidget* Owner = GetUserWidget())
	{
		// Before intializing, we resolve all components
		for (FUIComponentTarget& ComponentTarget : Components)
		{
			if (UWidget* Widget = ComponentTarget.Resolve(Owner->WidgetTree))
			{
				if (UUIComponent* Component = ComponentTarget.GetComponent())
				{
					Component->Initialize(Widget);
				}
			}
		}
	}
}

void UUIComponentContainer::Construct()
{
	for (FUIComponentTarget& ComponentTarget : Components)
	{
		if(UUIComponent* Component = ComponentTarget.GetComponent())
		{
			if (TStrongObjectPtr<UWidget> Owner = Component->GetOwner().Pin())
			{
				Component->Construct();
			}
		}		
	}
}

void UUIComponentContainer::Destruct()
{
	for (FUIComponentTarget& ComponentTarget : Components)
	{
		if (UUIComponent* Component = ComponentTarget.GetComponent())
		{
			if (TStrongObjectPtr<UWidget> Owner = Component->GetOwner().Pin())
			{
				Component->Destruct();
			}
		}
	}
}


TArray<UUIComponent*> UUIComponentContainer::GetExtensionsFor(FName TargetName)
{
	TArray<UUIComponent*> Result;

	for (FUIComponentTarget& Target : Components)
	{
		if (Target.GetTargetName() == TargetName)
		{
			Result.Push(Target.GetComponent());
		}
	}

	return MoveTemp(Result);
}

void UUIComponentContainer::AddComponent(FName TargetName, UUIComponent* Component)
{
	ensure(!TargetName.IsNone() && Component != nullptr);
	Components.Emplace(FUIComponentTarget(Component, TargetName));
}

void UUIComponentContainer::RemoveComponent(FName TargetName, UUIComponent* Component)
{
	ensure(!TargetName.IsNone() && Component != nullptr);

	for (int i = 0; i < Components.Num(); i++)
	{
		FUIComponentTarget& Target = Components[i];
		if (Target.GetComponent() == Component)
		{
			ensure(Target.GetTargetName() == TargetName);
			Components.RemoveAtSwap(i);
			return;
		}
	}
}

void UUIComponentContainer::Resolve()
{
	if (UUserWidget* Owner = GetUserWidget())
	{
		for (FUIComponentTarget& Component : Components)
		{
			Component.Resolve(Owner->WidgetTree);
		}
	}
}

void UUIComponentContainer::RemoveAllComponentsFor(FName TargetName)
{
	for (int32 Index = Components.Num() - 1; Index >= 0; --Index)
	{
		FUIComponentTarget& Target = Components[Index];
		if (Target.GetTargetName() == TargetName)
		{
			Components.RemoveAtSwap(Index);
		}
	}
}

void UUIComponentContainer::RenameWidget(FName OldName, FName NewName)
{
	bool bModified = false;
	auto UpdateModify = [&bModified, Self = this]()
		{
			if (!bModified)
			{
				bModified = true;
				Self->Modify();
			}
		};

	for (FUIComponentTarget& Target : Components)
	{
		if (Target.GetTargetName() == OldName)
		{
			UpdateModify();
			Target.SetTargetName(NewName);

			// @TODO Do our widget components need to be notified of name change?

			//if (Component.GetComponent())
			//{
			//	Component.GetComponent()->WidgetRenamed(OldName, NewName);
			//}
		}
	}
}


bool UUIComponentContainer::IsEmpty() const 
{
	return Components.IsEmpty();
}

void UUIComponentContainer::OnPreviewContentChanged(TSharedRef<SWidget> NewContent)
{
	if (TSharedPtr<SObjectWidget> ObjectWidget = StaticCastSharedPtr<SObjectWidget>(NewContent.ToSharedPtr()))
	{
		CleanupUIComponents(ObjectWidget->GetWidgetObject());
	}
}

void UUIComponentContainer::CleanupUIComponents(UUserWidget* UserWidget)
{
	if (UserWidget)
	{
		if (const UWidgetTree* WidgetTree = UserWidget->WidgetTree)
		{
			bool bModified = false;
			auto UpdateModify = [&bModified, Self = this]()
			{
				if (!bModified)
				{
					bModified = true;
					Self->Modify();
				}
			};
			
			TArray <FName, TInlineAllocator<4>> TargetNamesToRemove;
			for (int32 Index = Components.Num() - 1; Index >= 0; --Index)
			{
				// Remove components that are invalid
				if (Components[Index].GetComponent() == nullptr)
				{
					UpdateModify();
					Components.RemoveAtSwap(Index);
				}
				else if (!Components[Index].GetTargetName().IsNone())
				{
					// Fill list with all Valid Target Names
					TargetNamesToRemove.AddUnique(Components[Index].GetTargetName());
				}
			}

			// Exclude components in use from removal list
			if (TargetNamesToRemove.Num() > 0)
			{
				WidgetTree->ForEachWidget([&TargetNamesToRemove, this](TObjectPtr<UWidget> Widget) {
					if (Widget)
					{
						for (int32 Index = TargetNamesToRemove.Num() - 1; Index >= 0; Index--)
						{
							const FName TargetName = TargetNamesToRemove[Index];
							if (TargetName == Widget->GetFName())
							{
								TargetNamesToRemove.RemoveSingleSwap(TargetName);
							}
						}
					}
					});
			}

			// Remove all unused component 
			if (TargetNamesToRemove.Num() > 0)
			{
				UpdateModify();

				for (FName TargetName : TargetNamesToRemove)
				{
					RemoveAllComponentsFor(TargetName);
				}
			}
		}
	}
}