// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleElementControllerModel.h"

#include "Algo/AllOf.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFixturePatchMatrixCell.h"
#include "DMXControlConsoleFixturePatchFunctionFader.h"
#include "DMXControlConsoleRawFader.h"
#include "IDMXControlConsoleFaderGroupElement.h"
#include "Layouts/Controllers/DMXControlConsoleFaderGroupController.h"
#include "Layouts/DMXControlConsoleEditorGlobalLayoutBase.h"
#include "Layouts/DMXControlConsoleEditorLayouts.h"
#include "Models/DMXControlConsoleEditorModel.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleElementControllerModel"

namespace UE::DMX::Private
{
	FDMXControlConsoleElementControllerModel::FDMXControlConsoleElementControllerModel(const TWeakObjectPtr<UDMXControlConsoleElementController> InWeakElementController, const TWeakObjectPtr<UDMXControlConsoleEditorModel> InWeakEditorModel)
		: WeakElementController(InWeakElementController)
		, WeakEditorModel(InWeakEditorModel)
	{}

	UDMXControlConsoleElementController* FDMXControlConsoleElementControllerModel::GetElementController() const
	{
		return WeakElementController.Get();
	}

	UDMXControlConsoleFaderBase* FDMXControlConsoleElementControllerModel::GetFirstAvailableFader() const
	{
		if (!WeakElementController.IsValid() || WeakElementController->GetElements().IsEmpty())
		{
			return nullptr;
		}

		const TArray<UDMXControlConsoleFaderBase*> Faders = WeakElementController->GetFaders();
		if (Faders.IsEmpty())
		{
			return nullptr;
		}

		return Faders[0];
	}

	UDMXControlConsoleFixturePatchMatrixCell* FDMXControlConsoleElementControllerModel::GetMatrixCellElement() const
	{
		if (!WeakElementController.IsValid())
		{
			return nullptr;
		}

		const TArray<TScriptInterface<IDMXControlConsoleFaderGroupElement>> Elements = WeakElementController->GetElements();
		if (Elements.IsEmpty())
		{
			return nullptr;
		}

		const bool bHasOnlyMatrixCellElements = 
			Algo::AllOf(Elements,
			[](const TScriptInterface<IDMXControlConsoleFaderGroupElement>& Element)
			{
				return Element && IsValid(Cast<UDMXControlConsoleFixturePatchMatrixCell>(Element.GetObject()));
			});

		return bHasOnlyMatrixCellElements ? Cast<UDMXControlConsoleFixturePatchMatrixCell>(Elements[0].GetObject()) : nullptr;
	}

	TArray<UDMXControlConsoleElementController*> FDMXControlConsoleElementControllerModel::GetMatchingAttributeElementControllers(bool bSameOwnerControllerOnly) const
	{
		TArray<UDMXControlConsoleElementController*> MatchingAttributeElementControllers;

		const UDMXControlConsoleElementController* ThisController = GetElementController();
		const UDMXControlConsoleFaderBase* FirstFader = GetFirstAvailableFader();

		const UDMXControlConsoleEditorLayouts* ControlConsoleLayouts = WeakEditorModel.IsValid() ? WeakEditorModel->GetControlConsoleLayouts() : nullptr;
		const UDMXControlConsoleEditorGlobalLayoutBase* ActiveLayout = ControlConsoleLayouts ? ControlConsoleLayouts->GetActiveLayout() : nullptr;
		if (!ThisController || !FirstFader || !ActiveLayout)
		{
			return MatchingAttributeElementControllers;
		}

		// Find all controllers that match this controller's attribute (or name, if no attribute is available)
		FName AttributeNameToSelect = *FirstFader->GetFaderName();
		if (const UDMXControlConsoleFixturePatchFunctionFader* FirstFunctionFader = Cast<UDMXControlConsoleFixturePatchFunctionFader>(FirstFader))
		{
			AttributeNameToSelect = FirstFunctionFader->GetAttributeName().Name;
		}

		const UDMXControlConsoleFaderGroupController& OwnerFaderGroupController = ThisController->GetOwnerFaderGroupControllerChecked();
		const TArray<UDMXControlConsoleFaderGroupController*> FaderGroupControllers = ActiveLayout->GetAllFaderGroupControllers();
		for (UDMXControlConsoleFaderGroupController* FaderGroupController : FaderGroupControllers)
		{
			if (!FaderGroupController)
			{
				continue;
			}

			if (bSameOwnerControllerOnly && FaderGroupController != &OwnerFaderGroupController)
			{
				continue;
			}

			const TArray<UDMXControlConsoleElementController*> ElementControllers = FaderGroupController->GetAllElementControllers();
			for (UDMXControlConsoleElementController* ElementController : ElementControllers)
			{
				if (!ElementController)
				{
					continue;
				}

				const TArray<UDMXControlConsoleFaderBase*> Faders = ElementController->GetFaders();
				if (Faders.IsEmpty())
				{
					continue;
				}

				FName AttributeName = *Faders[0]->GetFaderName();
				if (const UDMXControlConsoleFixturePatchFunctionFader* FunctionFader = Cast<UDMXControlConsoleFixturePatchFunctionFader>(Faders[0]))
				{
					AttributeName = FunctionFader->GetAttributeName().Name;
				}

				if (AttributeName == AttributeNameToSelect)
				{
					MatchingAttributeElementControllers.Add(ElementController);
				}
			}
		}

		return MatchingAttributeElementControllers;
	}

	FString FDMXControlConsoleElementControllerModel::GetRelativeControllerName() const
	{
		if (!WeakElementController.IsValid())
		{
			return FString();
		}

		if (!HasSingleElement())
		{
			return WeakElementController->GetUserName();
		}

		const UDMXControlConsoleFaderBase* FirstFader = GetFirstAvailableFader();
		if (!FirstFader)
		{
			return WeakElementController->GetUserName();
		}

		return FirstFader->GetFaderName();
	}

	float FDMXControlConsoleElementControllerModel::GetRelativeValue() const
	{
		if (!WeakElementController.IsValid())
		{
			return 0.f;
		}

		if (!HasUniformDataType())
		{
			return WeakElementController->GetValue();
		}

		const UDMXControlConsoleFaderBase* FirstFader = GetFirstAvailableFader();
		if (!FirstFader)
		{
			return WeakElementController->GetValue();
		}

		return static_cast<float>(FirstFader->GetValue());
	}

	float FDMXControlConsoleElementControllerModel::GetRelativeMinValue() const
	{
		if (!WeakElementController.IsValid())
		{
			return 0.f;
		}

		if (!HasUniformDataType())
		{
			return WeakElementController->GetMinValue();
		}

		const UDMXControlConsoleFaderBase* FirstFader = GetFirstAvailableFader();
		if (!FirstFader)
		{
			return WeakElementController->GetMinValue();
		}

		return static_cast<float>(FirstFader->GetMinValue());
	}

	float FDMXControlConsoleElementControllerModel::GetRelativeMaxValue() const
	{
		if (!WeakElementController.IsValid())
		{
			return 0.f;
		}

		if (!HasUniformDataType())
		{
			return WeakElementController->GetMaxValue();
		}

		const UDMXControlConsoleFaderBase* FirstFader = GetFirstAvailableFader();
		if (!FirstFader)
		{
			return WeakElementController->GetMaxValue();
		}

		return static_cast<float>(FirstFader->GetMaxValue());
	}

	EDMXGDTFPhysicalUnit FDMXControlConsoleElementControllerModel::GetPhysicalUnit() const
	{
		if (!HasUniformPhysicalUnit())
		{
			return EDMXGDTFPhysicalUnit::None;
		}

		const UDMXControlConsoleFixturePatchFunctionFader* FirstFader = Cast<UDMXControlConsoleFixturePatchFunctionFader>(GetFirstAvailableFader());
		if (!FirstFader)
		{
			return EDMXGDTFPhysicalUnit::None;
		}

		return FirstFader->GetPhysicalUnit();
	}

	double FDMXControlConsoleElementControllerModel::GetPhysicalValue() const
	{
		if (!HasUniformPhysicalUnit())
		{
			return GetRelativeValue();
		}

		const UDMXControlConsoleFixturePatchFunctionFader* FirstFader = Cast<UDMXControlConsoleFixturePatchFunctionFader>(GetFirstAvailableFader());
		if (!FirstFader)
		{
			return GetRelativeValue();
		}

		return FirstFader->GetPhysicalValue();
	}

	double FDMXControlConsoleElementControllerModel::GetPhysicalFrom() const
	{
		if (!HasUniformPhysicalUnit())
		{
			return GetRelativeValue();
		}

		const UDMXControlConsoleFixturePatchFunctionFader* FirstFader = Cast<UDMXControlConsoleFixturePatchFunctionFader>(GetFirstAvailableFader());
		if (!FirstFader)
		{
			return GetRelativeValue();
		}

		return FirstFader->GetPhysicalFrom();
	}

	double FDMXControlConsoleElementControllerModel::GetPhysicalTo() const
	{
		if (!HasUniformPhysicalUnit())
		{
			return GetRelativeValue();
		}

		const UDMXControlConsoleFixturePatchFunctionFader* FirstFader = Cast<UDMXControlConsoleFixturePatchFunctionFader>(GetFirstAvailableFader());
		if (!FirstFader)
		{
			return GetRelativeValue();
		}

		return FirstFader->GetPhysicalTo();
	}

	bool FDMXControlConsoleElementControllerModel::HasSingleElement() const
	{
		return WeakElementController.IsValid() && WeakElementController->GetElements().Num() == 1;
	}

	bool FDMXControlConsoleElementControllerModel::HasUniformDataType() const
	{
		if (!WeakElementController.IsValid() || WeakElementController->GetElements().IsEmpty())
		{
			return false;
		}

		const TArray<UDMXControlConsoleFaderBase*> Faders = WeakElementController->GetFaders();
		if (Faders.IsEmpty())
		{
			return false;
		}

		const UDMXControlConsoleFaderBase* FirstFader = Faders[0];
		const bool bHasUniformDataType = Algo::AllOf(Faders, 
			[FirstFader](const UDMXControlConsoleFaderBase* Fader)
			{
				return Fader && Fader->GetDataType() == FirstFader->GetDataType();
			});

		return bHasUniformDataType;
	}

	bool FDMXControlConsoleElementControllerModel::HasUniformPhysicalUnit() const
	{
		if (!WeakElementController.IsValid() || WeakElementController->GetElements().IsEmpty())
		{
			return false;
		}

		const TArray<UDMXControlConsoleFaderBase*> Faders = WeakElementController->GetFaders();
		if (Faders.IsEmpty())
		{
			return false;
		}

		const UDMXControlConsoleFixturePatchFunctionFader* FirstFader = Cast<UDMXControlConsoleFixturePatchFunctionFader>(Faders[0]);
		if (!FirstFader)
		{
			return false;
		}

		const bool bHasUniformPhysicalUnit = Algo::AllOf(Faders,
			[FirstFader](const UDMXControlConsoleFaderBase* Fader)
			{
				const UDMXControlConsoleFixturePatchFunctionFader* FunctionFader = Cast<UDMXControlConsoleFixturePatchFunctionFader>(Fader);
				return FunctionFader && FunctionFader->GetPhysicalUnit() == FirstFader->GetPhysicalUnit();
			});

		return bHasUniformPhysicalUnit;
	}

	bool FDMXControlConsoleElementControllerModel::HasUniformValue() const
	{
		if (!WeakElementController.IsValid())
		{
			return false;
		}

		const TArray<UDMXControlConsoleFaderBase*> Faders = WeakElementController->GetFaders();
		if (Faders.IsEmpty())
		{
			return false;
		}

		const UDMXControlConsoleFaderBase* FirstFader = Faders[0];
		if (!FirstFader)
		{
			return false;
		}

		// Check if the values of all the faders in the controller are uniform
		const bool bHasUniformValue = Algo::AllOf(Faders, 
			[FirstFader](const UDMXControlConsoleFaderBase* Fader)
			{
				return Fader && Fader->GetValue() == FirstFader->GetValue();
			});

		return bHasUniformValue;
	}

	bool FDMXControlConsoleElementControllerModel::HasUniformMinValue() const
	{
		if (!WeakElementController.IsValid())
		{
			return false;
		}

		const TArray<UDMXControlConsoleFaderBase*> Faders = WeakElementController->GetFaders();
		if (Faders.IsEmpty())
		{
			return false;
		}

		const UDMXControlConsoleFaderBase* FirstFader = Faders[0];
		if (!FirstFader)
		{
			return false;
		}

		// Check if the values of all the faders in the controller are uniform
		const bool bHasUniformMinValue = Algo::AllOf(Faders, 
			[FirstFader](const UDMXControlConsoleFaderBase* Fader)
			{
				return Fader && Fader->GetMinValue() == FirstFader->GetMinValue();
			});

		return bHasUniformMinValue;
	}

	bool FDMXControlConsoleElementControllerModel::HasUniformMaxValue() const
	{
		if (!WeakElementController.IsValid())
		{
			return false;
		}

		const TArray<UDMXControlConsoleFaderBase*> Faders = WeakElementController->GetFaders();
		if (Faders.IsEmpty())
		{
			return false;
		}

		const UDMXControlConsoleFaderBase* FirstFader = Faders[0];
		if (!FirstFader)
		{
			return false;
		}

		// Check if the values of all the faders in the controller are uniform
		const bool bHasUniformMaxValue = Algo::AllOf(Faders, 
			[FirstFader](const UDMXControlConsoleFaderBase* Fader)
			{
				return Fader && Fader->GetMaxValue() == FirstFader->GetMaxValue();
			});

		return bHasUniformMaxValue;
	}

	bool FDMXControlConsoleElementControllerModel::HasOnlyRawFaders() const
	{
		if (!WeakElementController.IsValid())
		{
			return false;
		}

		const TArray<TScriptInterface<IDMXControlConsoleFaderGroupElement>>& Elements = WeakElementController->GetElements();
		return Algo::AllOf(Elements, [](const TScriptInterface<IDMXControlConsoleFaderGroupElement>& Element)
			{
				return Element && IsValid(Cast<UDMXControlConsoleRawFader>(Element.GetObject()));
			});
	}

	bool FDMXControlConsoleElementControllerModel::IsLocked() const
	{
		return WeakElementController.IsValid() && WeakElementController->IsLocked();
	}
}

#undef LOCTEXT_NAMESPACE 
