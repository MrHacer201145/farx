# Farx programming language
What is farx? My first programming language that compiles to llvm ir.
I initially created it with the goal of being C, but with a different syntax. Until a couple of days later, 
I realized it didn't really resemble C anymore, so I started developing it in a different direction...

# Types

The thing below shows the names of types in my language - their direct alternative in C

```
int1 - bool
int8 - char
int - int (surprise)
int64 - long
void - void (yeah...)
ptr - nothing, this is a pointer to obj that can be any type
str - char* (this is the same ptr, but just with syntax sugar)
```
If you want more types, then just ask me in "Issues" menu, i would like to hear from someone!

# Examples

Hello, world!
```
fn puts: int;

fn main(): int {
    puts("Hello, world!") // We cant use \n in printf, because... \n doesn't work :(((
    return 0
}
```

If-else condition
```
fn puts: int;

fn main(): int {
    int Bob's_debt = 1000 // We can use that as a variable name!!!
    if (Bob's_debt > 100) {
        puts("Oh, my gosh...") // Im very sorry, Bob...
    } else {
        puts("Not that bad")
    }
    return 0
}
```

While loop
```
fn printf: int;
fn puts: int;

fn main(): int {
    int a = 0
    while (a < 100) {
        a = a + 1
    }
    printf("a = %i", a)
    puts("") // We use 'puts' just like 'cr' in forth
    return 0
}
```

Array
```
fn printf: int;
fn puts: int;

fn main(): int {
    int a[100]
    a[1] = 10
    a[2] = 30
    printf("a[1] is %i", a[1])
    puts("")
    return 0
}
```

Struct
```
fn printf: int;
fn puts: int;

fn main(): int {
    struct two_ints {
        int
        int
    }
    two_ints mini_array; // To use declaration of variable without assigning value to it just place ';' ath the end of it
    mini_array[1] = 100
    printf("Second element is %i", mini_array[1])
    puts("")
    return 0
}
```

Namespaces
```
fn puts: int;

namespace i {
    namespace like {
        namespace cpp {
            fn main: void {
                puts("Yeah")
            }
        }
    }
}

fn main(): int {
    i::like::cpp::main()
    return 0
}
```

# Usage
I didn't write stdlib, so just declare function from c (for it we need to use clang).
Actually, i thinking about adding support for Rust/C++, but they mangle their functions, so yeah... pretty sad

First we need to compile cpp main file
```
clang++ main.cpp $(llvm-config --cflags --ldflags --libs core irreader native) -o farx --std=c++23 -O2
./farx <ypur file> > <output file.ll>
clang <output file.ll> -o <your output binary file>
# And then you can just execute it like normal binary
# ./smth for example 
```

# License
Farx is licensed under MIT License, so you can do a lot of things with it!
