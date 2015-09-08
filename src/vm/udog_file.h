#ifndef udog_file_h
#define udog_file_h

#include "udog.h"
#include "udog_config.h"

#if UDOG_USE_DEFAULT_FILE_LOADER

UDogValue* defaultModuleLoader(UDogVM* vm, const char* module);

// This module defines the FILE class and its associated methods. They are
// implemented using the C standard library.
void udogLoadFileLibrary(UDogVM* vm);


#endif

#endif