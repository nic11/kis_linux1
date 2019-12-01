#include "tfs_errs.h"

const char* TFS_GetError(int code) {
    if (code > 0) {
        return "success";
    }
    static char buf[128];
    switch (code) {
        case TFS_ENOENT:
            return "does not exist";
        case TFS_ENOSPACE:
            return "no space left";
        case TFS_EEXISTS:
            return "already exists";
        default:
            sprintf(buf, "unknown error code %d", code);
            return buf;
    }
}