cmake_minimum_required(VERSION 3.16)

project(ArcologyShell VERSION 0.1.0 LANGUAGES NONE)

# arcosh is authored in ArcoBASIC itself (RFC-0052 section 6, arcosh/rfcs/RFC-0052_The_Arcology_Shell.md)
# and compiled through ArcoFission from the umbrella ArcoBASIC project, not hand-written C++. This
# project is intentionally not add_subdirectory()'d into the root build (see arcosh/README.md), so
# it locates an already-built ArcoFission executable instead of building a compiler itself.
find_program(ARCOFISSION_EXECUTABLE
    NAMES ArcoFission
    HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../build
    DOC "Path to the ArcoFission compiler executable from the umbrella ArcoBASIC build tree"
)

if(NOT ARCOFISSION_EXECUTABLE)
    message(FATAL_ERROR
        "ArcoFission executable not found. Build the umbrella project first:\n"
        "  cmake -S .. -B ../build && cmake --build ../build --target ArcoFission\n"
        "or pass -DARCOFISSION_EXECUTABLE=/path/to/ArcoFission."
    )
endif()

# arcosh compiles to real native x86-64 machine code (RFC-0052 section 6's own build-path
# requirement -- see arcology-os/rfcs/RFC-0053_Native_Hosted_ArcoBASIC_Compilation_and_System_Runtime.md),
# not the bytecode-VM capsule format ArcoFission produces by default. This needed one real
# codegen bug fixed first (a loop with 2+ host-function calls per iteration corrupting the next
# iteration's first call -- see .agents/ARCO_NATIVE_RUNTIME_PROGRESS.md Entry 25/Bug C) since
# arcosh's own interactive loop is exactly that shape; confirmed fixed and verified against this
# exact source (--version/--diagnostic/--selftest-paths/interactive pwd+cd+EOF all match the
# capsule build) before switching this over.
get_filename_component(ARCOFISSION_BUILD_DIR ${ARCOFISSION_EXECUTABLE} DIRECTORY)

set(ARCOSH_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/arcosh.abas)
set(ARCOSH_BINARY ${CMAKE_CURRENT_BINARY_DIR}/arcosh)

add_custom_command(
    OUTPUT ${ARCOSH_BINARY}
    # arcosh.abas calls host functions (Console.*, Path.*, String.*, Array.*, Directory.Change),
    # which this backend only reaches through its generic host-function bridge -- an opt-in,
    # EXCLUDE_FROM_ALL probe in the umbrella build (ArcoNativeRuntimeCoreProbe) that isn't built by
    # a plain `cmake --build` there, so it's built here on demand rather than documenting it as a
    # manual prerequisite step someone has to remember (the same problem the umbrella project's own
    # linux_native_backend_smoke CTest fixture solves for its own test run).
    COMMAND ${CMAKE_COMMAND} --build ${ARCOFISSION_BUILD_DIR} --target ArcoNativeRuntimeCoreProbe
    COMMAND ${ARCOFISSION_EXECUTABLE} build ${ARCOSH_SOURCE} -o ${ARCOSH_BINARY} --target linux-x86_64
    DEPENDS ${ARCOSH_SOURCE}
    COMMENT "Compiling arcosh from ArcoBASIC source with ArcoFission (native x86-64)"
    VERBATIM
)
# Named arcosh_capsule rather than "arcosh" -- a same-named custom target and output file in one
# directory make the Unix Makefiles generator treat it as a circular self-dependency and drop it.
add_custom_target(arcosh_capsule ALL DEPENDS ${ARCOSH_BINARY})

include(CTest)
if(BUILD_TESTING)
    add_test(NAME arcosh_version_smoke COMMAND ${ARCOSH_BINARY} --version)
    add_test(NAME arcosh_diagnostic_smoke COMMAND ${ARCOSH_BINARY} --diagnostic)
    # RFC-0052 AP-0052-005 (WP-002): root, trailing colon, spaces, quoted paths, file paths, dot
    # components, Linux path passthrough, and malformed paths -- see RunPathLayerSelfTest in
    # src/arcosh.abas. --selftest-paths exits nonzero if any assertion fails.
    add_test(NAME arcosh_path_layer_selftest COMMAND ${ARCOSH_BINARY} --selftest-paths)
    # RFC-0052 AP-0052-008 (WP-005): the required "gti ... / oops git" regression -- only the
    # executable field changes, a quoted argument containing the failed executable's own name is
    # untouched -- see RunOopsSelfTest in src/arcosh.abas.
    add_test(NAME arcosh_oops_selftest COMMAND ${ARCOSH_BINARY} --selftest-oops)
    # RFC-0052 AP-0052-009 (WP-006): insert/replace/delete-by-bare-number stay numerically ordered,
    # and a real RUN via Runtime.RunString executes a GOTO loop correctly -- see
    # RunResidentProgramSelfTest in src/arcosh.abas.
    add_test(NAME arcosh_resident_program_selftest COMMAND ${ARCOSH_BINARY} --selftest-resident)
    # RFC-0052 AP-0052-010 (WP-007): `RUN <file>` on real, checked-in fixture files (unnumbered,
    # classic numbered GOTO, and mixed source) proves the canonical compiler/parser path handles
    # all three with zero preprocessing -- see RunScriptExecutionSelfTest in src/arcosh.abas and
    # tests/fixtures/wp007_*.abas. Needs the fixtures directory as an argument, unlike every other
    # --selftest-* flag, since these fixtures are real files rather than synthesized in-memory.
    add_test(NAME arcosh_script_execution_selftest
        COMMAND ${ARCOSH_BINARY} --selftest-script-execution ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)
    # RFC-0052 AP-0052-014 (WP-011): real pipelines and redirection against real Linux programs
    # (cat/grep/wc), verified by reading back the actual files/output they produced -- AP-0052-014's
    # own explicit rule ("Tests MUST inspect actual execution/output behavior. Parser-only tests are
    # insufficient.") -- see RunPipelineSelfTest in src/arcosh.abas.
    add_test(NAME arcosh_pipeline_selftest COMMAND ${ARCOSH_BINARY} --selftest-pipeline)
    # RFC-0052 AP-0052-009 (WP-006): "immediate ArcoBASIC input where supported" -- classification
    # (a PRINT/assignment line is ArcoBASIC, a bare external command is not) plus real execution
    # with variable state persisting across two SEPARATE Runtime.EvalImmediate calls -- see
    # RunImmediateInputSelfTest in src/arcosh.abas.
    add_test(NAME arcosh_immediate_input_selftest COMMAND ${ARCOSH_BINARY} --selftest-immediate)
    # RFC-0052 AP-0052-015 (WP-012): real POSIX job control against real processes and real
    # signals (background jobs polled to completion, an external `kill -STOP`/SIGCONT exercising
    # the exact same WUNTRACED stop-detection code path a real Ctrl-Z would, `bg` resuming a
    # genuinely stopped job) -- ctest has no controlling terminal, so a real Ctrl-Z itself is
    # verified manually against the compiled binary under a real pty instead (see arcosh/README.md
    # and .agents/reports/ARCO_SH_RFC0052_WP012_JOB_CONTROL_RESEARCH.md) -- see
    # RunJobControlSelfTest in src/arcosh.abas.
    add_test(NAME arcosh_job_control_selftest COMMAND ${ARCOSH_BINARY} --selftest-jobcontrol)
    # RFC-0052 AP-0052-011 (WP-008): semantic styling roles, TrueColor/256/16/None tier detection
    # and fallback, and the one bundled theme -- see RunDisplaySelfTest in src/arcosh.abas.
    add_test(NAME arcosh_display_selftest COMMAND ${ARCOSH_BINARY} --selftest-display)
endif()
