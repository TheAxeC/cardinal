#ifndef udog_debug_h
#define udog_debug_h

#include "udog_value.h"
#include "udog_vm.h"

#define note(S, ...) fprintf(stderr,                                     \
  "\x1b[1m(%s:%d, %s)\x1b[0m\n  \x1b[1m\x1b[90mnote:\x1b[0m " S "\n",    \
  __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define warn(S, ...) fprintf(stderr,                                     \
  "\x1b[1m(%s:%d, %s)\x1b[0m\n  \x1b[1m\x1b[33mwarning:\x1b[0m " S "\n", \
  __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define errn(S, ...) do { fprintf(stderr,                                \
  "\x1b[1m(%s:%d, %s)\x1b[0m\n  \x1b[1m\x1b[31merror:\x1b[0m " S "\n",   \
  __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); exit(1); } while (0) \
  
#define noteScript(vm, S, ...) vm->printFunction(                                     \
  "\x1b[0m\n  \x1b[1m\x1b[90mnote:\x1b[0m " S "\n", ##__VA_ARGS__)

#define warnScript(vm, S, ...) vm->printFunction(                                     \
  "\x1b[0m\n  \x1b[1m\x1b[33mwarning:\x1b[0m " S "\n", ##__VA_ARGS__)

#define errnScript(vm, S, ...) vm->printFunction(                                \
  "\x1b[0m\n  \x1b[1m\x1b[31merror:\x1b[0m " S "\n", ##__VA_ARGS__);

void udogDebugPrintStackTrace(UDogVM* vm, ObjFiber* fiber);
int udogDebugPrintInstruction(UDogVM* vm, ObjFn* fn, int i);
void udogDebugPrintCode(UDogVM* vm, ObjFn* fn);
void udogDebugPrintStack(UDogVM* vm, ObjFiber* fiber);
ObjString* udogDebugGetStackTrace(UDogVM* vm, ObjFiber* fiber);

void checkDebugger(UDogVM* vm);

#endif
