// Copyright Epic Games, Inc. All Rights Reserved.

#include "ApplicationWrapper.h"

namespace UE::PixelStreaming2Input
{
	FPixelStreaming2ApplicationWrapper::FPixelStreaming2ApplicationWrapper(TSharedPtr<GenericApplication> InWrappedApplication)
		: GenericApplication(MakeShareable(new FCursor()))
		, WrappedApplication(InWrappedApplication)
	{
		InitModifierKeys();
	}

	void FPixelStreaming2ApplicationWrapper::SetTargetWindow(TWeakPtr<SWindow> InTargetWindow)
	{
		TargetWindow = InTargetWindow;
	}

	TSharedPtr<FGenericWindow> FPixelStreaming2ApplicationWrapper::GetWindowUnderCursor()
	{
		TSharedPtr<SWindow> Window = TargetWindow.Pin();
		if (Window.IsValid())
		{
			FVector2D CursorPosition = Cursor->GetPosition();
			FGeometry WindowGeometry = Window->GetWindowGeometryInScreen();

			FVector2D WindowOffset = WindowGeometry.GetAbsolutePosition();
			FVector2D WindowSize = WindowGeometry.GetAbsoluteSize();

			FBox2D WindowRect(WindowOffset, WindowSize);
			if (WindowRect.IsInside(CursorPosition))
			{
				return Window->GetNativeWindow();
			}
		}

		return WrappedApplication->GetWindowUnderCursor();
	}

	/** Initialize the list of possible modifier keys. */
	void FPixelStreaming2ApplicationWrapper::InitModifierKeys()
	{
		ModifierKeys[EModifierKey::LeftShift].AgnosticKey = &EKeys::LeftShift;
		ModifierKeys[EModifierKey::RightShift].AgnosticKey = &EKeys::RightShift;
		ModifierKeys[EModifierKey::LeftControl].AgnosticKey = &EKeys::LeftControl;
		ModifierKeys[EModifierKey::RightControl].AgnosticKey = &EKeys::RightControl;
		ModifierKeys[EModifierKey::LeftAlt].AgnosticKey = &EKeys::LeftAlt;
		ModifierKeys[EModifierKey::RightAlt].AgnosticKey = &EKeys::RightAlt;
		ModifierKeys[EModifierKey::CapsLock].AgnosticKey = &EKeys::CapsLock;
	}

	/**
	 * When the user presses or releases a modifier key then update its state to
	 * active or back to inactive.
	 * @param InAgnosticKey - The key the user is pressing.
	 * @param bInActive - Whether the key is pressed (active) or released (inactive).
	 */
	void FPixelStreaming2ApplicationWrapper::UpdateModifierKey(const FKey* InAgnosticKey, bool bInActive)
	{
		for (int KeyIndex = EModifierKey::LeftShift; KeyIndex < EModifierKey::Count; KeyIndex++)
		{
			FModifierKey& ModifierKey = ModifierKeys[KeyIndex];
			if (ModifierKey.AgnosticKey == InAgnosticKey)
			{
				ModifierKey.bActive = bInActive;
				break;
			}
		}
	}

	/**
	 * Return the current set of active modifier keys.
	 * @return The current set of active modifier keys.
	 */
	FModifierKeysState FPixelStreaming2ApplicationWrapper::GetModifierKeys() const
	{
		FModifierKeysState ModifierKeysState(
			/*bInIsLeftShiftDown*/ ModifierKeys[EModifierKey::LeftShift].bActive,
			/*bInIsRightShiftDown*/ ModifierKeys[EModifierKey::RightShift].bActive,
			/*bInIsLeftControlDown*/ ModifierKeys[EModifierKey::LeftControl].bActive,
			/*bInIsRightControlDown*/ ModifierKeys[EModifierKey::RightControl].bActive,
			/*bInIsLeftAltDown*/ ModifierKeys[EModifierKey::LeftAlt].bActive,
			/*bInIsRightAltDown*/ ModifierKeys[EModifierKey::RightAlt].bActive,
			/*bInIsLeftCommandDown*/ false,
			/*bInIsRightCommandDown*/ false,
			/*bInAreCapsLocked*/ ModifierKeys[EModifierKey::CapsLock].bActive);
		return ModifierKeysState;
	}
} // namespace UE::PixelStreaming2Input