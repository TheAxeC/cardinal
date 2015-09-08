/*
 * main.cpp
 *
 *  Created on: 27 Nov 2014
 *      Author: axel
 *
 */
#include <stdio.h>
#include <string.h>

#include "io.h"
#include "vm.h"
#include "udog.h"
#include "../vm/udog_vm.h"
#include "../vm/udog_value.h"
#include "../vm/udog_debug.h"

#define MAX_LINE_LENGTH 1024 // TODO: Something less arbitrary.

static int runRepl() {
	UDogVM* vm = createVM(NULL);

	printReplIntro();
	
	char line[MAX_LINE_LENGTH];
	for (;;) {
		if (!readInput(line, MAX_LINE_LENGTH))
			break;

		runReplInput(vm, line);
	}

	udogFreeVM(vm);
	return 0;
}

int main(int argc, const char* argv[]) {
	if (argc < 1 || argc > 3) {
		fprintf(stderr, "Usage: udog [debug] [file]\n");
		return 64; // EX_USAGE.
	}

	if (argc == 1) runRepl();
	else if (argc == 2) runFile(argv[1], "0");
	else if (argc == 3) runFile(argv[2], argv[1]);

	return 0;
}