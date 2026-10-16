#include <stdlib.h>
static inline int setenv(const char*n,const char*v,int o){(void)o;return _putenv_s(n,v);}
