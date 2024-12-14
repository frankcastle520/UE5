// Copyright Epic Games, Inc. All Rights Reserved.

#include "WindowsStylusInputPluginAsync.h"

#if PLATFORM_WINDOWS

namespace UE::StylusInput::Private::Windows
{
	FWindowsStylusInputPluginAsync::FWindowsStylusInputPluginAsync(IStylusInputInstance* Instance,
	                                                               FGetWindowContextCallback&& GetWindowContext,
	                                                               FUpdateTabletContextsCallback&& UpdateTabletContextsCallback,
	                                                               IStylusInputEventHandler* EventHandler)
		: FWindowsStylusInputPluginBase(Instance, MoveTemp(GetWindowContext), MoveTemp(UpdateTabletContextsCallback))
	{
		if (EventHandler)
		{
			// Immediately install an event handler during construction to capture events coming through during plugin initialization.
			AddEventHandler(EventHandler);
		}
	}

	FWindowsStylusInputPluginAsync::~FWindowsStylusInputPluginAsync()
	{
	}

	HRESULT FWindowsStylusInputPluginAsync::RealTimeStylusEnabled(IRealTimeStylus* RealTimeStylus, const ULONG TabletContextIDsCount,
	                                                              const TABLET_CONTEXT_ID* TabletContextIDs)
	{
		return ProcessRealTimeStylusEnabled(RealTimeStylus, TabletContextIDsCount, TabletContextIDs);
	}

	HRESULT FWindowsStylusInputPluginAsync::RealTimeStylusDisabled(IRealTimeStylus* RealTimeStylus, const ULONG TabletContextIDsCount,
	                                                               const TABLET_CONTEXT_ID* TabletContextIDs)
	{
		return ProcessRealTimeStylusDisabled(RealTimeStylus, TabletContextIDsCount, TabletContextIDs);
	}

	HRESULT FWindowsStylusInputPluginAsync::StylusDown(IRealTimeStylus* RealTimeStylus, const StylusInfo* StylusInfo, const ULONG PropertyCount,
	                                                   LONG* PacketBuffer, LONG**)
	{
		return ProcessPackets(StylusInfo, 1, PropertyCount, EPacketType::StylusDown, reinterpret_cast<int32*>(PacketBuffer));
	}

	HRESULT FWindowsStylusInputPluginAsync::StylusUp(IRealTimeStylus* RealTimeStylus, const StylusInfo* StylusInfo, const ULONG PropertyCount,
	                                                 LONG* PacketBuffer, LONG**)
	{
		return ProcessPackets(StylusInfo, 1, PropertyCount, EPacketType::StylusUp, reinterpret_cast<int32*>(PacketBuffer));
	}

	HRESULT FWindowsStylusInputPluginAsync::TabletAdded(IRealTimeStylus* RealTimeStylus, IInkTablet* Tablet)
	{
		return ProcessTabletAdded(Tablet);
	}

	HRESULT FWindowsStylusInputPluginAsync::TabletRemoved(IRealTimeStylus* RealTimeStylus, const LONG TabletIndex)
	{
		return ProcessTabletRemoved(TabletIndex);
	}

	HRESULT FWindowsStylusInputPluginAsync::InAirPackets(IRealTimeStylus* RealTimeStylus, const StylusInfo* StylusInfo, const ULONG PacketCount,
	                                                const ULONG PacketBufferLength, LONG* PacketBuffer, ULONG*, LONG**)
	{
		return ProcessPackets(StylusInfo, PacketCount, PacketBufferLength, EPacketType::AboveDigitizer, reinterpret_cast<int32*>(PacketBuffer));
	}

	HRESULT FWindowsStylusInputPluginAsync::Packets(IRealTimeStylus* RealTimeStylus, const StylusInfo* StylusInfo, const ULONG PacketCount,
													const ULONG PacketBufferLength, LONG* PacketBuffer, ULONG*, LONG**)
	{
		return ProcessPackets(StylusInfo, PacketCount, PacketBufferLength, EPacketType::OnDigitizer, reinterpret_cast<int32*>(PacketBuffer));
	}

	HRESULT FWindowsStylusInputPluginAsync::StylusInRange(IRealTimeStylus* RealTimeStylus, TABLET_CONTEXT_ID TabletContextID, STYLUS_ID StylusID)
	{
		return E_NOTIMPL;
	}

	HRESULT FWindowsStylusInputPluginAsync::StylusOutOfRange(IRealTimeStylus* RealTimeStylus, TABLET_CONTEXT_ID TabletContextID, STYLUS_ID StylusID)
	{
		return E_NOTIMPL;
	}

	HRESULT FWindowsStylusInputPluginAsync::StylusButtonDown(IRealTimeStylus* RealTimeStylus, STYLUS_ID StylusID, const GUID* GuidStylusButton,
	                                                         POINT* StylusPos)
	{
		return E_NOTIMPL;
	}

	HRESULT FWindowsStylusInputPluginAsync::StylusButtonUp(IRealTimeStylus* RealTimeStylus, STYLUS_ID StylusID, const GUID* GuidStylusButton, POINT* StylusPos)
	{
		return E_NOTIMPL;
	}

	HRESULT FWindowsStylusInputPluginAsync::CustomStylusDataAdded(IRealTimeStylus* RealTimeStylus, const GUID* GuidId, ULONG DataCount, const BYTE* Data)
	{
		return E_NOTIMPL;
	}

	HRESULT FWindowsStylusInputPluginAsync::SystemEvent(IRealTimeStylus* RealTimeStylus, TABLET_CONTEXT_ID TabletContextID, STYLUS_ID StylusID,
	                                                    SYSTEM_EVENT Event, SYSTEM_EVENT_DATA EventData)
	{
		return E_NOTIMPL;
	}

	HRESULT FWindowsStylusInputPluginAsync::Error(IRealTimeStylus*, IStylusPlugin*, const RealTimeStylusDataInterest DataInterest, const HRESULT ErrorCode,
	                                              LONG_PTR*)
	{
		return ProcessError(DataInterest, ErrorCode);
	}

	HRESULT FWindowsStylusInputPluginAsync::UpdateMapping(IRealTimeStylus* RealTimeStylus)
	{
		return E_NOTIMPL;
	}

	HRESULT FWindowsStylusInputPluginAsync::DataInterest(RealTimeStylusDataInterest* DataInterest)
	{
		return ProcessDataInterest(DataInterest);
	}
}

#endif
