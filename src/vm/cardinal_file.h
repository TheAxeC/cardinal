#ifndef cardinal_file_h
#define cardinal_file_h

#include "cardinal.h"
#include "cardinal_config.h"

#if CARDINAL_USE_DEFAULT_FILE_LOADER

CardinalValue* defaultModuleLoader(CardinalVM* vm, const char* module);

// This module defines the FILE class and its associated methods. They are
// implemented using the C standard library.
void cardinalLoadFileLibrary(CardinalVM* vm);


#endif

#endif