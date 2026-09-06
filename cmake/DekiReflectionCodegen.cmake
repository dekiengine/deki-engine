# DekiReflectionCodegen.cmake
#
# Build-time codegen step using the C++26 static-reflection generator
# (reflection_codegen.cpp). It compiles the generator with a reflection-capable
# GCC (16.1+) and runs it to emit, per package, one <Component>.gen.h stub per
# reflected type plus ONE merged <Prefix>Reflection<shard>.gen.cpp holding every
# definition. The generator is invoked via COMMAND (a plain g++ call), so this
# works regardless of the project's own compiler (MSVC, xtensa-gcc, etc.).
#
# WHY ONE MERGED .gen.cpp
# Package source lists come from file(GLOB CONFIGURE_DEPENDS), which is evaluated at
# CONFIGURE time. A per-type .gen.cpp is therefore a brand new path that only joins
# the build at the next configure: adding a reflected type cost two builds, one to
# generate and one to compile. A file that always exists under the same name changes
# only in content, which the ordinary dependency scan already handles. Nothing is
# lost: package DLLs are unity-built, so these were already being concatenated.
#
# The same rule cuts the other way for CLEANUP: orphans (from the old scheme, or from
# a deleted/renamed type) are emptied in place by the generator rather than deleted,
# because a consumer may already have globbed a file into a target earlier in the same
# configure. Removing it then fails the configure with "Cannot find source file".
#
# WORK UNITS
# A generator TU costs a fixed ~2s (parsing the generator plus the reflection headers)
# before it emits anything, so one TU per package makes 20 small packages pay that floor
# 20 times. This file packs packages into "work units" of about UNIT_TARGET_HEADERS
# reflected headers each, paying the floor once per unit instead.
#
# Splitting a big package the other way (into parallel shards) is supported but off by
# default — see the note on SHARD_MIN_HEADERS in the planner below for the measurements.
#
# Usage:
#   include(DekiReflectionCodegen.cmake)
#   deki_reflection_codegen(
#     UNIT_PREFIX      <name used for scratch dirs + target names>
#     PACKAGE_DIRS      <dir1> <dir2> ...      # four parallel lists, one entry per package
#     PACKAGE_TAGS      <tag1> <tag2> ...      # substring identifying the package, e.g. "deki-2d"
#     PACKAGE_PREFIXES  <Pfx1> <Pfx2> ...      # {Prefix}Registration.gen.cpp
#     PACKAGE_OUTDIRS   <out1> <out2> ...      # where that package's .gen.* go
#     GENERATOR_SRC    <path to reflection_codegen.cpp>
#     GXX              <path to g++ 16.1+>
#     INCLUDE_DIRS     <list of -I roots>
#     DEFINES          <list of compile defines, same as the engine build>
#     [UNIT_TARGET_HEADERS <n>]               # unit sizing knob, default 12
#     [SHARD_MIN_HEADERS <n>]                 # split packages over n headers; 0 (default) = never
#     [CORE_BUILD|PLUGIN]
#   )
#
# For each package tag the function sets, in the caller's scope:
#   DEKI_RC_TARGETS_<c_identifier(tag)> — the codegen targets that package must
#   depend on (more than one when the package is sharded).

function(deki_reflection_codegen)
  cmake_parse_arguments(ARG "CORE_BUILD;PLUGIN" "UNIT_PREFIX;GENERATOR_SRC;GXX;UNIT_TARGET_HEADERS;SHARD_MIN_HEADERS"
                            "PACKAGE_DIRS;PACKAGE_TAGS;PACKAGE_PREFIXES;PACKAGE_OUTDIRS;INCLUDE_DIRS;DEFINES" ${ARGN})
  # CORE_BUILD and PLUGIN both glob recursively + use source-relative includes; CORE_BUILD
  # also emits CoreRegistration + aggregation, PLUGIN emits components only (no registration).
  if(ARG_CORE_BUILD OR ARG_PLUGIN)
    set(_recursive TRUE)
  else()
    set(_recursive FALSE)
  endif()
  if(NOT ARG_UNIT_TARGET_HEADERS)
    set(ARG_UNIT_TARGET_HEADERS 12)
  endif()

  list(LENGTH ARG_PACKAGE_DIRS _npkgs)
  if(_npkgs EQUAL 0)
    message(FATAL_ERROR "deki_reflection_codegen: no PACKAGE_DIRS given")
  endif()
  foreach(_l PACKAGE_TAGS PACKAGE_PREFIXES PACKAGE_OUTDIRS)
    list(LENGTH ARG_${_l} _n)
    if(NOT _n EQUAL _npkgs)
      message(FATAL_ERROR "deki_reflection_codegen: ${_l} has ${_n} entries, expected ${_npkgs}")
    endif()
  endforeach()
  math(EXPR _lastmod "${_npkgs} - 1")

  # ---------------------------------------------------------------------------
  # 1. Per-package inputs: the reflected headers and the explicit adapter functions.
  # ---------------------------------------------------------------------------
  foreach(_i RANGE ${_lastmod})
    list(GET ARG_PACKAGE_DIRS  ${_i} _dir)
    list(GET ARG_PACKAGE_TAGS  ${_i} _tag)

    # Package dirs glob top-level (subdirs hold editor-only extension headers that need
    # the editor DLL); the engine-core and plugin builds glob recursively.
    if(_recursive)
      file(GLOB_RECURSE _hdrs CONFIGURE_DEPENDS "${_dir}/*.h")
      list(FILTER _hdrs EXCLUDE REGEX "/generated/")
      # Keep only headers that declare a reflected type (component / serializable /
      # node), so the generator TU doesn't pull in non-compiling internals. DEKI_NODE
      # headers must be listed directly: node structs live in their own headers with
      # no component, so relying on a transitive include from some component header
      # is fragile (removing that include silently drops every node from codegen).
      set(_keep "")
      foreach(_h ${_hdrs})
        file(READ "${_h}" _ht)
        if(_ht MATCHES "DEKI_COMPONENT\\(" OR _ht MATCHES "DEKI_SERIALIZABLE" OR _ht MATCHES "DEKI_NODE\\(")
          list(APPEND _keep "${_h}")
        endif()
      endforeach()
      set(_hdrs ${_keep})
    else()
      file(GLOB _hdrs CONFIGURE_DEPENDS "${_dir}/*.h")
    endif()
    list(SORT _hdrs)
    set(_PKG_HDRS_${_i} "${_hdrs}")
    list(LENGTH _hdrs _nh)
    set(_PKG_NHDR_${_i} ${_nh})

    # Scan .cpp files for explicit adapter registration functions, the one thing
    # reflection can't see (they're free functions defined in .cpp, not headers).
    # Convention: void {Prefix}_Register{Name}Adapters().
    list(GET ARG_PACKAGE_PREFIXES ${_i} _pfx)
    file(GLOB _cpps CONFIGURE_DEPENDS "${_dir}/*.cpp")
    list(SORT _cpps)
    set(_adapters "")
    foreach(_c ${_cpps})
      file(READ "${_c}" _ctext)
      string(REGEX MATCHALL "void[ \t\r\n]+${_pfx}_Register[A-Za-z0-9_]+Adapters[ \t\r\n]*\\(" _defs "${_ctext}")
      foreach(_d ${_defs})
        string(REGEX MATCH "${_pfx}_Register[A-Za-z0-9_]+Adapters" _fn "${_d}")
        if(NOT _fn IN_LIST _adapters)
          list(APPEND _adapters "${_fn}")
        endif()
      endforeach()
    endforeach()
    set(_PKG_ADAPTERS_${_i} "${_adapters}")
  endforeach()

  # ---------------------------------------------------------------------------
  # 2. Plan the work units. Each entry is "<comma-separated package indices>:<shard>:<shards>".
  #    Packages are packed together in declaration order until a unit is full; a package
  #    over SHARD_MIN_HEADERS is instead split into shards of its own.
  #
  #    Sharding is OFF by default, and that is a measured choice, not caution. Splitting a
  #    package parallelises its per-component emit (~2/3 of its cost) but makes every shard
  #    re-pay the ~2s fixed floor AND re-parse the package's whole header set. That only wins
  #    when there are spare cores. During a real rebuild there are none: a dozen generators
  #    are already competing with each other, so on this 8-core box splitting the plugin
  #    into 2 shards measured 24.6s + 17.7s against 13.9s unsplit. Turn it on only for a
  #    machine with many more cores than work units.
  # ---------------------------------------------------------------------------
  if(NOT ARG_SHARD_MIN_HEADERS)
    set(ARG_SHARD_MIN_HEADERS 0)
  endif()
  set(_units "")
  set(_pack_pkgs "")
  set(_pack_count 0)
  foreach(_i RANGE ${_lastmod})
    set(_n ${_PKG_NHDR_${_i}})
    if(ARG_SHARD_MIN_HEADERS GREATER 0 AND _n GREATER ARG_SHARD_MIN_HEADERS)
      # flush whatever is packed so far, so shards stay a package's own units
      list(LENGTH _pack_pkgs _pack_len)
      if(_pack_len GREATER 0)
        string(REPLACE ";" "," _packed "${_pack_pkgs}")
        list(APPEND _units "${_packed}:0:1")
        set(_pack_pkgs "")
        set(_pack_count 0)
      endif()
      math(EXPR _shards "(${_n} + ${ARG_UNIT_TARGET_HEADERS} - 1) / ${ARG_UNIT_TARGET_HEADERS}")
      math(EXPR _lastshard "${_shards} - 1")
      foreach(_k RANGE ${_lastshard})
        list(APPEND _units "${_i}:${_k}:${_shards}")
      endforeach()
    else()
      # NOTE: test the LENGTH, never the list itself. A pack holding only package index 0
      # is the string "0", and if(<string>) treats "0" as boolean false — which silently
      # dropped the whole pack (and, for a single-package caller like the engine core
      # build, produced zero work units and generated nothing).
      list(LENGTH _pack_pkgs _pack_len)
      math(EXPR _sum "${_pack_count} + ${_n}")
      if(_pack_len GREATER 0 AND _sum GREATER ARG_UNIT_TARGET_HEADERS)
        string(REPLACE ";" "," _packed "${_pack_pkgs}")
        list(APPEND _units "${_packed}:0:1")
        set(_pack_pkgs "")
        set(_pack_count 0)
      endif()
      list(APPEND _pack_pkgs ${_i})
      math(EXPR _pack_count "${_pack_count} + ${_n}")
    endif()
  endforeach()
  list(LENGTH _pack_pkgs _pack_len)
  if(_pack_len GREATER 0)
    string(REPLACE ";" "," _packed "${_pack_pkgs}")
    list(APPEND _units "${_packed}:0:1")
  endif()
  list(LENGTH _units _nunits)
  if(_nunits EQUAL 0)
    message(FATAL_ERROR "deki_reflection_codegen: planned 0 work units for ${_npkgs} package(s)")
  endif()

  # ---------------------------------------------------------------------------
  # 3. Shared precompiled header for the generator's stable include prefix.
  # ---------------------------------------------------------------------------
  # Every generator TU re-parses <meta> + the engine reflection headers; that fixed
  # cost dominates small units. Precompile that prefix ONCE (shared across all units)
  # and force-include it.
  #
  # STALENESS IS CORRECTNESS here, not just speed: the .gch embeds the CONTENT of
  # the engine headers it includes, and GCC does NOT gracefully ignore a .gch whose
  # headers changed on disk — it loads the old definitions and the TU's own fresh
  # #include then redefines every type (a wall of "redefinition of 'struct
  # deki_refl::exported'" errors). So the .gch must be rebuilt whenever anything in
  # its include closure changes: the compile emits a depfile (-MD), the configure-
  # time gate checks every path recorded in it, and under Ninja a build-time rule
  # with DEPFILE regenerates the .gch before any build-time codegen consumes it.
  set(_flags_common -std=c++26 -freflection -DDEKI_REFLECT)
  foreach(d ${ARG_INCLUDE_DIRS})
    list(APPEND _flags_common "-I${d}")
  endforeach()
  foreach(d ${ARG_DEFINES})
    list(APPEND _flags_common "-D${d}")
  endforeach()

  get_filename_component(_gensrcdir "${ARG_GENERATOR_SRC}" DIRECTORY)
  set(_pch_src "${_gensrcdir}/reflection_codegen_pch.h")
  set(_pch_active FALSE)
  if(EXISTS "${_pch_src}")
    set(_pchdir "${CMAKE_BINARY_DIR}/reflcodegen/_pch")
    set(_pch_hdr "${_pchdir}/reflection_codegen_pch.h")
    set(_pch_gch "${_pch_hdr}.gch")
    set(_pch_dep "${_pch_gch}.d")
    # Copy next to where the .gch lives so `-include <name>` + `-I${_pchdir}` resolves the
    # header AND finds its sibling .gch (GCC looks for <resolved-header>.gch).
    configure_file("${_pch_src}" "${_pch_hdr}" COPYONLY)

    # PCH flags mirror the generator TU's (std/reflection/defines/includes) minus
    # the per-unit scratch -I.
    set(_pch_flags ${_flags_common} "-I${_pchdir}")

    # Configure-time staleness: missing artifacts, a newer pch source/generator, or
    # ANY file recorded in the previous compile's depfile being newer than the .gch.
    set(_pch_stale FALSE)
    if(NOT EXISTS "${_pch_gch}" OR NOT EXISTS "${_pch_dep}")
      set(_pch_stale TRUE)
    elseif("${_pch_src}" IS_NEWER_THAN "${_pch_gch}" OR "${ARG_GENERATOR_SRC}" IS_NEWER_THAN "${_pch_gch}")
      set(_pch_stale TRUE)
    else()
      file(READ "${_pch_dep}" _pch_dtxt)
      string(REPLACE "\r" "" _pch_dtxt "${_pch_dtxt}")
      string(REPLACE "\\\n" " " _pch_dtxt "${_pch_dtxt}")
      # Strip the "<path>.gch:" make-target (greedy: dep paths never contain ".gch:").
      string(REGEX REPLACE "^.*\\.gch:" "" _pch_dtxt "${_pch_dtxt}")
      separate_arguments(_pch_dlist UNIX_COMMAND "${_pch_dtxt}")
      foreach(_pd ${_pch_dlist})
        if(NOT EXISTS "${_pd}" OR "${_pd}" IS_NEWER_THAN "${_pch_gch}")
          set(_pch_stale TRUE)
          break()
        endif()
      endforeach()
    endif()

    if(_pch_stale)
      message(STATUS "Reflection codegen: building shared PCH")
      execute_process(COMMAND "${ARG_GXX}" ${_pch_flags} -MD -MF "${_pch_dep}"
                              -x c++-header "${_pch_hdr}" -o "${_pch_gch}"
                      RESULT_VARIABLE _pch_rc)
      if(NOT _pch_rc EQUAL 0)
        message(WARNING "deki_reflection_codegen: shared PCH build failed; generator will compile without it")
        file(REMOVE "${_pch_gch}" "${_pch_dep}")
      endif()
    endif()

    if(EXISTS "${_pch_gch}")
      set(_pch_active TRUE)
      # Build-time regeneration (Ninja only: DEPFILE carries the full include
      # closure), so the header-edit hot path — which regenerates codegen via the
      # stamp custom command WITHOUT reconfiguring — never compiles against a stale
      # .gch. ONE rule per build tree: the .gch path is global (CMAKE_BINARY_DIR),
      # so the guard must be too. It was a DIRECTORY property, which let a second
      # caller in another directory scope (the package unit tests under tests/)
      # register a duplicate rule and fail the Ninja generate with "multiple
      # rules generate reflcodegen/_pch/...".
      if(CMAKE_GENERATOR MATCHES "Ninja")
        get_property(_pch_rule_added GLOBAL PROPERTY _DEKI_REFLCODEGEN_PCH_RULE)
        if(NOT _pch_rule_added)
          set_property(GLOBAL PROPERTY _DEKI_REFLCODEGEN_PCH_RULE TRUE)
          add_custom_command(
            OUTPUT "${_pch_gch}"
            COMMAND "${ARG_GXX}" ${_pch_flags} -MD -MF "${_pch_dep}"
                    -x c++-header "${_pch_hdr}" -o "${_pch_gch}"
            DEPENDS "${_pch_hdr}" "${_pch_src}" "${ARG_GENERATOR_SRC}"
            DEPFILE "${_pch_dep}"
            COMMENT "Reflection codegen shared PCH"
            VERBATIM)
        endif()
      endif()
    endif()
  endif()

  # ---------------------------------------------------------------------------
  # 4. Emit one codegen step per work unit.
  # ---------------------------------------------------------------------------
  set(_all_targets "")
  foreach(_unit ${_units})
    string(REPLACE ":" ";" _parts "${_unit}")
    list(GET _parts 0 _modcsv)
    list(GET _parts 1 _shard)
    list(GET _parts 2 _shardcount)
    string(REPLACE "," ";" _pkgidx "${_modcsv}")

    # Unit name: first package's tag, plus the shard when the package is split. Stable
    # across configures as long as the package set is, so Ninja doesn't churn.
    list(GET _pkgidx 0 _first)
    list(GET ARG_PACKAGE_TAGS ${_first} _firsttag)
    string(MAKE_C_IDENTIFIER "${ARG_UNIT_PREFIX}_${_firsttag}" _unitid)
    if(_shardcount GREATER 1)
      set(_unitid "${_unitid}_s${_shard}")
    endif()

    # Per-unit codegen scratch (include manifest, the compiled generator, stamp).
    set(_gendir "${CMAKE_CURRENT_BINARY_DIR}/reflcodegen/${_unitid}")
    file(MAKE_DIRECTORY "${_gendir}")

    # ---- the unit's include manifest + package descriptor table ----
    set(_inc "// AUTO-GENERATED by DekiReflectionCodegen.cmake - DO NOT EDIT\n")
    string(APPEND _inc "#define DEKI_CODEGEN_SHARD_INDEX ${_shard}\n")
    string(APPEND _inc "#define DEKI_CODEGEN_SHARD_COUNT ${_shardcount}\n")
    if(ARG_CORE_BUILD)
      string(APPEND _inc "#define DEKI_CORE_BUILD 1\n")
    endif()
    if(ARG_PLUGIN)
      string(APPEND _inc "#define DEKI_NO_REGISTRATION 1\n")
    endif()

    set(_unit_hdrs "")
    set(_rows "")
    foreach(_i ${_pkgidx})
      list(GET ARG_PACKAGE_DIRS     ${_i} _dir)
      list(GET ARG_PACKAGE_TAGS     ${_i} _tag)
      list(GET ARG_PACKAGE_PREFIXES ${_i} _pfx)
      list(GET ARG_PACKAGE_OUTDIRS  ${_i} _out)
      set(_adapters "${_PKG_ADAPTERS_${_i}}")
      string(MAKE_C_IDENTIFIER "${_tag}" _tagid)
      list(LENGTH _adapters _nad)
      if(_nad GREATER 0)
        string(APPEND _inc "static constexpr const char* const kAdapters_${_tagid}[] = {")
        foreach(_a ${_adapters})
          string(APPEND _inc " \"${_a}\",")
        endforeach()
        string(APPEND _inc " };\n")
        set(_adref "kAdapters_${_tagid}")
      else()
        set(_adref "nullptr")
      endif()
      string(APPEND _rows "    { \"${_tag}\", \"${_pfx}\", \"${_dir}\", \"${_out}\", ${_adref}, ${_nad} },\n")
      list(APPEND _unit_hdrs ${_PKG_HDRS_${_i}})
    endforeach()
    string(APPEND _inc "static constexpr CodegenPackage kPackages[] = {\n${_rows}};\n")
    list(LENGTH _pkgidx _npkgs_unit)
    string(APPEND _inc "static constexpr int kPackageCount = ${_npkgs_unit};\n")

    foreach(_i ${_pkgidx})
      list(GET ARG_PACKAGE_DIRS ${_i} _dir)
      list(GET ARG_PACKAGE_TAGS ${_i} _tag)
      foreach(h ${_PKG_HDRS_${_i}})
        if(_recursive)
          file(RELATIVE_PATH _rel "${_dir}" "${h}")
          string(APPEND _inc "#include \"${_rel}\"\n")
        else()
          get_filename_component(_n "${h}" NAME)
          string(APPEND _inc "#include \"${_tag}/${_n}\"\n")
        endif()
      endforeach()
    endforeach()

    # Write-if-different: an unconditional file(WRITE) bumps the .inc mtime on every
    # configure, which would defeat the freshness gate below (and the build-time
    # stamp) by making every unit look changed. Only rewrite when the include set
    # actually changed, so mtimes stay meaningful.
    set(_inc_existing "")
    if(EXISTS "${_gendir}/deki_codegen_includes.inc")
      file(READ "${_gendir}/deki_codegen_includes.inc" _inc_existing)
    endif()
    if(NOT "${_inc_existing}" STREQUAL "${_inc}")
      file(WRITE "${_gendir}/deki_codegen_includes.inc" "${_inc}")
    endif()

    # ---- bootstrap stubs ----
    # Each component header does `#include "generated/<X>.gen.h"` (the engine's own:
    # `"deki/generated/<X>.gen.h"`), which doesn't exist
    # on a fresh package (generated/ is gitignored). The generator must COMPILE the
    # header to reflect it, so seed empty stubs first; the generator then overwrites
    # them with the real files. Without the stubs, a fresh-package install fails to
    # compile the header on the very first configure.
    foreach(_i ${_pkgidx})
      list(GET ARG_PACKAGE_OUTDIRS ${_i} _out)
      file(MAKE_DIRECTORY "${_out}")
      foreach(h ${_PKG_HDRS_${_i}})
        file(READ "${h}" _hc)
        string(REGEX MATCHALL "#include \"(deki/)?generated/[^\"]+\\.gen\\.h\"" _gincs "${_hc}")
        foreach(_gi ${_gincs})
          string(REGEX REPLACE "#include \"(deki/)?generated/([^\"]+)\"" "\\2" _gn "${_gi}")
          if(NOT EXISTS "${_out}/${_gn}")
            file(WRITE "${_out}/${_gn}" "#pragma once\n")
          endif()
        endforeach()
      endforeach()
    endforeach()

    # ---- generator flags for this unit (the .inc dir goes first on the include path) ----
    set(_flags "-I${_gendir}" ${_flags_common})
    if(_pch_active)
      # Force-include the precompiled prefix (first, so GCC serves it from the .gch). The
      # generator's own #include of the same headers then de-dupes via include guards.
      list(APPEND _flags "-I${_pchdir}" -include reflection_codegen_pch.h -Winvalid-pch)
    endif()

    set(_tool "${_gendir}/autogen_tool.exe")
    set(_stamp "${_gendir}/codegen.stamp")

    # Orphan per-type .gen.cpp files (from the old one-file-per-type scheme, or from a
    # type that was deleted or renamed) are emptied in place by the GENERATOR, not
    # removed here. Deleting them at configure time is not safe: a consumer may already
    # have globbed them into a target earlier in this same configure, and CMake then
    # fails with "Cannot find source file". Content is free to change at any point; the
    # SET of files is not.

    # ---- configure-time generation, gated on staleness ----
    # It still guarantees a downstream file(GLOB) sees the .gen.cpp on a fresh checkout
    # (or when a header / the generator changed), but skips the expensive C++26
    # compile+run when the outputs are already up to date. Adding/removing a source file
    # forces a full reconfigure (CONFIGURE_DEPENDS globs), so the gate is what keeps that
    # from re-running every unit's generator.
    set(_stale FALSE)
    if(NOT EXISTS "${_stamp}")
      set(_stale TRUE)  # fresh checkout, or the scratch dir was wiped
    else()
      foreach(_i ${_pkgidx})
        list(GET ARG_PACKAGE_OUTDIRS ${_i} _out)
        file(GLOB _existing_gen "${_out}/*.gen.cpp")
        if(NOT _existing_gen)
          set(_stale TRUE)  # generated/ was wiped
          break()
        endif()
      endforeach()
    endif()
    if(NOT _stale)
      foreach(_dep "${ARG_GENERATOR_SRC}" "${_gendir}/deki_codegen_includes.inc" ${_unit_hdrs})
        if("${_dep}" IS_NEWER_THAN "${_stamp}")
          set(_stale TRUE)
          break()
        endif()
      endforeach()
    endif()

    if(_stale)
      message(STATUS "Reflection codegen: regenerating ${_unitid}")
      execute_process(COMMAND "${ARG_GXX}" ${_flags} "${ARG_GENERATOR_SRC}" -o "${_tool}"
                      RESULT_VARIABLE _rc)
      if(_rc EQUAL 0)
        execute_process(COMMAND "${_tool}" RESULT_VARIABLE _rc2)
        if(NOT _rc2 EQUAL 0)
          message(FATAL_ERROR "deki_reflection_codegen: generator run failed for ${_unitid}. "
                              "Reflection metadata would be stale or missing and the build would fail later with unrelated-looking link errors; the generator's output is above.")
        else()
          # Stamp now so this configure's work isn't repeated by the build-time step
          # below (its DEPENDS are already satisfied), and so later configures skip.
          file(TOUCH "${_stamp}")
        endif()
      else()
        message(FATAL_ERROR "deki_reflection_codegen: generator compile failed for ${_unitid}. "
                            "The C++26 reflection generator needs GCC 16.1+ (${ARG_GXX} was used); "
                            "install it (MSYS2: pacman -S mingw-w64-x86_64-gcc; Ubuntu: ppa:ubuntu-toolchain-r/test g++-16) "
                            "and pass -DDEKI_GXX16=<path to g++>.")
      endif()
    endif()

    # ---- build-time: regenerate when the generator or any unit header changes ----
    # When the PCH is in play (Ninja), depend on the .gch so its rebuild rule runs
    # first and this compile never consumes a stale precompiled prefix.
    set(_stamp_pch_dep "")
    if(_pch_active AND CMAKE_GENERATOR MATCHES "Ninja")
      set(_stamp_pch_dep "${_pch_gch}")
    endif()
    add_custom_command(
      OUTPUT "${_stamp}"
      COMMAND "${ARG_GXX}" ${_flags} "${ARG_GENERATOR_SRC}" -o "${_tool}"
      COMMAND "${_tool}"
      COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
      DEPENDS "${ARG_GENERATOR_SRC}" ${_unit_hdrs} "${_gendir}/deki_codegen_includes.inc" ${_stamp_pch_dep}
      COMMENT "Reflection codegen (C++26) for ${_unitid}"
      VERBATIM
    )
    add_custom_target("reflcodegen_${_unitid}" ALL DEPENDS "${_stamp}")
    list(APPEND _all_targets "reflcodegen_${_unitid}")

    # Record the target against every package it generates for, so the caller can
    # order each package's compilation after its codegen.
    foreach(_i ${_pkgidx})
      list(GET ARG_PACKAGE_TAGS ${_i} _tag)
      string(MAKE_C_IDENTIFIER "${_tag}" _tagid)
      list(APPEND _RC_TARGETS_${_tagid} "reflcodegen_${_unitid}")
      set(DEKI_RC_TARGETS_${_tagid} "${_RC_TARGETS_${_tagid}}" PARENT_SCOPE)
    endforeach()
  endforeach()

  list(LENGTH _units _nunits)
  message(STATUS "Reflection codegen: ${_npkgs} package(s) in ${_nunits} work unit(s)")
endfunction()
