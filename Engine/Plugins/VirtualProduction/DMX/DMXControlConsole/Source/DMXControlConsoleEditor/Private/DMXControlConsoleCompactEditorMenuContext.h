// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXControlConsoleCompactEditorMenuContext.generated.h"

namespace UE::DMX::Private { class SDMXControlConsoleCompactEditorView; }


/** Menu context for the compact editor toolbar */
UCLASS()
class UDMXControlConsoleCompactEditorMenuContext
	: public UObject
{
	GENERATED_BODY()

public:
	/** The compact editor view that uses this menu context */
	TWeakPtr<UE::DMX::Private::SDMXControlConsoleCompactEditorView> WeakCompactEditorView;
};
