// license:GPLv3+

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#define __null 0
#define __WINE_WINCON_H

#include <windows.h>

#ifdef GetClassInfo
#undef GetClassInfo
#endif


#if defined(ENABLE_DX9)
 #ifdef _DEBUG
  #define D3D_DEBUG_INFO
 #endif
 #include "minid3d9.h"
#endif


#include <atlbase.h>

#ifdef _MSC_VER
#define PATH_SEPARATOR_CHAR '\\'
#define PATH_SEPARATOR_WCHAR L'\\'
#else
#define PATH_SEPARATOR_CHAR '/'
#define PATH_SEPARATOR_WCHAR L'/'
#endif

#include <oleauto.h>


#if defined(_M_IX86) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || defined(__i386__) || defined(__i386) || defined(__i486__) || defined(__i486) || defined(i386) || defined(__ia64__) || defined(__x86_64__)
 #define ENABLE_SSE_OPTIMIZATIONS
 #include <xmmintrin.h>
#elif (defined(_M_ARM) || defined(_M_ARM64) || defined(__arm__) || defined(__arm64__) || defined(__aarch64__)) && (!defined(__ARM_ARCH) || __ARM_ARCH >= 7) && (!defined(_MSC_VER) || defined(__clang__)) //!! disable sse2neon if MSVC&non-clang
 #define ENABLE_SSE_OPTIMIZATIONS
 #include "sse2neon.h"
#endif

#ifdef __SSSE3__
 #include <tmmintrin.h>
#endif

#include <vector>
using std::vector;

#include <format>
#include <algorithm>

#include <string>
using namespace std::string_literals;
using namespace std::string_view_literals;
using std::string;
using std::wstring;



#define fopen_s(pFile, filename, mode) (((*(pFile)) = fopen((filename), (mode))) == nullptr)
#define fprintf_s fprintf
#define fread_s(buffer, bufferSize, elementSize, count, stream) fread(buffer, bufferSize, count, stream)
#define fscanf_s fscanf

#define sscanf_s sscanf

#ifndef __MINGW32__
#define localtime_s(x, y) localtime_r(y, x)
#define gmtime_s(x, y) gmtime_r(y, x)
#define _aligned_malloc(size, align) aligned_alloc(align, size)
#define _aligned_free free
#endif

#define _T(x) (x)
#define AtoT(x) (x)
#define _ASSERTE(expr) ((void)0)

#undef SetCurrentDirectory
#define SetCurrentDirectory SetCurrentDirectoryA

#undef MessageBox
#define MessageBox MessageBoxA

typedef ULONG_PTR HCRYPTPROV;
typedef ULONG_PTR HCRYPTHASH;
typedef ULONG_PTR HCRYPTKEY;

#pragma pack(push, 1)
typedef struct {
   WORD wFormatTag;
   WORD nChannels;
   DWORD nSamplesPerSec;
   DWORD nAvgBytesPerSec;
   WORD nBlockAlign;
   WORD wBitsPerSample;
   WORD cbSize;
} WAVEFORMATEX, *LPWAVEFORMATEX;
#pragma pack(pop)

typedef struct {
   DWORD lStructSize;
   HWND hwndOwner;
   HINSTANCE hInstance;
   DWORD Flags;
   LPSTR lpstrFindWhat;
   LPSTR lpstrReplaceWith;
   WORD wFindWhatLen;
   WORD wReplaceWithLen;
   LPARAM lCustData;
   void* lpfnHook;
   LPCSTR lpTemplateName;
} FINDREPLACEA;

#define FINDREPLACE FINDREPLACEA
#define CREATESTRUCT CREATESTRUCTA
#define WNDCLASS WNDCLASSA
#define LOGFONT LOGFONTA

typedef LPSTR LPTSTR;
typedef LPCSTR LPCTSTR;
class PropertyDialog final { };
class SCNotification final { };

#include "utils/Logger.h"

#include <atldef.h>
#include <atlcom.h>
#include <atlcomcli.h>
#include <atlsafe.h>

#include <afx.h>
#include <afxdlgs.h>
#include <afxwin.h>
#include <atltypes.h>

#include "standalone/inc/win32xx/win32xx.h"

#include "def.h"

#include "math/math.h"
#include "math/vector.h"
#include "math/matrix.h"
#include "math/bbox.h"

#include "ui/win/resource.h"

#include "dispid.h"

#include "utils/color.h"

#include "standalone/vpinball_standalone_i.h"

#include "core/Settings.h"

#include "utils/wintimer.h"

#include "iselect.h"

#include "core/Scriptable.h"

#include "core/ieditable.h"

#include "physics/hitable.h"

#include "extern.h"

#include "core/VPApp.h"

#include "parts/pintable.h"

#include "core/player.h"

#include "editablereg.h"
