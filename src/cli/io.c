#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"

char const* rootDirectory = NULL;

// Reads the contents of the file at [path] and returns it as a heap allocated
// string.
//
// Returns `NULL` if the path could not be found. Exits if it was found but
// could not be read.
char* readFile(const char* path) {
	FILE* file = fopen(path, "rb");
	if (file == NULL) return NULL;

	// Find out how big the file is.
	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	// Allocate a buffer for it.
	char* buffer = (char*)malloc(fileSize + 1);
	if (buffer == NULL) {
		fprintf(stderr, "Could not read file \"%s\".\n", path);
		exit(74);
	}

	// Read the entire file.
	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	if (bytesRead < fileSize) {
		fprintf(stderr, "Could not read file \"%s\".\n", path);
		exit(74);
	}

	// Terminate the string.
	buffer[bytesRead] = '\0';

	fclose(file);
	return buffer;
}

void printReplIntro() {
	printf("\\\\/\"-\n");
	printf(" \\_/   %s\n", UDOG_VERSION);
	printf("Thunderdog Script is a small, fast, class-based, Object Oriented scripting language. \n"
	       "The language is under heavy development and is subject to change. \n\n");
}

bool readInput(char* input, int maxSize) {
	printf("> ");
	
	int len = maxSize;
	char* begin = input;
	
	while (true) {
		// Read the current line
		if (!fgets(begin, len, stdin)) {
			printf("\n");
			return false;
		}
		// Compile when we 
		if (begin[0] == '\n') {
			break;
		}
		int slen = strlen(begin);
		begin = begin + slen;
		len += slen;
		
		printf(".. ");
	}
	return true;
}