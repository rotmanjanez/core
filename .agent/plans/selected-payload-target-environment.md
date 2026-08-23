# Make target compilation consume one typed environment

This ExecPlan is a living document. The sections `Progress`,
`Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must
be kept up to date as work proceeds.

This ExecPlan must be maintained in accordance with `.agent/PLANS.md` from the
repository root.

## Purpose / Big Picture

Target compilation must know both the hardware target and the exact payload
contract selected for execution. After this change, callers provide those two
context-free values together. The compiler records them as one typed
`mqt.target_env` module attribute before it runs target passes. Mapping, native
synthesis, and conformance then reconstruct the hardware target from that
attribute. This makes the IR the single source of target facts and makes the
passes usable from MLIR textual pass pipelines.

The behavior is visible by compiling a QCO program for an explicit target and
payload specification, printing the result, and observing a complete typed
`mqt.target_env` attribute. Running a target pass without that attribute must
fail with a clear diagnostic.

## Progress

- [x] (2026-08-23 18:21Z) Inspected the target compilation APIs, Python
  bindings, command-line driver, typed MQT attributes, and the mapping,
  synthesis, and conformance passes.
- [x] (2026-08-23 18:21Z) Chose a context-free payload value model and one
      canonical module attribute instead of payload facts inside
      `CompilerTarget`.
- [x] (2026-08-23 19:06Z) Implemented the payload specification and
  module-attribute bridge.
- [x] (2026-08-23 19:06Z) Made target compilation attach the environment and
  made target passes consume it without captured caller state.
- [x] (2026-08-23 19:06Z) Updated C++, Python, and command-line interfaces and
  focused tests.
- [x] (2026-08-23 19:06Z) Built focused targets and MLIR documentation,
  regenerated stubs, and ran focused C++, Python, command-line, clang-tidy,
  and repository lint checks.
- [x] (2026-08-23 20:30Z) Replaced independently supplied C++ target and payload
      arguments with one `TargetEnvironment` argument. The targeted overload
      derives its output from the selected payload specification.
- [x] (2026-08-24 13:13Z) Added coverage for derived target output,
      non-destructive rejection of unsupported payload formats, and cached
      environment invalidation after `mqt.target_env` changes.

## Surprises & Discoveries

- Observation: `place-and-route` is already a generated, registered MLIR pass,
  but target-native synthesis and target conformance use custom pass wrappers
  with target-taking constructors. Evidence: `MappingPass` is declared in
  `mlir/include/mlir/Dialect/QCO/Transforms/Passes.td`; the other two factories
  take `CompilerTarget` in `Passes.h`.

- Observation: the command-line target path currently gets only a hardware
  snapshot from QDMI. It cannot select an exact payload without inventing facts.
  Evidence: `mqt-cc --qdmi-device` calls `compilerTargetFromDeviceId` and then
  the target pipeline with no payload argument.

- Observation: Python exposed a validated payload format by `const&`, but its
  record fields remain writable. Mutating the returned Python object changed the
  internal format. Returning the small record by value preserves the immutable
  payload contract, and the regression now exercises that copy.

- Observation: the repository already has a CMake subprocess test for `mqt-cc`
  QIR output. Extending that test covered option-pair rejection, attribute
  parsing, and valid target compilation without adding a new driver test
  framework.

- Observation: the generic spelling `all-to-all` did not reparse as an MLIR
  enum. The two-line `all_to_all` spelling fix was folded into the exact base
  commit `99f27868e75ee3e82a22aea9cc2883d2d877602b`; this slice has no duplicate
  diff.

- Observation: a repeated `tests-3.14` nox invocation tried to rebuild the
  editable package after a test-only edit, but that invocation did not inherit
  `MLIR_DIR` and failed during CMake configuration. The already installed test
  environment then ran the same two Qiskit tests directly, and both passed.

## Decision Log

- Decision: Keep `PayloadSpecification` in the new
  `mlir/Compiler/TargetEnvironment.h` header, separate from both
  `CompilerTarget` and the pass-pipeline header. Rationale: payload support is
  selected per submitted payload, not an intrinsic property of the hardware or a
  pass implementation. Date/Author: 2026-08-23 / Codex.

- Decision: Use extensible string IDs and unsigned values for capabilities and
  constraints, matching the typed MQT attributes. Do not add a closed feature
  enum or convenience support predicates. Rationale: providers and future QDMI
  versions must round-trip new capabilities without a Core release. Date/Author:
  2026-08-23 / Codex.

- Decision: Attach `mqt.target_env` before constructing the target pass manager,
  then make every target-dependent pass reconstruct `CompilerTarget` at
  `runOnOperation`. Rationale: captured C++ targets create a second source of
  truth and prevent reproducible textual pipelines. Date/Author: 2026-08-23 /
  Codex.

- Decision: Require one typed `#mqt.payload_spec` command-line value with
  `--qdmi-device`. Rationale: MLIR already supplies a complete syntax and
  parser; a second command-line capability grammar or output-format inference
  would add ambiguity and code. Date/Author: 2026-08-23 / Codex.

- Decision: Remove the obsolete hand-written mapping factory header and use
  generated pass declarations for all three target passes. Rationale: one
  generated pass contract makes the factories default-constructible and keeps
  textual pass pipelines reproducible. Date/Author: 2026-08-23 / Codex.

- Decision: Give `runDefaultPipeline` separate untargeted and targeted C++
  overloads. The targeted overload takes one `TargetEnvironment` and derives the
  output from its payload specification. Rationale: the type system excludes
  incomplete or contradictory target contracts. Date/Author: 2026-08-23 / Codex.

- Decision: Cache the validated environment as `TargetEnvironmentAnalysis` and
  invalidate it when `mqt.target_env` changes. Rationale: all target passes can
  share one reconstruction without retaining stale target facts. Date/Author:
  2026-08-24 / Codex.

## Outcomes & Retrospective

The implementation is published as PR #2219. Target compilation now records one
exact hardware and payload pair as `mqt.target_env`. Mapping, native synthesis,
and conformance reconstruct their hardware view from that attribute, and all
three run through registered textual pass syntax.

Validation passed for the release build and `mlir-doc`; 137 compiler tests, 84
mapping tests, 25 native-synthesis tests, 16 MQT IR tests, and the existing
`mqt-cc` CMake test; stub generation; three focused compiler Python tests, the
format-copy regression, the QDMI QIR test, and two updated Qiskit tests; and the
complete `uvx nox -s lint` session. A changed-line clang-tidy run on the new
`TargetEnvironment.cpp` and its public header passed after adding direct
includes and initializing the encoding value. An attempted broader manual
clang-tidy run was not valid evidence because its fresh lint tree lacked
generated headers and reported unrelated existing diagnostics; it was not used
as an acceptance result. A later multi-file changed-line run was stopped at the
requested handoff boundary.

## Context and Orientation

`mlir/include/mlir/Compiler/Target.h` and `mlir/lib/Compiler/Target.cpp` define
the immutable, context-free hardware model. The model can already convert to and
from `mqt::CompilationTargetAttr`. The MQT dialect definitions in
`mlir/include/mlir/Dialect/MQT/IR/MQTDialect.td` also define `PayloadSpecAttr`
and the combined `TargetEnvAttr`.

`mlir/lib/Compiler/Programs.cpp` owns typed compiler programs. Its
`QCOProgram::compileForTarget` method runs the pipeline declared in
`mlir/include/mlir/Compiler/TargetCompilation.h`. The method now attaches the
typed environment before it runs the canonical target pipeline. Mapping, native
synthesis, and conformance read the environment from the module. Those passes
live under `mlir/lib/Dialect/QCO/Transforms/`.

The Python API is bound in `bindings/mlir/register_mlir.cpp`, with handwritten
stub corrections in `bindings/patterns.txt`. The `mqt-cc` driver lives in
`mlir/tools/mqt-cc/mqt-cc.cpp`. QDMI remains responsible only for producing a
hardware `CompilerTarget` in this task; this task must not depend on unreleased
QDMI payload-capability APIs.

## Plan of Work

Add `TargetEnvironment.h` and `TargetEnvironment.cpp`. Define plain format,
capability, and constraint records plus a validated `PayloadSpecification` that
owns strings and vectors without an `MLIRContext`. Add conversion from and to
`mqt::PayloadSpecAttr`. Add narrow helpers that attach the combined typed target
environment to a module. Define a context-free `TargetEnvironment` that owns a
validated `CompilerTarget` and `PayloadSpecification`, and a cached analysis
that reconstructs that value from the module attribute. Keep validation
identical to the attribute contract: canonical semantic versions, valid
encoding, nonempty and null-free IDs, unique constraint IDs per capability, and
unique capability ID/value pairs.

Change `QCOProgram::compileForTarget` to require one `TargetEnvironment`. Attach
the module attribute before calling `runPasses` so the documented in-place
failure contract remains unchanged. Give `runDefaultPipeline` separate
untargeted and targeted overloads. The targeted overload takes the same value,
derives the output from its payload specification, and uses the canonical QCO
pipeline. Make `populateTargetCompilationPipeline` take only a pass manager.

Change mapping, native synthesis, and conformance to read the canonical module
attribute during `runOnOperation`. Remove target-taking constructors, factories,
and stored caller state. Declare native synthesis and conformance in `Passes.td`
so registration gives them textual pipeline names. Preserve all current
algorithms and diagnostics after target reconstruction.

Expose `PayloadSpecification` and `TargetEnvironment` in Python. Give
`compile_program` separate untargeted and targeted overloads. In the
command-line driver, parse a single `#mqt.payload_spec` attribute with MLIR's
existing parser, require it with `--qdmi-device`, attach the combined
environment, and run the context-free pipeline. Update the target-compilation
documentation and fold the new behavior into the existing Compiler Collection
changelog entry.

Update focused tests. Compiler tests must cover validation, exact attribute
materialization, one-argument target compilation, derived output, rejection of
an unsupported payload format without consuming the input, analysis-cache
invalidation, and end-to-end retention. Mapping and synthesis tests must attach
environments before running default-constructible passes and must cover the
missing-attribute diagnostic. A textual pipeline test must prove that each
target pass can be parsed and run from registered pass syntax.

## Concrete Steps

From the repository root, edit only the files named above and the corresponding
CMake and test files. Build and test in this order:

    cmake --build --preset release --target mqt-core-mlir-unittests-compiler mqt-core-mlir-unittests-qco mqt-cc mlir-doc
    ./build/release/mlir/unittests/Compiler/mqt-core-mlir-unittests-compiler
    ctest --test-dir build/release -L mqt-mlir-unittests --output-on-failure
    uvx nox -s stubs
    uv run --no-sync pytest test/python -k 'compiler or qdmi'
    uvx nox -s lint

If the broad MLIR label includes unrelated slow suites, run the exact mapping
and native-synthesis test binaries named by CTest first, then record the broader
result separately.

## Validation and Acceptance

Acceptance requires all focused C++ tests to pass. A compiled target program
must contain one `mqt.target_env` whose compilation-target and payload fields
equal the supplied context-free values. `place-and-route`,
`target-native-synthesis`, and `verify-target-conformance` must run from
default-constructed factories and registered textual syntax when that attribute
exists. Each must fail cleanly, without a fatal usage error, when it does not.

The Python binding must accept one `target_environment` argument and derive the
result format from its payload specification. The command-line driver must
reject `--qdmi-device` without `--payload-spec`, parse the exact typed attribute
when supplied, and reject a separate `--emit` selection. Generated stubs and
MLIR documentation must match the new public signatures. The final diff must
pass `git diff --check`, focused clang-tidy, and `uvx nox -s lint`.

## Idempotence and Recovery

All build and test commands are repeatable. The new work is isolated from other
tasks. If generated files or formatting change unexpectedly, inspect them before
keeping them and never discard unrelated changes. No remote state changes are
authorized by this plan.

## Artifacts and Notes

The typed attribute shape already exists:

    #mqt.target_env<
      compilation_target = #mqt.compilation_target<...>,
      payload_specification = #mqt.payload_spec<...>>

This task supplies the context-free payload half and makes that attribute the
only input read by target-dependent passes.

## Interfaces and Dependencies

The final public interfaces must include a validated
`mlir::PayloadSpecification` in `mlir/Compiler/TargetEnvironment.h`, conversion
to and from `mlir::mqt::PayloadSpecAttr`, a context-free
`mlir::TargetEnvironment`, a cached `mlir::TargetEnvironmentAnalysis`, and a
module attach helper. `QCOProgram::compileForTarget` must require
`(const TargetEnvironment&, bool, bool)`. `populateTargetCompilationPipeline`
must require only `OpPassManager&`. `qco::createMappingPass` may accept mapping
options but no target. `qco::createTargetNativeSynthesis` and
`qco::createVerifyTargetConformance` must take no arguments.

Use only LLVM, MLIR, and existing repository libraries. Do not add a dependency,
QDMI payload API, closed feature enum, capability inference from a requested
output format, program clone, or speculative transformation.
