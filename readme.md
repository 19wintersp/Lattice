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

## Documentation

See [doc/](doc/index.md).

## Build instructions

These instructions are designed for Linux. Other systems are not currently
supported in the source, but contributions which address this are welcome.

Lattice requires no libraries other than the JSON library/ies to be used.

### Cloning

First, you need to clone the repository locally.

```
git clone https://github.com/19wintersp/Lattice lattice/
cd lattice/
```

### Building

To build everything, run:

```
make
```

You can build only the library with:

```
make lib
```

### Build options

The Makefile will select whichever JSON libraries are available via `pkg-config`
unless an option (or multiple) is provided to make, as follows:

```
make cjson=no
make jsonc=no
make jansson=no
```

If an option is provided indicating use of a library, as in `cjson=yes`, and the
library is not found, an error will occur.

By default, a library will be selected automatically for building the binary
utility, but you can select which library to use with the `tool` option, as
follows:

```
make tool=cjson
make tool=jsonc
make tool=jansson
```
