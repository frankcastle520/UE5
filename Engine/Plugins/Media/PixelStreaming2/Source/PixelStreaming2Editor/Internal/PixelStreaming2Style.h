// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

namespace UE::EditorPixelStreaming2
{
    class PIXELSTREAMING2EDITOR_API FPixelStreaming2Style
    {
    public:
        static void Initialize();
        static void Shutdown();
        /** reloads textures used by slate renderer */
        static void ReloadTextures();
        /** @return The Slate style set for the Shooter game */
        static FSlateStyleSet& Get();
        static FName GetStyleSetName();
   
    private:
        static TSharedRef< class FSlateStyleSet > Create();
   
    private:
        static TSharedPtr< class FSlateStyleSet > StyleInstance;
    };
}