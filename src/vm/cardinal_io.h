#ifndef cardinal_io_h
#define cardinal_io_h

#include "cardinal.h"
#include "cardinal_config.h"

// This module defines the IO class and its associated methods. They are
// implemented using the C standard library.
#if CARDINAL_USE_LIB_IO

void cardinalLoadIOLibrary(CardinalVM* vm);

#endif

#endif
