// header.h : include file for standard system include files,
// or project specific include files
//

#pragma once

#include "targetver.h"
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <shellapi.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <comdef.h>
#include <propsys.h>
#include <initguid.h>
#include <mmreg.h>
#include <shlwapi.h>
#include <debugapi.h>
#include <processthreadsapi.h>
#include <winuser.h>
#include <setupapi.h>
#include <devpkey.h>          // For DEVPKEY_Device_FriendlyName
#include <devguid.h>          // For GUID_DEVCLASS_DISPLAY
#include <cfgmgr32.h>         // For CM_Get_Device_IDW