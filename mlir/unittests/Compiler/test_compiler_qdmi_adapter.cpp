/*
 * Copyright (c) 2023 - 2026 Chair for Design Automation, TUM
 * Copyright (c) 2025 - 2026 Munich Quantum Software Company GmbH
 * All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License
 */

#include "mlir/Compiler/QDMIAdapter.h"
#include "mlir/Compiler/Target.h"
#include "mlir/Compiler/TargetEnvironment.h"
#include "qdmi/Client.hpp"
#include "qdmi/ProgramFormat.hpp"

#include <gtest/gtest.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <qdmi/constants.h>
/// POSIX declares setenv in <stdlib.h>.
/// NOLINTNEXTLINE(modernize-deprecated-headers)
#include <stdlib.h>

#include <cassert>
#include <cstdlib>
#include <optional>
#include <string>

using mlir::CompilerTarget;

namespace {
struct ConfiguredClientEnvironment {
  ConfiguredClientEnvironment() noexcept {
#ifdef _WIN32
    if (_putenv_s("MQT_CORE_QDMI_CONFIG_FILE",
                  MQT_CORE_MLIR_QDMI_TEST_CONFIG) != 0) {
      std::abort();
    }
#else
    if (setenv("MQT_CORE_QDMI_CONFIG_FILE", MQT_CORE_MLIR_QDMI_TEST_CONFIG,
               1) != 0) {
      std::abort();
    }
#endif
  }
};

const ConfiguredClientEnvironment CONFIGURED_CLIENT_ENVIRONMENT;
} // namespace

[[nodiscard]] static const mlir::ProgramCapability&
findCapability(const mlir::PayloadSpecification& payload,
               const llvm::StringRef id) {
  const auto* const found =
      llvm::find_if(payload.capabilities(), [&](const auto& capability) {
        return capability.id == id;
      });
  assert(found != payload.capabilities().end() &&
         "Payload capability not found");
  return *found;
}

[[nodiscard]] static const CompilerTarget::Operation&
findOperation(const CompilerTarget& target, const llvm::StringRef name) {
  const auto* const found =
      llvm::find_if(target.operations(),
                    [&](const auto& op) { return op.canonicalName() == name; });
  assert(found != target.operations().end() && "Target operation not found");
  return *found;
}

TEST(CompilerQDMIAdapterTest, SnapshotsIQMCalibrationAndLifetime) {
  const auto target = llvm::cantFail([] {
    const auto device = qdmi::Session::openDevice("mqt.sc.iqm.garnet");
    return mlir::compilerTargetFromDevice(device);
  }());

  ASSERT_TRUE(target.name());
  EXPECT_EQ(*target.name(), "IQM Garnet");
  EXPECT_EQ(target.numSites(), 20);
  EXPECT_EQ(target.connectivityKind(),
            CompilerTarget::Connectivity::Kind::Explicit);
  EXPECT_EQ(target.couplings().size(), 30);

  ASSERT_TRUE(target.durationUnit());
  EXPECT_EQ(target.durationUnit()->unit(), "us");
  EXPECT_DOUBLE_EQ(target.durationUnit()->scaleFactor(), 0.001);

  ASSERT_EQ(target.sites().size(), 20);
  ASSERT_TRUE(target.sites().front().name());
  EXPECT_EQ(*target.sites().front().name(), "QB1");
  EXPECT_EQ(target.sites().front().t1(), 26626);
  EXPECT_EQ(target.sites().front().t2(), 8376);

  ASSERT_EQ(target.operations().size(), 3);
  const auto& r = findOperation(target, "r");
  const auto& cz = findOperation(target, "cz");
  const auto& measure = findOperation(target, "measure");
  EXPECT_EQ(r.siteTuples().size(), 20);
  EXPECT_EQ(cz.siteTuples().size(), 30);
  EXPECT_EQ(measure.siteTuples().size(), 20);
  for (const auto& operation : target.operations()) {
    EXPECT_FALSE(operation.duration());
    for (const auto& tuple : operation.siteTuples()) {
      EXPECT_FALSE(tuple.duration());
      EXPECT_TRUE(tuple.fidelity());
    }
  }

  EXPECT_EQ(target.supportsOperation("r", 1, 2), true);
  EXPECT_EQ(target.supportsOperation("cz", 2, 0), true);
  EXPECT_EQ(target.supportsOperation("measure", 1, 0), true);
  EXPECT_EQ(target.supportsOperation("rx", 1, 1), false);
  ASSERT_TRUE(target.synthesisBasis());
  EXPECT_EQ(target.synthesisBasis()->singleQubit,
            CompilerTarget::SingleQubitBasis::R);
  EXPECT_EQ(target.synthesisBasis()->entangler, CompilerTarget::GateKind::CZ);
}

TEST(CompilerQDMIAdapterTest, PreservesMissingTargetFactsAsUnknown) {
  const auto device = qdmi::Session::openDevice("mqt.ddsim.default");
  const auto target = llvm::cantFail(mlir::compilerTargetFromDevice(device));

  EXPECT_EQ(target.numSites(), 65535);
  EXPECT_EQ(target.connectivityKind(),
            CompilerTarget::Connectivity::Kind::Unknown);
  EXPECT_EQ(target.nativeOperationsKind(),
            CompilerTarget::NativeOperations::Kind::Unknown);
  EXPECT_EQ(target.supportsOperation("h", 1, 0), std::nullopt);
  EXPECT_EQ(target.supportsOperation("cx", 2, 0), std::nullopt);
  EXPECT_EQ(target.supportsOperation("measure", 1, 0), std::nullopt);
}

TEST(CompilerQDMIAdapterTest, SnapshotsExactPayloadAndFeatureGroups) {
  const auto device = qdmi::Session::openDevice("mqt.ddsim.default");
  const auto environment = llvm::cantFail(
      mlir::targetEnvironmentFromDevice(device, qdmi::OPENQASM3));
  const auto& payload = environment.payloadSpecification();

  EXPECT_EQ(payload.format(),
            (mlir::PayloadFormat{.id = "openqasm",
                                 .version = "3.0.0",
                                 .profile = "",
                                 .encoding = mlir::PayloadEncoding::Text}));
  EXPECT_TRUE(payload.optionalCapabilitiesKnown());
  ASSERT_EQ(payload.capabilities().size(), 5);
  for (const llvm::StringRef id : {QDMI_PROGRAM_FEATURE_MID_CIRCUIT_MEASUREMENT,
                                   QDMI_PROGRAM_FEATURE_MEASURED_QUBIT_REUSE,
                                   QDMI_PROGRAM_FEATURE_MEASUREMENT_RESULT_USE,
                                   QDMI_PROGRAM_FEATURE_BOOLEAN_COMPUTATION,
                                   QDMI_PROGRAM_FEATURE_FORWARD_BRANCHING}) {
    const auto& capability = findCapability(payload, id);
    EXPECT_EQ(capability.value, 0);
    EXPECT_TRUE(capability.constraints.empty());
  }
}

TEST(CompilerQDMIAdapterTest, AddsQIRAdaptiveBaselineOnce) {
  const auto environment = llvm::cantFail(mlir::targetEnvironmentFromDeviceId(
      "mqt.ddsim.default", qdmi::QIR21_ADAPTIVE_BINARY));
  const auto& payload = environment.payloadSpecification();

  EXPECT_EQ(payload.format(),
            (mlir::PayloadFormat{.id = "qir",
                                 .version = "2.1.0",
                                 .profile = "adaptive",
                                 .encoding = mlir::PayloadEncoding::Binary}));
  EXPECT_TRUE(payload.optionalCapabilitiesKnown());
  ASSERT_EQ(payload.capabilities().size(), 5);
  for (const llvm::StringRef id : {QDMI_PROGRAM_FEATURE_MID_CIRCUIT_MEASUREMENT,
                                   QDMI_PROGRAM_FEATURE_MEASURED_QUBIT_REUSE,
                                   QDMI_PROGRAM_FEATURE_MEASUREMENT_RESULT_USE,
                                   QDMI_PROGRAM_FEATURE_BOOLEAN_COMPUTATION,
                                   QDMI_PROGRAM_FEATURE_FORWARD_BRANCHING}) {
    EXPECT_TRUE(findCapability(payload, id).constraints.empty());
  }
}

TEST(CompilerQDMIAdapterTest, RejectsPayloadNotAcceptedByDevice) {
  constexpr QDMI_Program_Format unsupported{
      .version = QDMI_MAKE_VERSION(3, 1, 0),
      .encoding = QDMI_PROGRAM_ENCODING_TEXT,
      .id = "openqasm",
      .profile = ""};
  const auto device = qdmi::Session::openDevice("mqt.ddsim.default");
  auto environment = mlir::targetEnvironmentFromDevice(device, unsupported);

  ASSERT_FALSE(environment);
  EXPECT_NE(llvm::toString(environment.takeError()).find("does not accept"),
            std::string::npos);
}

TEST(CompilerQDMIAdapterTest, ListsRegisteredDeviceIds) {
  const auto deviceIds = llvm::cantFail(mlir::registeredQDMIDeviceIds());
  EXPECT_TRUE(llvm::is_contained(deviceIds, "mqt.ddsim.default"));
}

TEST(CompilerQDMIAdapterTest, ConvertsUnknownDeviceFailureToError) {
  auto target = mlir::compilerTargetFromDeviceId("mqt.unknown.device");
  ASSERT_FALSE(target);
  const auto message = llvm::toString(target.takeError());
  EXPECT_NE(message.find("mqt.unknown.device"), std::string::npos);

  auto environment = mlir::targetEnvironmentFromDeviceId("mqt.unknown.device",
                                                         qdmi::OPENQASM3);
  ASSERT_FALSE(environment);
  const auto environmentMessage = llvm::toString(environment.takeError());
  EXPECT_NE(environmentMessage.find("mqt.unknown.device"), std::string::npos);
}

TEST(CompilerQDMIAdapterTest, RejectsNonhomogeneousOperationSupport) {
  const auto device = qdmi::Session::openDevice("test.mlir.heterogeneous");
  auto environment = mlir::targetEnvironmentFromDevice(device, qdmi::OPENQASM3);
  ASSERT_FALSE(environment);
  const auto message = llvm::toString(environment.takeError());
  EXPECT_NE(message.find("homogeneous"), std::string::npos);
  EXPECT_NE(message.find("every topology edge"), std::string::npos);
}

TEST(CompilerQDMIAdapterTest, RejectsDirectionalOperationWithoutReverseSites) {
  const auto device =
      qdmi::Session::openDevice("test.mlir.directional-one-way");
  auto target = mlir::compilerTargetFromDevice(device);
  ASSERT_FALSE(target);
  const auto message = llvm::toString(target.takeError());
  EXPECT_NE(message.find("both orientations"), std::string::npos);
}

TEST(CompilerQDMIAdapterTest,
     PreservesDirectionalCalibrationWhenBothOrientationsExist) {
  const auto device =
      qdmi::Session::openDevice("test.mlir.directional-two-way");
  const auto target = llvm::cantFail(mlir::compilerTargetFromDevice(device));

  ASSERT_EQ(target.couplings().size(), 1);
  const auto& cx = findOperation(target, "cx");
  ASSERT_EQ(cx.siteTuples().size(), 2);
  EXPECT_EQ(cx.siteTuples()[0].sites(),
            (llvm::ArrayRef<CompilerTarget::SiteId>{0, 1}));
  EXPECT_DOUBLE_EQ(*cx.siteTuples()[0].fidelity(), 0.91);
  EXPECT_EQ(cx.siteTuples()[1].sites(),
            (llvm::ArrayRef<CompilerTarget::SiteId>{1, 0}));
  EXPECT_DOUBLE_EQ(*cx.siteTuples()[1].fidelity(), 0.92);
}
