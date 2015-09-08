#include "udog_file.h"

#if UDOG_USE_DEFAULT_FILE_LOADER

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "udog.h"
#include "udog_vm.h"
#include "udog_debug.h"

UDogValue* defaultModuleLoader(UDogVM* vm, const char* module) {
	const char* rootDirectory = vm->rootDirectory->value;
	if (vm->rootDirectory == NULL)
		rootDirectory = "";
	
	// The module path is relative to the root directory and with ".tus".
	size_t rootLength = strlen(rootDirectory);
	size_t moduleLength = strlen(module);
	size_t pathLength = rootLength + moduleLength + 4;
	char* path = (char*)malloc(pathLength + 1);
	memcpy(path, rootDirectory, rootLength);
	memcpy(path + rootLength, module, moduleLength);
	memcpy(path + rootLength + moduleLength, ".tus", 4);
	path[pathLength] = '\0';

	FILE* file = fopen(path, "rb");
	if (file == NULL) {
		errnScript(vm,"error opening file: '%s'\n", path);
		free(path);
		return NULL;
	}

	// Find out how big the file is.
	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	// Allocate a buffer for it.
	char* buffer = (char*)malloc(fileSize + 1);
	if (buffer == NULL) {
		errnScript(vm,"error allocating memory: '%d'\n", fileSize + 1);
		fclose(file);
		free(path);
		return NULL;
	}

	// Read the entire file.
	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	if (bytesRead < fileSize) {
		errnScript(vm,"error reading file: '%s'\nCould only read %d lines", path, bytesRead);
		free(buffer);
		fclose(file);
		free(path);
		
		return NULL;
	}

	// Terminate the string.
	buffer[bytesRead] = '\0';
	
	UDogValue* ret = udogCreateString(vm, buffer, bytesRead);

	fclose(file);
	free(path);
	free(buffer);

	return ret;
}

///////////////////////////////////////////////////////////////////////////////////
//// FILE LIBRARY
///////////////////////////////////////////////////////////////////////////////////

// Reads the contents of the file at [path] and returns it as a heap allocated
// string.
// Returns `NULL` if the path could not be found. Exits if it was found but
// could not be read.
static char* readFile(const char* path, int* size) {
	FILE* file = fopen(path, "rb");
	if (file == NULL) return NULL;

	// Find out how big the file is.
	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	// Allocate a buffer for it.
	char* buffer = (char*)malloc(fileSize + 1);
	if (buffer == NULL) return NULL;

	// Read the entire file.
	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	if (bytesRead < fileSize) return NULL;

	// Terminate the string.
	buffer[bytesRead] = '\0';
	
	*size = bytesRead;
	fclose(file);
	return buffer;
}

static bool writeFile(const char* path, const char* content) {
	FILE *f = fopen(path, "w");
	if (f == NULL) return false;

	if (fprintf(f, "%s", content) < 0) return false;

	fclose(f);
	return true;
}

static void fileRead(UDogVM* vm) {
	// get the file path
	const char* fileName = udogGetArgumentString(vm, 1);
	int size = -1;
	// read the file
	char* file = readFile(fileName, &size);
	if (file == NULL) {
		udogReturnString(vm, "file not found", 14);
	}
	else {
		// Return the string (is copied)
		udogReturnString(vm, file, size);
		// free the file
		free(file);
	}
}

static void fileWrite(UDogVM* vm) {
	// get the file path
	const char* fileName = udogGetArgumentString(vm, 1);
	// get the file content
	const char* fileContent = udogGetArgumentString(vm, 2);
	
	bool succes = writeFile(fileName, fileContent);
	udogReturnBool(vm, succes);
}

// This module defines the FILE class and its associated methods. They are
// implemented using the C standard library.
void udogLoadFileLibrary(UDogVM* vm) {
	udogDefineStaticMethod(vm, NULL, "File", "readFile(_)", fileRead);
	udogDefineStaticMethod(vm, NULL, "File", "writeFile(_,_)", fileWrite);
}

#endif