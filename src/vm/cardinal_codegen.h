#ifndef cardinal_codegen_h
#define cardinal_codegen_h

#include "cardinal_vm.h"

// The method binds the Code generator to the VM 
// This can be used to generate code during the running of a program
// It will be used to compile and generate bytecode
void cardinalInitializeCodeGenerator(CardinalVM* vm);

#endif
