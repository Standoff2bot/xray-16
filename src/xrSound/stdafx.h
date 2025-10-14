#pragma once

#include "Common/Common.hpp"
#include "xrCore/xrCore.h"
#include "xrCore/_std_extensions.h"
#include "xrCore/xr_resource.h"

#include "xrCDB/xrCDB.h"

#include "Sound.h"

// Steam Audio integration
#ifdef USE_STEAMAUDIO
#   include <phonon.h>
#elif __has_include(<phonon.h>) && defined(XR_PLATFORM_WINDOWS)
#   include <phonon.h>
#   define USE_STEAMAUDIO
#endif
