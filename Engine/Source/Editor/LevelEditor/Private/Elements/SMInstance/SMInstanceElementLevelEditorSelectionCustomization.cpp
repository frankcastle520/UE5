// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/SMInstance/SMInstanceElementLevelEditorSelectionCustomization.h"
#include "Elements/Component/ComponentElementLevelEditorSelectionCustomization.h"
#include "Elements/SMInstance/SMInstanceElementData.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "Elements/Framework/TypedElementList.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementInterfaceCustomization.h"
#include "Elements/Interfaces/TypedElementHierarchyInterface.h"

#include "LevelUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogSMInstanceLevelEditorSelection, Log, All);

bool FSMInstanceElementLevelEditorSelectionCustomization::CanSelectElement(const TTypedElement<ITypedElementSelectionInterface>& InElementSelectionHandle, const FTypedElementSelectionOptions& InSelectionOptions)
{
	const FSMInstanceManager SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandleChecked(InElementSelectionHandle);
	if (!SMInstance)
	{
		return false;
	}

	AActor* Owner = SMInstance.GetISMComponent()->GetOwner();
	AActor* SelectionRoot = Owner->GetRootSelectionParent();
	ULevel* SelectionLevel = (SelectionRoot != nullptr) ? SelectionRoot->GetLevel() : Owner->GetLevel();
	if (!Owner->IsTemplate() && FLevelUtils::IsLevelLocked(SelectionLevel))
	{
		return false;
	}
	
	// Bail if global selection is locked
	return !GEdSelectionLock;
}

bool FSMInstanceElementLevelEditorSelectionCustomization::CanDeselectElement(const TTypedElement<ITypedElementSelectionInterface>& InElementSelectionHandle, const FTypedElementSelectionOptions& InSelectionOptions)
{
	const FSMInstanceManager SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandleChecked(InElementSelectionHandle);
	if (!SMInstance)
	{
		return false;
	}

	// Bail if global selection is locked
	return !GEdSelectionLock;
}

bool FSMInstanceElementLevelEditorSelectionCustomization::SelectElement(const TTypedElement<ITypedElementSelectionInterface>& InElementSelectionHandle, FTypedElementListRef InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	const FSMInstanceManager SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandleChecked(InElementSelectionHandle);
	if (!SMInstance)
	{
		return false;
	}

	if (!InElementSelectionHandle.SelectElement(InSelectionSet, InSelectionOptions))
	{
		return false;
	}

	UE_LOG(LogSMInstanceLevelEditorSelection, Verbose, TEXT("Selected SMInstance: %s (%s), Index %d"), *SMInstance.GetISMComponent()->GetPathName(), *SMInstance.GetISMComponent()->GetClass()->GetName(), SMInstance.GetISMInstanceIndex());
	
	return true;
}

bool FSMInstanceElementLevelEditorSelectionCustomization::DeselectElement(const TTypedElement<ITypedElementSelectionInterface>& InElementSelectionHandle, FTypedElementListRef InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	const FSMInstanceManager SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandleChecked(InElementSelectionHandle);
	if (!SMInstance)
	{
		return false;
	}

	if (!InElementSelectionHandle.DeselectElement(InSelectionSet, InSelectionOptions))
	{
		return false;
	}

	UE_LOG(LogSMInstanceLevelEditorSelection, Verbose, TEXT("Deselected SMInstance: %s (%s), Index %d"), *SMInstance.GetISMComponent()->GetPathName(), *SMInstance.GetISMComponent()->GetClass()->GetName(), SMInstance.GetISMInstanceIndex());

	return true;
}

FTypedElementHandle FSMInstanceElementLevelEditorSelectionCustomization::GetSelectionElement(const TTypedElement<ITypedElementSelectionInterface>& InElementSelectionHandle, FTypedElementListConstRef InCurrentSelection, const ETypedElementSelectionMethod InSelectionMethod)
{
	const FSMInstanceManager SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandleChecked(InElementSelectionHandle);
	if (!SMInstance)
	{
		return InElementSelectionHandle;
	}

	if (const UInstancedStaticMeshComponent* Component = SMInstance.GetISMComponent())
	{
		const FTypedElementHandle OwningComponentHandle = UEngineElementsLibrary::AcquireEditorComponentElementHandle(Component);

		const bool bWasDoubleClick = InSelectionMethod == ETypedElementSelectionMethod::Secondary;
		const bool bComponentAlreadySelected = InCurrentSelection->Contains(OwningComponentHandle);
		const bool bIsISMAlreadySelected = InCurrentSelection->Contains(InElementSelectionHandle);

		bool bIsSiblingSelected = false;
		if (InCurrentSelection->HasElementsOfType(InElementSelectionHandle.GetId().GetTypeId()))
		{
			const FSMInstanceManager SelectedSMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandleChecked(InCurrentSelection->GetTopElement<ITypedElementHierarchyInterface>());
			if (SelectedSMInstance && (SelectedSMInstance.GetISMComponent() == Component))
			{
				bIsSiblingSelected = true;
			}
		}

		// If a single click, and the ISM was already selected, defer to the component selection, so that we traverse the hierarchy one level instead of picking the top level selection again if we're hit on the same selection.
		if (!bWasDoubleClick && (bIsSiblingSelected || bIsISMAlreadySelected))
		{
			return FComponentElementLevelEditorSelectionCustomization::GetSelectionElementStatic(OwningComponentHandle, InCurrentSelection, ETypedElementSelectionMethod::FromSecondary);
		}
		
		// consider the component already selected if it is in the selection, or if the ISM handle that was hit is a sibling to the current handle
		if (bWasDoubleClick && (bIsSiblingSelected || bComponentAlreadySelected))
		{
			return InElementSelectionHandle;
		}

		return FComponentElementLevelEditorSelectionCustomization::GetSelectionElementStatic(OwningComponentHandle, InCurrentSelection, InSelectionMethod);
	}

	return InElementSelectionHandle;
}
