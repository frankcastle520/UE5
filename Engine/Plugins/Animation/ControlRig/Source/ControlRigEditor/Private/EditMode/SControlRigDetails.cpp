// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditMode/SControlRigDetails.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "AssetRegistry/AssetData.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Styling/CoreStyle.h"
#include "ScopedTransaction.h"
#include "ControlRig.h"
#include "UnrealEdGlobals.h"
#include "EditMode/ControlRigEditMode.h"
#include "EditorModeManager.h"
#include "ISequencer.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "Selection.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "EditMode/AnimDetailsProxy.h"
#include "EditMode/ControlRigEditModeSettings.h"
#include "Modules/ModuleManager.h"
#include "TimerManager.h"
#include "CurveEditor.h"
#include "MVVM/CurveEditorExtension.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneIntegerTrack.h"
#include "Tracks/MovieSceneDoubleTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "MovieSceneCommonHelpers.h"

#define LOCTEXT_NAMESPACE "ControlRigDetails"

void FControlRigEditModeGenericDetails::CustomizeDetails(class IDetailLayoutBuilder& DetailLayout)
{
}
void SControlRigDetails::Construct(const FArguments& InArgs, FControlRigEditMode& InEditMode)
{
	using namespace UE::Sequencer;

	ModeTools = InEditMode.GetModeManager();
	FDetailsViewArgs DetailsViewArgs;
	{
		DetailsViewArgs.bAllowSearch = true;
		DetailsViewArgs.bHideSelectionTip = true;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.bSearchInitialKeyFocus = true;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bShowModifiedPropertiesOption = true;
		DetailsViewArgs.bCustomNameAreaLocation = true;
		DetailsViewArgs.bCustomFilterAreaLocation = false;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = false;
		DetailsViewArgs.bShowScrollBar = false; // Don't need to show this, as we are putting it in a scroll box
	}

	FDetailsViewArgs IndividualDetailsViewArgs = DetailsViewArgs;
	IndividualDetailsViewArgs.bAllowMultipleTopLevelObjects = true; //this is the secret sauce to show multiple objects in a details view

	auto CreateDetailsView = [this](FDetailsViewArgs InDetailsViewArgs) -> TSharedPtr<IDetailsView>
	{
		FControlRigEditMode* EditMode = GetEditMode();
		TSharedPtr<IDetailsView> DetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(InDetailsViewArgs);
		DetailsView->SetKeyframeHandler(EditMode->DetailKeyFrameCache);
		DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SControlRigDetails::ShouldShowPropertyOnDetailCustomization));
		DetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SControlRigDetails::IsReadOnlyPropertyOnDetailCustomization));
		DetailsView->SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateStatic(&FControlRigEditModeGenericDetails::MakeInstance, ModeTools));
		return DetailsView;
	};

	AllControlsView = CreateDetailsView(IndividualDetailsViewArgs);

	ChildSlot
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					AllControlsView.ToSharedRef()
				]
			]
		];

	SetEditMode(InEditMode);

}

void SControlRigDetails::SetEditMode(FControlRigEditMode& InEditMode)
{
	FControlRigBaseDockableView::SetEditMode(InEditMode);
	if (GetEditMode()->GetWeakSequencer().IsValid())
	{
		SequencerTracker.SetSequencerAndDetails(GetEditMode()->GetWeakSequencer(), this);
		UpdateProxies();
	}
}

SControlRigDetails::~SControlRigDetails()
{
	//base class handles control rig related cleanup
}

void SControlRigDetails::SelectedSequencerObjects(const TMap<UObject*, FArrayOfPropertyTracks>& InObjectsTracked)
{
	TMap<UObject*, FArrayOfPropertyTracks> SequencerObjects;
	for (const TPair<UObject*, FArrayOfPropertyTracks>& Pair : InObjectsTracked)
	{
		if(Pair.Key && (Pair.Key->IsA<AActor>() || Pair.Key->IsA<UActorComponent>()))
		{
			SequencerObjects.Add(Pair);
		}
	}

	HandleSequencerObjects(SequencerObjects);
	UpdateProxies();
}
void SControlRigDetails::HandleControlSelected(UControlRig* Subject, FRigControlElement* InControl, bool bSelected)
{
	FControlRigBaseDockableView::HandleControlSelected(Subject, InControl, bSelected);
	UpdateProxies();
}

static TArray<UControlRigControlsProxy*> GetParentProxies(UControlRigControlsProxy* ChildProxy, const TArray<UControlRigControlsProxy*>& Proxies)
{
	if (!ChildProxy || !ChildProxy->OwnerControlRig.IsValid())
	{
		return {};
	}
	if (!ChildProxy->OwnerControlElement.UpdateCache(ChildProxy->OwnerControlRig->GetHierarchy()))
	{
		return {};
	}
	TArray<FRigBaseElement*> Parents;
	if(ChildProxy && ChildProxy->OwnerControlRig.IsValid())
	{
		Parents = ChildProxy->OwnerControlRig->GetHierarchy()->GetParents(ChildProxy->OwnerControlElement.GetElement()); 
	}

	TArray<UControlRigControlsProxy*> ParentProxies;
	for (UControlRigControlsProxy* Proxy : Proxies)
	{
		if (Proxy && Proxy->OwnerControlRig.IsValid())
		{
			if (Proxy->OwnerControlElement.UpdateCache(Proxy->OwnerControlRig->GetHierarchy()))
			{
				if(const FRigControlElement* OwnerControlElement = Cast<FRigControlElement>(Proxy->OwnerControlElement.GetElement()))
				{
					if (Parents.Contains(OwnerControlElement))
					{
						ParentProxies.AddUnique(Proxy);
					}
					if(const FRigControlElement* ChildControlElement = Cast<FRigControlElement>(ChildProxy->OwnerControlElement.GetElement()))
					{
						if(ChildControlElement->Settings.Customization.AvailableSpaces.Contains(OwnerControlElement->GetKey()))
						{
							ParentProxies.AddUnique(Proxy);
						}
					}
				}
			}
		}
	}
	return ParentProxies;
}
static UControlRigControlsProxy* GetProxyWithSameType(TArray<TWeakObjectPtr<>>& AllProxies, ERigControlType ControlType, bool bIsEnum)
{
	for (TWeakObjectPtr<> ExistingProxy : AllProxies)
	{
		if (ExistingProxy.IsValid())
		{
			switch (ControlType)
			{
			case ERigControlType::Transform:
			case ERigControlType::TransformNoScale:
			case ERigControlType::EulerTransform:
			{
				if (ExistingProxy.Get()->IsA<UAnimDetailControlsProxyTransform>())
				{
					return  Cast<UControlRigControlsProxy>(ExistingProxy.Get());
				}
				break;
			}
			case ERigControlType::Float:
			case ERigControlType::ScaleFloat:
			{
				if (ExistingProxy.Get()->IsA<UAnimDetailControlsProxyFloat>())
				{
					return  Cast<UControlRigControlsProxy>(ExistingProxy.Get());
				}
				break;

			}
			case ERigControlType::Integer:
			{
				if (bIsEnum == false)
				{
					if (ExistingProxy.Get()->IsA<UAnimDetailControlsProxyInteger>())
					{
						return  Cast<UAnimDetailControlsProxyInteger>(ExistingProxy.Get());
					}
				}
				else
				{
					if (ExistingProxy.Get()->IsA<UAnimDetailControlsProxyEnum>())
					{
						return  Cast<UControlRigControlsProxy>(ExistingProxy.Get());
					}
				}
				break;

			}
			case ERigControlType::Position:
			{
				if (ExistingProxy.Get()->IsA<UAnimDetailControlsProxyLocation>())
				{
					return  Cast<UControlRigControlsProxy>(ExistingProxy.Get());
				}
				break;
			}
			case ERigControlType::Rotator:
				{
				if (ExistingProxy.Get()->IsA<UAnimDetailControlsProxyRotation>())
				{
					return  Cast<UControlRigControlsProxy>(ExistingProxy.Get());
				}
				break;
			}
			case ERigControlType::Scale:
			{
				if (ExistingProxy.Get()->IsA<UAnimDetailControlsProxyScale>())
				{
					return  Cast<UControlRigControlsProxy>(ExistingProxy.Get());
				}
				break;
			}
			case ERigControlType::Vector2D:
			{
				if (ExistingProxy.Get()->IsA<UAnimDetailControlsProxyVector2D>())
				{
					return  Cast<UControlRigControlsProxy>(ExistingProxy.Get());
				}
				break;
			}
			case ERigControlType::Bool:
			{
				if (ExistingProxy.Get()->IsA<UAnimDetailControlsProxyBool>())
				{
					return  Cast<UControlRigControlsProxy>(ExistingProxy.Get());
				}
				break;
			}
			}
		}
	}
	return nullptr;
}

void SControlRigDetails::HandleSequencerObjects(TMap<UObject*, FArrayOfPropertyTracks>& SequencerObjects)
{
	if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
	{
		if (UControlRigDetailPanelControlProxies* ControlProxy = EditMode->GetDetailProxies())
		{
			TMap<ERigControlType, FSequencerProxyPerType>  ProxyPerType;
			for (TPair<UObject*, FArrayOfPropertyTracks>& Pair : SequencerObjects)
			{
				for (UMovieSceneTrack* Track : Pair.Value.PropertyTracks)
				{
					if (UMovieScenePropertyTrack* PropTrack = Cast<UMovieScenePropertyTrack>(Track))
					{
						auto AddBinding = [this, PropTrack](UObject* InObject,FSequencerProxyPerType& Binding)
						{
							TArray<FBindingAndTrack>& Bindings = Binding.Bindings.FindOrAdd(InObject);
							TSharedPtr<FTrackInstancePropertyBindings> PropertyBindings = MakeShareable(new FTrackInstancePropertyBindings(PropTrack->GetPropertyName(), PropTrack->GetPropertyPath().ToString()));
							FBindingAndTrack BindingAndTrack(PropertyBindings, PropTrack);
							Bindings.Add(BindingAndTrack);
						};
						if (PropTrack->IsA<UMovieScene3DTransformTrack>())
						{
							FSequencerProxyPerType& Binding = ProxyPerType.FindOrAdd(ERigControlType::Transform);
							AddBinding(Pair.Key, Binding);
						}
						else if (PropTrack->IsA<UMovieSceneBoolTrack>())
						{
							FSequencerProxyPerType& Binding = ProxyPerType.FindOrAdd(ERigControlType::Bool);
							AddBinding(Pair.Key, Binding);
						}
						else if (PropTrack->IsA<UMovieSceneIntegerTrack>())
						{
							FSequencerProxyPerType& Binding = ProxyPerType.FindOrAdd(ERigControlType::Integer);
							AddBinding(Pair.Key, Binding);
						}
						else if (PropTrack->IsA<UMovieSceneDoubleTrack>() ||
							PropTrack->IsA<UMovieSceneFloatTrack>())
						{
							FSequencerProxyPerType& Binding = ProxyPerType.FindOrAdd(ERigControlType::Float);
							AddBinding(Pair.Key, Binding);
						}
					}
				}
			}
			ControlProxy->ResetSequencerProxies(ProxyPerType);
		}
	}
}

void SControlRigDetails::UpdateProxies()
{
	if(NextTickTimerHandle.IsValid() == false)
	{ 
		TWeakPtr<SWidget> WeakPtr = AsWeak();

		//proxies that are in edit mode are also listening to the same messages so they may not be set up yet so need to wait
		NextTickTimerHandle = GEditor->GetTimerManager()->SetTimerForNextTick([WeakPtr]()
		{
			if(!WeakPtr.IsValid())
			{
				return;
			}
			TSharedPtr<SControlRigDetails> StrongThis = StaticCastSharedPtr<SControlRigDetails>(WeakPtr.Pin());
			if(!StrongThis.IsValid())
			{
				return;
			}
		
			TArray<TWeakObjectPtr<>> AllProxies;
			TArray<UControlRigControlsProxy*> ChildProxies; //list of 'child' proxies that will show up as custom attributes
			if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(StrongThis->ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
			{
				if (UControlRigDetailPanelControlProxies* ControlProxy = EditMode->GetDetailProxies())
				{
					const TArray<UControlRigControlsProxy*>& Proxies = ControlProxy->GetAllSelectedProxies();
					for (UControlRigControlsProxy* Proxy : Proxies)
					{
						if (Proxy == nullptr || !IsValid(Proxy))
						{
							continue;
						}
						Proxy->ResetItems();

						if (Proxy->bIsIndividual)
						{
							ChildProxies.Add(Proxy);
						}
						else
						{
							TObjectPtr<UEnum> EnumPtr = nullptr;
							if (Proxy->OwnerControlRig.IsValid())
							{
								if (Proxy->OwnerControlElement.UpdateCache(Proxy->OwnerControlRig->GetHierarchy()))
								{
									if (const FRigControlElement* ControlElement = Cast<FRigControlElement>(Proxy->OwnerControlElement.GetElement()))
									{
										EnumPtr = ControlElement->Settings.ControlEnum;
									}
								}
							}
							if (UControlRigControlsProxy* ExistingProxy = GetProxyWithSameType(AllProxies, Proxy->Type, EnumPtr != nullptr))
							{
								ExistingProxy->AddItem(Proxy);
								ExistingProxy->ValueChanged();
							}
							else
							{
								AllProxies.Add(Proxy);
							}
						}
					}
					//now add child proxies to parents if parents also selected...
					for (UControlRigControlsProxy* Proxy : ChildProxies)
					{
						TArray<UControlRigControlsProxy*> ParentProxies = GetParentProxies(Proxy, Proxies);
						for (UControlRigControlsProxy* ParentProxy : ParentProxies)
						{
							TObjectPtr<UEnum> EnumPtr = nullptr;
							if (ParentProxy->OwnerControlRig.IsValid())
							{
								if (ParentProxy->OwnerControlElement.UpdateCache(ParentProxy->OwnerControlRig->GetHierarchy()))
								{
									if (const FRigControlElement* ControlElement = Cast<FRigControlElement>(ParentProxy->OwnerControlElement.GetElement()))
									{
										EnumPtr = ControlElement->Settings.ControlEnum;
									}
								}
							}
							if (UControlRigControlsProxy* ExistingProxy = GetProxyWithSameType(AllProxies, ParentProxy->Type, EnumPtr != nullptr))
							{
								ExistingProxy->AddChildProxy(Proxy);
							}
						}

						if(ParentProxies.IsEmpty())
						{
							AllProxies.Add(Proxy);
						}
					}
					for (UControlRigControlsProxy* Proxy : Proxies)
					{
						Proxy->ValueChanged();
					}
				}
			}
		
			StrongThis->AllControlsView->SetObjects(AllProxies,true);
			StrongThis->NextTickTimerHandle.Invalidate();
		});
	}
}

FReply SControlRigDetails::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
	{
		TWeakPtr<ISequencer> Sequencer = EditMode->GetWeakSequencer();
		if (Sequencer.IsValid())
		{
			using namespace UE::Sequencer;
			const TSharedPtr<FSequencerEditorViewModel> SequencerViewModel = Sequencer.Pin()->GetViewModel();
			const FCurveEditorExtension* CurveEditorExtension = SequencerViewModel->CastDynamic<FCurveEditorExtension>();
			check(CurveEditorExtension);
			TSharedPtr<FCurveEditor> CurveEditor = CurveEditorExtension->GetCurveEditor();
			if (CurveEditor->GetCommands()->ProcessCommandBindings(InKeyEvent))
			{
				return FReply::Handled();
			}
		}
	}
	return FReply::Unhandled();
}

bool SControlRigDetails::ShouldShowPropertyOnDetailCustomization(const FPropertyAndParent& InPropertyAndParent) const
{
	auto ShouldPropertyBeVisible = [](const FProperty& InProperty)
	{
		bool bShow = InProperty.HasAnyPropertyFlags(CPF_Interp) || InProperty.HasMetaData(FRigVMStruct::InputMetaName) || InProperty.HasMetaData(FRigVMStruct::OutputMetaName);
		return bShow;
	};

	bool bContainsVisibleProperty = false;
	if (InPropertyAndParent.Property.IsA<FStructProperty>())
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(&InPropertyAndParent.Property);
		for (TFieldIterator<FProperty> PropertyIt(StructProperty->Struct); PropertyIt; ++PropertyIt)
		{
			if (ShouldPropertyBeVisible(**PropertyIt))
			{
				return true;
			}
		}
	}

	return ShouldPropertyBeVisible(InPropertyAndParent.Property) ||
		(InPropertyAndParent.ParentProperties.Num() > 0 && ShouldPropertyBeVisible(*InPropertyAndParent.ParentProperties[0]));
}

bool SControlRigDetails::IsReadOnlyPropertyOnDetailCustomization(const FPropertyAndParent& InPropertyAndParent) const
{
	auto ShouldPropertyBeEnabled = [](const FProperty& InProperty)
	{
		bool bShow = InProperty.HasAnyPropertyFlags(CPF_Interp) || InProperty.HasMetaData(FRigVMStruct::InputMetaName);
		return bShow;
	};

	bool bContainsVisibleProperty = false;
	if (InPropertyAndParent.Property.IsA<FStructProperty>())
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(&InPropertyAndParent.Property);
		for (TFieldIterator<FProperty> PropertyIt(StructProperty->Struct); PropertyIt; ++PropertyIt)
		{
			if (ShouldPropertyBeEnabled(**PropertyIt))
			{
				return false;
			}
		}
	}

	return !(ShouldPropertyBeEnabled(InPropertyAndParent.Property) ||
		(InPropertyAndParent.ParentProperties.Num() > 0 && ShouldPropertyBeEnabled(*InPropertyAndParent.ParentProperties[0])));
}


FSequencerTracker::~FSequencerTracker()
{
	RemoveDelegates();
}

void FSequencerTracker::RemoveDelegates()
{
	if (TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin())
	{
		Sequencer->GetSelectionChangedObjectGuids().RemoveAll(this);
	}
}

void FSequencerTracker::SetSequencerAndDetails(TWeakPtr<ISequencer> InWeakSequencer, SControlRigDetails* InControlRigDetails)
{
	RemoveDelegates();
	WeakSequencer = InWeakSequencer;
	ControlRigDetails = InControlRigDetails;
	if (WeakSequencer.IsValid() == false || InControlRigDetails == nullptr)
	{
		return;
	}
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();

	TArray<FGuid> SequencerSelectedObjects;
	Sequencer->GetSelectedObjects(SequencerSelectedObjects);
	UpdateSequencerBindings(SequencerSelectedObjects);

	Sequencer->GetSelectionChangedObjectGuids().AddRaw(this, &FSequencerTracker::UpdateSequencerBindings);
	
}

void FSequencerTracker::UpdateSequencerBindings(TArray<FGuid> SequencerBindings)
{
	const FDateTime StartTime = FDateTime::Now();

	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	check(Sequencer);
	ObjectsTracked.Reset();
	for (FGuid BindingGuid : SequencerBindings)
	{
		FArrayOfPropertyTracks Properties;
		Properties.PropertyTracks = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->FindTracks(UMovieScenePropertyTrack::StaticClass(), BindingGuid);
		if (Properties.PropertyTracks.Num() == 0)
		{
			continue;
		}
		for (TWeakObjectPtr<> BoundObject : Sequencer->FindBoundObjects(BindingGuid, Sequencer->GetFocusedTemplateID()))
		{
			if (!BoundObject.IsValid())
			{
				continue;
			}
			ObjectsTracked.FindOrAdd(BoundObject.Get(), Properties);

		}
	}
	if (ControlRigDetails)
	{
		ControlRigDetails->SelectedSequencerObjects(ObjectsTracked);
	}
}


#undef LOCTEXT_NAMESPACE
