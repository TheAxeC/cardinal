#ifndef cardinal_debug_h
#define cardinal_debug_h

#include "cardinal_value.h"
#include "cardinal_vm.h"

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

void cardinalDebugPrintStackTrace(CardinalVM* vm, ObjFiber* fiber);
int cardinalDebugPrintInstruction(CardinalVM* vm, ObjFn* fn, int i);
void cardinalDebugPrintCode(CardinalVM* vm, ObjFn* fn);
void cardinalDebugPrintStack(CardinalVM* vm, ObjFiber* fiber);
ObjString* cardinalDebugGetStackTrace(CardinalVM* vm, ObjFiber* fiber);

void checkDebugger(CardinalVM* vm);

#endif
