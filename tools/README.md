# Deki Engine Reflection Codegen

Component metadata, deserialization, and registration code is generated at build
time from the C++ headers themselves, using **C++26 static reflection**.

## Files

- `reflection_codegen.cpp` — the generator. Compiled and run at configure/build
  time to emit `<Component>.gen.h` / `<Component>.gen.cpp` (property metadata,
  hash-switch deserialization) plus the per-package `{Prefix}Registration.gen.cpp`
  and, for the engine-core build, `CoreRegistration` + the aggregation files.
- `../cmake/DekiReflectionCodegen.cmake` — the `deki_reflection_codegen()` helper
  that compiles the generator with a reflection-capable GCC and runs it over a
  package / the engine / the project plugin.

## How it runs

The generator is built and invoked via a plain `g++` call, independent of the
project's own toolchain (MSVC, xtensa-gcc, etc.):

```
g++ -std=c++26 -freflection -DDEKI_REFLECT <includes/defines> reflection_codegen.cpp -o autogen_tool
autogen_tool <output-dir>
```

It requires **GCC 16.1+** (set `-DDEKI_GXX16=<path to g++>`, or it is auto-detected
from `C:/msys64/mingw64/bin` then `PATH`). There is no fallback backend: the editor
build, every package build, and the desktop/firmware builds all consume the
reflection-generated `.gen.cpp`.

## Authoring components

Declare components with the `DEKI_COMPONENT` / `DEKI_EXPORT` macros and the
generator discovers them by reflecting the type:

```cpp
DEKI_COMPONENT(MyComponent)
    DEKI_EXPORT float volume = 1.0f;
    DEKI_EXPORT std::string format = "Format1";
DEKI_COMPONENT_IMPL(MyComponent)
```

No manual registration or hand-written metadata is needed.
