// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GDTF/AttributeDefinitions/DMXGDTFPhysicalUnit.h"
#include "Layouts/Controllers/DMXControlConsoleElementController.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"

class UDMXControlConsoleEditorModel;
class UDMXControlConsoleElementController;
class UDMXControlConsoleFaderBase;
class UDMXControlConsoleFixturePatchMatrixCell;


namespace UE::DMX::Private
{
	/** Model for an element controller in the Control Console */
	class FDMXControlConsoleElementControllerModel
		: public TSharedFromThis<FDMXControlConsoleElementControllerModel>
	{
	public:
		/** Constructor */
		FDMXControlConsoleElementControllerModel(const TWeakObjectPtr<UDMXControlConsoleElementController> InWeakElementController, const TWeakObjectPtr<UDMXControlConsoleEditorModel> InWeakEditorModel);

		/** Gets the Element Controller this model is based on */
		UDMXControlConsoleElementController* GetElementController() const;

		/** Gets the first available Fader in the Element Controller, if valid */
		UDMXControlConsoleFaderBase* GetFirstAvailableFader() const;

		/** Gets the first Matrix Cell Element in the Controller, if valid */
		UDMXControlConsoleFixturePatchMatrixCell* GetMatrixCellElement() const;

		/** Gets all Element Controllers in the active layout matching this Element Controller's attribute (or name, if attribute is not valid) */
		TArray<UDMXControlConsoleElementController*> GetMatchingAttributeElementControllers(bool bSameOwnerControllerOnly = false) const;

		/** Gets the name of the Element Controller, relative to the contained Elements */
		FString GetRelativeControllerName() const;

		/** Gets the value of the Element Controller, relative to the contained Elements */
		float GetRelativeValue() const;

		/** Gets the min value of the Element Controller, relative to the contained Elements */
		float GetRelativeMinValue() const;

		/** Gets the max value of the Element Controller, relative to the contained Elements */
		float GetRelativeMaxValue() const;

		/** Gets the physical unit of the Element Controller, relative to the contained Elements */
		EDMXGDTFPhysicalUnit GetPhysicalUnit() const;

		/** Gets the physical value of the Element Controller, relative to the contained Elements */
		double GetPhysicalValue() const;

		/** Gets the physical from value of the Element Controller, relative to the contained Elements */
		double GetPhysicalFrom() const;

		/** Gets the physical to value of the Element Controller, relative to the contained Elements */
		double GetPhysicalTo() const;

		/** True if the Controller has just one Element */
		bool HasSingleElement() const;

		/** True if the Controller has Elements with the same data type */
		bool HasUniformDataType() const;

		/** True if the Controller has Elements with the same physical unit */
		bool HasUniformPhysicalUnit() const;

		/** True if the Controller has Elements with the same value */
		bool HasUniformValue() const;

		/** True if the Controller has Elements with the same min value */
		bool HasUniformMinValue() const;

		/** True if the Controller has Elements with the same max value */
		bool HasUniformMaxValue() const;

		/** True if all Elements in the controller are raw faders */
		bool HasOnlyRawFaders() const;

		/** True if all the Elements in the Controller are locked */
		bool IsLocked() const;

	private:
		/** Weak reference to the Element Controller this model is based on */
		TWeakObjectPtr<UDMXControlConsoleElementController> WeakElementController;

		/** Weak reference to the Control Console edior model */
		TWeakObjectPtr<UDMXControlConsoleEditorModel> WeakEditorModel;
	};
}
