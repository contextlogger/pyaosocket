#ifndef __local_symbian_utils_h__
#define __local_symbian_utils_h__

#include <e32std.h>

// These possibly make the inclusion of this header order sensitive.
#ifndef NONSHARABLE_CLASS
#define NONSHARABLE_CLASS(x) class x
#endif
#ifndef NONSHARABLE_STRUCT
#define NONSHARABLE_STRUCT(x) struct x
#endif

#endif /* __local_symbian_utils_h__ */
