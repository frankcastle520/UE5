// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)
#include "ContextInlines.h"
#include "Utils.h"
#include "Memcpy.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <float.h>

#ifdef __APPLE__
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace AutoRTFM
{

namespace
{

// A helper that opens a FILE to "/dev/null" on first call to Get()
// and automatically closes the file on static destruction.
class FNullFile
{
public:
    static FILE* Get()
    {
        static FNullFile Instance;
        return Instance.File;
    }

private:
    FNullFile() : File(fopen("/dev/null", "wb")) {}
    ~FNullFile() { fclose(File); }
    FILE* const File;
};

void ThrowErrorFormatContainsPercentN()
{
    UE_LOG(LogAutoRTFM, Warning, TEXT("AutoRTFM does not support format strings containing '%%n'"));
    FContext* Context = FContext::Get();
    Context->AbortByLanguageAndThrow();
}

// Throws an error if the format string contains a '%n'.
static void ThrowIfFormatContainsPercentN(const char* Format)
{
    for (const char* P = Format; *P != '\0'; ++P)
    {
        if (*P == '%')
        {
            switch (*++P)
            {
                case 'n':
                    ThrowErrorFormatContainsPercentN();
                    break;
                case '\0':
                    return;
            }
        }
    }
}

// Throws an error if the format string contains a '%n'.
static void ThrowIfFormatContainsPercentN(const wchar_t* Format)
{
    for (const wchar_t* P = Format; *P != L'\0'; ++P)
    {
        if (*P == L'%')
        {
            switch (*++P)
            {
                case L'n':
                    ThrowErrorFormatContainsPercentN();
                    break;
                case L'\0':
                    return;
            }
        }
    }
}

}

UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(memcpy, Memcpy);
UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(memmove, Memmove);
UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(memset, Memset);

void* RTFM_malloc(size_t Size)
{
    void* Result = malloc(Size);
	FContext* Context = FContext::Get();
    Context->GetCurrentTransaction()->DeferUntilAbort([Result]
    {
        free(Result);
    });
    Context->DidAllocate(Result, Size);
    return Result;
}

UE_AUTORTFM_REGISTER_OPEN_FUNCTION(malloc);

void* RTFM_calloc(size_t Count, size_t Size)
{
    void* Result = calloc(Count, Size);
	FContext* Context = FContext::Get();
    Context->GetCurrentTransaction()->DeferUntilAbort([Result]
    {
        free(Result);
    });
    Context->DidAllocate(Result, Count * Size);
    return Result;
}

UE_AUTORTFM_REGISTER_OPEN_FUNCTION(calloc);

void RTFM_free(void* Ptr)
{
    if (Ptr)
    {
		FContext* Context = FContext::Get();
        Context->GetCurrentTransaction()->DeferUntilCommit([Ptr]
        {
            free(Ptr);
        });
    }
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(free);

void* RTFM_realloc(void* Ptr, size_t Size)
{
    void* NewObject = RTFM_malloc(Size);
    if (Ptr)
    {
#if defined(__APPLE__)
		const size_t OldSize = malloc_size(Ptr);
#elif defined(_WIN32)
		const size_t OldSize = _msize(Ptr);
#else
		const size_t OldSize = malloc_usable_size(Ptr);
#endif
		FContext* Context = FContext::Get();
        MemcpyToNew(NewObject, Ptr,  FMath::Min(OldSize, Size), Context);
        RTFM_free(Ptr);
    }
    return NewObject;
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(realloc);

char* RTFM_strcpy(char* const Dst, const char* const Src)
{
    const size_t SrcLen = strlen(Src);

	FContext* Context = FContext::Get();
    Context->RecordWrite(Dst, SrcLen + sizeof(char));
    return strcpy(Dst, Src);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(strcpy);

char* RTFM_strncpy(char* const Dst, const char* const Src, const size_t Num)
{
	FContext* Context = FContext::Get();
    Context->RecordWrite(Dst, Num);
    return strncpy(Dst, Src, Num);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(strncpy);

char* RTFM_strcat(char* const Dst, const char* const Src)
{
    const size_t DstLen = strlen(Dst);
    const size_t SrcLen = strlen(Src);

	FContext* Context = FContext::Get();
    Context->RecordWrite(Dst + DstLen, SrcLen + 1);
    return strcat(Dst, Src);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(strcat);

char* RTFM_strncat(char* const Dst, const char* const Src, const size_t Num)
{
    const size_t DstLen = strlen(Dst);

	FContext* Context = FContext::Get();
    Context->RecordWrite(Dst + DstLen, Num + 1);
    return strncat(Dst, Src, Num);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(strncat);

UE_AUTORTFM_REGISTER_SELF_FUNCTION(memcmp);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(strcmp);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(strncmp);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<const char*(*)(const char*, int)>(&strchr));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<const char* (*)(const char*, int)>(&strrchr));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<const char* (*)(const char*, const char*)>(&strstr));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(strlen);

long int RTFM_strtol(const char* String, char** EndPtr, int Radix)
{
    if (nullptr != EndPtr)
    {
        FContext* Context = FContext::Get();
        Context->RecordWrite(EndPtr, sizeof(char*));
    }

    return strtol(String, EndPtr, Radix);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(strtol);

UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<const wchar_t* (*)(const wchar_t*, wchar_t)>(&wcschr));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<wchar_t* (*)(wchar_t*, wchar_t)>(&wcschr));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<wchar_t* (*)(wchar_t*, const wchar_t*)>(&wcsstr));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<const wchar_t* (*)(const wchar_t*, const wchar_t*)>(&wcsstr));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(wcscmp);

UE_AUTORTFM_REGISTER_SELF_FUNCTION(iswupper);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(iswlower);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(iswalpha);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(iswgraph);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(iswprint);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(iswpunct);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(iswalnum);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(iswdigit);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(iswxdigit);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(iswspace);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(iswcntrl);

#if PLATFORM_WINDOWS
UE_AUTORTFM_REGISTER_SELF_FUNCTION(sqrt);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(sin);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(cos);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(tan);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(asin);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(acos);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(atan);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(atan2);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(sinh);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(cosh);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(tanh);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(asinh);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(acosh);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(atanh);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(exp);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(log);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(pow);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(llrint);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(fmod);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(modf);
// Linux (likely Mac) have ambiguous overrides to these math functions
#else
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float)>(&sqrt));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double)>(&sqrt));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float)>(&sin));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double)>(&sin));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float)>(&cos));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double)>(&cos));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float)>(&tan));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double)>(&tan));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float)>(&asin));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double)>(&asin));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float)>(&acos));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double)>(&acos));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float)>(&atan));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double)>(&atan));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float, float)>(&atan2));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double, long double)>(&atan2));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float)>(&sinh));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double)>(&sinh));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float)>(&cosh));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double)>(&cosh));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float)>(&tanh));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double)>(&tanh));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float)>(&asinh));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double)>(&asinh));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float)>(&acosh));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double)>(&acosh));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float)>(&atanh));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double)>(&atanh));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float)>(&exp));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double)>(&exp));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float)>(&log));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double)>(&log));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float, float)>(&pow));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double, long double)>(&pow));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long long(*)(float)>(&llrint));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long long(*)(long double)>(&llrint));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float, float)>(&fmod));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double, long double)>(&fmod));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float, float*)>(&modf));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<long double(*)(long double, long double*)>(&modf));
#endif

// Self register Math functions
UE_AUTORTFM_REGISTER_SELF_FUNCTION(sqrtf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(sinf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(cosf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(tanf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(asinf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(asinhf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(acosf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(acoshf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(atanf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(atanhf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(atan2f);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(sinhf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(coshf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(tanhf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(expf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(logf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(powf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(llrintf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(fmodf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(fmodl);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(rand);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(modff);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(modfl);

// FIXME: Does not currently support %n format specifiers.
int RTFM_vsnprintf(char* Str, size_t Size, const char* Format, va_list ArgList)
{
    ThrowIfFormatContainsPercentN(Format);

    if (nullptr != Str && 0 != Size)
    {
        va_list ArgList2;
        va_copy(ArgList2, ArgList);

        FContext* Context = FContext::Get();
        int Count = vsnprintf(nullptr, 0, Format, ArgList2);
        if (Count >= 0)
        {
            size_t NumBytes = FMath::Min<size_t>(Size, (1 + Count)) * sizeof(char);
            Context->RecordWrite(Str, NumBytes);
        }
    }

    return vsnprintf(Str, Size, Format, ArgList);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(vsnprintf);

// FIXME: Does not currently support %n format specifiers.
int RTFM_vswprintf(wchar_t* Str, size_t Size, const wchar_t* Format, va_list ArgList)
{
    ThrowIfFormatContainsPercentN(Format);

    if (nullptr != Str && 0 != Size)
    {
        va_list ArgList2;
        va_copy(ArgList2, ArgList);

#if PLATFORM_WINDOWS
        int Count = vswprintf(nullptr, 0, Format, ArgList2);
#else
        // vswprintf(nullptr, 0, ...) will return -1.
        int Count = vfwprintf(FNullFile::Get(), Format, ArgList2);
#endif

        size_t NumChars = FMath::Min<size_t>(Size, (1 + FMath::Max(Count, 0)));
        size_t NumBytes = NumChars * sizeof(wchar_t);
        if (NumBytes >= 0)
        {
            FContext* Context = FContext::Get();
            Context->RecordWrite(Str, NumBytes);
        }
    }

    return vswprintf(Str, Size, Format, ArgList);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(
    static_cast<int(*)(wchar_t*, size_t, const wchar_t*, va_list)>(&vswprintf),
    RTFM_vswprintf);

// FIXME: Does not currently support %n format specifiers.
int RTFM_swprintf(wchar_t* Buffer, size_t BufferCount, wchar_t const* Format, ...)
{
    va_list ArgList;

    va_start(ArgList, Format);
    int Count = RTFM_vswprintf(Buffer, BufferCount, Format, ArgList);
    va_end(ArgList);

    return Count;
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(
    static_cast<int(*)(wchar_t*, size_t, wchar_t const*, ...)>(&swprintf),
    RTFM_swprintf);

// FIXME: Does not currently support %n format specifiers.
int RTFM_snprintf(char* Str, size_t Size, const char* Format, ...)
{
    va_list ArgList;

    va_start(ArgList, Format);
    int Count = RTFM_vsnprintf(Str, Size, Format, ArgList);
    va_end(ArgList);

    return Count;
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(snprintf);

// FIXME: Does not currently support %n format specifiers.
int RTFM_printf(const char* Format, ...)
{
    ThrowIfFormatContainsPercentN(Format);

    va_list ArgList;
    va_start(ArgList, Format);
    int Result = vprintf(Format, ArgList);
    va_end(ArgList);
    return Result;
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(printf);

// FIXME: Does not currently support %n format specifiers.
int RTFM_wprintf(const wchar_t* Format, ...)
{
    ThrowIfFormatContainsPercentN(Format);

    va_list ArgList;
    va_start(ArgList, Format);
    int Result = vwprintf(Format, ArgList);
    va_end(ArgList);
    return Result;
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(wprintf);

int RTFM_putchar(int Char)
{
    return putchar(Char);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(putchar);

int RTFM_puts(const char* Str)
{
    return puts(Str);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(puts);

#if PLATFORM_WINDOWS

FILE* RTFM___acrt_iob_func(int Index)
{
    switch (Index)
    {
    case 1:
    case 2:
        return __acrt_iob_func(Index);
    default:
	{
		UE_LOG(LogAutoRTFM, Warning, TEXT("Attempt to get file descriptor %d (not 1 or 2) in __acrt_iob_func."), Index);
		FContext* Context = FContext::Get();
        Context->AbortByLanguageAndThrow();
        return NULL;
	}
    }
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(__acrt_iob_func);

// FIXME: Does not currently support %n format specifiers.
int RTFM___stdio_common_vfprintf(
        unsigned __int64 Options,
        FILE*            Stream,
        char const*      Format,
        _locale_t        Locale,
        va_list          ArgList)
{
    ThrowIfFormatContainsPercentN(Format);

    return __stdio_common_vfprintf(Options, Stream, Format, Locale, ArgList);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(__stdio_common_vfprintf);

// FIXME: Does not currently support %n format specifiers.
int RTFM___stdio_common_vsprintf(
        unsigned __int64 Options,
        char*            Buffer,
        size_t           BufferCount,
        char const*      Format,
        _locale_t        Locale,
        va_list          ArgList)
{
    ThrowIfFormatContainsPercentN(Format);

    if (nullptr != Buffer && 0 != BufferCount)
    {
        va_list ArgList2;
        va_copy(ArgList2, ArgList);

        FContext* Context = FContext::Get();
        int Count = __stdio_common_vsprintf(Options, nullptr, 0, Format, Locale, ArgList2);
        if (Count >= 0)
        {
            size_t NumBytes = FMath::Min<size_t>(BufferCount, (1 + Count)) * sizeof(char);
            Context->RecordWrite(Buffer, NumBytes);
        }
    }

    return __stdio_common_vsprintf(Options, Buffer, BufferCount, Format, Locale, ArgList);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(__stdio_common_vsprintf);

// FIXME: Does not currently support %n format specifiers.
int RTFM___stdio_common_vswprintf(
        unsigned __int64 Options,
        wchar_t*         Buffer,
        size_t           BufferCount,
        wchar_t const*   Format,
        _locale_t        Locale,
        va_list          ArgList)
{
    ThrowIfFormatContainsPercentN(Format);

    if (nullptr != Buffer && 0 != BufferCount)
    {
        va_list ArgList2;
        va_copy(ArgList2, ArgList);

        FContext* Context = FContext::Get();
        int Count = __stdio_common_vswprintf(Options, nullptr, 0, Format, Locale, ArgList2);
        if (Count >= 0)
        {
            size_t NumBytes = FMath::Min<size_t>(BufferCount, (1 + Count)) * sizeof(wchar_t);
            Context->RecordWrite(Buffer, NumBytes);
        }
    }

    return __stdio_common_vswprintf(Options, Buffer, BufferCount, Format, Locale, ArgList);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(__stdio_common_vswprintf);

// FIXME: Does not currently support %n format specifiers.
int RTFM___stdio_common_vfwprintf(
        unsigned __int64 Options,
        FILE*            Stream,
        wchar_t const*   Format,
        _locale_t        Locale,
        va_list          ArgList)
{
    ThrowIfFormatContainsPercentN(Format);

    return __stdio_common_vfwprintf(Options, Stream, Format, Locale, ArgList);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(__stdio_common_vfwprintf);

#else // PLATFORM_WINDOWS -> so !PLATFORM_WINDOWS
extern "C" size_t _ZNSt3__112__next_primeEm(size_t N) __attribute__((weak));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(_ZNSt3__112__next_primeEm);
#endif // PLATFORM_WINDOWS -> so end of !PLATFORM_WINDOWS

UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float, float)>(&powf));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<double(*)(double, double)>(&pow));

#if PLATFORM_WINDOWS
UE_AUTORTFM_REGISTER_SELF_FUNCTION(_tcsncmp);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(_tcslen);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(_isnan);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(_fdtest);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(_dtest);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(_ldtest);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(_finite);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(IsDebuggerPresent);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(GetSystemTime);

UE_AUTORTFM_REGISTER_SELF_FUNCTION(QueryPerformanceCounter);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(QueryPerformanceFrequency);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(GetCurrentThreadId);

UE_AUTORTFM_REGISTER_SELF_FUNCTION(TlsGetValue);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(GetLocalTime);

BOOL RTFM_TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue)
{
	LPVOID CurrentValue = TlsGetValue(dwTlsIndex);

	AutoRTFM::OnAbort([dwTlsIndex, CurrentValue]
	{
		TlsSetValue(dwTlsIndex, CurrentValue);
	});

	return TlsSetValue(dwTlsIndex, lpTlsValue);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(TlsSetValue);
#endif // PLATFORM_WINDOWS

#if PLATFORM_LINUX
UE_AUTORTFM_REGISTER_SELF_FUNCTION(clock_gettime);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(gettimeofday);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(gmtime_r);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(localtime_r);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(bcmp);

UE_AUTORTFM_REGISTER_SELF_FUNCTION(pthread_getspecific);
#endif // PLATFORM_LINUX

wchar_t* RTFM_wcscpy(wchar_t* Dst, const wchar_t* Src)
{
    const size_t SrcLen = wcslen(Src);

	FContext* Context = FContext::Get();
    Context->RecordWrite(Dst, (SrcLen + 1) * sizeof(wchar_t));
    return wcscpy(Dst, Src);
}

wchar_t* RTFM_wcsncpy(wchar_t* Dst, const wchar_t* Src, size_t Count)
{
	FContext* Context = FContext::Get();
	Context->RecordWrite(Dst, Count * sizeof(wchar_t));
	return wcsncpy(Dst, Src, Count);
}

#ifdef _MSC_VER
/*
   Disable warning about deprecated STD C functions.
*/
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

UE_AUTORTFM_REGISTER_OPEN_FUNCTION(wcscpy);
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(wcsncpy);

#ifdef _MSC_VER
#pragma warning(pop)
#endif

int RTFM_atexit(void(__cdecl*Callback)(void))
{
	FContext* Context = FContext::Get();
	Context->GetCurrentTransaction()->DeferUntilCommit([Callback]
		{
			atexit(Callback);
		});

	return 0;
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(atexit);

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
