// Precompiled include prefix for the Deki reflection codegen (reflection_codegen.cpp).
//
// Holds ONLY the stable headers (stdlib, <meta>, engine reflection headers),
// the same for every package and the plugin. DekiReflectionCodegen.cmake
// precompiles this once into a shared .gch and force-includes it (-include), so
// each per-package generator compile skips re-parsing <meta>, the largest fixed
// cost. The per-package component headers are NOT here; they come through
// deki_codegen_includes.inc.
//
// Only a speed-up: reflection_codegen.cpp includes the same headers itself and
// compiles fine without a PCH. Keep this list in step with the stable includes
// at the top of reflection_codegen.cpp (a mismatch costs speed, never
// correctness).
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <deki/reflection/Property.h>
#include <deki/Vector.h>
#include <deki/Color.h>
#include <deki/assets/AssetRef.h>
#include <deki/reflection/ObjectRef.h>
#include <deki/reflection/PropertyRef.h>
#include <meta>
#include <iostream>
#include <fstream>
#include <string_view>
#include <vector>
#include <utility>
#include <algorithm>
#include <charconv>
