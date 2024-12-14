// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleCueStack.h"

#include "Algo/Find.h"
#include "DMXControlConsoleFaderBase.h"
#include "Layouts/Controllers/DMXControlConsoleControllerBase.h"


FDMXControlConsoleCue* UDMXControlConsoleCueStack::AddNewCue(const TArray<UDMXControlConsoleFaderBase*>& Faders, const FString CueLabel, const FLinearColor CueColor)
{
	if (Faders.IsEmpty())
	{
		return nullptr;
	}

	FDMXControlConsoleCue NewCue;
	NewCue.CueLabel = GenerateUniqueCueLabel(CueLabel);
	NewCue.CueColor = CueColor == FLinearColor::Transparent ? FLinearColor::MakeRandomColor() : CueColor;

	CuesArray.Add(NewCue);
	
	UpdateCueData(NewCue.CueID, Faders);

	return &CuesArray.Last();
}

void UDMXControlConsoleCueStack::RemoveCue(const FDMXControlConsoleCue& Cue)
{
	CuesArray.Remove(Cue);

	OnCueStackChanged.Broadcast();
}

FDMXControlConsoleCue* UDMXControlConsoleCueStack::FindCue(const FGuid CueID)
{
	return Algo::FindBy(CuesArray, CueID, &FDMXControlConsoleCue::CueID);
}

FDMXControlConsoleCue* UDMXControlConsoleCueStack::FindCue(const FString& CueLabel)
{
	return Algo::FindBy(CuesArray, CueLabel, &FDMXControlConsoleCue::CueLabel);
}

void UDMXControlConsoleCueStack::UpdateCueData(const FGuid CueID, const TArray<UDMXControlConsoleFaderBase*>& Faders)
{
	FDMXControlConsoleCue* Cue = FindCue(CueID);
	if (!Cue)
	{
		return;
	}

	Cue->FaderToValueMap.Reset();
	for (UDMXControlConsoleFaderBase* Fader : Faders)
	{
		if (Fader)
		{
			const uint32 Value = Fader->GetValue();
			Cue->FaderToValueMap.FindOrAdd(Fader) = Value;
		}
	}

	OnCueStackChanged.Broadcast();

#if WITH_EDITOR
	bCanStore = false;
#endif // WITH_EDITOR 
}

void UDMXControlConsoleCueStack::MoveCueToIndex(const FDMXControlConsoleCue& Cue, const uint32 NewIndex)
{
	if (!CuesArray.Contains(Cue) || !CuesArray.IsValidIndex(NewIndex))
	{
		return;
	}

	if (CuesArray.IndexOfByKey(Cue) == NewIndex)
	{
		return;
	}

	CuesArray.Remove(Cue);
	CuesArray.Insert(Cue, NewIndex);

	OnCueStackChanged.Broadcast();
}

void UDMXControlConsoleCueStack::Recall(const FDMXControlConsoleCue& Cue)
{
	if (!CuesArray.Contains(Cue))
	{
		return;
	}

	const TMap<TWeakObjectPtr<UDMXControlConsoleFaderBase>, uint32>& FaderToValueMap = Cue.FaderToValueMap;
	for (const TTuple<TWeakObjectPtr<UDMXControlConsoleFaderBase>, uint32>& FaderToValue : FaderToValueMap)
	{
		if (UDMXControlConsoleFaderBase* Fader = FaderToValue.Key.Get())
		{
			const uint32 Value = FaderToValue.Value;

			Fader->Modify();
			Fader->SetValue(Value);
		}
	}

	OnCueStackChanged.Broadcast();

#if WITH_EDITOR
	bCanStore = false;
#endif // WITH_EDITOR 
}

void UDMXControlConsoleCueStack::Clear()
{
	CuesArray.Reset();

	OnCueStackChanged.Broadcast();
}

#if WITH_EDITOR
void UDMXControlConsoleCueStack::OnFadersPropertiesChanged(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (!bCanStore)
	{
		bCanStore = true;
	}
}
#endif // WITH_EDITOR 

void UDMXControlConsoleCueStack::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITOR
	if (!UDMXControlConsoleControllerBase::GetOnPropertiesChanged().IsBoundToObject(this))
	{
		UDMXControlConsoleControllerBase::GetOnPropertiesChanged().AddUObject(this, &UDMXControlConsoleCueStack::OnFadersPropertiesChanged);
	}
#endif // WITH_EDITOR 
}

FString UDMXControlConsoleCueStack::GenerateUniqueCueLabel(const FString& CueLabel)
{
	FString NewCueLabel = CueLabel;
	if (!NewCueLabel.IsEmpty() && !FindCue(NewCueLabel))
	{
		return NewCueLabel;
	}
	else if (NewCueLabel.IsEmpty() && CuesArray.IsEmpty())
	{
		NewCueLabel = TEXT("Cue 0");
	}

	for (int32 Index = 0; Index < CuesArray.Num(); ++Index)
	{
		NewCueLabel = FString::Format(TEXT("Cue {0}"), { Index });

		const FDMXControlConsoleCue* Cue = FindCue(NewCueLabel);
		if (Cue == nullptr)
		{
			break;
		}
		else if (Index == CuesArray.Num() - 1)
		{
			NewCueLabel = FString::Format(TEXT("Cue {0}"), { CuesArray.Num() });
		}
	}

	return NewCueLabel;
}
