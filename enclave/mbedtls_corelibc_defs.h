// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#if !defined(OE_MBEDTLS_CORELIBC_DEFINES)
#define OE_MBEDTLS_CORELIBC_DEFINES

/* Use stdc names for definitions in limits.h */
#if !defined(OE_NEED_STDC_NAMES)
#define OE_NEED_STDC_NAMES
#define __UNDEF_OE_NEED_STDC_NAMES
#endif /* defined(OE_NEED_STDC_NAMES) */
#include <openenclave/corelibc/limits.h>
#if defined(__UNDEF_OE_NEED_STDC_NAMES)
#undef OE_NEED_STDC_NAMES
#undef __UNDEF_OE_NEED_STDC_NAMES
#endif /* defined (__UNDEF_OE_NEED_STDC_NAMES) */

/* Custom redefine of the pthread_mutex_t to oe_pthread_t.
 * This uses a define rather than the typedef in pthread.h so that its use
 * can be scoped to the mbedtls headers and subsequently undefined afterwards.
 */
#define pthread_mutex_t oe_pthread_mutex_t

#endif /* !defined(OE_MBEDTLS_CORELIBC_DEFINES) */
