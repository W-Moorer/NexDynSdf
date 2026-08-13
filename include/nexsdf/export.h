#pragma once

#if defined(_WIN32) && defined(NEXSDF_SHARED)
#  if defined(NEXSDF_BUILDING_LIBRARY)
#    define NEXSDF_API __declspec(dllexport)
#  else
#    define NEXSDF_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && defined(NEXSDF_BUILDING_LIBRARY)
#  define NEXSDF_API __attribute__((visibility("default")))
#else
#  define NEXSDF_API
#endif
