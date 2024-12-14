// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Toolchain/CommandLine.h"

namespace uLang
{
namespace CommandLine_Impl
{
    static bool bInited = false;

    static SCommandLine& GetCommandLine();
    static void ParseCommandLine(int ArgC, char* ArgV[], CUTF8String& OutFullCmdLine, TArray<CUTF8String>& OutTokens, TArray<CUTF8String>& OutSwitches);
}

static SCommandLine& CommandLine_Impl::GetCommandLine()
{
    static SCommandLine CommandLine;
    bInited = true;

    return CommandLine;
}

static void CommandLine_Impl::ParseCommandLine(int ArgC, char* ArgV[], CUTF8String& OutFullCmdLine, TArray<CUTF8String>& OutTokens, TArray<CUTF8String>& OutSwitches)
{
    OutTokens.Reserve(ArgC - 1);
    OutSwitches.Reserve(ArgC - 1);

    CUTF8StringBuilder FullCmdLine(ArgV[0]);

    // First argument is the path to the running executable (we skip it)
    for (int i = 1; i < ArgC; ++i)
    {
        const char* Arg = ArgV[i];
        if (Arg && *Arg == '-')
        {
            OutSwitches.Add(Arg + 1);
        }
        else
        {
            OutTokens.Add(Arg);
        }

        FullCmdLine.Append(' ').Append(Arg);
    }

    OutFullCmdLine = FullCmdLine.MoveToString();
}

void CommandLine::Init(int ArgC, char* ArgV[])
{
    ULANG_ENSUREF(!CommandLine_Impl::bInited, "CommandLine has already been initialized.");

    SCommandLine& CmdLine = CommandLine_Impl::GetCommandLine();
    CommandLine_Impl::ParseCommandLine(ArgC, ArgV, CmdLine._Unparsed, CmdLine._Tokens, CmdLine._Switches);
}

void CommandLine::Init(const SCommandLine& Rhs)
{
    ULANG_ENSUREF(!CommandLine_Impl::bInited, "CommandLine has already been initialized.");
    CommandLine_Impl::GetCommandLine() = Rhs;
}

bool CommandLine::IsSet()
{
    return CommandLine_Impl::bInited;
}

const SCommandLine& CommandLine::Get()
{
    bool bIsSet = IsSet();
    ULANG_ENSUREF(bIsSet, "CommandLine has not been initialized.");

    SCommandLine& CommandLine = CommandLine_Impl::GetCommandLine();
    CommandLine_Impl::bInited = bIsSet;

    return CommandLine;
}
} // namespace uLang
