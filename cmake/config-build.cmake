# Do we want to build extra SDK stuff or just the game binary?
option(RTS_BUILD_CORE_TOOLS "Build core tools" ON)
option(RTS_BUILD_CORE_EXTRAS "Build core extra tools/tests" OFF)
option(RTS_BUILD_ZEROHOUR "Build Zero Hour code." ON)
option(RTS_BUILD_GENERALS "Build Generals code." ON)
option(RTS_BUILD_OPTION_PROFILE "Build code with the \"Profile\" configuration." OFF)
option(RTS_BUILD_OPTION_PROFILE_TRACY "Build code with Tracy profiling enabled." OFF)
option(RTS_BUILD_OPTION_DEBUG "Build code with the \"Debug\" configuration." OFF)
option(RTS_BUILD_OPTION_ASAN "Build code with Address Sanitizer." OFF)
option(RTS_BUILD_OPTION_OPTIMIZED "Enable aggressive MSVC Release optimizations." OFF)
set(RTS_BUILD_OPTION_PGO "OFF" CACHE STRING "MSVC profile-guided optimization mode.")
set_property(CACHE RTS_BUILD_OPTION_PGO PROPERTY STRINGS OFF GENERATE USE)
set(RTS_PGO_FILE "${CMAKE_BINARY_DIR}/genzh.pgd" CACHE FILEPATH "MSVC PGO database path.")
option(RTS_BUILD_OPTION_VC6_FULL_DEBUG "Build VC6 with full debug info." OFF)
option(RTS_BUILD_OPTION_FFMPEG "Enable FFmpeg support" OFF)

# TheSuperHackers: Split floating point model per side.
# The lockstep simulation needs bit identical results on every machine, while rendering does not
# participate in the CRC and can be compiled as fast as possible.
#
# RTS_BUILD_OPTION_SIM_FP_STRICT is a DIAGNOSTIC option, not a shipping configuration. With
# RTS_BUILD_OPTION_UNIFIED_AVX2 enabled it is normally unnecessary: /fp:strict adds two things
# over /fp:precise, and the first one stops mattering once the ISA is unified -
#   1. it disables FMA contraction, which only mattered because an AVX2 build and an SSE2 build
#      would round differently. One binary on one ISA means every machine executes the very same
#      FMA (or the very same separate mul+add), so the results agree regardless.
#   2. it stops the compiler assuming the default FP environment. That one still has value, but
#      it is already covered by D3DCREATE_FPU_PRESERVE (command line -FPUPreserve 1) plus the
#      _controlfp call in GameLogic, which keep the runtime FP state fixed.
# Keep it around for CRC debugging: if a mismatch reproduces under /fp:precise but disappears
# under /fp:strict, the cause is a runtime FP environment difference, not a code path issue.
# Note it costs real performance (it disables /GL and /LTCG, see below) - do not ship it.
option(RTS_BUILD_OPTION_SIM_FP_STRICT "DIAGNOSTIC: simulation with /arch:AVX2 + /fp:strict (MSVC only)." OFF)
option(RTS_BUILD_OPTION_RENDER_FP_FAST "Compile the render libraries with /fp:fast (MSVC only)." OFF)

# TheSuperHackers: Compile everything (simulation AND render) for AVX2, instead of keeping the
# simulation on SSE2. Shipping one binary for one ISA is what actually makes aggressive
# floating point optimization safe:
#   * FMA3 is part of the AVX2 spec, so every AVX2 CPU implements it. Whether the compiler
#     contracts a*b+c into an FMA or not, every machine runs the exact same instruction and
#     gets the exact same bits - the AVX2-vs-SSE2 result split disappears.
#   * Reassociation done by /fp:fast is decided at compile time and baked into the binary, so
#     it cannot differ per machine either.
# Requirement: every player's CPU must support AVX2 (Intel Haswell 2013+ / AMD Excavator 2015+).
# Note this does NOT protect against a differing MXCSR (FTZ/DAZ/rounding) at runtime - see
# D3DCREATE_FPU_PRESERVE and the _controlfp call in GameLogic for that.
option(RTS_BUILD_OPTION_UNIFIED_AVX2 "Compile all targets with /arch:AVX2 (MSVC only, requires AVX2 CPUs)." OFF)

# TheSuperHackers @tweak diagnostic switch. Off by default: WWMath uses the portable
# implementations (SSE2 conversions, CRT sinf/cosf/sqrtf). Turn this on to restore the *legacy*
# x87 inline assembly (fistp/fsin/fcos/fsqrt/Newton-Raphson inverse sqrt) - handy when
# bisecting a rendering regression that looks like "the math changed". Must be defined for
# every translation unit that includes wwmath.h, so it goes on the global interface target.
option(RTS_BUILD_OPTION_WW_MATH_X87_ASM "Restore the legacy x87 inline assembly in WWMath (MSVC x86 only, diagnostic)." OFF)
if(RTS_BUILD_OPTION_WW_MATH_X87_ASM)
    target_compile_definitions(core_config INTERFACE RTS_WW_MATH_X87_ASM)
endif()

# TheSuperHackers @tweak diagnostic switch. PGO covers the whole binary by default (render
# libraries included); turn this on to keep core_wwmath, z_ww3d2 and z_gameenginedevice out
# of /GL - and therefore out of profile instrumentation and link-time PGO - when bisecting a
# rendering regression. Requires a full rebuild because /GL changes the object format.
option(RTS_BUILD_OPTION_PGO_EXCLUDE_RENDER "Exclude the render libraries from PGO (diagnostic)." OFF)

if(NOT RTS_BUILD_ZEROHOUR AND NOT RTS_BUILD_GENERALS)
    set(RTS_BUILD_ZEROHOUR TRUE)
    message("You must select one project to build, building Zero Hour by default.")
endif()

add_feature_info(CoreTools RTS_BUILD_CORE_TOOLS "Build Core Mod Tools")
add_feature_info(CoreExtras RTS_BUILD_CORE_EXTRAS "Build Core Extra Tools/Tests")
add_feature_info(ZeroHourStuff RTS_BUILD_ZEROHOUR "Build Zero Hour code")
add_feature_info(GeneralsStuff RTS_BUILD_GENERALS "Build Generals code")
add_feature_info(ProfileBuild RTS_BUILD_OPTION_PROFILE "Building as a \"Profile\" build")
add_feature_info(DebugBuild RTS_BUILD_OPTION_DEBUG "Building as a \"Debug\" build")
add_feature_info(AddressSanitizer RTS_BUILD_OPTION_ASAN "Building with address sanitizer")
add_feature_info(OptimizedBuild RTS_BUILD_OPTION_OPTIMIZED "Aggressive Release optimization")
add_feature_info(PGO RTS_BUILD_OPTION_PGO "Profile-guided optimization mode")
add_feature_info(SimFpStrict RTS_BUILD_OPTION_SIM_FP_STRICT "Simulation compiled with /arch:AVX2 + /fp:strict")
add_feature_info(RenderFpFast RTS_BUILD_OPTION_RENDER_FP_FAST "Render libraries compiled with /fp:fast")
add_feature_info(UnifiedAvx2 RTS_BUILD_OPTION_UNIFIED_AVX2 "All targets compiled with /arch:AVX2 (requires AVX2 CPUs)")

if(MSVC AND RTS_BUILD_OPTION_OPTIMIZED AND NOT IS_VS6_BUILD)
    # CMake's default Release flags carry /Ob2, which our /Ob3 supersedes. Drop the stale
    # flag from the cache so cl.exe stops emitting "D9025: overriding '/Ob2' with '/Ob3'"
    # on every single translation unit. The effective inlining level is unchanged (/Ob3).
    foreach(_rts_lang C CXX)
        string(REPLACE "/Ob2" "" _rts_flags "${CMAKE_${_rts_lang}_FLAGS_RELEASE}")
        string(STRIP "${_rts_flags}" _rts_flags)
        set(CMAKE_${_rts_lang}_FLAGS_RELEASE "${_rts_flags}" CACHE STRING
            "Flags used by the compiler during Release builds." FORCE)
    endforeach()

    target_compile_options(core_config INTERFACE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Ob3>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/Ot>
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Release>:/Gw>
    )
    # TheSuperHackers: /GL (whole program optimization) is dropped when the simulation is built
    # with /fp:strict. With /GL + /LTCG the linker re-optimizes across translation units and may
    # pull the /fp:fast render code into the deterministic simulation, silently breaking lockstep.
    # Note that a /GL- on the z_gameengine target does NOT help: CMake emits a target's own
    # COMPILE_OPTIONS *before* the INTERFACE ones inherited from linked targets, so this INTERFACE
    # /GL would land after it and win ("D9025: overriding '/GL-' with '/GL'").
    # TheSuperHackers @tweak targets that set the RTS_NO_PGO property are left out of /GL, and
    # therefore out of profile instrumentation and out of the link-time re-optimization that
    # PGO performs. RTS_BUILD_OPTION_PGO_EXCLUDE_RENDER (diagnostic, off by default) uses this
    # to keep the render side on plain /O2 codegen while the simulation stays PGO optimized.
    if(NOT RTS_BUILD_OPTION_SIM_FP_STRICT)
        target_compile_options(core_config INTERFACE
            $<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:$<TARGET_PROPERTY:RTS_NO_PGO>>>>:/GL>)
    endif()
    # Do NOT add /arch:SSE2 here. CMake emits a target's own COMPILE_OPTIONS *before*
    # the INTERFACE_COMPILE_OPTIONS inherited from linked targets, so a global /arch:SSE2
    # on core_config would land *after* the render libraries' own /arch:AVX2 and silently
    # downgrade them back to SSE2 ("D9025: overriding '/arch:AVX2' with '/arch:SSE2'").
    # On x86 MSVC defaults to SSE2 already, so omitting this flag loses nothing for the
    # non-render targets; it is only pinned when AVX2 is not in play.
    if(RTS_BUILD_OPTION_UNIFIED_AVX2)
        # TheSuperHackers: one ISA for the whole binary. Applied to every configuration (not just
        # Release) so a Debug build produces the same arithmetic as the shipped Release build.
        # Render libraries that add their own /arch:AVX2 when RTS_BUILD_OPTION_RENDER_AVX2 is on
        # get the identical value, so the later INTERFACE flag is a harmless no-op for them.
        target_compile_options(core_config INTERFACE /arch:AVX2)
    elseif(NOT RTS_BUILD_OPTION_RENDER_AVX2)
        target_compile_options(core_config INTERFACE $<$<CONFIG:Release>:/arch:SSE2>)
    endif()
    target_link_options(core_config INTERFACE
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
    # TheSuperHackers: see the /GL note above. /LTCG must go as well, otherwise the linker still
    # has intermediate language for the render libraries to inline into the simulation.
    if(NOT RTS_BUILD_OPTION_SIM_FP_STRICT)
        target_link_options(core_config INTERFACE $<$<CONFIG:Release>:/LTCG>)
    endif()
endif()

if(NOT "${RTS_BUILD_OPTION_PGO}" MATCHES "^(OFF|GENERATE|USE)$")
    message(FATAL_ERROR "RTS_BUILD_OPTION_PGO must be OFF, GENERATE, or USE")
elseif(MSVC AND NOT "${RTS_BUILD_OPTION_PGO}" STREQUAL "OFF" AND NOT IS_VS6_BUILD)
    # TheSuperHackers: PGO needs /GL + /LTCG, which cannot be combined with the /fp:strict
    # simulation (see the /GL note above) because the linker would then re-optimize across
    # translation units and may inline /fp:fast render code into the deterministic simulation.
    # Fail loudly instead of silently producing a non-deterministic build.
    if(RTS_BUILD_OPTION_SIM_FP_STRICT)
        message(FATAL_ERROR
            "RTS_BUILD_OPTION_PGO=${RTS_BUILD_OPTION_PGO} requires /GL + /LTCG, which conflicts "
            "with RTS_BUILD_OPTION_SIM_FP_STRICT. Set RTS_BUILD_OPTION_PGO=OFF or "
            "RTS_BUILD_OPTION_SIM_FP_STRICT=OFF.")
    endif()
    # see the /GL note above: render targets with RTS_NO_PGO stay out of PGO entirely
    target_compile_options(core_config INTERFACE
        $<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:$<TARGET_PROPERTY:RTS_NO_PGO>>>>:/GL>)
    target_link_options(core_config INTERFACE $<$<CONFIG:Release>:/LTCG>)
    if("${RTS_BUILD_OPTION_PGO}" STREQUAL "GENERATE")
        target_link_options(core_config INTERFACE
            $<$<CONFIG:Release>:/GENPROFILE:PGD=${RTS_PGO_FILE}>
        )
    else()
        target_link_options(core_config INTERFACE
            $<$<CONFIG:Release>:/USEPROFILE:PGD=${RTS_PGO_FILE}>
        )
    endif()
endif()
add_feature_info(Vc6FullDebug RTS_BUILD_OPTION_VC6_FULL_DEBUG "Building VC6 with full debug info")
add_feature_info(FFmpegSupport RTS_BUILD_OPTION_FFMPEG "Building with FFmpeg support")

set(RTS_BUILD_OUTPUT_SUFFIX "" CACHE STRING "Suffix appended to output names of installable targets")

if(RTS_BUILD_ZEROHOUR)
    option(RTS_BUILD_ZEROHOUR_TOOLS "Build tools for Zero Hour" ON)
    option(RTS_BUILD_ZEROHOUR_EXTRAS "Build extra tools/tests for Zero Hour" OFF)
    option(RTS_BUILD_ZEROHOUR_DOCS "Build documentation for Zero Hour" OFF)

    add_feature_info(ZeroHourTools RTS_BUILD_ZEROHOUR_TOOLS "Build Zero Hour Mod Tools")
    add_feature_info(ZeroHourExtras RTS_BUILD_ZEROHOUR_EXTRAS "Build Zero Hour Extra Tools/Tests")
    add_feature_info(ZeroHourDocs RTS_BUILD_ZEROHOUR_DOCS "Build Zero Hour Documentation")
endif()

if(RTS_BUILD_GENERALS)
    option(RTS_BUILD_GENERALS_TOOLS "Build tools for Generals" ON)
    option(RTS_BUILD_GENERALS_EXTRAS "Build extra tools/tests for Generals" OFF)
    option(RTS_BUILD_GENERALS_DOCS "Build documentation for Generals" OFF)

    add_feature_info(GeneralsTools RTS_BUILD_GENERALS_TOOLS "Build Generals Mod Tools")
    add_feature_info(GeneralsExtras RTS_BUILD_GENERALS_EXTRAS "Build Generals Extra Tools/Tests")
    add_feature_info(GeneralsDocs RTS_BUILD_GENERALS_DOCS "Build Generals Documentation")
endif()

if(NOT IS_VS6_BUILD)
    # Because we set CMAKE_CXX_STANDARD_REQUIRED and CMAKE_CXX_EXTENSIONS in the compilers.cmake this should be enforced.
    target_compile_features(core_config INTERFACE cxx_std_20)
endif()

if(IS_VS6_BUILD AND RTS_BUILD_OPTION_VC6_FULL_DEBUG)
    target_compile_options(core_config INTERFACE ${RTS_FLAGS} /Zi)
else()
    target_compile_options(core_config INTERFACE ${RTS_FLAGS})
endif()

# This disables a lot of warnings steering developers to use windows only functions/function names.
if(MSVC)
    target_compile_definitions(core_config INTERFACE _CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS $<$<CONFIG:DEBUG>:_DEBUG_CRT>)
endif()

if(UNIX)
    target_compile_definitions(core_config INTERFACE _UNIX)
endif()

if(RTS_BUILD_OPTION_DEBUG)
    target_compile_definitions(core_config INTERFACE RTS_DEBUG WWDEBUG DEBUG)
else()
    target_compile_definitions(core_config INTERFACE RTS_RELEASE NDEBUG)
endif()

if(RTS_BUILD_OPTION_PROFILE)
    target_compile_definitions(core_config INTERFACE RTS_PROFILE_LEGACY)
endif()

# Define a dummy Tracy target when the build option is disabled.
if(RTS_BUILD_OPTION_PROFILE_TRACY)
    include(cmake/tracy.cmake)
else()
    add_library(core_profile_tracy INTERFACE)
endif()
