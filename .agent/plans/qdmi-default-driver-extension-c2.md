# Add the optional packaged QDMI Driver extension

This ExecPlan is a living document. The sections `Progress`,
`Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must
be kept up to date as work proceeds.

This ExecPlan must be maintained in accordance with `.agent/PLANS.md` from the
repository root.

## Purpose / Big Picture

An installed QDMI device package needs a safe way to make its manifest visible
to MQT Core's packaged Driver without copying files beside that Driver and
without importing vendor code. An application that explicitly depends on the
packaged Driver also needs to open one configured stable ID with per-call
overrides, while the generic QDMI Client API must remain portable to other
Drivers.

After this change, Python distributions can advertise manifests through package
metadata. C++ and Python users can stage a trusted manifest and open one device
through an optional private packaged-Driver extension. Standard Client sessions
continue to work when a Driver does not export the extension.

## Progress

- [x] (2026-08-24 17:01Z) Add optional private-symbol loading and C++ extension
  entry points without changing public QDMI headers.
- [x] (2026-08-24 17:01Z) Add transactional package-manifest staging and strict
  targeted session allocation to the packaged Driver.
- [x] (2026-08-24 17:01Z) Add metadata-only Python discovery, bindings, and
  focused native and Python tests.
- [x] (2026-08-24 17:01Z) Document the package metadata and private-extension
  contracts and update the existing unreleased QDMI migration notes.
- [x] (2026-08-24 17:15Z) Regenerate Python stubs and pass focused native and
      Python tests, documentation, lint, the release build, and all 4,088
      configured CTests with one expected skip.
- [x] (2026-08-24 22:10Z) Accept valid provider warnings without exposing a
      warning with a null targeted session, keep the packaged extension
      independent of the generic Driver override, make metadata enumeration
      resilient, and remove Python test-order dependence.
- [x] (2026-08-24 18:11Z) Preserve INVALIDARGUMENT when targeted allocation
      triggers invalid Driver configuration, and require an actual wheel
      `RECORD` before trusting a distribution file list.
- [x] (2026-08-24 18:16Z) Reject warning-plus-null allocation results before
  logging them, and free non-null device handles returned with an error.
- [x] (2026-08-24 19:07Z) Replay the single C2 commit onto exact upstream head
  `cfc4815849836c43855f20dabc23be86767e41be`; verify the final signature and
  report the commit identity in the handoff.
- [x] (2026-08-24 20:16Z) Replay the single C2 commit onto the QDMI cleanup head
      `d83d5e141b8187d83d90e80c51c802e707801ddf`; preserve the separate cleanup
      changelog entry; and pass all 115 focused native tests, all 12 focused
      Python tests, stub generation, and lint.
- [x] (2026-08-24 21:51Z) Sequence targeted-device initialization before moving
      its session owner, isolate generated manifests from Windows runtime
      directories, retain initialized provider libraries, and cover a packaged
      DDSIM targeted open.

## Surprises & Discoveries

- Observation: A Driver may accept a raw targeted session and then reject
  initialization. Evidence: the dedicated fake Client returns success from the
  private allocation symbol and `QDMI_ERROR_PERMISSIONDENIED` from
  `QDMI_session_init`. The Client selection remains fixed, as it does for a
  standard raw allocation.
- Observation: QDMI session values use explicit sizes, so an embedded null byte
  is valid for provider-defined custom slots. Evidence: the registry parser test
  preserves all three bytes in `"x\u0000y"`. Only IDs and values that cross a
  filesystem or C-string boundary reject null bytes.
- Observation: A previously accepted manifest path must remain idempotent even
  when the file is later absent. Evidence: the package-manifest test removes an
  accepted manifest after freeze and stages the same canonical path again.
- Observation: MSVC can move an object-valued braced-initializer argument before
  evaluating an earlier argument. Evidence: the hosted Windows call moved the
  targeted session owner before device initialization, and the minimized MSVC
  19.44 assembly has the same order.
- Observation: Multi-config test executables and generated fixtures shared one
  Windows directory. Evidence: the main QDMI test discovered malformed private
  extension manifests during static GoogleTest registration and terminated
  before test discovery.

## Decision Log

- Decision: Keep both extension symbols private and optional on the packaged
  Driver shared library. Rationale: the standard QDMI Client ABI remains the
  portable boundary, and generic sessions must work with any conforming Driver.
  Date/Author: 2026-08-24 / Codex.
- Decision: Stage package manifests at the lowest configuration precedence and
  freeze them when the packaged Driver successfully constructs its registry
  during a session-allocation request. Rationale: package defaults must not
  override administrator, user, project, or environment configuration, and one
  Driver instance needs an immutable registry. Date/Author: 2026-08-24 / Codex.
- Decision: Select the Client after successful raw targeted allocation, before
  initialization and device validation. Rationale: this matches standard Client
  selection. Provider calls after allocation run without the process selection
  mutex to avoid reentrant deadlock. Date/Author: 2026-08-24 / Codex.
- Decision: Resolve Python manifests only from entry-point distribution metadata
  and wheel `RECORD` paths. Rationale: discovery must not execute provider code,
  and the module anchor prevents unrelated files in the distribution from being
  selected. Date/Author: 2026-08-24 / Codex.
- Decision: Resolve `default_driver` calls against the packaged Driver by
  default and ignore `MQT_CORE_QDMI_DRIVER`, while retaining the explicit
  compatible-extension override. Rationale: installed providers depend on the
  packaged extension, while standard Client sessions own the replaceable Driver
  selection contract. Date/Author: 2026-08-24 / Codex.
- Decision: Finish targeted initialization before moving the session owner into
  the returned device. Rationale: two full expressions remove an order-dependent
  move and follow the required ownership sequence on every compiler.
  Date/Author: 2026-08-24 / Codex.
- Decision: Generate private-extension manifests in dedicated subdirectories.
  Rationale: tests receive exact paths, while production registry discovery must
  never ingest unrelated negative fixtures. Date/Author: 2026-08-24 / Codex.
- Decision: Keep initialized device libraries in the process-wide Driver cache.
  Rationale: QDMI requires one initialization and finalization per device use;
  fresh targeted sessions must not unload and reinitialize the provider.
  Date/Author: 2026-08-24 / Codex.

## Outcomes & Retrospective

The implementation separates the packaged-Driver convenience layer from the
standard Client path. It adds no public QDMI symbol and does not alter generic
device enumeration. The final one-commit replay is based directly on the exact
assigned QDMI cleanup head. All 115 focused C++ tests and all 12 focused Python
tests pass after the replay. Generated stubs and lint also pass. Documentation,
the release build, and the full configured CTest suite passed on the preceding
signed checkpoint.

## Context and Orientation

`include/mqt-core/qdmi/Client.hpp` and `src/qdmi/Client.cpp` implement MQT
Core's owning C++ wrappers around the standard QDMI Client ABI. A process loads
one Client driver and fixes that selection after the first successful raw
session allocation. `src/qdmi/driver/Driver.cpp` is MQT Core's packaged
implementation of that Client ABI. `src/qdmi/driver/DeviceRegistry.cpp` reads
device definitions and merges their session defaults.

A package manifest is a `*.qdmi.json` file that names a stable device ID, its
native device library, and the library's QDMI symbol prefix. Staging means
adding that manifest to the packaged Driver's lowest-precedence inputs before
the registry becomes immutable. A targeted session is a fresh QDMI session that
contains exactly one device selected by stable ID.

`bindings/qdmi/qdmi.cpp` exposes the C++ API through nanobind.
`python/mqt/core/_qdmi_discovery.py` uses `importlib.metadata` to inspect
installed distributions. The module must not import the package that owns a
manifest. `test/qdmi/` owns native integration tests, while `test/python/qdmi/`
owns binding and discovery tests.

This task changes only the optional packaged-Driver extension. It does not
change installed-consumer CMake deployment helpers, `AddMQTQDMIDevice`, MLIR
payloads, or compiler control flow. Preserve all unrelated changes and follow
`AGENTS.md` and `docs/ai_usage.md`. This plan does not authorize a push or any
GitHub action.

## Plan of Work

In `Client.hpp` and `Client.cpp`, declare `qdmi::default_driver::addManifest`
and `qdmi::default_driver::openDevice`. Load the exact optional symbols
`MQT_CORE_QDMI_driver_add_manifest_v1` and
`MQT_CORE_QDMI_driver_session_alloc_for_device_v1` after validating the standard
Client function table. Do not require either symbol for a generic session.
Loading the packaged Driver to stage a manifest must not select it. Successful
raw targeted allocation commits selection; session initialization and
exact-one-device validation then run outside the selection mutex.

In `DeviceRegistry.cpp` and `Driver.cpp`, canonicalize and stage package
manifests with low precedence. Accept the same canonical path more than once,
reject conflicting IDs, and reject new paths after the packaged Driver
successfully constructs its registry. Copy staging state before mutation so
allocation failure cannot leave a partial transaction. Roll back the freeze when
Driver construction fails. Parse targeted JSON strictly, merge it over manifest
defaults, and create a fresh session for the requested ID. Set the output handle
to null before work. Map exceptions to QDMI status codes and preserve exact
statuses returned by the device library.

Validate stable IDs and all filesystem or C-string values before conversion.
Reject empty or embedded-null library, authentication-file, and
device-configuration paths, and reject embedded-null symbol prefixes. Preserve
the explicit size of ordinary and custom QDMI session values. Reject a typed
`device-config` combined with raw CUSTOM1 or CUSTOM2 before staging succeeds.

In `bindings/qdmi/qdmi.cpp`, expose `mqt.core.qdmi.default_driver` with
`add_manifest` and `open_device`. Keep generic `mqt.core.qdmi.open_device`
unchanged. In `_qdmi_discovery.py`, inspect entry points in
`mqt.core.qdmi.manifests`. Require a safe manifest basename, a dotted module
anchor, distribution metadata, and exactly one matching wheel `RECORD` path
below that anchor. Materialize entry-point enumeration inside the optional
discovery boundary. Warn once and skip each invalid automatic entry or one
failed enumeration. Keep explicit staging strict.

Add focused tests for optional-symbol absence, staging without selection,
selection timing, freeze rollback, path idempotence, conflicting IDs, strict
status and configuration handling, valid warning outputs, malformed JSON, UTF-8
paths, independent session lifetime, packaged-path selection, and the generic
boundary. Add separate Python tests for metadata discovery and the
default-driver binding. Regenerate stubs, update the QDMI configuration and
Driver documentation, add a provisional link-free C2 changelog bullet, and
update the existing QDMI upgrade section.

## Concrete Steps

Run all commands from the repository root. Configure and build the focused
native targets with the repository's release preset:

    cmake --preset release
    cmake --build --preset release --target \
      mqt-core-qdmi-client-runtime-test \
      mqt-core-qdmi-targeted-selection-test \
      mqt-core-qdmi-default-driver-extension-test \
      mqt-core-qdmi-driver-test mqt-core-qdmi-registry-test

Run the five focused binaries directly. Each must report that all tests passed:

    ./build/release/test/qdmi/mqt-core-qdmi-client-runtime-test
    ./build/release/test/qdmi/mqt-core-qdmi-targeted-selection-test
    ./build/release/test/qdmi/mqt-core-qdmi-default-driver-extension-test
    ./build/release/test/qdmi/driver/mqt-core-qdmi-driver-test
    ./build/release/test/qdmi/registry/mqt-core-qdmi-registry-test

Regenerate and check Python bindings, then run the two focused test files:

    uvx nox -s stubs
    uv run --no-sync pytest test/python/qdmi/test_discovery.py \
      test/python/qdmi/test_default_driver.py

Build the documentation and run repository checks:

    uvx nox --non-interactive -s docs
    uvx nox -s lint
    cmake --build --preset release
    ctest --preset release

Record any environment failure separately from a code failure. Inspect
`git diff --check`, the final diff, and `git status --short` before committing.

## Validation and Acceptance

The native Client tests must show that missing private symbols and failed raw
targeted allocation leave selection open, while successful raw allocation
followed by failed initialization keeps selection fixed. The extension test must
show that staging does not select the Client, accepted paths remain idempotent
after freeze, new paths fail after freeze, a failed Driver construction permits
retry, strict overrides propagate provider errors, and each targeted open owns a
fresh session. Registry tests must preserve embedded null bytes in sized custom
values while rejecting them in IDs and path or symbol fields.

The discovery tests must prove that the provider package is not imported. They
must reject missing `RECORD` metadata, ambiguous names, traversal, off-anchor
paths, unsafe basenames, and invalid module anchors with one warning per entry.
The default-driver binding tests must prove strict explicit errors and fresh
stable-ID sessions. Generated stubs must expose only the `default_driver`
submodule additions and must not restore a public registry API.

Documentation succeeds when Sphinx resolves the C++ and Python API links and the
configuration guide no longer claims that copying beside the Driver is the only
installed-package route. Lint, the release build, and configured CTest must pass
or have an evidence-backed environment limitation.

## Idempotence and Recovery

CMake configuration, focused builds, stub generation, documentation, lint, and
tests are safe to rerun. Package-manifest tests run in separate processes when
they depend on process-global selection or freeze state. A failed Driver
construction restores staging state by contract.

Before any history rewrite, retain a signed checkpoint or backup ref. The final
branch must contain one signed commit on its assigned parent. Verify it with
`git verify-commit HEAD`. Do not push from this task.

## Artifacts and Notes

The focused native binaries passed 1 Client runtime test, 1 selection-timing
test, 1 extension integration test, 98 Driver tests, and 16 registry tests. The
two focused Python files passed 12 tests. `uvx nox -s stubs`,
`uvx nox --non-interactive -s docs`, and the second `uvx nox -s lint` run
succeeded. The same 117 focused native tests and 12 Python tests passed after
the replay onto `cfc4815849836c43855f20dabc23be86767e41be`. The release build
completed on the preceding checkpoint, and `ctest --preset release` reported:

    100% tests passed out of 4088
    The following tests did not run:
        1109 - ScQDMIJobSpecificationTest.QueryJobId (Skipped)

After QDMI pull requests 512 and 513 removed two parameterized Driver cases, the
replay onto `d83d5e141b8187d83d90e80c51c802e707801ddf` passed 1 Client runtime
test, 1 selection-timing test, 1 extension integration test, 96 Driver tests,
and 16 registry tests. These 115 focused native tests, all 12 focused Python
tests, stub generation, and lint passed on the exact replayed head.

The final coverage pass added tests for the existing manifest,
private-extension, and device-registration error contracts. It also removed a
duplicate session-buffer check that the sole caller already owns. All 116
focused native tests passed in release and coverage builds. Changed-line
coverage was 357/391 (91.30%); the exact hosted-map projection was 334/367
(91.01%). Full lint also passed.

## Interfaces and Dependencies

The public C++ additions are
`qdmi::default_driver::addManifest(const std::filesystem::path&)` and
`qdmi::default_driver::openDevice(std::string_view, std::string_view, const std::optional<std::filesystem::path>&)`.
The Python additions are `mqt.core.qdmi.default_driver.add_manifest` and
`mqt.core.qdmi.default_driver.open_device`. No public QDMI C header changes.

The packaged Driver exports exactly
`MQT_CORE_QDMI_driver_add_manifest_v1(const char*)` and
`MQT_CORE_QDMI_driver_session_alloc_for_device_v1` with parameters
`const char*`, `size_t`, `const char*`, and `QDMI_Session*`. The Client treats
both as optional private symbols. Python discovery uses only
`importlib.metadata`, `pathlib`, and `warnings` from the standard library.

Plan update note: The final revision records successful local validation,
clarifies the separate registry-freeze and Client-selection clocks, requires
wheel `RECORD` metadata, and records the exact final parent.

Plan update, 2026-08-24: Replayed the extension onto the separate QDMI cleanup
layer, preserved that layer's removal entry, and recorded exact focused
validation after QDMI removed two Driver test cases.

Plan update, 2026-08-24: Added public contract tests to meet the C++ patch
coverage policy without changing production code, then recorded the final
release, coverage, and lint results.

Plan update, 2026-08-24: Fixed the hosted Windows targeted-open evaluation-order
failure, separated generated negative manifests from runtime discovery, and kept
initialized providers loaded across fresh sessions.
