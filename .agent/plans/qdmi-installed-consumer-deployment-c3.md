# Harden QDMI runtime staging for installed consumers

This ExecPlan is a living document. The sections `Progress`,
`Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must
be kept up to date as work proceeds.

This ExecPlan must be maintained in accordance with `.agent/PLANS.md` from the
repository root.

## Purpose / Big Picture

An application that consumes an installed MQT Core CMake package must receive
the same runnable QDMI layout as an in-tree application. After this change,
`mqt_copy_qdmi_runtime` copies the public Client library, the packaged Driver,
selected device libraries, declared runtime assets, generated manifests, and
their transitive Windows DLLs beside the application. The existing imported
device fixture demonstrates the complete layout without adding a second
`find_package` test project.

## Progress

- [x] (2026-08-24 17:28Z) Inspect the current helper, installed target model,
  and the existing imported-device fixture.
- [x] (2026-08-24 18:08Z) Make imported Client, Driver, and device targets
      participate in runtime staging, including `TARGET_RUNTIME_DLLS` on
      Windows.
- [x] (2026-08-24 18:29Z) Extend the existing fixture to install MQT Core,
      resolve its imported targets through `find_package`, build and run a
      public Client consumer, and verify libraries, Windows dependencies,
      assets, manifests, and build RPATH selection.
- [x] (2026-08-24 18:30Z) Install the public `ProgramFormat.hpp` dependency of
      `Client.hpp`, update the focused documentation, and record the final
      design.
- [x] (2026-08-24 18:32Z) Run documentation, lint, and final diff checks. The
      focused build, two fixture tests, all 98 Driver tests, documentation, and
      full lint pass. The full release build and 4,091 configured tests also
      pass, with one expected skip. Preserve one signed commit without
      publishing it.
- [x] (2026-08-24 20:19Z) Replay the single deployment commit onto exact C2 head
      `149665514d261d211adfd751627e5d2a147c656e`; preserve the separate QDMI
      cleanup entry; and pass both installed-consumer fixture tests, all 96
      Driver tests, and lint.
- [x] (2026-08-24 22:35Z) Route imported Windows runtime dependency discovery
      through a non-imported CMake closure target after exact hosted Windows ARM
      exposed the imported-target linker-language failure. Both fixture tests,
      all 97 Driver tests, lint, and simulated Windows generation with CMake
      3.28 and 4.4 pass. Prepare the replacement hosted Windows run.

## Surprises & Discoveries

- Observation: The helper already names both `MQT::CoreQDMI` and
  `MQT::CoreQDMIDriver`, but it skips both when an installed package exposes
  them as imported targets. Evidence: the runtime loop is guarded by
  `NOT runtime_imported`.
- Observation: Imported device libraries are copied, but their transitive
  Windows DLLs are skipped. Evidence: the device branch adds
  `TARGET_RUNTIME_DLLS` only when `NOT device_imported`.
- Observation: The installed `Client.hpp` includes `qdmi/ProgramFormat.hpp`, but
  the CoreQDMI public header file set omitted that file. Evidence: the first
  real installed-consumer build failed with
  `qdmi/ProgramFormat.hpp: No such file or directory`.
- Observation: A nested project that installs the built package and calls
  `find_package(mqt-core)` can reuse the existing imported-device fixture. The
  fixture does not need a second full package-consumer harness.
- Observation: `TARGET_RUNTIME_DLLS` cannot compute link information when its
  root is an imported shared target without a linker language. Evidence: exact
  hosted Windows ARM generation failed for the imported Client, Driver, and
  device targets. In the 4,086-test suite, only the fixture configure failed,
  and its dependent fixture build did not run.

## Decision Log

- Decision: Reuse `test/qdmi/driver/imported_device/CMakeLists.txt` as a real
  installed-package consumer. Stage the required QDMI and MQT Core install
  components into a fixture-local prefix and call `find_package(mqt-core)` from
  that prefix. Rationale: the fixture already tests exported device metadata and
  runtime assets, so a second package-consumer harness would duplicate setup
  without testing a different contract. Date/Author: 2026-08-24 / Codex.
- Decision: Stage the two Core runtime targets through per-consumer custom
  targets. Rationale: imported targets cannot be build dependencies, while an
  always-runnable `copy_if_different` target works for both imported and local
  targets and refreshes the runtime layout when only an imported file changes.
  Date/Author: 2026-08-24 / Codex.
- Decision: Keep device copying on the consumer's existing post-build command.
  Rationale: only the Windows dependency filter is wrong for imported devices;
  changing the established manifest and asset path adds no value. Date/Author:
  2026-08-24 / Codex.
- Decision: Add `ProgramFormat.hpp` to CoreQDMI's existing public header file
  set. Rationale: an installed consumer cannot include the public `Client.hpp`
  without this direct dependency. The one-line packaging fix preserves the real
  Session-based acceptance test. Date/Author: 2026-08-24 / Codex.
- Decision: Compute imported Windows runtime DLLs through one excluded module
  closure per imported root. Rationale: the closure supplies CMake with a real
  C++ linker language without linking or loading the runtime in the consumer.
  The imported test dependencies provide both DLL and import-library metadata.
  Date/Author: 2026-08-24 / Codex.
- Decision: Do not add a standalone changelog bullet before publication.
  Rationale: the work hardens unreleased v4 functionality. After a draft pull
  request receives a number, fold the deployment text and reference into the
  existing QDMI entry. Date/Author: 2026-08-24 / Codex.

## Outcomes & Retrospective

The implementation now stages local and imported QDMI runtime targets through
the same public helper. The real installed-package fixture builds and runs a
`qdmi::Session` consumer from `find_package(mqt-core)`, and it verifies the
complete colocated runtime layout. The work also installs the missing public
`ProgramFormat.hpp` dependency. It does not change QDMI Client or Driver C++
sources, bindings, the private extension, device payloads, or compiler control
flow. Focused native tests, the installed-consumer fixture, documentation, and
full lint pass. The exact replay passes both installed-consumer fixture tests
and all 97 Driver tests. The full release build and CTest suite passed on the
preceding signed checkpoint. Hosted Windows remains the decisive check for the
imported-target branch.

## Context and Orientation

`cmake/AddMQTQDMIDevice.cmake` is installed with the MQT Core CMake package. Its
`mqt_copy_qdmi_runtime` function places shared QDMI runtime files beside one
executable or library target. A CMake target is imported when it describes a
library built outside the current project, as all targets from an installed MQT
Core package do. `TARGET_RUNTIME_DLLS` is a CMake generator expression that
lists the transitive DLL dependencies of a target on Windows.

The helper handles three layers. `MQT::CoreQDMI` is the public Client wrapper.
`MQT::CoreQDMIDriver` is the optional packaged Driver. Device targets provide
native providers and carry `QDMI_DEVICE_ID`, `QDMI_DEVICE_PREFIX`,
`QDMI_MANIFEST_NAME`, and `QDMI_RUNTIME_FILES` target properties. A manifest is
a JSON file that lets the packaged Driver resolve a stable device ID to the
colocated provider library. A runtime asset is a provider-owned file, such as a
device model, named by `QDMI_RUNTIME_FILES`.

`test/qdmi/driver/CMakeLists.txt` exports a metadata-only device target and
configures `test/qdmi/driver/imported_device/CMakeLists.txt` as a nested CMake
fixture. The nested project installs the required package components into a
fixture-local prefix, resolves MQT Core from that prefix, and combines the real
imported Core targets with the exported metadata-only device target.

The helper sets `BUILD_WITH_INSTALL_RPATH` to false on its consumer. RPATH is
the Unix runtime search path embedded in a binary. This setting makes a build
use its build-tree search path instead of an unrelated final install path while
the staged libraries sit beside the consumer. The fixture must set the property
to true before calling the helper and verify that the helper resets it.

## Plan of Work

In `cmake/AddMQTQDMIDevice.cmake`, update the runtime loop for `MQT::CoreQDMI`
and `MQT::CoreQDMIDriver`. Create one deterministic copy target for each
consumer/runtime pair. Copy the target file and, on Windows, its
`TARGET_RUNTIME_DLLS`. Add a build dependency from the copy target to a local
runtime target, but do not add one to an imported target. Make the consumer
depend on the copy target. Keep aliases and a consumer that is itself the
runtime target safe.

In the same helper, include `TARGET_RUNTIME_DLLS` for every Windows device,
including imported devices. Preserve the existing local target dependency,
manifest generation, and `QDMI_RUNTIME_FILES` copying.

In `test/qdmi/driver/CMakeLists.txt`, pass the main build directory, build
configuration, a fixture-local install prefix, and the exported metadata-only
device target to the nested fixture. In
`test/qdmi/driver/imported_device/CMakeLists.txt`, install the QDMI and MQT Core
runtime and development components and resolve `mqt-core` only from that prefix.
Assert that `MQT::CoreQDMI` and `MQT::CoreQDMIDriver` are imported. Build and
run a small `qdmi::Session` consumer on Unix. On Windows, load the two staged
Core DLLs because the synthetic dependency targets do not provide valid import
libraries. Attach one synthetic imported DLL to each Core target and one to the
imported device. Compare the staged Core libraries, device library, asset, and
generated manifest with their sources. Compare the three synthetic DLLs on
Windows. Set `BUILD_WITH_INSTALL_RPATH` before the helper call and reject a
configuration in which the helper leaves it enabled.

In `src/qdmi/CMakeLists.txt`, include `ProgramFormat.hpp` in CoreQDMI's public
header file set so the installed `Client.hpp` remains self-contained.

Update `docs/qdmi/configuration.md` to state the installed-consumer behavior and
Windows dependency handling. Do not add a standalone changelog or upgrading
entry because this only fixes unreleased v4 behavior. Fold the final pull
request reference into the existing QDMI changelog entry during publication.

## Milestones

The first milestone ends when the helper treats local and imported Core runtime
targets alike and includes imported device DLL dependencies. Configure the
release preset; CMake generation must accept both branches on the host.

The second milestone ends when the existing imported-device fixture installs and
resolves MQT Core, compiles and runs the public Client consumer, and checks the
full staged layout and RPATH property. Run only its configure and build CTests.
Both tests must pass, the consumer must return zero, and the build step's file
comparisons must report no difference.

The third milestone ends when documentation, lint, and the relevant release
tests pass and the complete focused diff is stored in one signed commit. The
worktree must be clean, `git verify-commit HEAD` must succeed, and no remote
state may change.

## Concrete Steps

Run all commands from the repository root. Configure and build the release
preset:

    cmake --preset release
    cmake --build --preset release --target mqt-core-qdmi-driver-test

Run the nested imported-device fixture:

    ctest --test-dir build/release --output-on-failure \
      -R '^mqt-core-qdmi-imported-device-(configure|build)$'

Run the documentation and repository checks:

    uvx nox --non-interactive -s docs
    uvx nox -s lint

Run the QDMI Driver tests and, when time permits, the full configured suite:

    ./build/release/test/qdmi/driver/mqt-core-qdmi-driver-test
    ctest --preset release

Inspect `git diff --check`, the complete diff, and `git status --short` before
creating the signed commit. Verify the commit with `git verify-commit HEAD`.

## Validation and Acceptance

The nested fixture must install MQT Core into its own prefix and resolve
imported `MQT::CoreQDMI` and `MQT::CoreQDMIDriver` targets through
`find_package(mqt-core)`. Its build must compile and run a public Client
consumer and prove that the consumer directory contains byte-identical copies of
both Core libraries, the device library, its runtime asset, and its generated
manifest. On Windows, the directory must also contain the synthetic transitive
DLL for each of the three runtime layers. Configuration must prove that
`mqt_copy_qdmi_runtime` resets `BUILD_WITH_INSTALL_RPATH` to false. The
installed include tree must provide the direct `ProgramFormat.hpp` dependency of
`Client.hpp`.

The normal Driver test binary must continue to pass. Documentation must explain
that installed targets work and that Windows dependencies are copied. Lint must
pass without suppressions. The final diff must not touch Client or Driver C++
sources, bindings, Python code, device payloads, `AddMQTQDMIDevice` callers
outside the existing fixture, or compiler control flow.

## Idempotence and Recovery

CMake configuration, build, CTest, documentation, and lint commands are safe to
rerun. Copy targets use `copy_if_different`, so repeated builds do not rewrite
unchanged runtime files. If the nested fixture has stale generated state,
rerunning its configure CTest refreshes it before the build CTest. Preserve a
signed checkpoint and backup ref before any later replay. Do not push or create
a pull request from this task.

## Artifacts and Notes

The release build of `mqt-core-qdmi-driver-test` passes. The focused nested
fixture reports two passing CTests. The first configures the installed package;
the second builds and runs the consumer and checks every staged file. The Driver
binary reports 98 passing tests. The documentation build passes when given the
repository's MLIR 22.1.8 package path. The full `uvx nox -s lint` session passes
without suppressions. The full release build completes 433 steps, and
`ctest --preset release --output-on-failure` reports 4,091 passing tests with
one expected skipped test.

After QDMI pull requests 512 and 513 removed two parameterized Driver cases, the
replay onto `e81a27576ade1ff2e63dcade90eda8f925b1cb69` passes both
installed-consumer fixture tests and all 97 Driver tests. Lint passes on the
exact replayed head.

## Interfaces and Dependencies

The production deployment changes are the behavior of the existing CMake
function and the complete installed CoreQDMI public header set:

    mqt_copy_qdmi_runtime(<consumer> [<device-target>...])

The implementation uses CMake 3.24 features already required by the fixture:
imported targets, aliases, custom targets, generator expressions,
`TARGET_RUNTIME_DLLS`, `COMMAND_EXPAND_LISTS`, and `copy_if_different`. It adds
no source or package dependency.

Plan revision note: The initial revision records the deployment-only boundary,
the imported-target failure modes, the reuse of the existing fixture, and the
required validation.

Plan update, 2026-08-24: Replayed deployment hardening onto the final C2 head,
preserved the separate QDMI cleanup entry, and recorded focused validation after
QDMI removed two Driver test cases.

Plan update, 2026-08-24: Replayed the unchanged deployment layer onto the C2
Windows runtime fix and reran the installed-consumer and Driver checks.

Plan update, 2026-08-24: Added a linker-language-bearing runtime closure after
hosted Windows proved that CMake cannot evaluate transitive DLLs directly from
the exported imported targets.
