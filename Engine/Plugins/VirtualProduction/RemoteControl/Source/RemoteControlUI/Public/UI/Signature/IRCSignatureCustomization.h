// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FDragDropEvent;
class FReply;
class IRCSignatureItem;

/** Customization Interface to extend the capabilities of RC Signature UI */
class IRCSignatureCustomization
{
public:
	virtual bool CanAcceptDrop(const FDragDropEvent& InDragDropEvent, IRCSignatureItem* InSignatureItem) const = 0;

	virtual FReply AcceptDrop(const FDragDropEvent& InDragDropEvent, IRCSignatureItem* InSignatureItem) const = 0;
};
