// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeTypes.h"
#include "StateTreeEvents.h"
#include "StateTree.h" // FStateTreeCustomVersion
#include "Math/ColorList.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeTypes)

DEFINE_LOG_CATEGORY(LogStateTree);

namespace UE::StateTree::Colors
{
	FColor Darken(const FColor Col, const float Level)
	{
		const int32 Mul = (int32)FMath::Clamp(Level * 255.f, 0.f, 255.f);
		const int32 R = (int32)Col.R * Mul / 255;
		const int32 G = (int32)Col.G * Mul / 255;
		const int32 B = (int32)Col.B * Mul / 255;
		return FColor((uint8)R, (uint8)G, (uint8)B, Col.A);
	}

	const FColor Grey = FColor::FromHex(TEXT("#949494"));
	const FColor Red = FColor::FromHex(TEXT("#DE6659"));
	const FColor Orange = FColor::FromHex(TEXT("#E3983F"));
	const FColor Yellow = FColor::FromHex(TEXT("#EFD964"));
	const FColor Green = FColor::FromHex(TEXT("#8AB75E"));
	const FColor Cyan = FColor::FromHex(TEXT("#56C3BD"));
	const FColor Blue = FColor::FromHex(TEXT("#649ED3"));
	const FColor Purple = FColor::FromHex(TEXT("#B397D6"));
	const FColor Magenta = FColor::FromHex(TEXT("#CE85C7"));
	const FColor Bronze = FColorList::Bronze;

	constexpr float DarkenLevel = 0.6f;
	const FColor DarkGrey = Darken(Grey, DarkenLevel);
	const FColor DarkRed = Darken(Red, DarkenLevel);
	const FColor DarkOrange = Darken(Orange, DarkenLevel);
	const FColor DarkYellow = Darken(Yellow, DarkenLevel);
	const FColor DarkGreen = Darken(Green, DarkenLevel);
	const FColor DarkCyan = Darken(Cyan, DarkenLevel);
	const FColor DarkBlue = Darken(Blue, DarkenLevel);
	const FColor DarkPurple = Darken(Purple, DarkenLevel);
	const FColor DarkMagenta = Darken(Magenta, DarkenLevel);
	const FColor DarkBronze = Darken(Bronze, DarkenLevel);
} // UE::StateTree::Colors


const FStateTreeStateHandle FStateTreeStateHandle::Invalid = FStateTreeStateHandle();
const FStateTreeStateHandle FStateTreeStateHandle::Succeeded = FStateTreeStateHandle(SucceededIndex);
const FStateTreeStateHandle FStateTreeStateHandle::Failed = FStateTreeStateHandle(FailedIndex);
const FStateTreeStateHandle FStateTreeStateHandle::Stopped = FStateTreeStateHandle(StoppedIndex);
const FStateTreeStateHandle FStateTreeStateHandle::Root = FStateTreeStateHandle(0);

const FStateTreeDataHandle FStateTreeDataHandle::Invalid = FStateTreeDataHandle();

const FStateTreeIndex16 FStateTreeIndex16::Invalid = FStateTreeIndex16();
const FStateTreeIndex8 FStateTreeIndex8::Invalid = FStateTreeIndex8();


//////////////////////////////////////////////////////////////////////////
// FStateTreeStateLink

EStateTreeRunStatus FStateTreeStateHandle::ToCompletionStatus() const
{
	if (Index == SucceededIndex)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	if (Index == FailedIndex)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (Index == StoppedIndex)
	{
		return EStateTreeRunStatus::Stopped;
	}
	return EStateTreeRunStatus::Unset;
}

FStateTreeStateHandle FStateTreeStateHandle::FromCompletionStatus(const EStateTreeRunStatus Status)
{
	if (Status == EStateTreeRunStatus::Succeeded)
	{
		return Succeeded;
	}

	if (Status == EStateTreeRunStatus::Failed)
	{
		return Failed;
	}

	if (Status == EStateTreeRunStatus::Stopped)
	{
		return Stopped;
	}
	return {};
}

bool FStateTreeStateLink::Serialize(FStructuredArchive::FSlot Slot)
{
	Slot.GetUnderlyingArchive().UsingCustomVersion(FStateTreeCustomVersion::GUID);
	return false; // Let the default serializer handle serializing.
}

void FStateTreeStateLink::PostSerialize(const FArchive& Ar)
{
#if WITH_EDITORONLY_DATA
	const int32 CurrentVersion = Ar.CustomVer(FStateTreeCustomVersion::GUID);
	if (CurrentVersion < FStateTreeCustomVersion::AddedExternalTransitions)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		LinkType = Type_DEPRECATED;
		if (LinkType == EStateTreeTransitionType::NotSet)
		{
			LinkType = EStateTreeTransitionType::None;
		}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
#endif // WITH_EDITORONLY_DAT
}

bool FCompactEventDesc::DoesEventMatchDesc(const FStateTreeEvent& Event) const
{
	if (Tag.IsValid() && (!Event.Tag.IsValid() || !Event.Tag.MatchesTag(Tag)))
	{
		return false;
	}

	const UScriptStruct* EventPayloadStruct = Event.Payload.GetScriptStruct();
	if (PayloadStruct && (EventPayloadStruct == nullptr || !EventPayloadStruct->IsChildOf(PayloadStruct)))
	{
		return false;
	}

	return true;
}
