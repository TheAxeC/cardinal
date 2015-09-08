#include "udog_regex.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <setjmp.h>

#define MAX_CHAR		0xFF
#define OP_GREEDY		(MAX_CHAR+1) // * + ? {n}
#define OP_OR			(MAX_CHAR+2)
#define OP_EXPR			(MAX_CHAR+3) //parentesis ()
#define OP_NOCAPEXPR	(MAX_CHAR+4) //parentesis (?:)
#define OP_DOT			(MAX_CHAR+5)
#define OP_CLASS		(MAX_CHAR+6)
#define OP_CCLASS		(MAX_CHAR+7)
#define OP_NCLASS		(MAX_CHAR+8) //negates class the [^
#define OP_RANGE		(MAX_CHAR+9)
#define OP_CHAR			(MAX_CHAR+10)
#define OP_EOL			(MAX_CHAR+11)
#define OP_BOL			(MAX_CHAR+12)
#define OP_WB			(MAX_CHAR+13)

#define UDOG_SYMBOL_ANY_CHAR ('.')
#define UDOG_SYMBOL_GREEDY_ONE_OR_MORE ('+')
#define UDOG_SYMBOL_GREEDY_ZERO_OR_MORE ('*')
#define UDOG_SYMBOL_GREEDY_ZERO_OR_ONE ('?')
#define UDOG_SYMBOL_BRANCH ('|')
#define UDOG_SYMBOL_END_OF_STRING ('$')
#define UDOG_SYMBOL_BEGINNING_OF_STRING ('^')
#define UDOG_SYMBOL_ESCAPE_CHAR ('\\')

/// Type of a node
typedef int UDogNodeType;

/// UDog Regex node
typedef struct UDogRegexNode {
	/// Indicates the type
	UDogNodeType type;
	/// Left leaf
	int left;
	/// right leaf
	int right;
	/// next node
	int next;
} UDogRegexNode;

/// Represents an entire compiled regex expression
struct UDogRegex {
	/// End of line
	const char* eol;
	/// Begin of line
	const char* bol;
	/// pointer into a regex
	const char* p;
	/// First processed node
	int first;
	/// Current opcode
	int op;
	/// Used nodes
	UDogRegexNode* nodes;
	/// Allocated bytes
	int allocated;
	/// Size of the regex
	int size;
	/// Nbr of subexpressions
	int nbrSubExpr;
	/// All matches
	UDogRegexMatch* matches;
	/// Current subexpression
	int currSubExp;
	/// Jump buffer
	void* jmpBuf;
	/// Error buffer
	const char** error;
};

static int udogList(UDogRegex *exp);

static int udogNewNode(UDogRegex *exp, UDogNodeType type) {
	UDogRegexNode n;
	int newid;
	n.type = type;
	n.next = n.right = n.left = -1;
	if(type == OP_EXPR)
		n.right = exp->currSubExp++;
	if(exp->allocated < (exp->size + 1)) {
		exp->allocated *= 2;
		exp->nodes = (UDogRegexNode *)realloc(exp->nodes, exp->allocated * sizeof(UDogRegexNode));
	}
	exp->nodes[exp->size++] = n;
	newid = exp->size - 1;
	return (int)newid;
}

static void udogError(UDogRegex *exp,const char *error) {
	if(exp->error) *exp->error = error;
	longjmp(*((jmp_buf*)exp->jmpBuf),-1);
}

static void udogExpect(UDogRegex *exp, int n) {
	if((*exp->p) != n) 
		udogError(exp,  ("expected paren"));
	exp->p++;
}

static char udogEscapeChar(UDogRegex *exp) {
	if(*exp->p == UDOG_SYMBOL_ESCAPE_CHAR){
		exp->p++;
		switch(*exp->p) {
		case 'v': exp->p++; return '\v';
		case 'n': exp->p++; return '\n';
		case 't': exp->p++; return '\t';
		case 'r': exp->p++; return '\r';
		case 'f': exp->p++; return '\f';
		default: return (*exp->p++);
		}
	} else if(!isprint(*exp->p)) udogError(exp, ("letter expected"));
	return (*exp->p++);
}

static int udogCharClass(UDogRegex *exp,int classid) {
	int n = udogNewNode(exp,OP_CCLASS);
	exp->nodes[n].left = classid;
	return n;
}

static int udogCharNode(UDogRegex *exp,bool isclass) {
	char t;
	if(*exp->p == UDOG_SYMBOL_ESCAPE_CHAR) {
		exp->p++;
		switch(*exp->p) {
			case 'n': exp->p++; return udogNewNode(exp,'\n');
			case 't': exp->p++; return udogNewNode(exp,'\t');
			case 'r': exp->p++; return udogNewNode(exp,'\r');
			case 'f': exp->p++; return udogNewNode(exp,'\f');
			case 'v': exp->p++; return udogNewNode(exp,'\v');
			case 'a': case 'A': case 'w': case 'W': case 's': case 'S': 
			case 'd': case 'D': case 'x': case 'X': case 'c': case 'C': 
			case 'p': case 'P': case 'l': case 'u': 
				{
				t = *exp->p; exp->p++; 
				return udogCharClass(exp,t);
				}
			case 'b': 
			case 'B':
				if(!isclass) {
					int node = udogNewNode(exp,OP_WB);
					exp->nodes[node].left = *exp->p;
					exp->p++; 
					return node;
				} //else default
			default: 
				t = *exp->p; exp->p++; 
				return udogNewNode(exp,t);
		}
	}
	else if(!isprint(*exp->p)) {
		
		udogError(exp, ("letter expected"));
	}
	t = *exp->p; exp->p++; 
	return udogNewNode(exp,t);
}

static int udogClass(UDogRegex *exp) {
	int ret = -1;
	int first = -1,chain;
	if(*exp->p == UDOG_SYMBOL_BEGINNING_OF_STRING){
		ret = udogNewNode(exp,OP_NCLASS);
		exp->p++;
	}else ret = udogNewNode(exp,OP_CLASS);
	
	if(*exp->p == ']') udogError(exp, ("empty class"));
	chain = ret;
	while(*exp->p != ']' && exp->p != exp->eol) {
		if(*exp->p == '-' && first != -1){ 
			int r,t;
			if(*exp->p++ == ']') udogError(exp, ("unfinished range"));
			r = udogNewNode(exp,OP_RANGE);
			if(first>*exp->p) udogError(exp, ("invalid range"));
			if(exp->nodes[first].type == OP_CCLASS) udogError(exp, ("cannot use character classes in ranges"));
			exp->nodes[r].left = exp->nodes[first].type;
			t = udogEscapeChar(exp);
			exp->nodes[r].right = t;
            exp->nodes[chain].next = r;
			chain = r;
			first = -1;
		}
		else{
			if(first!=-1){
				int c = first;
				exp->nodes[chain].next = c;
				chain = c;
				first = udogCharNode(exp,true);
			}
			else{
				first = udogCharNode(exp,true);
			}
		}
	}
	if(first!=-1){
		int c = first;
		exp->nodes[chain].next = c;
		chain = c;
		first = -1;
	}
	// hack? 
	exp->nodes[ret].left = exp->nodes[ret].next;
	exp->nodes[ret].next = -1;
	return ret;
}

static int udogParseNumber(UDogRegex *exp) {
	int ret = *exp->p-'0';
	int positions = 10;
	exp->p++;
	while(isdigit(*exp->p)) {
		ret = ret*10+(*exp->p++-'0');
		if(positions==1000000000) udogError(exp, ("overflow in numeric constant"));
		positions *= 10;
	};
	return ret;
}

static int udogElement(UDogRegex *exp) {
	int ret = -1;
	switch(*exp->p)
	{
	case '(': {
		int expr,newn;
		exp->p++;


		if(*exp->p =='?') {
			exp->p++;
			udogExpect(exp,':');
			expr = udogNewNode(exp,OP_NOCAPEXPR);
		}
		else
			expr = udogNewNode(exp,OP_EXPR);
		newn = udogList(exp);
		exp->nodes[expr].left = newn;
		ret = expr;
		udogExpect(exp,')');
			  }
			  break;
	case '[':
		exp->p++;
		ret = udogClass(exp);
		udogExpect(exp,']');
		break;
	case UDOG_SYMBOL_END_OF_STRING: exp->p++; ret = udogNewNode(exp,OP_EOL);break;
	case UDOG_SYMBOL_ANY_CHAR: exp->p++; ret = udogNewNode(exp,OP_DOT);break;
	default:
		ret = udogCharNode(exp,false);
		break;
	}

	{
		//int op;
		bool isgreedy = false;
		unsigned short p0 = 0, p1 = 0;
		switch(*exp->p) {
			case UDOG_SYMBOL_GREEDY_ZERO_OR_MORE: p0 = 0; p1 = 0xFFFF; exp->p++; isgreedy = true; break;
			case UDOG_SYMBOL_GREEDY_ONE_OR_MORE: p0 = 1; p1 = 0xFFFF; exp->p++; isgreedy = true; break;
			case UDOG_SYMBOL_GREEDY_ZERO_OR_ONE: p0 = 0; p1 = 1; exp->p++; isgreedy = true; break;
			case '{':
				exp->p++;
				if(!isdigit(*exp->p)) udogError(exp, ("number expected"));
				p0 = (unsigned short)udogParseNumber(exp);
				////////////////////////////////
				switch(*exp->p) {
					case '}':
						p1 = p0; exp->p++;
						break;
					case ',':
						exp->p++;
						p1 = 0xFFFF;
						if(isdigit(*exp->p)){
							p1 = (unsigned short)udogParseNumber(exp);
						}
						udogExpect(exp,'}');
						break;
					default:
						udogError(exp, (", or } expected"));
				}
				//////////////////////////////////
				isgreedy = true; 
				break;
			default:
				break;
		}
		if(isgreedy) {
			int nnode = udogNewNode(exp,OP_GREEDY);
			//op = OP_GREEDY;
			exp->nodes[nnode].left = ret;
			exp->nodes[nnode].right = ((p0)<<16)|p1;
			ret = nnode;
		}
	}
	if((*exp->p != UDOG_SYMBOL_BRANCH) && (*exp->p != ')') && (*exp->p != UDOG_SYMBOL_GREEDY_ZERO_OR_MORE) && (*exp->p != UDOG_SYMBOL_GREEDY_ONE_OR_MORE) && (*exp->p != '\0')) {
		int nnode = udogElement(exp);
		exp->nodes[ret].next = nnode;
	}

	return ret;
}

static int udogList(UDogRegex *exp) {
	int ret=-1,e;
	if(*exp->p == UDOG_SYMBOL_BEGINNING_OF_STRING) {
		exp->p++;
		ret = udogNewNode(exp,OP_BOL);
	}
	e = udogElement(exp);
	if(ret != -1) {
		exp->nodes[ret].next = e;
	}
	else ret = e;

	if(*exp->p == UDOG_SYMBOL_BRANCH) {
		int temp,tright;
		exp->p++;
		temp = udogNewNode(exp,OP_OR);
		exp->nodes[temp].left = ret;
		tright = udogList(exp);
		exp->nodes[temp].right = tright;
		ret = temp;
	}
	return ret;
}

static bool udogMatchCClass(int cclass, char c) {
	switch(cclass) {
	case 'a': return isalpha(c)?true:false;
	case 'A': return !isalpha(c)?true:false;
	case 'w': return (isalnum(c) || c == '_')?true:false;
	case 'W': return (!isalnum(c) && c != '_')?true:false;
	case 's': return isspace(c)?true:false;
	case 'S': return !isspace(c)?true:false;
	case 'd': return isdigit(c)?true:false;
	case 'D': return !isdigit(c)?true:false;
	case 'x': return isxdigit(c)?true:false;
	case 'X': return !isxdigit(c)?true:false;
	case 'c': return iscntrl(c)?true:false;
	case 'C': return !iscntrl(c)?true:false;
	case 'p': return ispunct(c)?true:false;
	case 'P': return !ispunct(c)?true:false;
	case 'l': return islower(c)?true:false;
	case 'u': return isupper(c)?true:false;
	default:
		return false; //cannot happen//
	}
	return false; //cannot happen//
}

static bool udogMatchClass(UDogRegex* exp,UDogRegexNode *node,char c) {
	do {
		switch(node->type) {
			case OP_RANGE:
				if(c >= node->left && c <= node->right) return true;
				break;
			case OP_CCLASS:
				if(udogMatchCClass(node->left,c)) return true;
				break;
			default:
				if(c == node->type)return true;
		}
	} while((node->next != -1) && (node = &exp->nodes[node->next]));
	return false;
}

static const char *udogMatchNode(UDogRegex* exp,UDogRegexNode *node,const char *str,UDogRegexNode *next) {
	
	UDogNodeType type = node->type;
	switch(type) {
	case OP_GREEDY: {
		//UDogRegexNode *greedystop = (node->next != -1) ? &exp->nodes[node->next] : NULL;
		UDogRegexNode *greedystop = NULL;
		int p0 = (node->right >> 16)&0x0000FFFF, p1 = node->right&0x0000FFFF, nmaches = 0;
		const char *s=str, *good = str;

		if(node->next != -1) {
			greedystop = &exp->nodes[node->next];
		}
		else {
			greedystop = next;
		}

		while((nmaches == 0xFFFF || nmaches < p1)) {

			const char *stop;
			if(!(s = udogMatchNode(exp,&exp->nodes[node->left],s,greedystop)))
				break;
			nmaches++;
			good=s;
			if(greedystop) {
				//checks that 0 matches satisfy the expression(if so skips)
				//if not would always stop(for instance if is a '?')
				if(greedystop->type != OP_GREEDY ||
				(greedystop->type == OP_GREEDY && ((greedystop->right >> 16)&0x0000FFFF) != 0))
				{
					UDogRegexNode *gnext = NULL;
					if(greedystop->next != -1) {
						gnext = &exp->nodes[greedystop->next];
					}else if(next && next->next != -1){
						gnext = &exp->nodes[next->next];
					}
					stop = udogMatchNode(exp,greedystop,s,gnext);
					if(stop) {
						//if satisfied stop it
						if(p0 == p1 && p0 == nmaches) break;
						else if(nmaches >= p0 && p1 == 0xFFFF) break;
						else if(nmaches >= p0 && nmaches <= p1) break;
					}
				}
			}
			
			if(s >= exp->eol)
				break;
		}
		if(p0 == p1 && p0 == nmaches) return good;
		else if(nmaches >= p0 && p1 == 0xFFFF) return good;
		else if(nmaches >= p0 && nmaches <= p1) return good;
		return NULL;
	}
	case OP_OR: {
			const char *asd = str;
			UDogRegexNode *temp=&exp->nodes[node->left];
			while( (asd = udogMatchNode(exp,temp,asd,NULL)) ) {
				if(temp->next != -1)
					temp = &exp->nodes[temp->next];
				else
					return asd;
			}
			asd = str;
			temp = &exp->nodes[node->right];
			while( (asd = udogMatchNode(exp,temp,asd,NULL)) ) {
				if(temp->next != -1)
					temp = &exp->nodes[temp->next];
				else
					return asd;
			}
			return NULL;
			break;
	}
	case OP_EXPR:
	case OP_NOCAPEXPR:{
			UDogRegexNode *n = &exp->nodes[node->left];
			const char *cur = str;
			int capture = -1;
			if(node->type != OP_NOCAPEXPR && node->right == exp->currSubExp) {
				capture = exp->currSubExp;
				exp->matches[capture].begin = cur;
				exp->currSubExp++;
			}
			
			do {
				UDogRegexNode *subnext = NULL;
				if(n->next != -1) {
					subnext = &exp->nodes[n->next];
				}else {
					subnext = next;
				}
				if(!(cur = udogMatchNode(exp,n,cur,subnext))) {
					if(capture != -1){
						exp->matches[capture].begin = 0;
						exp->matches[capture].len = 0;
					}
					return NULL;
				}
			} while((n->next != -1) && (n = &exp->nodes[n->next]));

			if(capture != -1) 
				exp->matches[capture].len = cur - exp->matches[capture].begin;
			return cur;
	}				 
	case OP_WB:
		if((str == exp->bol && !isspace(*str))
		 || (str == exp->eol && !isspace(*(str-1)))
		 || (!isspace(*str) && isspace(*(str+1)))
		 || (isspace(*str) && !isspace(*(str+1))) ) {
			return (node->left == 'b')?str:NULL;
		}
		return (node->left == 'b')?NULL:str;
	case OP_BOL:
		if(str == exp->bol) return str;
		return NULL;
	case OP_EOL:
		if(str == exp->eol) return str;
		return NULL;
	case OP_DOT:{
		UNUSED(*str++);
				}
		return str;
	case OP_NCLASS:
	case OP_CLASS:
		if(udogMatchClass(exp,&exp->nodes[node->left],*str)?(type == OP_CLASS?true:false):(type == OP_NCLASS?true:false)) {
			UNUSED(*str++);
			return str;
		}
		return NULL;
	case OP_CCLASS:
		if(udogMatchCClass(node->left,*str)) {
			UNUSED(*str++);
			return str;
		}
		return NULL;
	default: // char //
		if(*str != node->type) return NULL;
		UNUSED(*str++);
		return str;
	}
	return NULL;
}

/**
 * @brief Compiles a pattern [pattern] into a regex [UDogRegex]
 * @param pattern, the pattern that needs to be compiled
 * @param error, here the error message gets printed into in case of an error
 * @return the compiled pattern or NULL (in case of failure)
 */
UDogRegex* udogCompileRegex(const char* pattern, const char** error) {
	UDogRegex* exp = (UDogRegex *)malloc(sizeof(UDogRegex));
	exp->eol = exp->bol = NULL;
	exp->p = pattern;
	exp->allocated = (int)strlen(pattern) * sizeof(char);
	exp->nodes = (UDogRegexNode *)malloc(exp->allocated * sizeof(UDogRegexNode));
	exp->size = 0;
	exp->matches = 0;
	exp->nbrSubExpr = 0;
	exp->currSubExp = 0;
	exp->first = udogNewNode(exp,OP_EXPR);
	exp->error = error;
	exp->jmpBuf = malloc(sizeof(jmp_buf));
	if(setjmp(*((jmp_buf*)exp->jmpBuf)) == 0) {
		int res = udogList(exp);
		exp->nodes[exp->first].left = res;
		if(*exp->p!='\0')
			udogError(exp, ("unexpected character"));
		exp->matches = (UDogRegexMatch *) malloc(exp->currSubExp * sizeof(UDogRegexMatch));
		memset(exp->matches,0,exp->currSubExp * sizeof(UDogRegexMatch));
	}
	else{
		udogFreeRegex(exp);
		return NULL;
	}
	return exp;
}

/**
 * @brief deallocates an compiled expression [exp]
 * @param exp, the compiled expression
 */
void udogFreeRegex(UDogRegex* exp) {
	if(exp)	{
		if(exp->nodes) free(exp->nodes);
		if(exp->jmpBuf) free(exp->jmpBuf);
		if(exp->matches) free(exp->matches);
		free(exp);
	}
}

/**
 * @brief checks if the string [text] can be matched by the regex [exp]
 * @param exp the compiled regex
 * @param text the string that needs to be matched
 * @return whether the string [text] matches or not
 */
bool udogMatch(UDogRegex* exp, const char* text) {
	const char* res = NULL;
	exp->bol = text;
	exp->eol = text + strlen(text);
	exp->currSubExp = 0;
	res = udogMatchNode(exp,exp->nodes,text,NULL);
	if(res == NULL || res != exp->eol)
		return false;
	return true;
}

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
bool udogSearch(UDogRegex* exp,const char* text, const char** outBegin, const char** outEnd) {
	return udogSearchrange(exp, text, text + strlen(text), outBegin, outEnd);
}

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
bool udogSearchrange(UDogRegex* exp,const char* textBegin,const char* textEnd,const char** outBegin, const char** outEnd) {
	const char* cur = NULL;
	int node = exp->first;
	if(textBegin >= textEnd) return false;
	exp->bol = textBegin;
	exp->eol = textEnd;
	do {
		cur = textBegin;
		while(node != -1) {
			exp->currSubExp = 0;
			cur = udogMatchNode(exp,&exp->nodes[node],cur,NULL);
			if(!cur)
				break;
			node = exp->nodes[node].next;
		}
		UNUSED(*textBegin++);
	} while(cur == NULL && textBegin != textEnd);

	if(cur == NULL)
		return false;

	--textBegin;

	if(outBegin) *outBegin = textBegin;
	if(outEnd) *outEnd = cur;
	return true;
}

/**
 * @brief Gets the number of groups that matched within the compiled regex
 * @param exp, the compiled expression
 * @return  the number of matches
 */
int udogGetGroupCount(UDogRegex* exp) {
	return exp->nbrSubExpr;
}

/**
 * @brief searches for matched groups in a compiled expression
 * @param exp, the compiled expression
 * @param n, the index of which subexpression needs to be retrieved
 * 			0 indicates the entire expression
 * 			1-n indicate the different groups
 * @param subexp, the submatch will be stored within this struct
 * @return whether the subexpression was found or not
 */
bool udogGetSubexp(UDogRegex* exp, int n, UDogRegexMatch* subexp) {
	if(n < 0 || n >= exp->nbrSubExpr) return false;
	*subexp = exp->matches[n];
	return true;
}

///////////////////////////////////////////////////////////////////////////////////
//// REGEX LIBRARY
///////////////////////////////////////////////////////////////////////////////////

/// Defines the struct used in UDog
typedef struct ScriptRegex {
	/// The regex
	UDogRegex* regex;
	/// Set if the regex is being used or not
	bool inUse;
} ScriptRegex;

// Creates a new regex expression
static void newRegex(UDogVM* vm) {
	UDogValue* val = udogGetArgument(vm, 0);
	ScriptRegex* regex = (ScriptRegex*) udogGetInstance(vm, val);
	
	regex->inUse = false;
	regex->regex = NULL;
	
	udogReturnValue(vm, val);
}

// Destroys regex expression
static void destructRegex(void* obj) {
	ScriptRegex* regex = (ScriptRegex*) obj;
	
	if (regex->inUse) udogFreeRegex(regex->regex);
}

// Gets the number of groups that matched within the compiled regex
static void getGroupCountRegex(UDogVM* vm) {
	UDogValue* val = udogGetArgument(vm, 0);
	ScriptRegex* regex = (ScriptRegex*) udogGetInstance(vm, val);
	
	int nbGroups = -1;
	if (regex->inUse) nbGroups = udogGetGroupCount(regex->regex);
	
	udogReturnDouble(vm, nbGroups);
	udogReleaseObject(vm, val);
}

// Compiles a pattern [pattern] into a regex [UDogRegex]
static void compileRegex(UDogVM* vm) {
	UDogValue* val = udogGetArgument(vm, 0);
	ScriptRegex* regex = (ScriptRegex*) udogGetInstance(vm, val);
	const char* pattern = udogGetArgumentString(vm, 1);
	
	if (regex->inUse)
		udogFreeRegex(regex->regex);
	
	const char* error = NULL;
	UDogRegex* x = udogCompileRegex(pattern, &error);
	
	if (x) {
		regex->regex = x;
		regex->inUse = true;
		udogReturnValue(vm, val);
	}
	else {
		udogReturnString(vm, error, strlen(error));
		udogReleaseObject(vm, val);
	}
}

// checks if the string [text] can be matched by the regex [exp]
static void matchRegex(UDogVM* vm) {
	UDogValue* val = udogGetArgument(vm, 0);
	ScriptRegex* regex = (ScriptRegex*) udogGetInstance(vm, val);
	const char* pattern = udogGetArgumentString(vm, 1);
	bool match = false;
	if (regex->inUse)
		match = udogMatch(regex->regex, pattern);
	udogReturnBool(vm, match);
	udogReleaseObject(vm, val);
}

// Searches for a match to regex [exp] in the string [text]
static void searchRegex(UDogVM* vm) {
	UDogValue* val = udogGetArgument(vm, 0);
	ScriptRegex* regex = (ScriptRegex*) udogGetInstance(vm, val);
	const char* begin = NULL;
	const char* end = NULL;
	const char* text = udogGetArgumentString(vm, 1);
	
	bool match = false;
	if (regex->inUse)
		match = udogSearch(regex->regex, text, &begin, &end);
	if (match) {
		udogReturnString(vm, begin, end-begin);
	}
	else udogReturnNull(vm);
	
	udogReleaseObject(vm, val);
}

// searches for matched groups in a compiled expression
static void getGroupRegex(UDogVM* vm) {
	UDogValue* val = udogGetArgument(vm, 0);
	ScriptRegex* regex = (ScriptRegex*) udogGetInstance(vm, val);
	double nb = udogGetArgumentDouble(vm, 1);
	
	if (regex->inUse) {
		UDogRegexMatch subExp;
		subExp.len = -1;
		subExp.begin = "empty";
		udogGetSubexp(regex->regex, nb, &subExp);
		
		// Put subExp into a string and return
		udogReturnString(vm, subExp.begin, subExp.len);
	} else {
		udogReturnNull(vm);
	}
	
	udogReleaseObject(vm, val);
}

// This module defines the Regex class and its associated methods. They are
// implemented using the C standard library and the above engine
void udogLoadRegexLibrary(UDogVM* vm) {
	// Defines the regex class
	udogDefineClass(vm, NULL, "Regex", sizeof(ScriptRegex), NULL);
	
	// Defines the constructor and destructor
	udogDefineMethod(vm, NULL, "Regex", "new", newRegex);
	udogDefineDestructor(vm, NULL, "Regex", destructRegex);
	
	// Define the methods on the Regex class
	udogDefineMethod(vm, NULL, "Regex", "match(_)", matchRegex);
	udogDefineMethod(vm, NULL, "Regex", "search(_)", searchRegex);
	udogDefineMethod(vm, NULL, "Regex", "getGroup(_)", getGroupRegex);
	udogDefineMethod(vm, NULL, "Regex", "compile(_)", compileRegex);
	udogDefineMethod(vm, NULL, "Regex", "getGroupCount()", getGroupCountRegex);
}

