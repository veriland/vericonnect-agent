#include "vc/vc_adapter.h"
#include <dlfcn.h>

void *vc_dynlib_open(const char *path)  { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
void *vc_dynlib_sym(void *h, const char *n) { return dlsym(h, n); }
void  vc_dynlib_close(void *h)          { if (h) dlclose(h); }
