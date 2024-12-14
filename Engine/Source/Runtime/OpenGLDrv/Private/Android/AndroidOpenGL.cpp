// Copyright Epic Games, Inc. All Rights Reserved.

#include "AndroidOpenGL.h"

#if USE_ANDROID_OPENGL

#include "OpenGLDrvPrivate.h"
#include "OpenGLES.h"
#include "Android/AndroidWindow.h"
#include "AndroidOpenGLPrivate.h"
#include "Android/AndroidPlatformMisc.h"
#include "Android/AndroidPlatformFramePacer.h"
#include "Android/AndroidJNI.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "Misc/ConfigCacheIni.h"
#include "String/Find.h"
#include "String/LexFromString.h"
#include "Android/AndroidDynamicRHI.h"
#include "PSOMetrics.h"

int32 FAndroidOpenGL::GLMajorVerion = 0;
int32 FAndroidOpenGL::GLMinorVersion = 0;

bool FAndroidOpenGL::bSupportsImageExternal = false;
bool FAndroidOpenGL::bRequiresAdrenoTilingHint = false;

static TAutoConsoleVariable<int32> CVarEnableAdrenoTilingHint(
	TEXT("r.Android.EnableAdrenoTilingHint"),
	1,
	TEXT("Whether Adreno-based Android devices should hint to the driver to use tiling mode for the mobile base pass.\n")
	TEXT("  0 = hinting disabled\n")
	TEXT("  1 = hinting enabled for Adreno devices running Andorid 8 or earlier [default]\n")
	TEXT("  2 = hinting always enabled for Adreno devices\n"));

static TAutoConsoleVariable<int32> CVarDisableEarlyFragmentTests(
	TEXT("r.Android.DisableEarlyFragmentTests"),
	0,
	TEXT("Whether to disable early_fragment_tests if any \n"),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarDisableFBFNonCoherent(
	TEXT("r.Android.DisableFBFNonCoherent"),
	0,
	TEXT("Whether to disable usage of QCOM_shader_framebuffer_fetch_noncoherent extension\n"),
	ECVF_ReadOnly);

struct FPlatformOpenGLDevice
{
	bool TargetDirty;

	void SetCurrentSharedContext();
	void SetCurrentRenderingContext();
	void SetupCurrentContext();
	void SetCurrentNULLContext();

	FPlatformOpenGLDevice();
	~FPlatformOpenGLDevice();
	void Init();
	void LoadEXT();
	void Terminate();
	void ReInit();
};


FPlatformOpenGLDevice::~FPlatformOpenGLDevice()
{
	FPlatformRHIFramePacer::Destroy();

	FAndroidAppEntry::ReleaseEGL();
}

FPlatformOpenGLDevice::FPlatformOpenGLDevice()
{
}

// call out to JNI to see if the application was packaged for Oculus Mobile
extern bool AndroidThunkCpp_IsOculusMobileApplication();


// RenderDoc
#define GL_DEBUG_TOOL_EXT	0x6789
static bool bRunningUnderRenderDoc = false;


#if UE_BUILD_SHIPPING
#define CHECK_JNI_EXCEPTIONS(env)  env->ExceptionClear();
#else
#define CHECK_JNI_EXCEPTIONS(env)  if (env->ExceptionCheck()) {env->ExceptionDescribe();env->ExceptionClear();}
#endif

struct FOpenGLRemoteGLProgramCompileJNI
{
	jclass OGLServiceAccessor = 0;
	jmethodID DispatchProgramLink = 0;
	jmethodID StartRemoteProgramLink = 0;
	jmethodID StopRemoteProgramLink = 0;
	jmethodID AreProgramServicesReady = 0;
	jmethodID HaveServicesFailed = 0;
	jclass ProgramResponseClass = 0;
	jfieldID ProgramResponse_SuccessField = 0;
	jfieldID ProgramResponse_ErrorField = 0;
	jfieldID ProgramResponse_SHMOutputHandleField = 0;
	jfieldID ProgramResponse_CompiledBinaryField = 0;
	jfieldID ProgramResponse_CompilationDurationField = 0;

	bool bAllFound = false;

	void Init(JNIEnv* Env)
	{
		// class JNIProgramLinkResponse
		// {
		// 	boolean bCompileSuccess;
		// 	String ErrorMessage;
		// 	byte[] CompiledProgram;
		// };
		// JNIProgramLinkResponse AndroidThunkJava_OGLRemoteProgramLink(...):


		check(OGLServiceAccessor == 0);
		OGLServiceAccessor = AndroidJavaEnv::FindJavaClassGlobalRef("com/epicgames/unreal/psoservices/PSOProgramServiceAccessor");
		CHECK_JNI_EXCEPTIONS(Env);
		if(OGLServiceAccessor)
		{
			DispatchProgramLink = FJavaWrapper::FindStaticMethod(Env, OGLServiceAccessor, "AndroidThunkJava_OGLRemoteProgramLink", "([BJLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)Lcom/epicgames/unreal/psoservices/PSOProgramServiceAccessor$JNIProgramLinkResponse;", false);
			CHECK_JNI_EXCEPTIONS(Env);
			StartRemoteProgramLink = FJavaWrapper::FindStaticMethod(Env, OGLServiceAccessor, "AndroidThunkJava_StartRemoteProgramLink", "(IZZ)Z", false);
			CHECK_JNI_EXCEPTIONS(Env);
			StopRemoteProgramLink = FJavaWrapper::FindStaticMethod(Env, OGLServiceAccessor, "AndroidThunkJava_StopRemoteProgramLink", "()V", false);
			CHECK_JNI_EXCEPTIONS(Env);
			AreProgramServicesReady = FJavaWrapper::FindStaticMethod(Env, OGLServiceAccessor, "AndroidThunkJava_AreProgramServicesReady", "()Z", false);
			CHECK_JNI_EXCEPTIONS(Env);
			HaveServicesFailed = FJavaWrapper::FindStaticMethod(Env, OGLServiceAccessor, "AndroidThunkJava_HaveServicesFailed", "()Z", false);
			CHECK_JNI_EXCEPTIONS(Env);
			ProgramResponseClass = AndroidJavaEnv::FindJavaClassGlobalRef("com/epicgames/unreal/psoservices/PSOProgramServiceAccessor$JNIProgramLinkResponse");
			CHECK_JNI_EXCEPTIONS(Env);
			ProgramResponse_SuccessField = FJavaWrapper::FindField(Env, ProgramResponseClass, "bCompileSuccess", "Z", true);
			CHECK_JNI_EXCEPTIONS(Env);
			ProgramResponse_CompiledBinaryField = FJavaWrapper::FindField(Env, ProgramResponseClass, "CompiledProgram", "[B", true);
			CHECK_JNI_EXCEPTIONS(Env);
			ProgramResponse_ErrorField = FJavaWrapper::FindField(Env, ProgramResponseClass, "ErrorMessage", "Ljava/lang/String;", true);
			CHECK_JNI_EXCEPTIONS(Env);
			ProgramResponse_SHMOutputHandleField = FJavaWrapper::FindField(Env, ProgramResponseClass, "SHMOutputHandle", "I", true);
			CHECK_JNI_EXCEPTIONS(Env);
			ProgramResponse_CompilationDurationField = FJavaWrapper::FindField(Env, ProgramResponseClass, "CompilationDuration", "F", true);
			CHECK_JNI_EXCEPTIONS(Env);
		}

		bAllFound = OGLServiceAccessor && DispatchProgramLink && StartRemoteProgramLink && StopRemoteProgramLink && AreProgramServicesReady && HaveServicesFailed && ProgramResponseClass && ProgramResponse_SuccessField && ProgramResponse_CompiledBinaryField && ProgramResponse_ErrorField && ProgramResponse_SHMOutputHandleField && ProgramResponse_CompilationDurationField;
		UE_CLOG(!bAllFound, LogRHI, Fatal, TEXT("Failed to find JNI GL remote program compiler."));
	}
}OpenGLRemoteGLProgramCompileJNI;

static bool AreAndroidOpenGLRemoteCompileServicesAvailable()
{
	static int RemoteCompileService = -1;
	if (RemoteCompileService == -1)
	{
		const FString* ConfigRulesDisableProgramCompileServices = FAndroidMisc::GetConfigRulesVariable(TEXT("DisableProgramCompileServices"));
		bool bConfigRulesDisableProgramCompileServices = ConfigRulesDisableProgramCompileServices && ConfigRulesDisableProgramCompileServices->Equals("true", ESearchCase::IgnoreCase);
		static const auto CVarBinaryProgramCache = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ProgramBinaryCache.Enable"));
		static const auto CVarNumRemoteProgramCompileServices = IConsoleManager::Get().FindConsoleVariable(TEXT("Android.OpenGL.NumRemoteProgramCompileServices"));

		RemoteCompileService = !bConfigRulesDisableProgramCompileServices && OpenGLRemoteGLProgramCompileJNI.bAllFound && (CVarBinaryProgramCache->GetInt() != 0) && (CVarNumRemoteProgramCompileServices->GetInt() > 0);
		FGenericCrashContext::SetEngineData(TEXT("Android.PSOService"), RemoteCompileService == 0? TEXT("disabled") : TEXT("enabled"));
		UE_CLOG(!RemoteCompileService, LogRHI, Log, TEXT("Remote PSO services disabled: (%d, %d, %d, %d)"), bConfigRulesDisableProgramCompileServices, OpenGLRemoteGLProgramCompileJNI.bAllFound, CVarBinaryProgramCache->GetInt(), CVarNumRemoteProgramCompileServices->GetInt());
	}
	return RemoteCompileService;
}

void FPlatformOpenGLDevice::Init()
{
	// Initialize frame pacer
	FPlatformRHIFramePacer::Init(new FAndroidOpenGLFramePacer());

	extern void InitDebugContext();

	bRunningUnderRenderDoc = glIsEnabled(GL_DEBUG_TOOL_EXT) != GL_FALSE;

	FPlatformMisc::LowLevelOutputDebugString(TEXT("FPlatformOpenGLDevice:Init"));
	bool bCreateSurface = !AndroidThunkCpp_IsOculusMobileApplication();
	AndroidEGL::GetInstance()->InitSurface(false, bCreateSurface);

	LoadEXT();
	PlatformRenderingContextSetup(this);

	InitDefaultGLContextState();
	InitDebugContext();

	PlatformSharedContextSetup(this);
	InitDefaultGLContextState();
	InitDebugContext();

	AndroidEGL::GetInstance()->InitBackBuffer(); //can be done only after context is made current.

	OpenGLRemoteGLProgramCompileJNI.Init(FAndroidApplication::GetJavaEnv());

	// AsyncPipelinePrecompile can be enabled on android GL, precompiles are compiled via separate processes and the result is stored in GL's LRU cache as an evicted binary.
	// The lru cache is a requirement as the precompile produces binary program data only.
	GRHISupportsAsyncPipelinePrecompile = AreAndroidOpenGLRemoteCompileServicesAvailable();

#if USE_ANDROID_OPENGL_SWAPPY
	bool bIsSwappyEnabled = FAndroidPlatformRHIFramePacer::CVarUseSwappyForFramePacing.GetValueOnAnyThread() == 1;
	
	// don't even initialize this if swappy is not enabled
	if (bIsSwappyEnabled)
	{
		IConsoleVariable* CVarAndroidSupportsTimestampQueries = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Android.SupportsTimestampQueries"));
		IConsoleVariable* CVarAndroidSupportsDynamicResolution = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Android.SupportsDynamicResolution"));

		bool bSupportsTimestampQueries = CVarAndroidSupportsTimestampQueries != nullptr && CVarAndroidSupportsTimestampQueries->GetBool();
		bool bSupportsDynamicResolution = CVarAndroidSupportsDynamicResolution != nullptr && CVarAndroidSupportsDynamicResolution->GetBool();

		GRHISupportsDynamicResolution = bSupportsDynamicResolution;
		GSupportsTimestampRenderQueries = bSupportsTimestampQueries;
		GRHISupportsGPUTimestampBubblesRemoval = true;
	}
#endif
}

FPlatformOpenGLDevice* PlatformCreateOpenGLDevice()
{
	FPlatformOpenGLDevice* Device = new FPlatformOpenGLDevice();
	Device->Init();
	return Device;
}

bool PlatformCanEnableGPUCapture()
{
	return bRunningUnderRenderDoc;
}

void PlatformReleaseOpenGLContext(FPlatformOpenGLDevice* Device, FPlatformOpenGLContext* Context)
{
}

void* PlatformGetWindow(FPlatformOpenGLContext* Context, void** AddParam)
{
	check(Context);

	return (void*)&Context->eglContext;
}

bool PlatformBlitToViewport(IRHICommandContext& RHICmdContext, FPlatformOpenGLDevice* Device, const FOpenGLViewport& Viewport, uint32 BackbufferSizeX, uint32 BackbufferSizeY, bool bPresent,bool bLockToVsync )
{
	SCOPED_NAMED_EVENT(STAT_PlatformBlitToViewportTime, FColor::Red)
	
	FPlatformOpenGLContext* const Context = Viewport.GetGLContext();

	if (bPresent && AndroidEGL::GetInstance()->IsOfflineSurfaceRequired())
	{
		if (Device->TargetDirty)
		{
			VERIFY_GL_SCOPE();
			glBindFramebuffer(GL_FRAMEBUFFER, Context->ViewportFramebuffer);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, Context->BackBufferTarget, Context->BackBufferResource, 0);

			Device->TargetDirty = false;
		}

		{
			VERIFY_GL_SCOPE();
			glDisable(GL_FRAMEBUFFER_SRGB);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			FOpenGL::DrawBuffer(GL_BACK);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, Context->ViewportFramebuffer);
			FOpenGL::ReadBuffer(GL_COLOR_ATTACHMENT0);

			FOpenGL::BlitFramebuffer(
				0, 0, BackbufferSizeX, BackbufferSizeY,
				0, BackbufferSizeY, BackbufferSizeX, 0,
				GL_COLOR_BUFFER_BIT,
				GL_NEAREST
			);

			glEnable(GL_FRAMEBUFFER_SRGB);

			// Bind viewport FBO so driver knows we don't need backbuffer image anymore
			glBindFramebuffer(GL_FRAMEBUFFER, Context->ViewportFramebuffer);
		}
	}

	if (bPresent && Viewport.GetCustomPresent())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FAndroidOpenGL_PlatformBlitToViewport_CustomPresent);
		int32 SyncInterval = FAndroidPlatformRHIFramePacer::GetLegacySyncInterval();
		bPresent = Viewport.GetCustomPresent()->Present(RHICmdContext, SyncInterval);
	}
	if (bPresent)
	{
		AndroidEGL::GetInstance()->UpdateBuffersTransform();
		FAndroidPlatformRHIFramePacer::SwapBuffers(bLockToVsync);
	}
	static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("a.UseFrameTimeStampsForPacing"));
	const bool bForceGPUFence = CVar ? CVar->GetInt() != 0 : false;

	return bPresent && ShouldUseGPUFencesToLimitLatency();
}

void PlatformRenderingContextSetup(FPlatformOpenGLDevice* Device)
{
	Device->SetCurrentRenderingContext();
	Device->SetupCurrentContext();
}

void PlatformFlushIfNeeded()
{
}

void PlatformSharedContextSetup(FPlatformOpenGLDevice* Device)
{
	Device->SetCurrentSharedContext();
	Device->SetupCurrentContext();
}

void PlatformNULLContextSetup()
{
	AndroidEGL::GetInstance()->ReleaseContextOwnership();
}

EOpenGLCurrentContext PlatformOpenGLCurrentContext(FPlatformOpenGLDevice* Device)
{
	return (EOpenGLCurrentContext)AndroidEGL::GetInstance()->GetCurrentContextType();
}

void PlatformRestoreDesktopDisplayMode()
{
}

bool PlatformInitOpenGL()
{
	check(!FAndroidMisc::ShouldUseVulkan());

	{
		// determine ES version. PlatformInitOpenGL happens before ProcessExtensions and therefore FAndroidOpenGL::bES31Support.
		FString FullVersionString, VersionString, SubVersionString;
		FAndroidGPUInfo::Get().GLVersion.Split(TEXT("OpenGL ES "), nullptr, &FullVersionString);
		FullVersionString.Split(TEXT(" "), &FullVersionString, nullptr);
		FullVersionString.Split(TEXT("."), &VersionString, &SubVersionString);
		FAndroidOpenGL::GLMajorVerion = FCString::Atoi(*VersionString);
		FAndroidOpenGL::GLMinorVersion = FCString::Atoi(*SubVersionString);

		bool bES32Supported = FAndroidOpenGL::GLMajorVerion == 3 && FAndroidOpenGL::GLMinorVersion >= 2;
		static const auto CVarDisableES31 = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Android.DisableOpenGLES31Support"));

		bool bBuildForES31 = false;
		GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bBuildForES31"), bBuildForES31, GEngineIni);

		const bool bSupportsFloatingPointRTs = FAndroidMisc::SupportsFloatingPointRenderTargets();

		if (bBuildForES31 && bES32Supported)
		{
			FOpenGLES::CurrentFeatureLevelSupport = FAndroidOpenGL::GLMinorVersion >= 2 ? FOpenGLES::EFeatureLevelSupport::ES32 : FOpenGLES::EFeatureLevelSupport::ES31;
			UE_LOG(LogRHI, Log, TEXT("App is packaged for OpenGL ES 3.1 and an ES %d.%d-capable device was detected."), FAndroidOpenGL::GLMajorVerion, FAndroidOpenGL::GLMinorVersion);
		}
		else
		{
			FString Message = TEXT("");

			if (bES32Supported)
			{
				Message.Append(TEXT("This device does not support Vulkan but the app was not packaged with ES 3.1 support."));
				if (FAndroidMisc::GetAndroidBuildVersion() < 26)
				{
					Message.Append(TEXT(" Updating to a newer Android version may resolve this issue."));
				}
				FPlatformMisc::LowLevelOutputDebugString(*Message);
				FAndroidMisc::MessageBoxExt(EAppMsgType::Ok, *Message, TEXT("Unable to run on this device!"));
			}
			else
			{
				Message.Append(TEXT("This device only supports OpenGL ES 2/3/3.1 which is not supported, only supports ES 3.2+ "));
				FPlatformMisc::LowLevelOutputDebugString(*Message);
				FAndroidMisc::MessageBoxExt(EAppMsgType::Ok, *Message, TEXT("Unable to run on this device!"));
			}
		}

		// Needs to initialize GPU vendor id before AndroidEGL::AcquireCurrentRenderingContext
		FString& VendorName = FAndroidGPUInfo::Get().VendorName;
		if (VendorName.Contains(TEXT("ImgTec")) || VendorName.Contains(TEXT("Imagination")))
		{
			GRHIVendorId = 0x1010;
		}
		else if (VendorName.Contains(TEXT("ARM")))
		{
			GRHIVendorId = 0x13B5;
		}
		else if (VendorName.Contains(TEXT("Qualcomm")))
		{
			GRHIVendorId = 0x5143;
		}
	}
	return true;
}

bool PlatformOpenGLContextValid()
{
	return AndroidEGL::GetInstance()->IsCurrentContextValid();
}

// =============================================================

void PlatformGetNewOcclusionQuery( GLuint* OutQuery, uint64* OutQueryContext )
{
}

bool PlatformContextIsCurrent( uint64 QueryContext )
{
	return true;
}

void FPlatformOpenGLDevice::LoadEXT()
{
	eglGetSystemTimeNV_p = (PFNEGLGETSYSTEMTIMENVPROC)((void*)eglGetProcAddress("eglGetSystemTimeNV"));
	eglCreateSyncKHR_p = (PFNEGLCREATESYNCKHRPROC)((void*)eglGetProcAddress("eglCreateSyncKHR"));
	eglDestroySyncKHR_p = (PFNEGLDESTROYSYNCKHRPROC)((void*)eglGetProcAddress("eglDestroySyncKHR"));
	eglClientWaitSyncKHR_p = (PFNEGLCLIENTWAITSYNCKHRPROC)((void*)eglGetProcAddress("eglClientWaitSyncKHR"));
	eglGetSyncAttribKHR_p = (PFNEGLGETSYNCATTRIBKHRPROC)((void*)eglGetProcAddress("eglGetSyncAttribKHR"));

	eglPresentationTimeANDROID_p = (PFNeglPresentationTimeANDROID)((void*)eglGetProcAddress("eglPresentationTimeANDROID"));
	eglGetNextFrameIdANDROID_p = (PFNeglGetNextFrameIdANDROID)((void*)eglGetProcAddress("eglGetNextFrameIdANDROID"));
	eglGetCompositorTimingANDROID_p = (PFNeglGetCompositorTimingANDROID)((void*)eglGetProcAddress("eglGetCompositorTimingANDROID"));
	eglGetFrameTimestampsANDROID_p = (PFNeglGetFrameTimestampsANDROID)((void*)eglGetProcAddress("eglGetFrameTimestampsANDROID"));
	eglQueryTimestampSupportedANDROID_p = (PFNeglQueryTimestampSupportedANDROID)((void*)eglGetProcAddress("eglQueryTimestampSupportedANDROID"));
	eglGetCompositorTimingSupportedANDROID_p = (PFNeglQueryTimestampSupportedANDROID)((void*)eglGetProcAddress("eglGetCompositorTimingSupportedANDROID"));
	eglGetFrameTimestampsSupportedANDROID_p = (PFNeglQueryTimestampSupportedANDROID)((void*)eglGetProcAddress("eglGetFrameTimestampsSupportedANDROID"));

	const TCHAR* NotAvailable = TEXT("NOT Available");
	const TCHAR* Present = TEXT("Present");

	UE_LOG(LogRHI, Log, TEXT("Extension %s %s"), TEXT("eglPresentationTimeANDROID"), eglPresentationTimeANDROID_p  ? Present : NotAvailable);
	UE_LOG(LogRHI, Log, TEXT("Extension %s %s"), TEXT("eglGetNextFrameIdANDROID"), eglGetNextFrameIdANDROID_p ? Present : NotAvailable);
	UE_LOG(LogRHI, Log, TEXT("Extension %s %s"), TEXT("eglGetCompositorTimingANDROID"), eglGetCompositorTimingANDROID_p  ? Present : NotAvailable);
	UE_LOG(LogRHI, Log, TEXT("Extension %s %s"), TEXT("eglGetFrameTimestampsANDROID"), eglGetFrameTimestampsANDROID_p  ? Present : NotAvailable);
	UE_LOG(LogRHI, Log, TEXT("Extension %s %s"), TEXT("eglQueryTimestampSupportedANDROID"), eglQueryTimestampSupportedANDROID_p ? Present : NotAvailable);
	UE_LOG(LogRHI, Log, TEXT("Extension %s %s"), TEXT("eglGetCompositorTimingSupportedANDROID"), eglGetCompositorTimingSupportedANDROID_p ? Present : NotAvailable);
	UE_LOG(LogRHI, Log, TEXT("Extension %s %s"), TEXT("eglGetFrameTimestampsSupportedANDROID"), eglGetFrameTimestampsSupportedANDROID_p ? Present : NotAvailable);

	glDebugMessageControlKHR = (PFNGLDEBUGMESSAGECONTROLKHRPROC)((void*)eglGetProcAddress("glDebugMessageControlKHR"));

	// Some PowerVR drivers (Rogue Han and Intel-based devices) are crashing using glDebugMessageControlKHR (causes signal 11 crash)
	if (glDebugMessageControlKHR != NULL && FAndroidMisc::GetGPUFamily().Contains(TEXT("PowerVR")))
	{
		glDebugMessageControlKHR = NULL;
	}

	glDebugMessageInsertKHR = (PFNGLDEBUGMESSAGEINSERTKHRPROC)((void*)eglGetProcAddress("glDebugMessageInsertKHR"));
	glDebugMessageCallbackKHR = (PFNGLDEBUGMESSAGECALLBACKKHRPROC)((void*)eglGetProcAddress("glDebugMessageCallbackKHR"));
	glDebugMessageLogKHR = (PFNGLGETDEBUGMESSAGELOGKHRPROC)((void*)eglGetProcAddress("glDebugMessageLogKHR"));
	glGetPointervKHR = (PFNGLGETPOINTERVKHRPROC)((void*)eglGetProcAddress("glGetPointervKHR"));
	glPushDebugGroupKHR = (PFNGLPUSHDEBUGGROUPKHRPROC)((void*)eglGetProcAddress("glPushDebugGroupKHR"));
	glPopDebugGroupKHR = (PFNGLPOPDEBUGGROUPKHRPROC)((void*)eglGetProcAddress("glPopDebugGroupKHR"));
	glObjectLabelKHR = (PFNGLOBJECTLABELKHRPROC)((void*)eglGetProcAddress("glObjectLabelKHR"));
	glGetObjectLabelKHR = (PFNGLGETOBJECTLABELKHRPROC)((void*)eglGetProcAddress("glGetObjectLabelKHR"));
	glObjectPtrLabelKHR = (PFNGLOBJECTPTRLABELKHRPROC)((void*)eglGetProcAddress("glObjectPtrLabelKHR"));
	glGetObjectPtrLabelKHR = (PFNGLGETOBJECTPTRLABELKHRPROC)((void*)eglGetProcAddress("glGetObjectPtrLabelKHR"));
}

FPlatformOpenGLContext* PlatformCreateOpenGLContext(FPlatformOpenGLDevice* Device, void* InWindowHandle)
{
	//Assumes Device is already initialized and context already created.
	return AndroidEGL::GetInstance()->GetRenderingContext();
}

void PlatformDestroyOpenGLContext(FPlatformOpenGLDevice* Device, FPlatformOpenGLContext* Context)
{
}

FOpenGLTexture* PlatformCreateBuiltinBackBuffer(FOpenGLDynamicRHI* OpenGLRHI, uint32 SizeX, uint32 SizeY)
{
	check(IsInRenderingThread());
	// Create the built-in back buffer if we disable backbuffer sampling.
	// Otherwise return null and we will create an off-screen surface afterward.
	if (!AndroidEGL::GetInstance()->IsOfflineSurfaceRequired())
	{
		const FOpenGLTextureCreateDesc CreateDesc =
			FRHITextureCreateDesc::Create2D(TEXT("PlatformCreateBuiltinBackBuffer"), SizeX, SizeY, PF_B8G8R8A8)
			.SetFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::Presentable | ETextureCreateFlags::ResolveTargetable)
			.DetermineInititialState();

		return new FOpenGLTexture(FRHICommandListImmediate::Get(), CreateDesc);
	}
	else
	{
		return nullptr;
	}
}

void PlatformResizeGLContext( FPlatformOpenGLDevice* Device, FPlatformOpenGLContext* Context, uint32 SizeX, uint32 SizeY, bool bFullscreen, bool bWasFullscreen, GLenum BackBufferTarget, GLuint BackBufferResource)
{
	check(Context);
	VERIFY_GL_SCOPE();

	Context->BackBufferResource = BackBufferResource;
	Context->BackBufferTarget = BackBufferTarget;

	if (AndroidEGL::GetInstance()->IsOfflineSurfaceRequired())
	{
		Device->TargetDirty = true;
		glBindFramebuffer(GL_FRAMEBUFFER, Context->ViewportFramebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, BackBufferTarget, BackBufferResource, 0);
	}

	glViewport(0, 0, SizeX, SizeY);
	VERIFY_GL(glViewport);
}

void PlatformGetSupportedResolution(uint32 &Width, uint32 &Height)
{
}

bool PlatformGetAvailableResolutions(FScreenResolutionArray& Resolutions, bool bIgnoreRefreshRate)
{
	return true;
}

int32 PlatformGlGetError()
{
	return glGetError();
}

// =============================================================

void PlatformReleaseOcclusionQuery( GLuint Query, uint64 QueryContext )
{
}

void FPlatformOpenGLDevice::SetCurrentSharedContext()
{
	AndroidEGL::GetInstance()->SetCurrentSharedContext();
}

void PlatformDestroyOpenGLDevice(FPlatformOpenGLDevice* Device)
{
	delete Device;
}

void FPlatformOpenGLDevice::SetCurrentRenderingContext()
{
	AndroidEGL::GetInstance()->AcquireCurrentRenderingContext();
}

void FPlatformOpenGLDevice::SetupCurrentContext()
{
	GLuint* DefaultVao = nullptr;
	EOpenGLCurrentContext ContextType = (EOpenGLCurrentContext)AndroidEGL::GetInstance()->GetCurrentContextType();

	if (ContextType == CONTEXT_Rendering)
	{
		DefaultVao = &AndroidEGL::GetInstance()->GetRenderingContext()->DefaultVertexArrayObject;
	}
	else if (ContextType == CONTEXT_Shared)
	{
		DefaultVao = &AndroidEGL::GetInstance()->GetSharedContext()->DefaultVertexArrayObject;
	}
	else
	{
		//Invalid or Other return
		return;
	}
	
	if (*DefaultVao == 0)
	{
		glGenVertexArrays(1, DefaultVao);
		glBindVertexArray(*DefaultVao);
	}
}

//--------------------------------

bool FAndroidOpenGL::SupportsFramebufferSRGBEnable()
{	
	static auto* MobileUseHWsRGBEncodingCVAR = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.UseHWsRGBEncoding"));
	const bool bMobileUseHWsRGBEncoding = (MobileUseHWsRGBEncodingCVAR && MobileUseHWsRGBEncodingCVAR->GetValueOnAnyThread() == 1);
	return bMobileUseHWsRGBEncoding;
}

FAndroidOpenGL::EImageExternalType FAndroidOpenGL::ImageExternalType = FAndroidOpenGL::EImageExternalType::None;

extern bool AndroidThunkCpp_GetMetaDataBoolean(const FString& Key);
extern FString AndroidThunkCpp_GetMetaDataString(const FString& Key);

void FAndroidOpenGL::SetupDefaultGLContextState(const FString& ExtensionsString)
{
	// Enable QCOM non-coherent framebuffer fetch if supported
	if (CVarDisableFBFNonCoherent.GetValueOnAnyThread() == 0 &&
		ExtensionsString.Contains(TEXT("GL_QCOM_shader_framebuffer_fetch_noncoherent")) && 
		ExtensionsString.Contains(TEXT("GL_EXT_shader_framebuffer_fetch")))
	{
		bDefaultStateNonCoherentFramebufferFetchEnabled = true;
		glEnable(GL_FRAMEBUFFER_FETCH_NONCOHERENT_QCOM);
	}
}

bool FAndroidOpenGL::RequiresAdrenoTilingModeHint()
{
	return bRequiresAdrenoTilingHint;
}

void FAndroidOpenGL::EnableAdrenoTilingModeHint(bool bEnable)
{
	if(bEnable && CVarEnableAdrenoTilingHint.GetValueOnAnyThread() != 0)
	{
		glEnable(GL_BINNING_CONTROL_HINT_QCOM);
		glHint(GL_BINNING_CONTROL_HINT_QCOM, GL_GPU_OPTIMIZED_QCOM);
	}
	else
	{
		glDisable(GL_BINNING_CONTROL_HINT_QCOM);
	}
}

bool FAndroidOpenGL::ResetNonCoherentFramebufferFetch()
{
	if (bDefaultStateNonCoherentFramebufferFetchEnabled)
	{
		glEnable(GL_FRAMEBUFFER_FETCH_NONCOHERENT_QCOM);
		return true;
	}
	return false;
}

void FAndroidOpenGL::DisableNonCoherentFramebufferFetch()
{
	if (bDefaultStateNonCoherentFramebufferFetchEnabled)
	{
		glDisable(GL_FRAMEBUFFER_FETCH_NONCOHERENT_QCOM);
	}
}

bool FAndroidOpenGL::bDefaultStateNonCoherentFramebufferFetchEnabled = false;

void FAndroidOpenGL::ProcessExtensions(const FString& ExtensionsString)
{
	FString VersionString = FString(ANSI_TO_TCHAR((const ANSICHAR*)glGetString(GL_VERSION)));
	FString SubVersionString;

	FOpenGLES::ProcessExtensions(ExtensionsString);

	FString RendererString = FString(ANSI_TO_TCHAR((const ANSICHAR*)glGetString(GL_RENDERER)));

	// Common GPU types
	const bool bIsNvidiaBased = RendererString.Contains(TEXT("NVIDIA"));
	const bool bIsPoverVRBased = RendererString.Contains(TEXT("PowerVR"));
	const bool bIsAdrenoBased = RendererString.Contains(TEXT("Adreno"));
	const bool bIsMaliBased = RendererString.Contains(TEXT("Mali"));

	if (bIsPoverVRBased)
	{
		bHasHardwareHiddenSurfaceRemoval = true;
		UE_LOG(LogRHI, Log, TEXT("Enabling support for Hidden Surface Removal on PowerVR"));
	}

	if (bIsAdrenoBased)
	{
		uint32 AdrenoDriverMajorVersion = 0;
		FStringView VersionStringView = MakeStringView(VersionString);
		FStringView AdrenoDriverVersionPrefix = TEXT("V@");
		int32 VersionStart = UE::String::FindFirst(VersionStringView, AdrenoDriverVersionPrefix, ESearchCase::CaseSensitive);
		if (VersionStart != INDEX_NONE)
		{
			VersionStart += AdrenoDriverVersionPrefix.Len();
			const int32 VersionCharCount = UE::String::FindFirst(VersionStringView.Mid(VersionStart), TEXT("."), ESearchCase::CaseSensitive);
			if (VersionCharCount != INDEX_NONE)
			{
				LexFromString(AdrenoDriverMajorVersion, VersionStringView.SubStr(VersionStart, VersionCharCount));
			}
		}

		GRHIMaximumInFlightQueries = 510;
		// This is to avoid a bug in Adreno drivers that define GL_ARM_shader_framebuffer_fetch_depth_stencil even when device does not support this extension
		// OpenGL ES 3.1 V@127.0 (GIT@I1af360237c)
		bRequiresARMShaderFramebufferFetchDepthStencilUndef = !bSupportsShaderDepthStencilFetch;

		if (AdrenoDriverMajorVersion > 0 && AdrenoDriverMajorVersion < 331)
		{
			// Shader compiler causes a freeze on older drivers
			// version 331 is known to work, 313 known not to work
			bSupportsShaderFramebufferFetchProgrammableBlending = false;
		}

		// FORT-221329's broken adreno driver not common on Android 9 and above. TODO: check adreno driver version instead.
		bRequiresAdrenoTilingHint = FAndroidMisc::GetAndroidBuildVersion() < 28 || CVarEnableAdrenoTilingHint.GetValueOnAnyThread() == 2;
		UE_CLOG(bRequiresAdrenoTilingHint, LogRHI, Log, TEXT("Enabling Adreno tiling hint."));
	}

	if (bIsMaliBased)
	{
		//TODO restrict this to problematic drivers only
		bRequiresReadOnlyBuffersWorkaround = true;
	}

	// Disable ASTC if requested by device profile
	static const auto CVarDisableASTC = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Android.DisableASTCSupport"));
	if (bSupportsASTC && CVarDisableASTC->GetValueOnAnyThread())
	{
		bSupportsASTC = false;
		bSupportsASTCHDR = false;
		FAndroidGPUInfo::Get().RemoveTargetPlatform(TEXT("Android_ASTC"));
		UE_LOG(LogRHI, Log, TEXT("ASTC was disabled via r.OpenGL.DisableASTCSupport"));
	}
	
	// Check for external image support for different ES versions
	ImageExternalType = EImageExternalType::None;

	static const auto CVarOverrideExternalTextureSupport = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Android.OverrideExternalTextureSupport"));
	const int32 OverrideExternalTextureSupport = CVarOverrideExternalTextureSupport->GetValueOnAnyThread();
	switch (OverrideExternalTextureSupport)
	{
	case 1:
		ImageExternalType = EImageExternalType::None;
		break;

	case 2:
		ImageExternalType = EImageExternalType::ImageExternal100;
		break;

	case 3:
		ImageExternalType = EImageExternalType::ImageExternal300;
		break;

	case 4:
		ImageExternalType = EImageExternalType::ImageExternalESSL300;
		break;

	case 0:
	default:
		// auto-detect by extensions (default)
		bool bHasImageExternal = ExtensionsString.Contains(TEXT("GL_OES_EGL_image_external ")) || ExtensionsString.EndsWith(TEXT("GL_OES_EGL_image_external"));
		bool bHasImageExternalESSL3 = ExtensionsString.Contains(TEXT("OES_EGL_image_external_essl3"));
		if (bHasImageExternal || bHasImageExternalESSL3)
		{
			ImageExternalType = EImageExternalType::ImageExternal100;
			if (bHasImageExternalESSL3)
			{
				ImageExternalType = EImageExternalType::ImageExternalESSL300;
			}
			else
			{
				// Adreno 5xx can do essl3 even without extension in list
				if (bIsAdrenoBased && RendererString.Contains(TEXT("(TM) 5")))
				{
					ImageExternalType = EImageExternalType::ImageExternalESSL300;
				}
			}
			
			if (bIsNvidiaBased)
			{
				// Nvidia needs version 100 even though it supports ES3
				ImageExternalType = EImageExternalType::ImageExternal100;
			}
		}
		break;
	}
	switch (ImageExternalType)
	{
	case EImageExternalType::None:
		UE_LOG(LogRHI, Log, TEXT("Image external disabled"));
		break;

	case EImageExternalType::ImageExternal100:
		UE_LOG(LogRHI, Log, TEXT("Image external enabled: ImageExternal100"));
		break;

	case EImageExternalType::ImageExternal300:
		UE_LOG(LogRHI, Log, TEXT("Image external enabled: ImageExternal300"));
		break;

	case EImageExternalType::ImageExternalESSL300:
		UE_LOG(LogRHI, Log, TEXT("Image external enabled: ImageExternalESSL300"));
		break;

	default:
		ImageExternalType = EImageExternalType::None;
		UE_LOG(LogRHI, Log, TEXT("Image external disabled; unknown type"));
	}
	bSupportsImageExternal = ImageExternalType != EImageExternalType::None;

	// check for supported texture formats if enabled
	bool bCookOnTheFly = false;
#if !UE_BUILD_SHIPPING
	FString FileHostIP;
	bCookOnTheFly = FParse::Value(FCommandLine::Get(), TEXT("filehostip"), FileHostIP);
#endif
	if (!bCookOnTheFly && AndroidThunkCpp_GetMetaDataBoolean(TEXT("com.epicgames.unreal.GameActivity.bValidateTextureFormats")))
	{
		FString CookedFlavorsString = AndroidThunkCpp_GetMetaDataString(TEXT("com.epicgames.unreal.GameActivity.CookedFlavors"));
		if (!CookedFlavorsString.IsEmpty())
		{
			TArray<FString> CookedFlavors;
			CookedFlavorsString.ParseIntoArray(CookedFlavors, TEXT(","), true);

			// check each cooked flavor for support (only need one to be supported)
			bool bFoundSupported = false;
			for (FString Flavor : CookedFlavors)
			{
				if (Flavor.Equals(TEXT("ETC2")))
				{
					if (FOpenGL::SupportsETC2())
					{
						bFoundSupported = true;
						break;
					}
				}
				if (Flavor.Equals(TEXT("DXT")))
				{
					if (FOpenGL::SupportsDXT())
					{
						bFoundSupported = true;
						break;
					}
				}
				if (Flavor.Equals(TEXT("ASTC")))
				{
					if (FOpenGL::SupportsASTC())
					{
						bFoundSupported = true;
						break;
					}
				}
			}

			if (!bFoundSupported)
			{
				FString Message = TEXT("Cooked Flavors: ") + CookedFlavorsString + TEXT("\n\nSupported: ETC2") +
					(FOpenGL::SupportsDXT() ? TEXT(",DXT") : TEXT("")) +
					(FOpenGL::SupportsASTC() ? TEXT(",ASTC") : TEXT(""));

				FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Error: Unsupported Texture Format\n%s"), *Message);
				FAndroidMisc::MessageBoxExt(EAppMsgType::Ok, *Message, TEXT("Unsupported Texture Format"));
			}
		}
	}

	// Qualcomm non-coherent framebuffer_fetch
	if (CVarDisableFBFNonCoherent.GetValueOnAnyThread() == 0 &&
		ExtensionsString.Contains(TEXT("GL_QCOM_shader_framebuffer_fetch_noncoherent")) && 
		ExtensionsString.Contains(TEXT("GL_EXT_shader_framebuffer_fetch")))
	{
		glFramebufferFetchBarrierQCOM = (PFNGLFRAMEBUFFERFETCHBARRIERQCOMPROC)((void*)eglGetProcAddress("glFramebufferFetchBarrierQCOM"));
		if (glFramebufferFetchBarrierQCOM != nullptr)
		{
			UE_LOG(LogRHI, Log, TEXT("Using QCOM_shader_framebuffer_fetch_noncoherent"));
		}
	}

	if (CVarDisableEarlyFragmentTests.GetValueOnAnyThread() != 0)
	{
		bRequiresDisabledEarlyFragmentTests = true;
		UE_LOG(LogRHI, Log, TEXT("Disabling early_fragment_tests"));
	}
}

namespace AndroidOGLService
{
	std::atomic<bool> GRemoteCompileServicesStarted = false;
	std::atomic<bool> GRemoteCompileServicesActive = false;
	std::atomic<bool> bOneTimeErrorEncountered = false;
	std::atomic<int> TotalErrors = 0;
}
extern bool AreAndroidOpenGLRemoteCompileServicesAvailable();

bool FAndroidOpenGL::AreRemoteCompileServicesActive()
{
	// The services could be stopped at any point elsewhere, the return value is not guaranteed to be correct.
	// it does not need to be exact as the PSO service will reject any new requests after service stop has been encountered.
	// any existing PSOservice jobs will complete as normal.
	if (AndroidOGLService::GRemoteCompileServicesStarted && AreAndroidOpenGLRemoteCompileServicesAvailable())
	{
		if (!AndroidOGLService::GRemoteCompileServicesActive)
		{
			JNIEnv* Env = FAndroidApplication::GetJavaEnv();
			AndroidOGLService::GRemoteCompileServicesActive = (bool)Env->CallStaticBooleanMethod(OpenGLRemoteGLProgramCompileJNI.OGLServiceAccessor, OpenGLRemoteGLProgramCompileJNI.AreProgramServicesReady);
			if (!AndroidOGLService::GRemoteCompileServicesActive)
			{
				if ((bool)Env->CallStaticBooleanMethod(OpenGLRemoteGLProgramCompileJNI.OGLServiceAccessor, OpenGLRemoteGLProgramCompileJNI.HaveServicesFailed))
				{
					UE_LOG(LogRHI, Error, TEXT("Remote compile services failed to start."));
					StopRemoteCompileServices();
				}
			}
			else
			{
				UE_LOG(LogRHI, Log, TEXT("Remote compile services are active."));
			}
		}
		return AndroidOGLService::GRemoteCompileServicesActive;
	}
	return false;
}

bool FAndroidOpenGL::StartRemoteCompileServices(int NumServices)
{
	JNIEnv* Env = FAndroidApplication::GetJavaEnv();

	if (Env && AreAndroidOpenGLRemoteCompileServicesAvailable() && !AndroidOGLService::GRemoteCompileServicesStarted)
	{
		bool bUseRobustContexts = AndroidEGL::GetInstance()->IsUsingRobustContext();
		AndroidOGLService::GRemoteCompileServicesStarted = (bool)Env->CallStaticBooleanMethod(OpenGLRemoteGLProgramCompileJNI.OGLServiceAccessor, OpenGLRemoteGLProgramCompileJNI.StartRemoteProgramLink, (jint)NumServices, (jboolean)bUseRobustContexts, /*bUseVulkan*/(jboolean)false);
	}

	return AndroidOGLService::GRemoteCompileServicesStarted;
}

void FAndroidOpenGL::StopRemoteCompileServices()
{
	bool bExpected = true;
	if (AndroidOGLService::GRemoteCompileServicesStarted.compare_exchange_strong(bExpected, false))
	{
		UE_LOG(LogRHI, Log, TEXT("Stopping Remote Compile Services"));
		AndroidOGLService::GRemoteCompileServicesActive = false;
		JNIEnv* Env = FAndroidApplication::GetJavaEnv();

		if (Env && ensure(AreAndroidOpenGLRemoteCompileServicesAvailable()))
		{
			Env->CallStaticVoidMethod(OpenGLRemoteGLProgramCompileJNI.OGLServiceAccessor, OpenGLRemoteGLProgramCompileJNI.StopRemoteProgramLink);
		}
	}
}

TArray<uint8> FAndroidOpenGL::DispatchAndWaitForRemoteGLProgramCompile(FGraphicsPipelineStateInitializer::EPSOPrecacheCompileType PSOCompileType, const TArrayView<uint8> ContextData, const TArray<ANSICHAR>& VertexGlslCode, const TArray<ANSICHAR>& PixelGlslCode, const TArray<ANSICHAR>& ComputeGlslCode, FString& FailureMessageOUT)
{
	bool bResult = false;
	JNIEnv* Env = FAndroidApplication::GetJavaEnv();
	TArray<uint8> CompiledProgramBinary;
	FString ErrorMessage;

	if (Env && ensure(AndroidOGLService::GRemoteCompileServicesActive) && ensure(AreAndroidOpenGLRemoteCompileServicesAvailable()))
	{
		// todo: double conversion :(
		auto jVS = NewScopedJavaObject(Env, Env->NewStringUTF(TCHAR_TO_UTF8(ANSI_TO_TCHAR(VertexGlslCode.IsEmpty() ? "" : VertexGlslCode.GetData()))));
		auto jPS = NewScopedJavaObject(Env, Env->NewStringUTF(TCHAR_TO_UTF8(ANSI_TO_TCHAR(PixelGlslCode.IsEmpty() ? "" : PixelGlslCode.GetData()))));
		auto jCS = NewScopedJavaObject(Env, Env->NewStringUTF(TCHAR_TO_UTF8(ANSI_TO_TCHAR(ComputeGlslCode.IsEmpty() ? "" : ComputeGlslCode.GetData()))));
		auto ProgramKeyBuffer = NewScopedJavaObject(Env, Env->NewByteArray(ContextData.Num()));
		Env->SetByteArrayRegion(*ProgramKeyBuffer, 0, ContextData.Num(), reinterpret_cast<const jbyte*>(ContextData.GetData()));
		// dont time out if the debugger is attached.
		bool bEnableTimeOuts = !FPlatformMisc::IsDebuggerPresent();
		FPlatformDynamicRHI::FPSOServicePriInfo PriorityInfo(PSOCompileType);
		auto ProgramResponseObj = NewScopedJavaObject(Env, Env->CallStaticObjectMethod(OpenGLRemoteGLProgramCompileJNI.OGLServiceAccessor, OpenGLRemoteGLProgramCompileJNI.DispatchProgramLink, *ProgramKeyBuffer, PriorityInfo.GetPriorityInfo(), *jVS, *jPS, *jCS, bEnableTimeOuts));
 		CHECK_JNI_EXCEPTIONS(Env);

		if(*ProgramResponseObj)
		{
			bool bSucceeded = (bool)Env->GetBooleanField(*ProgramResponseObj, OpenGLRemoteGLProgramCompileJNI.ProgramResponse_SuccessField);
			if (bSucceeded)
			{
				auto ProgramResult = NewScopedJavaObject(Env, (jbyteArray)Env->GetObjectField(*ProgramResponseObj, OpenGLRemoteGLProgramCompileJNI.ProgramResponse_CompiledBinaryField));
				int len = Env->GetArrayLength(*ProgramResult);
				CompiledProgramBinary.SetNumUninitialized(len);
				Env->GetByteArrayRegion(*ProgramResult, 0, len, reinterpret_cast<jbyte*>(CompiledProgramBinary.GetData()));
				float CompilationDuration = (float)Env->GetFloatField(*ProgramResponseObj, OpenGLRemoteGLProgramCompileJNI.ProgramResponse_CompilationDurationField);
				AccumulatePSOMetrics(CompilationDuration);
			}
			else
			{
				if (AndroidOGLService::bOneTimeErrorEncountered.exchange(true) == false)
				{
					FGenericCrashContext::SetEngineData(TEXT("Android.PSOService"), TEXT("ec"));
				}

				FailureMessageOUT = FJavaHelper::FStringFromLocalRef(Env, (jstring)Env->GetObjectField(*ProgramResponseObj, OpenGLRemoteGLProgramCompileJNI.ProgramResponse_ErrorField));
				check(!FailureMessageOUT.IsEmpty());
			}
		}
		else
		{
			if (AndroidOGLService::bOneTimeErrorEncountered.exchange(true) == false)
			{
				FGenericCrashContext::SetEngineData(TEXT("Android.PSOService"), TEXT("es"));
			}
			FailureMessageOUT = TEXT("Remote compiler failed.");
		}

		if(CompiledProgramBinary.IsEmpty())
		{
			check(!FailureMessageOUT.IsEmpty());
			if ((AndroidOGLService::TotalErrors++) == FPlatformDynamicRHI::GetPSOServiceFailureThreshold())
			{
				StopRemoteCompileServices();
				FailureMessageOUT = TEXT("Remote compiler passed failure threshold, disabling further remote compiles.");
			}
		}
	}
	return CompiledProgramBinary;
}


#endif
