// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "MetaHumanBodyType.generated.h"

UENUM()
enum class EMetaHumanBodyType : uint8
{
	f_med_nrw = 0,
	f_med_ovw,
	f_med_unw,
	f_srt_nrw,
	f_srt_ovw,
	f_srt_unw,
	f_tal_nrw,
	f_tal_ovw,
	f_tal_unw,
	m_med_nrw,
	m_med_ovw,
	m_med_unw,
	m_srt_nrw,
	m_srt_ovw,
	m_srt_unw,
	m_tal_nrw,
	m_tal_ovw,
	m_tal_unw,
	Count UMETA(Hidden)
};


UENUM()
enum class EMetaHumanBodyBodyPartIndex : uint32
{
	Body = 0,
	Face,
	Torso,
	Legs,
	Feet,
	Count UMETA(Hidden)
};
