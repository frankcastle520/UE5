// Copyright Epic Games, Inc. All Rights Reserved.

#include "OutputProviderLayoutCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Output/VCamOutputProviderBase.h"
#include "UI/VCamWidget.h"
#include "Widgets/VPFullScreenUserWidget.h"
#include "VCamCoreEditorModule.h"

#include "Blueprint/WidgetTree.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailGroup.h"
#include "PropertyCustomizationHelpers.h"
#include "SSimpleComboButton.h"
#include "Util/WidgetTreeUtils.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FOutputProviderCustomization"

namespace UE::VCamCoreEditor
{
	namespace Private
	{
		/** Static because IDetailCustomization is destroyed when details panel is refreshed */
		static FTargetConnectionDisplaySettings DisplaySettings;
		
		struct FWidgetDisplayInfo
		{
			FName UniqueName;
			FText WidgetDisplayName;
			bool bNeedsToDisplayParentWidgetName;
		};
		
		static TMap<TWeakObjectPtr<UVCamWidget>, FWidgetDisplayInfo> GenerateWidgetRowNames(TArray<TWeakObjectPtr<UVCamWidget>> SortedWidgets)
		{
			TMap<FName, int32> NameCounter;
			TMap<TWeakObjectPtr<UVCamWidget>, FWidgetDisplayInfo> TargetDisplayInfo;
			for (const TWeakObjectPtr<UVCamWidget>& Widget : SortedWidgets)
			{
				FName UniqueNameForWidget = Widget->GetFName();
				int32& NameCount = NameCounter.FindOrAdd(UniqueNameForWidget);
				++NameCount;
				// If widgets have the same names (can happen with multiple Blueprints) fall back to expensive string construction
				if (NameCount > 1)
				{
					UniqueNameForWidget = FName(FString::Printf(TEXT("%s_%d"), *UniqueNameForWidget.ToString(), NameCount));
				}
			
				FWidgetDisplayInfo& Info = TargetDisplayInfo.Add(Widget);
				Info.UniqueName = UniqueNameForWidget;
				Info.WidgetDisplayName = FText::FromName(Widget->GetFName());
			}

			for (TPair<TWeakObjectPtr<UVCamWidget>, FWidgetDisplayInfo>& Pair : TargetDisplayInfo)
			{
				Pair.Value.bNeedsToDisplayParentWidgetName = NameCounter[Pair.Key->GetFName()] > 1;
			}
			return TargetDisplayInfo;
		}

		static void SetToolTipForAllChildren(SWidget& Widget, const TAttribute<FText>& TooltipAttribute)
		{
			Widget.SetToolTipText(TooltipAttribute);
			FChildren* Children = Widget.GetChildren();
			if (!Children)
			{
				return;
			}

			Children->ForEachWidget([&TooltipAttribute](SWidget& ChildWidget)
			{
				SetToolTipForAllChildren(ChildWidget, TooltipAttribute);
			});
		}

		static FText GetActivationNameTooltip(TSharedRef<IPropertyHandle> PropertyHandle, const TWeakObjectPtr<UVCamOutputProviderBase>& WeakOutputProvider)
		{
			FText ActivationReason;
			if (UVCamOutputProviderBase* OutputProviderBase = WeakOutputProvider.Get();
				OutputProviderBase && !OutputProviderBase->IsActivationChangeAllowedWithReason(!OutputProviderBase->IsActive(), ActivationReason))
			{
				return ActivationReason.IsEmpty() ? LOCTEXT("NotAllowed", "Cannot toggle activation") : ActivationReason;
			}

			const FProperty* Property = PropertyHandle->GetProperty();
			return Property ? PropertyHandle->GetProperty()->GetToolTipText() : FText::GetEmpty();
		}
		
		static FText GetActivationValueTooltip(TSharedRef<IPropertyHandle> PropertyHandle, const TWeakObjectPtr<UVCamOutputProviderBase>& WeakOutputProvider)
		{
			FText ActivationReason;
			if (UVCamOutputProviderBase* OutputProviderBase = WeakOutputProvider.Get();
				OutputProviderBase && !OutputProviderBase->IsActivationChangeAllowedWithReason(!OutputProviderBase->IsActive(), ActivationReason))
			{
				return ActivationReason.IsEmpty() ? LOCTEXT("NotAllowed", "Cannot toggle activation") : ActivationReason;
			}
				
			FText TooltipText;
			if( PropertyHandle->GetValueAsFormattedText(TooltipText) == FPropertyAccess::MultipleValues )
			{
				return LOCTEXT("MultipleValues", "Multiple Values");
			}
			return TooltipText;
		}

		static void OverrideIsActiveProperty(
			IDetailLayoutBuilder& DetailBuilder,
			IDetailCategoryBuilder& Category,
			TWeakObjectPtr<UVCamOutputProviderBase> WeakOutputProvider
			)
		{
			const TSharedRef<IPropertyHandle> PropertyHandle = DetailBuilder.GetProperty(UVCamOutputProviderBase::GetIsActivePropertyName());
			const TSharedRef<SWidget> NameWidget = PropertyHandle->CreatePropertyNameWidget();
			const TSharedRef<SWidget> ValueWidget = PropertyHandle->CreatePropertyValueWidget();
			
			const TAttribute<FText> NameTooltipTextAttr = TAttribute<FText>::CreateLambda([PropertyHandle, WeakOutputProvider]
			{
				return GetActivationNameTooltip(PropertyHandle, WeakOutputProvider);
			});
			const TAttribute<FText> ValueTooltipTextAttr = TAttribute<FText>::CreateLambda([PropertyHandle, WeakOutputProvider]
			{
				return GetActivationValueTooltip(PropertyHandle, WeakOutputProvider);
			});
			SetToolTipForAllChildren(*NameWidget, NameTooltipTextAttr);
			SetToolTipForAllChildren(*ValueWidget, ValueTooltipTextAttr);
			
			Category.AddProperty(PropertyHandle)
				.CustomWidget()
				.NameContent() [ NameWidget ]
				.ValueContent()[ ValueWidget ];
		}
	}
	
	TSharedRef<IDetailCustomization> FOutputProviderLayoutCustomization::MakeInstance()
	{
		return MakeShared<FOutputProviderLayoutCustomization>();
	}

	FOutputProviderLayoutCustomization::~FOutputProviderLayoutCustomization()
	{
		// Technically unsubscribing is not needed because delegates clean dangling reference automatically, but let's not dangle on purpose...
		if (CustomizedOutputProvider.IsValid())
		{
			CustomizedOutputProvider->OnActivatedDelegate.Remove(OnActivatedDelegateHandle);
		}
		
		ClearWidgetData(EditableWidgets);
	}

	void FOutputProviderLayoutCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
	{
		bRequestedRefresh = false;
		
		TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
		DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);
		CustomizedOutputProvider = CustomizedObjects.Num() == 1
			? Cast<UVCamOutputProviderBase>(CustomizedObjects[0])
			: nullptr;
		if (!CustomizedOutputProvider.IsValid())
		{
			return;
		}
		
		if (!OnActivatedDelegateHandle.IsValid())
		{
			OnActivatedDelegateHandle = CustomizedOutputProvider->OnActivatedDelegate.AddSP(this, &FOutputProviderLayoutCustomization::OnActivationChanged);
		}

		// Important properties should show before widgets, then ...
		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Output"));
		Category.SetSortOrder(0);
		Private::OverrideIsActiveProperty(DetailBuilder, Category, CustomizedOutputProvider);
		Category.AddProperty(DetailBuilder.GetProperty(UVCamOutputProviderBase::GetTargetViewportPropertyName()));
		Category.AddProperty(DetailBuilder.GetProperty(UVCamOutputProviderBase::GetUMGClassPropertyName()));

		// ... the widgets should show after important properties, and ...
		RebuildWidgetData();
		if (!EditableWidgets.IsEmpty())
		{
			IDetailGroup& WidgetGroup = Category.AddGroup(TEXT("Widgets"), LOCTEXT("WidgetsLabel", "Widgets"));
			ExtendWidgetsRow(DetailBuilder, WidgetGroup);
			GenerateWidgetRows(WidgetGroup, DetailBuilder);
		}
		
		// ... all other properties should be shown after widgets
	}

	void FOutputProviderLayoutCustomization::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
	{
		if (DetailBuilder != WeakDetailBuilder)
		{
			WeakDetailBuilder = DetailBuilder;
		}
		
		IDetailCustomization::CustomizeDetails(DetailBuilder);
	}
	
	FDetailWidgetRow FOutputProviderLayoutCustomization::ExtendWidgetsRow(IDetailLayoutBuilder& DetailBuilder,IDetailGroup& WidgetGroup)
	{
		return WidgetGroup.HeaderRow()
			.NameContent()
			[
				SNew(STextBlock)
					.Font(DetailBuilder.GetDetailFont())
					.Text(LOCTEXT("Widget", "Widgets"))
			]
			.ValueContent()
			[
				SNew(SSimpleComboButton)
				.Icon(FAppStyle::Get().GetBrush("DetailsView.ViewOptions"))
				.OnGetMenuContent_Lambda([this]()
				{
					FMenuBuilder MenuBuilder(true, nullptr);
					MenuBuilder.AddMenuEntry(
						LOCTEXT("FTargetConnectionDisplaySettings.bOnlyShowManuallyConfiguredConnections", "Only Manually Configured Connections"),
						FText::GetEmpty(),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([this]()
							{
								Private::DisplaySettings.bOnlyShowManuallyConfiguredConnections = !Private::DisplaySettings.bOnlyShowManuallyConfiguredConnections;
								ForceRefreshDetailsIfSafe();
							}),
							FCanExecuteAction::CreateLambda([](){ return true; }),
							FIsActionChecked::CreateLambda([](){ return Private::DisplaySettings.bOnlyShowManuallyConfiguredConnections; })
							),
							NAME_None,
							EUserInterfaceActionType::Check
						);
					return MenuBuilder.MakeWidget();
				})
			];
	}

	void FOutputProviderLayoutCustomization::RebuildWidgetData()
	{
		const UVPFullScreenUserWidget* FullScreenUserWidget = CustomizedOutputProvider->GetUMGWidget();
		UUserWidget* RootWidget = FullScreenUserWidget ? FullScreenUserWidget->GetWidget() : nullptr;
		UWidgetTree* WidgetTree = RootWidget ? RootWidget->WidgetTree : nullptr;
		if (!WidgetTree || !RootWidget) // !RootWidget pointless but maks static analyzer happy
		{
			return;
		}

		// Move so EditableWidgets gets reset and does not retain old references
		TMap<TWeakObjectPtr<UVCamWidget>, FWidgetData> OldEditableWidgets = MoveTemp(EditableWidgets);
		VCamCore::ForEachWidgetToConsiderForVCam(*RootWidget, [this, &OldEditableWidgets](UWidget* Widget)
		{
			if (UVCamWidget* VCamWidget = Cast<UVCamWidget>(Widget))
			{
				FWidgetData ExistingWidgetData;
				if (OldEditableWidgets.RemoveAndCopyValue(VCamWidget, ExistingWidgetData))
				{
					EditableWidgets.Emplace(VCamWidget, MoveTemp(ExistingWidgetData));
				}
				else if (TSharedPtr<IConnectionRemapCustomization> Customization = FVCamCoreEditorModule::Get().CreateConnectionRemapCustomization(VCamWidget->GetClass()))
				{
					const TWeakObjectPtr<UVCamWidget> WeakVCamWidget(VCamWidget);
					EditableWidgets.Emplace(
						WeakVCamWidget,
						FWidgetData{ Customization, MakeShared<FConnectionRemapUtilsImpl>(WeakDetailBuilder.Pin().ToSharedRef()) }
						);
				}
			}
		});
		
		// The hierarchy may have changed so anything that is left is not part of the hierarchy and can be unsubscribed from
		ClearWidgetData(OldEditableWidgets);
	}

	void FOutputProviderLayoutCustomization::GenerateWidgetRows(IDetailGroup& RootWidgetGroup, IDetailLayoutBuilder& DetailBuilder)
	{
		TArray<TWeakObjectPtr<UVCamWidget>> SortedWidgets;
		EditableWidgets.GenerateKeyArray(SortedWidgets);
		SortedWidgets.Sort([](const TWeakObjectPtr<UVCamWidget>& First, const TWeakObjectPtr<UVCamWidget>& Second)
		{
			return First->GetName() < Second->GetName(); 
		});
		
		TMap<TWeakObjectPtr<UVCamWidget>, Private::FWidgetDisplayInfo> WidgetDisplayData = Private::GenerateWidgetRowNames(SortedWidgets);
		for (const TWeakObjectPtr<UVCamWidget>& Widget : SortedWidgets)
		{
			const FWidgetData* WidgetData = EditableWidgets.Find(Widget);
			if (!ensure(WidgetData))
			{
				continue;
			}

			if (!WidgetData->Customization->CanGenerateGroup({ Widget, Private::DisplaySettings}))
			{
				continue;
			}
			
			const Private::FWidgetDisplayInfo& DisplayInfo = WidgetDisplayData[Widget];
			// Row name is "WidgetName" or "WidgetName (Outer name)" 
			const FText RowDisplayName = DisplayInfo.bNeedsToDisplayParentWidgetName
				? FText::Format(LOCTEXT("WidgetNameFmt", "{0} ({1})"), DisplayInfo.WidgetDisplayName, FText::FromName(Widget->GetOuter()->GetFName()))
				: DisplayInfo.WidgetDisplayName;
			IDetailGroup& WidgetGroup = RootWidgetGroup.AddGroup(DisplayInfo.UniqueName, RowDisplayName);
			WidgetGroup.HeaderRow()
				.NameContent()
				[
					SNew(STextBlock)
					.Text(RowDisplayName)
					.Font(DetailBuilder.GetDetailFont())
				];
			WidgetData->Customization->Customize({ DetailBuilder, WidgetGroup, WidgetData->RemapUtils.ToSharedRef(), Widget, Private::DisplaySettings });
		}
	}
	
	void FOutputProviderLayoutCustomization::OnActivationChanged(bool bNewIsActivated)
	{
		if (bRequestedRefresh || !CustomizedOutputProvider.IsValid())
		{
			return;
		}

		UWorld* World = CustomizedOutputProvider->GetWorld();
		if (!IsValid(World))
		{
			return;
		}

		bRequestedRefresh = true;
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakThis = AsWeak()]()
		{
			// ForceRefreshDetails may want to delete us ... in that case we must not keep ourselves alive.
			FOutputProviderLayoutCustomization* This = nullptr;
			if (TSharedPtr<IDetailCustomization> ThisPin = WeakThis.Pin())
			{
				This = StaticCastSharedPtr<FOutputProviderLayoutCustomization>(ThisPin).Get();
			}

			if (This)
			{
				This->ForceRefreshDetailsIfSafe();
			}
		}));
	}

	void FOutputProviderLayoutCustomization::ForceRefreshDetailsIfSafe() const
	{
		// ForceRefreshDetails may want to delete our IDetailLayoutBuilder... in that case we must not keep it alive.
		IDetailLayoutBuilder* DetailBuilder = nullptr;
		if (TSharedPtr<IDetailLayoutBuilder> DetailBuilderPin = WeakDetailBuilder.Pin())
		{
			DetailBuilder = DetailBuilderPin.Get();
		}

		if (!DetailBuilder)
		{
			return;
		}

		const bool bCanRefresh = CustomizedOutputProvider.IsValid();
		if (bCanRefresh)
		{
			DetailBuilder->ForceRefreshDetails();
		}
	}

	void FOutputProviderLayoutCustomization::ClearWidgetData(TMap<TWeakObjectPtr<UVCamWidget>, FWidgetData>& InEditableWidgets)
	{
		InEditableWidgets.Empty();
	}
}

#undef LOCTEXT_NAMESPACE