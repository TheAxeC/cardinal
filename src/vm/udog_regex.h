#ifndef udog_regex_h
#define udog_regex_h

#include "udog.h"
#include "udog_value.h"

// This file defines a couple of functions that form a regex engine
// The engine is written to be as compact as possible
// There is nearly no dynamic memory involved
// It provides a simple interface and is easily extensible
//
// The engine can be used in normal C or C++ projects,
// But it is designed to integrate with ThunderdogScript
//
// The following expressions are supported by the Engine
//		x		where x is an element from the 
//					alphabet supported by the regex
// 		\		Quote the next character
//		^		Match the beginning of the string
//		.		Match any character
//		$		Match the end of the string
//		|		Alternation
//		()		Grouping (creates a capture group)
//		[]		Character class  
//
// 		*	  	Match 0 or more times
// 		+	   	Match 1 or more times
// 		?	   	Match 1 or 0 times
// 		{n}    	Match exactly n times
// 		{n,}   	Match at least n times
// 		{n,m}  	Match at least n but not more than m times  
//
//		\t		tab (HT, TAB)
//		\n		newline (LF, NL)
//		\r		return (CR)
//		\f		form feed (FF)
//
//		\l		lowercase next char
//		\u		uppercase next char
//		\a		letters
//		\A		non letters
//		\w		alphanimeric [0-9a-zA-Z]
//		\W		non alphanimeric
//		\s		space
//		\S		non space
//		\d		digits
//		\D		non nondigits
//		\x		exadecimal digits
//		\X		non exadecimal digits
//		\c		control charactrs
//		\C		non control charactrs
//		\p		punctation
//		\P		non punctation
//		\b		word boundary
//		\B		non word boundary

/// Struct containing the compiled regex expression
/// The inputted string is compiled into binary data that
/// is easier and faster to read by the regex engine
typedef struct UDogRegex UDogRegex;

/// A match in a regex expression
typedef struct {
	/// Beginning of the match
	const char* begin;
	/// Length of the match
	int len;
} UDogRegexMatch;

/**
 * @brief Compiles a pattern [pattern] into a regex [UDogRegex]
 * @param pattern, the pattern that needs to be compiled
 * @param error, here the error message gets printed into in case of an error
 * @return the compiled pattern or NULL (in case of failure)
 */
UDogRegex* udogCompileRegex(const char* pattern, const char** error);

/**
 * @brief deallocates an compiled expression [exp]
 * @param exp, the compiled expression
 */
void udogFreeRegex(UDogRegex* exp);

/**
 * @brief checks if the string [text] can be matched by the regex [exp]
 * @param exp the compiled regex
 * @param text the string that needs to be matched
 * @return whether the string [text] matches or not
 */
bool udogMatch(UDogRegex* exp, const char* text);

/**
 * @brief Searches for a match to regex [exp] in the string [text]
 * 			[outBegin] marks the beginning of the match and [outEnd]
 * 			marks the end of the match
 * @param exp, the regex expression
 * @param text, the string in which [exp] is to be tested
 * @param outBegin, marks the beginning of the match
 * @param outEnd, marks the beginning of the match
 * @return true if a match is found, false otherwise
 */
bool udogSearch(UDogRegex* exp,const char* text, const char** outBegin, const char** outEnd);

/**
 * @brief Searches for a match to regex [exp] in a string marked by
 * 			the boundaries [textBegin] and [textEnd]
 * 			[outBegin] marks the beginning of the match and [outEnd]
 * 			marks the end of the match
 * @param exp, the regex expression
 * @param textBegin, the beginning of the string
 * @param textEnd, marks the end of the string
 * @param outBegin, marks the beginning of a match
 * @param outEnd, marks the end of a match
 * @return true if a match is found, false otherwise
 */
bool udogSearchrange(UDogRegex* exp,const char* textBegin,const char* textEnd,const char** outBegin, const char** outEnd);

/**
 * @brief Gets the number of groups that matched within the compiled regex
 * @param exp, the compiled expression
 * @return  the number of matches
 */
int udogGetGroupCount(UDogRegex* exp);

/**
 * @brief searches for matched groups in a compiled expression
 * @param exp, the compiled expression
 * @param n, the index of which subexpression needs to be retrieved
 * 			0 indicates the entire expression
 * 			1-n indicate the different groups
 * @param subexp, the submatch will be stored within this struct
 * @return whether the subexpression was found or not
 */
bool udogGetSubexp(UDogRegex* exp, int n, UDogRegexMatch* subexp);

// This module defines the Regex class and its associated methods. They are
// implemented using the C standard library and the above engine
void udogLoadRegexLibrary(UDogVM* vm);


#endif
