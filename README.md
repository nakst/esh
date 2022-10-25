# esh

A small scripting language to replace Unix shell scripts.

## Building

Run `./build.sh`, or `./esh build.esh` if you've already built it.

## Running examples

For example, run `./esh examples/hello_world.esh`.

## Documentation

See `examples/basic_usage.esh` for a whirlwind tour of the language's syntax.

TODO Make an example demonstrating the standard library.

## Rationale

Why did I make another programming language? Because I felt like there wasn't a good statically typed language to compete with Unix shell scripts. That is, something lightweight and designed for simple file operations and launching other processes. If you want something dynamically typed, you're probably better off with Python, for example.
