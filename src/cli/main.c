/*
 * main.cpp
 *
 *  Created on: 8 september 2015
 *      Author: Axel Faes
 */
#include <stdio.h>
#include <string.h>

#include "io.h"
#include "vm.h"
#include "cardinal.h"
#include "../vm/cardinal_vm.h"
#include "../vm/cardinal_value.h"
#include "../vm/cardinal_debug.h"

#define MAX_LINE_LENGTH 1024 // TODO: Something less arbitrary.

static int runRepl() {
	CardinalVM* vm = createVM(NULL);

	printReplIntro();
	
	char line[MAX_LINE_LENGTH];
	for (;;) {
		if (!readInput(line, MAX_LINE_LENGTH))
			break;

		runReplInput(vm, line);
	}

	cardinalFreeVM(vm);
	return 0;
}

int main(int argc, const char* argv[]) {
	if (argc < 1 || argc > 3) {
		fprintf(stderr, "Usage: cardinal [debug] [file]\n");
		return 64; // EX_USAGE.
	}

	if (argc == 1) runRepl();
	else if (argc == 2) runFile(argv[1], "0");
	else if (argc == 3) runFile(argv[2], argv[1]);

	return 0;
}