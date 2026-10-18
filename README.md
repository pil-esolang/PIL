<img src="assets/pil.png"/>

## About
PIL is a primitive interpreted esolang (esoteric programming language) based off COBOL and assembly. It is a loosely-typed language with no GC.

Here's a factorial example: 
```txt
# calculates the factorial of N
factorial(n)
   local result;                 # define a new variable
   leeq n, 1, $0;                # check if N is smaller or equal to 1
   jmp $0, factorial-end;        # if it is, jump to end, where we return 1

   sub n, 1, $1;                 # calculate n-1
   call result, factorial, $1;   # call factorial and store in result
   mul result, n, result;        # calculate the factorial of N
   return result;                # return the result

factorial-end:
   return 1;

main()                           # main is called on start
   factorial 5.0;                # calculate 5!
   println r$0;                  # 120
```
Think of it like interpreted assembly with functions, returns and higher-level instructions. Documentation can be found [here](https://pil-esolang.github.io/).

## Building
You'll first need to clone the repository or download it as ZIP. If cloning:
```bash
git clone https://github.com/pil-esolang/PIL.git
cd PIL
```

The project uses C++20 and can be built with CMake:
```bash
cmake -B build
cmake --build build
```
After, the executable will be located in `bsuild/pil`.

You can also simply build using your favorite compiler:
```bash
g++ -std=c++20 -DPIL_MAJOR=0 -DPIL_MINOR=0 -DPIL_PATCH=0 -Iinclude source/*.cpp -o pil
```

## Installation
You can download a pre-built executable from [Github Releases](https://github.com/pil-esolang/PIL/releases).

## Usage
To run a file or cached bytecode:
```bash
pil run FILE.pil/o
```
To compile a file into cached bytecode:
```bash
pil compile FILE.pil OUTPUT.pilo
```

## Credits
This project was made by Daniel Vishnevsky and is licensed under MIT License.
