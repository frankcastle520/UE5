// Copyright Epic Games, Inc. All Rights Reserved.

#include "Experimental/ZenServerInterface.h"

#include "ZenBackendUtils.h"
#include "ZenSerialization.h"
#include "ZenServerHttp.h"
#include "ZenServerState.h"
#include "ZenVersion.h"

#include "AnalyticsEventAttribute.h"
#include "Async/Async.h"
#include "Async/UniqueLock.h"
#include "Containers/AnsiString.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/Platform.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Logging/LogScopedCategoryAndVerbosityOverride.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"
#include "Misc/FileHelper.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/ScopeExit.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "String/LexFromString.h"
#include "Serialization/CompactBinaryWriter.h"

#if PLATFORM_WINDOWS
#	include "Windows/AllowWindowsPlatformTypes.h"
#	include <Windows.h>
#	include "Windows/HideWindowsPlatformTypes.h"
#endif

#define ALLOW_SETTINGS_OVERRIDE_FROM_COMMANDLINE			(UE_SERVER || !(UE_BUILD_SHIPPING))

namespace UE::Zen
{

DEFINE_LOG_CATEGORY_STATIC(LogZenServiceInstance, Log, All);

struct FZenServiceLink
{
	FString ServicePath;
	FString UtilityPath;
	FZenVersion Version;

	operator bool() const
	{
		return !ServicePath.IsEmpty() && !UtilityPath.IsEmpty() && Version;
	}

	static FZenServiceLink Read(const FString& Filename)
	{
		FString JsonText;
		if (FFileHelper::LoadFileToString(JsonText, *Filename))
		{
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonText);
			if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
			{
				FString ServicePath = JsonObject->Values.FindRef(TEXT("ServicePath"))->AsString();
				FString UtilityPath = JsonObject->Values.FindRef(TEXT("UtilityPath"))->AsString();
				auto VersionObject = JsonObject->Values.FindRef(TEXT("Version"))->AsObject();
				if (VersionObject)
				{
					uint32_t MajorVersion = static_cast<uint32_t>(VersionObject->TryGetField(TEXT("Major"))->AsNumber());
					uint32_t MinorVersion = static_cast<uint32_t>(VersionObject->TryGetField(TEXT("Minor"))->AsNumber());
					uint32_t PatchVersion = static_cast<uint32_t>(VersionObject->TryGetField(TEXT("Patch"))->AsNumber());
					FString Details = VersionObject->TryGetField(TEXT("Details"))->AsString();
					return FZenServiceLink{
						.ServicePath = ServicePath,
						.UtilityPath = UtilityPath,
						.Version = FZenVersion{
							.MajorVersion = MajorVersion,
							.MinorVersion = MinorVersion,
							.PatchVersion = PatchVersion,
							.Details = Details}
					};
				}
			}
		}
		return {};
	}

	static bool Write(const FZenServiceLink& Link, const FString& Filename)
	{
		FString JsonTcharText;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonTcharText);
		Writer->WriteObjectStart();

		Writer->WriteValue(TEXT("ServicePath"), Link.ServicePath);
		Writer->WriteValue(TEXT("UtilityPath"), Link.UtilityPath);

		Writer->WriteObjectStart(TEXT("Version"));
		Writer->WriteValue(TEXT("Major"), Link.Version.MajorVersion);
		Writer->WriteValue(TEXT("Minor"), Link.Version.MinorVersion);
		Writer->WriteValue(TEXT("Patch"), Link.Version.PatchVersion);
		Writer->WriteValue(TEXT("Details"), Link.Version.Details);
		Writer->WriteObjectEnd();

		Writer->WriteObjectEnd();
		Writer->Close();

		if (!FFileHelper::SaveStringToFile(JsonTcharText, *Filename))
		{
			return false;
		}

		return true;
	}
};

static FString
GetLocalZenRootPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPlatformProcess::UserSettingsDir(), *FApp::GetEpicProductIdentifier(), TEXT("Common")) + TEXT("/"));
}

static FString
GetServiceExecutableName()
{
	return
#if PLATFORM_WINDOWS
		TEXT("zenserver.exe")
#else
		TEXT("zenserver")
#endif
		;
}

static FString
GetUtilityExecutableName()
{
	return
#if PLATFORM_WINDOWS
		TEXT("zen.exe")
#else
		TEXT("zen")
#endif
		;
}

static FString
GetLocalInstallPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(GetLocalZenRootPath(), TEXT("Zen\\Install")));
}

static FString
GetServiceLinkPath()
{
	return FPaths::Combine(GetLocalInstallPath(), TEXT("zen.link"));
}

static FString
GetServiceCopyInstallPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(GetLocalInstallPath(), GetServiceExecutableName()));
}

static FString
GetUtilityCopyInstallPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(GetLocalInstallPath(), GetUtilityExecutableName()));
}

static FString
GetInstallVersionCachePath()
{
	FString InstallUtilityPath = GetUtilityCopyInstallPath();
	FString InstallVersionCache = FPaths::SetExtension(InstallUtilityPath, TEXT("version"));
	return InstallVersionCache;
}

static FString
GetInTreeVersionCache()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineSavedDir(), TEXT("Zen"), TEXT("zen.version")));
}

static FString
GetServiceRunContextPath()
{
	return FPaths::SetExtension(GetServiceCopyInstallPath(), TEXT(".runcontext"));
}

static FString
GetInTreeUtilityPath()
{
	return FPaths::ConvertRelativePathToFull(FPlatformProcess::GenerateApplicationPath(TEXT("zen"), EBuildConfiguration::Development));
}

static FString
GetInTreeServicePath()
{
	return FPaths::ConvertRelativePathToFull(FPlatformProcess::GenerateApplicationPath(TEXT("zenserver"), EBuildConfiguration::Development));
}

static FString
GetInTreeCrashpadHandlerFilePath()
{
	return FPaths::ConvertRelativePathToFull(FPlatformProcess::GenerateApplicationPath(TEXT("crashpad_handler"), EBuildConfiguration::Development));
}

static FString
GetInstallCrashpadHandlerFilePath(const FString& InTreePath)
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(GetLocalInstallPath(), FString(FPathViews::GetCleanFilename(InTreePath))));
}


static bool GetZenVersion(const FString& UtilityPath, const FString& ServicePath, FZenVersion& OutVersion)
{
	FString AbsoluteUtilityPath = FPaths::ConvertRelativePathToFull(UtilityPath);
	FMonitoredProcess MonitoredUtilityProcess(AbsoluteUtilityPath, TEXT("version --detailed"), FPaths::GetPath(UtilityPath), true);

	bool bLaunched = MonitoredUtilityProcess.Launch();
	if (!bLaunched)
	{
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Failed to launch zen utility to gather version data: '%s'."), *UtilityPath);
		return false;
	}

	while (MonitoredUtilityProcess.Update())
	{
		FPlatformProcess::Sleep(0.1f);
		if (MonitoredUtilityProcess.GetDuration().GetTotalSeconds() > 10)
		{
			MonitoredUtilityProcess.Cancel(true);
			UE_LOG(LogZenServiceInstance, Warning, TEXT("Cancelled launch of zen utility for gathering version data: '%s'."), *UtilityPath);
			return false;
		}
	}

	FString OutputString = MonitoredUtilityProcess.GetFullOutputWithoutDelegate();
	if (MonitoredUtilityProcess.GetReturnCode() != 0)
	{
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Unexpected return code after launch of zen utility for gathering version data: '%s' (%d). Output: '%s'"), *UtilityPath, MonitoredUtilityProcess.GetReturnCode(), *OutputString);
		return false;
	}

	FString VersionOutputString = OutputString.TrimStartAndEnd();

	if (!OutVersion.TryParse(*VersionOutputString))
	{
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Invalid version information after launch of zen utility for gathering version data: '%s' (`%s`)"), *UtilityPath, *VersionOutputString);
		return false;
	}
	return true;
}

static FZenVersion
GetZenVersion(const FString& UtilityPath, const FString& ServicePath, const FString& VersionCachePath)
{
	IFileManager& FileManager = IFileManager::Get();
	FDateTime UtilityExecutableModificationTime = FileManager.GetTimeStamp(*UtilityPath);
	FDateTime ServiceExecutableModificationTime = FileManager.GetTimeStamp(*ServicePath);

	FDateTime VersionCacheModificationTime = FileManager.GetTimeStamp(*VersionCachePath);
	bool VersionCacheIsOlderThanUtilityExecutable = VersionCacheModificationTime < UtilityExecutableModificationTime;
	bool VersionCacheIsOlderThanServerExecutable = VersionCacheModificationTime < ServiceExecutableModificationTime;
	bool VersionCacheIsUpToDate = (!VersionCacheIsOlderThanUtilityExecutable) && (!VersionCacheIsOlderThanServerExecutable);
	if (VersionCacheIsUpToDate)
	{
		FString VersionFileContents;
		if (FFileHelper::LoadFileToString(VersionFileContents, *VersionCachePath))
		{
			FZenVersion CachedVersion;
			if (CachedVersion.TryParse(*VersionFileContents))
			{
				return CachedVersion;
			}
		}
	}
	auto GetFallbackVersion = [UtilityExecutableModificationTime, ServiceExecutableModificationTime]()
		{
			FZenVersion FallbackVersion;
			if (UtilityExecutableModificationTime > ServiceExecutableModificationTime)
			{
				FallbackVersion.Details = UtilityExecutableModificationTime.ToString();
				return FallbackVersion;
			}
			FallbackVersion.Details = ServiceExecutableModificationTime.ToString();
			return FallbackVersion;
		};


	FZenVersion Version;
	if (!GetZenVersion(UtilityPath, ServicePath, Version))
	{
		checkf(false, TEXT("Unable to determine version using zen utility executable path: '%s'."), *UtilityPath);
		Version = GetFallbackVersion();
	}
	FFileHelper::SaveStringToFile(Version.ToString(), *VersionCachePath);
	return Version;
}

static void
PromptUserToSyncInTreeVersion(const FString& ServerFilePath)
{
#if !IS_PROGRAM
	if (!FApp::IsUnattended() && !IsRunningCommandlet() && !GIsRunningUnattendedScript)
	{
		FText ZenSyncSourcePromptTitle = NSLOCTEXT("Zen", "Zen_SyncSourcePromptTitle", "Failed to launch");
		FText ZenSyncSourcePromptText = FText::Format(NSLOCTEXT("Zen", "Zen_SyncSourcePromptText", "Unreal Zen Storage Server can not verify installation. Please make sure your source installation in properly synced at '{0}'"), FText::FromString(FPaths::GetPath(ServerFilePath)));
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ZenSyncSourcePromptText.ToString(), *ZenSyncSourcePromptTitle.ToString());
	}
	else
#endif
	{
		// Just log as there is no one to show a message
		UE_LOG(LogZenServiceInstance, Display, TEXT("Unreal Zen Storage Server can not verify installation. Please make sure your source installation in properly synced at '%s'"), *FPaths::GetPath(ServerFilePath));
	}
}

static bool
IsInstallVersionOutOfDate(const FString& InTreeUtilityPath, const FString& InstallUtilityPath, const FString& InTreeServicePath, const FString& InstallServicePath, const FString& InTreeVersionCache, const FString& InstallVersionCache)
{
	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.FileExists(*InTreeUtilityPath) || !FileManager.FileExists(*InTreeServicePath))
	{
		UE_LOG(LogZenServiceInstance, Warning, TEXT("InTree version at '%s' is invalid"), *InTreeServicePath);
		PromptUserToSyncInTreeVersion(InTreeServicePath);
		return false;
	}

	// Always get the InTree utility path so cached version information is up to date
	FZenVersion InTreeVersion = GetZenVersion(InTreeUtilityPath, InTreeServicePath, InTreeVersionCache);
	UE_LOG(LogZenServiceInstance, Log, TEXT("InTree version at '%s' is '%s'"), *InTreeServicePath, *InTreeVersion.ToString());

	if (!FileManager.FileExists(*InstallUtilityPath) || !FileManager.FileExists(*InstallServicePath))
	{
		UE_LOG(LogZenServiceInstance, Log, TEXT("No installation found at '%s'"), *InstallServicePath);
		return true;
	}
	FZenVersion InstallVersion = GetZenVersion(InstallUtilityPath, InstallServicePath, InstallVersionCache);
	UE_LOG(LogZenServiceInstance, Log, TEXT("Installed version at '%s' is '%s'"), *InstallServicePath, *InstallVersion.ToString());

	if (InstallVersion < InTreeVersion)
	{
		UE_LOG(LogZenServiceInstance, Log, TEXT("Installed version at '%s' (%s) is older than '%s' (%s)"), *InstallServicePath, *InstallVersion.ToString(), *InTreeServicePath, *InTreeVersion.ToString());
		return true;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("ForceZenInstall")))
	{
		UE_LOG(LogZenServiceInstance, Display, TEXT("Forcing install from '%s' (%s) over '%s' (%s)"), *InTreeServicePath, *InTreeVersion.ToString(), *InstallServicePath, *InstallVersion.ToString());
		return true;
	}
	return false;
}

static bool
AttemptFileCopyWithRetries(const TCHAR* Dst, const TCHAR* Src, double RetryDurationSeconds)
{
	IFileManager& FileManager = IFileManager::Get();
	uint32 CopyResult = FileManager.Copy(Dst, Src, true, true, false);
	uint64 CopyWaitStartTime = FPlatformTime::Cycles64();
	while (CopyResult != COPY_OK)
	{
		double CopyWaitDuration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - CopyWaitStartTime);
		if (CopyWaitDuration < RetryDurationSeconds)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		else
		{
			break;
		}
		CopyResult = FileManager.Copy(Dst, Src, true, true, false);
	}
	if (CopyResult == COPY_OK)
	{
		return true;
	}
	UE_LOG(LogZenServiceInstance, Warning, TEXT("copy from '%s' to '%s', '%s'"), Src, Dst, CopyResult == COPY_Fail ? TEXT("Failed to copy file") : TEXT("Cancelled file copy"));
	return false;
}

static bool
AttemptFileDeleteWithRetries(const TCHAR* Path, double RetryDurationSeconds)
{
	IFileManager& FileManager = IFileManager::Get();
	bool DeleteResult = FileManager.Delete(Path, false, false, true);
	uint64 DeleteWaitStartTime = FPlatformTime::Cycles64();
	while (!DeleteResult)
	{
		double DeleteWaitDuration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - DeleteWaitStartTime);
		if (DeleteWaitDuration < RetryDurationSeconds)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		else
		{
			break;
		}
		DeleteResult = FileManager.Delete(Path, false, false, true);
	}
	if (DeleteResult)
	{
		return true;
	}
	return false;
}

static void EnsureEditorSettingsConfigLoaded()
{
#if !WITH_EDITOR
	if (GEditorSettingsIni.IsEmpty())
	{
		FConfigContext Context = FConfigContext::ReadIntoGConfig();
		Context.GeneratedConfigDir = FPaths::EngineEditorSettingsDir();
		Context.Load(TEXT("EditorSettings"), GEditorSettingsIni);
	}
#endif
}

static void
DetermineLocalDataCachePath(const TCHAR* ConfigSection, FString& DataPath)
{
	FString DataPathEnvOverride;
	if (GConfig->GetString(ConfigSection, TEXT("LocalDataCachePathEnvOverride"), DataPathEnvOverride, GEngineIni))
	{
		FString DataPathEnvOverrideValue = FPlatformMisc::GetEnvironmentVariable(*DataPathEnvOverride);
		if (!DataPathEnvOverrideValue.IsEmpty())
		{
			DataPath = DataPathEnvOverrideValue;
			UE_LOG(LogZenServiceInstance, Log, TEXT("Found environment variable %s=%s"), *DataPathEnvOverride, *DataPathEnvOverrideValue);
		}

		if (FPlatformMisc::GetStoredValue(TEXT("Epic Games"), TEXT("GlobalDataCachePath"), *DataPathEnvOverride, DataPathEnvOverrideValue))
		{
			if (!DataPathEnvOverrideValue.IsEmpty())
			{
				DataPath = DataPathEnvOverrideValue;
				UE_LOG(LogZenServiceInstance, Log, TEXT("Found registry key GlobalDataCachePath %s=%s"), *DataPathEnvOverride, *DataPath);
			}
		}
	}

	FString DataPathCommandLineOverride;
	if (GConfig->GetString(ConfigSection, TEXT("LocalDataCachePathCommandLineOverride"), DataPathCommandLineOverride, GEngineIni))
	{
		FString DataPathCommandLineOverrideValue;
		if (FParse::Value(FCommandLine::Get(), *(DataPathCommandLineOverride + TEXT("=")), DataPathCommandLineOverrideValue))
		{
			DataPath = DataPathCommandLineOverrideValue;
			UE_LOG(LogZenServiceInstance, Log, TEXT("Found command line override %s=%s"), *DataPathCommandLineOverride, *DataPath);
		}
	}

	// Paths starting with a '?' are looked up from config
	if (DataPath.StartsWith(TEXT("?")) && !GConfig->GetString(TEXT("DerivedDataCacheSettings"), *DataPath + 1, DataPath, GEngineIni))
	{
		DataPath.Empty();
	}

	FString DataPathEditorOverrideSetting;
	if (GConfig->GetString(ConfigSection, TEXT("LocalDataCachePathEditorOverrideSetting"), DataPathEditorOverrideSetting, GEngineIni))
	{
		EnsureEditorSettingsConfigLoaded();
		FString Setting = GConfig->GetStr(TEXT("/Script/UnrealEd.EditorSettings"), *DataPathEditorOverrideSetting, GEditorSettingsIni);
		if (!Setting.IsEmpty())
		{
			FString SettingPath;
			if (FParse::Value(*Setting, TEXT("Path="), SettingPath))
			{
				SettingPath.TrimQuotesInline();
				SettingPath.ReplaceEscapedCharWithCharInline();
				if (!SettingPath.IsEmpty())
				{
					DataPath = SettingPath;
					UE_LOG(LogZenServiceInstance, Log, TEXT("Found editor setting /Script/UnrealEd.EditorSettings.Path=%s"), *DataPath);
				}
			}
		}
	}
}

static bool
DetermineDataPath(const TCHAR* ConfigSection, FString& DataPath, bool& bHasInvalidPathConfigurations, bool& bIsDefaultDataPath)
{
	auto ValidateDataPath = [](const FString& InDataPath)
	{
		if (InDataPath.IsEmpty())
		{
			return FString{};
		}
		IFileManager& FileManager = IFileManager::Get();
		FString FinalPath = FPaths::ConvertRelativePathToFull(InDataPath);
		FPaths::NormalizeDirectoryName(FinalPath);
		FFileStatData StatData = FileManager.GetStatData(*InDataPath);
		if (StatData.bIsValid && StatData.bIsDirectory)
		{
			FString TestFilePath = FinalPath / FString::Printf(TEXT(".zen-startup-test-file-%d"), FPlatformProcess::GetCurrentProcessId());
			FArchive* TestFile = FileManager.CreateFileWriter(*TestFilePath, FILEWRITE_Silent);
			if (!TestFile)
			{
				return FString{};
			}
			TestFile->Close();
			delete TestFile;
			FileManager.Delete(*TestFilePath);
			return FinalPath;
		}
		if (FileManager.MakeDirectory(*InDataPath, true))
		{
			return FinalPath;
		}
		return FString{};
	};

	// Zen commandline
	FString CommandLineOverrideValue;
	if (FParse::Value(FCommandLine::Get(), TEXT("ZenDataPath="), CommandLineOverrideValue) && !CommandLineOverrideValue.IsEmpty())
	{
		if (FString Path = ValidateDataPath(CommandLineOverrideValue); !Path.IsEmpty())
		{
			DataPath = Path;
			UE_LOG(LogZenServiceInstance, Log, TEXT("Found command line override ZenDataPath=%s"), *CommandLineOverrideValue);
			return true;
		}
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Skipping command line override ZenDataPath=%s due to an invalid path"), *CommandLineOverrideValue);
		bHasInvalidPathConfigurations = true;
	}

	// Zen subprocess environment
	if (FString SubprocessDataPathEnvOverrideValue = FPlatformMisc::GetEnvironmentVariable(TEXT("UE-ZenSubprocessDataPath")); !SubprocessDataPathEnvOverrideValue.IsEmpty())
	{
		if (FString Path = ValidateDataPath(SubprocessDataPathEnvOverrideValue); !Path.IsEmpty())
		{
			DataPath = Path;
			UE_LOG(LogZenServiceInstance, Log, TEXT("Found subprocess environment variable UE-ZenSubprocessDataPath=%s"), *SubprocessDataPathEnvOverrideValue);
			return true;
		}
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Skipping subprocess environment variable UE-ZenSubprocessDataPath=%s due to an invalid path"), *SubprocessDataPathEnvOverrideValue);
		bHasInvalidPathConfigurations = true;
	}

	// Zen registry/stored
	FString DataPathEnvOverrideValue;
	if (FPlatformMisc::GetStoredValue(TEXT("Epic Games"), TEXT("Zen"), TEXT("DataPath"), DataPathEnvOverrideValue) && !DataPathEnvOverrideValue.IsEmpty())
	{
		if (FString Path = ValidateDataPath(DataPathEnvOverrideValue); !Path.IsEmpty())
		{
			DataPath = Path;
			UE_LOG(LogZenServiceInstance, Log, TEXT("Found registry key Zen DataPath=%s"), *DataPathEnvOverrideValue);
			return true;
		}
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Skipping registry key Zen DataPath=%s due to an invalid path"), *DataPathEnvOverrideValue);
		bHasInvalidPathConfigurations = true;
	}

	// Zen environment
	if (FString ZenDataPathEnvOverrideValue = FPlatformMisc::GetEnvironmentVariable(TEXT("UE-ZenDataPath")); !ZenDataPathEnvOverrideValue.IsEmpty())
	{
		if (FString Path = ValidateDataPath(ZenDataPathEnvOverrideValue); !Path.IsEmpty())
		{
			DataPath = Path;
			UE_LOG(LogZenServiceInstance, Log, TEXT("Found environment variable UE-ZenDataPath=%s"), *ZenDataPathEnvOverrideValue);
			return true;
		}
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Skipping environment variable UE-ZenDataPath=%s due to an invalid path"), *ZenDataPathEnvOverrideValue);
		bHasInvalidPathConfigurations = true;
	}

	// Follow local DDC (if outside workspace)
	FString LocalDataCachePath;
	DetermineLocalDataCachePath(ConfigSection, LocalDataCachePath);
	if (!LocalDataCachePath.IsEmpty() && (LocalDataCachePath != TEXT("None")) && !FPaths::IsUnderDirectory(LocalDataCachePath, FPaths::RootDir()))
	{
		FString ZenLocalDataCachePath = FPaths::Combine(LocalDataCachePath, TEXT("Zen"));
		if (FString Path = ValidateDataPath(ZenLocalDataCachePath); !Path.IsEmpty())
		{
			DataPath = Path;
			UE_LOG(LogZenServiceInstance, Log, TEXT("Found local data cache path=%s"), *LocalDataCachePath);
			return true;
		}
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Skipping local data cache path=%s due to an invalid path"), *LocalDataCachePath);
		bHasInvalidPathConfigurations = true;
	}

	// Zen config default
	FString ConfigDefaultPath;
	GConfig->GetString(ConfigSection, TEXT("DataPath"), ConfigDefaultPath, GEngineIni);
	if (!ConfigDefaultPath.IsEmpty())
	{
		ConfigDefaultPath.ReplaceInline(TEXT("%ENGINEVERSIONAGNOSTICINSTALLEDUSERDIR%"), *GetLocalZenRootPath());
		if (FString Path = ValidateDataPath(ConfigDefaultPath); !Path.IsEmpty())
		{
			DataPath = Path;
			bIsDefaultDataPath = true;
			UE_LOG(LogZenServiceInstance, Log, TEXT("Found Zen config default=%s"), *ConfigDefaultPath);
			return true;
		}
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Skipping Zen config default=%s due to an invalid path"), *ConfigDefaultPath);
		bHasInvalidPathConfigurations = true;
	}
	UE_LOG(LogZenServiceInstance, Warning, TEXT("Unable to determine a valid Zen data path"));
	return false;
}

static void
ReadUInt16FromConfig(const TCHAR* Section, const TCHAR* Key, uint16& Value, const FString& ConfigFile)
{
	int32 ValueInt32 = Value;
	GConfig->GetInt(Section, Key, ValueInt32, ConfigFile);
	Value = (uint16)ValueInt32;
}

static bool
IsLocalHost(const FString& Host)
{
	if (Host.Compare(FString(TEXT("localhost")), ESearchCase::IgnoreCase) == 0)
	{
		return true;
	}

	if (Host.Compare(FString(TEXT("127.0.0.1"))) == 0)
	{
		return true;
	}

	if (Host.Compare(FString(TEXT("[::1]"))) == 0)
	{
		return true;
	}

	return false;
}

static void
ApplyProcessLifetimeOverride(bool& bLimitProcessLifetime)
{
	FString LimitProcessLifetime = FPlatformMisc::GetEnvironmentVariable(TEXT("UE-ZenLimitProcessLifetime"));
	if (!LimitProcessLifetime.IsEmpty())
	{
		bLimitProcessLifetime = LimitProcessLifetime.ToBool();
	}
}

static void
PromptUserUnableToDetermineValidDataPath()
{
#if !IS_PROGRAM
	if (!FApp::IsUnattended() && !IsRunningCommandlet() && !GIsRunningUnattendedScript)
	{
		FString LogDirPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir());
		FText ZenInvalidDataPathPromptTitle = NSLOCTEXT("Zen", "Zen_InvalidDataPathPromptTitle", "No Valid Data Path Configuration");
		FText ZenInvalidDataPathPromptText = FText::Format(NSLOCTEXT("Zen", "Zen_InvalidDataPathPromptText", "Unreal Zen Storage Server can not determine a valid data path.\nPlease check the log in '{0}' for details.\nUpdate your configuration and restart."), FText::FromString(LogDirPath));
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ZenInvalidDataPathPromptText.ToString(), *ZenInvalidDataPathPromptTitle.ToString());
	}
	else
#endif
	{
		// Just log as there is no one to show a message
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Unreal Zen Storage Server is unable to determine a valid data path"));
	}
}

static void
PromptUserAboutInvalidValidDataPathConfiguration(const FString& UsedDataPath)
{
#if !IS_PROGRAM
	if (!FApp::IsUnattended() && !IsRunningCommandlet() && !GIsRunningUnattendedScript)
	{
		FString LogDirPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir());
		FText ZenInvalidValidDataPathConfigurationPromptTitle = NSLOCTEXT("Zen", "Zen_InvalidValidDataPathConfigurationPromptTitle", "Invalid Data Paths");
		FText ZenInvalidValidDataPathConfigurationPromptText = FText::Format(NSLOCTEXT("Zen", "Zen_InvalidValidDataPathConfigurationPromptText", "Unreal Zen Storage Server has detected invalid data path configuration.\nPlease check the log in '{0}' for details.\n\nFalling back to using '{1}' as data path."), FText::FromString(LogDirPath), FText::FromString(UsedDataPath));
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ZenInvalidValidDataPathConfigurationPromptText.ToString(), *ZenInvalidValidDataPathConfigurationPromptTitle.ToString());
	}
	else
#endif
	{
		// Just log as there is no one to show a message
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Unreal Zen Storage Server has detected invalid data path configuration. Falling back to '%s'"), *UsedDataPath);
	}
}

#if PLATFORM_WINDOWS
static void
PromptUserIsUsingGoogleDriveAsDataPath()
{
#if !IS_PROGRAM
	if (!FApp::IsUnattended() && !IsRunningCommandlet() && !GIsRunningUnattendedScript)
	{
		FString LogDirPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir());
		FText ZenInvalidDataPathPromptTitle = NSLOCTEXT("Zen", "Zen_GoogleDriveDataPathPromptTitle", "Using Google Drive as a data path");
		FText ZenInvalidDataPathPromptText = FText::Format(NSLOCTEXT("Zen", "Zen_GoogleDriveDataPathPromptText", "Unreal Zen Storage Server is configured to use Google Drive as a data path, this is highly inadvisable.\nPlease use a data path on a local physical drive.\nCheck the log in '{0}' for details.\nUpdate your configuration and restart."), FText::FromString(LogDirPath));
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ZenInvalidDataPathPromptText.ToString(), *ZenInvalidDataPathPromptTitle.ToString());
	}
	else
#endif
	{
		// Just log as there is no one to show a message
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Unreal Zen Storage Server is configured to use Google Drive as a data path, this is highly inadvisable. Please use a path on a local physical drive."));
	}
}
#endif // PLATFORM_WINDOWS

static void ReadCbField(FCbFieldView Field, UE::Zen::FZenSizeStats& OutValue)
{
	FCbObjectView ObjectView = Field.AsObjectView();
	OutValue.Disk = ObjectView["disk"].AsDouble();
	OutValue.Memory = ObjectView["memory"].AsDouble();
}

static void ReadCbField(FCbFieldView Field, UE::Zen::FZenCIDSizeStats& OutValue)
{
	FCbObjectView ObjectView = Field.AsObjectView();
	OutValue.Tiny = ObjectView["tiny"].AsInt64();
	OutValue.Small = ObjectView["small"].AsInt64();
	OutValue.Large = ObjectView["large"].AsInt64();
	OutValue.Total = ObjectView["total"].AsInt64();
}

static void ReadCbField(FCbFieldView Field, UE::Zen::FZenCIDStats& OutValue)
{
	FCbObjectView ObjectView = Field.AsObjectView();
	ReadCbField(ObjectView["size"], OutValue.Size);
}

static FServiceAutoLaunchSettings::EInstallMode ZenGetInstallModeFromString(const FString& InstallMode)
{
	if (InstallMode.IsEmpty() || FCString::Stricmp(*InstallMode, TEXT("auto")) == 0)
	{
		return FApp::IsEngineInstalled() ? FServiceAutoLaunchSettings::EInstallMode::Link : FServiceAutoLaunchSettings::EInstallMode::Copy;
	}
	else if (FCString::Stricmp(*InstallMode, TEXT("copy")) == 0)
	{
		return FServiceAutoLaunchSettings::EInstallMode::Copy;
	}
	else if (FCString::Stricmp(*InstallMode, TEXT("link")) == 0)
	{
		return FServiceAutoLaunchSettings::EInstallMode::Link;
	}
	else
	{
		checkf(false, TEXT("Invalid zenserver install mode: {InstallMode}"), *InstallMode);
		return FServiceAutoLaunchSettings::EInstallMode::Copy;
	}
}

static FString ZenGetInstallModeToString(FServiceAutoLaunchSettings::EInstallMode InstallMode)
{
	switch (InstallMode)
	{
	case FServiceAutoLaunchSettings::EInstallMode::Copy:
		return TEXT("copy");
	case FServiceAutoLaunchSettings::EInstallMode::Link:
		return TEXT("link");
	}
	checkf(false, TEXT("Invalid zenserver install mode: {%d}"), static_cast<int>(InstallMode));
	return "";
}

bool
FServiceSettings::ReadFromConfig()
{
	check(GConfig && GConfig->IsReadyForUse());
	const TCHAR* ConfigSection = TEXT("Zen");
	bool bAutoLaunch = true;
	GConfig->GetBool(ConfigSection, TEXT("AutoLaunch"), bAutoLaunch, GEngineIni);

	if (bAutoLaunch)
	{
		if (!TryApplyAutoLaunchOverride())
		{
			// AutoLaunch settings
			const TCHAR* AutoLaunchConfigSection = TEXT("Zen.AutoLaunch");
			SettingsVariant.Emplace<FServiceAutoLaunchSettings>();
			FServiceAutoLaunchSettings& AutoLaunchSettings = SettingsVariant.Get<FServiceAutoLaunchSettings>();

			bool bHasInvalidPathConfigurations = false;
			if (!DetermineDataPath(AutoLaunchConfigSection, AutoLaunchSettings.DataPath, bHasInvalidPathConfigurations, AutoLaunchSettings.bIsDefaultDataPath))
			{
				PromptUserUnableToDetermineValidDataPath();
				return false;
			}
			else if (bHasInvalidPathConfigurations)
			{
				PromptUserAboutInvalidValidDataPathConfiguration(AutoLaunchSettings.DataPath);
			}

#if PLATFORM_WINDOWS
			{
				int32 DriveEnd = 0;
				if (AutoLaunchSettings.DataPath.FindChar(':', DriveEnd))
				{
					FString DrivePath = AutoLaunchSettings.DataPath.Left(DriveEnd + 1);
					TCHAR VolumeName[128];

					BOOL OK = GetVolumeInformation(
						*DrivePath,
						VolumeName,
						127,
						NULL,
						NULL,
						NULL,
						NULL,
						NULL);

					if (OK)
					{
						VolumeName[127] = 0;
						if (FString(VolumeName) == TEXT("Google Drive"))
						{
							PromptUserIsUsingGoogleDriveAsDataPath();
						}
					}
				}
			}
#endif // PLATFORM_WINDOWS

			GConfig->GetString(AutoLaunchConfigSection, TEXT("ExtraArgs"), AutoLaunchSettings.ExtraArgs, GEngineIni);

			ReadUInt16FromConfig(AutoLaunchConfigSection, TEXT("DesiredPort"), AutoLaunchSettings.DesiredPort, GEngineIni);
			GConfig->GetBool(AutoLaunchConfigSection, TEXT("ShowConsole"), AutoLaunchSettings.bShowConsole, GEngineIni);
			GConfig->GetBool(AutoLaunchConfigSection, TEXT("LimitProcessLifetime"), AutoLaunchSettings.bLimitProcessLifetime, GEngineIni);
			ApplyProcessLifetimeOverride(AutoLaunchSettings.bLimitProcessLifetime);
			GConfig->GetBool(AutoLaunchConfigSection, TEXT("AllowRemoteNetworkService"), AutoLaunchSettings.bAllowRemoteNetworkService, GEngineIni);
			FString InstallMode;
			if (GConfig->GetString(AutoLaunchConfigSection, TEXT("InstallMode"), InstallMode, GEngineIni))
			{
				AutoLaunchSettings.InstallMode = ZenGetInstallModeFromString(InstallMode);
			}
			EnsureEditorSettingsConfigLoaded();
			GConfig->GetBool(TEXT("/Script/UnrealEd.CrashReportsPrivacySettings"), TEXT("bSendUnattendedBugReports"), AutoLaunchSettings.bSendUnattendedBugReports, GEditorSettingsIni);
		}
	}
	else
	{
		// ConnectExisting settings
		const TCHAR* ConnectExistingConfigSection = TEXT("Zen.ConnectExisting");
		SettingsVariant.Emplace<FServiceConnectSettings>();
		FServiceConnectSettings& ConnectExistingSettings = SettingsVariant.Get<FServiceConnectSettings>();

		GConfig->GetString(ConnectExistingConfigSection, TEXT("HostName"), ConnectExistingSettings.HostName, GEngineIni);
		ReadUInt16FromConfig(ConnectExistingConfigSection, TEXT("Port"), ConnectExistingSettings.Port, GEngineIni);
	}
	return true;
}

bool
FServiceSettings::ReadFromCompactBinary(FCbFieldView Field)
{
	if (bool bAutoLaunchValue = Field["bAutoLaunch"].AsBool())
	{
		if (!TryApplyAutoLaunchOverride())
		{
			SettingsVariant.Emplace<FServiceAutoLaunchSettings>();
			FServiceAutoLaunchSettings& AutoLaunchSettings = SettingsVariant.Get<FServiceAutoLaunchSettings>();

			if (FCbObjectView AutoLaunchSettingsObject = Field["AutoLaunchSettings"].AsObjectView())
			{
				AutoLaunchSettings.DataPath = FString(AutoLaunchSettingsObject["DataPath"].AsString());
				AutoLaunchSettings.ExtraArgs = FString(AutoLaunchSettingsObject["ExtraArgs"].AsString());
				AutoLaunchSettings.DesiredPort = AutoLaunchSettingsObject["DesiredPort"].AsInt16();
				AutoLaunchSettings.bShowConsole = AutoLaunchSettingsObject["ShowConsole"].AsBool();
				AutoLaunchSettings.bIsDefaultDataPath = AutoLaunchSettingsObject["IsDefaultDataPath"].AsBool();
				AutoLaunchSettings.bLimitProcessLifetime = AutoLaunchSettingsObject["LimitProcessLifetime"].AsBool();
				ApplyProcessLifetimeOverride(AutoLaunchSettings.bLimitProcessLifetime);
				AutoLaunchSettings.bAllowRemoteNetworkService = AutoLaunchSettingsObject["AllowRemoteNetworkService"].AsBool();
				AutoLaunchSettings.bSendUnattendedBugReports = AutoLaunchSettingsObject["SendUnattendedBugReports"].AsBool();
				AutoLaunchSettings.bIsDefaultSharedRunContext = AutoLaunchSettingsObject["IsDefaultSharedRunContext"].AsBool(AutoLaunchSettings.bIsDefaultSharedRunContext);
				AutoLaunchSettings.InstallMode = ZenGetInstallModeFromString(FString(AutoLaunchSettingsObject["InstallMode"].AsString()));
			}
		}
	}
	else
	{
		SettingsVariant.Emplace<FServiceConnectSettings>();
		FServiceConnectSettings& ConnectExistingSettings = SettingsVariant.Get<FServiceConnectSettings>();

		if (FCbObjectView ConnectExistingSettingsObject = Field["ConnectExistingSettings"].AsObjectView())
		{
			ConnectExistingSettings.HostName = FString(ConnectExistingSettingsObject["HostName"].AsString());
			ConnectExistingSettings.Port = ConnectExistingSettingsObject["Port"].AsInt16();
		}
	}
	return true;
}

bool
FServiceSettings::ReadFromURL(FStringView InstanceURL)
{
	SettingsVariant.Emplace<FServiceConnectSettings>();
	FServiceConnectSettings& ConnectExistingSettings = SettingsVariant.Get<FServiceConnectSettings>();

	if (InstanceURL.StartsWith(TEXT("http://")))
	{
		InstanceURL.RightChopInline(7);
	}

	int32 PortDelimIndex = INDEX_NONE;
	InstanceURL.FindLastChar(TEXT(':'), PortDelimIndex);
	if (PortDelimIndex != INDEX_NONE)
	{
		ConnectExistingSettings.HostName = InstanceURL.Left(PortDelimIndex);
		LexFromString(ConnectExistingSettings.Port, InstanceURL.RightChop(PortDelimIndex + 1));
	}
	else
	{
		ConnectExistingSettings.HostName = InstanceURL;
		ConnectExistingSettings.Port = 8558;
	}
	return true;
}

void
FServiceSettings::WriteToCompactBinary(FCbWriter& Writer) const
{
	bool bAutoLaunch = IsAutoLaunch();
	Writer << "bAutoLaunch" << bAutoLaunch;
	if (bAutoLaunch)
	{
		const FServiceAutoLaunchSettings& AutoLaunchSettings = SettingsVariant.Get<FServiceAutoLaunchSettings>();
		Writer.BeginObject("AutoLaunchSettings");
		Writer << "DataPath" << AutoLaunchSettings.DataPath;
		Writer << "ExtraArgs" <<AutoLaunchSettings.ExtraArgs;
		Writer << "DesiredPort" << AutoLaunchSettings.DesiredPort;
		Writer << "ShowConsole" << AutoLaunchSettings.bShowConsole;
		Writer << "IsDefaultDataPath" << AutoLaunchSettings.bIsDefaultDataPath;
		Writer << "LimitProcessLifetime" << AutoLaunchSettings.bLimitProcessLifetime;
		Writer << "AllowRemoteNetworkService" << AutoLaunchSettings.bAllowRemoteNetworkService;
		Writer << "SendUnattendedBugReports" << AutoLaunchSettings.bSendUnattendedBugReports;
		Writer << "IsDefaultSharedRunContext" << AutoLaunchSettings.bIsDefaultSharedRunContext;
		Writer << "InstallMode" << ZenGetInstallModeToString(AutoLaunchSettings.InstallMode);
		Writer.EndObject();
	}
	else
	{
		const FServiceConnectSettings& ConnectExistingSettings = SettingsVariant.Get<FServiceConnectSettings>();
		Writer.BeginObject("ConnectExistingSettings");
		Writer << "HostName" << ConnectExistingSettings.HostName;
		Writer << "Port" << ConnectExistingSettings.Port;
		Writer.EndObject();
	}
}

bool
FServiceSettings::TryApplyAutoLaunchOverride()
{
#if ALLOW_SETTINGS_OVERRIDE_FROM_COMMANDLINE
	if (FParse::Param(FCommandLine::Get(), TEXT("NoZenAutoLaunch")))
	{
		SettingsVariant.Emplace<FServiceConnectSettings>();
		FServiceConnectSettings& ConnectExistingSettings = SettingsVariant.Get<FServiceConnectSettings>();
		ConnectExistingSettings.HostName = TEXT("[::1]");
		ConnectExistingSettings.Port = 8558;
		return true;
	}

	FString Host;
	if  (FParse::Value(FCommandLine::Get(), TEXT("-NoZenAutoLaunch="), Host))
	{
		SettingsVariant.Emplace<FServiceConnectSettings>();
		FServiceConnectSettings& ConnectExistingSettings = SettingsVariant.Get<FServiceConnectSettings>();

		int32 PortDelimIndex = INDEX_NONE;
		if (Host.FindChar(TEXT(':'), PortDelimIndex))
		{
			ConnectExistingSettings.HostName = Host.Left(PortDelimIndex);
			LexFromString(ConnectExistingSettings.Port, Host.RightChop(PortDelimIndex + 1));
		}
		else
		{
			ConnectExistingSettings.HostName = Host;
			ConnectExistingSettings.Port = 8558;
		}

		return true;
	}
#endif
	return false;
}

#if UE_WITH_ZEN

uint16 FZenServiceInstance::AutoLaunchedPort = 0;
uint32 FZenServiceInstance::AutoLaunchedPid = 0;

static bool
IsZenProcessUsingEffectivePort(uint16 EffectiveListenPort)
{
	ZenSharedEvent ShutDownEvent(ZenSharedEvent::GetShutdownEventName(EffectiveListenPort));
	return ShutDownEvent.Exists();
}

static bool
RequestZenShutdownOnEffectivePort(uint16 EffectiveListenPort)
{
	ZenSharedEvent ShutDownEvent(ZenSharedEvent::GetShutdownEventName(EffectiveListenPort));
	if (!ShutDownEvent.Open())
	{
		return false;
	}
	if (!ShutDownEvent.Set())
	{
		return false;
	}
	return true;
}

static bool ShutdownZenServerProcess(int Pid, double MaximumWaitDurationSeconds = 25.0)
{
	const ZenServerState ServerState(/* ReadOnly */true);
	const ZenServerState::ZenServerEntry* Entry = ServerState.LookupByPid(Pid);
	if (Entry)
	{
		uint16 EffectivePort = Entry->EffectiveListenPort.load(std::memory_order_relaxed);
		UE_LOG(LogZenServiceInstance, Display, TEXT("Requesting shut down of zenserver process %d runnning on effective port %u"), Pid, EffectivePort);
		if (RequestZenShutdownOnEffectivePort(EffectivePort))
		{
			uint64 ZenShutdownWaitStartTime = FPlatformTime::Cycles64();
			while (ZenServerState::IsProcessRunning(Pid))
			{
				double ZenShutdownWaitDuration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - ZenShutdownWaitStartTime);
				if (ZenShutdownWaitDuration < MaximumWaitDurationSeconds)
				{
					FPlatformProcess::Sleep(0.01f);
				}
				else
				{
					UE_LOG(LogZenServiceInstance, Warning, TEXT("Timed out waiting for shut down of running service with pid %d"), Pid);
					break;
				}
			}
		}
	}
	if (ZenServerState::IsProcessRunning(Pid))
	{
		UE_LOG(LogZenServiceInstance, Display, TEXT("Attempting termination of zenserver process with pid %d"), Pid);
		if (!ZenServerState::Terminate(Pid))
		{
			if (ZenServerState::IsProcessRunning(Pid))
			{
				UE_LOG(LogZenServiceInstance, Warning, TEXT("Failed to terminate zenserver process with pid %d"), Pid);
				return false;
			}
		}
	}
	UE_LOG(LogZenServiceInstance, Display, TEXT("Successfully shut down zenserver process with pid %d"), Pid);
	return true;
}

static bool ShutDownZenServerProcessExecutable(const FString& ExecutablePath, double MaximumWaitDurationSeconds = 25.0)
{
	uint64 ZenShutdownWaitStartTime = FPlatformTime::Cycles64();
	uint32_t Pid = 0;
	while (ZenServerState::FindRunningProcessId(*ExecutablePath, &Pid))
	{
		UE_LOG(LogZenServiceInstance, Display, TEXT("Attempting to shut down of zenserver executable '%s' process with pid %d"), *ExecutablePath, Pid);
		double ZenShutdownWaitDuration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - ZenShutdownWaitStartTime);
		if (ShutdownZenServerProcess(Pid, MaximumWaitDurationSeconds - ZenShutdownWaitDuration))
		{
			return true;
		}
		else
		{
			UE_LOG(LogZenServiceInstance, Warning, TEXT("Failed to shut down zenserver executable '%s' process with pid %d"), *ExecutablePath, Pid);
			return false;
		}
	}
	return true;
}

static bool ShutDownZenServerProcessLockingDataDir(const FString& DataPath, double MaximumWaitDurationSeconds = 25.0)
{
	const FString LockFilePath = FPaths::Combine(DataPath, TEXT(".lock"));

	uint64 ZenShutdownWaitStartTime = FPlatformTime::Cycles64();
	if (!ZenLockFileData::IsLockFileLocked(*LockFilePath, true))
	{
		UE_LOG(LogZenServiceInstance, Display, TEXT("Lock file '%s' is not active, nothing to do"), *LockFilePath);
		return true;
	}
	ZenLockFileData LockFileState = ZenLockFileData::ReadCbLockFile(*LockFilePath);
	if (!LockFileState.IsValid)
	{
		while (true)
		{
			if (!ZenLockFileData::IsLockFileLocked(*LockFilePath, true))
			{
				return true;
			}
			uint32_t Pid = 0;
			if (!ZenServerState::FindRunningProcessId(*GetServiceExecutableName(), &Pid))
			{
				if (!ZenLockFileData::IsLockFileLocked(*LockFilePath, true))
				{
					UE_LOG(LogZenServiceInstance, Display, TEXT("Lock file '%s' is no longer active, nothing to do"), *LockFilePath);
					return true;
				}
				UE_LOG(LogZenServiceInstance, Warning, TEXT("Failed to find zenserver process locking file '%s'"), *LockFilePath);
				return false;
			}
			UE_LOG(LogZenServiceInstance, Warning, TEXT("Found locked but invalid lock file at '%s', attempting shut down of zenserver process with pid %d"), *LockFilePath, Pid);
			double ZenShutdownWaitDuration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - ZenShutdownWaitStartTime);
			if (!ShutdownZenServerProcess(Pid, MaximumWaitDurationSeconds - ZenShutdownWaitDuration))
			{
				break;
			}
		}
		if (!ZenLockFileData::IsLockFileLocked(*LockFilePath))
		{
			UE_LOG(LogZenServiceInstance, Display, TEXT("Successfully shut down zenserver using lock file '%s'"), *LockFilePath);
			return true;
		}
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Failed to shut down zenserver process locking file '%s'"), *LockFilePath);
		return false;
	}

	uint16 EffectivePort = LockFileState.EffectivePort;

	const ZenServerState ServerState(/* ReadOnly */true);
	const ZenServerState::ZenServerEntry* Entry = ServerState.LookupByEffectiveListenPort(EffectivePort);
	if (Entry)
	{
		UE_LOG(LogZenServiceInstance, Display, TEXT("Requesting shut down of zenserver process using lock file '%s' with effective port %d"), *LockFilePath, EffectivePort);
		if (RequestZenShutdownOnEffectivePort(EffectivePort))
		{
			while (ZenLockFileData::IsLockFileLocked(*LockFilePath, true))
			{
				double ZenShutdownWaitDuration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - ZenShutdownWaitStartTime);
				if (ZenShutdownWaitDuration < MaximumWaitDurationSeconds)
				{
					FPlatformProcess::Sleep(0.01f);
				}
				else
				{
					UE_LOG(LogZenServiceInstance, Warning, TEXT("Timed out waiting for shut down of zensever process using lock file '%s' with effective port %u"), *LockFilePath, EffectivePort);
					break;
				}
			}
			if (!ZenLockFileData::IsLockFileLocked(*LockFilePath, true))
			{
				UE_LOG(LogZenServiceInstance, Display, TEXT("Successfully shut down zenserver process using lock file '%s' with effective port %u"), *LockFilePath, EffectivePort);
				return true;
			}
		}
	}

	while (true)
	{
		if (!ZenLockFileData::IsLockFileLocked(*LockFilePath, true))
		{
			return true;
		}
		uint32_t Pid = 0;
		if (!ZenServerState::FindRunningProcessId(*GetServiceExecutableName(), &Pid))
		{
			if (!ZenLockFileData::IsLockFileLocked(*LockFilePath, true))
			{
				UE_LOG(LogZenServiceInstance, Display, TEXT("Lock file '%s' is no longer active, nothing to do"), *LockFilePath);
				return true;
			}
			UE_LOG(LogZenServiceInstance, Warning, TEXT("Failed to find zenserver process locking file '%s'"), *LockFilePath);
			return false;
		}
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Found locked but invalid lock file at '%s', attempting shut down of zenserver process with pid %d"), *LockFilePath, Pid);
		double ZenShutdownWaitDuration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - ZenShutdownWaitStartTime);
		if (!ShutdownZenServerProcess(Pid, MaximumWaitDurationSeconds - ZenShutdownWaitDuration))
		{
			break;
		}
	}

	if (!ZenLockFileData::IsLockFileLocked(*LockFilePath))
	{
		UE_LOG(LogZenServiceInstance, Display, TEXT("Successfully shut down zenserver using lock file '%s'"), *LockFilePath);
		return true;
	}
	UE_LOG(LogZenServiceInstance, Warning, TEXT("Failed to shut down zenserver process locking file '%s'"), *LockFilePath);
	return false;
}

static bool
IsZenProcessUsingDataDir(const TCHAR* LockFilePath, ZenLockFileData* OutLockFileData)
{
	if (ZenLockFileData::IsLockFileLocked(LockFilePath, true))
	{
		if (OutLockFileData)
		{
			// If an instance is running with this data path, check if we can use it and what port it is on
			*OutLockFileData = ZenLockFileData::ReadCbLockFile(LockFilePath);
		}
		return true;
	}
	return false;
}

static FString
DetermineCmdLineWithoutTransientComponents(const FServiceAutoLaunchSettings& InSettings, int16 OverrideDesiredPort)
{
	FString PlatformDataPath(InSettings.DataPath);
	FPaths::MakePlatformFilename(PlatformDataPath);

	FString Parms;
	Parms.Appendf(TEXT("--port %d --data-dir \"%s\""),
		OverrideDesiredPort,
		*PlatformDataPath);

	if (!InSettings.ExtraArgs.IsEmpty())
	{
		Parms.AppendChar(TEXT(' '));
		Parms.Append(InSettings.ExtraArgs);
	}

	FString LogCommandLineOverrideValue;
	if (FParse::Value(FCommandLine::Get(), TEXT("ZenLogPath="), LogCommandLineOverrideValue))
	{
		if (!LogCommandLineOverrideValue.IsEmpty())
		{
			Parms.Appendf(TEXT(" --abslog \"%s\""),
				*FPaths::ConvertRelativePathToFull(LogCommandLineOverrideValue));
		}
	}

	FString CfgCommandLineOverrideValue;
	if (FParse::Value(FCommandLine::Get(), TEXT("ZenCfgPath="), CfgCommandLineOverrideValue))
	{
		if (!CfgCommandLineOverrideValue.IsEmpty())
		{
			Parms.Appendf(TEXT(" --config \"%s\""),
				*FPaths::ConvertRelativePathToFull(CfgCommandLineOverrideValue));
		}
	}

	if (!InSettings.bSendUnattendedBugReports)
	{
		Parms.Append(TEXT(" --no-sentry"));
	}

	if (!InSettings.bAllowRemoteNetworkService)
	{
		Parms.Append(TEXT(" --http-forceloopback"));
	}

	return Parms;
}

bool
Private::IsLocalAutoLaunched(FStringView InstanceURL)
{
	if (!InstanceURL.IsEmpty() && !InstanceURL.Equals(TEXT("<DefaultInstance>")))
	{
		FString TempURL(InstanceURL);
		return IsLocalHost(TempURL);
	}
	return true;
}

bool
Private::GetLocalDataCachePathOverride(FString& OutDataPath)
{
	const TCHAR* AutoLaunchConfigSection = TEXT("Zen.AutoLaunch");
	FString DataPath;
	DetermineLocalDataCachePath(AutoLaunchConfigSection, DataPath);
	if (DataPath.IsEmpty())
	{
		return false;
	}
	OutDataPath = DataPath;
	return true;
}

bool
TryGetLocalServiceRunContext(FZenLocalServiceRunContext& OutContext)
{
	return OutContext.ReadFromJsonFile(*GetServiceRunContextPath());
}

bool
FZenLocalServiceRunContext::ReadFromJson(FJsonObject& JsonObject)
{
	Executable = JsonObject.Values.FindRef(TEXT("Executable"))->AsString();
	CommandlineArguments = JsonObject.Values.FindRef(TEXT("CommandlineArguments"))->AsString();
	WorkingDirectory = JsonObject.Values.FindRef(TEXT("WorkingDirectory"))->AsString();
	DataPath = JsonObject.Values.FindRef(TEXT("DataPath"))->AsString();
	bShowConsole = JsonObject.Values.FindRef(TEXT("ShowConsole"))->AsBool();
	return true;
}

void
FZenLocalServiceRunContext::WriteToJson(TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>& Writer) const
{
	Writer.WriteValue(TEXT("Executable"), Executable);
	Writer.WriteValue(TEXT("CommandlineArguments"), CommandlineArguments);
	Writer.WriteValue(TEXT("WorkingDirectory"), WorkingDirectory);
	Writer.WriteValue(TEXT("DataPath"), DataPath);
	Writer.WriteValue(TEXT("ShowConsole"), bShowConsole);
}

bool
FZenLocalServiceRunContext::ReadFromJsonFile(const TCHAR* Filename)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, Filename))
	{
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	return ReadFromJson(*JsonObject);
}

bool
FZenLocalServiceRunContext::WriteToJsonFile(const TCHAR* Filename) const
{
	FString JsonTcharText;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonTcharText);
	Writer->WriteObjectStart();
	WriteToJson(*Writer);
	Writer->WriteObjectEnd();
	Writer->Close();

	if (!FFileHelper::SaveStringToFile(JsonTcharText, Filename))
	{
		return false;
	}

	return true;
}

bool
IsLocalServiceRunning(const TCHAR* DataPath, uint16* OutEffectivePort)
{
	const FString LockFilePath = FPaths::Combine(DataPath, TEXT(".lock"));
	ZenLockFileData LockFileState;
	if (IsZenProcessUsingDataDir(*LockFilePath, &LockFileState))
	{
		if (OutEffectivePort != nullptr && LockFileState.IsValid && LockFileState.IsReady)
		{
			*OutEffectivePort = LockFileState.EffectivePort;
		}
		return true;
	}
	return false;
}

FProcHandle
StartLocalService(const FZenLocalServiceRunContext& Context, const TCHAR* TransientArgs)
{
	FString Parms = Context.GetCommandlineArguments();
	if (TransientArgs)
	{
		Parms.Appendf(TEXT(" %s"), TransientArgs);
	}

	UE_LOG(LogZenServiceInstance, Display, TEXT("Launching executable '%s', working dir '%s', data dir '%s', args '%s'"), *Context.GetExecutable(), *Context.GetWorkingDirectory(), *Context.GetDataPath(), *Parms);

	FProcHandle Proc;
#if PLATFORM_WINDOWS
	FString PlatformExecutable = Context.GetExecutable();
	FPaths::MakePlatformFilename(PlatformExecutable);
	FString PlatformWorkingDirectory = Context.GetWorkingDirectory();
	FPaths::MakePlatformFilename(PlatformWorkingDirectory);
	{
		// We could switch to FPlatformProcess::CreateProc for Windows as well if we are able to add the CREATE_BREAKAWAY_FROM_JOB flag
		// as that is needed on CI to stop Horde from terminating the zenserver process
		STARTUPINFO StartupInfo = {
			sizeof(STARTUPINFO),
			NULL, NULL, NULL,
			(::DWORD)CW_USEDEFAULT,
			(::DWORD)CW_USEDEFAULT,
			(::DWORD)CW_USEDEFAULT,
			(::DWORD)CW_USEDEFAULT,
			(::DWORD)0, (::DWORD)0, (::DWORD)0,
			(::DWORD)STARTF_USESHOWWINDOW,
			(::WORD)(Context.GetShowConsole() ? SW_SHOWMINNOACTIVE : SW_HIDE),
			0, NULL,
			HANDLE(nullptr),
			HANDLE(nullptr),
			HANDLE(nullptr)
		};

		FString CommandLine = FString::Printf(TEXT("\"%s\" %s"), *PlatformExecutable, *Parms);
		::DWORD CreationFlagsArray[] = {
			NORMAL_PRIORITY_CLASS | DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB, // Try with the breakaway flag first
			NORMAL_PRIORITY_CLASS | DETACHED_PROCESS // If that fails (access denied), try without the breakaway flag next
		};

		for (::DWORD CreationFlags : CreationFlagsArray)
		{
			PROCESS_INFORMATION ProcInfo;
			if (CreateProcess(NULL, CommandLine.GetCharArray().GetData(), nullptr, nullptr, false, CreationFlags, nullptr, PlatformWorkingDirectory.GetCharArray().GetData(), &StartupInfo, &ProcInfo))
			{
				::CloseHandle(ProcInfo.hThread);
				Proc = FProcHandle(ProcInfo.hProcess);
				break;
			}
		}

		if (!Proc.IsValid())
		{
			UE_LOG(LogZenServiceInstance, Warning, TEXT("Failed launching %s status: %d."), *CommandLine, GetLastError());
		}
	}
#else
	{
		bool bLaunchDetached = true;
		bool bLaunchHidden = true;
		bool bLaunchReallyHidden = !Context.GetShowConsole();
		uint32* OutProcessID = nullptr;
		int32 PriorityModifier = 0;
		void* PipeWriteChild = nullptr;
		void* PipeReadChild = nullptr;
		Proc = FPlatformProcess::CreateProc(
			*Context.GetExecutable(),
			*Parms,
			bLaunchDetached,
			bLaunchHidden,
			bLaunchReallyHidden,
			OutProcessID,
			PriorityModifier,
			*Context.GetWorkingDirectory(),
			PipeWriteChild,
			PipeReadChild);
	}
#endif
	return Proc;
}

bool
StopLocalService(const TCHAR* DataPath, double MaximumWaitDurationSeconds)
{
	const FString LockFilePath = FPaths::Combine(DataPath, TEXT(".lock"));
	ZenLockFileData LockFileState;
	if (ZenLockFileData::IsLockFileLocked(*LockFilePath, true))
	{
		return ShutDownZenServerProcessLockingDataDir(DataPath, MaximumWaitDurationSeconds);
	}
	return true;
}

FString
GetLocalServiceInstallPath()
{
	if (FZenServiceLink Link = FZenServiceLink::Read(GetServiceLinkPath()); Link)
	{
		return Link.ServicePath;
	}
	else
	{
		return GetServiceCopyInstallPath();
	}
}

FString
GetLocalInstallUtilityPath()
{
	if (FZenServiceLink Link = FZenServiceLink::Read(GetServiceLinkPath()); Link)
	{
		return Link.UtilityPath;
	}
	else
	{
		return GetUtilityCopyInstallPath();
	}
}

FString
GetLocalServiceInstallVersion(bool bDetailed)
{
	IFileManager& FileManager = IFileManager::Get();
	if (FZenServiceLink Link = FZenServiceLink::Read(GetServiceLinkPath()); Link)
	{
		if (!FileManager.FileExists(*Link.ServicePath))
		{
			return FZenVersion().ToString(bDetailed);
		}
		return Link.Version.ToString(bDetailed);
	}
	else
	{
		const FString ServicePath = GetServiceCopyInstallPath();

		if (!FileManager.FileExists(*ServicePath))
		{
			return FZenVersion().ToString(bDetailed);
		}

		FZenVersion InstallVersion = GetZenVersion(GetUtilityCopyInstallPath(), ServicePath, GetInstallVersionCachePath());
		return InstallVersion.ToString(bDetailed);
	}
}

static bool GIsDefaultServicePresent = false;

FZenServiceInstance& GetDefaultServiceInstance()
{
	static FZenServiceInstance DefaultServiceInstance;
	GIsDefaultServicePresent = true;
	return DefaultServiceInstance;
}

bool IsDefaultServicePresent()
{
	return GIsDefaultServicePresent;
}

FScopeZenService::FScopeZenService()
	: FScopeZenService(FStringView())
{
}

FScopeZenService::FScopeZenService(FStringView InstanceURL)
{
	if (!InstanceURL.IsEmpty() && !InstanceURL.Equals(TEXT("<DefaultInstance>")))
	{
		UniqueNonDefaultInstance = MakeUnique<FZenServiceInstance>(InstanceURL);
		ServiceInstance = UniqueNonDefaultInstance.Get();
	}
	else
	{
		ServiceInstance = &GetDefaultServiceInstance();
	}
}

FScopeZenService::FScopeZenService(FServiceSettings&& InSettings)
{
	UniqueNonDefaultInstance = MakeUnique<FZenServiceInstance>(MoveTemp(InSettings));
	ServiceInstance = UniqueNonDefaultInstance.Get();
}

FScopeZenService::~FScopeZenService()
{}

FZenServiceInstance::FZenServiceInstance()
: FZenServiceInstance(FStringView())
{
}

FZenServiceInstance::FZenServiceInstance(FStringView InstanceURL)
{
	if (InstanceURL.IsEmpty())
	{
		Settings.ReadFromConfig();
		if (Settings.IsAutoLaunch())
		{
			// Ensure that the zen data path is inherited by subprocesses
			FPlatformMisc::SetEnvironmentVar(TEXT("UE-ZenSubprocessDataPath"), *Settings.SettingsVariant.Get<FServiceAutoLaunchSettings>().DataPath);
		}
	}
	else
	{
		Settings.ReadFromURL(InstanceURL);
	}

	Initialize();
}

FZenServiceInstance::FZenServiceInstance(FServiceSettings&& InSettings)
: Settings(MoveTemp(InSettings))
{
	Initialize();
}

FZenServiceInstance::~FZenServiceInstance()
{
}

const FString FZenServiceInstance::GetPath() const
{
	if (Settings.IsAutoLaunch())
	{
		return Settings.SettingsVariant.Get<FServiceAutoLaunchSettings>().DataPath;
	}
	return GetURL();
}

bool
FZenServiceInstance::IsServiceRunning()
{
	return !Settings.IsAutoLaunch() || bHasLaunchedLocal;
}

bool 
FZenServiceInstance::IsServiceReady()
{
	uint32 Attempt = 0;
	while (IsServiceRunning())
	{
		TStringBuilder<128> ZenDomain;
		ZenDomain << HostName << TEXT(":") << Port;
		Zen::FZenHttpRequest Request(ZenDomain.ToString(), false);
		Zen::FZenHttpRequest::Result Result = Request.PerformBlockingDownload(TEXTVIEW("health/ready"), nullptr, Zen::EContentType::Text);
		
		if (Result == Zen::FZenHttpRequest::Result::Success && Zen::IsSuccessCode(Request.GetResponseCode()))
		{
			UE_LOG(LogZenServiceInstance, Display, TEXT("Unreal Zen Storage Server HTTP service at %s status: %s."), ZenDomain.ToString(), *Request.GetResponseAsString());
			return true;
		}

		if (IsServiceRunningLocally())
		{
			if (Attempt > 4)
			{
				UE_LOG(LogZenServiceInstance, Warning, TEXT("Unable to reach Unreal Zen Storage Server HTTP service at %s. Status: %d . Response: %s"), ZenDomain.ToString(), Request.GetResponseCode(), *Request.GetResponseAsString());
				break;
			}
		}
		else
		{
			UE_LOG(LogZenServiceInstance, Display, TEXT("Unable to reach Unreal Zen Storage Server HTTP service at %s. Status: %d . Response: %s"), ZenDomain.ToString(), Request.GetResponseCode(), *Request.GetResponseAsString());
			break;
		}
		Attempt++;
	}
	return false;
}

bool 
FZenServiceInstance::TryRecovery()
{
	if (!bHasLaunchedLocal)
	{
		return false;
	}

	const FString ExecutablePath = GetLocalServiceInstallPath();
	const FString ExecutionContextFilePath = GetServiceRunContextPath();

	static std::atomic<int64> LastRecoveryTicks;
	static bool bLastRecoveryResult = false;
	const FTimespan MaximumWaitForHealth = FTimespan::FromSeconds(30);
	const FTimespan MinimumDurationSinceLastRecovery = FTimespan::FromMinutes(2);

	FTimespan TimespanSinceLastRecovery = FDateTime::UtcNow() - FDateTime(LastRecoveryTicks.load(std::memory_order_relaxed));

	if (TimespanSinceLastRecovery > MinimumDurationSinceLastRecovery)
	{
		FSystemWideCriticalSection RecoveryCriticalSection(TEXT("ZenServerLaunch"), MaximumWaitForHealth);
		if (!RecoveryCriticalSection.IsValid())
		{
			// A recovery is already in progress but did not complete in time, we assume we failed and let recovery continue on a different thread
			return false;
		}

		// We test if the service is healthy as a different process might already have triggered a recovery
		bLastRecoveryResult = IsServiceReady();
		if (bLastRecoveryResult)
		{
			UE_LOG(LogZenServiceInstance, Display, TEXT("Local ZenServer status: Healthy. Skipping recovery"));
		}
		else
		{
			// Update timespan since it may have changed since we waited to enter the crit section
			TimespanSinceLastRecovery = FDateTime::UtcNow() - FDateTime(LastRecoveryTicks.load(std::memory_order_relaxed));
			if (TimespanSinceLastRecovery > MinimumDurationSinceLastRecovery)
			{
				UE_LOG(LogZenServiceInstance, Display, TEXT("Local ZenServer recovery being attempted..."));

				bool bShutdownExistingInstance = true;
				{
					const ZenServerState ServerState(/* ReadOnly */true);
					const ZenServerState::ZenServerEntry* Entry = ServerState.LookupByEffectiveListenPort(Port);
					if (Entry)
					{
						if (Entry->Pid.load(std::memory_order_relaxed) != AutoLaunchedPid)
						{
							// The running process pid is not the same as the one we launched.  The process was relaunched elsewhere. Avoid shutting it down again.
							bShutdownExistingInstance = false;
						}
					}
				}
				if (bShutdownExistingInstance && !ShutdownZenServerProcess((int)AutoLaunchedPid))	// !ShutdownRunningServiceUsingEffectivePort(Port))
				{
					return false;
				}

				AutoLaunch(Settings.SettingsVariant.Get<FServiceAutoLaunchSettings>(), ExecutablePath, ExecutionContextFilePath, HostName, Port);

				FDateTime StartedWaitingForHealth = FDateTime::UtcNow();
				bLastRecoveryResult = IsServiceReady();
				while (!bLastRecoveryResult)
				{
					FTimespan WaitForHealth = FDateTime::UtcNow() - StartedWaitingForHealth;
					if (WaitForHealth > MaximumWaitForHealth)
					{
						UE_LOG(LogZenServiceInstance, Warning, TEXT("Local ZenServer recovery timed out waiting for service to become healthy"));
						break;
					}

					FPlatformProcess::Sleep(0.5f);
					if (!IsZenProcessUsingEffectivePort(Port))
					{
						AutoLaunch(Settings.SettingsVariant.Get<FServiceAutoLaunchSettings>(), ExecutablePath, ExecutionContextFilePath, HostName, Port);
					}
					bLastRecoveryResult = IsServiceReady();
				}
				LastRecoveryTicks.store(FDateTime::UtcNow().GetTicks(), std::memory_order_relaxed);
				UE_LOG(LogZenServiceInstance, Display, TEXT("Local ZenServer recovery finished."));
				if (bLastRecoveryResult)
				{
					UE_LOG(LogZenServiceInstance, Display, TEXT("Local ZenServer post recovery status: Healthy"));
				}
				else
				{
					UE_LOG(LogZenServiceInstance, Display, TEXT("Local ZenServer post recovery status: NOT healthy"));
				}
			}
		}
	}

	return bLastRecoveryResult;
}

bool
FZenServiceInstance::AddSponsorProcessIDs(TArrayView<uint32> SponsorProcessIDs)
{
	ZenServerState State(/*bReadOnly*/ false);
	ZenServerState::ZenServerEntry* Entry = State.LookupByEffectiveListenPort(Port);
	if (Entry)
	{
		bool bAllAdded = true;
		for (uint32 SponsorProcessID : SponsorProcessIDs)
		{
			if (!Entry->AddSponsorProcess(SponsorProcessID))
			{
				bAllAdded = false;
			}
		}
		return bAllAdded;
	}
	return false;
}

uint16
FZenServiceInstance::GetAutoLaunchedPort()
{
	return AutoLaunchedPort;
}

void
FZenServiceInstance::Initialize()
{
	if (Settings.IsAutoLaunch())
	{
		uint64 ZenAutoLaunchStartTime = FPlatformTime::Cycles64();
		const FServiceAutoLaunchSettings& AutoLaunchSettings = Settings.SettingsVariant.Get<FServiceAutoLaunchSettings>();
		bool ServiceIsInstalled = ConditionalUpdateLocalInstall(AutoLaunchSettings.InstallMode);
		if (ServiceIsInstalled)
		{
			const FString ExecutablePath = GetLocalServiceInstallPath();
			const FString ExecutionContextFilePath = GetServiceRunContextPath();

			int LaunchAttempts = 0;
			const FTimespan MaximumWaitForHealth = FTimespan::FromSeconds(20);
			FDateTime StartedWaitingForHealth = FDateTime::UtcNow();
			while (true)
			{
				{
					FSystemWideCriticalSection RecoveryCriticalSection(TEXT("ZenServerLaunch"), FTimespan::FromSeconds(5));
					if (!RecoveryCriticalSection.IsValid())
					{
						UE_LOG(LogZenServiceInstance, Warning, TEXT("Local ZenServer AutoLaunch initialization timed out waiting for other process to complete startup/recovery"));
					}
					else
					{
						bHasLaunchedLocal = AutoLaunch(AutoLaunchSettings, ExecutablePath, ExecutionContextFilePath, HostName, Port);
						if (bHasLaunchedLocal)
						{
							const ZenServerState State(/*ReadOnly*/true);
							const ZenServerState::ZenServerEntry* RunningEntry = State.LookupByEffectiveListenPort(Port);
							if (RunningEntry != nullptr)
							{
								AutoLaunchedPid = RunningEntry->Pid.load(std::memory_order_relaxed);
							}
							AutoLaunchedPort = Port;
							bIsRunningLocally = true;
							if (IsServiceReady())
							{
								break;
							}
						}
					}
				}

				++LaunchAttempts;
				FTimespan WaitForHealth = FDateTime::UtcNow() - StartedWaitingForHealth;
				if ((WaitForHealth > MaximumWaitForHealth) && (LaunchAttempts > 1))
				{
					bHasLaunchedLocal = false;
					bIsRunningLocally = false;
					UE_LOG(LogZenServiceInstance, Warning, TEXT("Local ZenServer AutoLaunch initialization timed out waiting for service to become healthy"));
					break;
				}
				UE_LOG(LogZenServiceInstance, Log, TEXT("Awaiting ZenServer readiness"));
				FPlatformProcess::Sleep(0.5f);
			}
		}
		double ZenAutoLaunchDuration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - ZenAutoLaunchStartTime);
		UE_LOG(LogZenServiceInstance, Log, TEXT("Local ZenServer AutoLaunch initialization completed in %.3lf seconds"), ZenAutoLaunchDuration);
	}
	else
	{
		const FServiceConnectSettings& ConnectExistingSettings = Settings.SettingsVariant.Get<FServiceConnectSettings>();
		HostName = ConnectExistingSettings.HostName;
		Port = ConnectExistingSettings.Port;
		bIsRunningLocally = IsLocalHost(HostName);
	}
	URL = WriteToString<64>(TEXT("http://"), HostName, TEXT(":"), Port, TEXT("/"));
}

static void
PromptUserToStopRunningServerInstanceForUpdate(const FString& ServerFilePath)
{
#if !IS_PROGRAM
	if (!FApp::IsUnattended() && !IsRunningCommandlet() && !GIsRunningUnattendedScript)
	{
		FText ZenUpdatePromptTitle = NSLOCTEXT("Zen", "Zen_UpdatePromptTitle", "Update required");
		FText ZenUpdatePromptText = FText::Format(NSLOCTEXT("Zen", "Zen_UpdatePromptText", "Unreal Zen Storage Server needs to be updated to a new version. Please shut down Unreal Editor and any tools that are using the ZenServer at '{0}'"), FText::FromString(ServerFilePath));
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ZenUpdatePromptText.ToString(), *ZenUpdatePromptTitle.ToString());
	}
	else
#endif
	{
		// Just log as there is no one to show a message
		UE_LOG(LogZenServiceInstance, Display, TEXT("Unreal Zen Storage Server needs to be updated to a new version. Please shut down any tools that are using the ZenServer at '%s'"), *ServerFilePath);
	}
}

static void
PromptUserOfLockedDataFolder(const FString& DataPath)
{
#if !IS_PROGRAM
	if (!FApp::IsUnattended() && !IsRunningCommandlet() && !GIsRunningUnattendedScript)
	{
		FText ZenLaunchFailurePromptTitle = NSLOCTEXT("Zen", "Zen_NonLocalProcessUsesDataDirPromptTitle", "Failed to launch");
		FText ZenLaunchFailurePromptText = FText::Format(NSLOCTEXT("Zen", "Zen_NonLocalProcessUsesDataDirPromptText", "Unreal Zen Storage Server Failed to auto launch, an unknown process is locking the data folder '{0}'"), FText::FromString(DataPath));
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ZenLaunchFailurePromptText.ToString(), *ZenLaunchFailurePromptTitle.ToString());
	}
	else
#endif
	{
		// Just log as there is no one to show a message
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Unreal Zen Storage Server Failed to auto launch, an unknown process is locking the data folder '%s'"), *DataPath);
	}
}

static void
PromptUserOfFailedShutDownOfExistingProcess(uint16 Port)
{
#if !IS_PROGRAM
	if (!FApp::IsUnattended() && !IsRunningCommandlet() && !GIsRunningUnattendedScript)
	{
		FText ZenLaunchFailurePromptTitle = NSLOCTEXT("Zen", "Zen_ShutdownFailurePromptTitle", "Failed to launch");
		FText ZenLaunchFailurePromptText = FText::Format(NSLOCTEXT("Zen", "Zen_ShutdownFailurePromptText", "Unreal Zen Storage Server Failed to auto launch, failed to shut down currently running service using port '{0}'"), FText::AsNumber(Port, &FNumberFormattingOptions::DefaultNoGrouping()));
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ZenLaunchFailurePromptText.ToString(), *ZenLaunchFailurePromptTitle.ToString());
	}
	else
#endif
	{
		// Just log as there is no one to show a message
		UE_LOG(LogZenServiceInstance, Warning, TEXT("Unreal Zen Storage Server Failed to auto launch, failed to shut down currently running service using port %u"), Port);
	}
}

bool
FZenServiceInstance::ConditionalUpdateLocalInstall(FServiceAutoLaunchSettings::EInstallMode InstallMode)
{
	if (InstallMode == FServiceAutoLaunchSettings::EInstallMode::Link)
	{
		const FString LinkPath = GetServiceLinkPath();
		IFileManager& FileManager = IFileManager::Get();

		bool LinkIsValid = false;
		const FZenServiceLink Link = FZenServiceLink::Read(LinkPath);
		if (Link)
		{
			if (!FileManager.FileExists(*Link.ServicePath))
			{
				UE_LOG(LogZenServiceInstance, Warning, TEXT("Found service link file '%s' pointing to unreachable service executable '%s'"), *LinkPath, *Link.ServicePath);
			}
			else if (!FileManager.FileExists(*Link.UtilityPath))
			{
				UE_LOG(LogZenServiceInstance, Warning, TEXT("Found service link file '%s' pointing to unreachable utility executable '%s'"), *LinkPath, *Link.UtilityPath);
			}
			else
			{
				LinkIsValid = true;
			}
		}
		else if (FileManager.FileExists(*LinkPath))
		{
			UE_LOG(LogZenServiceInstance, Warning, TEXT("Found invalid service link file '%s', ignoring it"), *LinkPath);
		}
		
		FString InTreeServicePath = GetInTreeServicePath();
		if (LinkIsValid && InTreeServicePath == Link.ServicePath)
		{
			// If the running process already points to our executable and we have a valid link file we are good to go
			uint32_t Pid = 0;
			if (ZenServerState::FindRunningProcessId(*InTreeServicePath, &Pid))
			{
				UE_LOG(LogZenServiceInstance, Log, TEXT("Service link '%s' pointing to '%s', version %s is up and running"), *LinkPath, *Link.ServicePath, *Link.Version.ToString(false));
				return true;
			}
		}

		FString InTreeUtilityPath = GetInTreeUtilityPath();
		FZenVersion InTreeVersion;
		if (!GetZenVersion(InTreeUtilityPath, InTreeServicePath, InTreeVersion))
		{
			checkf(false, TEXT("Unable to determine version using zen utility executable path: '%s'."), *InTreeUtilityPath);
			return false;
		}
		if (LinkIsValid)
		{
			if (Link.Version < InTreeVersion)
			{
				UE_LOG(LogZenServiceInstance, Display, TEXT("Installing service link '%s' to '%s', version %s"), *LinkPath, *InTreeServicePath, *InTreeVersion.ToString(false));
			}
			else
			{
				// If the instance is running, assume it is valid and up to date
				uint32_t Pid = 0;
				if (ZenServerState::FindRunningProcessId(*Link.ServicePath, &Pid))
				{
					UE_LOG(LogZenServiceInstance, Display, TEXT("Service link '%s' pointing to '%s', version %s is up to date and running"), *LinkPath, *Link.ServicePath, *Link.Version.ToString(false));
					return true;
				}

				// Verify that the executable pointed at is runnable and is of matching version
				FZenVersion LinkedVersion;
				if (GetZenVersion(Link.UtilityPath, Link.ServicePath, LinkedVersion))
				{
					if (LinkedVersion == Link.Version)
					{
						UE_LOG(LogZenServiceInstance, Display, TEXT("Service link '%s' pointing to '%s', version %s is up to date"), *LinkPath, *Link.ServicePath, *Link.Version.ToString(false));
						return true;
					}
					else
					{
						UE_LOG(LogZenServiceInstance, Display, TEXT("Updating service link '%s' to '%s', version %s (link '%s', version %s does not match executable version %s)"), *LinkPath, *InTreeServicePath, *InTreeVersion.ToString(false), *Link.ServicePath, *Link.Version.ToString(false), *LinkedVersion.ToString(false));
					}
				}
				else
				{
					UE_LOG(LogZenServiceInstance, Display, TEXT("Updating service link '%s' to '%s', version %s (link '%s', version %s pointing to invalid executable)"), *LinkPath, *InTreeServicePath, *InTreeVersion.ToString(false), *Link.ServicePath, *Link.Version.ToString(false));
				}
			}
		}

		FZenServiceLink NewLink{ .ServicePath = InTreeServicePath, .UtilityPath = InTreeUtilityPath, .Version = InTreeVersion };
		if (!FZenServiceLink::Write(NewLink, LinkPath))
		{
			UE_LOG(LogZenServiceInstance, Warning, TEXT("Failed to update service link file '%s'"), *LinkPath);
			return false;
		}

		FString ServiceCopyInstallPath = GetServiceCopyInstallPath();
		if (FileManager.FileExists(*ServiceCopyInstallPath))
		{
			ShutDownZenServerProcessExecutable(ServiceCopyInstallPath);
		}

		TArray<FString> FilesToCleanUp{ GetUtilityCopyInstallPath(), ServiceCopyInstallPath, GetInstallVersionCachePath(), GetInstallVersionCachePath() };
#if PLATFORM_WINDOWS
		FilesToCleanUp.Add(FPaths::SetExtension(FilesToCleanUp[0], TEXT("pdb")));
		FilesToCleanUp.Add(FPaths::SetExtension(FilesToCleanUp[1], TEXT("pdb")));
#endif // PLATFORM_WINDOWS
		for (const FString& FileToCleanUp : FilesToCleanUp)
		{
			// If zenserver is still running we may fail to clean up a file. Not critical, just try again next startup
			if (!AttemptFileDeleteWithRetries(*FileToCleanUp, 1.0))
			{
				UE_LOG(LogZenServiceInstance, Log, TEXT("Failed cleaning up file {%s} (not critical)"), *FileToCleanUp);
			}
		}
		return true;
	}
	else if (InstallMode == FServiceAutoLaunchSettings::EInstallMode::Copy)
	{
		FString InTreeUtilityPath = GetInTreeUtilityPath();
		FString InstallUtilityPath = GetUtilityCopyInstallPath();
		FString InTreeVersionCache = GetInTreeVersionCache();

		FString InTreeServicePath = GetInTreeServicePath();
		FString InstallServicePath = GetServiceCopyInstallPath();
		FString InstallVersionCache = GetInstallVersionCachePath();

		IFileManager& FileManager = IFileManager::Get();

		bool bMainExecutablesUpdated = false;
		if (IsInstallVersionOutOfDate(InTreeUtilityPath, InstallUtilityPath, InTreeServicePath, InstallServicePath, InTreeVersionCache, InstallVersionCache))
		{
			UE_LOG(LogZenServiceInstance, Display, TEXT("Installing service from '%s' to '%s'"), *InTreeServicePath, *InstallServicePath);
			if (!ShutDownZenServerProcessExecutable(InstallServicePath))
			{
				PromptUserToStopRunningServerInstanceForUpdate(InstallServicePath);
				return false;
			}

			// Even after waiting for the process to shut down we have a tolerance for failure when overwriting the target files
			if (!AttemptFileCopyWithRetries(*InstallServicePath, *InTreeServicePath, 5.0))
			{
				PromptUserToStopRunningServerInstanceForUpdate(InstallServicePath);
				return false;
			}

			if (!AttemptFileCopyWithRetries(*InstallUtilityPath, *InTreeUtilityPath, 5.0))
			{
				PromptUserToStopRunningServerInstanceForUpdate(InstallServicePath);
				return false;
			}

			AttemptFileCopyWithRetries(*InstallVersionCache, *InTreeVersionCache, 1.0);

			bMainExecutablesUpdated = true;
		}

#if PLATFORM_WINDOWS
		struct FZenExecutable
		{
			FString& InTreeFilePath;
			FString& InstallFilePath;
		};
		const FZenExecutable ZenExecutables[] = {
			// Service executable (zenserver.exe)
			{InTreeServicePath, InstallServicePath},
			// Utility executable (zen.exe)
			{InTreeUtilityPath, InstallUtilityPath},
		};
		for (const FZenExecutable& Executable : ZenExecutables)
		{
			FString InTreeSymbolFilePath = FPaths::SetExtension(Executable.InTreeFilePath, TEXT("pdb"));
			FString InstallSymbolFilePath = FPaths::SetExtension(Executable.InstallFilePath, TEXT("pdb"));

			if (FileManager.FileExists(*InTreeSymbolFilePath) && (bMainExecutablesUpdated || !FileManager.FileExists(*InstallSymbolFilePath)))
			{
				AttemptFileCopyWithRetries(*InstallSymbolFilePath, *InTreeSymbolFilePath, 1.0);
			}
		}
#endif

		FString InTreeCrashpadHandlerFilePath = GetInTreeCrashpadHandlerFilePath();
		FString InstallCrashpadHandlerFilePath = GetInstallCrashpadHandlerFilePath(InTreeCrashpadHandlerFilePath);

		if (FileManager.FileExists(*InTreeCrashpadHandlerFilePath) && (bMainExecutablesUpdated || !FileManager.FileExists(*InstallCrashpadHandlerFilePath)))
		{
			AttemptFileCopyWithRetries(*InstallCrashpadHandlerFilePath, *InTreeCrashpadHandlerFilePath, 1.0);
		}

		const FString LinkPath = GetServiceLinkPath();
		if (FZenServiceLink Link = FZenServiceLink::Read(LinkPath); Link)
		{
			ShutDownZenServerProcessExecutable(Link.ServicePath);
			if (!AttemptFileDeleteWithRetries(*LinkPath, 1.0))
			{
				UE_LOG(LogZenServiceInstance, Log, TEXT("Failed cleaning up file {%s} (not critical)"), *LinkPath);
			}
		}

		return true;
	}
	else
	{
		return false;
	}
}

bool
FZenServiceInstance::AutoLaunch(const FServiceAutoLaunchSettings& InSettings, const FString& ExecutablePath, const FString& ExecutionContextFilePath, FString& OutHostName, uint16& OutPort)
{
	IFileManager& FileManager = IFileManager::Get();
	const FString LockFilePath = FPaths::Combine(InSettings.DataPath, TEXT(".lock"));

	FString WorkingDirectory = FPaths::GetPath(ExecutablePath);

	ZenLockFileData LockFileState;
	uint64 ZenWaitForRunningProcessReadyStartTime = FPlatformTime::Cycles64();
	while (IsZenProcessUsingDataDir(*LockFilePath, &LockFileState) && LockFileState.IsValid && !LockFileState.IsReady)
	{
		// Server is starting up, wait for it to get ready
		double ZenWaitDuration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - ZenWaitForRunningProcessReadyStartTime);
		if (ZenWaitDuration > 5.0)
		{
			break;
		}
		FPlatformProcess::Sleep(0.1f);
		LockFileState = ZenLockFileData();
	}

	bool bShutDownExistingInstanceForDataPath = true;
	uint32 ShutdownExistingInstanceForPid = 0;
	bool bLaunchNewInstance = true;

	if (LockFileState.IsReady)
	{
		const ZenServerState State(/*ReadOnly*/true);
		if (State.LookupByPid(LockFileState.ProcessId) == nullptr && IsZenProcessUsingDataDir(*LockFilePath, nullptr))
		{
			UE_LOG(LogZenServiceInstance, Warning, TEXT("Found locked valid lock file '%s' but can't find registered process (Pid: %d), will attempt shut down"), *LockFilePath, LockFileState.ProcessId);
			bShutDownExistingInstanceForDataPath = true;
		}
		else
		{
			if (InSettings.bIsDefaultSharedRunContext)
			{
				FZenLocalServiceRunContext DesiredRunContext;
				DesiredRunContext.Executable = ExecutablePath;
				DesiredRunContext.CommandlineArguments = DetermineCmdLineWithoutTransientComponents(InSettings, InSettings.DesiredPort);
				DesiredRunContext.WorkingDirectory = WorkingDirectory;
				DesiredRunContext.DataPath = InSettings.DataPath;
				DesiredRunContext.bShowConsole = InSettings.bShowConsole;

				FZenLocalServiceRunContext CurrentRunContext;

				bool ReadCurrentContextOK = CurrentRunContext.ReadFromJsonFile(*ExecutionContextFilePath);
				if (ReadCurrentContextOK && (DesiredRunContext == CurrentRunContext))
				{
					UE_LOG(LogZenServiceInstance, Log, TEXT("Found existing instance running on port %u matching our settings, no actions needed"), InSettings.DesiredPort);
					bLaunchNewInstance = false;
					bShutDownExistingInstanceForDataPath = false;
				}
				else
				{
					FString JsonTcharText;
					{
						TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonTcharText);
						Writer->WriteObjectStart();
						Writer->WriteObjectStart("Current");
						CurrentRunContext.WriteToJson(*Writer);
						Writer->WriteObjectEnd();
						Writer->WriteObjectStart("Desired");
						DesiredRunContext.WriteToJson(*Writer);
						Writer->WriteObjectEnd();
						Writer->WriteObjectEnd();
						Writer->Close();
					}
					UE_LOG(LogZenServiceInstance, Log, TEXT("Found existing instance running on port %u with different run context, will attempt shut down\n{%s}"), InSettings.DesiredPort, *JsonTcharText);
					bShutDownExistingInstanceForDataPath = true;
					bLaunchNewInstance = true;
				}
			}
			else
			{
				UE_LOG(LogZenServiceInstance, Log, TEXT("Found existing instance running on port %u when not using shared context, will use it"), InSettings.DesiredPort);
				bShutDownExistingInstanceForDataPath = false;
				bLaunchNewInstance = false;
			}
		}
	}
	else
	{
		const ZenServerState State(/*ReadOnly*/true);
		const ZenServerState::ZenServerEntry* RunningEntry = State.LookupByDesiredListenPort(InSettings.DesiredPort);
		if (RunningEntry != nullptr)
		{
			// It is necessary to tear down an existing zenserver running on our desired port but in a different data path because:
			// 1. zenserver won't accept port collision with itself, and will instead say "Exiting since there is already a process listening to port ..."
			// 2. When UE is changing data directories (eg: DDC path config change) we don't want to leave zenservers running on the past directories for no reason
			// Unlike other shutdown scenarios, this one can't be done based on our desired data path because the zenserver we want to shut down is running in a different data path
			UE_LOG(LogZenServiceInstance, Log, TEXT("Found existing instance running on port %u with different data directory, will attempt shutdown"), InSettings.DesiredPort);
			ShutdownExistingInstanceForPid = RunningEntry->Pid;
		}
		else
		{
			UE_LOG(LogZenServiceInstance, Log, TEXT("No current process using the data dir found, launching a new instance"));
		}
		bShutDownExistingInstanceForDataPath = false;
		bLaunchNewInstance = true;
	}

	if (bShutDownExistingInstanceForDataPath)
	{
		if (!ShutDownZenServerProcessLockingDataDir(InSettings.DataPath))
		{
			PromptUserOfFailedShutDownOfExistingProcess(InSettings.DesiredPort);
			return false;
		}
	}

	if (ShutdownExistingInstanceForPid != 0)
	{
		if (!ShutdownZenServerProcess(ShutdownExistingInstanceForPid))
		{
			PromptUserOfFailedShutDownOfExistingProcess(InSettings.DesiredPort);
			return false;
		}
	}

	if (bLaunchNewInstance)
	{
		if (InSettings.bIsDefaultDataPath && InSettings.bIsDefaultSharedRunContext)
		{
			// See if the default data path is migrating, and if so, clean up after the old one.
			// Non-default data paths don't do the same thing because users are free to switch them back and forth
			// and expext the contents to remain when they change.  Only the default one cleans up after itself
			// to avoid a situation wherey the accumulate over time as the default location changes in config.
			// This cleanup is best-effort and may fail if an instance is unexpectedly still using the previous path.
			EnsureEditorSettingsConfigLoaded();
			FString InUseDefaultDataPath;
			if (!GConfig->GetString(TEXT("/Script/UnrealEd.ZenServerSettings"), TEXT("InUseDefaultDataPath"), InUseDefaultDataPath, GEditorSettingsIni))
			{
				InUseDefaultDataPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPlatformProcess::ApplicationSettingsDir(), TEXT("Zen\\Data")));
			}
			if (!InUseDefaultDataPath.IsEmpty())
			{
				const FString InUseLockFilePath = FPaths::Combine(InUseDefaultDataPath, TEXT(".lock"));
				if (!FPaths::IsSamePath(InUseDefaultDataPath, InSettings.DataPath) && !IsZenProcessUsingDataDir(*InUseLockFilePath, nullptr))
				{
					UE_LOG(LogZenServiceInstance, Display, TEXT("Migrating default data path from '%s' to '%s'.  Old location will be deleted."), *InUseDefaultDataPath, *InSettings.DataPath);
					IFileManager::Get().DeleteDirectory(*InUseDefaultDataPath, false, true);
				}
			}
		}

		FString ParmsWithoutTransients = DetermineCmdLineWithoutTransientComponents(InSettings, InSettings.DesiredPort);
		FString TransientParms;

		if (InSettings.bLimitProcessLifetime)
		{
			TransientParms.Appendf(TEXT("--owner-pid %d"), FPlatformProcess::GetCurrentProcessId());
		}

		FZenLocalServiceRunContext EffectiveRunContext;
		EffectiveRunContext.Executable = ExecutablePath;
		EffectiveRunContext.CommandlineArguments = ParmsWithoutTransients;
		EffectiveRunContext.WorkingDirectory = WorkingDirectory;
		EffectiveRunContext.DataPath = InSettings.DataPath;
		EffectiveRunContext.bShowConsole = InSettings.bShowConsole;

		FString StartupEventName = ZenSharedEvent::GetStartupEventName();
		ZenSharedEvent StartupEvent(StartupEventName);
		if (!StartupEvent.Create())
		{
			return false;
		}
		TransientParms.Appendf(TEXT(" --child-id %s"), *StartupEventName);

		FProcHandle Proc = StartLocalService(EffectiveRunContext, TransientParms.IsEmpty() ? nullptr : *TransientParms);

		if (Proc.IsValid())
		{
			// Only write run context if we're using the default shared run context
			if (InSettings.bIsDefaultSharedRunContext)
			{
				EffectiveRunContext.WriteToJsonFile(*ExecutionContextFilePath);
			}

			bool ZenServerIsReady = false;
			FScopedSlowTask WaitForZenReadySlowTask(0, NSLOCTEXT("Zen", "Zen_WaitingForReady", "Waiting for ZenServer to be ready"));
			uint64 ZenWaitStartTime = FPlatformTime::Cycles64();

			enum class EWaitDurationPhase
			{
				Short,
				Medium,
				Long
			} DurationPhase = EWaitDurationPhase::Short;

			while (FPlatformProcess::IsProcRunning(Proc))
			{
				if (StartupEvent.Wait(5000))
				{
					ZenServerIsReady = true;
					break;
				}

				double ZenWaitDuration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - ZenWaitStartTime);
				if (ZenWaitDuration >= 10.0)
				{
					if (DurationPhase == EWaitDurationPhase::Short)
					{
						if (!FPlatformProcess::IsProcRunning(Proc))
						{
#if !IS_PROGRAM
							if (!FApp::IsUnattended() && !IsRunningCommandlet() && !GIsRunningUnattendedScript)
							{
								FText ZenLaunchFailurePromptTitle = NSLOCTEXT("Zen", "Zen_LaunchFailurePromptTitle", "Failed to launch");

								FFormatNamedArguments FormatArguments;
								FString LogFilePath = FPaths::Combine(InSettings.DataPath, TEXT("logs"), TEXT("zenserver.log"));
								FPaths::MakePlatformFilename(LogFilePath);
								FormatArguments.Add(TEXT("LogFilePath"), FText::FromString(LogFilePath));
								FText ZenLaunchFailurePromptText = FText::Format(NSLOCTEXT("Zen", "Zen_LaunchFailurePromptText", "Unreal Zen Storage Server failed to launch. Please check the ZenServer log file for details:\n{LogFilePath}"), FormatArguments);
								FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ZenLaunchFailurePromptText.ToString(), *ZenLaunchFailurePromptTitle.ToString());
								break;
							}
							else
#endif
							{
								// Just log as there is no one to show a message
								UE_LOG(LogZenServiceInstance, Warning, TEXT("Unreal Zen Storage Server did not launch in the expected duration"));
								break;
							}
						}

						// Note that the dialog may not show up when zenserver is needed early in the launch cycle, but this will at least ensure
						// the splash screen is refreshed with the appropriate text status message.
						WaitForZenReadySlowTask.MakeDialog(true, false);
						UE_LOG(LogZenServiceInstance, Display, TEXT("Waiting for ZenServer to be ready..."));
						DurationPhase = EWaitDurationPhase::Medium;
					}
#if !IS_PROGRAM
					else if (!(FApp::IsUnattended() || IsRunningCommandlet() || GIsRunningUnattendedScript) && ZenWaitDuration > 20.0 && (DurationPhase == EWaitDurationPhase::Medium))
					{
						FText ZenLongWaitPromptTitle = NSLOCTEXT("Zen", "Zen_LongWaitPromptTitle", "Wait for ZenServer?");
						FText ZenLongWaitPromptText = NSLOCTEXT("Zen", "Zen_LongWaitPromptText", "Unreal Zen Storage Server is taking a long time to launch. It may be performing maintenance. Keep waiting?");
						if (FPlatformMisc::MessageBoxExt(EAppMsgType::YesNo, *ZenLongWaitPromptText.ToString(), *ZenLongWaitPromptTitle.ToString()) == EAppReturnType::No)
						{
							break;
						}
						DurationPhase = EWaitDurationPhase::Long;
					}
#endif
					if (WaitForZenReadySlowTask.ShouldCancel())
					{
						break;
					}
				}
			}

			if (!ZenServerIsReady)
			{
				if (FPlatformProcess::IsProcRunning(Proc))
				{
					UE_LOG(LogZenServiceInstance, Warning, TEXT("Terminating unresponsive process for executable '%s' on port %u"), *ExecutablePath, InSettings.DesiredPort);
					FPlatformProcess::TerminateProc(Proc, true);
				}
			}
		}
		else
		{
			UE_LOG(LogZenServiceInstance, Warning, TEXT("Failed launch service using executable '%s' on port %u"), *ExecutablePath, InSettings.DesiredPort);
			return false;
		}
	}
	else if (InSettings.bLimitProcessLifetime)
	{
		ZenServerState State(/*ReadOnly*/ false);
		ZenServerState::ZenServerEntry* RunningEntry = State.LookupByDesiredListenPort(InSettings.DesiredPort);
		if (RunningEntry == nullptr)
		{
			UE_LOG(LogZenServiceInstance, Warning, TEXT("Failed attach as sponsor process to executable '%s' on port %u, can't find entry in shared state"), *ExecutablePath, InSettings.DesiredPort);
		}
		else if (!RunningEntry->AddSponsorProcess(FPlatformProcess::GetCurrentProcessId()))
		{
			UE_LOG(LogZenServiceInstance, Warning, TEXT("Failed attach as sponsor process to executable '%s' on port %u, to many sponsored processes attached already"), *ExecutablePath, InSettings.DesiredPort);
		}
	}

	if (InSettings.bIsDefaultDataPath && InSettings.bIsDefaultSharedRunContext)
	{
		GConfig->SetString(TEXT("/Script/UnrealEd.ZenServerSettings"), TEXT("InUseDefaultDataPath"), *InSettings.DataPath, GEditorSettingsIni);
	}

	OutHostName = TEXT("[::1]");
	// Default to assuming that we get to run on the port we want
	OutPort = InSettings.DesiredPort;

	ZenLockFileData RunningLockFileState = ZenLockFileData::ReadCbLockFile(*LockFilePath);
	if (!RunningLockFileState.IsValid)
	{
		return false;
	}
	if (!RunningLockFileState.IsReady)
	{
		return false;
	}
	OutPort = RunningLockFileState.EffectivePort;
	return true;
}

bool 
FZenServiceInstance::GetCacheStats(FZenCacheStats& Stats)
{
	UE::Zen::FZenHttpRequest* Request = nullptr;
	{
		TUniqueLock Lock(LastCacheStatsMutex);
		// If we've already requested stats and they are ready then grab them
		if ( CacheStatsRequest.IsReady() == true )
		{
			LastCacheStats		= CacheStatsRequest.Get();
			LastCacheStatsTime	= FPlatformTime::Cycles64();

			CacheStatsRequest.Reset();
		}
	
		// Make a copy of the last updated stats
		Stats = LastCacheStats;

		const uint64 CurrentTime = FPlatformTime::Cycles64();
		constexpr double MinTimeBetweenRequestsInSeconds = 0.5;
		const double DeltaTimeInSeconds = FPlatformTime::ToSeconds64(CurrentTime - LastCacheStatsTime);

		if (CacheStatsRequest.IsValid() || DeltaTimeInSeconds <= MinTimeBetweenRequestsInSeconds)
		{
			return Stats.bIsValid;
		}

		if (!CacheStatsHttpRequest.IsValid())
		{
			TStringBuilder<128> ZenDomain;
			ZenDomain << HostName << TEXT(":") << Port;
			CacheStatsHttpRequest = MakePimpl<FZenHttpRequest>(ZenDomain.ToString(), false);
		}

		Request = CacheStatsHttpRequest.Get();
	}

#if WITH_EDITOR
	EAsyncExecution ThreadPool = EAsyncExecution::LargeThreadPool;
#else
	EAsyncExecution ThreadPool = EAsyncExecution::ThreadPool;
#endif
		// We've not got any requests in flight and we've met a given time requirement for requests
	CacheStatsRequest = Async(ThreadPool, [Request]
	{
		check(Request != nullptr);
		Request->Reset();

		TArray64<uint8> GetBuffer;
		FZenHttpRequest::Result Result = Request->PerformBlockingDownload(TEXTVIEW("/stats/z$"), &GetBuffer, Zen::EContentType::CbObject);

		FZenCacheStats Stats;

		if (Result == Zen::FZenHttpRequest::Result::Success && Request->GetResponseCode() == 200)
		{
			FCbFieldView RootView(GetBuffer.GetData());
			Stats.bIsValid = LoadFromCompactBinary(RootView, Stats);
		}

		return Stats;
	});

	return Stats.bIsValid;
}

bool 
FZenServiceInstance::GetProjectStats(FZenProjectStats& Stats)
{
	UE::Zen::FZenHttpRequest* Request = nullptr;
	{
		TUniqueLock Lock(LastProjectStatsMutex);
		// If we've already requested stats and they are ready then grab them
		if ( ProjectStatsRequest.IsReady() == true )
		{
			LastProjectStats		= ProjectStatsRequest.Get();
			LastProjectStatsTime	= FPlatformTime::Cycles64();

			ProjectStatsRequest.Reset();
		}
	
		// Make a copy of the last updated stats
		Stats = LastProjectStats;

		const uint64 CurrentTime = FPlatformTime::Cycles64();
		constexpr double MinTimeBetweenRequestsInSeconds = 0.5;
		const double DeltaTimeInSeconds = FPlatformTime::ToSeconds64(CurrentTime - LastProjectStatsTime);

		if (ProjectStatsRequest.IsValid() || DeltaTimeInSeconds <= MinTimeBetweenRequestsInSeconds)
		{
			return Stats.bIsValid;
		}
		if (!ProjectStatsHttpRequest.IsValid())
		{
			TStringBuilder<128> ZenDomain;
			ZenDomain << HostName << TEXT(":") << Port;
			ProjectStatsHttpRequest = MakePimpl<FZenHttpRequest>(ZenDomain.ToString(), false);
		}
		Request = ProjectStatsHttpRequest.Get();
	}

#if WITH_EDITOR
	EAsyncExecution ThreadPool = EAsyncExecution::LargeThreadPool;
#else
	EAsyncExecution ThreadPool = EAsyncExecution::ThreadPool;
#endif
			// We've not got any requests in flight and we've met a given time requirement for requests
	ProjectStatsRequest = Async(ThreadPool, [Request]
	{
		check(Request);
		Request->Reset();

			TArray64<uint8> GetBuffer;
		FZenHttpRequest::Result Result = Request->PerformBlockingDownload(TEXTVIEW("/stats/prj"), &GetBuffer, Zen::EContentType::CbObject);

			FZenProjectStats Stats;

		if (Result == Zen::FZenHttpRequest::Result::Success && Request->GetResponseCode() == 200)
			{
				FCbFieldView RootView(GetBuffer.GetData());
				Stats.bIsValid = LoadFromCompactBinary(RootView, Stats);
			}

			return Stats;
		});

	return Stats.bIsValid;
}

bool 
FZenServiceInstance::GetGCStatus(FGCStatus& Status)
{
	check(IsInGameThread());

	// If we've already requested status and it is ready then grab it
	if (GCStatusRequest.IsReady() == true )
	{
		LastGCStatus	 = GCStatusRequest.Get();
		LastGCStatusTime = FPlatformTime::Cycles64();

		GCStatusRequest.Reset();
	}
	
	// Make a copy of the last updated status
	if (LastGCStatus.IsSet())
	{
		Status = LastGCStatus.GetValue();
	}

	const uint64 CurrentTime = FPlatformTime::Cycles64();
	constexpr double MinTimeBetweenRequestsInSeconds = 0.5;
	const double DeltaTimeInSeconds = FPlatformTime::ToSeconds64(CurrentTime - LastGCStatusTime);

	if (!GCStatusRequest.IsValid() && DeltaTimeInSeconds > MinTimeBetweenRequestsInSeconds)
	{
#if WITH_EDITOR
		EAsyncExecution ThreadPool = EAsyncExecution::LargeThreadPool;
#else
		EAsyncExecution ThreadPool = EAsyncExecution::ThreadPool;
#endif
		if (!GCStatusHttpRequest.IsValid())
		{
			TStringBuilder<128> ZenDomain;
			ZenDomain << HostName << TEXT(":") << Port;
			GCStatusHttpRequest = MakePimpl<FZenHttpRequest>(ZenDomain.ToString(), false);
		}

		// We've not got any requests in flight and we've met a given time requirement for requests
		GCStatusRequest = Async(ThreadPool, [this]
			{
				UE::Zen::FZenHttpRequest& Request = *GCStatusHttpRequest.Get();
				Request.Reset();

				TArray64<uint8> GetBuffer;
				FZenHttpRequest::Result Result = Request.PerformBlockingDownload(TEXTVIEW("/admin/gc"), &GetBuffer, Zen::EContentType::CbObject);

				TOptional<FGCStatus> GCStatus;

				if (Result == Zen::FZenHttpRequest::Result::Success && Request.GetResponseCode() == 200)
				{
					FCbObjectView RootObjectView(GetBuffer.GetData());

					GCStatus.Emplace();
					GCStatus->Description = FString(RootObjectView["Status"].AsString());
				}

				return GCStatus;
			});
	}

	return LastGCStatus.IsSet();
}

bool 
FZenServiceInstance::RequestGC(const bool* OverrideCollectSmallObjects, const uint32* OverrideMaxCacheDuration)
{
	TStringBuilder<128> ZenDomain;
	ZenDomain << HostName << TEXT(":") << Port;
	UE::Zen::FZenHttpRequest Request(ZenDomain.ToString(), false);

	TCHAR Separators[] = {TEXT('?'), TEXT('&')};
	int32 SeparatorIndex = 0;
	TStringBuilder<128> Query;
	Query << TEXTVIEW("/admin/gc");

	if (OverrideCollectSmallObjects)
	{
		Query << Separators[SeparatorIndex] << TEXT("smallobjects=") << LexToString(*OverrideCollectSmallObjects);
		SeparatorIndex = FMath::Min(SeparatorIndex + 1, (int32)UE_ARRAY_COUNT(Separators));
	}

	if (OverrideMaxCacheDuration)
	{
		Query << Separators[SeparatorIndex] << TEXT("maxcacheduration=") << LexToString(*OverrideMaxCacheDuration);
		SeparatorIndex = FMath::Min(SeparatorIndex + 1, (int32)UE_ARRAY_COUNT(Separators));
	}

	FZenHttpRequest::Result Result = Request.PerformBlockingPost(Query.ToString(), FMemoryView());

	if (Result == Zen::FZenHttpRequest::Result::Success && Request.GetResponseCode() == 200)
	{
		FCbObjectView ResponseObject = FCbObjectView(Request.GetResponseBuffer().GetData());
		FUtf8StringView ResponseStatus = ResponseObject["status"].AsString();

		return (ResponseStatus == "Started") || (ResponseStatus == "Running");
	}
	return false;
}

bool 
FZenServiceInstance::GatherAnalytics(TArray<FAnalyticsEventAttribute>& Attributes)
{
	FZenCacheStats ZenCacheStats;
	FZenProjectStats ZenProjectStats;

	if (GetCacheStats(ZenCacheStats) == false)
		return false;

	if (GetProjectStats(ZenProjectStats) == false)
		return false;

	const FString BaseName = TEXT("Zen_");

	{
		FString AttrName = BaseName + TEXT("Enabled");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.bIsValid && ZenProjectStats.bIsValid);
	}

	///////////// Cache
	{
		FString AttrName = BaseName + TEXT("Cache_Size_Disk");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.General.Size.Disk);
	}

	{
		FString AttrName = BaseName + TEXT("Cache_Size_Memory");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.General.Size.Memory);
	}

	{
		FString AttrName = BaseName + TEXT("Cache_Hits");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.General.Hits);
	}

	{
		FString AttrName = BaseName + TEXT("Cache_Misses");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.General.Misses);
	}

	{
		FString AttrName = BaseName + TEXT("Cache_Writes");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.General.Writes);
	}

	{
		FString AttrName = BaseName + TEXT("Cache_HitRatio");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.General.HitRatio);
	}

	{
		FString AttrName = BaseName + TEXT("Cache_Cas_Hits");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.General.CidHits);
	}

	{
		FString AttrName = BaseName + TEXT("Cache_Cas_Misses");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.General.CidMisses);
	}

	{
		FString AttrName = BaseName + TEXT("Cache_Cas_Writes");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.General.CidWrites);
	}

	{
		FString AttrName = BaseName + TEXT("Cache_Requests");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.Request.Count);
	}

	{
		FString AttrName = BaseName + TEXT("Cache_BadRequests");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.General.BadRequestCount);
	}


	{
		FString AttrName = BaseName + TEXT("Cache_Requests_Count");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.Request.Count);
	}

	{
		FString AttrName = BaseName + TEXT("Cache_Requests_RateMean");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.Request.RateMean);
	}

	{
		FString AttrName = BaseName + TEXT("Cache_Requests_TAverage");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.Request.TAverage);
	}

	{
		FString AttrName = BaseName + TEXT("Cache_Requests_TMin");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.Request.TMin);
	}

	{
		FString AttrName = BaseName + TEXT("Cache_Requests_TMax");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.Request.TMax);
	}

	{
		FString AttrName = BaseName + TEXT("Cache_TotalUploadedMB");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.Upstream.TotalUploadedMB);
	}

	{
		FString AttrName = BaseName + TEXT("Upstream_TotalDownloadedMB");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.Upstream.TotalDownloadedMB);
	}

	{
		FString AttrName = BaseName + TEXT("Upstream_TotalUploadedMB");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.Upstream.TotalUploadedMB);
	}

	{
		FString AttrName = BaseName + TEXT("Upstream_Requests_Count");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.UpstreamRequest.Count);
	}
	{
		FString AttrName = BaseName + TEXT("Upstream_Requests_RateMean");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.UpstreamRequest.RateMean);
	}
	{
		FString AttrName = BaseName + TEXT("Upstream_Requests_TAverage");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.UpstreamRequest.TAverage);
	}
	{
		FString AttrName = BaseName + TEXT("Upstream_Requests_TMin");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.UpstreamRequest.TMin);
	}
	{
		FString AttrName = BaseName + TEXT("Upstream_Requests_TMax");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.UpstreamRequest.TMax);
	}

	{
		FString AttrName = BaseName + TEXT("Cas_Size_Large");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.CID.Size.Large);
	}

	{
		FString AttrName = BaseName + TEXT("Cas_Size_Small");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.CID.Size.Small);
	}

	{
		FString AttrName = BaseName + TEXT("Cas_Size_Tiny");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.CID.Size.Tiny);
	}

	{
		FString AttrName = BaseName + TEXT("Cas_Size_Total");
		Attributes.Emplace(MoveTemp(AttrName), ZenCacheStats.CID.Size.Total);
	}

	///////////// Project
	{
		FString AttrName = BaseName + TEXT("Project_Size_Disk");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.Size.Disk);
	}

	{
		FString AttrName = BaseName + TEXT("Project_Size_Memory");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.Size.Memory);
	}

	{
		FString AttrName = BaseName + TEXT("Project_WriteCount");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.Project.WriteCount);
	}

	{
		FString AttrName = BaseName + TEXT("Project_ReadCount");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.Project.ReadCount);
	}

	{
		FString AttrName = BaseName + TEXT("Project_DeleteCount");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.Project.DeleteCount);
	}

	{
		FString AttrName = BaseName + TEXT("Project_Oplog_WriteCount");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.Oplog.WriteCount);
	}

	{
		FString AttrName = BaseName + TEXT("Project_Oplog_ReadCount");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.Oplog.ReadCount);
	}

	{
		FString AttrName = BaseName + TEXT("Project_Oplog_DeleteCount");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.Oplog.DeleteCount);
	}

	{
		FString AttrName = BaseName + TEXT("Project_Op_Hits");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.Op.HitCount);
	}

	{
		FString AttrName = BaseName + TEXT("Project_Op_Misses");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.Op.MissCount);
	}

	{
		FString AttrName = BaseName + TEXT("Project_Op_Writes");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.Op.WriteCount);
	}

	{
		FString AttrName = BaseName + TEXT("Project_Chunk_Hits");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.Chunk.HitCount);
	}

	{
		FString AttrName = BaseName + TEXT("Project_Chunk_Misses");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.Chunk.MissCount);
	}

	{
		FString AttrName = BaseName + TEXT("Project_Chunk_Writes");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.Chunk.WriteCount);
	}

	{
		FString AttrName = BaseName + TEXT("Project_Requests");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.RequestCount);
	}

	{
		FString AttrName = BaseName + TEXT("Project_BadRequests");
		Attributes.Emplace(MoveTemp(AttrName), ZenProjectStats.General.BadRequestCount);
	}

	{
		FString AttrName = BaseName + TEXT("Project_Op_HitRatio");
		double Total = static_cast<double>(ZenProjectStats.General.Op.HitCount + ZenProjectStats.General.Op.MissCount);
		Attributes.Emplace(MoveTemp(AttrName), Total > 0 ? static_cast<double>(ZenProjectStats.General.Op.HitCount) / Total : 0.0);
	}

	{
		FString AttrName = BaseName + TEXT("Project_Chunk_HitRatio");
		double Total = static_cast<double>(ZenProjectStats.General.Chunk.HitCount + ZenProjectStats.General.Chunk.MissCount);
		Attributes.Emplace(MoveTemp(AttrName), Total > 0 ? static_cast<double>(ZenProjectStats.General.Chunk.HitCount) / Total : 0.0);
	}

	return true;
}

#endif // UE_WITH_ZEN

}

