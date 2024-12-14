// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

class IPropertyHandle;

namespace UE::RemoteControl::DMX
{
	/** Details customization for URemoteControlDMXPresetUserData */
	class FRemoteControlProtocolDMXPresetUserDataDetails final : public IDetailCustomization
	{
	public:
		/** Creates a new instance of this details customization. */
		static TSharedRef<IDetailCustomization> MakeInstance();

		//~ Begin IDetailCustomization interface
		virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
		//~ End IDetailCustomization interface

	private:
		/** Property handle for the DMXLibrary property */
		TSharedPtr<IPropertyHandle> DMXLibraryHandle;
	};
}
