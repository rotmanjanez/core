# Adopt exact QDMI 1.4 payload contracts across Core

This ExecPlan is a living document. The sections `Progress`,
`Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must
be kept up to date as work proceeds.

This ExecPlan must be maintained in accordance with `.agent/PLANS.md` from the
repository root.

## Purpose / Big Picture

MQT Core must preserve the exact payload contract that a QDMI device accepts.
After this change, C++ and Python users can inspect immutable format descriptors
and optional feature records, submit one or more text or binary programs, and
retrieve each program's raw result without format-name inference or byte loss.
The Qiskit and PennyLane consumers select exact descriptors and map indexed
results to their framework layouts at the adapter boundary.

## Progress

- [x] (2026-08-24 02:10Z) Reconciled the Client, driver, and bundled devices
      with exact QDMI 1.4 format descriptors and indexed results.
- [x] (2026-08-24 06:00Z) Pinned QDMI pull request 509 at revision
      `ff59f75ce18344122c5de265cccdd560df448325`.
- [x] (2026-08-24 14:20Z) Restored the `NEEDSCALIBRATION` device property that
      remains part of QDMI 1.4.
- [x] (2026-08-24 14:30Z) Passed the release build, all 4,073 configured CTest
      cases, focused low-level C++ and Python tests, and stub generation on the
      final stack. One existing SC job-ID test remains skipped.
- [x] (2026-08-24 14:35Z) Confirmed the exact A1 head passes all focused
      low-level C++ tests and stub generation.
- [x] (2026-08-24 14:35Z) Migrated Qiskit and PennyLane from the removed enum
      and global serializer registry to exact, backend-owned payload behavior.
- [x] (2026-08-24 14:35Z) Passed all 103 Qiskit and 48 PennyLane tests,
      documentation, and lint on the atomic producer-and-consumer layer.
- [x] (2026-08-24 14:50Z) Removed three redundant Python tests that polluted the
      process-wide device registry. The combined low-level QDMI, Qiskit, and
      PennyLane run passes all 395 tests in one pytest process.

## Surprises & Discoveries

- Observation: Revision `ff59f75ce` removed the single-program job parameter.
  Evidence: the pinned headers expose only `QDMI_job_set_programs`; a
  one-program submission is the list operation with count one.
- Observation: QDMI 1.4 removes calibration as a program pseudo-format but keeps
  `QDMI_DEVICE_PROPERTY_NEEDSCALIBRATION`. Evidence: the pinned migration guide
  lists the property as retained.
- Observation: Program output is an arbitrary byte sequence. Evidence: QDMI does
  not require a final NUL for `QDMI_JOB_RESULT_PROGRAMOUTPUT`.
- Observation: The old framework adapters cannot import or type-check after the
  descriptor change. Evidence: a low-level-only split reached the removed
  `ProgramFormat.CALIBRATION` member during the documentation import and
  reported 134 `ty` diagnostics. The producer and consumers therefore form one
  atomic migration layer.
- Observation: The old Qiskit registry resolved an enum member from each entry
  point name. Evidence: exact descriptors have no global name-to-value mapping,
  so retaining the registry would require a temporary discovery contract.
- Observation: Three Python registration tests left invalid devices in the
  process-wide Driver registry. Evidence: a later provider test could observe
  `python.missing` when pytest scheduled both files in one worker. The Driver
  C++ suite already covers lazy loading, idempotent registration, and ordered
  enumeration without leaking state into Python tests.

## Decision Log

- Decision: Treat ID, version, profile, and encoding as one immutable value.
  Rationale: two descriptors with one ID can define different accepted payloads.
  Date/Author: 2026-08-24 / GPT-5.6 Sol via Codex.
- Decision: Implement single-program submission with the atomic list operation.
  Rationale: this is the only payload setter in the pinned QDMI contract.
  Date/Author: 2026-08-24 / GPT-5.6 Sol via Codex.
- Decision: Preserve `needs_calibration` but remove calibration job helpers.
  Rationale: the state property remains portable; the pseudo-format does not.
  Date/Author: 2026-08-24 / GPT-5.6 Sol via Codex.
- Decision: Let a Qiskit backend own its serializer and decoder. Rationale: the
  package that defines a vendor payload also defines its byte and result layout.
  Date/Author: 2026-08-24 / GPT-5.6 Sol via Codex.
- Decision: Keep framework layout conversion at each adapter boundary.
  Rationale: Core's raw result API preserves QDMI output-slot order and cannot
  infer a framework's register or wire order. Date/Author: 2026-08-24 / GPT-5.6
  Sol via Codex.
- Decision: Remove the three process-mutating Python registry tests. Rationale:
  the Driver C++ suite owns the same contracts and the Python tests cannot undo
  a process-wide registration through the public API. Date/Author: 2026-08-24 /
  GPT-5.6 Sol via Codex.

## Outcomes & Retrospective

The atomic producer-and-consumer migration is complete. The exact A1 head passes
234 Client, 101 driver, 53 DDSIM, and 44 SC tests, with one existing SC skip.
All 244 low-level QDMI, 103 Qiskit, and 48 PennyLane Python tests pass together.
Stub generation is clean. The layer passes the release build, all 4,073
configured CTest cases, documentation, and lint. The implementation removes the
old global serializer discovery path instead of adding a temporary
exact-descriptor registry that the same stack would then delete.

## Context and Orientation

`QDMI_Program_Format` is the C record that identifies one accepted payload.
`include/mqt-core/qdmi/ProgramFormat.hpp` defines canonical standard values and
validation helpers. `include/mqt-core/qdmi/Client.hpp` and `src/qdmi/Client.cpp`
wrap the QDMI Client interface. The driver under `src/qdmi/driver/` forwards
that interface to provider libraries. DDSIM and the superconducting test
provider live under `src/qdmi/devices/`.

The Qiskit and PennyLane adapters live under `python/mqt/core/plugins/qiskit/`
and `python/mqt/core/plugins/pennylane/`. They consume the low-level contract in
the same Python package, so imports and static checks require both sides to
migrate together.

## Plan of Work

Pin QDMI 1.4 in `cmake/ExternalDependencies.cmake`. Replace the closed format
enum with immutable descriptor values in C++ and Python. Query optional feature
records for one exact descriptor. Route one-program and multi-program submission
through `QDMI_job_set_programs`. Add a program count and an index to all result
queries. Preserve program-output bytes and parse self-delimiting text results
without relying on historical job parameters. Update the bundled providers,
driver, stubs, documentation, and focused tests. Replace the enum-keyed Qiskit
serializer registry with backend-owned exact serializers and decoders. Preserve
submitted Qiskit classical layouts, and convert QDMI output slots to PennyLane
wire and mid-circuit-measurement layouts.

## Concrete Steps

Run from the repository root:

    cmake --preset release -DMLIR_DIR=/path/to/mlir/lib/cmake/mlir
    cmake --build --preset release --target mqt-core-qdmi-test \
      mqt-core-qdmi-ddsim-device-test mqt-core-qdmi-driver-test
    ctest --preset release
    uvx nox -s stubs
    uvx nox -s tests-3.12 -- \
      test/python/qdmi \
      test/python/plugins/qiskit \
      test/python/plugins/qdmi_pennylane
    uvx nox --non-interactive -s docs
    uvx nox -s lint

Success means every configured test passes apart from an explicitly recorded
pre-existing skip, stub generation changes only binding-owned files, and the
documentation resolves QDMI links.

## Validation and Acceptance

Reconstructed descriptors compare equal only when all four fields match. A
successful empty feature query means the optional set is known and empty;
`NOTSUPPORTED` means it is unknown. Text input has one final NUL and no earlier
NUL. Binary input and `PROGRAMOUTPUT` preserve all bytes. Program lists retain
their count, and every result index maps to the same input index. The
`needs_calibration` state query remains available.

The generic Qiskit backend accepts only exact OpenQASM descriptors. A subclass
can own a vendor serializer and decoder without global registration. Qiskit
multi-register counts match the submitted classical-bit layout. PennyLane
samples match wire order for asymmetric rows. Constrained or duplicate Boolean
feature records do not enable control flow.

## Idempotence and Recovery

Build, test, stub, documentation, and lint commands are repeatable. Generated
stubs come only from `uvx nox -s stubs`. No step changes remote state. If the
pinned headers change, reconfigure and fix the typed boundary instead of adding
a compatibility shim.

## Artifacts and Notes

Temporary documentation links use the pull request 509 preview. The extension's
dormant default tag URL remains the published QDMI 1.3.3 URL.

## Interfaces and Dependencies

The layer requires QDMI revision `ff59f75ce18344122c5de265cccdd560df448325`.
Public C++ APIs use `QDMI_Program_Format`, `Device::submitPrograms`,
`Job::getProgramsNum`, and indexed result methods. Python exposes
`ProgramFormat`, `ProgramFeature`, `submit_programs`, `programs_num`, and exact
`bytes` results. `QDMIBackend._program_serializer` and `_decode_counts` let a
vendor backend own its exact payload and result conversion.

Plan update, 2026-08-24: Restored the retained calibration-state property.
Folded the framework consumers into this layer because a low-level-only commit
cannot import or type-check, and adapting the enum-keyed serializer registry
would create throwaway code.
