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
#include "mlir/Compiler/TargetEnvironment.h"
#include "qdmi/Client.hpp"
#include "qdmi/ProgramFormat.hpp"

#include <gtest/gtest.h>
#include <llvm-c/Error.h>
#include <llvm/Support/Error.h>
#include <qdmi/constants.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

[[nodiscard]] static auto openAdapterTestDevice(const std::string_view token)
    -> qdmi::Device {
  return qdmi::Session::openDevice(
      "test.fake.client",
      qdmi::SessionConfig{.driverPath = MQT_CORE_MLIR_QDMI_TEST_DRIVER,
                          .token = std::string(token)});
}

TEST(CompilerQDMIFeatureAdapterTest, GroupsConstrainedFeatures) {
  const auto device = openAdapterTestDevice("adapter-feature-groups");
  const auto environment = llvm::cantFail(
      mlir::targetEnvironmentFromDevice(device, qdmi::OPENQASM3));
  const auto& capabilities = environment.payloadSpecification().capabilities();

  ASSERT_EQ(capabilities.size(), 1U);
  const auto& capability = capabilities.front();
  EXPECT_EQ(capability.id, QDMI_PROGRAM_FEATURE_COUNTED_ITERATION);
  EXPECT_EQ(capability.value, 0U);
  EXPECT_EQ(
      capability.constraints,
      (std::vector<mlir::ProgramConstraint>{
          {.id = QDMI_PROGRAM_CONSTRAINT_MAX_CONTROL_FLOW_NESTING_DEPTH,
           .value = 3U},
          {.id = QDMI_PROGRAM_CONSTRAINT_MAX_ITERATION_COUNT, .value = 100U}}));
}

TEST(CompilerQDMIFeatureAdapterTest, RejectsInvalidOptionalFeatureRecords) {
  constexpr std::array cases{std::string_view{"adapter-invalid-record"},
                             std::string_view{"adapter-duplicate-unrestricted"},
                             std::string_view{"adapter-mixed-group"},
                             std::string_view{"adapter-duplicate-constraint"}};

  for (const auto token : cases) {
    SCOPED_TRACE(token);
    const auto device = openAdapterTestDevice(token);
    auto environment =
        mlir::targetEnvironmentFromDevice(device, qdmi::OPENQASM3);
    ASSERT_FALSE(environment);
    LLVMConsumeError(llvm::wrap(environment.takeError()));
  }
}

TEST(CompilerQDMIFeatureAdapterTest, RejectsInvalidProgramFormats) {
  const auto device = openAdapterTestDevice("adapter-invalid-formats");
  auto missingVersion = qdmi::OPENQASM3;
  missingVersion.version = 0U;
  auto invalidEncoding = qdmi::OPENQASM3;
  invalidEncoding.encoding = 3U;

  for (const auto& format : {missingVersion, invalidEncoding}) {
    auto environment = mlir::targetEnvironmentFromDevice(device, format);
    ASSERT_FALSE(environment);
    EXPECT_NE(llvm::toString(environment.takeError()).find("not canonical"),
              std::string::npos);
  }
}
