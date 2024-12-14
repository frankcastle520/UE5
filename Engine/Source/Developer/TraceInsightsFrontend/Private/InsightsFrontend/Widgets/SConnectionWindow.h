// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Async/TaskGraphFwd.h"
#include "Input/Reply.h"
#include "Templates/SharedPointer.h"
#include "Widgets/SCompoundWidget.h"

#include <atomic>

class SVerticalBox;
class SEditableTextBox;
class SNotificationList;

namespace UE::Trace
{
	class FStoreConnection;
}

namespace UE::Insights
{

/** Implements the Connection window. */
class SConnectionWindow : public SCompoundWidget
{
public:
	SConnectionWindow();
	virtual ~SConnectionWindow();

	SLATE_BEGIN_ARGS(SConnectionWindow) {}
	SLATE_END_ARGS()

	/** Constructs this widget. */
	void Construct(const FArguments& InArgs, TSharedRef<UE::Trace::FStoreConnection> InTraceStoreConnection);

private:
	TSharedRef<SWidget> ConstructConnectPanel();
	FReply Connect_OnClicked();

private:
	TSharedPtr<UE::Trace::FStoreConnection> TraceStoreConnection;

	TSharedPtr<SVerticalBox> MainContentPanel;
	TSharedPtr<SEditableTextBox> TraceRecorderAddressTextBox;
	TSharedPtr<SEditableTextBox> RunningInstanceAddressTextBox;
	TSharedPtr<SEditableTextBox> ChannelsTextBox;

	/** Widget for the non-intrusive notifications. */
	TSharedPtr<SNotificationList> NotificationList;

	FGraphEventRef ConnectTask;

	std::atomic<bool> bIsConnecting = false;
	std::atomic<bool> bIsConnectedSuccessfully = false;
};

} // namespace UE::Insights
