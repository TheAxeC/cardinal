#include "cardinal_regex.h"

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

#define CARDINAL_SYMBOL_ANY_CHAR ('.')
#define CARDINAL_SYMBOL_GREEDY_ONE_OR_MORE ('+')
#define CARDINAL_SYMBOL_GREEDY_ZERO_OR_MORE ('*')
#define CARDINAL_SYMBOL_GREEDY_ZERO_OR_ONE ('?')
#define CARDINAL_SYMBOL_BRANCH ('|')
#define CARDINAL_SYMBOL_END_OF_STRING ('$')
#define CARDINAL_SYMBOL_BEGINNING_OF_STRING ('^')
#define CARDINAL_SYMBOL_ESCAPE_CHAR ('\\')

/// Type of a node
typedef int CardinalNodeType;

/// Cardinal Regex node
typedef struct CardinalRegexNode {
	/// Indicates the type
	CardinalNodeType type;
	/// Left leaf
	int left;
	/// right leaf
	int right;
	/// next node
	int next;
} CardinalRegexNode;

/// Represents an entire compiled regex expression
struct CardinalRegex {
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
	CardinalRegexNode* nodes;
	/// Allocated bytes
	int allocated;
	/// Size of the regex
	int size;
	/// Nbr of subexpressions
	int nbrSubExpr;
	/// All matches
	CardinalRegexMatch* matches;
	/// Current subexpression
	int currSubExp;
	/// Jump buffer
	void* jmpBuf;
	/// Error buffer
	const char** error;
};

static int cardinalList(CardinalRegex *exp);

static int cardinalNewNode(CardinalRegex *exp, CardinalNodeType type) {
	CardinalRegexNode n;
	int newid;
	n.type = type;
	n.next = n.right = n.left = -1;
	if(type == OP_EXPR)
		n.right = exp->currSubExp++;
	if(exp->allocated < (exp->size + 1)) {
		exp->allocated *= 2;
		exp->nodes = (CardinalRegexNode *)realloc(exp->nodes, exp->allocated * sizeof(CardinalRegexNode));
	}
	exp->nodes[exp->size++] = n;
	newid = exp->size - 1;
	return (int)newid;
}

static void cardinalError(CardinalRegex *exp,const char *error) {
	if(exp->error) *exp->error = error;
	longjmp(*((jmp_buf*)exp->jmpBuf),-1);
}

static void cardinalExpect(CardinalRegex *exp, int n) {
	if((*exp->p) != n) 
		cardinalError(exp,  ("expected paren"));
	exp->p++;
}

static char cardinalEscapeChar(CardinalRegex *exp) {
	if(*exp->p == CARDINAL_SYMBOL_ESCAPE_CHAR){
		exp->p++;
		switch(*exp->p) {
		case 'v': exp->p++; return '\v';
		case 'n': exp->p++; return '\n';
		case 't': exp->p++; return '\t';
		case 'r': exp->p++; return '\r';
		case 'f': exp->p++; return '\f';
		default: return (*exp->p++);
		}
	} else if(!isprint(*exp->p)) cardinalError(exp, ("letter expected"));
	return (*exp->p++);
}

static int cardinalCharClass(CardinalRegex *exp,int classid) {
	int n = cardinalNewNode(exp,OP_CCLASS);
	exp->nodes[n].left = classid;
	return n;
}

static int cardinalCharNode(CardinalRegex *exp,bool isclass) {
	char t;
	if(*exp->p == CARDINAL_SYMBOL_ESCAPE_CHAR) {
		exp->p++;
		switch(*exp->p) {
			case 'n': exp->p++; return cardinalNewNode(exp,'\n');
			case 't': exp->p++; return cardinalNewNode(exp,'\t');
			case 'r': exp->p++; return cardinalNewNode(exp,'\r');
			case 'f': exp->p++; return cardinalNewNode(exp,'\f');
			case 'v': exp->p++; return cardinalNewNode(exp,'\v');
			case 'a': case 'A': case 'w': case 'W': case 's': case 'S': 
			case 'd': case 'D': case 'x': case 'X': case 'c': case 'C': 
			case 'p': case 'P': case 'l': case 'u': 
				{
				t = *exp->p; exp->p++; 
				return cardinalCharClass(exp,t);
				}
			case 'b': 
			case 'B':
				if(!isclass) {
					int node = cardinalNewNode(exp,OP_WB);
					exp->nodes[node].left = *exp->p;
					exp->p++; 
					return node;
				} //else default
			default: 
				t = *exp->p; exp->p++; 
				return cardinalNewNode(exp,t);
		}
	}
	else if(!isprint(*exp->p)) {
		
		cardinalError(exp, ("letter expected"));
	}
	t = *exp->p; exp->p++; 
	return cardinalNewNode(exp,t);
}

static int cardinalClass(CardinalRegex *exp) {
	int ret = -1;
	int first = -1,chain;
	if(*exp->p == CARDINAL_SYMBOL_BEGINNING_OF_STRING){
		ret = cardinalNewNode(exp,OP_NCLASS);
		exp->p++;
	}else ret = cardinalNewNode(exp,OP_CLASS);
	
	if(*exp->p == ']') cardinalError(exp, ("empty class"));
	chain = ret;
	while(*exp->p != ']' && exp->p != exp->eol) {
		if(*exp->p == '-' && first != -1){ 
			int r,t;
			if(*exp->p++ == ']') cardinalError(exp, ("unfinished range"));
			r = cardinalNewNode(exp,OP_RANGE);
			if(first>*exp->p) cardinalError(exp, ("invalid range"));
			if(exp->nodes[first].type == OP_CCLASS) cardinalError(exp, ("cannot use character classes in ranges"));
			exp->nodes[r].left = exp->nodes[first].type;
			t = cardinalEscapeChar(exp);
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
				first = cardinalCharNode(exp,true);
			}
			else{
				first = cardinalCharNode(exp,true);
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

static int cardinalParseNumber(CardinalRegex *exp) {
	int ret = *exp->p-'0';
	int positions = 10;
	exp->p++;
	while(isdigit(*exp->p)) {
		ret = ret*10+(*exp->p++-'0');
		if(positions==1000000000) cardinalError(exp, ("overflow in numeric constant"));
		positions *= 10;
	};
	return ret;
}

static int cardinalElement(CardinalRegex *exp) {
	int ret = -1;
	switch(*exp->p)
	{
	case '(': {
		int expr,newn;
		exp->p++;


		if(*exp->p =='?') {
			exp->p++;
			cardinalExpect(exp,':');
			expr = cardinalNewNode(exp,OP_NOCAPEXPR);
		}
		else
			expr = cardinalNewNode(exp,OP_EXPR);
		newn = cardinalList(exp);
		exp->nodes[expr].left = newn;
		ret = expr;
		cardinalExpect(exp,')');
			  }
			  break;
	case '[':
		exp->p++;
		ret = cardinalClass(exp);
		cardinalExpect(exp,']');
		break;
	case CARDINAL_SYMBOL_END_OF_STRING: exp->p++; ret = cardinalNewNode(exp,OP_EOL);break;
	case CARDINAL_SYMBOL_ANY_CHAR: exp->p++; ret = cardinalNewNode(exp,OP_DOT);break;
	default:
		ret = cardinalCharNode(exp,false);
		break;
	}

	{
		//int op;
		bool isgreedy = false;
		unsigned short p0 = 0, p1 = 0;
		switch(*exp->p) {
			case CARDINAL_SYMBOL_GREEDY_ZERO_OR_MORE: p0 = 0; p1 = 0xFFFF; exp->p++; isgreedy = true; break;
			case CARDINAL_SYMBOL_GREEDY_ONE_OR_MORE: p0 = 1; p1 = 0xFFFF; exp->p++; isgreedy = true; break;
			case CARDINAL_SYMBOL_GREEDY_ZERO_OR_ONE: p0 = 0; p1 = 1; exp->p++; isgreedy = true; break;
			case '{':
				exp->p++;
				if(!isdigit(*exp->p)) cardinalError(exp, ("number expected"));
				p0 = (unsigned short)cardinalParseNumber(exp);
				////////////////////////////////
				switch(*exp->p) {
					case '}':
						p1 = p0; exp->p++;
						break;
					case ',':
						exp->p++;
						p1 = 0xFFFF;
						if(isdigit(*exp->p)){
							p1 = (unsigned short)cardinalParseNumber(exp);
						}
						cardinalExpect(exp,'}');
						break;
					default:
						cardinalError(exp, (", or } expected"));
				}
				//////////////////////////////////
				isgreedy = true; 
				break;
			default:
				break;
		}
		if(isgreedy) {
			int nnode = cardinalNewNode(exp,OP_GREEDY);
			//op = OP_GREEDY;
			exp->nodes[nnode].left = ret;
			exp->nodes[nnode].right = ((p0)<<16)|p1;
			ret = nnode;
		}
	}
	if((*exp->p != CARDINAL_SYMBOL_BRANCH) && (*exp->p != ')') && (*exp->p != CARDINAL_SYMBOL_GREEDY_ZERO_OR_MORE) && (*exp->p != CARDINAL_SYMBOL_GREEDY_ONE_OR_MORE) && (*exp->p != '\0')) {
		int nnode = cardinalElement(exp);
		exp->nodes[ret].next = nnode;
	}

	return ret;
}

static int cardinalList(CardinalRegex *exp) {
	int ret=-1,e;
	if(*exp->p == CARDINAL_SYMBOL_BEGINNING_OF_STRING) {
		exp->p++;
		ret = cardinalNewNode(exp,OP_BOL);
	}
	e = cardinalElement(exp);
	if(ret != -1) {
		exp->nodes[ret].next = e;
	}
	else ret = e;

	if(*exp->p == CARDINAL_SYMBOL_BRANCH) {
		int temp,tright;
		exp->p++;
		temp = cardinalNewNode(exp,OP_OR);
		exp->nodes[temp].left = ret;
		tright = cardinalList(exp);
		exp->nodes[temp].right = tright;
		ret = temp;
	}
	return ret;
}

static bool cardinalMatchCClass(int cclass, char c) {
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

static bool cardinalMatchClass(CardinalRegex* exp,CardinalRegexNode *node,char c) {
	do {
		switch(node->type) {
			case OP_RANGE:
				if(c >= node->left && c <= node->right) return true;
				break;
			case OP_CCLASS:
				if(cardinalMatchCClass(node->left,c)) return true;
				break;
			default:
				if(c == node->type)return true;
		}
	} while((node->next != -1) && (node = &exp->nodes[node->next]));
	return false;
}

static const char *cardinalMatchNode(CardinalRegex* exp,CardinalRegexNode *node,const char *str,CardinalRegexNode *next) {
	
	CardinalNodeType type = node->type;
	switch(type) {
	case OP_GREEDY: {
		//CardinalRegexNode *greedystop = (node->next != -1) ? &exp->nodes[node->next] : NULL;
		CardinalRegexNode *greedystop = NULL;
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
			if(!(s = cardinalMatchNode(exp,&exp->nodes[node->left],s,greedystop)))
				break;
			nmaches++;
			good=s;
			if(greedystop) {
				//checks that 0 matches satisfy the expression(if so skips)
				//if not would always stop(for instance if is a '?')
				if(greedystop->type != OP_GREEDY ||
				(greedystop->type == OP_GREEDY && ((greedystop->right >> 16)&0x0000FFFF) != 0))
				{
					CardinalRegexNode *gnext = NULL;
					if(greedystop->next != -1) {
						gnext = &exp->nodes[greedystop->next];
					}else if(next && next->next != -1){
						gnext = &exp->nodes[next->next];
					}
					stop = cardinalMatchNode(exp,greedystop,s,gnext);
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
			CardinalRegexNode *temp=&exp->nodes[node->left];
			while( (asd = cardinalMatchNode(exp,temp,asd,NULL)) ) {
				if(temp->next != -1)
					temp = &exp->nodes[temp->next];
				else
					return asd;
			}
			asd = str;
			temp = &exp->nodes[node->right];
			while( (asd = cardinalMatchNode(exp,temp,asd,NULL)) ) {
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
			CardinalRegexNode *n = &exp->nodes[node->left];
			const char *cur = str;
			int capture = -1;
			if(node->type != OP_NOCAPEXPR && node->right == exp->currSubExp) {
				capture = exp->currSubExp;
				exp->matches[capture].begin = cur;
				exp->currSubExp++;
			}
			
			do {
				CardinalRegexNode *subnext = NULL;
				if(n->next != -1) {
					subnext = &exp->nodes[n->next];
				}else {
					subnext = next;
				}
				if(!(cur = cardinalMatchNode(exp,n,cur,subnext))) {
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
		if(cardinalMatchClass(exp,&exp->nodes[node->left],*str)?(type == OP_CLASS?true:false):(type == OP_NCLASS?true:false)) {
			UNUSED(*str++);
			return str;
		}
		return NULL;
	case OP_CCLASS:
		if(cardinalMatchCClass(node->left,*str)) {
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
 * @brief Compiles a pattern [pattern] into a regex [CardinalRegex]
 * @param pattern, the pattern that needs to be compiled
 * @param error, here the error message gets printed into in case of an error
 * @return the compiled pattern or NULL (in case of failure)
 */
CardinalRegex* cardinalCompileRegex(const char* pattern, const char** error) {
	CardinalRegex* exp = (CardinalRegex *)malloc(sizeof(CardinalRegex));
	exp->eol = exp->bol = NULL;
	exp->p = pattern;
	exp->allocated = (int)strlen(pattern) * sizeof(char);
	exp->nodes = (CardinalRegexNode *)malloc(exp->allocated * sizeof(CardinalRegexNode));
	exp->size = 0;
	exp->matches = 0;
	exp->nbrSubExpr = 0;
	exp->currSubExp = 0;
	exp->first = cardinalNewNode(exp,OP_EXPR);
	exp->error = error;
	exp->jmpBuf = malloc(sizeof(jmp_buf));
	if(setjmp(*((jmp_buf*)exp->jmpBuf)) == 0) {
		int res = cardinalList(exp);
		exp->nodes[exp->first].left = res;
		if(*exp->p!='\0')
			cardinalError(exp, ("unexpected character"));
		exp->matches = (CardinalRegexMatch *) malloc(exp->currSubExp * sizeof(CardinalRegexMatch));
		memset(exp->matches,0,exp->currSubExp * sizeof(CardinalRegexMatch));
	}
	else{
		cardinalFreeRegex(exp);
		return NULL;
	}
	return exp;
}

/**
 * @brief deallocates an compiled expression [exp]
 * @param exp, the compiled expression
 */
void cardinalFreeRegex(CardinalRegex* exp) {
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
bool cardinalMatch(CardinalRegex* exp, const char* text) {
	const char* res = NULL;
	exp->bol = text;
	exp->eol = text + strlen(text);
	exp->currSubExp = 0;
	res = cardinalMatchNode(exp,exp->nodes,text,NULL);
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
bool cardinalSearch(CardinalRegex* exp,const char* text, const char** outBegin, const char** outEnd) {
	return cardinalSearchrange(exp, text, text + strlen(text), outBegin, outEnd);
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
bool cardinalSearchrange(CardinalRegex* exp,const char* textBegin,const char* textEnd,const char** outBegin, const char** outEnd) {
	const char* cur = NULL;
	int node = exp->first;
	if(textBegin >= textEnd) return false;
	exp->bol = textBegin;
	exp->eol = textEnd;
	do {
		cur = textBegin;
		while(node != -1) {
			exp->currSubExp = 0;
			cur = cardinalMatchNode(exp,&exp->nodes[node],cur,NULL);
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
int cardinalGetGroupCount(CardinalRegex* exp) {
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
bool cardinalGetSubexp(CardinalRegex* exp, int n, CardinalRegexMatch* subexp) {
	if(n < 0 || n >= exp->nbrSubExpr) return false;
	*subexp = exp->matches[n];
	return true;
}

///////////////////////////////////////////////////////////////////////////////////
//// REGEX LIBRARY
///////////////////////////////////////////////////////////////////////////////////

/// Defines the struct used in Cardinal
typedef struct ScriptRegex {
	/// The regex
	CardinalRegex* regex;
	/// Set if the regex is being used or not
	bool inUse;
} ScriptRegex;

// Creates a new regex expression
static void newRegex(CardinalVM* vm) {
	CardinalValue* val = cardinalGetArgument(vm, 0);
	ScriptRegex* regex = (ScriptRegex*) cardinalGetInstance(vm, val);
	
	regex->inUse = false;
	regex->regex = NULL;
	
	cardinalReturnValue(vm, val);
}

// Destroys regex expression
static void destructRegex(void* obj) {
	ScriptRegex* regex = (ScriptRegex*) obj;
	
	if (regex->inUse) cardinalFreeRegex(regex->regex);
}

// Gets the number of groups that matched within the compiled regex
static void getGroupCountRegex(CardinalVM* vm) {
	CardinalValue* val = cardinalGetArgument(vm, 0);
	ScriptRegex* regex = (ScriptRegex*) cardinalGetInstance(vm, val);
	
	int nbGroups = -1;
	if (regex->inUse) nbGroups = cardinalGetGroupCount(regex->regex);
	
	cardinalReturnDouble(vm, nbGroups);
	cardinalReleaseObject(vm, val);
}

// Compiles a pattern [pattern] into a regex [CardinalRegex]
static void compileRegex(CardinalVM* vm) {
	CardinalValue* val = cardinalGetArgument(vm, 0);
	ScriptRegex* regex = (ScriptRegex*) cardinalGetInstance(vm, val);
	const char* pattern = cardinalGetArgumentString(vm, 1);
	
	if (regex->inUse)
		cardinalFreeRegex(regex->regex);
	
	const char* error = NULL;
	CardinalRegex* x = cardinalCompileRegex(pattern, &error);
	
	if (x) {
		regex->regex = x;
		regex->inUse = true;
		cardinalReturnValue(vm, val);
	}
	else {
		cardinalReturnString(vm, error, strlen(error));
		cardinalReleaseObject(vm, val);
	}
}

// checks if the string [text] can be matched by the regex [exp]
static void matchRegex(CardinalVM* vm) {
	CardinalValue* val = cardinalGetArgument(vm, 0);
	ScriptRegex* regex = (ScriptRegex*) cardinalGetInstance(vm, val);
	const char* pattern = cardinalGetArgumentString(vm, 1);
	bool match = false;
	if (regex->inUse)
		match = cardinalMatch(regex->regex, pattern);
	cardinalReturnBool(vm, match);
	cardinalReleaseObject(vm, val);
}

// Searches for a match to regex [exp] in the string [text]
static void searchRegex(CardinalVM* vm) {
	CardinalValue* val = cardinalGetArgument(vm, 0);
	ScriptRegex* regex = (ScriptRegex*) cardinalGetInstance(vm, val);
	const char* begin = NULL;
	const char* end = NULL;
	const char* text = cardinalGetArgumentString(vm, 1);
	
	bool match = false;
	if (regex->inUse)
		match = cardinalSearch(regex->regex, text, &begin, &end);
	if (match) {
		cardinalReturnString(vm, begin, end-begin);
	}
	else cardinalReturnNull(vm);
	
	cardinalReleaseObject(vm, val);
}

// searches for matched groups in a compiled expression
static void getGroupRegex(CardinalVM* vm) {
	CardinalValue* val = cardinalGetArgument(vm, 0);
	ScriptRegex* regex = (ScriptRegex*) cardinalGetInstance(vm, val);
	double nb = cardinalGetArgumentDouble(vm, 1);
	
	if (regex->inUse) {
		CardinalRegexMatch subExp;
		subExp.len = -1;
		subExp.begin = "empty";
		cardinalGetSubexp(regex->regex, nb, &subExp);
		
		// Put subExp into a string and return
		cardinalReturnString(vm, subExp.begin, subExp.len);
	} else {
		cardinalReturnNull(vm);
	}
	
	cardinalReleaseObject(vm, val);
}

// This module defines the Regex class and its associated methods. They are
// implemented using the C standard library and the above engine
void cardinalLoadRegexLibrary(CardinalVM* vm) {
	// Defines the regex class
	cardinalDefineClass(vm, NULL, "Regex", sizeof(ScriptRegex), NULL);
	
	// Defines the constructor and destructor
	cardinalDefineConstructor(vm, NULL, "Regex", "new", newRegex);
	cardinalDefineDestructor(vm, NULL, "Regex", destructRegex);
	
	// Define the methods on the Regex class
	cardinalDefineMethod(vm, NULL, "Regex", "match(_)", matchRegex);
	cardinalDefineMethod(vm, NULL, "Regex", "search(_)", searchRegex);
	cardinalDefineMethod(vm, NULL, "Regex", "getGroup(_)", getGroupRegex);
	cardinalDefineMethod(vm, NULL, "Regex", "compile(_)", compileRegex);
	cardinalDefineMethod(vm, NULL, "Regex", "getGroupCount()", getGroupCountRegex);
}

