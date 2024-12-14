// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraSimCacheViewModel.h"
#include "NiagaraComponent.h"
#include "NiagaraSimCache.h"
#include "NiagaraEditorCommon.h"
#include "NiagaraDebuggerCommon.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Serialization/MemoryReader.h"
#include "Widgets/SNiagaraSimCacheTreeView.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "NiagaraSimCacheViewModel"

FNiagaraSimCacheViewModel::FNiagaraSimCacheViewModel()
{
}

FNiagaraSimCacheViewModel::~FNiagaraSimCacheViewModel()
{
	UNiagaraSimCache::OnCacheEndWrite.RemoveAll(this);
	bDelegatesAdded = false;
	SimCache = nullptr;
	PreviewComponent = nullptr;
}

void FNiagaraSimCacheViewModel::Initialize(TWeakObjectPtr<UNiagaraSimCache> InSimCache) 
{
	if (bDelegatesAdded == false)
	{
		bDelegatesAdded = true;
		UNiagaraSimCache::OnCacheEndWrite.AddSP(this, &FNiagaraSimCacheViewModel::OnCacheModified);
	}

	if(InSimCache.IsValid())
	{
		SimCache = InSimCache.Get();
	}
	
	UpdateComponentInfos();
	UpdateCachedFrame();
	SetupPreviewComponentAndInstance();
	
	OnSimCacheChangedDelegate.Broadcast();
	OnViewDataChangedDelegate.Broadcast(true);
}

void FNiagaraSimCacheViewModel::SetComponentFilters(const TArray<FString>& NewComponentFilterArray)
{
	bComponentFilterActive = true;
	ComponentFilterArray.Empty();
	ComponentFilterArray.Append(NewComponentFilterArray);
	OnViewDataChangedDelegate.Broadcast(true);
}

void FNiagaraSimCacheViewModel::SetupPreviewComponentAndInstance()
{
	UNiagaraSystem* System = SimCache ? SimCache->GetSystem(true) : nullptr;

	if(SimCache && System)
	{
		PreviewComponent = NewObject<UNiagaraComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		PreviewComponent->CastShadow = 1;
		PreviewComponent->bCastDynamicShadow = 1;
		PreviewComponent->SetAllowScalability(false);
		PreviewComponent->SetAsset(System);
		PreviewComponent->SetForceSolo(true);
		PreviewComponent->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
		PreviewComponent->SetCanRenderWhileSeeking(false);
		PreviewComponent->Activate(true);
		PreviewComponent->SetSimCache(SimCache);
		PreviewComponent->SetRelativeLocation(FVector::ZeroVector);
		PreviewComponent->SetDesiredAge(SimCache->GetStartSeconds());
	}
}

TConstArrayView<FNiagaraSimCacheViewModel::FComponentInfo> FNiagaraSimCacheViewModel::GetSelectedComponentInfos() const
{
	if (SelectionMode == ESelectionMode::SystemInstance)
	{
		return GetComponentInfos(INDEX_NONE);
	}
	else if (SelectionMode == ESelectionMode::Emitter)
	{
		return GetComponentInfos(SelectedEmitterIndex);
	}
	return MakeArrayView<FComponentInfo>(nullptr, 0);
}

TConstArrayView<FNiagaraSimCacheViewModel::FComponentInfo> FNiagaraSimCacheViewModel::GetComponentInfos(int32 InEmitterIndex) const
{
	if( InEmitterIndex == INDEX_NONE)
	{
		return SystemComponentInfos;
	}

	if(EmitterComponentInfos.IsValidIndex(InEmitterIndex))
	{
		return EmitterComponentInfos[InEmitterIndex];
	}
	
	return TConstArrayView<FComponentInfo>();
}

FText FNiagaraSimCacheViewModel::GetComponentText(const FName ComponentName, const int32 InstanceIndex) const
{
	const FComponentInfo* ComponentInfo = GetSelectedComponentInfos().FindByPredicate([ComponentName](const FComponentInfo& FoundInfo) { return FoundInfo.Name == ComponentName; });

	if (ComponentInfo)
	{
		if (InstanceIndex >= 0 && InstanceIndex < NumInstances)
		{
			if (ComponentInfo->bIsFloat)
			{
				const float Value = FloatComponents[(ComponentInfo->ComponentOffset * NumInstances) + InstanceIndex];
				return FText::AsNumber(Value);
			}
			else if (ComponentInfo->bIsHalf)
			{
				const FFloat16 Value = HalfComponents[(ComponentInfo->ComponentOffset * NumInstances) + InstanceIndex];
				return FText::AsNumber(Value.GetFloat());
			}
			else if (ComponentInfo->bIsInt32)
			{
				const int32 Value = Int32Components[(ComponentInfo->ComponentOffset * NumInstances) + InstanceIndex];
				if (ComponentInfo->bShowAsBool)
				{
					return Value == 0 ? LOCTEXT("False", "False") : LOCTEXT("True", "True");
				}
				else if (ComponentInfo->Enum != nullptr)
				{
					return ComponentInfo->Enum->GetDisplayNameTextByValue(Value);
				}
				else
				{
					return FText::AsNumber(Value);
				}
			}
		}
	}
	return LOCTEXT("Error", "Error");
}


int32 FNiagaraSimCacheViewModel::GetNumFrames() const
{
	
	return SimCache ? SimCache->GetNumFrames() : 0;
}

void FNiagaraSimCacheViewModel::SetFrameIndex(const int32 InFrameIndex)
{
	FrameIndex = InFrameIndex;
	UpdateCachedFrame();
	if(PreviewComponent && SimCache)
	{
		const float Duration = SimCache->GetDurationSeconds();
		const int NumFrames = SimCache->GetNumFrames();
		const float StartSeconds = SimCache->GetStartSeconds();

		const float NormalizedFrame = FMath::Clamp( NumFrames == 0 ? 0.0f : float(InFrameIndex) / float(NumFrames - 1), 0.0f, 1.0f );
		const float DesiredAge = FMath::Clamp(StartSeconds + (Duration * NormalizedFrame), StartSeconds, StartSeconds + Duration);
		
		PreviewComponent->Activate();
		PreviewComponent->SetDesiredAge(DesiredAge);
	}
	OnViewDataChangedDelegate.Broadcast(false);
}

const UObject* FNiagaraSimCacheViewModel::GetSelectedDataInterfaceStorage() const
{
	if (SelectionMode == ESelectionMode::DataInterface && SimCache)
	{
		return SimCache->GetDataInterfaceStorageObject(SelectedDataInterface);
	}
	return nullptr;
}

void FNiagaraSimCacheViewModel::SetSelectedSystemInstance()
{
	SelectionMode			= ESelectionMode::SystemInstance;
	SelectedEmitterIndex	= INDEX_NONE;
	SelectedDataInterface	= FNiagaraVariableBase();

	RefreshFromSelectionChanged();
}

void FNiagaraSimCacheViewModel::SetSelectedEmitter(int32 EmitterIndex)
{
	SelectionMode			= ESelectionMode::Emitter;
	SelectedEmitterIndex	= EmitterIndex;
	SelectedDataInterface	= FNiagaraVariableBase();

	RefreshFromSelectionChanged();
}

void FNiagaraSimCacheViewModel::SetSelectedDataInterface(FNiagaraVariableBase DIVariable)
{
	SelectionMode			= ESelectionMode::DataInterface;
	SelectedEmitterIndex	= INDEX_NONE;
	SelectedDataInterface	= DIVariable;

	RefreshFromSelectionChanged();
}

void FNiagaraSimCacheViewModel::SetSelectedDebugData()
{
	SelectionMode			= ESelectionMode::DebugData;
	SelectedEmitterIndex	= INDEX_NONE;
	SelectedDataInterface	= FNiagaraVariableBase();

	RefreshFromSelectionChanged();
}

void FNiagaraSimCacheViewModel::RefreshFromSelectionChanged()
{
	UpdateCachedFrame();
	UpdateCurrentEntries();
	bComponentFilterActive = false;
	OnBufferChangedDelegate.Broadcast();
	OnViewDataChangedDelegate.Broadcast(true);
}

bool FNiagaraSimCacheViewModel::IsCacheValid() const
{
	return SimCache ? SimCache->IsCacheValid() : false;
}

int32 FNiagaraSimCacheViewModel::GetNumEmitterLayouts() const
{
	return SimCache ? SimCache->GetNumEmitters() : 0;
}

FName FNiagaraSimCacheViewModel::GetEmitterLayoutName(const int32 Index) const
{
	return SimCache ? SimCache->GetEmitterName(Index) : NAME_None;
}

const UNiagaraSimCacheDebugData* FNiagaraSimCacheViewModel::GetCacheDebugData() const
{
	return SimCache ? SimCache->GetDebugData() : nullptr;
}

FNiagaraSimCacheViewModel::FOnViewDataChanged& FNiagaraSimCacheViewModel::OnViewDataChanged()
{
	return OnViewDataChangedDelegate;
}

FNiagaraSimCacheViewModel::FOnSimCacheChanged& FNiagaraSimCacheViewModel::OnSimCacheChanged()
{
	return OnSimCacheChangedDelegate;
}

FNiagaraSimCacheViewModel::FOnBufferChanged& FNiagaraSimCacheViewModel::OnBufferChanged()
{
	return OnBufferChangedDelegate;
}

void FNiagaraSimCacheViewModel::OnCacheModified(UNiagaraSimCache* InSimCache)
{
	if ( SimCache )
	{
		if ( SimCache == InSimCache )
		{
			SetFrameIndex(0);
			UpdateComponentInfos();
			UpdateCachedFrame();
			OnSimCacheChangedDelegate.Broadcast();
			OnViewDataChangedDelegate.Broadcast(true);
		}
	}
}

void FNiagaraSimCacheViewModel::UpdateCachedFrame()
{
	NumInstances = 0;
	FloatComponents.Empty();
	HalfComponents.Empty();
	Int32Components.Empty();
	
	if (SimCache == nullptr)
	{
		return;
	}

	if ( FrameIndex < 0 || FrameIndex >= SimCache->GetNumFrames() )
	{
		return;
	}

	// Determine if we need to read attributes from the cache
	TOptional<int32> EmitterIndex;
	switch (SelectionMode)
	{
		case ESelectionMode::SystemInstance:
			NumInstances = 1;
			EmitterIndex = INDEX_NONE;
			break;

		case ESelectionMode::Emitter:
			if (SelectedEmitterIndex >= 0 && SelectedEmitterIndex < SimCache->GetNumEmitters())
			{
				EmitterIndex = SelectedEmitterIndex;
				NumInstances = SimCache->GetEmitterNumInstances(SelectedEmitterIndex, FrameIndex);
			}
			break;

		case ESelectionMode::DataInterface:
		case ESelectionMode::DebugData:
			NumInstances = 1;
			break;
	}

	// Read attributes
	if (EmitterIndex.IsSet())
	{
		const FName EmitterName = EmitterIndex.GetValue() == INDEX_NONE ? NAME_None : SimCache->GetEmitterName(EmitterIndex.GetValue());

		SimCache->ForEachEmitterAttribute(
			EmitterIndex.GetValue(),
			[&](const FNiagaraSimCacheVariable& Variable)
			{
				// Pull in data
				SimCache->ReadAttribute(FloatComponents, HalfComponents, Int32Components, Variable.Variable.GetName(), EmitterName, FrameIndex);

				return true;
			}
		);
	}
}

void FNiagaraSimCacheViewModel::UpdateComponentInfos()
{
	SystemComponentInfos.Empty();
	EmitterComponentInfos.Empty();
	FoundFloatComponents = 0;
	FoundHalfComponents = 0;
	FoundInt32Components = 0;
	
	if (SimCache == nullptr)
	{
		return;
	}
	
	SimCache->ForEachEmitterAttribute(INDEX_NONE,
		[&](const FNiagaraSimCacheVariable& Variable)
		{
			// Build component info
			const FNiagaraTypeDefinition& TypeDef = Variable.Variable.GetType();
			if (TypeDef.IsEnum())
			{
				FComponentInfo& ComponentInfo = SystemComponentInfos.AddDefaulted_GetRef();
				ComponentInfo.Name = Variable.Variable.GetName();
				ComponentInfo.ComponentOffset = FoundInt32Components++;
				ComponentInfo.bIsInt32 = true;
				ComponentInfo.Enum = TypeDef.GetEnum();
			}
			else
			{
				BuildComponentInfos(Variable.Variable.GetName(), TypeDef.GetScriptStruct(), SystemComponentInfos);
			}

			return true;
		}
	);

	for (int32 i = 0; i < SimCache->GetNumEmitters(); ++i)
	{
		TArray<FComponentInfo>& CurrentComponentInfos = EmitterComponentInfos.AddDefaulted_GetRef();

		FoundFloatComponents = 0;
		FoundHalfComponents = 0;
		FoundInt32Components = 0;
		
		SimCache->ForEachEmitterAttribute(i,
		[&](const FNiagaraSimCacheVariable& Variable)
			{
				// Build component info
				const FNiagaraTypeDefinition& TypeDef = Variable.Variable.GetType();
				if (TypeDef.IsEnum())
				{
					FComponentInfo& ComponentInfo = CurrentComponentInfos.AddDefaulted_GetRef();
					ComponentInfo.Name = Variable.Variable.GetName();
					ComponentInfo.ComponentOffset = FoundInt32Components++;
					ComponentInfo.bIsInt32 = true;
					ComponentInfo.Enum = TypeDef.GetEnum();
				}
				else
				{
					BuildComponentInfos(Variable.Variable.GetName(), TypeDef.GetScriptStruct(), CurrentComponentInfos);
				}

				return true;
			}
		);
	}
}

void FNiagaraSimCacheViewModel::BuildTreeItemChildren(TSharedPtr<FNiagaraSimCacheTreeItem> InTreeItem, TWeakPtr<SNiagaraSimCacheTreeView> OwningTreeView)
{
	FNiagaraSimCacheTreeItem* TreeItem = InTreeItem.Get();

	if(TreeItem && SimCache)
	{
		int32 BufferIndex = TreeItem->GetBufferIndex();
		
		SimCache->ForEachEmitterAttribute(BufferIndex,
			[&](const FNiagaraSimCacheVariable& Variable)
			{
				FNiagaraTypeDefinition TypeDef = Variable.Variable.GetType();

				TSharedRef<FNiagaraSimCacheComponentTreeItem> CurrentItem = MakeShared<FNiagaraSimCacheComponentTreeItem>(OwningTreeView);
				
				CurrentItem->SetDisplayName(FText::FromName(Variable.Variable.GetName()));
				CurrentItem->SetFilterName(Variable.Variable.GetName().ToString());
				CurrentItem->TypeDef = TypeDef;
				CurrentItem->BufferIndex = TreeItem->GetBufferIndex();
				//CurrentItem->RootItem = TreeItem;

				TreeItem->AddChild(CurrentItem);
				
				if(!TypeDef.IsEnum() && !FNiagaraTypeDefinition::IsScalarDefinition(TypeDef))
				{
					RecursiveBuildTreeItemChildren(TreeItem, CurrentItem, TypeDef, OwningTreeView);
				}
				return true;
			}
		);
		
	}
}

void FNiagaraSimCacheViewModel::RecursiveBuildTreeItemChildren(FNiagaraSimCacheTreeItem* Root,
	TSharedRef<FNiagaraSimCacheComponentTreeItem> Parent, FNiagaraTypeDefinition TypeDefinition, TWeakPtr<SNiagaraSimCacheTreeView> OwningTreeView)
{
	UScriptStruct* Struct = TypeDefinition.GetScriptStruct();

	for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		TSharedRef<FNiagaraSimCacheComponentTreeItem> CurrentItem = MakeShared<FNiagaraSimCacheComponentTreeItem> (OwningTreeView);
		
		FString PropertyName = Property->GetName();
		
		CurrentItem->SetDisplayName(FText::FromString(PropertyName));
		CurrentItem->SetFilterName(Parent->GetFilterName().Append(".").Append(PropertyName));
		CurrentItem->SetBufferIndex(Root->GetBufferIndex());
		//CurrentItem->RootItem = Root;

		Parent->AddChild(CurrentItem);

		if(Property->IsA(FStructProperty::StaticClass()))
		{
			const FStructProperty* StructProperty = CastFieldChecked<FStructProperty>(Property);
			UScriptStruct* FriendlyStruct = FNiagaraTypeHelper::FindNiagaraFriendlyTopLevelStruct(StructProperty->Struct, ENiagaraStructConversion::Simulation);
			FNiagaraTypeDefinition StructTypeDef = FNiagaraTypeDefinition(FriendlyStruct);
			CurrentItem->TypeDef = StructTypeDef;
			RecursiveBuildTreeItemChildren(Root, CurrentItem, StructTypeDef, OwningTreeView);
		}
		else if (Property->IsA(FNumericProperty::StaticClass()))
		{
			if(Property->IsA(FIntProperty::StaticClass()))
			{
				CurrentItem->TypeDef = FNiagaraTypeDefinition::GetIntDef();
			}
			else if (Property->IsA(FFloatProperty::StaticClass()))
			{
				CurrentItem->TypeDef = FNiagaraTypeDefinition::GetFloatDef();
			}
		}
		else if (Property->IsA(FBoolProperty::StaticClass()))
		{
			CurrentItem->TypeDef = FNiagaraTypeDefinition::GetBoolDef();
		}
	}
}

void FNiagaraSimCacheViewModel::BuildEntries(TWeakPtr<SNiagaraSimCacheTreeView> OwningTreeView)
{
	RootEntries.Empty();
	BufferEntries.Empty();
	
	const TSharedRef<FNiagaraSimCacheTreeItem> SharedSystemTreeItem = MakeShared<FNiagaraSimCacheTreeItem>(OwningTreeView);
	const TSharedRef<FNiagaraSimCacheOverviewSystemItem> SharedSystemBufferItem = MakeShared<FNiagaraSimCacheOverviewSystemItem>();

	if(IsCacheValid())
	{
		SharedSystemTreeItem->SetDisplayName(LOCTEXT("SystemInstance", "System Instance"));
		SharedSystemBufferItem->SetDisplayName(LOCTEXT("SystemInstance", "System Instance"));
	}
	else
	{
		SharedSystemTreeItem->SetDisplayName(LOCTEXT("InvalidCache", "Invalid Cache"));
		SharedSystemBufferItem->SetDisplayName(LOCTEXT("InvalidCache", "Invalid Cache"));
	}

	RootEntries.Add(SharedSystemTreeItem);
	BufferEntries.Add(SharedSystemBufferItem);

	if(!IsCacheValid())
	{
		UpdateCurrentEntries();
		return;
	}
		
	BuildTreeItemChildren(SharedSystemTreeItem, OwningTreeView);
	
	for(int32 i = 0; i < GetNumEmitterLayouts(); i++)
	{
		const TSharedRef<FNiagaraSimCacheEmitterTreeItem> CurrentEmitterItem = MakeShared<FNiagaraSimCacheEmitterTreeItem>(OwningTreeView);
		const TSharedRef<FNiagaraSimCacheOverviewEmitterItem> CurrentEmitterBufferItem = MakeShared<FNiagaraSimCacheOverviewEmitterItem>();
		
		CurrentEmitterItem->SetDisplayName(FText::FromName(GetEmitterLayoutName(i)));
		CurrentEmitterBufferItem->SetDisplayName(FText::FromName(GetEmitterLayoutName(i)));
		
		CurrentEmitterItem->SetBufferIndex(i);
		CurrentEmitterBufferItem->SetBufferIndex(i);

		RootEntries.Add(CurrentEmitterItem);
		BufferEntries.Add(CurrentEmitterBufferItem);

		BuildTreeItemChildren(CurrentEmitterItem, OwningTreeView);
	}
	if (SimCache)
	{
		for (const FNiagaraVariableBase& Var : SimCache->GetStoredDataInterfaces())
		{
			const TSharedRef<FNiagaraSimCacheDataInterfaceTreeItem> CurrentDataInterfaceItem = MakeShared<FNiagaraSimCacheDataInterfaceTreeItem>(OwningTreeView);
			const TSharedRef<FNiagaraSimCacheOverviewDataInterfaceItem> CurrentDataInterfaceBufferItem = MakeShared<FNiagaraSimCacheOverviewDataInterfaceItem>();
		
			CurrentDataInterfaceItem->SetDisplayName(FText::FromName(Var.GetName()));
			CurrentDataInterfaceItem->DataInterfaceReference = Var;
			CurrentDataInterfaceBufferItem->SetDisplayName(FText::FromName(Var.GetName()));
			CurrentDataInterfaceBufferItem->DataInterfaceReference = Var;

			for(int32 i = 0; i < GetNumEmitterLayouts(); i++)
			{
				if (Var.GetName().ToString().StartsWith(GetEmitterLayoutName(i).ToString() + "."))
				{
					CurrentDataInterfaceItem->SetBufferIndex(i);
					CurrentDataInterfaceBufferItem->SetBufferIndex(i);
					break;
				}
			}
		
			RootEntries.Add(CurrentDataInterfaceItem);
			BufferEntries.Add(CurrentDataInterfaceBufferItem);
		}

		if (SimCache->GetDebugData() != nullptr)
		{
			const TSharedRef<FNiagaraSimCacheDebugDataTreeItem> TreeItem = MakeShared<FNiagaraSimCacheDebugDataTreeItem>(OwningTreeView);
			const TSharedRef<FNiagaraSimCacheOverviewDebugDataItem> DataItem = MakeShared<FNiagaraSimCacheOverviewDebugDataItem>();
			const FText DisplayNameText = LOCTEXT("DebugData", "Debug Data");
			TreeItem->SetDisplayName(DisplayNameText);
			DataItem->SetDisplayName(DisplayNameText);

			RootEntries.Add(TreeItem);
			BufferEntries.Add(DataItem);
		}
	}

	UpdateCurrentEntries();
}

void FNiagaraSimCacheViewModel::UpdateCurrentEntries()
{
	SelectedRootEntries.Empty();
	switch (SelectionMode)
	{
		case ESelectionMode::SystemInstance:
			SelectedRootEntries.Add(RootEntries[0]);
			return;
		case ESelectionMode::Emitter:
			if (RootEntries.IsValidIndex(SelectedEmitterIndex + 1))
			{
				SelectedRootEntries.Add(RootEntries[SelectedEmitterIndex + 1]);
			}
			return;
		default:
			//-TODO: DO we need to do something here?
			break;
	}
}

TArray<TSharedRef<FNiagaraSimCacheTreeItem>>* FNiagaraSimCacheViewModel::GetSelectedRootEntries()
{
	return &SelectedRootEntries;
}

TArray<TSharedRef<FNiagaraSimCacheOverviewItem>>* FNiagaraSimCacheViewModel::GetBufferEntries()
{
	return &BufferEntries;
}

bool FNiagaraSimCacheViewModel::CanCopyActiveToClipboard() const
{
	return IsCacheValid() && (SelectionMode == ESelectionMode::SystemInstance || SelectionMode == ESelectionMode::Emitter);
}

void FNiagaraSimCacheViewModel::CopyActiveToClipboard() const
{
	if (!CanCopyActiveToClipboard())
	{
		return;
	}

	FString ClipboardString;
	TConstArrayView<FComponentInfo> ComponentInfos = GetSelectedComponentInfos();

	ClipboardString.Append(TEXT("Instance"));
	for ( int iComponent=0; iComponent < ComponentInfos.Num(); ++iComponent)
	{
		ClipboardString.AppendChar(TEXT(','));
		ComponentInfos[iComponent].Name.AppendString(ClipboardString);
	}

	for (int32 iInstance=0; iInstance < NumInstances; ++iInstance)
	{
		ClipboardString.AppendChar(TEXT('\n'));
		ClipboardString.AppendInt(iInstance);

		for (int iComponent = 0; iComponent < ComponentInfos.Num(); ++iComponent)
		{
			ClipboardString.AppendChar(TEXT(','));
			ClipboardString.Append(GetComponentText(ComponentInfos[iComponent].Name, iInstance).ToString());
		}
	}
	FPlatformApplicationMisc::ClipboardCopy(*ClipboardString);
}

void FNiagaraSimCacheViewModel::AddReferencedObjects(FReferenceCollector& Collector)
{
	if(SimCache != nullptr)
	{
		Collector.AddReferencedObject(SimCache);
	}

	if(PreviewComponent != nullptr)
	{
		Collector.AddReferencedObject(PreviewComponent);
	}
}

void FNiagaraSimCacheViewModel::BuildComponentInfos(const FName Name, const UScriptStruct* Struct, TArray<FComponentInfo>& InComponentInfos)
{
	int32 NumProperties = 0;
	for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		++NumProperties;
	}

	for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		const FName PropertyName = NumProperties > 1 ? FName(*FString::Printf(TEXT("%s.%s"), *Name.ToString(), *Property->GetName())) : Name;
		if (Property->IsA(FFloatProperty::StaticClass()))
		{
			FComponentInfo& ComponentInfo = InComponentInfos.AddDefaulted_GetRef();
			ComponentInfo.Name = PropertyName;
			ComponentInfo.ComponentOffset = FoundFloatComponents++;
			ComponentInfo.bIsFloat = true;
		}
		else if (Property->IsA(FUInt16Property::StaticClass()))
		{
			FComponentInfo& ComponentInfo = InComponentInfos.AddDefaulted_GetRef();
			ComponentInfo.Name = PropertyName;
			ComponentInfo.ComponentOffset = FoundHalfComponents++;
			ComponentInfo.bIsHalf = true;
		}
		else if (Property->IsA(FIntProperty::StaticClass()))
		{
			FComponentInfo& ComponentInfo = InComponentInfos.AddDefaulted_GetRef();
			ComponentInfo.Name = PropertyName;
			ComponentInfo.ComponentOffset = FoundInt32Components++;
			ComponentInfo.bIsInt32 = true;
			ComponentInfo.bShowAsBool = (NumProperties == 1) && (Struct == FNiagaraTypeDefinition::GetBoolStruct());
		}
		else if (Property->IsA(FBoolProperty::StaticClass()))
		{
			FComponentInfo& ComponentInfo = InComponentInfos.AddDefaulted_GetRef();
			ComponentInfo.Name = PropertyName;
			ComponentInfo.ComponentOffset = FoundInt32Components++;
			ComponentInfo.bIsInt32 = true;
			ComponentInfo.bShowAsBool = true;
		}
		else if (Property->IsA(FEnumProperty::StaticClass()))
		{
			FComponentInfo& ComponentInfo = InComponentInfos.AddDefaulted_GetRef();
			ComponentInfo.Name = PropertyName;
			ComponentInfo.ComponentOffset = FoundInt32Components++;
			ComponentInfo.bIsInt32 = true;
			ComponentInfo.Enum = CastFieldChecked<FEnumProperty>(Property)->GetEnum();
		}
		else if (Property->IsA(FStructProperty::StaticClass()))
		{
			const FStructProperty* StructProperty = CastFieldChecked<FStructProperty>(Property);
			BuildComponentInfos(PropertyName, FNiagaraTypeHelper::FindNiagaraFriendlyTopLevelStruct(StructProperty->Struct, ENiagaraStructConversion::Simulation), InComponentInfos);
		}
		else
		{
			// Fail
		}
	}
}

#undef LOCTEXT_NAMESPACE
