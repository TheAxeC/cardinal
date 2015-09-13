#ifndef cardinal_h
#define cardinal_h

///////////////////////////////////////////////////////////////////////////////////
//// THE CARDINAL SCRIPTING LANGUAGE
//// 
//// author: Axel Faes 
///////////////////////////////////////////////////////////////////////////////////

// Some includes required for the VM
#include <stdlib.h> // for size_t
#include <stdint.h>  // for fixed size integer types
#include <stdbool.h> // to be able to use bools

// Version of the Cardinal Scripting Language
#define CARDINAL_VERSION "cardinal v1.1.0"

#define CARDINAL_EXT ".crd"

///////////////////////////////////////////////////////////////////////////////////
//// Methods dealing with VM management.
////
//// [CardinalVM] the Vm used for virtualy all interaction with the
//// scripting language.
//// 
//// [cardinalReallocateFn] is a function prototype used for memory allocation
//// within the scripting engine.
////
//// [CardinalConfiguration] a configuration concerning memory usage
//// [cardinalNewVM] creates a new VM given some configuration
//// [cardinalFreeVM] frees a VM
////
///////////////////////////////////////////////////////////////////////////////////

// Define for the Cardinal Virtual machine
typedef struct CardinalVM CardinalVM;

// Define for the Cardinal values that you store inside the c++ host application
typedef struct CardinalValue CardinalValue;

// A generic allocation function that handles all explicit memory management
// used by Cardinal. It's used like so:
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
typedef void* (*cardinalReallocateFn)(void* memory, size_t oldSize, size_t newSize);

// Loads and returns the source code for the module [name].
typedef CardinalValue* (*cardinalLoadModuleFn)(CardinalVM* vm, const char* name);

// Callback used to print strings for debugging purposes
typedef int (*cardinalPrintCallBack)(const char* format, ...);

// Used for the callback function used by the debugger
typedef void (*cardinalCallBack) (CardinalVM* vm);

/// The configuration needed for a new virtual machine
typedef struct CardinalConfiguration {
	/// The callback Cardinal will use to allocate, reallocate, and deallocate memory.
	///
	/// If `NULL`, defaults to a built-in function that uses `realloc` and `free`.
	cardinalReallocateFn reallocateFn;
	
	/// The callback function used for the printing
	cardinalPrintCallBack printFn;
	
	/// The callback function used for the debugger
	cardinalCallBack debugCallback;
	
	/// The callback Cardinal uses to load a module.
	///
	/// Since Cardinal does not talk directly to the file system, it relies on the
	/// embedder to phyisically locate and read the source code for a module. The
	/// first time an import appears, Cardinal will call this and pass in the name of
	/// the module being imported. The VM should return the soure code for that
	/// module. Memory for the source should be allocated using [reallocateFn] and
	/// Cardinal will take ownership over it.
	///
	/// This will only be called once for any given module name. Cardinal caches the
	/// result internally so subsequent imports of the same module will use the
	/// previous source and not call this.
	///
	/// If a module with the given name could not be found by the embedder, it
	/// should return NULL and Cardinal will report that as a runtime error.
	cardinalLoadModuleFn loadModuleFn;
	
	/// Cardinal will grow (and shrink) the heap automatically as the number of bytes
	/// remaining in use after a collection changes. This number determines the
	/// amount of additional memory Cardinal will use after a collection, as a
	/// percentage of the current heap size.
	///
	/// For example, say that this is 50. After a garbage collection, Cardinal there
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
	/// determined based on the number of bytes remaining in use. This allows Cardinal
	/// to shrink its memory usage automatically after reclaiming a large amount
	/// of memory.
	///
	/// This can be used to ensure that the heap does not get too small, which can
	/// in turn lead to a large number of collections afterwards as the heap grows
	/// back to a usable size.
	///
	/// If zero, defaults to 1MB.
	size_t minHeapSize;

	/// The number of bytes Cardinal will allocate before triggering the first garbage
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

} CardinalConfiguration;

// Creates a new Cardinal virtual machine using the given [configuration]. Cardinal
// will copy the configuration data, so the argument passed to this can be
// freed after calling this. If [configuration] is `NULL`, uses a default
// configuration.
CardinalVM* cardinalNewVM(CardinalConfiguration* configuration);

// Disposes of all resources is use by [vm], which was previously created by a
// call to [cardinalNewVM].
void cardinalFreeVM(CardinalVM* vm);

// Set the root directory
void cardinalSetRootDirectory(CardinalVM* vm, const char* path);


///////////////////////////////////////////////////////////////////////////////////
//// Methods dealing with running Cardinal Code.
////
//// [CardinalValue] a value wrapper for the host application
//// [CardinalValueParam] a value wrapper concerning function parameters.
//// [CardinalLangResult] indicates the result from compiling/running code.
//// [CardinalCode] indicates the type of the source code.
////
//// [cardinalInterpret] is a function which compiles the given source and 
//// executes any global code that was within the script.
////
//// [cardinalCompileScript] will only compile the script and return the created
//// function which contains the global code.
//// This can be thrown away if it is not required by the host application.
////
//// [cardinalRunFunction] will run a function created by [cardinalCompileScript].
///////////////////////////////////////////////////////////////////////////////////

// Gives an error code concerning the Cardinal Scripting Language
// Possible errors are:
//		- Compile error
// 	- Runtime error
//		- No error, succes running
typedef enum CardinalLangResult {
	CARDINAL_COMPILE_ERROR,
	CARDINAL_RUNTIME_ERROR,
	CARDINAL_SUCCESS
} CardinalLangResult;


// Runs [source], a string of Cardinal source code in a new fiber in [vm].
CardinalLangResult cardinalInterpret(CardinalVM* vm, const char* sourcePath, const char* source);

// Compiles [source], a string of Cardinal source code, to an [ObjFn] that will
// execute that code when invoked.
CardinalValue* cardinalCompileScript(CardinalVM* vm, const char* sourcePath, const char* source);

// Runs [source], a string of Cardinal source code in a new fiber in [vm].
CardinalLangResult cardinalInterpretModule(CardinalVM* vm, const char* sourcePath, const char* source, const char* module);

// Compiles [source], a string of Cardinal source code, to an [ObjFn] that will
// execute that code when invoked.
CardinalValue* cardinalCompileScriptModule(CardinalVM* vm, const char* sourcePath, const char* source, const char* module);

// Runs a fiber loaded with a function [val] in the virtual machine [vm].
CardinalLangResult cardinalRunFunction(CardinalVM* vm, CardinalValue* val);

// Loads [source], a string of Cardinal byte code, to an [ObjFn] that will
// execute that code when invoked.
CardinalValue* cardinalLoadByteCode(CardinalVM* vm, const char* sourcePath, const char* source);

// Saves the entire state of the VM to a string
// The value has to be released by the host application
CardinalValue* cardinalSaveByteCode(CardinalVM* vm);

// Sets the debug mode of the VM [vm] to boolean value [set]
void cardinalSetDebugMode(CardinalVM* vm, bool set);

///////////////////////////////////////////////////////////////////////////////////
//// Methods dealing with object management.
////
//// These functions can be used to dynamically create (and destroy) object.
//// They can then be stored within the host application.
//// From there on, they can be used to call methods on them.
//// 
//// [cardinalCreateValue] create a value
//// [cardinalCreateObject] creates objects
//// [cardinalBindObject] bind a given object (as a void*) to the VM as an instance
//// [cardinalCreateNumber] creates numbers
//// [cardinalCreateBool] creates boolean values
//// [cardinalCreateString] create string values
//// [cardinalGetInstance] gets the instance of an CardinalValue
//// [cardinalGetBoolean] gets the bool value of an CardinalValue
//// [cardinalGetNumber] gets the number value of an CardinalValue
//// [cardinalGetString] gets the string value of an CardinalValue
//// [cardinalReleaseObject] releases objects
//// [cardinalGetMethod] creates a method that operates on a variable from a module
//// [cardinalGetMethodObject] creates a method that operates on a host object
//// [cardinalCall] calls a created method
//// [createModule] creates a module
//// [removeModule] removes a module
//// [cardinalRemoveVariable] removes a variable
//// [cardinalRemoveMethod] removes a method from a variable
//// [cardinalRemoveMethodObject] removes a method from a given object
////
//// [cardinalCreateObjectList] create a new list
//// [cardinalObjectListAdd] adds an element to the list
//// [cardinalCreateObjectMap] create a new map
//// [cardinalObjectMapSet] sets a key in the map to a certain value
////
///////////////////////////////////////////////////////////////////////////////////

// Flush all host objects
void cardinalFlushHostObjects(CardinalVM* vm);

// Get a top-level variable from a given module
CardinalValue* getModuleVariable(CardinalVM* vm, const char* module, const char* variable);

// Create a value
CardinalValue* cardinalCreateValue(CardinalVM* vm);

// Will create an object with from a name [className]
// signature is the correct template for new
// eg. new(_,_)
CardinalValue* cardinalCreateObject(CardinalVM* vm, const char* module, const char* className, const char* signature, int args, ...);

// Bind the object to the VM as an instance
CardinalValue* cardinalBindObject(CardinalVM* vm, const char* module, const char* className, void* obj, size_t size);

// Will create a number with value [num]
CardinalValue* cardinalCreateNumber(CardinalVM* vm, double num);

// Will create a bool with value [val]
CardinalValue* cardinalCreateBool(CardinalVM* vm, bool val);

// Will create a string with string [text] of length [length].
CardinalValue* cardinalCreateString(CardinalVM* vm, const char* text, int length);

// Get the object as a void*
// This assumes that the object is an instance
void* cardinalGetInstance(CardinalVM* vm, CardinalValue* val);

// Get the object as a bool
bool cardinalGetBoolean(CardinalVM* vm, CardinalValue* val);

// Get the object as a number
double cardinalGetNumber(CardinalVM* vm, CardinalValue* val);

// Get the object as a const char*
const char* cardinalGetString(CardinalVM* vm, CardinalValue* val);

// Release's a host object
void cardinalReleaseObject(CardinalVM* vm, CardinalValue* val);

// Get a method from the VM
CardinalValue* cardinalGetMethod(CardinalVM* vm, const char* module, const char* variable, const char* signature);

// Get a method from the VM with an host object
CardinalValue* cardinalGetMethodObject(CardinalVM* vm, const char* module, CardinalValue* variable, const char* signature);

// Call a created method
CardinalValue* cardinalCall(CardinalVM* vm, CardinalValue* method, int args, ...);

// Creates a module
void createModule(CardinalVM* vm, const char* name);

// Removes a module
void removeModule(CardinalVM* vm, const char* name);

// Removes a variable from the VM
void cardinalRemoveVariable(CardinalVM* vm, const char* module, const char* variable);

// Get a method from the VM
void cardinalRemoveMethod(CardinalVM* vm, const char* module, const char* variable, const char* signature);

// Get a method from the VM with an host object
void cardinalRemoveMethodObject(CardinalVM* vm, CardinalValue* variable, const char* signature);

//// 
//// The following methods expose lists
//// 

// Creates a new list
CardinalValue* cardinalCreateObjectList(CardinalVM* vm);

// Adds an element to the list
void cardinalObjectListAdd(CardinalVM* vm, CardinalValue* list, CardinalValue* variable);

// Creates a new list
CardinalValue* cardinalCreateObjectMap(CardinalVM* vm);

// Adds an element to the list
void cardinalObjectMapSet(CardinalVM* vm, CardinalValue* list, CardinalValue* key, CardinalValue* val);


///////////////////////////////////////////////////////////////////////////////////
//// Methods below can be used to define classes and methods to the VM.
//// They should be used in conjunction with the defines from [cardinal_wrapper.h].
////
//// [cardinalDefineClass] defines a class from the host application.
///////////////////////////////////////////////////////////////////////////////////

// Defines a new class [classname] of size [size] in bytes. 
// If the class already exists, nothing will be done.
// An optional parent class can be provided [parent], this parameter should be NULL
// If there is no parent class
void cardinalDefineClass(CardinalVM* vm, const char* module, const char* className, size_t size, const char* parent);

///////////////////////////////////////////////////////////////////////////////////
//// Methods below can be avoided by using the defines from [cardinal_wrapper.h].
//// Methods below are used to define methods (regular and static) to the VM.
////
//// The wrapper functions make it so that the host application doesnt need to
//// call these functions themselfs.
////
//// Below methods can come in handy when trying to add methods to classes
//// defined within the scripting language.
////
//// [cardinalDefineDestructor] defines a destructor on a class
//// [cardinalDefineMethod] defines a method on a class.
//// [cardinalDefineStaticMethod] defines a static method on a class.
///////////////////////////////////////////////////////////////////////////////////

// A foreign method from which it is possible to interact with a script
typedef void (*cardinalForeignMethodFn)(CardinalVM* vm);

// A foreign method from which it is possible to interact with a script
typedef void (*cardinalDestructorFn)(void* obj);

// Defines a destructor [destructor] for the given class
// All instances of class [className] will call the destructor when the GC
// decides to destroy the object.
// The exact timing of the destruction can not be known.
// The destructors purpose is to clean up any manual memory from an instance
// of [className]
void cardinalDefineDestructor(CardinalVM* vm, const char* module, const char* className, cardinalDestructorFn destructor);

// Defines a foreign method implemented by the host application. Looks for a
// global class named [className] to bind the method to. If not found, it will
// be created automatically.
//
// Defines a method on that class with [signature]. If a method already exists
// with that signature, it will be replaced. When invoked, the method will call
// [method].
void cardinalDefineMethod(CardinalVM* vm, const char* module, const char* className,
                            const char* signature,
                            cardinalForeignMethodFn methodFn);


// Defines a static foreign method implemented by the host application. Looks
// for a global class named [className] to bind the method to. If not found, it
// will be created automatically.
//
// Defines a static method on that class with [signature]. If a method already
// exists with that signature, it will be replaced. When invoked, the method
// will call [method].
void cardinalDefineStaticMethod(CardinalVM* vm, const char* module, const char* className,
                            const char* signature,
                            cardinalForeignMethodFn methodFn);


///////////////////////////////////////////////////////////////////////////////////
//// Methods below can be avoided by using the defines from [cardinal_wrapper.h]
//// Methods below are used to get arguments in a foreign method from the VM
//// Or they are used to return results to the VM.
////
//// The wrapper functions make it so that the host application doesnt need to
//// call these functions themselfs.
////
//// Below methods can come in handy when trying to add methods to classes
//// defined within the scripting language.
//// 
//// [cardinalGetArgument] receive an argument from the VM
//// [cardinalReturnValue] returns a value from a foreign method
//// [cardinalGetArgumentDouble] receive a double argument from the VM
//// [cardinalGetArgumentString] receive a string argument from the VM
//// [cardinalReturnDouble]  returns a double from a foreign method
//// [cardinalReturnNull] returns null from a foreign method
//// [cardinalReturnString] returns a string from a foreign method
//// [cardinalReturnBool] returns a bool from a foreign method
///////////////////////////////////////////////////////////////////////////////////

// The following functions read one of the arguments passed to a foreign call.
// They may only be called while within a function provided to
// [cardinalDefineMethod] or [cardinalDefineStaticMethod] that Cardinal has invoked.
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
// a function provided to [cardinalDefineMethod]. Retrieves the argument at [index]
// which ranges from 0 to the number of parameters the method expects - 1.
CardinalValue* cardinalGetArgument(CardinalVM* vm, int index);

// Reads an numeric argument for a foreign call. This must only be called within
// a function provided to [cardinalDefineMethod]. Retrieves the argument at [index]
// which ranges from 0 to the number of parameters the method expects - 1.
double cardinalGetArgumentDouble(CardinalVM* vm, int index);

// Reads an string argument for a foreign call. This must only be called within
// a function provided to [cardinalDefineMethod]. Retrieves the argument at [index]
// which ranges from 0 to the number of parameters the method expects - 1.
//
// The memory for the returned string is owned by Udog. You can inspect it
// while in your foreign function, but cannot keep a pointer to it after the
// function returns, since the garbage collector may reclaim it.
const char* cardinalGetArgumentString(CardinalVM* vm, int index);

// Reads a boolean argument for a foreign call. Returns false if the argument
// is not a boolean.
bool cardinalGetArgumentBool(CardinalVM* vm, int index);




// The following functions provide the return value for a foreign method back
// to Udog. Like above, they may only be called during a foreign call invoked
// by Udog.
//
// If none of these is called by the time the foreign function returns, the
// method implicitly returns `null`. Within a given foreign call, you may only
// call one of these once. It is an error to access any of the foreign calls
// arguments after one of these has been called.

// Provides a return value for a foreign call. This must only be called
// within a function provided to [cardinalDefineMethod]. Once this is called, the
// foreign call is done, and no more arguments can be read or return calls made.
void cardinalReturnValue(CardinalVM* vm, CardinalValue* val);

// Provides a numeric return value for a foreign call. This must only be called
// within a function provided to [cardinalDefineMethod]. Once this is called, the
// foreign call is done, and no more arguments can be read or return calls made.
void cardinalReturnDouble(CardinalVM* vm, double value);

// Provides a string return value for a foreign call. This must only be called
// within a function provided to [cardinalDefineMethod]. Once this is called, the
// foreign call is done, and no more arguments can be read or return calls made.
//
// The [text] will be copied to a new string within Udog's heap, so you can
// free memory used by it after this is called. If [length] is non-zero, Udog
// will copy that many bytes from [text]. If it is -1, then the length of
// [text] will be calculated using `strlen()`.
void cardinalReturnString(CardinalVM* vm, const char* text, int length);

// Provides a boolean return value for a foreign call. This must only be called
// within a function provided to [cardinalDefineMethod]. Once this is called, the
// foreign call is done, and no more arguments can be read or return calls made.
void cardinalReturnBool(CardinalVM* vm, bool value);

// Provides a null return value for a foreign call. This must only be called
// within a function provided to [cardinalDefineMethod]. Once this is called, the
// foreign call is done, and no more arguments can be read or return calls made.
void cardinalReturnNull(CardinalVM* vm);


#endif