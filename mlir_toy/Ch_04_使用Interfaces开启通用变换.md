# 第4章：使用 Interfaces 开启通用变换

[TOC]

## 背景：应对可扩展 IR 的挑战

通过方言，MLIR 允许表示许多不同的抽象层级；我们之前定义的 Toy 方言就是这样一个例子。虽然这些不同的方言可能代表不同的抽象，但通常我们希望对它们执行一组通用的变换和分析。问题在于，如果朴素地为每种方言分别实现每种变换，会导致大量的代码重复，因为内部算法通常非常相似，甚至完全相同。我们希望能够让变换以不透明的方式接入像 Toy 这样的方言，以获取它们所需的信息。

MLIR 为某些核心变换提供了一组始终可用的钩子，如[上一章](Ch_03_高层语言特定分析与变换.md)所示，我们在操作上通过钩子（`getCanonicalizationPatterns`）注册了一些规范化变换。然而，这类钩子的扩展性并不好。因此，一个更通用的解决方案被设计出来，这就是 [接口（Interface）](../../Interfaces.md)。其目标是使 MLIR 基础设施与表示本身一样可扩展。接口为方言和操作提供了一种通用机制，以便向变换或分析提供信息。

## 形状推导：为代码生成做准备

我们的 Toy IR 目前操作在泛型张量上，这意味着除了在常量初始化期间我们不知道张量的形状。这使优化和代码生成变得复杂。幸运的是，我们可以简单地通过计算过程传播形状，直到它们全部变为已知。问题在于如何处理对用户自定义泛型函数的调用：每个调用点可能推导出不同的形状。一种可能的方案是基于参数类型进行符号推导，但如果我们在语言中引入更多控制流，这将难以推广。另一种方案是函数特化，即每个具有新参数形状的调用点都复制被调用的函数并对其进行特化。我们在 Toy 中采用的方法是将所有函数调用内联（inline），然后执行过程内形状传播。

### 内联（Inlining）

这里我们可以编写一个专门为 Toy 方言设计的内联算法，但根据我们想要的复杂度级别，这可能会变得相当复杂。不考虑成本建模，纯粹的结构的变换从头实现已经相当复杂。幸运的是，MLIR 提供了一个通用的内联器算法，方言可以接入其中。在 Toy 中，我们所需要做的就是为内联器提供[接口（Interface）](../../Interfaces.md) 以接入。

我们需要做的第一件事是定义 Toy 方言中内联操作的约束。此信息通过[方言接口（Dialect Interface）](../../Interfaces.md/#dialect-interfaces)提供。这本质上是一个包含一组虚拟钩子的类，方言可以重写这些钩子。在这里，接口是 `DialectInlinerInterface`。

```c++
/// This class defines the interface for handling inlining with Toy operations.
/// We simplify inherit from the base interface class and override
/// the necessary methods.
struct ToyInlinerInterface : public DialectInlinerInterface {
  using DialectInlinerInterface::DialectInlinerInterface;

  /// This hook checks to see if the given callable operation is legal to inline
  /// into the given call. For Toy this hook can simply return true, as the Toy
  /// Call operation is always inlinable.
  bool isLegalToInline(Operation *call, Operation *callable,
                       bool wouldBeCloned) const final {
    return true;
  }

  /// This hook checks to see if the given operation is legal to inline into the
  /// given region. For Toy this hook can simply return true, as all Toy
  /// operations are inlinable.
  bool isLegalToInline(Operation *, Region *, bool,
                       IRMapping &) const final {
    return true;
  }

  /// This hook checks if the given 'src' region can be inlined into the 'dest'
  /// region. The regions here are the bodies of the callable functions. For
  /// Toy, any function can be inlined, so we simply return true.
  bool isLegalToInline(Region *dest, Region *src, bool wouldBeCloned,
                       IRMapping &valueMapping) const final {
    return true;
  }

  /// This hook is called when a terminator operation has been inlined. The only
  /// terminator that we have in the Toy dialect is the return
  /// operation(toy.return). We handle the return by replacing the values
  /// previously returned by the call operation with the operands of the
  /// return.
  void handleTerminator(Operation *op,
                        ValueRange valuesToRepl) const final {
    // Only "toy.return" needs to be handled here.
    auto returnOp = cast<ReturnOp>(op);

    // Replace the values directly with the return operands.
    assert(returnOp.getNumOperands() == valuesToRepl.size());
    for (const auto &it : llvm::enumerate(returnOp.getOperands()))
      valuesToRepl[it.index()].replaceAllUsesWith(it.value());
  }
};
```

此外，内联器只会丢弃私有（private）不可见的未使用函数定义。我们还需要在 MLIR 生成器中设置函数（除 main 函数外）的可见性。

```c++
/// Emit a new function and add it to the MLIR module.
mlir::toy::FuncOp mlirGen(FunctionAST &funcAST) {
  ...
  // If this function isn't main, then set the visibility to private.
  if (funcAST.getProto()->getName() != "main")
    function.setPrivate();

  return function;
}
```

然后我们将方言接口注册到 Toy 方言上，类似于我们为操作注册的方式。

```c++
void ToyDialect::initialize() {
  addInterfaces<ToyInlinerInterface>();
}
```

接下来，我们需要为内联器提供一种方式来知道 `toy.generic_call` 表示一个调用，而 `toy.func` 表示一个函数。MLIR 提供了[操作接口（Operation Interface）](../../Interfaces.md/#attributeoperationtype-interfaces)，可用于将某个操作标记为"类似调用的"或"类似可调用的"。与方言接口不同，操作接口提供了更细粒度的信息，这些信息针对单个操作且是其核心。我们将要添加的接口是 `CallOpInterface` 和 `CallableOpInterface`（注意：在新版本中为 `FunctionOpInterface`）。

要添加此接口，我们只需在操作规范文件（`Ops.td`）中包含定义：

```tablegen
include "mlir/Interfaces/CallInterfaces.td"
```

并将其添加到 `GenericCallOp` 的 traits 列表中：

```tablegen
def FuncOp : Toy_Op<"func",
    [FunctionOpInterface, IsolatedFromAbove]> {
  ...
}

def GenericCallOp : Toy_Op<"generic_call",
    [DeclareOpInterfaceMethods<CallOpInterface>]> {
  ...
}
```

在上面的代码中，我们还使用了 `DeclareOpInterfaceMethods` 指令来自动在 `GenericCallOp` 的类声明中声明所有接口方法。然而，将此指令与 `CallOpInterface` 一起使用会包含用于处理参数属性和结果属性的方法。因此，我们需要将这些特定命名的属性添加到 `GenericCallOp` 的定义中：

```tablegen
let arguments = (ins
  ...
  OptionalAttr<DictArrayAttr>:$arg_attrs,
  OptionalAttr<DictArrayAttr>:$res_attrs
);
```

我们已经在 `FuncOp` 类的 `extraClassDeclaration` 字段中提供了定义：

```c++
/// Returns the region on the function operation that is callable.
Region *FuncOp::getCallableRegion() { return &getBody(); }

// ....

/// Return the callee of the generic call operation, this is required by the
/// call interface.
CallInterfaceCallable GenericCallOp::getCallableForCallee() {
  return (*this)->getAttrOfType<SymbolRefAttr>("callee");
}

/// Set the callee for the generic call operation, this is required by the call
/// interface.
void GenericCallOp::setCalleeFromCallable(CallInterfaceCallable callee) {
  (*this)->setAttr("callee", callee.get<SymbolRefAttr>());
}

/// Get the argument operands to the called function, this is required by the
/// call interface.
Operation::operand_range GenericCallOp::getArgOperands() { return getInputs(); }

/// Get the argument operands to the called function as a mutable range, this is
/// required by the call interface.
MutableOperandRange GenericCallOp::getArgOperandsMutable() {
  return getInputsMutable();
}
```

现在内联器已经了解了 Toy 方言，我们可以将内联 pass 添加到 Toy 的 pass manager 中：

```c++
  pm.addPass(mlir::createInlinerPass());
```

现在让我们看一个工作的示例：

```mlir
toy.func @multiply_transpose(%arg0: tensor<*xf64>, %arg1: tensor<*xf64>) -> tensor<*xf64> {
  %0 = toy.transpose(%arg0 : tensor<*xf64>) to tensor<*xf64>
  %1 = toy.transpose(%arg1 : tensor<*xf64>) to tensor<*xf64>
  %2 = toy.mul %0, %1 : tensor<*xf64>
  toy.return %2 : tensor<*xf64>
}
toy.func @main() {
  %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
  %1 = toy.reshape(%0 : tensor<2x3xf64>) to tensor<2x3xf64>
  %2 = toy.constant dense<[1.000000e+00, 2.000000e+00, 3.000000e+00, 4.000000e+00, 5.000000e+00, 6.000000e+00]> : tensor<6xf64>
  %3 = toy.reshape(%2 : tensor<6xf64>) to tensor<2x3xf64>
  %4 = toy.generic_call @multiply_transpose(%1, %3) : (tensor<2x3xf64>, tensor<2x3xf64>) -> tensor<*xf64>
  %5 = toy.generic_call @multiply_transpose(%3, %1) : (tensor<2x3xf64>, tensor<2x3xf64>) -> tensor<*xf64>
  toy.print %5 : tensor<*xf64>
  toy.return
}
```

我们在 `main` 中有两个对 `multiply_transpose` 的调用，我们想将它们内联到 `main` 中。但如果我们查看输出，什么都没变。我们遗漏了最后一个微妙的要点：在调用的边缘存在一个隐藏的类型转换。如果我们看上面，`generic_call` 的操作数是类型 `tensor<2x3xf64>`，而函数的输入期望的是 `tensor<*xf64>`。为了解决这个差异，内联器期望插入一个显式的类型转换操作。为此，我们需要向 Toy 方言添加一个新的操作 `ToyCastOp`（`toy.cast`），用于表示两种不同形状之间的转换。

```tablegen
def CastOp : Toy_Op<"cast", [
    DeclareOpInterfaceMethods<CastOpInterface>,
    Pure,
    SameOperandsAndResultShape]
  > {
  let summary = "shape cast operation";
  let description = [{
    The "cast" operation converts a tensor from one type to an equivalent type
    without changing any data elements. The source and destination types
    must both be tensor types with the same element type. If both are ranked,
    then shape is required to match. The operation is invalid if converting
    to a mismatching constant dimension.
  }];

  let arguments = (ins F64Tensor:$input);
  let results = (outs F64Tensor:$output);
  let assemblyFormat = "$input attr-dict `:` type($input) `to` type($output)";
}
```

请注意，这个转换操作的定义在 traits 列表中添加了 `CastOpInterface`。此接口为类似转换的操作提供了几个实用工具，例如折叠恒等转换和验证。我们通过提供 `areCastCompatible` 方法的定义来接入此接口：

```c++
/// Returns true if the given set of input and result types are compatible with
/// this cast operation. This is required by the `CastOpInterface` to verify
/// this operation and provide other additional utilities.
bool CastOp::areCastCompatible(TypeRange inputs, TypeRange outputs) {
  if (inputs.size() != 1 || outputs.size() != 1)
    return false;
  // The inputs must be Tensors with the same element type.
  TensorType input = llvm::dyn_cast<TensorType>(inputs.front());
  TensorType output = llvm::dyn_cast<TensorType>(outputs.front());
  if (!input || !output || input.getElementType() != output.getElementType())
    return false;
  // The shape is required to match if both types are ranked.
  return !input.hasRank() || !output.hasRank() || input == output;
}
```

有了合适的转换操作，我们现在可以重写 `ToyInlinerInterface` 上的必要钩子，以便在需要时为我们插入它：

```c++
struct ToyInlinerInterface : public DialectInlinerInterface {
  ...

  /// Attempts to materialize a conversion for a type mismatch between a call
  /// from this dialect, and a callable region. This method should generate an
  /// operation that takes 'input' as the only operand, and produces a single
  /// result of 'resultType'. If a conversion can not be generated, nullptr
  /// should be returned.
  Operation *materializeCallConversion(OpBuilder &builder, Value input,
                                       Type resultType,
                                       Location conversionLoc) const final {
    return CastOp::create(builder, conversionLoc, resultType, input);
  }
};
```

如果我们再次通过流水线运行工作示例，我们得到预期的结果：

```mlir
toy.func @main() {
  %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
  %1 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
  %2 = toy.cast %1 : tensor<2x3xf64> to tensor<*xf64>
  %3 = toy.cast %0 : tensor<2x3xf64> to tensor<*xf64>
  %4 = toy.transpose(%2 : tensor<*xf64>) to tensor<*xf64>
  %5 = toy.transpose(%3 : tensor<*xf64>) to tensor<*xf64>
  %6 = toy.mul %4, %5 : tensor<*xf64>
  toy.print %6 : tensor<*xf64>
  toy.return
}
```

注意：通用内联器还将执行简化，因此输出可能比预期的更加干净。

### 过程内形状推导

既然我们已经内联了所有的函数，我们留下了一个包含静态和动态形状操作混合的 main 函数。我们现在可以编写一个简单的形状推导 pass 来在过程内（单个函数内部）传播形状。我们可以将此写成一个直接编码 Toy 方言中操作约束的 pass，但这看起来是一个可以用通用方式编写的变换的理想候选。一个好的经验法则是，尽可能将变换表达为通用的，以便将来可以扩展到其他方言。谁也不知道有多少其他方言可能有类似的需求或遇到相同的问题。

对于形状推导，如果我们将问题分解到核心，我们实际上只是希望操作能够告诉我们：给定一组静态已知的输入，预期的输出是什么。（我们当然可以做得比这更复杂，但针对我们的需求，我们可以保持简单。）鉴于此属性是特定操作的核心，我们可以定义一个操作接口，该接口可以在需要推导其结果形状的操作上指定。

与操作类似，我们也可以使用操作定义规范（ODS）框架[定义操作接口](../../Interfaces.md/#attributeoperationtype-interfaces)。

接口通过继承 `OpInterface` 来定义，该类将赋予生成的 C++ 接口类的名称作为模板参数。出于我们的目的，我们将生成的类简单地命名为 `ShapeInference`。我们还为接口提供一个描述。

```tablegen
def ShapeInferenceOpInterface : OpInterface<"ShapeInference"> {
  let description = [{
    Interface to access a registered method to infer the return types for an
    operation that can be used during type inference.
  }];
}
```

接下来，我们定义操作需要提供的接口方法。接口方法包括：描述；字符串形式的 C++ 返回类型；字符串形式的方法名；以及一些可选组件，视需求而定。更多信息请参阅 [ODS 文档](../../Interfaces.md/#attributeoperationtype-interfaces)。

```tablegen
def ShapeInferenceOpInterface : OpInterface<"ShapeInference"> {
  ...

  let methods = [
    InterfaceMethod<"Infer and set the output shape for the current operation.",
                    "void", "inferShapes">
  ];
}
```

现在接口已经定义好了，我们可以将其添加到必要的 Toy 操作中，方式与我们为 GenericCallOp 添加 `CallOpInterface` 类似：

```tablegen
def MulOp : Toy_Op<"mul",
    [..., DeclareOpInterfaceMethods<ShapeInferenceOpInterface>]> {
  ...
}
```

然后这些操作中的每一个都需要为 `inferShapes()` 方法提供定义。作为示例，对于 mul 操作，结果形状被推导为输入的形状。

```c++
/// Infer the output shape of the MulOp, this is required by the shape inference
/// interface.
void MulOp::inferShapes() { getResult().setType(getLhs().getType()); }
```

此时，每个必要的 Toy 操作都提供了一种机制，通过它可以推导其输出形状。ShapeInferencePass 将作用于函数：它将孤立地运行在每个函数上。MLIR 也支持可以运行在任何隔离操作上的通用 [OperationPass](../../PassManagement.md/#operation-pass)，但在这里我们的模块只包含函数，因此无需推广到所有操作。

实现这样一个 pass 的方式是创建一个继承自 `mlir::OperationPass<FuncOp>` 的类，并重写 `runOnOperation()` 方法。

```c++
class ShapeInferencePass
    : public mlir::PassWrapper<ShapeInferencePass, OperationPass<FuncOp>> {
  void runOnOperation() override {
    FuncOp function = getOperation();
    ...
  }
};
```

同时，我们也创建一个辅助方法用于实例化该 pass：

```c++
std::unique_ptr<mlir::Pass> mlir::toy::createShapeInferencePass() {
  return std::make_unique<ShapeInferencePass>();
}
```

形状推导算法按以下方式运作：

1.  构建一个工作列表，包含所有返回动态形状张量的操作：这些是需要形状推导的操作。
2.  遍历工作列表：
    -   找到一个要处理的操作：工作列表中下一个就绪的操作，其所有参数都是非泛型的；
    -   如果没有找到操作，则跳出循环；
    -   从工作列表中移除该操作；
    -   根据参数类型推导其输出的形状。
3.  如果工作列表为空，算法成功。

在处理上述类似的操作时，我们查询它是否注册了 `ShapeInference` 接口，使用以下代码片段：

```c++
  // Ask the operation to infer its output shapes.
  LDBG() << "Inferring shape for: " << *op;

  /// We check if an operation has a particular interface by casting.
  if (ShapeInference shapeOp = dyn_cast<ShapeInference>(op)) {
    shapeOp.inferShapes();
  } else {
    op->emitError("unable to infer shape of operation without shape "
                  "inference interface");
    return signalPassFailure();
  }
```

然后我们可以将我们的 pass 添加到 pass manager 中：

```c++
  pm.addPass(mlir::createShapeInferencePass());
```

如果我们重新运行原始示例，我们得到以下内容：

```mlir
toy.func @main() {
  %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
  %1 = toy.transpose(%0 : tensor<2x3xf64>) to tensor<3x2xf64>
  %2 = toy.mul %1, %1 : tensor<3x2xf64>
  toy.print %2 : tensor<3x2xf64>
  toy.return
}
```

你可以构建 `toyc-ch4` 并亲自尝试：`toyc-ch4 test/Examples/Toy/Ch4/codegen.toy -emit=mlir -opt`。

在[下一章](Ch_05_部分Lowering至低层Dialect.md)中，我们将启动代码生成过程，通过将一些计算密集型的 Toy 操作 lowering 到更低层次的方言来进行优化。
