/*
 * Copyright (c) 2023 - 2026 Chair for Design Automation, TUM
 * Copyright (c) 2025 - 2026 Munich Quantum Software Company GmbH
 * All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License
 */

#pragma once

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/SmallVector.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/Value.h>
#include <mlir/Support/LLVM.h>
#include <mlir/Support/LogicalResult.h>

#include <cstdint>
#include <iterator>

namespace mlir::qtensor {

/**
 * @brief A bidirectional_iterator traversing the tensor chain.
 **/
class [[nodiscard]] TensorIterator {
public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = Operation*;

  TensorIterator()
      : op_(nullptr), tensor_(nullptr), isFinal_(false), isSentinel_(false) {}
  explicit TensorIterator(TypedValue<RankedTensorType> tensor)
      : op_(tensor.getDefiningOp()), tensor_(tensor), isFinal_(false),
        isSentinel_(false) {}

  /// @returns the operation the iterator points to.
  [[nodiscard]] Operation* operation() const { return op_; }

  /// @returns the operation the iterator points to.
  [[nodiscard]] Operation* operator*() const { return operation(); }

  /// @returns the tensor the iterator points to.
  [[nodiscard]] TypedValue<RankedTensorType> tensor() const;

  TensorIterator& operator++() {
    forward();
    return *this;
  }

  TensorIterator operator++(int) {
    auto tmp = *this;
    operator++();
    return tmp;
  }

  TensorIterator& operator--() {
    backward();
    return *this;
  }

  TensorIterator operator--(int) {
    auto tmp = *this;
    operator--();
    return tmp;
  }

  bool operator==(const TensorIterator& other) const {
    return other.tensor_ == tensor_ && other.op_ == op_ &&
           other.isSentinel_ == isSentinel_;
  }

  bool operator==([[maybe_unused]] std::default_sentinel_t s) const {
    return isSentinel_;
  }

private:
  /// @brief Move to the next operation on the tensor def-use chain.
  void forward();

  /// @brief Move to the previous operation on the tensor def-use chain.
  void backward();

  Operation* op_;
  TypedValue<RankedTensorType> tensor_;
  bool isFinal_;
  bool isSentinel_;
};

/**
 * @brief Which qubit-tensor result of a call continues which of its operands.
 *
 * @details
 * The tensor counterpart of `CallQubitMapping` in
 * `mlir/Dialect/QCO/Utils/WireIterator.h`: the correspondence is derived by
 * threading each tensor argument through the callee body, cached per callee.
 * The mapping fails when the callee cannot be analyzed.
 */
class CallTensorMapping {
public:
  /**
   * @brief Get the call result that continues the chain of a tensor operand.
   *
   * @param callOp The call the tensor flows into.
   * @param operand The qubit-tensor operand of @p callOp.
   * @return The matching result, a null value when the callee keeps the tensor,
   * or failure when the correspondence cannot be derived.
   */
  [[nodiscard]] FailureOr<Value> getResultForOperand(func::CallOp callOp,
                                                     Value operand);

private:
  /// Marks a tensor argument that never reaches a result.
  static constexpr int64_t KEPT = -1;

  /// @returns per tensor argument position, the result index it flows into.
  FailureOr<ArrayRef<int64_t>> mappingFor(func::CallOp callOp);

  /// Derive the mapping of @p callee by threading its tensor arguments.
  FailureOr<SmallVector<int64_t>> computeMapping(func::FuncOp callee);

  /// Follow @p arg to its operand index in @p returnOp, hopping over calls.
  FailureOr<int64_t> threadToResult(Value arg, func::ReturnOp returnOp);

  DenseMap<Operation*, SmallVector<int64_t>> cache;
  DenseSet<Operation*> inProgress;
};
} // namespace mlir::qtensor
