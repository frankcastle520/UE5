// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/SourceProject/SourceProject.h"

namespace uLang
{

/**
 * A source snippet in memory
 **/
class CSourceDataSnippet : public ISourceSnippet
{
public:
    CSourceDataSnippet(CUTF8String&& Path, CUTF8String&& Text)
        : _Path(Move(Path))
        , _Text(Move(Text))
    {}

    //~ Begin ISourceSnippet interface
    virtual CUTF8String GetPath() const override { return _Path; }
    virtual void SetPath(const CUTF8String& Path) override { _Path = Path; }
    virtual TOptional<CUTF8String> GetText() const override { return _Text; }
    virtual TOptional<TSRef<Verse::Vst::Snippet>> GetVst() const override { return _Vst; }
    virtual void SetVst(TSRef<Verse::Vst::Snippet> Snippet) override { _Vst = Snippet; }
    //~ End ISourceSnippet interface

private:
    CUTF8String  _Path; // Original path of this snippet (usually on disk)
    CUTF8String  _Text; // UTF8 encoded content of this snippet

    TOptional<TSRef<Verse::Vst::Snippet>> _Vst;
};

/**
 * A source package in memory
 **/
class CSourceDataPackage : public CSourcePackage
{
public:

    CSourceDataPackage(const CUTF8String& Name, const CUTF8String& DirPath, const CSourcePackage::SSettings& Settings)
        : CSourcePackage(Name, TSRef<CSourceModule>::New(""))
        , _DirPath(DirPath)
    {
        _Settings = Settings;
    }

    virtual const CUTF8String& GetDirPath() const override { return _DirPath; }
    virtual EOrigin GetOrigin() const override { return EOrigin::Memory; }

private:

    CUTF8String _DirPath;    // The directory where the contained snippets will be saved
};

}