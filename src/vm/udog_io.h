#ifndef udog_io_h
#define udog_io_h

#include "udog.h"
#include "udog_config.h"

// This module defines the IO class and its associated methods. They are
// implemented using the C standard library.
#if UDOG_USE_LIB_IO

void udogLoadIOLibrary(UDogVM* vm);

#endif

#endif
