// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMessageContext.h"
#include "ITraceController.h"
#include "MessageEndpoint.h"
#include "TraceControllerCommands.h"
#include "TraceControlMessages.h"

struct FTraceControlSettings;
struct FTraceControlStatus;
struct FTraceControlDiscovery;
class IMessageBus;
class FMessageEndpoint;

/**
 * Interface to control other sessions tracing.
 */
class FTraceController : public ITraceController 
{
public:
	FTraceController(const TSharedRef<IMessageBus>& InMessageBus);
	virtual ~FTraceController() override;

private:
	virtual void SendDiscoveryRequest(const FGuid& SessionId, const FGuid& InstanceId) const override;
	virtual void SendDiscoveryRequest() override;
	virtual void SendStatusUpdateRequest() override;
	virtual void SendChannelUpdateRequest() override;
	virtual void SendSettingsUpdateRequest() override;
	virtual bool HasAvailableInstance(const FGuid& InstanceId) override;
	virtual void WithInstance(FGuid InstanceId, FCallback Func) override;
	
	DECLARE_DERIVED_EVENT(FTraceController, ITraceController::FStatusRecievedEvent, FStatusRecievedEvent);
	virtual FStatusRecievedEvent& OnStatusReceived() override
	{
		return StatusReceivedEvent;
	}

	/* Message handlers */
	void OnNotification(const FMessageBusNotification& MessageBusNotification);
	void OnDiscoveryResponse(const FTraceControlDiscovery& Message, const TSharedRef<IMessageContext>& Context);
	void OnStatus(const FTraceControlStatus& Message, const TSharedRef<IMessageContext>& Context);
	void OnChannelsDesc(const FTraceControlChannelsDesc& Message, const TSharedRef<IMessageContext>& Context);
	void OnChannelsStatus(const FTraceControlChannelsStatus& Message, const TSharedRef<IMessageContext>& Context);
	void OnSettings(const FTraceControlSettings& Message, const TSharedRef<IMessageContext>& Context);
	static void UpdateStatus(const FTraceControlStatus& Message, FTraceStatus& Status);

private:
	
	struct FTracingInstance
	{
		FTraceStatus Status;
		FTraceControllerCommands Commands;

		FTracingInstance(const TSharedRef<IMessageBus>& InMessageBus, FMessageAddress Service);
		FTracingInstance() = delete;
	};

	/**
	 * Needed to create command instances when new sessions are discovered. We don't need a ref counted
	 * pointer to the message bus.
	 */
	TWeakPtr<IMessageBus> MessageBus;

	/** Our own endpoint for messages */
	TSharedPtr<FMessageEndpoint> MessageEndpoint;

	/** Address of the runtime endpoint for trace controls */
	FMessageAddress TraceControlAddress;

	/** Event for status updates on any session */
	FStatusRecievedEvent StatusReceivedEvent;

	/** Lock to protect access to Instances list */
	FRWLock InstancesLock;

	/** Known instances with an active trace service */
	TMap<FMessageAddress, FTracingInstance> Instances;

	/** Secondary lookup from instance -> address */
	TMap<FGuid, FMessageAddress> InstanceToAddress;
};
