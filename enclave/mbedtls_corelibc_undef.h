// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#if defined(OE_MBEDTLS_CORELIBC_DEFINES)
#undef OE_MBEDTLS_CORELIBC_DEFINES

/* Remove the stdc to oe definition mappings provided by limits.h */
#define OE_REMOVE_STDC_DEFINES
#include <openenclave/corelibc/limits.h>
#undef OE_REMOVE_STDC_DEFINES

/* Undefine the custom pthread_mutex_t redefine */
#undef pthread_mutex_t

#endif /* defined(OE_MBEDTLS_CORELIBC_DEFINES) */
