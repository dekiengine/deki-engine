# Deki Engine Reflection Codegen

Component metadata, deserialization, and registration code is generated at build
time from the C++ headers themselves, using **C++26 static reflection**.

## Files

- `reflection_codegen.cpp` - the generator. Compiled and run at configure/build
  time to emit the per-package `{Prefix}Reflection0.gen.cpp` (type entries,
  hash-switch deserialization, editor metadata) plus `{Prefix}Registration.gen.cpp`
  and, for the engine-core build, `CoreRegistration` + the aggregation files.
- `../cmake/DekiReflectionCodegen.cmake` - the `deki_reflection_codegen()` helper
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

A component is any class deriving publicly from `Deki::Component`; the generator discovers it by reflecting its bases, and its
identity is its qualified class name (`Deki::TypeId<T>()`, see
`include/deki/TypeId.h`). Optional markers above the class add editor metadata:

```cpp
DEKI_CATEGORY("Enemies")
DEKI_DESCRIPTION("Hit points for anything that can be damaged.")
class Health : public Deki::Component
{
public:
    DEKI_EXPORT int hp = 10;
};
```

Per package the generator writes one merged `{Prefix}Reflection0.gen.cpp` (a
`ComponentTypeInfo` entry, deserializer, reference fixups, field table and
editor metadata per class) and `{Prefix}Registration.gen.cpp`, which hands the
entries to the factory and the registries. Nothing is included from a
component header, and no manual registration exists.
