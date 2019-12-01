#pragma once

#include <stdio.h>

#define TFS_ESUCC 1
#define TFS_EUNKNOWN0 0
#define TFS_EUNKNOWN1 -1
#define TFS_ENOENT -2
#define TFS_ENOSPACE -3
#define TFS_EEXISTS -4

const char* TFS_GetError(int code);
