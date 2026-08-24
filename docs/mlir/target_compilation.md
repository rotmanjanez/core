# Compile for a QDMI device

An MLIR {code}`mlir::CompilerTarget` is an immutable snapshot of a circuit-model
device. It contains the device sites, topology, native operations, and available
calibration data. Compilation decomposes supported multi-qubit operations,
optimizes and maps the program, synthesizes native gates, and verifies that the
result conforms to the target.

The snapshot is independent of its originating QDMI session. It can therefore be
stored, copied cheaply, and reused for multiple compilations.

## Python

Open a configured QDMI device and snapshot it as a compiler target:

```python
from mqt.core.mlir import (
    TargetEnvironment,
    compile_program,
)
from mqt.core.qdmi import ProgramFormat

environment = TargetEnvironment.from_device_id(
    "mqt.sc.iqm.garnet",
    ProgramFormat.QIR21_BASE_BINARY,
)
compiled = compile_program(
    "bell.qasm",
    target_environment=environment,
)
```

The QDMI adapter checks that the device accepts the exact program format. It
groups program-feature records by ID and value, adds the selected format's
normative baseline, and preserves whether the optional feature list is known.
MQT Core derives the compiler output from this payload specification and uses
the canonical QCO pipeline. The targeted overload therefore accepts one
`TargetEnvironment` and no independent output or custom pipeline.

### Payload control flow

Target compilation removes unused symbols, lifts reducible ControlFlow dialect
graphs to SCF, and propagates constants. It checks constant loop ranges with
widened arithmetic before generic canonicalization, unrolls unsupported static
loops, and then runs the standard QCO cleanup pipeline. It applies these
structural capabilities to the remaining control flow:

| Capability           | Residual operations                                 |
| -------------------- | --------------------------------------------------- |
| `forward-branching`  | `qco.if` and classical `scf.if`                     |
| `counted-iteration`  | `scf.for`                                           |
| `conditional-loop`   | `scf.while`                                         |
| `multiway-branching` | `qco.index_switch` and classical `scf.index_switch` |

A finite `scf.for` that exceeds the selected counted-iteration contract is fully
unrolled when this clones at most 65,536 body operations. Cleanup runs again
because unrolling can make nested bounds and conditions constant. An unsupported
index switch is lowered to nested forward branches when that form fits the
selected contract. Generic SCF branches cannot capture or return QCO qubits or
quantum tensors; use the corresponding QCO branch operation for linear quantum
state. SCF loops must carry linear quantum state through their iteration
arguments instead of capturing it.

The supported constraints are `max-control-flow-nesting-depth` on all four
capabilities, `max-iteration-count` on both iteration capabilities, and
`max-case-count` on multiway branching. Limits are inclusive. The compiler must
prove a constrained loop's trip count. It currently proves constant `scf.for`
bounds and rejects a constrained `scf.while` because no general termination
bound is available. The compiler rejects a constant range when MLIR's native
trip-count result disagrees with widened arithmetic. A zero, unknown, or
misapplied constraint makes that capability group unusable. Missing or
incomplete optional metadata never implies support.

This stage checks structural control flow only. Later lowering stages remain
responsible for scalar types and operations, measurement provenance, function
features, allocation, and final payload-profile conformance.

The target can also be constructed directly. Connectivity and native-operation
metadata are unknown unless the caller states them:

```python
target = CompilerTarget(
    3,
    connectivity=CompilerTarget.Connectivity([(0, 1), (1, 2)]),
    native_operations=CompilerTarget.NativeOperations.unrestricted(),
)
```

Use `CompilerTarget.Connectivity.all_to_all()` for an all-to-all target. An
empty `CompilerTarget.NativeOperations([])` means that no operation is native.
The default-constructed metadata objects mean that the corresponding support is
unknown; target compilation rejects an unknown property when a pass needs it.

Use {py:meth}`~mqt.core.mlir.QCOProgram.compile_for_target` with the target
environment to apply target compilation to an existing QCO program. Compilation
runs in place. If a pass fails, the environment and earlier pass changes remain
on the program. Copy the program before compilation if the caller must preserve
the input. The target passes read the typed `mqt.target_env` module attribute.
Their default factories therefore also work in textual MLIR pass pipelines.

Target compilation preserves quantum operations even when their final qubit
values are not measured or returned. This supports measurement-free programs,
such as state preparation or larger building blocks compiled to a target-native
instruction set. Dead gates are removed only by the explicit `remove-dead-gates`
pass and by pipelines that include it, such as `mqt-qubit-reuse`.

## Command line from a source build

List the stable IDs of configured QDMI devices:

```console
mqt-cc --qdmi-list-devices
```

Select a device when compiling:

```console
mqt-cc --qdmi-device=mqt.sc.iqm.garnet \
  --payload-spec='#mqt.payload_spec<format = <id = "qir", version = "2.1.0", profile = "base", encoding = binary>, capabilities = [], optional_capabilities_known = false>' \
  -o output.bc input.qasm
```

An explicit registry file can be selected before device discovery:

```console
mqt-cc --qdmi-config=/path/to/qdmi.json \
  --qdmi-device=example.device \
  --payload-spec='#mqt.payload_spec<format = <id = "qir", version = "2.1.0", profile = "base", encoding = binary>, capabilities = [], optional_capabilities_known = false>' \
  input.qasm
```

The payload specification selects the emitted format and encoding. For targeted
QIR, the selected encoding takes precedence over the output filename extension.
Target compilation rejects `--emit` and custom `--passes` pipelines because the
target contract owns the output and required pass ordering.

## C++ source-tree API

The source build provides a narrow, non-throwing QDMI bridge between a stable
device ID and the compiler-owned target:

```cpp
#include "mlir/Compiler/QDMIAdapter.h"
#include "mlir/Compiler/Programs.h"
#include "qdmi/ProgramFormat.hpp"
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>

auto environment = mlir::targetEnvironmentFromDeviceId(
    "mqt.sc.iqm.garnet", qdmi::QIR21_BASE_BINARY);
if (!environment) {
  llvm::errs() << "Failed to create target environment: "
               << llvm::toString(environment.takeError()) << '\n';
  return 1;
}

auto qc = mlir::QCProgram::fromQASMFile("input.qasm");
if (!qc) {
  return 1;
}
auto qco = std::move(*qc).intoQCO();
if (!qco || !qco->compileForTarget(*environment)) {
  return 1;
}
```

The adapter accepts circuit-model devices whose operations are available
throughout the topology in both operand orientations. Operand-symmetric gates,
such as CZ, may report each edge once. Neutral-atom zone models require a
different compilation model and are rejected with a diagnostic.

The bundled Garnet and Emerald snapshots contain available T1, T2, and fidelity
data. Operation durations are absent because they were unavailable. See
{doc}`../qdmi/sc_device` for their stable IDs and {doc}`../qdmi/configuration`
for registry configuration.

If the program should use fewer physical qubits, run the {code}`mqt-qubit-reuse`
pipeline before target compilation.

## Qiskit export

When exporting a program that has already been mapped to a
{py:class}`~mqt.core.mlir.CompilerTarget`, pass the same target to
{py:meth}`~mqt.core.mlir.QCProgram.to_qiskit`. The exporter maps each static
target site ID to its index in {py:attr}`~mqt.core.mlir.CompilerTarget.sites`
and creates a canonical physical Qiskit circuit. The circuit has one register
named {code}`q` with {py:attr}`~mqt.core.mlir.CompilerTarget.num_sites` qubits.
This option does not run target compilation or emit Qiskit layout metadata.
Target-aware export requires static qubits whose site IDs belong to that target.
