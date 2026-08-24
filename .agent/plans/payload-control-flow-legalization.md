# Legalize control flow for the selected payload

This ExecPlan is a living document. The sections `Progress`,
`Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must
be kept up to date as work proceeds.

This ExecPlan must be maintained in accordance with `.agent/PLANS.md` from the
repository root.

## Purpose / Big Picture

Target compilation already records an exact payload contract in the typed
`mqt.target_env` module attribute. After this change, the compiler can use that
contract to retain supported structured control flow, remove or lower control
flow that the payload does not support, and reject programs for which no
implemented legalization exists. A user can observe the behavior by compiling
QCO with different payload capability lists: a static unsupported counted loop
is unrolled, a multiway branch is lowered to nested forward branches when
possible, and an unsupported dynamic loop fails at the illegal control
operation.

The change deliberately covers only structural control flow. It does not analyze
scalar computations, measurement provenance, functions, dynamic allocation, or
final QIR profile requirements. Those concerns need separate lowering-stage
analyses because the current QCO types do not contain all facts, such as the
concrete width of MLIR's `index` type.

## Progress

- [x] (2026-08-23 19:11Z) Created an isolated worktree from the exact
      selected-payload foundation and inspected its target-environment and
      compiler-pipeline contracts.
- [x] (2026-08-23 19:11Z) Traced existing QCO and SCF control operations,
      cleanup passes, mapping support, QIR lowering, and the reusable
      control-flow work from the earlier feature branch.
- [x] (2026-08-23 22:29Z) Added the two payload-control passes and the stock
      MLIR normalization and conversion pipeline.
- [x] (2026-08-23 22:29Z) Added ten focused compiler tests for normalization,
      bounded fixed-point unrolling, both switch dialects, typed constraints,
      linear captures, and failure paths.
- [x] (2026-08-23 22:29Z) Updated target-compilation documentation and folded
      [#2162] into the Compiler Collection launch entry.
- [x] (2026-08-23 23:02Z) Reached 90.5% line coverage in the new pass source and
      passed repository lint, a strict non-unity Clang 22 build, and focused
      clang-tidy checks.
- [x] (2026-08-24 13:45Z) Reapplied the control-flow-only change to the revised
      selected-payload foundation, adapted it to `PayloadSpecification` and
      `TargetEnvironmentAnalysis`, and validated the exact ten-file scope.

## Surprises & Discoveries

- Observation: loop unrolling and final dialect conversion cannot be one pass
  without losing valid programs. Full unrolling can expose branch conditions
  that become constant only after sparse constant propagation and
  canonicalization. Evidence: `scf::loopUnrollFull` clones the body but does not
  invoke the QCO branch canonicalizers.
- Observation: the existing mapping implementation models `scf.for`,
  `scf.while`, `qco.if`, and `qco.index_switch` as quantum control constructs.
  It does not model SCF branches that capture linear QCO values. The legality
  pass must reject that noncanonical representation instead of allowing a later
  mapping assertion.
- Observation: the first implementation repeated every dynamic-legality query in
  192 lines of custom diagnostics. MLIR already reports the exact illegal
  operation. Removing the mirror cut 242 production lines without changing
  legality or rewrites.
- Observation: QTensor register shrinking asserted when an earlier pipeline
  stage contained a nonlinear tensor value. The shared transform now skips such
  values in the independent Core PR #2220.
- Observation: generic SCF loops can capture a qubit without threading its
  updated value. Target compilation previously accepted that invalid repeated
  use. Both loop forms now reject linear captures; valid linear state must use
  loop iteration arguments.
- Observation: MLIR 22 warns that its index trip-count calculation can overflow.
  A signed minimum-to-maximum range is reported as zero and generic
  canonicalization then deletes the loop. The unroll pass now compares MLIR's
  result with widened exact arithmetic before the first generic cleanup.

## Decision Log

- Decision: implement one feature as two named module passes in one new source
  file. The first pass unrolls unsupported static counted loops. The second pass
  applies MLIR dialect conversion to residual control flow. Rationale: stock
  cleanup must run between the transformations, while one source keeps the
  implementation local. Date/Author: 2026-08-23 / Codex.
- Decision: use `createSymbolDCEPass`, `createLiftControlFlowToSCFPass`,
  `createSCCPPass`, the existing QCO cleanup pipeline, `scf::loopUnrollFull`,
  `ConversionTarget`, and `applyPartialConversion`. Rationale: these LLVM and
  MLIR components already own symbol reachability, CFG restructuring, constant
  propagation, counted-loop expansion, and operation legality. Date/Author:
  2026-08-23 / Codex.
- Decision: read the context-free payload specification through the cached
  `TargetEnvironmentAnalysis` and recognize four Boolean capability IDs plus
  three control constraints locally. Rationale: the selected payload is
  authoritative, the representation is intentionally extensible, and one
  consumer does not justify a public capability registry or interface.
  Date/Author: 2026-08-23 / Codex.
- Decision: preserve the in-place failure contract. Rationale: target
  compilation explicitly permits earlier pass changes to remain after a later
  failure, so this work must not clone the module or test transactional
  behavior. Date/Author: 2026-08-23 / Codex.
- Decision: rely on dialect conversion's native illegal-operation diagnostic.
  Rationale: a second legality walk duplicated MLIR and made diagnostics part of
  an unsupported contract. The pass keeps custom errors only for unroll safety
  limits and invalid linear loop captures. Date/Author: 2026-08-23 / Codex.
- Decision: run the exact loop-range guard before generic canonicalization and
  apply only branch canonicalization patterns inside that pass. Rationale: this
  avoids MLIR's documented index-overflow case without analyzing loops in dead
  branches or replacing the standard cleanup pipeline. Date/Author: 2026-08-23 /
  Codex.

## Outcomes & Retrospective

The reduced implementation and local validation are complete. The release
compiler suite passes all 147 tests, the MLIR documentation target builds, and
repository lint passes. The production source remains 166 lines smaller than the
first working version. The original implementation reached 90.5% line coverage
and passed a strict non-unity Clang 22 build, focused clang-tidy, and both
QTensor transform tests. Scalar and provenance requirements remain intentionally
outside this plan.

## Context and Orientation

`mlir/include/mlir/Compiler/TargetEnvironment.h` defines the context-free
`PayloadSpecification`, `TargetEnvironment`, and `TargetEnvironmentAnalysis`.
`mlir/lib/Compiler/Programs.cpp` materializes a selected compiler target and
payload specification as the typed `mqt.target_env` attribute before it runs
`populateTargetCompilationPipeline`. The payload specification contains an exact
format, a list of capability groups, and a bit that records whether all optional
capability metadata is known. Producers must include format baselines in the
capability list; compiler passes must not infer support from a format name.

`mlir/lib/Compiler/TargetCompilation.cpp` currently runs QCO cleanup,
decomposition, optimization, mapping, native synthesis, and target conformance.
`mlir/lib/Support/Passes.cpp` defines `populateQCOCleanupPipeline`, which runs
an unbounded greedy canonicalizer, global-phase normalization,
common-subexpression elimination, tensor shrinking, and dead-value removal.

QCO uses `qco.if` and `qco.index_switch` to carry qubits and quantum tensors as
explicit linear inputs and results across mutually exclusive regions. Counted
and conditional loops use `scf.for` and `scf.while`. Purely classical branches
can use the SCF forms. A conversion target is an MLIR object that declares
whether each operation is legal and supplies rewrite patterns for illegal
operations. A partial conversion requires explicitly illegal operations to be
rewritten while leaving unrelated dialects unchanged.

The payload control features are `forward-branching`, `counted-iteration`,
`conditional-loop`, and `multiway-branching`. Each is Boolean and therefore uses
value zero. The recognized constraints are `max-control-flow-nesting-depth`,
`max-iteration-count`, and `max-case-count`. The maximum nesting depth counts
lexical branch and loop constructs, with the outermost construct at depth one.
The iteration maximum is inclusive and requires a proved upper bound. The case
maximum counts explicit switch cases and excludes the default region. An
unknown, zero-valued, or misapplied constraint makes only its capability group
unusable.

The work is confined to compiler and QCO MLIR files, one existing compiler test
binary, the target-compilation documentation, the changelog, and this plan. It
does not modify QDMI, Python bindings, generated stubs, Qiskit or PennyLane
plugins, or QIR conversion code. Repository `AGENTS.md`, `docs/ai_usage.md`, and
generated-file rules remain in force.

## Plan of Work

Add `UnrollUnsupportedPayloadLoops` and `LegalizePayloadControlFlow` to
`mlir/include/mlir/Dialect/QCO/Transforms/Passes.td`. Implement both in
`mlir/lib/Dialect/QCO/Transforms/LegalizePayloadControlFlow.cpp`. The source
will read the four known capability groups through `TargetEnvironmentAnalysis`,
retain an exact Boolean group only when its value is zero, and expose small
local queries for depth, trip-count, and case-count coverage.

The unroll pass will collect the outermost unsupported static `scf.for`
operations, unroll them, fold the result, and rescan. Processing unsupported
ancestors first avoids spending the clone budget on a nested loop that an outer
rewrite will replace. Dynamic loops remain for the final legality pass. The pass
will use `scf::loopUnrollFull` and a fixed pass-wide ceiling of 65,536 cloned
body operations. It will compare arbitrary-width `APInt` trip counts before
converting them to `uint64_t` so an oversized value cannot be silently
saturated.

The final pass will use `ConversionTarget` with dynamic legality for `qco.if`,
`qco.index_switch`, `scf.if`, `scf.index_switch`, `scf.for`, and `scf.while`. It
will lower a default-only switch by inlining the default region. It will lower a
one-case switch to one `if`. It will retain a multiway switch when the multiway
capability covers its case count and depth; otherwise it will lower the switch
to nested `if` operations if forward branching is usable. Generated branches are
checked by the same conversion target, so their increased nesting must also fit
the forward-branch constraint. QCO rewrites preserve classical results followed
by all linear results. An equivalent SCF rewrite handles purely classical
switches.

Before these passes, target compilation will remove unused private symbols, lift
reducible ControlFlow dialect CFGs to SCF, and propagate constants. The unroll
pass will fold static branches, compare directly constant ranges with widened
arithmetic, and reject a mismatch with MLIR's native trip count before generic
loop canonicalization can change the program. Target compilation will repeat
propagation and run QCO cleanup after loop unrolling, then run cleanup after
dialect conversion. Any residual ControlFlow branch or SCF region-branch form
outside the modeled set will fail. QCO modifier regions are not execution
control flow and remain legal.

Update `mlir/lib/Dialect/QCO/Transforms/CMakeLists.txt` and
`mlir/lib/Compiler/CMakeLists.txt` with the direct ControlFlow and conversion
dependencies. Register both passes in `mlir/lib/Support/Passes.cpp` so their
textual forms work. Extend `docs/mlir/target_compilation.md` with the actual
ordering and constraint behavior. Add the eventual pull request reference to the
existing Compiler Collection launch entry in `CHANGELOG.md`; do not create a
separate entry.

Add focused tests to `mlir/unittests/Compiler/test_compiler_pipeline.cpp`. Use
the existing compiler fixture and payload specification API rather than adding a
new executable. Test missing environment diagnostics, constant-control cleanup,
static and dynamic counted loops, the unroll budget, QCO switch conversion with
classical and linear results, classical SCF switch conversion, noncanonical
linear SCF branch rejection, constraint boundaries, fail-closed constraint
groups, and reducible CFG lifting. Do not assert that a failed compilation
preserves the input.

## Concrete Steps

All commands run from the repository root.

First, add the plan and implementation files with `apply_patch`. Format the
changed C++ with the repository hooks or the focused `clang-format` hook. Build
the focused compiler unit test with:

    cmake --preset release
    cmake --build --preset release --target mqt-core-mlir-unittests-compiler

Run the focused binary with filters while iterating:

    ./build/release/mlir/unittests/Compiler/mqt-core-mlir-unittests-compiler \
      --gtest_filter='CompilerPipelineTest.*PayloadControl*'

Run the complete compiler binary after focused tests pass:

    ./build/release/mlir/unittests/Compiler/mqt-core-mlir-unittests-compiler

Build the MLIR documentation because the target-compilation page changes:

    cmake --build --preset release --target mlir-doc

Finish with:

    uvx nox -s lint
    git diff --check
    git status --short

Record concise pass or failure transcripts in this plan as validation proceeds.
A failing hosted or unrelated test must be distinguished from a failure caused
by this change.

## Validation and Acceptance

The implementation is accepted when the focused compiler binary proves these
observable outcomes. With no control capability, constant branches disappear and
a finite static loop is expanded to straight-line QCO. A dynamic counted loop
fails at `scf.for`. With unconstrained counted support, the same loop remains. A
multiway QCO branch remains with usable multiway support, becomes nested
`qco.if` with forward-only support, and fails when neither feature is available.
Default-only switches inline their default region. Constraint values are
inclusive at their boundary; an unknown, misapplied, or zero constraint makes
its feature group unusable. A switch rewrite must preserve classical and linear
result types and leave verifiable MLIR.

The complete compiler test binary must pass. The MLIR documentation target and
`uvx nox -s lint` must pass, or this plan must record an exact environmental or
pre-existing limitation. The final diff must contain no generated files,
bindings, QDMI changes, or unrelated cleanup.

## Idempotence and Recovery

Configuration, builds, tests, documentation, and lint commands are repeatable.
The pass implementations mutate IR in place by contract. A failed test can
reparse its source to obtain a fresh program; tests must not reuse a program
after failure. If an edit introduces invalid generated pass declarations, rerun
the CMake build after correcting `Passes.td`; CMake regenerates the build tree.
Preserve unrelated work and use `apply_patch` for every source edit.

## Artifacts and Notes

The exact implementation base contains the selected-payload compiler API and
records `mqt.target_env` before target passes. Simon Hofmann's commits
`f484908e3` and `3b72e2191` established the feature goal and structural test
cases. The QCO index-switch rewrite was adapted from Lukas Burgholzer's
`ed80a41b2`. The final commit credits Simon as a human co-author because his
original implementation materially shaped the retained contract.

## Interfaces and Dependencies

At completion, `mlir::qco` must expose the TableGen-created factories:

    std::unique_ptr<Pass> createUnrollUnsupportedPayloadLoops();
    std::unique_ptr<Pass> createLegalizePayloadControlFlow();

Their textual names must be `unroll-unsupported-payload-loops` and
`legalize-payload-control-flow`. Both are module passes and use
`TargetEnvironmentAnalysis`. The QCO transforms library must link the compiler
target, SCF, arithmetic, and MLIR transform utilities it uses. The compiler
pipeline must link `MLIRControlFlowToSCF` for `createLiftControlFlowToSCFPass`.
No public capability enum, new target API, third-party dependency, or QDMI API
is part of this plan.

Revision note (2026-08-24 13:45Z): Reapplied the reduced implementation to the
revised selected-payload foundation. The passes now consume the cached,
context-free target environment through `TargetEnvironmentAnalysis`.
