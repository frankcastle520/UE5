// Copyright Epic Games, Inc. All Rights Reserved.

#include "WindowsOpenGLFunctions.h"

#define DEFINE_GL_ENTRYPOINTS(Type,Func) Type Func = NULL;
	ENUM_GL_ENTRYPOINTS_ALL(DEFINE_GL_ENTRYPOINTS);
#undef DEFINE_GL_ENTRYPOINTS

PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = NULL;
