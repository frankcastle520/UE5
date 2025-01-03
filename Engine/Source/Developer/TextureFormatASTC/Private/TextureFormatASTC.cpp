// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/SharedString.h"
#include "DerivedDataBuildFunctionFactory.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "Interfaces/ITextureFormat.h"
#include "Interfaces/ITextureFormatModule.h"
#include "Modules/ModuleManager.h"
#include "PixelFormat.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"
#include "TextureBuildFunction.h"
#include "TextureCompressorModule.h"

#include "astcenc.h"

/****
* 
* TextureFormatASTC runs the ARM astcenc
* 
* or redirects to Intel ISPC texcomp* 
* 
*****/

// when GASTCCompressor == 0 ,use TextureFormatIntelISPCTexComp instead of this
// @todo Oodle : GASTCCompressor global breaks DDC2.  Need to pass through so TBW can see.
int32 GASTCCompressor = 1;
static FAutoConsoleVariableRef CVarASTCCompressor(
	TEXT("cook.ASTCTextureCompressor"),
	GASTCCompressor,
	TEXT("0: IntelISPC, 1: Arm"),
	ECVF_Default | ECVF_ReadOnly
);

#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC
	#define SUPPORTS_ISPC_ASTC	1
#else
	#define SUPPORTS_ISPC_ASTC	0
#endif

// increment this if you change anything that will affect compression in this file
#define BASE_ASTC_FORMAT_VERSION 48

#define MAX_QUALITY_BY_SIZE 4
#define MAX_QUALITY_BY_SPEED 3

/**

"Quality" in this file is ETextureCompressionQuality-1

so a "3" here == High == 6x6

enum ETextureCompressionQuality : int
{
	TCQ_Default = 0		UMETA(DisplayName="Default"),
	TCQ_Lowest = 1		UMETA(DisplayName="Lowest (ASTC 12x12)"),
	TCQ_Low = 2			UMETA(DisplayName="Low (ASTC 10x10)"),
	TCQ_Medium = 3		UMETA(DisplayName="Medium (ASTC 8x8)"),
	TCQ_High= 4			UMETA(DisplayName="High (ASTC 6x6)"),
	TCQ_Highest = 5		UMETA(DisplayName="Highest (ASTC 4x4)"),
	TCQ_MAX,
};


**/


DEFINE_LOG_CATEGORY_STATIC(LogTextureFormatASTC, Log, All);

class FASTCTextureBuildFunction final : public FTextureBuildFunction
{
	const UE::FUtf8SharedString& GetName() const final
	{
		static const UE::FUtf8SharedString Name(UTF8TEXTVIEW("ASTCTexture"));
		return Name;
	}

	void GetVersion(UE::DerivedData::FBuildVersionBuilder& Builder, ITextureFormat*& OutTextureFormatVersioning) const final
	{
		static FGuid Version(TEXT("4788dab5-b99c-479f-bc34-6d7df1cf30e5"));
		Builder << Version;
		OutTextureFormatVersioning = FModuleManager::GetModuleChecked<ITextureFormatModule>(TEXT("TextureFormatASTC")).GetTextureFormat();
	}
};

/**
 * Macro trickery for supported format names.
 */
#define ENUM_SUPPORTED_FORMATS(op) \
	op(ASTC_RGB) \
	op(ASTC_RGBA) \
	op(ASTC_RGBAuto) \
	op(ASTC_RGBA_HQ) \
	op(ASTC_RGB_HDR) \
	op(ASTC_NormalLA) \
	op(ASTC_NormalAG) \
	op(ASTC_NormalRG) \
	op(ASTC_NormalRG_Precise) // Encoded as LA for precision, mapped to RG at runtime. RHI needs to support PF_ASTC_*_NORM_RG formats (requires runtime swizzle)

	#define DECL_FORMAT_NAME(FormatName) static FName GTextureFormatName##FormatName = FName(TEXT(#FormatName));
		ENUM_SUPPORTED_FORMATS(DECL_FORMAT_NAME);
	#undef DECL_FORMAT_NAME

	#define DECL_FORMAT_NAME_ENTRY(FormatName) GTextureFormatName##FormatName ,
		static FName GSupportedTextureFormatNames[] =
		{
			ENUM_SUPPORTED_FORMATS(DECL_FORMAT_NAME_ENTRY)
		};
	#undef DECL_FORMAT_NAME_ENTRY
#undef ENUM_SUPPORTED_FORMATS

// ASTC file header format
#if PLATFORM_SUPPORTS_PRAGMA_PACK
	#pragma pack(push, 4)
#endif

	#define ASTC_MAGIC_CONSTANT 0x5CA1AB13
	struct FASTCHeader
	{
		uint32 Magic;
		uint8  BlockSizeX;
		uint8  BlockSizeY;
		uint8  BlockSizeZ;
		uint8  TexelCountX[3];
		uint8  TexelCountY[3];
		uint8  TexelCountZ[3];
	};

#if PLATFORM_SUPPORTS_PRAGMA_PACK
	#pragma pack(pop)
#endif

static bool IsNormalMapFormat(FName TextureFormatName)
{
	return
		TextureFormatName == GTextureFormatNameASTC_NormalAG ||
		TextureFormatName == GTextureFormatNameASTC_NormalRG ||
		TextureFormatName == GTextureFormatNameASTC_NormalLA ||
		TextureFormatName == GTextureFormatNameASTC_NormalRG_Precise;
}

static int32 GetDefaultCompressionBySizeValue(FCbObjectView InFormatConfigOverride)
{
	// this is code duped between TextureFormatASTC and TextureFormatISPC
	if (InFormatConfigOverride)
	{
		// If we have an explicit format config, then use it directly
		FCbFieldView FieldView = InFormatConfigOverride.FindView("DefaultASTCQualityBySize");
		checkf(FieldView.HasValue(), TEXT("Missing DefaultASTCQualityBySize key from FormatConfigOverride"));
		int32 CompressionModeValue = FieldView.AsInt32();
		checkf(!FieldView.HasError(), TEXT("Failed to parse DefaultASTCQualityBySize value from FormatConfigOverride"));
		return CompressionModeValue;
	}
	else
	{
		// default of 3 == 6x6

		auto GetCompressionModeValue = []() {
			// start at default quality, then lookup in .ini file
			int32 CompressionModeValue = 3;
			GConfig->GetInt(TEXT("/Script/UnrealEd.CookerSettings"), TEXT("DefaultASTCQualityBySize"), CompressionModeValue, GEngineIni);
	
			FParse::Value(FCommandLine::Get(), TEXT("-astcqualitybysize="), CompressionModeValue);
			
			return FMath::Min<uint32>(CompressionModeValue, MAX_QUALITY_BY_SIZE);
		};

		static int32 CompressionModeValue = GetCompressionModeValue();

		return CompressionModeValue;
	}
}

static int32 GetDefaultCompressionBySizeValueHQ(FCbObjectView InFormatConfigOverride)
{
	// this is code duped between TextureFormatASTC and TextureFormatISPC
	if (InFormatConfigOverride)
	{
		// If we have an explicit format config, then use it directly
		FCbFieldView FieldView = InFormatConfigOverride.FindView("DefaultASTCQualityBySizeHQ");
		checkf(FieldView.HasValue(), TEXT("Missing DefaultASTCQualityBySizeHQ key from FormatConfigOverride"));
		int32 CompressionModeValue = FieldView.AsInt32();
		checkf(!FieldView.HasError(), TEXT("Failed to parse DefaultASTCQualityBySizeHQ value from FormatConfigOverride"));
		return CompressionModeValue;
	}
	else
	{
		// default of 4 == 4x4

		auto GetCompressionModeValue = []() {
			// start at default quality, then lookup in .ini file
			int32 CompressionModeValue = 4;
			GConfig->GetInt(TEXT("/Script/UnrealEd.CookerSettings"), TEXT("DefaultASTCQualityBySizeHQ"), CompressionModeValue, GEngineIni);
	
			FParse::Value(FCommandLine::Get(), TEXT("-astcqualitybysizehq="), CompressionModeValue);
			
			return FMath::Min<uint32>(CompressionModeValue, MAX_QUALITY_BY_SIZE);
		};

		static int32 CompressionModeValue = GetCompressionModeValue();

		return CompressionModeValue;
	}
}

static int32 GetDefaultCompressionBySpeedValue(FCbObjectView InFormatConfigOverride)
{
	if (InFormatConfigOverride)
	{
		// If we have an explicit format config, then use it directly
		FCbFieldView FieldView = InFormatConfigOverride.FindView("DefaultASTCQualityBySpeed");
		checkf(FieldView.HasValue(), TEXT("Missing DefaultASTCQualityBySpeed key from FormatConfigOverride"));
		int32 CompressionModeValue = FieldView.AsInt32();
		checkf(!FieldView.HasError(), TEXT("Failed to parse DefaultASTCQualityBySpeed value from FormatConfigOverride"));
		return CompressionModeValue;
	}
	else
	{

		// default of 2 == ASTCENC_PRE_MEDIUM

		auto GetCompressionModeValue = []() {
			// start at default quality, then lookup in .ini file
			int32 CompressionModeValue = 2;
			GConfig->GetInt(TEXT("/Script/UnrealEd.CookerSettings"), TEXT("DefaultASTCQualityBySpeed"), CompressionModeValue, GEngineIni);
	
			FParse::Value(FCommandLine::Get(), TEXT("-astcqualitybyspeed="), CompressionModeValue);
			
			return FMath::Min<uint32>(CompressionModeValue, MAX_QUALITY_BY_SPEED);
		};

		static int32 CompressionModeValue = GetCompressionModeValue();

		return CompressionModeValue;
	}
}

static EPixelFormat GetQualityFormat(const FTextureBuildSettings& BuildSettings)
{
	// code dupe between TextureFormatASTC  and TextureFormatISPC

	const FCbObjectView& InFormatConfigOverride = BuildSettings.FormatConfigOverride;
	int32 OverrideSizeValue= BuildSettings.CompressionQuality;

	bool bIsNormalMap = IsNormalMapFormat(BuildSettings.TextureFormatName);

	if ( bIsNormalMap )
	{
		// normal map hard coded to always use 6x6 currently
		//	ignores per-texture quality

		if ( BuildSettings.TextureFormatName == GTextureFormatNameASTC_NormalRG_Precise )
		{
			return PF_ASTC_6x6_NORM_RG;
		}
		else
		{
			return PF_ASTC_6x6;
		}
	}
	else if (BuildSettings.bVirtualStreamable)
	{
		return PF_ASTC_4x4;		
	}

	// CompressionQuality value here is ETextureCompressionQuality minus 1
	
	bool bIsHQ = BuildSettings.TextureFormatName == GTextureFormatNameASTC_RGBA_HQ;
	bool bHDRFormat = BuildSettings.TextureFormatName == GTextureFormatNameASTC_RGB_HDR;
	
	if ( OverrideSizeValue < 0 )
	{
		if ( bIsHQ )
		{
			OverrideSizeValue = GetDefaultCompressionBySizeValueHQ(InFormatConfigOverride);
		}
		else
		{
			OverrideSizeValue = GetDefaultCompressionBySizeValue(InFormatConfigOverride);
		}
	}

	// convert to a string
	EPixelFormat Format = PF_Unknown;
	if (bHDRFormat)
	{
		switch (OverrideSizeValue)
		{
			case 0:	Format = PF_ASTC_12x12_HDR; break;
			case 1:	Format = PF_ASTC_10x10_HDR; break;
			case 2:	Format = PF_ASTC_8x8_HDR; break;
			case 3:	Format = PF_ASTC_6x6_HDR; break;
			case 4:	Format = PF_ASTC_4x4_HDR; break;
			default: UE_LOG(LogTextureFormatASTC, Fatal, TEXT("Max quality higher than expected"));
		}
	}
	else
	{
		switch (OverrideSizeValue)
		{
			case 0:	Format = PF_ASTC_12x12; break;
			case 1:	Format = PF_ASTC_10x10; break;
			case 2:	Format = PF_ASTC_8x8; break;
			case 3:	Format = PF_ASTC_6x6; break;
			case 4:	Format = PF_ASTC_4x4; break;
			default: UE_LOG(LogTextureFormatASTC, Fatal, TEXT("Max quality higher than expected"));
		}
	}
	return Format;
}

static bool IsASTCPixelFormatHDR(EPixelFormat PF)
{
	switch (PF)
	{
	case PF_ASTC_4x4_HDR:
	case PF_ASTC_6x6_HDR:
	case PF_ASTC_8x8_HDR:
	case PF_ASTC_10x10_HDR:
	case PF_ASTC_12x12_HDR:
		{
			return true;
		}
	}
	return false;
}



static astcenc_swizzle GetDecodeSwizzleForFormat(EPixelFormat InPixelFormat, FName InTextureFormatName)
{
	astcenc_swizzle EncSwizzle{ ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };

	if (IsASTCPixelFormatHDR(InPixelFormat))
	{
		// BC6H, our compressed HDR format on non-ASTC targets, does not support A
		EncSwizzle.a = ASTCENC_SWZ_1;
	}
	else
	{
		// Check for the other variants individually here
		// set everything up with normal (RGBA) swizzles
		if (InTextureFormatName == GTextureFormatNameASTC_NormalAG)
		{
			EncSwizzle.r = ASTCENC_SWZ_A;
			EncSwizzle.g = ASTCENC_SWZ_G;
			EncSwizzle.b = ASTCENC_SWZ_0;
			EncSwizzle.a = ASTCENC_SWZ_1;
}
		else if (InTextureFormatName == GTextureFormatNameASTC_NormalRG)
		{
			EncSwizzle.r = ASTCENC_SWZ_R;
			EncSwizzle.g = ASTCENC_SWZ_G;
			EncSwizzle.b = ASTCENC_SWZ_0;
			EncSwizzle.a = ASTCENC_SWZ_1;
}
		else if (InTextureFormatName == GTextureFormatNameASTC_NormalLA || InTextureFormatName == GTextureFormatNameASTC_NormalRG_Precise)
		{
			EncSwizzle.r = ASTCENC_SWZ_R;
			EncSwizzle.g = ASTCENC_SWZ_A;
			EncSwizzle.b = ASTCENC_SWZ_0;
			EncSwizzle.a = ASTCENC_SWZ_1;
}

		// Finally, last step, because ASTCEnc produces RGBA channel order and we want BGRA for 8-bit formats:
		Swap(EncSwizzle.r, EncSwizzle.b);
	}
	return EncSwizzle;
}

static bool ASTCEnc_Compress(
	const FImage& InImage,
	const FTextureBuildSettings& BuildSettings,
	const FIntVector3& InMip0Dimensions,
	int32 InMip0NumSlicesNoDepth,
	FStringView DebugTexturePathName,
	bool bImageHasAlphaChannel,
	FCompressedImage2D& OutCompressedImage)
{
	bool bHDRImage = BuildSettings.TextureFormatName == GTextureFormatNameASTC_RGB_HDR;
	// DestGamma is how the texture will be bound to GPU
	bool bSRGB = BuildSettings.GetDestGammaSpace() == EGammaSpace::sRGB;
	check( !bHDRImage || !bSRGB );

	// Get Raw Image Data from passed in FImage & convert to BGRA8 or RGBA16F
	// note: wasteful, often copies image to same format
	FImage Image;
	InImage.CopyTo(Image, bHDRImage ? ERawImageFormat::RGBA16F : ERawImageFormat::BGRA8, BuildSettings.GetDestGammaSpace());

	if (bHDRImage)
	{
		// ASTC can encode floats that BC6H can't
		//  but still clamp as if we were BC6H, so that the same output is made
		// (eg. ASTC can encode A but BC6 can't; we stuff 1 in A here)
		FImageCore::SanitizeFloat16AndSetAlphaOpaqueForBC6H(Image);
	}

	bool bIsNormalMap = IsNormalMapFormat(BuildSettings.TextureFormatName);
		
	// Determine the compressed pixel format and compression parameters
	EPixelFormat CompressedPixelFormat = GetQualityFormat(BuildSettings);

	uint32 EncFlags = 0;
	if (bIsNormalMap)
	{
		EncFlags |= ASTCENC_FLG_MAP_NORMAL;
	}

	astcenc_profile EncProfile = (bHDRImage ? ASTCENC_PRF_HDR_RGB_LDR_A : (bSRGB ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR));
	float EncQuality = ASTCENC_PRE_FAST;
	switch (GetDefaultCompressionBySpeedValue(BuildSettings.FormatConfigOverride))
	{
		case 0:	EncQuality = ASTCENC_PRE_FASTEST; break;
		case 1:	EncQuality = ASTCENC_PRE_FAST; break;
		case 2:	EncQuality = ASTCENC_PRE_MEDIUM; break;
		case 3:	EncQuality = ASTCENC_PRE_THOROUGH; break;
		default: UE_LOG(LogTextureFormatASTC, Fatal, TEXT("ASTC speed quality higher than expected"));
	}

	uint32 BlockSizeX = GPixelFormats[CompressedPixelFormat].BlockSizeX;
	uint32 BlockSizeY = GPixelFormats[CompressedPixelFormat].BlockSizeX;
	uint32 BlockSizeZ = 1;

	astcenc_config EncConfig;
	{
		astcenc_error EncStatus = astcenc_config_init(
			EncProfile,
			BlockSizeX,
			BlockSizeX,
			BlockSizeZ,
			EncQuality,
			EncFlags,
			&EncConfig);

		if (EncStatus != ASTCENC_SUCCESS)
		{
			UE_LOG(LogTextureFormatASTC, Error, TEXT("astcenc_config_init has failed: %s"), ANSI_TO_TCHAR(astcenc_get_error_string(EncStatus)));
			return false;
		}
	}

	astcenc_swizzle EncSwizzle { ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };

	if (bHDRImage)
	{
		// BC6H does not support A
		EncSwizzle.a = ASTCENC_SWZ_1;
	}
	else if (BuildSettings.TextureFormatName == GTextureFormatNameASTC_RGB ||
		BuildSettings.TextureFormatName == GTextureFormatNameASTC_RGBA || 
		BuildSettings.TextureFormatName == GTextureFormatNameASTC_RGBAuto || 
		BuildSettings.TextureFormatName == GTextureFormatNameASTC_RGBA_HQ)
	{
		if (BuildSettings.TextureFormatName == GTextureFormatNameASTC_RGB || !bImageHasAlphaChannel)
		{
			// even if Name was RGBA we still use the RGB profile if !bImageHasAlphaChannel
			//	so that "Compress Without Alpha" can force us to opaque

			// we need to set alpha to opaque here
			// can do it using "1" in the bgra swizzle to astcenc
			EncSwizzle.a = ASTCENC_SWZ_1;
		}

		// source is BGRA
		EncSwizzle.r = ASTCENC_SWZ_B;
		EncSwizzle.b = ASTCENC_SWZ_R;
	}
	else if (BuildSettings.TextureFormatName == GTextureFormatNameASTC_NormalAG)
	{
		// note that DXT5n processing does "1g0r"
		EncSwizzle.r = ASTCENC_SWZ_1;
		EncSwizzle.g = ASTCENC_SWZ_G;
		EncSwizzle.b = ASTCENC_SWZ_0;
		EncSwizzle.a = ASTCENC_SWZ_B; // source is BGRA
		
		EncConfig.tune_db_limit = FMath::Max(60.f, EncConfig.tune_db_limit);
		EncConfig.cw_r_weight = 0.0f;
		EncConfig.cw_g_weight = 1.0f;
		EncConfig.cw_b_weight = 0.0f;
		EncConfig.cw_a_weight = 1.0f;
	}
	else if (BuildSettings.TextureFormatName == GTextureFormatNameASTC_NormalRG)
	{
		EncSwizzle.r = ASTCENC_SWZ_B; // source is BGRA
		EncSwizzle.g = ASTCENC_SWZ_G;
		EncSwizzle.b = ASTCENC_SWZ_0;
		EncSwizzle.a = ASTCENC_SWZ_1;

		EncConfig.tune_db_limit = FMath::Max(60.f, EncConfig.tune_db_limit);
		EncConfig.cw_r_weight = 1.0f;
		EncConfig.cw_g_weight = 1.0f;
		EncConfig.cw_b_weight = 0.0f;
		EncConfig.cw_a_weight = 0.0f;
	}
	else if (BuildSettings.TextureFormatName == GTextureFormatNameASTC_NormalLA || BuildSettings.TextureFormatName == GTextureFormatNameASTC_NormalRG_Precise)
	{
		// L+A mode: rrrg
		EncSwizzle.r = ASTCENC_SWZ_B;
		EncSwizzle.g = ASTCENC_SWZ_B;
		EncSwizzle.b = ASTCENC_SWZ_B;
		EncSwizzle.a = ASTCENC_SWZ_G;

		EncConfig.tune_db_limit = FMath::Max(60.f, EncConfig.tune_db_limit);
		EncConfig.cw_r_weight = 1.0f;
		EncConfig.cw_g_weight = 0.0f;
		EncConfig.cw_b_weight = 0.0f;
		EncConfig.cw_a_weight = 1.0f;
	}
	else
	{
		check(false);
	}

	astcenc_context* EncContext = nullptr;
	{
		uint32 EncThreadCount = 1;
		astcenc_error EncStatus = astcenc_context_alloc(&EncConfig, EncThreadCount, &EncContext);
		if (EncStatus != ASTCENC_SUCCESS)
		{
			UE_LOG(LogTextureFormatASTC, Error, TEXT("astcenc_context_alloc has failed: %s"), ANSI_TO_TCHAR(astcenc_get_error_string(EncStatus)));
			return false;
		}
	}

	const int AlignedSizeX = AlignArbitrary(Image.SizeX, BlockSizeX);
	const int AlignedSizeY = AlignArbitrary(Image.SizeY, BlockSizeY);
	const int WidthInBlocks = AlignedSizeX / BlockSizeX;
	const int HeightInBlocks = AlignedSizeY / BlockSizeY;
	const int64 SizePerSlice = (int64)WidthInBlocks * HeightInBlocks * 16;
	OutCompressedImage.RawData.AddUninitialized(SizePerSlice * Image.NumSlices);

	TArray<uint8*, TInlineAllocator<1>> ImageSrcData;
	ImageSrcData.Reserve(Image.NumSlices);

	for (int32 SliceIdx = 0; SliceIdx < Image.NumSlices; SliceIdx++)
	{
		FImageView Slice = Image.GetSlice(SliceIdx);
		uint8* SliceData;
		if (bHDRImage)
		{
			SliceData = (uint8*)Slice.AsRGBA16F().GetData();
		}
		else
		{
			SliceData = (uint8*)Slice.AsBGRA8().GetData();
		}
		ImageSrcData.Add(SliceData);
	}
	
	astcenc_image EncImage;
	EncImage.dim_x = Image.SizeX;
	EncImage.dim_y = Image.SizeY;
	EncImage.dim_z = Image.NumSlices;
	EncImage.data = (void**)ImageSrcData.GetData();
	EncImage.data_type = (bHDRImage ? ASTCENC_TYPE_F16 : ASTCENC_TYPE_U8);

	astcenc_error EncStatus = astcenc_compress_image(
			EncContext,
			&EncImage,
			&EncSwizzle,
			OutCompressedImage.RawData.GetData(),
			OutCompressedImage.RawData.Num(),
			0);
	
	astcenc_context_free(EncContext);

	if (EncStatus == ASTCENC_SUCCESS)
	{
		OutCompressedImage.SizeX = Image.SizeX;
		OutCompressedImage.SizeY = Image.SizeY;
		OutCompressedImage.NumSlicesWithDepth = Image.NumSlices;
		OutCompressedImage.PixelFormat = CompressedPixelFormat;
		return true;
	}
	else
	{
		
		UE_LOG(LogTextureFormatASTC, Error, TEXT("astcenc_compress_image has failed: %s"), ANSI_TO_TCHAR(astcenc_get_error_string(EncStatus)));
		return false;
	}
}


/**
 * ASTC texture format handler.
 */
class FTextureFormatASTC : public ITextureFormat
{
public:
	FTextureFormatASTC()
	:	IntelISPCTexCompFormat(*FModuleManager::LoadModuleChecked<ITextureFormatModule>(TEXT("TextureFormatIntelISPCTexComp")).GetTextureFormat()),
		ImageWrapperModule(FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper")))
	{
		// LoadModule has to be done on Main thread
		// can't be done on-demand in the Compress call
	}

	static FGuid GetDecodeBuildFunctionVersionGuid()
	{
		static FGuid Version(TEXT("0520C2CC-FD1D-48FE-BDCB-4E6E07E01E5B"));
		return Version;
	}
	static FUtf8StringView GetDecodeBuildFunctionNameStatic()
	{
		return UTF8TEXTVIEW("FDecodeTextureFormatASTC");
	}
	virtual const FUtf8StringView GetDecodeBuildFunctionName() const override final
	{
		return GetDecodeBuildFunctionNameStatic();
	}

	virtual bool AllowParallelBuild() const override
	{
#if SUPPORTS_ISPC_ASTC
		if(GASTCCompressor == 0)
		{
			return IntelISPCTexCompFormat.AllowParallelBuild();
		}
#endif
		return true;
	}
	virtual FName GetEncoderName(FName Format) const override
	{
#if SUPPORTS_ISPC_ASTC
		if (GASTCCompressor == 0)
		{
			return IntelISPCTexCompFormat.GetEncoderName(Format);
		}
#endif
		static const FName ASTCName("ArmASTC");
		return ASTCName;
	}

	virtual FCbObject ExportGlobalFormatConfig(const FTextureBuildSettings& BuildSettings) const override
	{
#if SUPPORTS_ISPC_ASTC
		if(GASTCCompressor == 0)
		{
			return IntelISPCTexCompFormat.ExportGlobalFormatConfig(BuildSettings);
		}
#endif
		FCbWriter Writer;
		Writer.BeginObject("TextureFormatASTCSettings");
		Writer.AddInteger("DefaultASTCQualityBySize", GetDefaultCompressionBySizeValue(FCbObjectView()));
		Writer.AddInteger("DefaultASTCQualityBySizeHQ", GetDefaultCompressionBySizeValueHQ(FCbObjectView()));
		Writer.AddInteger("DefaultASTCQualityBySpeed", GetDefaultCompressionBySpeedValue(FCbObjectView()));
		Writer.EndObject();
		return Writer.Save().AsObject();
	}

	// Version for all ASTC textures, whether it's handled by the ARM encoder or the ISPC encoder.
	virtual uint16 GetVersion(
		FName Format,
		const FTextureBuildSettings* BuildSettings = nullptr
	) const override
	{
#if SUPPORTS_ISPC_ASTC
		if(GASTCCompressor == 0)
		{
			// set high bit so version numbers of ISPC and ASTC don't overlap :
			check( BASE_ASTC_FORMAT_VERSION < 0x80 );
			return 0x80 | IntelISPCTexCompFormat.GetVersion(Format,BuildSettings);
		}
#endif

		return BASE_ASTC_FORMAT_VERSION;
	}

	virtual FString GetDerivedDataKeyString(const FTextureBuildSettings& InBuildSettings, int32 InMipCount, const FIntVector3& InMip0Dimensions) const override
	{
#if SUPPORTS_ISPC_ASTC
		if(GASTCCompressor == 0)
		{
			return IntelISPCTexCompFormat.GetDerivedDataKeyString(InBuildSettings, InMipCount, InMip0Dimensions);
		}
#endif

		// ASTC block size chosen is in PixelFormat
		EPixelFormat PixelFormat = GetQualityFormat(InBuildSettings);
		int Speed = GetDefaultCompressionBySpeedValue(InBuildSettings.FormatConfigOverride);

 		return FString::Printf(TEXT("ASTC_%d_%d"), (int)PixelFormat,Speed);
	}

	virtual void GetSupportedFormats(TArray<FName>& OutFormats) const override
	{
		OutFormats.Append(GSupportedTextureFormatNames, sizeof(GSupportedTextureFormatNames)/sizeof(GSupportedTextureFormatNames[0]) ); 
	}

	virtual EPixelFormat GetEncodedPixelFormat(const FTextureBuildSettings& InBuildSettings, bool bInImageHasAlphaChannel) const override
	{
		return GetQualityFormat(InBuildSettings);
	}


	virtual bool CanDecodeFormat(EPixelFormat InPixelFormat) const
	{
		return IsASTCBlockCompressedTextureFormat(InPixelFormat);
	}

	virtual bool DecodeImage(int32 InSizeX, int32 InSizeY, int32 InNumSlices, EPixelFormat InPixelFormat, bool bInSRGB, const FName& InTextureFormatName, FSharedBuffer InEncodedData, FImage& OutImage, FStringView InTextureName) const
	{
		astcenc_swizzle EncSwizzle = GetDecodeSwizzleForFormat(InPixelFormat, InTextureFormatName);
		bool bHDRImage = IsASTCPixelFormatHDR(InPixelFormat);

		astcenc_profile EncProfile = (bHDRImage ? ASTCENC_PRF_HDR_RGB_LDR_A : (bInSRGB ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR));

		uint32 BlockSizeX = GPixelFormats[InPixelFormat].BlockSizeX;
		uint32 BlockSizeY = GPixelFormats[InPixelFormat].BlockSizeX;
		uint32 BlockSizeZ = 1;

		astcenc_config EncConfig;
		astcenc_error EncStatus = astcenc_config_init(
			EncProfile,
			BlockSizeX,
			BlockSizeX,
			BlockSizeZ,
			ASTCENC_PRE_THOROUGH, // shouldn't be used?
			ASTCENC_FLG_DECOMPRESS_ONLY,
			&EncConfig);

		if (EncStatus != ASTCENC_SUCCESS)
		{
			UE_LOG(LogTextureFormatASTC, Error, TEXT("astcenc_config_init has failed in DecodeImage: %s - texture %.*s"), ANSI_TO_TCHAR(astcenc_get_error_string(EncStatus)), InTextureName.Len(), InTextureName.GetData());
			return false;
		}

		astcenc_context* EncContext = nullptr;
		uint32 EncThreadCount = 1;
		EncStatus = astcenc_context_alloc(&EncConfig, EncThreadCount, &EncContext);
		if (EncStatus != ASTCENC_SUCCESS)
		{
			UE_LOG(LogTextureFormatASTC, Error, TEXT("astcenc_context_alloc has failed in DecodeImage: %s - texture %.*s"), ANSI_TO_TCHAR(astcenc_get_error_string(EncStatus)), InTextureName.Len(), InTextureName.GetData());
			return false;
		}


		OutImage.Format = bHDRImage ? ERawImageFormat::RGBA16F : ERawImageFormat::BGRA8;
		OutImage.GammaSpace = bInSRGB ? EGammaSpace::sRGB : EGammaSpace::Linear;
		OutImage.SizeX = InSizeX;
		OutImage.SizeY = InSizeY;
		OutImage.NumSlices = InNumSlices;

		const FPixelFormatInfo& OutputPF = GPixelFormats[bHDRImage ? PF_FloatRGBA : PF_B8G8R8A8];

		uint64 SliceSizeBytes = OutputPF.Get2DImageSizeInBytes(InSizeX, InSizeY);

		OutImage.RawData.AddUninitialized(SliceSizeBytes * InNumSlices);

		// astc image basically wants views into the image but also wants them as an array of pointers
		// to each slice.
		TArray<uint8*, TInlineAllocator<6>> ImageSrcData;
		ImageSrcData.Reserve(OutImage.NumSlices);
		for (int32 SliceIdx = 0; SliceIdx < OutImage.NumSlices; SliceIdx++)
		{
			FImageView Slice = OutImage.GetSlice(SliceIdx);
			uint8* SliceData;
			if (bHDRImage)
			{
				SliceData = (uint8*)Slice.AsRGBA16F().GetData();
			}
			else
			{
				SliceData = (uint8*)Slice.AsBGRA8().GetData();
			}
			ImageSrcData.Add(SliceData);
		}

		astcenc_image DecodedImage;
		DecodedImage.dim_x = OutImage.SizeX;
		DecodedImage.dim_y = OutImage.SizeY;
		DecodedImage.dim_z = OutImage.NumSlices;
		DecodedImage.data = (void**)ImageSrcData.GetData();
		DecodedImage.data_type = (bHDRImage ? ASTCENC_TYPE_F16 : ASTCENC_TYPE_U8);

		EncStatus = astcenc_decompress_image(EncContext, (uint8*)InEncodedData.GetData(), InEncodedData.GetSize(), &DecodedImage, &EncSwizzle, 0);
		astcenc_context_free(EncContext);

		if (EncStatus != ASTCENC_SUCCESS)
		{
			UE_LOG(LogTextureFormatASTC, Error, TEXT("astcenc_decompress_image has failed in DecodeImage: %s - texture %.*s"), ANSI_TO_TCHAR(astcenc_get_error_string(EncStatus)), InTextureName.Len(), InTextureName.GetData());
			return false;
		}

		return true;
	}


	virtual bool CompressImage(
			const FImage& InImage,
			const FTextureBuildSettings& BuildSettings,
			const FIntVector3& InMip0Dimensions,
			int32 InMip0NumSlicesNoDepth,
			int32 InMipIndex,
			int32 InMipCount,
			FStringView DebugTexturePathName,
			bool bImageHasAlphaChannel,
			FCompressedImage2D& OutCompressedImage
		) const override
	{
#if SUPPORTS_ISPC_ASTC
		if(GASTCCompressor == 0)
		{
			UE_CALL_ONCE( [&](){
				UE_LOG(LogTextureFormatASTC, Display, TEXT("TextureFormatASTC using ISPC"))
			} );

			// Route ASTC compression work to the ISPC module instead.
			// note: ISPC can't do HDR, will throw an error
			return IntelISPCTexCompFormat.CompressImage(InImage, BuildSettings, InMip0Dimensions, InMip0NumSlicesNoDepth, InMipIndex, InMipCount, DebugTexturePathName, bImageHasAlphaChannel, OutCompressedImage);
		}
#endif

		TRACE_CPUPROFILER_EVENT_SCOPE(ASTC.CompressImage);

		UE_CALL_ONCE( [&](){
			UE_LOG(LogTextureFormatASTC, Display, TEXT("TextureFormatASTC using astcenc"))
		} );

		return ASTCEnc_Compress(InImage, BuildSettings, InMip0Dimensions, InMip0NumSlicesNoDepth, DebugTexturePathName, bImageHasAlphaChannel, OutCompressedImage);
	}

private:
	const ITextureFormat& IntelISPCTexCompFormat;
	
	IImageWrapperModule& ImageWrapperModule;
};

/**
 * Module for ASTC texture compression.
 */
static ITextureFormat* Singleton = NULL;

class FTextureFormatASTCModule : public ITextureFormatModule
{
public:
	FTextureFormatASTCModule()
	{
	}
	virtual ~FTextureFormatASTCModule()
	{
		delete Singleton;
		Singleton = NULL;
	}
	
	virtual void StartupModule() override
	{
	}

	virtual bool CanCallGetTextureFormats() override { return false; }

	virtual ITextureFormat* GetTextureFormat()
	{
		if (!Singleton)
		{
			Singleton = new FTextureFormatASTC();
		}
		return Singleton;
	}

	static inline UE::DerivedData::TBuildFunctionFactory<FASTCTextureBuildFunction> BuildFunctionFactory;
	static inline UE::DerivedData::TBuildFunctionFactory<FGenericTextureDecodeBuildFunction<FTextureFormatASTC>> DecodeBuildFunctionFactory;
};

IMPLEMENT_MODULE(FTextureFormatASTCModule, TextureFormatASTC);
