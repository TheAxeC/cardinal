#include <stdio.h>
#include <string.h>

#include "io.h"
#include "vm.h"

CardinalVM* createVM(const char* path) {
	CardinalConfiguration config;

	// Since we're running in a standalone process, be generous with memory.
	config.initialHeapSize = 1024 * 1024 * 100;

	// Use defaults for these.
	config.printFn = NULL;
	config.loadModuleFn = NULL;
	config.reallocateFn = NULL;
	config.minHeapSize = 0;
	config.heapGrowthPercent = 0;
	config.debugCallback = NULL;
	config.stackMax = 0;
	config.callDepth = 0;
	
	// Set the path correctly
	config.rootDirectory = path;

	return cardinalNewVM(&config);
}

void runFile(const char* path, const char* debug) {
	char* source = readFile(path);
	if (source == NULL) {
		fprintf(stderr, "Could not find file \"%s\".\n", path);
		exit(66);
	}

	CardinalVM* vm = createVM(path);
	
	if (debug[0] == '1')
		cardinalSetDebugMode(vm, true);

	CardinalLangResult result = cardinalInterpret(vm, path, source);

	cardinalFreeVM(vm);
	free(source);

	// Exit with an error code if the script failed.
	if (result == CARDINAL_COMPILE_ERROR) exit(65); // EX_DATAERR.
	if (result == CARDINAL_RUNTIME_ERROR) exit(70); // EX_SOFTWARE.
}

// Runs input on the Repl
void runReplInput(CardinalVM* vm, const char* input) {
	if (strlen(input) <= 1) return;
	CardinalLangResult result = cardinalInterpret(vm, "Prompt", input);
		
	switch (result) {
		case CARDINAL_COMPILE_ERROR:
			printf("\x1b[0m\n  \x1b[1m\x1b[31merror:\x1b[0m compile error\n");
			break;
		case CARDINAL_RUNTIME_ERROR:
			printf("\x1b[0m\n  \x1b[1m\x1b[31merror:\x1b[0m runtime error\n");
			break;
		case CARDINAL_SUCCESS:
		default:
			break;
	}
}