#ifndef DSPOTTER_WRAPPER_H
#define DSPOTTER_WRAPPER_H

// 保存并undef冲突的宏
#ifdef min
#define DSpotter_MIN_WAS_DEFINED
#undef min
#endif

#ifdef max
#define DSpotter_MAX_WAS_DEFINED  
#undef max
#endif

#include "DSpotterSDKApi.h"

// 恢复宏定义
#ifdef DSpotter_MIN_WAS_DEFINED
#define min(a,b) (((a) < (b)) ? (a) : (b))
#undef DSpotter_MIN_WAS_DEFINED
#endif

#ifdef DSpotter_MAX_WAS_DEFINED
#define max(a,b) (((a) > (b)) ? (a) : (b))
#undef DSpotter_MAX_WAS_DEFINED
#endif

#endif // DSPOTTER_WRAPPER_H