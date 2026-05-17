# 第5章：部分 Lowering 至低层 Dialect 以进行优化

[TOC]

至此，我们迫不及待地想要生成实际的代码，看到我们的 Toy 语言焕发生机。我们将使用 LLVM 来生成代码，但仅仅在此展示 LLVM builder 接口并不会很有趣。相反，我们将展示如何通过在同一函数中共存的不同方言混合体来执行渐进式 lowering。

为了增加趣味性，在本章中我们将考虑重用在一个用于优化仿射变换（affine transformation）的方言中实现的现有优化：`Affine` 方言。此方言专门针对程序的计算密集型部分，并且有一定的局限性：例如，它不支持表示我们的 `toy.print` 内置函数，也不应该支持！相反，我们可以将 Toy 的计算密集型部分以 `Affine` 为目标，而在[下一章](Ch_06_Lowering至LLVM与代码生成.md)中直接以 `LLVM IR` 方言为目标来 lowering `print`。作为此 lowering 的一部分，我们将从 Toy 操作的 [TensorType](../../Dialects/Builtin.md/#rankedtensortype) lowering 到通过仿射循环嵌套索引的 [MemRefType](../../Dialects/Builtin.md/#memreftype)。张量表示抽象的值类型数据序列，意味着它们不驻留在任何内存中。另一方面，MemRef 表示更低层的缓冲区访问，因为它们是对内存区域的具体引用。

# 方言转换（Dialect Conversion）

MLIR 有许多不同的方言，因此拥有一个统一的框架来进行 [转换（Conversion）](../../../getting_started/Glossary.md/#conversion) 非常重要。这就是 `DialectConversion` 框架发挥作用的地方。此框架允许将一组*非法*（illegal）操作转换为一组*合法*（legal）操作。要使用此框架，我们需要提供两个（以及可选的第三个）要素：

*   [转换目标（Conversion Target）](../../DialectConversion.md/#conversion-target)

    -   这是对哪些操作或方言对此次转换是合法的正式规范。不合法的操作将需要重写模式来执行[合法化（legalization）](../../../getting_started/Glossary.md/#legalization)。

*   一组 [重写模式（Rewrite Patterns）](../../DialectConversion.md/#rewrite-pattern-specification)

    -   这是用于将*非法*操作转换为一组零个或多个*合法*操作的[模式](../QuickstartRewrites.md)集合。

*   可选地，一个 [类型转换器（Type Converter）](../../DialectConversion.md/#type-conversion)。

    -   如果提供，它用于转换基本块参数的类型。我们的转换不需要这个。

## 转换目标

出于我们的目的，我们想将计算密集型的 `Toy` 操作转换为来自 `Affine`、`Arith`、`Func` 和 `MemRef` 方言的操作的组合，以便进一步优化。为了启动 lowering，我们首先定义我们的转换目标：

```c++
void ToyToAffineLoweringPass::runOnOperation() {
  // The first thing to define is the conversion target. This will define the
  // final target for this lowering.
  mlir::ConversionTarget target(getContext());

  // We define the specific operations, or dialects, that are legal targets for
  // this lowering. In our case, we are lowering to a combination of the
  // `Affine`, `Arith`, `Func`, and `MemRef` dialects.
  target.addLegalDialect<affine::AffineDialect, arith::ArithDialect,
                         func::FuncDialect, memref::MemRefDialect>();

  // We also define the Toy dialect as Illegal so that the conversion will fail
  // if any of these operations are *not* converted. Given that we actually want
  // a partial lowering, we explicitly mark the Toy operations that don't want
  // to lower, `toy.print`, as *legal*. `toy.print` will still need its operands
  // to be updated though (as we convert from TensorType to MemRefType), so we
  // only treat it as `legal` if its operands are legal.
  target.addIllegalDialect<ToyDialect>();
  target.addDynamicallyLegalOp<toy::PrintOp>([](toy::PrintOp op) {
    return llvm::none_of(op->getOperandTypes(), llvm::IsaPred<TensorType>);
  });
  ...
}
```

上面，我们首先将 toy 方言设置为非法，然后将 print 操作设置为合法。我们本可以反过来做。单个操作始终优先于（更通用的）方言定义，因此顺序无关紧要。详见 `ConversionTarget::getOpInfo`。

## 转换模式

在定义了转换目标之后，我们可以定义如何将*非法*操作转换为*合法*操作。与[第3章](Ch_03_高层语言特定分析与变换.md)中介绍的规范化框架类似，[`DialectConversion` 框架](../../DialectConversion.md) 使用一种特殊的 `ConversionPattern` 来执行转换逻辑。`ConversionPattern` 与传统的 `RewritePattern` 不同，因为它接受一个额外的 `operands`（或 `adaptor`）参数，其中包含已被重映射/替换的操作数。这在处理类型转换时非常有用，因为模式将希望操作新类型的值，但匹配旧类型的值。对于我们的 lowering，这个不变量将非常有用，因为它会在当前操作的 [TensorType](../../Dialects/Builtin.md/#rankedtensortype) 和 [MemRefType](../../Dialects/Builtin.md/#memreftype) 之间进行转换。让我们看看 lowering `toy.transpose` 操作的代码片段：

```c++
/// Lower the `toy.transpose` operation to an affine loop nest.
struct TransposeOpLowering : public OpConversionPattern<toy::TransposeOp> {
  using OpConversionPattern<toy::TransposeOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(toy::TransposeOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    auto loc = op->getLoc();
    lowerOpToLoops(op, rewriter,
                   [&](OpBuilder &builder, ValueRange loopIvs) {
                     Value input = adaptor.getInput();

                     // Transpose the elements by generating a load from the
                     // reverse indices.
                     SmallVector<Value, 2> reverseIvs(llvm::reverse(loopIvs));
                     return affine::AffineLoadOp::create(builder, loc, input,
                                                         reverseIvs);
                   });
    return success();
  }
};
```

现在我们可以准备在 lowering 过程中使用的模式列表：

```c++
void ToyToAffineLoweringPass::runOnOperation() {
  ...

  // Now that the conversion target has been defined, we just need to provide
  // the set of patterns that will lower the Toy operations.
  mlir::RewritePatternSet patterns(&getContext());
  patterns.add<..., TransposeOpLowering>(&getContext());

  ...
```

## 部分 Lowering

一旦模式定义好了，我们就可以执行实际的 lowering 了。`DialectConversion` 框架提供了几种不同的 lowering 模式，但是出于我们的目的，我们将执行部分 lowering，因为我们在此时不会转换 `toy.print`。

```c++
void ToyToAffineLoweringPass::runOnOperation() {
  ...

  // With the target and rewrite patterns defined, we can now attempt the
  // conversion. The conversion will signal failure if any of our *illegal*
  // operations were not converted successfully.
  if (mlir::failed(mlir::applyPartialConversion(getOperation(), target, patterns)))
    signalPassFailure();
}
```

### 部分 Lowering 的设计考量

在深入我们的 lowering 结果之前，现在是讨论部分 lowering 潜在设计考量的好时机。在我们的 lowering 中，我们从值类型 TensorType 转换到已分配（类缓冲区）类型 MemRefType。然而，鉴于我们没有 lowering `toy.print` 操作，我们需要在这两个世界之间临时建立桥梁。有许多方法可以做到这一点，每种方法都有其各自的权衡：

*   从缓冲区生成 `load` 操作

    一种选项是从缓冲区类型生成 `load` 操作以物化一个值类型实例。这允许 `toy.print` 操作的定义保持不变。这种方法的缺点是 `affine` 方言上的优化受到限制，因为 `load` 将实际涉及一个完整的拷贝，而这个拷贝仅在我们的优化*之后*才可见。

*   生成一个新版本的 `toy.print`，使其操作在 lowered 后的类型上

    另一种选项是拥有另一个 lowered 后的 `toy.print` 变体，它操作在 lowered 后的类型上。此选项的好处是没有隐藏的、不必要的拷贝对优化器可见。缺点是可能需要重复第一个版本的许多方面来定义另一个操作。在 [ODS](../../DefiningDialects/Operations.md) 中定义基类可以简化这一点，但你仍然需要分别处理这些操作。

*   更新 `toy.print` 使其能够操作在 lowered 后的类型上

    第三种选项是更新 `toy.print` 的当前定义，使其能够操作在 lowered 后的类型上。这种方法的好处是简单，不会引入额外的隐藏拷贝，也不需要另一个操作定义。此选项的缺点是需要在 `Toy` 方言中混合不同的抽象层级。

为简单起见，我们将在本次 lowering 中使用第三种选项。这涉及在操作定义文件中更新 PrintOp 的类型约束：

```tablegen
def PrintOp : Toy_Op<"print"> {
  ...

  // The print operation takes an input tensor to print.
  // We also allow a F64MemRef to enable interop during partial lowering.
  let arguments = (ins AnyTypeOf<[F64Tensor, F64MemRef]>:$input);
}
```

## 完整的 Toy 示例

让我们看一个具体的例子：

```mlir
toy.func @main() {
  %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
  %2 = toy.transpose(%0 : tensor<2x3xf64>) to tensor<3x2xf64>
  %3 = toy.mul %2, %2 : tensor<3x2xf64>
  toy.print %3 : tensor<3x2xf64>
  toy.return
}
```

将 affine lowering 添加到我们的流水线后，我们现在可以生成：

```mlir
func.func @main() {
  %cst = arith.constant 1.000000e+00 : f64
  %cst_0 = arith.constant 2.000000e+00 : f64
  %cst_1 = arith.constant 3.000000e+00 : f64
  %cst_2 = arith.constant 4.000000e+00 : f64
  %cst_3 = arith.constant 5.000000e+00 : f64
  %cst_4 = arith.constant 6.000000e+00 : f64

  // Allocating buffers for the inputs and outputs.
  %0 = memref.alloc() : memref<3x2xf64>
  %1 = memref.alloc() : memref<3x2xf64>
  %2 = memref.alloc() : memref<2x3xf64>

  // Initialize the input buffer with the constant values.
  affine.store %cst, %2[0, 0] : memref<2x3xf64>
  affine.store %cst_0, %2[0, 1] : memref<2x3xf64>
  affine.store %cst_1, %2[0, 2] : memref<2x3xf64>
  affine.store %cst_2, %2[1, 0] : memref<2x3xf64>
  affine.store %cst_3, %2[1, 1] : memref<2x3xf64>
  affine.store %cst_4, %2[1, 2] : memref<2x3xf64>

  // Load the transpose value from the input buffer and store it into the
  // next input buffer.
  affine.for %arg0 = 0 to 3 {
    affine.for %arg1 = 0 to 2 {
      %3 = affine.load %2[%arg1, %arg0] : memref<2x3xf64>
      affine.store %3, %1[%arg0, %arg1] : memref<3x2xf64>
    }
  }

  // Multiply and store into the output buffer.
  affine.for %arg0 = 0 to 3 {
    affine.for %arg1 = 0 to 2 {
      %3 = affine.load %1[%arg0, %arg1] : memref<3x2xf64>
      %4 = affine.load %1[%arg0, %arg1] : memref<3x2xf64>
      %5 = arith.mulf %3, %4 : f64
      affine.store %5, %0[%arg0, %arg1] : memref<3x2xf64>
    }
  }

  // Print the value held by the buffer.
  toy.print %0 : memref<3x2xf64>
  memref.dealloc %2 : memref<2x3xf64>
  memref.dealloc %1 : memref<3x2xf64>
  memref.dealloc %0 : memref<3x2xf64>
  return
}
```

## 利用 Affine 优化

我们朴素的 lowering 是正确的，但在效率方面还有很多不足之处。例如，`toy.mul` 的 lowering 产生了一些冗余的 `load`。让我们看看在流水线中添加一些现有的优化如何帮助清理这些问题。将 `LoopFusion` 和 `AffineScalarReplacement` pass 添加到流水线中得到以下结果：

```mlir
func.func @main() {
  %cst = arith.constant 1.000000e+00 : f64
  %cst_0 = arith.constant 2.000000e+00 : f64
  %cst_1 = arith.constant 3.000000e+00 : f64
  %cst_2 = arith.constant 4.000000e+00 : f64
  %cst_3 = arith.constant 5.000000e+00 : f64
  %cst_4 = arith.constant 6.000000e+00 : f64

  // Allocating buffers for the inputs and outputs.
  %0 = memref.alloc() : memref<3x2xf64>
  %1 = memref.alloc() : memref<2x3xf64>

  // Initialize the input buffer with the constant values.
  affine.store %cst, %1[0, 0] : memref<2x3xf64>
  affine.store %cst_0, %1[0, 1] : memref<2x3xf64>
  affine.store %cst_1, %1[0, 2] : memref<2x3xf64>
  affine.store %cst_2, %1[1, 0] : memref<2x3xf64>
  affine.store %cst_3, %1[1, 1] : memref<2x3xf64>
  affine.store %cst_4, %1[1, 2] : memref<2x3xf64>

  affine.for %arg0 = 0 to 3 {
    affine.for %arg1 = 0 to 2 {
      // Load the transpose value from the input buffer.
      %2 = affine.load %1[%arg1, %arg0] : memref<2x3xf64>

      // Multiply and store into the output buffer.
      %3 = arith.mulf %2, %2 : f64
      affine.store %3, %0[%arg0, %arg1] : memref<3x2xf64>
    }
  }

  // Print the value held by the buffer.
  toy.print %0 : memref<3x2xf64>
  memref.dealloc %1 : memref<2x3xf64>
  memref.dealloc %0 : memref<3x2xf64>
  return
}
```

在这里，我们可以看到冗余的分配被移除了，两个循环嵌套被融合了，还有一些不必要的 `load` 被移除了。你可以构建 `toyc-ch5` 并亲自尝试：`toyc-ch5 test/Examples/Toy/Ch5/affine-lowering.mlir -emit=mlir-affine`。我们还可以通过添加 `-opt` 来检查我们的优化。

在本章中，我们以优化为目标探索了部分 lowering 的一些方面。在[下一章](Ch_06_Lowering至LLVM与代码生成.md)中，我们将通过以 LLVM 为代码生成目标，继续讨论方言转换。
