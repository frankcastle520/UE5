// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ContainersFwd.h"
#include "Templates/SharedPointerFwd.h"

enum class EDMPropertyHandlePriority : uint8;
class FName;
class FProperty;
class SDMMaterialComponentEditor;
class UDMMaterialComponent;
struct FDMPropertyHandle;

/** Used to generate editable properties for objects editable in the Material Designer. */
class FDMComponentPropertyRowGenerator
{
public:
	DYNAMICMATERIALEDITOR_API static const TSharedRef<FDMComponentPropertyRowGenerator>& Get();

	virtual ~FDMComponentPropertyRowGenerator() = default;

	/**
	 * Generate properties for the given component.
	 * @param InComponentEditorWidget The edit widget generating the properties.
	 * @param InComponent The component being edited.
	 * @param InOutPropertyRows The generated rows.
	 * @param InOutProcessedObjects The already processed objects - add to this to avoid possible recursive generation.
	 */
	DYNAMICMATERIALEDITOR_API virtual void AddComponentProperties(const TSharedRef<SDMMaterialComponentEditor>& InComponentEditorWidget, 
		UDMMaterialComponent* InComponent, TArray<FDMPropertyHandle>& InOutPropertyRows, TSet<UDMMaterialComponent*>& InOutProcessedObjects);

	/**
	 * Add the rows needed for a specific property.
	 * @param InProperty The name of the property.
	 */
	DYNAMICMATERIALEDITOR_API virtual void AddPropertyEditRows(const TSharedRef<SDMMaterialComponentEditor>& InComponentEditorWidget, 
		UDMMaterialComponent* InComponent, const FName& InProperty, TArray<FDMPropertyHandle>& InOutPropertyRows, 
		TSet<UDMMaterialComponent*>& InOutProcessedObjects);

	/**
	 * Adds the rows needed for a specific property.
	 * @param InProperty The FProperty for this property.
	 * @param MemoryPtr The pointer to the property's value in memory
	 */
	DYNAMICMATERIALEDITOR_API virtual void AddPropertyEditRows(const TSharedRef<SDMMaterialComponentEditor>& InComponentEditorWidget, 
		UDMMaterialComponent* InComponent, FProperty* InProperty, void* MemoryPtr, TArray<FDMPropertyHandle>& InOutPropertyRows, 
		TSet<UDMMaterialComponent*>& InOutProcessedObjects);

	DYNAMICMATERIALEDITOR_API virtual bool AllowKeyframeButton(UDMMaterialComponent* InComponent, FProperty* InProperty);

protected:
	/** Returns true if the component edit widget is editing a Material Designer Dynamic. */
	DYNAMICMATERIALEDITOR_API static bool IsDynamic(const TSharedRef<SDMMaterialComponentEditor>& InComponentEditorWidget);
};
