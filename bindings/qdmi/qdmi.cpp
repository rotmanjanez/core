/*
 * Copyright (c) 2023 - 2026 Chair for Design Automation, TUM
 * Copyright (c) 2025 - 2026 Munich Quantum Software Company GmbH
 * All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License
 */

#include "qdmi/Client.hpp"
#include "qdmi/ProgramFormat.hpp"

#include <nanobind/nanobind.h>
#include <nanobind/operators.h>
#include <nanobind/stl/complex.h>     // NOLINT(misc-include-cleaner)
#include <nanobind/stl/filesystem.h>  // NOLINT(misc-include-cleaner)
#include <nanobind/stl/map.h>         // NOLINT(misc-include-cleaner)
#include <nanobind/stl/optional.h>    // NOLINT(misc-include-cleaner)
#include <nanobind/stl/pair.h>        // NOLINT(misc-include-cleaner)
#include <nanobind/stl/string.h>      // NOLINT(misc-include-cleaner)
#include <nanobind/stl/string_view.h> // NOLINT(misc-include-cleaner)
#include <nanobind/stl/tuple.h>       // NOLINT(misc-include-cleaner)
#include <nanobind/stl/variant.h>     // NOLINT(misc-include-cleaner)
#include <nanobind/stl/vector.h>      // NOLINT(misc-include-cleaner)
#include <qdmi/client.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace mqt {

namespace nb = nanobind;
using namespace nb::literals;

namespace bindings {
void registerSlurm(nb::module_& qdmiModule);
}

namespace {
void copyProgramFormatField(char (&destination)[64],
                            const std::string_view value,
                            const std::string_view field) {
  if (value.size() >= std::size(destination) ||
      value.find('\0') != std::string_view::npos) {
    throw nb::value_error(
        (std::string(field) + " must contain fewer than 64 non-NUL bytes")
            .c_str());
  }
  std::ranges::copy(value, std::span{destination}.begin());
}

template <size_t N>
[[nodiscard]] std::string_view decodeFixedField(const char (&value)[N],
                                                const std::string_view field) {
  if (!qdmi::detail::isCanonicalFixedString(value)) {
    throw nb::value_error(
        (std::string(field) + " is not a canonical fixed string").c_str());
  }
  const auto begin = std::cbegin(value);
  const auto terminator = std::find(begin, std::cend(value), '\0');
  return {begin, static_cast<size_t>(terminator - begin)};
}

QDMI_Program_Format makeProgramFormat(const std::string_view id,
                                      const uint32_t major,
                                      const uint32_t minor,
                                      const uint32_t patch,
                                      const std::string_view profile,
                                      const QDMI_Program_Encoding encoding) {
  if (id.empty()) {
    throw nb::value_error("id must not be empty");
  }
  if (major > 0x3FFU || minor > 0x3FFU || patch > 0xFFFU) {
    throw nb::value_error("version components exceed the QDMI packed range");
  }
  if (major == 0U && minor == 0U && patch == 0U) {
    throw nb::value_error("version must not be zero");
  }
  QDMI_Program_Format format{.version = QDMI_MAKE_VERSION(major, minor, patch),
                             .encoding = static_cast<uint32_t>(encoding),
                             .id = {},
                             .profile = {}};
  copyProgramFormatField(format.id, id, "id");
  copyProgramFormatField(format.profile, profile, "profile");
  return format;
}

qdmi::SessionConfig makeClientSessionConfig(
    std::optional<std::filesystem::path> driverPath,
    std::optional<std::string> token,
    std::optional<std::filesystem::path> authFile,
    std::optional<std::string> authUrl, std::optional<std::string> username,
    std::optional<std::string> password, std::optional<std::string> projectId,
    std::optional<std::string> custom1, std::optional<std::string> custom2,
    std::optional<std::string> custom3, std::optional<std::string> custom4,
    std::optional<std::string> custom5) {
  return {.driverPath = std::move(driverPath),
          .token = std::move(token),
          .authFile = std::move(authFile),
          .authUrl = std::move(authUrl),
          .username = std::move(username),
          .password = std::move(password),
          .projectId = std::move(projectId),
          .custom1 = std::move(custom1),
          .custom2 = std::move(custom2),
          .custom3 = std::move(custom3),
          .custom4 = std::move(custom4),
          .custom5 = std::move(custom5)};
}

template <typename Query>
[[nodiscard]] nb::object queryCustomValue(Query query,
                                          const nb::handle valueType) {
  const auto returnValue =
      []<typename T>(std::optional<T> value) -> nb::object {
    if (!value.has_value()) {
      return nb::none();
    }
    return nb::cast(std::move(*value));
  };

  const auto builtins = nb::builtins();
  if (valueType.is(builtins["str"])) {
    return returnValue(query.template operator()<std::string>());
  }
  if (valueType.is(builtins["bool"])) {
    return returnValue(query.template operator()<bool>());
  }
  if (valueType.is(builtins["int"])) {
    return returnValue(query.template operator()<int>());
  }
  if (valueType.is(builtins["float"])) {
    return returnValue(query.template operator()<double>());
  }
  if (valueType.is(builtins["bytes"])) {
    const auto value = query.template operator()<std::vector<std::byte>>();
    if (!value.has_value()) {
      return nb::none();
    }
    return nb::bytes(reinterpret_cast<const char*>(value->data()),
                     value->size());
  }
  throw nb::type_error(
      "value_type must be exactly str, bool, int, float, or bytes");
}

} // namespace

NB_MODULE(MQT_CORE_MODULE_NAME, qdmiModule) {
  qdmiModule.doc() = "QDMI Client entities.";
  bindings::registerSlurm(qdmiModule);

  nb::class_<qdmi::Session>(qdmiModule, "ClientSession",
                            "One initialized QDMI Client session.")
      .def(
          "__init__",
          [](qdmi::Session* self,
             std::optional<std::filesystem::path> driverPath,
             std::optional<std::string> token,
             std::optional<std::filesystem::path> authFile,
             std::optional<std::string> authUrl,
             std::optional<std::string> username,
             std::optional<std::string> password,
             std::optional<std::string> projectId,
             std::optional<std::string> custom1,
             std::optional<std::string> custom2,
             std::optional<std::string> custom3,
             std::optional<std::string> custom4,
             std::optional<std::string> custom5) {
            new (self) qdmi::Session(makeClientSessionConfig(
                std::move(driverPath), std::move(token), std::move(authFile),
                std::move(authUrl), std::move(username), std::move(password),
                std::move(projectId), std::move(custom1), std::move(custom2),
                std::move(custom3), std::move(custom4), std::move(custom5)));
          },
          nb::kw_only(), "driver_path"_a = std::nullopt,
          "token"_a = std::nullopt, "auth_file"_a = std::nullopt,
          "auth_url"_a = std::nullopt, "username"_a = std::nullopt,
          "password"_a = std::nullopt, "project_id"_a = std::nullopt,
          "custom1"_a = std::nullopt, "custom2"_a = std::nullopt,
          "custom3"_a = std::nullopt, "custom4"_a = std::nullopt,
          "custom5"_a = std::nullopt)
      .def_prop_ro("devices", &qdmi::Session::getDevices,
                   "The devices visible to this authenticated session.");

  qdmiModule.def(
      "open_device",
      [](const std::string& deviceId,
         std::optional<std::filesystem::path> driverPath,
         std::optional<std::string> token,
         std::optional<std::filesystem::path> authFile,
         std::optional<std::string> authUrl,
         std::optional<std::string> username,
         std::optional<std::string> password,
         std::optional<std::string> projectId,
         std::optional<std::string> custom1, std::optional<std::string> custom2,
         std::optional<std::string> custom3, std::optional<std::string> custom4,
         std::optional<std::string> custom5) {
        return qdmi::Session::openDevice(
            deviceId,
            makeClientSessionConfig(
                std::move(driverPath), std::move(token), std::move(authFile),
                std::move(authUrl), std::move(username), std::move(password),
                std::move(projectId), std::move(custom1), std::move(custom2),
                std::move(custom3), std::move(custom4), std::move(custom5)));
      },
      "device_id"_a, nb::kw_only(), "driver_path"_a = std::nullopt,
      "token"_a = std::nullopt, "auth_file"_a = std::nullopt,
      "auth_url"_a = std::nullopt, "username"_a = std::nullopt,
      "password"_a = std::nullopt, "project_id"_a = std::nullopt,
      "custom1"_a = std::nullopt, "custom2"_a = std::nullopt,
      "custom3"_a = std::nullopt, "custom4"_a = std::nullopt,
      "custom5"_a = std::nullopt,
      "Open a Client-visible device by stable ID in a fresh session.");

  // Job class
  auto job = nb::class_<qdmi::Job>(
      qdmiModule, "Job",
      "A job represents a submitted quantum program execution.");

  job.def("check", &qdmi::Job::check, "Returns the current status of the job.");

  job.def("wait", &qdmi::Job::wait, "timeout"_a = 0,
          R"pb(Waits for the job to complete.

Args:
    timeout: The maximum time to wait in seconds. If 0, waits indefinitely.

Returns:
    True if the job completed within the timeout, False otherwise.)pb");

  job.def("cancel", &qdmi::Job::cancel, "Cancels the job.");

  job.def("get_shots", &qdmi::Job::getShots, "program_index"_a = 0U,
          "Returns the raw shot results from the job.");

  job.def("get_counts", &qdmi::Job::getCounts, "program_index"_a = 0U,
          "Returns the measurement counts from the job.");

  job.def(
      "get_results",
      [](const qdmi::Job& self, const size_t programIndex,
         const QDMI_Job_Result result) {
        const auto value = self.getResults(programIndex, result);
        return nb::bytes(reinterpret_cast<const char*>(value.data()),
                         value.size());
      },
      "program_index"_a, "result"_a,
      "Returns one indexed result as exact bytes.");

  job.def(
      "get_program_output",
      [](const qdmi::Job& self, const size_t programIndex) {
        const auto output = self.getProgramOutput(programIndex);
        return nb::bytes(reinterpret_cast<const char*>(output.data()),
                         output.size());
      },
      "program_index"_a = 0U,
      "Returns the exact format-defined program output bytes.");

  job.def("get_dense_statevector", &qdmi::Job::getDenseStateVector,
          "program_index"_a = 0U,
          "Returns the dense statevector from the job (typically only "
          "available from simulator devices).");

  job.def("get_dense_probabilities", &qdmi::Job::getDenseProbabilities,
          "program_index"_a = 0U,
          "Returns the dense probabilities from the job (typically only "
          "available from simulator devices).");

  job.def("get_sparse_statevector", &qdmi::Job::getSparseStateVector,
          "program_index"_a = 0U,
          "Returns the sparse statevector from the job (typically only "
          "available from simulator devices).");

  job.def("get_sparse_probabilities", &qdmi::Job::getSparseProbabilities,
          "program_index"_a = 0U,
          "Returns the sparse probabilities from the job (typically only "
          "available from simulator devices).");

  job.def(
      "query_custom_property",
      [](const qdmi::Job& self, const qdmi::CustomProperty customProperty,
         const nb::handle valueType) {
        return queryCustomValue(
            [&self, customProperty]<qdmi::custom_property_value T>() {
              return self.queryCustomProperty<T>(customProperty);
            },
            valueType);
      },
      "custom_property"_a, "value_type"_a,
      nb::sig("def query_custom_property(self, custom_property: "
              "CustomProperty, "
              "value_type: type[str] | type[bool] | type[int] | type[float] | "
              "type[bytes]) -> str | bool | int | float | bytes | None"),
      R"pb(Query an implementation-defined custom job property.

The caller must provide the type documented by the device implementation.
Use ``bytes`` to retrieve the value without interpretation. Returns ``None``
when the custom slot is unsupported.)pb");

  job.def(
      "get_custom_result",
      [](const qdmi::Job& self, const qdmi::CustomProperty customProperty,
         const nb::handle valueType, const size_t programIndex) {
        return queryCustomValue(
            [&self, customProperty,
             programIndex]<qdmi::custom_property_value T>() {
              return self.getCustomResult<T>(customProperty, programIndex);
            },
            valueType);
      },
      "custom_property"_a, "value_type"_a, "program_index"_a = 0U,
      nb::sig("def get_custom_result(self, custom_property: CustomProperty, "
              "value_type: type[str] | type[bool] | type[int] | type[float] | "
              "type[bytes], program_index: int = 0) -> str | bool | int | "
              "float | bytes | None"),
      R"pb(Return an implementation-defined custom job result.

The caller must provide the type documented by the device implementation.
Use ``bytes`` to retrieve the value without interpretation. Returns ``None``
when the custom slot is unsupported.)pb");

  job.def_prop_ro("id", &qdmi::Job::getId, "The job ID.");

  job.def_prop_ro("program_format", &qdmi::Job::getProgramFormat,
                  "The format of the submitted program.");

  job.def_prop_ro("program", &qdmi::Job::getProgram, "The submitted program.");

  job.def_prop_ro(
      "program_bytes",
      [](const qdmi::Job& self) {
        const auto program = self.getProgramBytes();
        return nb::bytes(program.data(), program.size());
      },
      "The exact bytes of the submitted program.");

  job.def_prop_ro("num_shots", &qdmi::Job::getNumShots, "The number of shots.");

  job.def_prop_ro("programs_num", &qdmi::Job::getProgramsNum,
                  "The number of programs in the job.");

  job.def_prop_ro(
      "queue_position", &qdmi::Job::getQueuePosition,
      "The number of jobs ahead in the queue, or None if unavailable or not "
      "applicable in the current state.");

  job.def(nb::self == nb::self,
          nb::sig("def __eq__(self, arg: object, /) -> bool"));
  job.def(nb::self != nb::self,
          nb::sig("def __ne__(self, arg: object, /) -> bool"));

  // JobStatus enum
  nb::enum_<QDMI_Job_Status>(job, "Status", "Enumeration of job status.")
      .value("CREATED", QDMI_JOB_STATUS_CREATED)
      .value("SUBMITTED", QDMI_JOB_STATUS_SUBMITTED)
      .value("QUEUED", QDMI_JOB_STATUS_QUEUED)
      .value("RUNNING", QDMI_JOB_STATUS_RUNNING)
      .value("DONE", QDMI_JOB_STATUS_DONE)
      .value("CANCELED", QDMI_JOB_STATUS_CANCELED)
      .value("FAILED", QDMI_JOB_STATUS_FAILED);

  nb::enum_<QDMI_Job_Result>(job, "Result", "One raw job result format.")
      .value("SHOTS", QDMI_JOB_RESULT_SHOTS)
      .value("HIST_KEYS", QDMI_JOB_RESULT_HIST_KEYS)
      .value("HIST_VALUES", QDMI_JOB_RESULT_HIST_VALUES)
      .value("STATEVECTOR_DENSE", QDMI_JOB_RESULT_STATEVECTOR_DENSE)
      .value("PROBABILITIES_DENSE", QDMI_JOB_RESULT_PROBABILITIES_DENSE)
      .value("STATEVECTOR_SPARSE_KEYS", QDMI_JOB_RESULT_STATEVECTOR_SPARSE_KEYS)
      .value("STATEVECTOR_SPARSE_VALUES",
             QDMI_JOB_RESULT_STATEVECTOR_SPARSE_VALUES)
      .value("PROBABILITIES_SPARSE_KEYS",
             QDMI_JOB_RESULT_PROBABILITIES_SPARSE_KEYS)
      .value("PROBABILITIES_SPARSE_VALUES",
             QDMI_JOB_RESULT_PROBABILITIES_SPARSE_VALUES)
      .value("PROGRAM_OUTPUT", QDMI_JOB_RESULT_PROGRAMOUTPUT);

  nb::enum_<QDMI_Program_Encoding>(qdmiModule, "ProgramEncoding",
                                   "Program payload encoding.")
      .value("TEXT", QDMI_PROGRAM_ENCODING_TEXT)
      .value("BINARY", QDMI_PROGRAM_ENCODING_BINARY);

  auto programFormat = nb::class_<QDMI_Program_Format>(
      qdmiModule, "ProgramFormat",
      "The exact format, version, profile, and encoding of a payload.");
  programFormat
      .def(
          "__init__",
          [](QDMI_Program_Format* self, const std::string_view id,
             const std::tuple<uint32_t, uint32_t, uint32_t>& version,
             const std::string_view profile,
             const QDMI_Program_Encoding encoding) {
            new (self) QDMI_Program_Format(makeProgramFormat(
                id, std::get<0>(version), std::get<1>(version),
                std::get<2>(version), profile, encoding));
          },
          "format_id"_a, "version"_a, "profile"_a = "",
          "encoding"_a = QDMI_PROGRAM_ENCODING_TEXT)
      .def_prop_ro("format_id",
                   [](const QDMI_Program_Format& self) {
                     return std::string(
                         decodeFixedField(self.id, "program format ID"));
                   })
      .def_prop_ro("version",
                   [](const QDMI_Program_Format& self) {
                     return std::tuple{QDMI_VERSION_MAJOR(self.version),
                                       QDMI_VERSION_MINOR(self.version),
                                       QDMI_VERSION_PATCH(self.version)};
                   })
      .def_prop_ro("profile",
                   [](const QDMI_Program_Format& self) {
                     return std::string(
                         decodeFixedField(self.profile, "program profile"));
                   })
      .def_prop_ro("encoding",
                   [](const QDMI_Program_Format& self) {
                     return static_cast<QDMI_Program_Encoding>(self.encoding);
                   })
      .def(
          "__eq__",
          [](const QDMI_Program_Format& self, const nb::handle other) {
            return nb::isinstance<QDMI_Program_Format>(other) &&
                   qdmi::equal(self, nb::cast<QDMI_Program_Format>(other));
          },
          nb::sig("def __eq__(self, arg: object, /) -> bool"))
      .def("__hash__", [](const QDMI_Program_Format& self) {
        size_t hash = std::hash<std::string_view>{}(
            decodeFixedField(self.id, "program format ID"));
        hash ^= static_cast<size_t>(self.version) << 1U;
        hash ^= static_cast<size_t>(self.encoding) << 3U;
        hash ^= std::hash<std::string_view>{}(
                    decodeFixedField(self.profile, "program profile"))
                << 5U;
        return hash;
      });
  programFormat
      .def_prop_ro_static(
          "OPENQASM2", [](nb::handle) { return qdmi::OPENQASM2; },
          "The canonical OpenQASM 2.0 text format.")
      .def_prop_ro_static(
          "OPENQASM3", [](nb::handle) { return qdmi::OPENQASM3; },
          "The canonical OpenQASM 3.0 text format.")
      .def_prop_ro_static(
          "QIR21_BASE_TEXT", [](nb::handle) { return qdmi::QIR21_BASE_TEXT; },
          "The canonical QIR 2.1 Base Profile text format.")
      .def_prop_ro_static(
          "QIR21_BASE_BINARY",
          [](nb::handle) { return qdmi::QIR21_BASE_BINARY; },
          "The canonical QIR 2.1 Base Profile binary format.")
      .def_prop_ro_static(
          "QIR21_ADAPTIVE_TEXT",
          [](nb::handle) { return qdmi::QIR21_ADAPTIVE_TEXT; },
          "The canonical QIR 2.1 Adaptive Profile text format.")
      .def_prop_ro_static(
          "QIR21_ADAPTIVE_BINARY",
          [](nb::handle) { return qdmi::QIR21_ADAPTIVE_BINARY; },
          "The canonical QIR 2.1 Adaptive Profile binary "
          "format.");

  nb::class_<QDMI_Program_Feature>(
      qdmiModule, "ProgramFeature",
      "One exact feature or constraint record for a program format.")
      .def_prop_ro("id",
                   [](const QDMI_Program_Feature& self) {
                     return std::string(
                         decodeFixedField(self.id, "program feature ID"));
                   })
      .def_ro("value", &QDMI_Program_Feature::value)
      .def_prop_ro("constraint_id",
                   [](const QDMI_Program_Feature& self) {
                     return std::string(decodeFixedField(
                         self.constraint_id, "program constraint ID"));
                   })
      .def_ro("constraint_value", &QDMI_Program_Feature::constraint_value)
      .def(
          "__eq__",
          [](const QDMI_Program_Feature& self, const nb::handle other) {
            if (!nb::isinstance<QDMI_Program_Feature>(other)) {
              return false;
            }
            const auto value = nb::cast<QDMI_Program_Feature>(other);
            return std::ranges::equal(self.id, value.id) &&
                   self.value == value.value &&
                   std::ranges::equal(self.constraint_id,
                                      value.constraint_id) &&
                   self.constraint_value == value.constraint_value;
          },
          nb::sig("def __eq__(self, arg: object, /) -> bool"))
      .def("__hash__", [](const QDMI_Program_Feature& self) {
        size_t hash = std::hash<std::string_view>{}(
            decodeFixedField(self.id, "program feature ID"));
        hash ^= static_cast<size_t>(self.value) << 1U;
        hash ^= std::hash<std::string_view>{}(decodeFixedField(
                    self.constraint_id, "program constraint ID"))
                << 3U;
        hash ^= static_cast<size_t>(self.constraint_value) << 5U;
        return hash;
      });

  qdmiModule.def("is_binary_program_format", &qdmi::isBinaryProgramFormat,
                 "program_format"_a,
                 R"pb(Returns whether a program format carries a binary payload.

Binary payloads may contain null bytes. Pass ``bytes`` to
:meth:`Device.submit_job` for binary descriptors and ``str`` for text.

Args:
    program_format: The program format to classify.

Returns:
    True if the format requires exact-byte submission.)pb");

  nb::enum_<qdmi::CustomProperty>(
      qdmiModule, "CustomProperty",
      "An implementation-defined custom property or result slot.")
      .value("CUSTOM1", qdmi::CustomProperty::Custom1)
      .value("CUSTOM2", qdmi::CustomProperty::Custom2)
      .value("CUSTOM3", qdmi::CustomProperty::Custom3)
      .value("CUSTOM4", qdmi::CustomProperty::Custom4)
      .value("CUSTOM5", qdmi::CustomProperty::Custom5);

  // Device class
  auto device = nb::class_<qdmi::Device>(
      qdmiModule, "Device",
      "A device represents a quantum device with its properties and "
      "capabilities.");

  nb::enum_<QDMI_Device_Status>(device, "Status",
                                "Enumeration of device status.")
      .value("OFFLINE", QDMI_DEVICE_STATUS_OFFLINE)
      .value("IDLE", QDMI_DEVICE_STATUS_IDLE)
      .value("BUSY", QDMI_DEVICE_STATUS_BUSY)
      .value("ERROR", QDMI_DEVICE_STATUS_ERROR)
      .value("MAINTENANCE", QDMI_DEVICE_STATUS_MAINTENANCE)
      .value("CALIBRATION", QDMI_DEVICE_STATUS_CALIBRATION);

  device.def("name", &qdmi::Device::getName, "Returns the name of the device.");

  device.def_prop_ro("id", &qdmi::Device::getId,
                     "The stable Client-visible device ID.");

  device.def("version", &qdmi::Device::getVersion,
             "Returns the version of the device.");

  device.def("status", &qdmi::Device::getStatus,
             "Returns the current status of the device.");

  device.def("library_version", &qdmi::Device::getLibraryVersion,
             "Returns the version of the library used to define the device.");

  device.def("qubits_num", &qdmi::Device::getQubitsNum,
             "Returns the number of qubits available on the device.");

  device.def("sites", &qdmi::Device::getSites,
             "Returns the list of all sites (zone and regular sites) available "
             "on the device.");

  device.def("regular_sites", &qdmi::Device::getRegularSites,
             "Returns the list of regular sites (without zone sites) available "
             "on the device.");

  device.def("zones", &qdmi::Device::getZones,
             "Returns the list of zone sites (without regular sites) available "
             "on the device.");

  device.def("operations", &qdmi::Device::getOperations,
             "Returns the list of operations supported by the device.");

  device.def("coupling_map", &qdmi::Device::getCouplingMap,
             "Returns the coupling map of the device as a list of site pairs.");

  device.def("needs_calibration", &qdmi::Device::getNeedsCalibration,
             "Returns whether the device needs calibration.");

  device.def("queue_length", &qdmi::Device::getQueueLength,
             "Returns the current queue length, or None if unavailable.");

  device.def("length_unit", &qdmi::Device::getLengthUnit,
             "Returns the unit of length used by the device.");

  device.def("length_scale_factor", &qdmi::Device::getLengthScaleFactor,
             "Returns the scale factor for length used by the device.");

  device.def("duration_unit", &qdmi::Device::getDurationUnit,
             "Returns the unit of duration used by the device.");

  device.def("duration_scale_factor", &qdmi::Device::getDurationScaleFactor,
             "Returns the scale factor for duration used by the device.");

  device.def("min_atom_distance", &qdmi::Device::getMinAtomDistance,
             "Returns the minimum atom distance on the device.");

  device.def("supported_program_formats",
             &qdmi::Device::getSupportedProgramFormats,
             "Returns the list of program formats supported by the device.");

  device.def("try_program_features", &qdmi::Device::tryGetProgramFeatures,
             "program_format"_a,
             "Returns the complete optional capability list for an exact "
             "payload, or None when the metadata is unknown.");

  device.def("child_devices", &qdmi::Device::getChildDevices,
             "Returns the direct child devices managed by this device.");

  device.def(
      "query_custom_operations", &qdmi::Device::queryCustomOperations,
      "custom_property"_a,
      R"pb(Query a custom device property that contains operation handles.

Returns normal :class:`Device.Operation` objects, or ``None`` when the custom
slot is unsupported. A supported empty list is returned as an empty list.)pb");

  device.def(
      "query_custom_property",
      [](const qdmi::Device& self, const qdmi::CustomProperty customProperty,
         const nb::handle valueType) {
        return queryCustomValue(
            [&self, customProperty]<qdmi::custom_property_value T>() {
              return self.queryCustomProperty<T>(customProperty);
            },
            valueType);
      },
      "custom_property"_a, "value_type"_a,
      nb::sig("def query_custom_property(self, custom_property: "
              "CustomProperty, "
              "value_type: type[str] | type[bool] | type[int] | type[float] | "
              "type[bytes]) -> str | bool | int | float | bytes | None"),
      R"pb(Query an implementation-defined custom device property.

The caller must provide the type documented by the device implementation.
Use ``bytes`` to retrieve the value without interpretation. Returns ``None``
when the custom slot is unsupported.)pb");

  device.def(
      "submit_job",
      [](const qdmi::Device& self, const std::string& program,
         const QDMI_Program_Format format, const size_t numShots,
         const std::optional<qdmi::CustomJobParameter>& custom1,
         const std::optional<qdmi::CustomJobParameter>& custom2,
         const std::optional<qdmi::CustomJobParameter>& custom3,
         const std::optional<qdmi::CustomJobParameter>& custom4,
         const std::optional<qdmi::CustomJobParameter>& custom5) {
        return self.submitJob(program, format, numShots, custom1, custom2,
                              custom3, custom4, custom5);
      },
      "program"_a, "program_format"_a, "num_shots"_a, nb::kw_only(),
      "custom1"_a = nb::none(), "custom2"_a = nb::none(),
      "custom3"_a = nb::none(), "custom4"_a = nb::none(),
      "custom5"_a = nb::none(), nb::rv_policy::reference_internal,
      "Submits a text job to the device.");

  device.def(
      "submit_job",
      [](const qdmi::Device& self, const nb::bytes& program,
         const QDMI_Program_Format format, const size_t numShots,
         const std::optional<qdmi::CustomJobParameter>& custom1,
         const std::optional<qdmi::CustomJobParameter>& custom2,
         const std::optional<qdmi::CustomJobParameter>& custom3,
         const std::optional<qdmi::CustomJobParameter>& custom4,
         const std::optional<qdmi::CustomJobParameter>& custom5) {
        const auto bytes = std::span{
            static_cast<const std::byte*>(program.data()), program.size()};
        return self.submitJob(bytes, format, numShots, custom1, custom2,
                              custom3, custom4, custom5);
      },
      "program"_a, "program_format"_a, "num_shots"_a, nb::kw_only(),
      "custom1"_a = nb::none(), "custom2"_a = nb::none(),
      "custom3"_a = nb::none(), "custom4"_a = nb::none(),
      "custom5"_a = nb::none(), nb::rv_policy::reference_internal,
      "Submits an exact byte payload to the device.");

  device.def(
      "submit_programs",
      [](const qdmi::Device& self, const std::vector<std::string>& programs,
         const QDMI_Program_Format format, const size_t numShots,
         const std::optional<qdmi::CustomJobParameter>& custom1,
         const std::optional<qdmi::CustomJobParameter>& custom2,
         const std::optional<qdmi::CustomJobParameter>& custom3,
         const std::optional<qdmi::CustomJobParameter>& custom4,
         const std::optional<qdmi::CustomJobParameter>& custom5) {
        return self.submitPrograms(programs, format, numShots, custom1, custom2,
                                   custom3, custom4, custom5);
      },
      "programs"_a, "program_format"_a, "num_shots"_a, nb::kw_only(),
      "custom1"_a = nb::none(), "custom2"_a = nb::none(),
      "custom3"_a = nb::none(), "custom4"_a = nb::none(),
      "custom5"_a = nb::none(), nb::rv_policy::reference_internal,
      "Submits an ordered list of text programs atomically.");

  device.def(
      "submit_programs",
      [](const qdmi::Device& self, const std::vector<nb::bytes>& programs,
         const QDMI_Program_Format format, const size_t numShots,
         const std::optional<qdmi::CustomJobParameter>& custom1,
         const std::optional<qdmi::CustomJobParameter>& custom2,
         const std::optional<qdmi::CustomJobParameter>& custom3,
         const std::optional<qdmi::CustomJobParameter>& custom4,
         const std::optional<qdmi::CustomJobParameter>& custom5) {
        std::vector<std::vector<std::byte>> bytes;
        bytes.reserve(programs.size());
        for (const auto& program : programs) {
          const std::span value{static_cast<const std::byte*>(program.data()),
                                program.size()};
          bytes.emplace_back(value.begin(), value.end());
        }
        return self.submitPrograms(bytes, format, numShots, custom1, custom2,
                                   custom3, custom4, custom5);
      },
      "programs"_a, "program_format"_a, "num_shots"_a, nb::kw_only(),
      "custom1"_a = nb::none(), "custom2"_a = nb::none(),
      "custom3"_a = nb::none(), "custom4"_a = nb::none(),
      "custom5"_a = nb::none(), nb::rv_policy::reference_internal,
      "Submits an ordered list of exact byte programs atomically.");

  device.def(
      "retrieve_job_by_id",
      [](const qdmi::Device& self, const std::string& jobId) {
        return self.retrieveJobById(jobId);
      },
      "job_id"_a, nb::rv_policy::reference_internal,
      "Retrieves an existing job by its device-provided ID.");

  device.def("__repr__", [](const qdmi::Device& dev) {
    return "<Device name=\"" + dev.getName() + "\">";
  });

  device.def(nb::self == nb::self,
             nb::sig("def __eq__(self, arg: object, /) -> bool"));
  device.def(nb::self != nb::self,
             nb::sig("def __ne__(self, arg: object, /) -> bool"));

  // Site class
  auto site = nb::class_<qdmi::Site>(
      device, "Site",
      "A site represents a potential qubit location on a quantum device.");

  site.def("index", &qdmi::Site::getIndex, "Returns the index of the site.");

  site.def("t1", &qdmi::Site::getT1,
           "Returns the T1 coherence time of the site.");

  site.def("t2", &qdmi::Site::getT2,
           "Returns the T2 coherence time of the site.");

  site.def("name", &qdmi::Site::getName, "Returns the name of the site.");

  site.def("x_coordinate", &qdmi::Site::getXCoordinate,
           "Returns the x coordinate of the site.");

  site.def("y_coordinate", &qdmi::Site::getYCoordinate,
           "Returns the y coordinate of the site.");

  site.def("z_coordinate", &qdmi::Site::getZCoordinate,
           "Returns the z coordinate of the site.");

  site.def("is_zone", &qdmi::Site::isZone,
           "Returns whether the site is a zone.");

  site.def("x_extent", &qdmi::Site::getXExtent,
           "Returns the x extent of the site.");

  site.def("y_extent", &qdmi::Site::getYExtent,
           "Returns the y extent of the site.");

  site.def("z_extent", &qdmi::Site::getZExtent,
           "Returns the z extent of the site.");

  site.def("module_index", &qdmi::Site::getModuleIndex,
           "Returns the index of the module the site belongs to.");

  site.def("submodule_index", &qdmi::Site::getSubmoduleIndex,
           "Returns the index of the submodule the site belongs to.");

  site.def(
      "query_custom_property",
      [](const qdmi::Site& self, const qdmi::CustomProperty customProperty,
         const nb::handle valueType) {
        return queryCustomValue(
            [&self, customProperty]<qdmi::custom_property_value T>() {
              return self.queryCustomProperty<T>(customProperty);
            },
            valueType);
      },
      "custom_property"_a, "value_type"_a,
      nb::sig("def query_custom_property(self, custom_property: "
              "CustomProperty, "
              "value_type: type[str] | type[bool] | type[int] | type[float] | "
              "type[bytes]) -> str | bool | int | float | bytes | None"),
      R"pb(Query an implementation-defined custom site property.

The caller must provide the type documented by the device implementation.
Use ``bytes`` to retrieve the value without interpretation. Returns ``None``
when the custom slot is unsupported.)pb");

  site.def("__repr__", [](const qdmi::Site& s) {
    return "<Site index=" + std::to_string(s.getIndex()) + ">";
  });

  site.def(nb::self == nb::self,
           nb::sig("def __eq__(self, arg: object, /) -> bool"));
  site.def(nb::self != nb::self,
           nb::sig("def __ne__(self, arg: object, /) -> bool"));
  // Operation class
  auto operation = nb::class_<qdmi::Operation>(
      device, "Operation",
      "An operation represents a quantum operation that can be performed on a "
      "quantum device.");

  operation.def("name", &qdmi::Operation::getName,
                "sites"_a.sig("...") = std::vector<qdmi::Site>{},
                "params"_a.sig("...") = std::vector<double>{},
                "Returns the name of the operation.");

  operation.def("qubits_num", &qdmi::Operation::getQubitsNum,
                "sites"_a.sig("...") = std::vector<qdmi::Site>{},
                "params"_a.sig("...") = std::vector<double>{},
                "Returns the number of qubits the operation acts on.");

  operation.def("parameters_num", &qdmi::Operation::getParametersNum,
                "sites"_a.sig("...") = std::vector<qdmi::Site>{},
                "params"_a.sig("...") = std::vector<double>{},
                "Returns the number of parameters the operation has.");

  operation.def("duration", &qdmi::Operation::getDuration,
                "sites"_a.sig("...") = std::vector<qdmi::Site>{},
                "params"_a.sig("...") = std::vector<double>{},
                "Returns the duration of the operation.");

  operation.def("fidelity", &qdmi::Operation::getFidelity,
                "sites"_a.sig("...") = std::vector<qdmi::Site>{},
                "params"_a.sig("...") = std::vector<double>{},
                "Returns the fidelity of the operation.");

  operation.def("interaction_radius", &qdmi::Operation::getInteractionRadius,
                "sites"_a.sig("...") = std::vector<qdmi::Site>{},
                "params"_a.sig("...") = std::vector<double>{},
                "Returns the interaction radius of the operation.");

  operation.def("blocking_radius", &qdmi::Operation::getBlockingRadius,
                "sites"_a.sig("...") = std::vector<qdmi::Site>{},
                "params"_a.sig("...") = std::vector<double>{},
                "Returns the blocking radius of the operation.");

  operation.def("idling_fidelity", &qdmi::Operation::getIdlingFidelity,
                "sites"_a.sig("...") = std::vector<qdmi::Site>{},
                "params"_a.sig("...") = std::vector<double>{},
                "Returns the idling fidelity of the operation.");

  operation.def("is_zoned", &qdmi::Operation::isZoned,
                "Returns whether the operation is zoned.");

  operation.def("sites", &qdmi::Operation::getSites,
                "Returns the list of sites the operation can be performed on.");

  operation.def("site_pairs", &qdmi::Operation::getSitePairs,
                "Returns the list of site pairs the local 2-qubit operation "
                "can be performed on.");

  operation.def("mean_shuttling_speed", &qdmi::Operation::getMeanShuttlingSpeed,
                "sites"_a.sig("...") = std::vector<qdmi::Site>{},
                "params"_a.sig("...") = std::vector<double>{},
                "Returns the mean shuttling speed of the operation.");

  operation.def(
      "query_custom_property",
      [](const qdmi::Operation& self, const qdmi::CustomProperty customProperty,
         const nb::handle valueType, const std::vector<qdmi::Site>& sites,
         const std::vector<double>& params) {
        return queryCustomValue(
            [&self, customProperty, &sites,
             &params]<qdmi::custom_property_value T>() {
              return self.queryCustomProperty<T>(customProperty, sites, params);
            },
            valueType);
      },
      "custom_property"_a, "value_type"_a,
      "sites"_a.sig("...") = std::vector<qdmi::Site>{},
      "params"_a.sig("...") = std::vector<double>{},
      nb::sig("def query_custom_property(self, custom_property: "
              "CustomProperty, "
              "value_type: type[str] | type[bool] | type[int] | type[float] | "
              "type[bytes], sites: Sequence[mqt.core.qdmi.Device.Site] = "
              "..., params: Sequence[float] = ...) -> str | bool | int | "
              "float | bytes | None"),
      R"pb(Query an implementation-defined custom operation property.

The caller must provide the type documented by the device implementation.
Use ``bytes`` to retrieve the value without interpretation. Returns ``None``
when the custom slot is unsupported.)pb");

  operation.def("__repr__", [](const qdmi::Operation& op) {
    return "<Operation name=\"" + op.getName() + "\">";
  });

  operation.def(nb::self == nb::self,
                nb::sig("def __eq__(self, arg: object, /) -> bool"));
  operation.def(nb::self != nb::self,
                nb::sig("def __ne__(self, arg: object, /) -> bool"));
}

} // namespace mqt
