// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamUserSettings.h"
#include "Misc/Paths.h"

UVirtualCameraUserSettings::UVirtualCameraUserSettings()
{
	PhotoSaveLocation.Path = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("VCamPhotos"));
	TextureSaveLocation.Path = TEXT("/Game/VCamPhotos");
	PhotoSaveMode = EVCamPhotoSaveMode::Both;
}