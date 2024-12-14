// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleMainStreamObjectVersion.h"

#include "Serialization/CustomVersion.h"

const FGuid FDMXControlConsoleMainStreamObjectVersion::GUID(0x6C3BE9C2, 0x4D818685, 0x4D93ABAE, 0x9AF3C0BA);

// Register the custom version with core
FCustomVersionRegistration GRegisterDMXControlConsoleMainStreamObjectVersion(FDMXControlConsoleMainStreamObjectVersion::GUID, FDMXControlConsoleMainStreamObjectVersion::LatestVersion, TEXT("DMXControlConsoleMainStreamObjectVersion"));
