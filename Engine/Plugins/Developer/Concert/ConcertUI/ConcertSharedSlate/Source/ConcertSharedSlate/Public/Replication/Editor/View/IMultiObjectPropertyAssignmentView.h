// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyAssignmentView.h"

namespace UE::ConcertSharedSlate
{
	/** A special reassignment view which shows the display object and all its children. */
	class CONCERTSHAREDSLATE_API IMultiObjectPropertyAssignmentView : public IPropertyAssignmentView
	{
	public:

		/** Sets whether subobjects should be shown in the view (applies only to subobjects other than UActorComponents, which are always shown). */
		virtual void SetShouldShowSubobjects(bool bShowSubobjects) = 0;
		/** @return Whether subobjects should be shown */
		virtual bool GetShouldShowSubobjects() const = 0;
	};
}
