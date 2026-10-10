/* Must come first: <string.h>, <stdint.h> and even <io.h> can pull
 * in <windows.h> on modern SDKs, and once <windows.h> has been seen,
 * the SDK commits to C++ or C dialect for exdisp.h/mshtml.h/shobjidl.h. */
#if defined(_WIN32) && !defined(CINTERFACE)
#define CINTERFACE
#endif

#define WEBVIEW_IMPLEMENTATION
#include "webview.h"