// core/version.h - the build's version string.
//
// CMake stamps every translation unit with APP_VERSION_W taken from
// `git describe --tags` at configure time, so the bottom bar and the About
// dialog always say what was actually built (a tag when on one, otherwise
// <last tag>-<n>-g<hash>). The fallback keeps source-archive builds, where
// git metadata is absent, compiling with the plain project version.
#pragma once

#ifndef APP_VERSION_W
#define APP_VERSION_W L"1.0.0"
#endif

inline const wchar_t* appVersion() { return APP_VERSION_W; }
