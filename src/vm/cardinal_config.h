#ifndef cardinal_config_h
#define cardinal_config_h

// This header contains macros and defines used across the entire Cardinal
// implementation. In particular, it contains "configuration" defines that
// control how Cardinal works. Some of these are only used while hacking on
// debugging Cardinal itself.
//
// This header is *not* intended to be included by code outside of Cardinal itself.

#include "limits.h"

// Cardinal pervasively uses the C99 integer types (uint16_t, etc.) along with some
// of the associated limit constants (UINT32_MAX, etc.). The constants are not
// part of standard C++, so aren't included by default by C++ compilers when you
// include <stdint> unless __STDC_LIMIT_MACROS is defined.
#define __STDC_LIMIT_MACROS 1
#include <stdint.h>


///////////////////////////////////////////////////////////////////////////////////
//// SYSTEM
///////////////////////////////////////////////////////////////////////////////////

// CARDINAL_BITPLATFORM_XX designates whether the platform is 32 or 64 bits
// This is used to define different types such as
// 	- using floats or doubles
//		- using 32/64 bit ints
// Check windows
#if defined(_WIN32) || defined(_WIN64)
	#if defined(_WIN64)
		#define CARDINAL_BITPLATFORM_64
	#else
		#define CARDINAL_BITPLATFORM_32
	#endif
#endif

// Check GCC
#if defined(__GNUC__)
	#if defined(__x86_64__) || defined(__ppc64__)
		#define CARDINAL_BITPLATFORM_64
	#else
		#define CARDINAL_BITPLATFORM_32
	#endif
#endif

// If true, the compiled exe or library will contain the required
// code to load and save bytecode
// Defaults to on.
#ifndef CARDINAL_BYTECODE
	#define CARDINAL_BYTECODE 1
#endif

// If true, the compiled exe or library will contain the required
// code to debug code
// Defaults to on.
#ifndef CARDINAL_DEBUGGER
	#define CARDINAL_DEBUGGER 1
#endif

///////////////////////////////////////////////////////////////////////////////////
//// STANDARD LIBRARIES
///////////////////////////////////////////////////////////////////////////////////

// If true, loads the "IO" class in the standard library.
// Defaults to on.
#ifndef CARDINAL_USE_LIB_IO
	#define CARDINAL_USE_LIB_IO 1
#endif

// If true, uses the default file loader
// Defaults to on.
#ifndef CARDINAL_USE_DEFAULT_FILE_LOADER
	#define CARDINAL_USE_DEFAULT_FILE_LOADER 1
#endif

// If true, loads the "Regex" class in the standard library.
// Defaults to on.
#ifndef CARDINAL_USE_REGEX
	#define CARDINAL_USE_REGEX 1
#endif

// Use the VM's allocator to allocate an object of [type].
#define ALLOCATE(vm, type) \
    ((type*)cardinalReallocate(vm, NULL, 0, sizeof(type)))

// Use the VM's allocator to allocate an object of [mainType] containing a
// flexible array of [count] objects of [arrayType].
#define ALLOCATE_FLEX(vm, mainType, arrayType, count) \
    ((mainType*)cardinalReallocate(vm, NULL, 0, \
    sizeof(mainType) + sizeof(arrayType) * count))

// Use the VM's allocator to allocate an array of [count] elements of [type].
#define ALLOCATE_ARRAY(vm, type, count) \
    ((type*)cardinalReallocate(vm, NULL, 0, sizeof(type) * count))

// Use the VM's allocator to free the previously allocated memory at [pointer].
#define DEALLOCATE(vm, pointer) cardinalReallocate(vm, pointer, 0, 0)


///////////////////////////////////////////////////////////////////////////////////
//// NAN TAGGING AND COMPUTED GOTO
///////////////////////////////////////////////////////////////////////////////////

// These flags let you control some details of the interpreter's implementation.
// Usually they trade-off a bit of portability for speed. They default to the
// most efficient behavior.

// If true, then Cardinal will use a NaN-tagged double for its core value
// representation. Otherwise, it will use a larger more conventional struct.
// The former is significantly faster and more compact. The latter is useful for
// debugging and may be more portable.
//
// Defaults to on.
#ifndef CARDINAL_NAN_TAGGING
	#define CARDINAL_NAN_TAGGING 1
#endif

// If true, the VM's interpreter loop uses computed gotos. See this for more:
// http://gcc.gnu.org/onlinedocs/gcc-3.1.1/gcc/Labels-as-Values.html
// Enabling this speeds up the main dispatch loop a bit, but requires compiler
// support.
//
// Defaults to on.
#ifndef COMPUTED_GOTO
	#ifdef _MSC_VER
		// No computed gotos in Visual Studio.
		#define COMPUTED_GOTO 0
	#elif !defined(__cplusplus) && __STDC_VERSION__ >= 199901L
		#define COMPUTED_GOTO 1
	#elif defined(__cplusplus)
		#define COMPUTED_GOTO 1
	#else
		#define COMPUTED_GOTO 0
	#endif
#endif

// The Microsoft compiler does not support the "inline" modifier when compiling
// as plain C.
#if defined( _MSC_VER ) && !defined(__cplusplus)
	#define inline _inline
#endif

// This is used to clearly mark flexible-sized arrays that appear at the end of
// some dynamically-allocated structs, known as the "struct hack".
#if defined(__cplusplus) || (__STDC_VERSION__ >= 199901L)
	// In C99, a flexible array member is just "[]".
	#define FLEXIBLE_ARRAY
#else
	// Elsewhere, use a zero-sized array. It's technically undefined behavior, but
	// works reliably in most known compilers.
	#define FLEXIBLE_ARRAY 0
#endif

///////////////////////////////////////////////////////////////////////////////////
//// STACK
///////////////////////////////////////////////////////////////////////////////////

// The starting stack size of the vm
#define STACKSIZE 256

// The rate at which a stacksize's capacity grows when the size exceeds the current
// capacity. The new capacity will be determined by *multiplying* the old
// capacity by this. Growing geometrically is necessary to ensure that adding
// to a stacksize has O(1) amortized complexity.
#define STACKSIZE_GROW_FACTOR 1.7

// Used to prevent a stackoverflow
#define STACKSIZE_MAX 1024 * 1024 * 1 // 1mb

///////////////////////////////////////////////////////////////////////////////////
//// CALLFRAME
///////////////////////////////////////////////////////////////////////////////////

// The start amount of call frames to be active
// Used to prevent a stackoverflow
#define CALLFRAMESIZE 256

// Used to prevent a stackoverflow
#define CALLFRAME_MAX 256

// The rate at which a callframe's capacity grows when the size exceeds the current
// capacity. The new capacity will be determined by *multiplying* the old
// capacity by this. Growing geometrically is necessary to ensure that adding
// to a callframe has O(1) amortized complexity.
#define CALLFRAME_GROW_FACTOR 2

///////////////////////////////////////////////////////////////////////////////////
//// LIST AND TABLE
///////////////////////////////////////////////////////////////////////////////////

// The initial (and minimum) capacity of a non-empty list object.
#define LIST_MIN_CAPACITY (10)

// The initial (and minimum) capacity of a non-empty table object.
#define TABLE_MIN_CAPACITY (16)

// The rate at which a list's capacity grows when the size exceeds the current
// capacity. The new capacity will be determined by *multiplying* the old
// capacity by this. Growing geometrically is necessary to ensure that adding
// to a list has O(1) amortized complexity.
#define LIST_GROW_FACTOR (2)

// The rate at which a table's capacity grows when the size exceeds the current
// capacity. The new capacity will be determined by *multiplying* the old
// capacity by this. Growing geometrically is necessary to ensure that adding
// to a table has O(1) amortized complexity.
#define TABLE_GROW_FACTOR (2)

// The maximum percentage of map entries that can be filled before the map is
// grown. A lower load takes more memory but reduces collisions which makes
// lookup faster.
#define MAP_LOAD_PERCENT 75

///////////////////////////////////////////////////////////////////////////////////
//// MAXIMUMS FOR NAMES OF METHODS AND VARS
///////////////////////////////////////////////////////////////////////////////////

// The maximum number of arguments that can be passed to a method. Note that
// this limitation is hardcoded in other places in the VM, in particular, the
// `CODE_CALL_XX` instructions assume a certain maximum number.
#define MAX_PARAMETERS 16

// The maximum name of a method, not including the signature. This is an
// arbitrary but enforced maximum just so we know how long the method name
// strings need to be in the parser.
#define MAX_METHOD_NAME 64

// The maximum length of a method signature. This includes the name, and the
// extra spaces added to handle arity, and another byte to terminate the string.
#define MAX_METHOD_SIGNATURE (MAX_METHOD_NAME + MAX_PARAMETERS + 1)

// The maximum length of an identifier. The only real reason for this limitation
// is so that error messages mentioning variables can be stack allocated.
#define MAX_VARIABLE_NAME 64

///////////////////////////////////////////////////////////////////////////////////
//// UNSETTABLE
///////////////////////////////////////////////////////////////////////////////////

// The maximum number of methods that may be defined at one time.
#define MAX_METHODS 65536

// The maximum offset used for the bytecode to make jumps.
#define MAX_OFFSET 65536

// The maximum number of globals that may be defined at one time.
#define MAX_GLOBALS 65536

// The maximum number of fields a class can have, including inherited fields. 
// Note that [standard] it's 255 and not 256 because creating a class 
// takes the *number* of fields, not the *highest field index*.
#define MAX_FIELDS MAX_METHODS 

// The maximum number of local (i.e. non-global) variables that can be declared
// in a single function, method, or chunk of top level code. This is the
// maximum number of variables in scope at one time, and spans block scopes.
#define MAX_LOCALS (256)

// The maximum number of upvalues (i.e. variables from enclosing functions)
// that a function can close over.
#define MAX_UPVALUES MAX_LOCALS //(256)

// The maximum number of distinct constants that a function can contain.
#define MAX_CONSTANTS 65536

///////////////////////////////////////////////////////////////////////////////////
//// DEBUG VARIABLES
///////////////////////////////////////////////////////////////////////////////////

// These flags are useful for debugging and hacking on Cardinal itself. They are not
// intended to be used for production code. They default to off.

// Set this to true to stress test the GC. It will perform a collection before
// every allocation. This is useful to ensure that memory is always correctly
// pinned.
#define CARDINAL_DEBUG_GC_STRESS 0

// Set this to true to log memory operations as they occur.
#define CARDINAL_DEBUG_TRACE_MEMORY 0

// Set this to true to log all releases from the GC
#define CARDINAL_DEBUG_TRACE_FREE 0

// Set this to true to log garbage collections as they occur.
#define CARDINAL_DEBUG_TRACE_GC 0

// Set this to true to print out the compiled bytecode of each function.
#define CARDINAL_DEBUG_DUMP_COMPILED_CODE 0

// Set this to trace each instruction as it's executed.
#define CARDINAL_DEBUG_TRACE_INSTRUCTIONS 0

// Set this to true to print the code that will be bound to a specific class
#define CARDINAL_DEBUG_DUMP_BOUND_CODE 0

///////////////////////////////////////////////////////////////////////////////////
//// USEFUL DEFINE
///////////////////////////////////////////////////////////////////////////////////

// Designates an unused parameter.
// Sometimes parameters might be passed to confirm to coding standards but
// the parameter might be unnecessary, or might only be used in debug builds.
// This define stops the compiler from complaining about unused parameters.
#define UNUSED(x) (void) x

// Assertions are used to validate program invariants. They indicate things the
// program expects to be true about its internal state during execution. If an
// assertion fails, there is a bug.
//
// Assertions add significant overhead, so are only enabled in debug builds.
#ifdef DEBUG
	#include <stdio.h>

	#define ASSERT(condition, message) \
		if (!(condition)) {\
		  fprintf(stderr, "[%s:%d] Assert failed in %s(): %s\n", \
			  __FILE__, __LINE__, __func__, message); \
		  abort(); \
		}

	// Assertion to indicate that the given point in the code should never be
	// reached.
	#define UNREACHABLE(str) \
		fprintf(stderr, "This line should be unreachable: %s\n", str); \
		abort();
#else
	#define ASSERT(condition, message) 
	#define UNREACHABLE(str) 
#endif

///////////////////////////////////////////////////////////////////////////////////
//// DONT TOUCH THESE DEFINE
//// OTHERWISE STUFF WONT WORK
///////////////////////////////////////////////////////////////////////////////////

// define for the number of bytes needed to 
// represent the globals in the bytecode
#if (defined ULLONG_MAX) and MAX_GLOBALS - 1 > ULLONG_MAX
	#error more than 8 byte numbers not supported for globals
#elif MAX_GLOBALS - 1 > ULONG_MAX 
	#define GLOBAL_BYTE 8
#elif MAX_GLOBALS - 1 > USHRT_MAX 
	#define GLOBAL_BYTE 4
#elif MAX_GLOBALS - 1 > UCHAR_MAX 
	#define GLOBAL_BYTE 2
#else
	#define GLOBAL_BYTE 1
#endif

// define for the number of bytes needed to 
// represent the fields in the bytecode

#if MAX_FIELDS - 1 > ULONG_MAX
	#error more than 4 byte numbers not supported for fields
#elif MAX_FIELDS - 1 > USHRT_MAX 
	#define FIELD_BYTE 4
#elif MAX_FIELDS - 1 > UCHAR_MAX 
	#define FIELD_BYTE 2
#else
	#define FIELD_BYTE 1
#endif
	
// define for the number of bytes needed to 
// represent the constants in the bytecode
#if (defined ULLONG_MAX) and MAX_CONSTANTS - 1 > ULLONG_MAX
	#error more than 8 byte numbers not supported for constants
#elif MAX_CONSTANTS - 1 > ULONG_MAX 
	#define CONSTANT_BYTE 8
#elif MAX_CONSTANTS - 1 > USHRT_MAX 
	#define CONSTANT_BYTE 4
#elif MAX_CONSTANTS - 1 > UCHAR_MAX 
	#define CONSTANT_BYTE 2
#else
	#define CONSTANT_BYTE 1
#endif

// define for the number of bytes needed to 
// represent the upvalues in the bytecode
#if (defined ULLONG_MAX) and MAX_UPVALUES - 1 > ULLONG_MAX
	#error more than 8 byte numbers not supported for upvalues
#elif MAX_UPVALUES - 1 > ULONG_MAX 
	#define UPVALUE_BYTE 8
#elif MAX_UPVALUES - 1 > USHRT_MAX 
	#define UPVALUE_BYTE 4
#elif MAX_UPVALUES - 1 > UCHAR_MAX 
	#define UPVALUE_BYTE 2
#else
	#define UPVALUE_BYTE 1
#endif

// define for the number of bytes needed to 
// represent the locals in the bytecode
#if (defined ULLONG_MAX) and MAX_LOCALS - 1 > ULLONG_MAX
	#error more than 8 byte numbers not supported for locals
#elif MAX_LOCALS - 1 > ULONG_MAX 
	#define LOCAL_BYTE 8
#elif MAX_LOCALS - 1 > USHRT_MAX 
	#define LOCAL_BYTE 4
#elif MAX_LOCALS - 1 > UCHAR_MAX 
	#define LOCAL_BYTE 2
#else
	#define LOCAL_BYTE 1
#endif

// define for the number of bytes needed to 
// represent the locals in the bytecode
#if (defined ULLONG_MAX) and MAX_OFFSET - 1 > ULLONG_MAX
	#error more than 8 byte numbers not supported for offsets
#elif MAX_OFFSET - 1 > ULONG_MAX 
	#define OFFSET_BYTE 8
#elif MAX_OFFSET - 1 > USHRT_MAX 
	#define OFFSET_BYTE 4
#else
	#define OFFSET_BYTE 2
#endif

// define for the number of bytes needed to 
// represent the locals in the bytecode
#if MAX_METHODS - 1 > ULONG_MAX
	#error more than 4 byte numbers not supported for methods
#elif MAX_METHODS - 1 > USHRT_MAX 
	#define METHOD_BYTE 4
#else
	#define METHOD_BYTE 2
#endif

#endif
