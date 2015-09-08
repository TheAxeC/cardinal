#ifndef udog_h
#define udog_h

///////////////////////////////////////////////////////////////////////////////////
//// THE THUNDERDOG SCRIPTING LANGUAGE
//// 
//// author: Axel Faes 
///////////////////////////////////////////////////////////////////////////////////

// Some includes required for the VM
#include <stdlib.h> // for size_t
#include <stdint.h>  // for fixed size integer types
#include <stdbool.h> // to be able to use bools

// Version of the UDog Scripting Language
#define UDOG_VERSION "udog v1.1.0"

// In this library are 23 functions and 8 types (structs and unions) defined

///////////////////////////////////////////////////////////////////////////////////
//// Methods dealing with VM management.
////
//// [UDogVM] the Vm used for virtualy all interaction with the
//// scripting language.
//// 
//// [udogReallocateFn] is a function prototype used for memory allocation
//// within the scripting engine.
////
//// [UDogConfiguration] a configuration concerning memory usage
//// [udogNewVM] creates a new VM given some configuration
//// [udogFreeVM] frees a VM
////
///////////////////////////////////////////////////////////////////////////////////

// Define for the UDog Virtual machine
typedef struct UDogVM UDogVM;

// Define for the UDog values that you store inside the c++ host application
typedef struct UDogValue UDogValue;

// A generic allocation function that handles all explicit memory management
// used by UDog. It's used like so:
//
// - To allocate new memory, [memory] is NULL and [oldSize] is zero. It should
//   return the allocated memory or NULL on failure.
//
// - To attempt to grow an existing allocation, [memory] is the memory,
//   [oldSize] is its previous size, and [newSize] is the desired size.
//   It should return [memory] if it was able to grow it in place, or a new
//   pointer if it had to move it.
//
// - To shrink memory, [memory], [oldSize], and [newSize] are the same as above
//   but it will always return [memory].
//
// - To free memory, [memory] will be the memory to free and [newSize] and
//   [oldSize] will be zero. It should return NULL.
typedef void* (*udogReallocateFn)(void* memory, size_t oldSize, size_t newSize);

// Loads and returns the source code for the module [name].
typedef UDogValue* (*udogLoadModuleFn)(UDogVM* vm, const char* name);

// Callback used to print strings for debugging purposes
typedef int (*udogPrintCallBack)(const char* format, ...);

// Used for the callback function used by the debugger
typedef void (*udogCallBack) (UDogVM* vm);

/// The configuration needed for a new virtual machine
typedef struct UDogConfiguration {
	/// The callback UDog will use to allocate, reallocate, and deallocate memory.
	///
	/// If `NULL`, defaults to a built-in function that uses `realloc` and `free`.
	udogReallocateFn reallocateFn;
	
	/// The callback function used for the printing
	udogPrintCallBack printFn;
	
	/// The callback function used for the debugger
	udogCallBack debugCallback;
	
	/// The callback Udog uses to load a module.
	///
	/// Since Udog does not talk directly to the file system, it relies on the
	/// embedder to phyisically locate and read the source code for a module. The
	/// first time an import appears, Udog will call this and pass in the name of
	/// the module being imported. The VM should return the soure code for that
	/// module. Memory for the source should be allocated using [reallocateFn] and
	/// Udog will take ownership over it.
	///
	/// This will only be called once for any given module name. Udog caches the
	/// result internally so subsequent imports of the same module will use the
	/// previous source and not call this.
	///
	/// If a module with the given name could not be found by the embedder, it
	/// should return NULL and Udog will report that as a runtime error.
	udogLoadModuleFn loadModuleFn;
	
	/// UDog will grow (and shrink) the heap automatically as the number of bytes
	/// remaining in use after a collection changes. This number determines the
	/// amount of additional memory UDog will use after a collection, as a
	/// percentage of the current heap size.
	///
	/// For example, say that this is 50. After a garbage collection, UDog there
	/// are 400 bytes of memory still in use. That means the next collection will
	/// be triggered after a total of 600 bytes are allocated (including the 400
	/// already in use.
	///
	/// Setting this to a smaller number wastes less memory, but triggers more
	/// frequent garbage collections.
	///
	/// If zero, defaults to 50.
	int heapGrowthPercent;

	/// After a collection occurs, the threshold for the next collection is
	/// determined based on the number of bytes remaining in use. This allows UDog
	/// to shrink its memory usage automatically after reclaiming a large amount
	/// of memory.
	///
	/// This can be used to ensure that the heap does not get too small, which can
	/// in turn lead to a large number of collections afterwards as the heap grows
	/// back to a usable size.
	///
	/// If zero, defaults to 1MB.
	size_t minHeapSize;

	/// The number of bytes UDog will allocate before triggering the first garbage
	/// collection.
	///
	/// If zero, defaults to 10MB.
	size_t initialHeapSize;
	
	/// The root directoy
	const char* rootDirectory;
	
	/// Maximum stack size
	/// The default is 0 (sets max stack to 1mb)
	int stackMax;
	
	/// Maximum call depth size
	/// The default is 0 (sets max calldepth to 255)
	int callDepth;

} UDogConfiguration;

// Creates a new UDog virtual machine using the given [configuration]. UDog
// will copy the configuration data, so the argument passed to this can be
// freed after calling this. If [configuration] is `NULL`, uses a default
// configuration.
UDogVM* udogNewVM(UDogConfiguration* configuration);

// Disposes of all resources is use by [vm], which was previously created by a
// call to [udogNewVM].
void udogFreeVM(UDogVM* vm);

// Set the root directory
void setRootDirectory(UDogVM* vm, const char* path);

///////////////////////////////////////////////////////////////////////////////////
//// Methods dealing with running UDog Code.
////
//// [UDogValue] a value wrapper for the host application
//// [UDogValueParam] a value wrapper concerning function parameters.
//// [UDogLangResult] indicates the result from compiling/running code.
//// [UDogCode] indicates the type of the source code.
////
//// [udogInterpret] is a function which compiles the given source and 
//// executes any global code that was within the script.
////
//// [udogCompileScript] will only compile the script and return the created
//// function which contains the global code.
//// This can be thrown away if it is not required by the host application.
////
//// [udogRunFunction] will run a function created by [udogCompileScript].
///////////////////////////////////////////////////////////////////////////////////

// Gives an error code concerning the UDog Scripting Language
// Possible errors are:
//		- Compile error
// 	- Runtime error
//		- No error, succes running
typedef enum UDogLangResult {
	UDOG_COMPILE_ERROR,
	UDOG_RUNTIME_ERROR,
	UDOG_SUCCESS
} UDogLangResult;


// Runs [source], a string of UDog source code in a new fiber in [vm].
UDogLangResult udogInterpret(UDogVM* vm, const char* sourcePath, const char* source);

// Compiles [source], a string of UDog source code, to an [ObjFn] that will
// execute that code when invoked.
UDogValue* udogCompileScript(UDogVM* vm, const char* sourcePath, const char* source);

// Runs [source], a string of UDog source code in a new fiber in [vm].
UDogLangResult udogInterpretModule(UDogVM* vm, const char* sourcePath, const char* source, const char* module);

// Compiles [source], a string of UDog source code, to an [ObjFn] that will
// execute that code when invoked.
UDogValue* udogCompileScriptModule(UDogVM* vm, const char* sourcePath, const char* source, const char* module);

// Runs a fiber loaded with a function [val] in the virtual machine [vm].
UDogLangResult udogRunFunction(UDogVM* vm, UDogValue* val);

// Loads [source], a string of UDog byte code, to an [ObjFn] that will
// execute that code when invoked.
UDogValue* udogLoadByteCode(UDogVM* vm, const char* sourcePath, const char* source);

// Saves the entire state of the VM to a string
// The value has to be released by the host application
UDogValue* udogSaveByteCode(UDogVM* vm);

// Sets the debug mode of the VM [vm] to boolean value [set]
void udogSetDebugMode(UDogVM* vm, bool set);

///////////////////////////////////////////////////////////////////////////////////
//// Methods dealing with object management.
////
//// These functions can be used to dynamically create (and destroy) object.
//// They can then be stored within the host application.
//// From there on, they can be used to call methods on them.
//// 
//// [udogCreateValue] create a value
//// [udogCreateObject] creates objects
//// [udogBindObject] bind a given object (as a void*) to the VM as an instance
//// [udogCreateNumber] creates numbers
//// [udogCreateBool] creates boolean values
//// [udogCreateString] create string values
//// [udogGetInstance] gets the instance of an UDogValue
//// [udogGetBoolean] gets the bool value of an UDogValue
//// [udogGetNumber] gets the number value of an UDogValue
//// [udogGetString] gets the string value of an UDogValue
//// [udogReleaseObject] releases objects
//// [udogGetMethod] creates a method that operates on a variable from a module
//// [udogGetMethodObject] creates a method that operates on a host object
//// [udogCall] calls a created method
//// [createModule] creates a module
//// [removeModule] removes a module
//// [udogRemoveVariable] removes a variable
//// [udogRemoveMethod] removes a method from a variable
//// [udogRemoveMethodObject] removes a method from a given object
////
//// [udogCreateObjectList] create a new list
//// [udogObjectListAdd] adds an element to the list
//// [udogCreateObjectMap] create a new map
//// [udogObjectMapSet] sets a key in the map to a certain value
////
///////////////////////////////////////////////////////////////////////////////////

// Flush all host objects
void udogFlushHostObjects(UDogVM* vm);

// Get a top-level variable from a given module
UDogValue* getModuleVariable(UDogVM* vm, const char* module, const char* variable);

// Create a value
UDogValue* udogCreateValue(UDogVM* vm);

// Will create an object with from a name [className]
// signature is the correct template for new
// eg. new(_,_)
UDogValue* udogCreateObject(UDogVM* vm, const char* module, const char* className, const char* signature, int args, ...);

// Bind the object to the VM as an instance
UDogValue* udogBindObject(UDogVM* vm, const char* module, const char* className, void* obj, size_t size);

// Will create a number with value [num]
UDogValue* udogCreateNumber(UDogVM* vm, double num);

// Will create a bool with value [val]
UDogValue* udogCreateBool(UDogVM* vm, bool val);

// Will create a string with string [text] of length [length].
UDogValue* udogCreateString(UDogVM* vm, const char* text, int length);

// Get the object as a void*
// This assumes that the object is an instance
void* udogGetInstance(UDogVM* vm, UDogValue* val);

// Get the object as a bool
bool udogGetBoolean(UDogVM* vm, UDogValue* val);

// Get the object as a number
double udogGetNumber(UDogVM* vm, UDogValue* val);

// Get the object as a const char*
const char* udogGetString(UDogVM* vm, UDogValue* val);

// Release's a host object
void udogReleaseObject(UDogVM* vm, UDogValue* val);

// Get a method from the VM
UDogValue* udogGetMethod(UDogVM* vm, const char* module, const char* variable, const char* signature);

// Get a method from the VM with an host object
UDogValue* udogGetMethodObject(UDogVM* vm, const char* module, UDogValue* variable, const char* signature);

// Call a created method
UDogValue* udogCall(UDogVM* vm, UDogValue* method, int args, ...);

// Creates a module
void createModule(UDogVM* vm, const char* name);

// Removes a module
void removeModule(UDogVM* vm, const char* name);

// Removes a variable from the VM
void udogRemoveVariable(UDogVM* vm, const char* module, const char* variable);

// Get a method from the VM
void udogRemoveMethod(UDogVM* vm, const char* module, const char* variable, const char* signature);

// Get a method from the VM with an host object
void udogRemoveMethodObject(UDogVM* vm, UDogValue* variable, const char* signature);

//// 
//// The following methods expose lists
//// 

// Creates a new list
UDogValue* udogCreateObjectList(UDogVM* vm);

// Adds an element to the list
void udogObjectListAdd(UDogVM* vm, UDogValue* list, UDogValue* variable);

// Creates a new list
UDogValue* udogCreateObjectMap(UDogVM* vm);

// Adds an element to the list
void udogObjectMapSet(UDogVM* vm, UDogValue* list, UDogValue* key, UDogValue* val);


///////////////////////////////////////////////////////////////////////////////////
//// Methods below can be used to define classes and methods to the VM.
//// They should be used in conjunction with the defines from [udog_wrapper.h].
////
//// [udogDefineClass] defines a class from the host application.
///////////////////////////////////////////////////////////////////////////////////

// Defines a new class [classname] of size [size] in bytes. 
// If the class already exists, nothing will be done.
// An optional parent class can be provided [parent], this parameter should be NULL
// If there is no parent class
void udogDefineClass(UDogVM* vm, const char* module, const char* className, size_t size, const char* parent);

///////////////////////////////////////////////////////////////////////////////////
//// Methods below can be avoided by using the defines from [udog_wrapper.h].
//// Methods below are used to define methods (regular and static) to the VM.
////
//// The wrapper functions make it so that the host application doesnt need to
//// call these functions themselfs.
////
//// Below methods can come in handy when trying to add methods to classes
//// defined within the scripting language.
////
//// [udogDefineDestructor] defines a destructor on a class
//// [udogDefineMethod] defines a method on a class.
//// [udogDefineStaticMethod] defines a static method on a class.
///////////////////////////////////////////////////////////////////////////////////

// A foreign method from which it is possible to interact with a script
typedef void (*udogForeignMethodFn)(UDogVM* vm);

// A foreign method from which it is possible to interact with a script
typedef void (*udogDestructorFn)(void* obj);

// Defines a destructor [destructor] for the given class
// All instances of class [className] will call the destructor when the GC
// decides to destroy the object.
// The exact timing of the destruction can not be known.
// The destructors purpose is to clean up any manual memory from an instance
// of [className]
void udogDefineDestructor(UDogVM* vm, const char* module, const char* className, udogDestructorFn destructor);

// Defines a foreign method implemented by the host application. Looks for a
// global class named [className] to bind the method to. If not found, it will
// be created automatically.
//
// Defines a method on that class with [signature]. If a method already exists
// with that signature, it will be replaced. When invoked, the method will call
// [method].
void udogDefineMethod(UDogVM* vm, const char* module, const char* className,
                            const char* signature,
                            udogForeignMethodFn methodFn);


// Defines a static foreign method implemented by the host application. Looks
// for a global class named [className] to bind the method to. If not found, it
// will be created automatically.
//
// Defines a static method on that class with [signature]. If a method already
// exists with that signature, it will be replaced. When invoked, the method
// will call [method].
void udogDefineStaticMethod(UDogVM* vm, const char* module, const char* className,
                            const char* signature,
                            udogForeignMethodFn methodFn);


///////////////////////////////////////////////////////////////////////////////////
//// Methods below can be avoided by using the defines from [udog_wrapper.h]
//// Methods below are used to get arguments in a foreign method from the VM
//// Or they are used to return results to the VM.
////
//// The wrapper functions make it so that the host application doesnt need to
//// call these functions themselfs.
////
//// Below methods can come in handy when trying to add methods to classes
//// defined within the scripting language.
//// 
//// [udogGetArgument] receive an argument from the VM
//// [udogReturnValue] returns a value from a foreign method
//// [udogGetArgumentDouble] receive a double argument from the VM
//// [udogGetArgumentString] receive a string argument from the VM
//// [udogReturnDouble]  returns a double from a foreign method
//// [udogReturnNull] returns null from a foreign method
//// [udogReturnString] returns a string from a foreign method
//// [udogReturnBool] returns a bool from a foreign method
///////////////////////////////////////////////////////////////////////////////////

// The following functions read one of the arguments passed to a foreign call.
// They may only be called while within a function provided to
// [udogDefineMethod] or [udogDefineStaticMethod] that UDog has invoked.
//
// They retreive the argument at a given index which ranges from 0 to the number
// of parameters the method expects. The zeroth parameter is used for the
// receiver of the method. For example, given a foreign method "foo" on String
// invoked like:
//
//     "receiver".foo("one", "two", "three")
//
// The foreign function will be able to access the arguments like so:
//
//     0: "receiver"
//     1: "one"
//     2: "two"
//     3: "three"
//
// It is an error to pass an invalid argument index.

// Reads an argument for a foreign call. This must only be called within
// a function provided to [udogDefineMethod]. Retrieves the argument at [index]
// which ranges from 0 to the number of parameters the method expects - 1.
UDogValue* udogGetArgument(UDogVM* vm, int index);

// Reads an numeric argument for a foreign call. This must only be called within
// a function provided to [udogDefineMethod]. Retrieves the argument at [index]
// which ranges from 0 to the number of parameters the method expects - 1.
double udogGetArgumentDouble(UDogVM* vm, int index);

// Reads an string argument for a foreign call. This must only be called within
// a function provided to [udogDefineMethod]. Retrieves the argument at [index]
// which ranges from 0 to the number of parameters the method expects - 1.
//
// The memory for the returned string is owned by Udog. You can inspect it
// while in your foreign function, but cannot keep a pointer to it after the
// function returns, since the garbage collector may reclaim it.
const char* udogGetArgumentString(UDogVM* vm, int index);

// Reads a boolean argument for a foreign call. Returns false if the argument
// is not a boolean.
bool udogGetArgumentBool(UDogVM* vm, int index);




// The following functions provide the return value for a foreign method back
// to Udog. Like above, they may only be called during a foreign call invoked
// by Udog.
//
// If none of these is called by the time the foreign function returns, the
// method implicitly returns `null`. Within a given foreign call, you may only
// call one of these once. It is an error to access any of the foreign calls
// arguments after one of these has been called.

// Provides a return value for a foreign call. This must only be called
// within a function provided to [udogDefineMethod]. Once this is called, the
// foreign call is done, and no more arguments can be read or return calls made.
void udogReturnValue(UDogVM* vm, UDogValue* val);

// Provides a numeric return value for a foreign call. This must only be called
// within a function provided to [udogDefineMethod]. Once this is called, the
// foreign call is done, and no more arguments can be read or return calls made.
void udogReturnDouble(UDogVM* vm, double value);

// Provides a string return value for a foreign call. This must only be called
// within a function provided to [udogDefineMethod]. Once this is called, the
// foreign call is done, and no more arguments can be read or return calls made.
//
// The [text] will be copied to a new string within Udog's heap, so you can
// free memory used by it after this is called. If [length] is non-zero, Udog
// will copy that many bytes from [text]. If it is -1, then the length of
// [text] will be calculated using `strlen()`.
void udogReturnString(UDogVM* vm, const char* text, int length);

// Provides a boolean return value for a foreign call. This must only be called
// within a function provided to [udogDefineMethod]. Once this is called, the
// foreign call is done, and no more arguments can be read or return calls made.
void udogReturnBool(UDogVM* vm, bool value);

// Provides a null return value for a foreign call. This must only be called
// within a function provided to [udogDefineMethod]. Once this is called, the
// foreign call is done, and no more arguments can be read or return calls made.
void udogReturnNull(UDogVM* vm);

///////////////////////////////////////////////////////////////////////////////////
//// Include for the wrapper defines
///////////////////////////////////////////////////////////////////////////////////

#include "udog_wrapper.h"

#endif