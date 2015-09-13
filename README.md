Cardinal is an extension from ThunderdogScript. 

It is a scripting language designed around the concept of being completely dynamic. 
The language natively supports manual memory management. This can be disable if not required.

```dart
IO.println("Hello, world!")

function func {
    IO.println("let's get started")
}

class Welcome {

	public field home
	
	letsBegin() {}
	
	+(operatorOverloading) {}
}
```

Cardinal has been confirmed to compile and run on Linux (Arch Linux and Ubuntu), Windows (8.1 and 10) and MacOSX (Yosemite). 

The codebase is about **12000 lines long** which keeps the language and libraries small.
It is fully documented and easily understandable. You can skim [the whole thing][src] in one sitting. 
Cardinal is written fairly clean. It's written in warning-free standard C99. It compiles cleanly as C99 or C++98 and onwards.

Cardinal uses a fast single-pass compiler and a simple and compact object representation. 
Because of this, Cardinal is **fast**. A stack based VM is used in Cardinal. This keeps things simple and still retains
good performance since Cardinal is heavily focused upon function calls.

Cardinal is a class based scripting languages. There are lots of scripting languages out there,
but many have unusual or non-existent object models like Lua. Cardinal has **first-class objects**. 
This to allow for a flexible scripting language.
Cardinal also places focus on **functional programming**. First class functions,  lazy evaluation and closures are supported. This allows scripts to 
be written in a functional style. This comes in handy for behaviour scripts.

Cardinal implements fiber to be able to concurrently execute multiple coroutines.

Cardinal is intended for **embedding** in applications, mainly for embedding within a game engine. 
It has no dependencies, a small standard library, and a simple but extended C API from 
which functions and classes can be linked to Thunderdog-Script. However it can also be used as a general purpose language.

[src]: https://github.com/TheAxeC/Cardinal/tree/master/src