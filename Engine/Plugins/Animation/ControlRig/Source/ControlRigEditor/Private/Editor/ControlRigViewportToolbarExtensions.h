// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointerFwd.h"
#include "UObject/NameTypes.h"

class FUICommandList;

namespace UE::ControlRig
{

void PopulateControlRigViewportToolbarTransformsSubmenu(const FName InMenuName);
void PopulateControlRigViewportToolbarSelectionSubmenu(const FName InMenuName);
void PopulateControlRigViewportToolbarShowSubmenu(const FName InMenuName, const TSharedPtr<const FUICommandList>& InCommandList);

void RemoveControlRigViewportToolbarExtensions();

} // namespace UE::ControlRig
