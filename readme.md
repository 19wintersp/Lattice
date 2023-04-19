# Lattice

**Less-awful text templating in C.**

Lattice is a templating engine written in pure C which processes textual
templates interspersed with lattice code. Lattice operates on JSON data (via
[cJSON](https://github.com/DaveGamble/cJSON),
[JSON-C](https://github.com/json-c/json-c) (not recommended), or
[Jansson](https://github.com/akheron/jansson)) for simplicity and portability,
and offers a small DSL within this which allows for basic manipulation and
inspection of data in an immutable environment. It is designed with HTML in
mind, but can be adapted for use with any textual format with minimal effort.

Licensed under the MIT licence.
