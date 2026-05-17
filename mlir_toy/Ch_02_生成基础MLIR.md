# 第2章：生成基础 MLIR

[TOC]

现在我们已经熟悉了 Toy 语言及其 AST，让我们看看 MLIR 如何帮助编译 Toy。

## 引言：多级中间表示

其他编译器（如 LLVM，参见 [Kaleidoscope 教程](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html)）提供了一组固定的预定义类型和（通常是*低层级*的 / 类 RISC 风格的）指令。前端需要负责执行任何语言特定的类型检查、分析或变换，然后才能生成 LLVM IR。例如，Clang 会使用其 AST 不仅执行静态分析，还会执行各种变换，比如通过 AST 克隆和重写来实现 C++ 模板实例化。最终，那些抽象层级比 C/C++ 更高的语言可能需要从它们的 AST 进行非平凡的 lowering 才能生成 LLVM IR。

结果是，多个前端最终会重复实现大量的基础设施来支持这些分析和变换的需求。MLIR 通过设计上的可扩展性来解决此问题。因此，MLIR 中很少有预定义的指令（在 MLIR 术语中称为*操作*，Operation）或类型。

## 与 MLIR 交互

[语言参考](../../LangRef.md)

MLIR 被设计为完全可扩展的基础设施；属性（attribute，可以理解为常量元数据）、操作（operation）或类型（type）都不存在封闭的集合。MLIR 通过 [方言（Dialect）](../../LangRef.md/#dialects) 的概念来支持这种可扩展性。方言提供了一个在唯一 `namespace` 下进行抽象的编组机制。

在 MLIR 中，[`操作（Operation）`](../../LangRef.md/#operations) 是抽象和计算的核心单元，在许多方面类似于 LLVM 指令。操作可以具有特定于应用的语义，并且可用于表示 LLVM 中所有的核心 IR 结构：指令、全局量（如函数）、模块等。

以下是 Toy `transpose` 操作的 MLIR 汇编：

```mlir
%t_tensor = "toy.transpose"(%tensor) {inplace = true} : (tensor<2x3xf64>) -> tensor<3x2xf64> loc("example/file/path":12:1)
```

让我们拆解这个 MLIR 操作的各个组成部分：

-   `%t_tensor`

    *   由该操作定义的结果的名称（包含 [一个防止冲突的前缀符号](../../LangRef.md/#identifiers-and-keywords)）。一个操作可以定义零个或多个结果（在 Toy 的上下文中，我们将限制为单结果操作），这些结果是 SSA 值。该名称在解析时使用，但不是持久的（例如，它不会在 SSA 值的内存表示中被追踪）。

-   `"toy.transpose"`

    *   操作的名称。预期为一个唯一的字符串，方言的命名空间前置于 "`.`" 之前。这可以理解为 `toy` 方言中的 `transpose` 操作。

-   `(%tensor)`

    *   零个或多个输入操作数（或参数）的列表，它们是其他操作定义或引用自基本块参数的 SSA 值。

-   `{ inplace = true }`

    *   零个或多个属性（attribute）的字典，属性是始终为常量的特殊操作数。这里我们定义了一个名为 'inplace' 的布尔属性，其常量值为 true。

-   `(tensor<2x3xf64>) -> tensor<3x2xf64>`

    *   以函数式形式表示操作的类型，括号中拼写参数的类型，后面跟着返回值的类型。

-   `loc("example/file/path":12:1)`

    *   这是该操作在源代码中的起源位置。

上面展示的是操作的通用形式。如上所述，MLIR 中的操作集合是可扩展的。操作使用一小组概念进行建模，使得操作可以被通用地推理和操作。这些概念是：

-   操作的名称。
-   SSA 操作数值的列表。
-   [属性（attribute）](../../LangRef.md/#attributes) 的列表。
-   结果值的 [类型（type）](../../LangRef.md/#type-system) 列表。
-   用于调试目的的 [源码位置（source location）](../../Diagnostics.md/#source-locations)。
-   后继 [基本块（block）](../../LangRef.md/#blocks) 的列表（主要用于分支）。
-   [区域（region）](../../LangRef.md/#regions) 的列表（用于结构化操作，如函数）。

在 MLIR 中，每个操作都有一个与之关联的强制性源码位置。与 LLVM 中调试信息位置是元数据且可能被丢弃不同，在 MLIR 中，位置是核心要求，API 依赖并操作它。因此，丢弃位置是一个显式的选择，不可能意外发生。

举例说明：如果一个变换将某个操作替换为另一个操作，新操作仍然必须附带一个位置。这使得追踪该操作的来源成为可能。

值得注意的是，`mlir-opt` 工具（一个用于测试编译器 pass 的工具）默认在输出中不包含位置信息。`-mlir-print-debuginfo` 标志用于指定包含位置信息。（运行 `mlir-opt --help` 查看更多选项。）

### 不透明 API

MLIR 被设计为允许所有的 IR 元素（如属性、操作和类型）被自定义。同时，IR 元素始终可以归结为上述的基本概念。这使得 MLIR 能够为*任何*操作解析、表示和 [round-trip](../../../getting_started/Glossary.md/#round-trip) IR。例如，我们可以将上面的 Toy 操作放入 `.mlir` 文件中，并在不注册任何 `toy` 相关方言的情况下通过 *mlir-opt* 进行 round-trip：

```mlir
func.func @toy_func(%tensor: tensor<2x3xf64>) -> tensor<3x2xf64> {
  %t_tensor = "toy.transpose"(%tensor) { inplace = true } : (tensor<2x3xf64>) -> tensor<3x2xf64>
  return %t_tensor : tensor<3x2xf64>
}
```

对于未注册的属性、操作和类型，MLIR 将强制执行一些结构性约束（例如支配关系等），但除此之外它们是完全不透明的。例如，MLIR 对于未注册的操作能否操作特定数据类型、可以接受多少个操作数或产生多少个结果知之甚少。这种灵活性对于引导阶段可能很有用，但在成熟系统中通常不建议这样做。变换和分析必须保守地处理未注册的操作，而且它们更难构建和操作。

这种处理方式可以通过构造一个对 Toy 来说应该无效的 IR 来观察，看它如何在不触发验证器的情况下完成 round-trip：

```mlir
func.func @main() {
  %0 = "toy.print"() : () -> tensor<2x3xf64>
}
```

这里存在多个问题：`toy.print` 操作不是终止符（terminator）；它应该接受一个操作数；而且它不应该返回任何值。在下一节中，我们将向 MLIR 注册我们的方言和操作，接入验证器，并添加更好的 API 来操作我们的操作。

## 定义一个 Toy 方言

为了有效地与 MLIR 交互，我们将定义一个新的 Toy 方言。这个方言将建模 Toy 语言的结构，并为高层分析和变换提供便捷的途径。

```c++
/// This is the definition of the Toy dialect. A dialect inherits from
/// mlir::Dialect and registers custom attributes, operations, and types. It can
/// also override virtual methods to change some general behavior, which will be
/// demonstrated in later chapters of the tutorial.
class ToyDialect : public mlir::Dialect {
public:
  explicit ToyDialect(mlir::MLIRContext *ctx);

  /// Provide a utility accessor to the dialect namespace.
  static llvm::StringRef getDialectNamespace() { return "toy"; }

  /// An initializer called from the constructor of ToyDialect that is used to
  /// register attributes, operations, types, and more within the Toy dialect.
  void initialize();
};
```

这是方言的 C++ 定义，但 MLIR 也支持通过 [TableGen](https://llvm.org/docs/TableGen/ProgRef.html) 声明式地定义方言。使用声明式规范更加简洁，因为它消除了定义新方言时的大量样板代码。它还支持轻松生成方言文档，这些文档可以直接与方言定义一起编写。在这种声明式格式中，toy 方言将被指定为：

```tablegen
// Provide a definition of the 'toy' dialect in the ODS framework so that we
// can define our operations.
def Toy_Dialect : Dialect {
  // The namespace of our dialect, this corresponds 1-1 with the string we
  // provided in `ToyDialect::getDialectNamespace`.
  let name = "toy";

  // A short one-line summary of our dialect.
  let summary = "A high-level dialect for analyzing and optimizing the "
                "Toy language";

  // A much longer description of our dialect.
  let description = [{
    The Toy language is a tensor-based language that allows you to define
    functions, perform some math computation, and print results. This dialect
    provides a representation of the language that is amenable to analysis and
    optimization.
  }];

  // The C++ namespace that the dialect class definition resides in.
  let cppNamespace = "toy";
}
```

要查看它生成了什么内容，我们可以运行 `mlir-tblgen` 命令并指定 `gen-dialect-decls` 动作：

```shell
${build_root}/bin/mlir-tblgen -gen-dialect-decls ${mlir_src_root}/examples/toy/Ch2/include/toy/Ops.td -I ${mlir_src_root}/include/
```

在方言被定义之后，现在可以将其加载到 MLIRContext 中：

```c++
  context.loadDialect<ToyDialect>();
```

默认情况下，`MLIRContext` 仅加载[内置方言（Builtin Dialect）](../../Dialects/Builtin.md)，它提供了一些核心 IR 组件，这意味着其他方言（如我们的 `Toy` 方言）必须显式加载。

## 定义 Toy 操作（Operation）

现在我们有了一个 `Toy` 方言，可以开始定义操作了。这将允许提供语义信息，使得系统的其他部分能够接入。作为示例，让我们逐步创建一个 `toy.constant` 操作。这个操作将表示 Toy 语言中的常量值。

```mlir
 %4 = "toy.constant"() {value = dense<1.0> : tensor<2x3xf64>} : () -> tensor<2x3xf64>
```

该操作接受零个操作数，一个名为 `value` 的 [密集元素（dense elements）属性](../../Dialects/Builtin.md/#densetypedelementsattr) 来表示常量值，并返回一个 [RankedTensorType](../../Dialects/Builtin.md/#rankedtensortype) 的单一结果。操作类继承自 [CRTP](https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern) 模式的 `mlir::Op` 类，该类还接受一些可选的 [*Traits*](../../Traits) 来定制其行为。`Traits` 是一种机制，通过它我们可以向 Operation 注入额外行为，例如额外的访问器、验证等。让我们看看下面描述的常量操作的可能定义：

```c++
class ConstantOp : public mlir::Op<
                     /// `mlir::Op` is a CRTP class, meaning that we provide the
                     /// derived class as a template parameter.
                     ConstantOp,
                     /// The ConstantOp takes zero input operands.
                     mlir::OpTrait::ZeroOperands,
                     /// The ConstantOp returns a single result.
                     mlir::OpTrait::OneResult,
                     /// We also provide a utility `getType` accessor that
                     /// returns the TensorType of the single result.
                     mlir::OpTrait::OneTypedResult<TensorType>::Impl> {

 public:
  /// Inherit the constructors from the base Op class.
  using Op::Op;

  /// Provide the unique name for this operation. MLIR will use this to register
  /// the operation and uniquely identify it throughout the system. The name
  /// provided here must be prefixed by the parent dialect namespace followed
  /// by a `.`.
  static llvm::StringRef getOperationName() { return "toy.constant"; }

  /// Return the value of the constant by fetching it from the attribute.
  mlir::DenseElementsAttr getValue();

  /// Operations may provide additional verification beyond what the attached
  /// traits provide.  Here we will ensure that the specific invariants of the
  /// constant operation are upheld, for example the result type must be
  /// of TensorType and matches the type of the constant `value`.
  LogicalResult verifyInvariants();

  /// Provide an interface to build this operation from a set of input values.
  /// This interface is used by the `builder` classes to allow for easily
  /// generating instances of this operation:
  ///   mlir::OpBuilder::create<ConstantOp>(...)
  /// This method populates the given `state` that MLIR uses to create
  /// operations. This state is a collection of all of the discrete elements
  /// that an operation may contain.
  /// Build a constant with the given return type and `value` attribute.
  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Type result, mlir::DenseElementsAttr value);
  /// Build a constant and reuse the type from the given 'value'.
  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::DenseElementsAttr value);
  /// Build a constant by broadcasting the given 'value'.
  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    double value);
};
```

然后我们可以在 `ToyDialect` 的初始化器中注册这个操作：

```c++
void ToyDialect::initialize() {
  addOperations<ConstantOp>();
}
```

### Op vs Operation：使用 MLIR 操作

既然我们已经定义了一个操作，我们会想要访问和变换它。在 MLIR 中，有两个与操作相关的主要类：`Operation` 和 `Op`。`Operation` 类用于通用地建模所有操作。它是"不透明的"，意味着它不描述特定操作或操作类型的属性。相反，`Operation` 类提供了操作实例的通用 API。另一方面，每种特定类型的操作由 `Op` 派生类表示。例如，`ConstantOp` 表示一个具有零个输入和一个输出的操作，其输出始终设置为相同的值。`Op` 派生类充当围绕 `Operation*` 的智能指针包装器，提供特定于操作的访问器方法和操作的类型安全属性。这意味着当我们定义 Toy 操作时，我们实际上是在为构建 `Operation` 类并与之交互定义一个清晰、语义有用的接口。这就是为什么我们的 `ConstantOp` 没有定义任何类字段；此操作的所有数据都存储在引用的 `Operation` 中。此设计的一个副作用是，我们始终"按值"传递 `Op` 派生类，而不是按引用或指针（*按值传递*是 MLIR 中的常见习惯用法，同样适用于属性、类型等）。给定一个通用的 `Operation*` 实例，我们始终可以使用 LLVM 的类型转换基础设施获取特定的 `Op` 实例：

```c++
void processConstantOp(mlir::Operation *operation) {
  ConstantOp op = llvm::dyn_cast<ConstantOp>(operation);

  // This operation is not an instance of `ConstantOp`.
  if (!op)
    return;

  // Get the internal operation instance wrapped by the smart pointer.
  mlir::Operation *internalOperation = op.getOperation();
  assert(internalOperation == operation &&
         "these operation instances are the same");
}
```

### 使用操作定义规范（ODS）框架

除了特化 `mlir::Op` C++ 模板之外，MLIR 还支持以声明式方式定义操作。这是通过 [操作定义规范（Operation Definition Specification）](../../DefiningDialects/Operations.md) 框架实现的。关于操作的事实被简洁地写入 TableGen 记录中，该记录将在编译时展开为等效的 `mlir::Op` C++ 模板特化。给定其简洁性、简明性以及在 C++ API 变更时保持稳定性方面的普遍优势，使用 ODS 框架是 MLIR 中定义操作的推荐方式。

让我们看看如何定义与我们的 ConstantOp 等效的 ODS：

ODS 中的操作通过继承 `Op` 类来定义。为了简化我们的操作定义，我们将为 Toy 方言中的操作定义一个基类。

```tablegen
// Base class for toy dialect operations. This operation inherits from the base
// `Op` class in OpBase.td, and provides:
//   * The parent dialect of the operation.
//   * The mnemonic for the operation, or the name without the dialect prefix.
//   * A list of traits for the operation.
class Toy_Op<string mnemonic, list<Trait> traits = []> :
    Op<Toy_Dialect, mnemonic, traits>;
```

所有预备部分都已定义完毕，我们可以开始定义常量操作了。

我们通过继承上面的基类 'Toy_Op' 来定义一个 toy 操作。这里我们提供助记符（mnemonic）和该操作的 traits 列表。此处的 [助记符](../../DefiningDialects/Operations.md/#operation-name) 与 `ConstantOp::getOperationName` 中给出的匹配，但没有方言前缀 `toy.`。这里从我们的 C++ 定义中缺失的是 `ZeroOperands` 和 `OneResult` traits；这些将根据我们稍后定义的 `arguments` 和 `results` 字段自动推导。

```tablegen
def ConstantOp : Toy_Op<"constant"> {
}
```

此时你可能想知道 TableGen 生成的 C++ 代码是什么样子的。只需运行 `mlir-tblgen` 命令并指定 `gen-op-decls` 或 `gen-op-defs` 动作即可：

```shell
${build_root}/bin/mlir-tblgen -gen-op-defs ${mlir_src_root}/examples/toy/Ch2/include/toy/Ops.td -I ${mlir_src_root}/include/
```

根据所选的动作，这将打印出 `ConstantOp` 类的声明或其实现。将此输出与手工编写的实现进行比较在开始使用 TableGen 时非常有帮助。

#### 定义参数和结果

在操作的基本外壳定义好之后，我们现在可以提供操作的 [输入（input）](../../DefiningDialects/Operations.md/#operation-arguments) 和 [输出（output）](../../DefiningDialects/Operations.md/#operation-results)。操作的输入或参数可以是 SSA 操作数值的属性或类型。结果对应于操作产生的值的一组类型：

```tablegen
def ConstantOp : Toy_Op<"constant"> {
  // The constant operation takes an attribute as the only input.
  // `F64ElementsAttr` corresponds to a 64-bit floating-point ElementsAttr.
  let arguments = (ins F64ElementsAttr:$value);

  // The constant operation returns a single value of TensorType.
  // F64Tensor corresponds to a 64-bit floating-point TensorType.
  let results = (outs F64Tensor);
}
```

通过为参数或结果提供名称，例如 `$value`，ODS 将自动生成匹配的访问器：`DenseElementsAttr ConstantOp::value()`。

#### 添加文档

定义操作后的下一步是为其编写文档。操作可以提供 [`summary` 和 `description`](../../DefiningDialects/Operations.md/#operation-documentation) 字段来描述操作的语义。此信息对方言用户非常有用，甚至可以用来自动生成 Markdown 文档。

```tablegen
def ConstantOp : Toy_Op<"constant"> {
  // Provide a summary and description for this operation. This can be used to
  // auto-generate documentation of the operations within our dialect.
  let summary = "constant operation";
  let description = [{
    Constant operation turns a literal into an SSA value. The data is attached
    to the operation as an attribute. For example:

      %0 = "toy.constant"()
         { value = dense<[[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]> : tensor<2x3xf64> }
        : () -> tensor<2x3xf64>
  }];

  // The constant operation takes an attribute as the only input.
  // `F64ElementsAttr` corresponds to a 64-bit floating-point ElementsAttr.
  let arguments = (ins F64ElementsAttr:$value);

  // The generic call operation returns a single value of TensorType.
  // F64Tensor corresponds to a 64-bit floating-point TensorType.
  let results = (outs F64Tensor);
}
```

#### 验证操作语义

至此我们已经涵盖了原始 C++ 操作定义的大部分内容。接下来需要定义的是验证器（verifier）。幸运的是，与命名访问器类似，ODS 框架将根据我们给出的约束自动生成大量的必要验证逻辑。这意味着我们不需要验证返回类型的结构，甚至不需要验证输入属性 `value`。在许多情况下，ODS 操作甚至不需要额外的验证。若要添加额外的验证逻辑，操作可以重写 [`verifier`](../../DefiningDialects/Operations.md/#custom-verifier-code) 字段。`verifier` 字段允许定义一个 C++ 代码块，该代码块将作为 `ConstantOp::verify` 的一部分运行。此代码块可以假定操作的所有其他不变量已经过验证：

```tablegen
def ConstantOp : Toy_Op<"constant"> {
  // Provide a summary and description for this operation. This can be used to
  // auto-generate documentation of the operations within our dialect.
  let summary = "constant operation";
  let description = [{
    Constant operation turns a literal into an SSA value. The data is attached
    to the operation as an attribute. For example:

      %0 = "toy.constant"()
         { value = dense<[[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]> : tensor<2x3xf64> }
        : () -> tensor<2x3xf64>
  }];

  // The constant operation takes an attribute as the only input.
  // `F64ElementsAttr` corresponds to a 64-bit floating-point ElementsAttr.
  let arguments = (ins F64ElementsAttr:$value);

  // The generic call operation returns a single value of TensorType.
  // F64Tensor corresponds to a 64-bit floating-point TensorType.
  let results = (outs F64Tensor);

  // Add additional verification logic to the constant operation. Setting this bit
  // to `1` will generate a `::llvm::LogicalResult verify()` declaration on the
  // operation class that is called after ODS constructs have been verified, for
  // example the types of arguments and results. We implement additional verification
  // in the definition of this `verify` method in the C++ source file.
  let hasVerifier = 1;
}
```

#### 附加 `build` 方法

从原始 C++ 示例中最后缺失的组件是 `build` 方法。ODS 可以自动生成一些简单的 build 方法，在这种情况下它将为我们生成第一个 build 方法。对于其余的，我们定义 [`builders`](../../DefiningDialects/Operations.md/#custom-builder-methods) 字段。该字段接受一个 `OpBuilder` 对象的列表，这些对象接受一个对应于 C++ 参数列表的字符串，以及一个可选的代码块，用于内联指定实现。

```tablegen
def ConstantOp : Toy_Op<"constant"> {
  ...

  // Add custom build methods for the constant operation. These methods populate
  // the `state` that MLIR uses to create operations, i.e. these are used when
  // using `ConstantOp::create(builder, ...)`.
  let builders = [
    // Build a constant with a given constant tensor value.
    OpBuilder<(ins "DenseElementsAttr":$value), [{
      // Call into an autogenerated `build` method.
      build(builder, result, value.getType(), value);
    }]>,

    // Build a constant with a given constant floating-point value. This builder
    // creates a declaration for `ConstantOp::build` with the given parameters.
    OpBuilder<(ins "double":$value)>
  ];
}
```

#### 指定自定义汇编格式

此时我们可以生成"Toy IR"了。例如，以下 Toy 代码：

```toy
# User defined generic function that operates on unknown shaped arguments.
def multiply_transpose(a, b) {
  return transpose(a) * transpose(b);
}

def main() {
  var a<2, 3> = [[1, 2, 3], [4, 5, 6]];
  var b<2, 3> = [1, 2, 3, 4, 5, 6];
  var c = multiply_transpose(a, b);
  var d = multiply_transpose(b, a);
  print(d);
}
```

会生成以下 IR：

```mlir
module {
  "toy.func"() ({
  ^bb0(%arg0: tensor<*xf64> loc("test/Examples/Toy/Ch2/codegen.toy":4:1), %arg1: tensor<*xf64> loc("test/Examples/Toy/Ch2/codegen.toy":4:1)):
    %0 = "toy.transpose"(%arg0) : (tensor<*xf64>) -> tensor<*xf64> loc("test/Examples/Toy/Ch2/codegen.toy":5:10)
    %1 = "toy.transpose"(%arg1) : (tensor<*xf64>) -> tensor<*xf64> loc("test/Examples/Toy/Ch2/codegen.toy":5:25)
    %2 = "toy.mul"(%0, %1) : (tensor<*xf64>, tensor<*xf64>) -> tensor<*xf64> loc("test/Examples/Toy/Ch2/codegen.toy":5:25)
    "toy.return"(%2) : (tensor<*xf64>) -> () loc("test/Examples/Toy/Ch2/codegen.toy":5:3)
  }) {sym_name = "multiply_transpose", type = (tensor<*xf64>, tensor<*xf64>) -> tensor<*xf64>} : () -> () loc("test/Examples/Toy/Ch2/codegen.toy":4:1)
  "toy.func"() ({
    %0 = "toy.constant"() {value = dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>} : () -> tensor<2x3xf64> loc("test/Examples/Toy/Ch2/codegen.toy":9:17)
    %1 = "toy.reshape"(%0) : (tensor<2x3xf64>) -> tensor<2x3xf64> loc("test/Examples/Toy/Ch2/codegen.toy":9:3)
    %2 = "toy.constant"() {value = dense<[1.000000e+00, 2.000000e+00, 3.000000e+00, 4.000000e+00, 5.000000e+00, 6.000000e+00]> : tensor<6xf64>} : () -> tensor<6xf64> loc("test/Examples/Toy/Ch2/codegen.toy":10:17)
    %3 = "toy.reshape"(%2) : (tensor<6xf64>) -> tensor<2x3xf64> loc("test/Examples/Toy/Ch2/codegen.toy":10:3)
    %4 = "toy.generic_call"(%1, %3) {callee = @multiply_transpose} : (tensor<2x3xf64>, tensor<2x3xf64>) -> tensor<*xf64> loc("test/Examples/Toy/Ch2/codegen.toy":11:11)
    %5 = "toy.generic_call"(%3, %1) {callee = @multiply_transpose} : (tensor<2x3xf64>, tensor<2x3xf64>) -> tensor<*xf64> loc("test/Examples/Toy/Ch2/codegen.toy":12:11)
    "toy.print"(%5) : (tensor<*xf64>) -> () loc("test/Examples/Toy/Ch2/codegen.toy":13:3)
    "toy.return"() : () -> () loc("test/Examples/Toy/Ch2/codegen.toy":8:1)
  }) {sym_name = "main", type = () -> ()} : () -> () loc("test/Examples/Toy/Ch2/codegen.toy":8:1)
} loc(unknown)
```

这里需要注意的一点是，我们所有的 Toy 操作都使用通用汇编格式打印。这种格式就是本章开头拆解 `toy.transpose` 时所展示的格式。MLIR 允许操作定义自己的自定义汇编格式，可以是 [声明式的](../../DefiningDialects/Operations.md/#declarative-assembly-format)，也可以是通过 C++ 命令式地指定。定义自定义汇编格式可以通过去除通用格式所要求的大量冗余内容，将生成的 IR 调整为更可读的形式。让我们通过一个我们希望简化的操作格式示例来说明。

##### `toy.print`

当前 `toy.print` 的形式有些冗长。有很多额外的字符我们想要剥离掉。让我们首先思考一下 `toy.print` 的良好格式应该是什么样子，再看看如何实现它。查看 `toy.print` 的基本内容，我们得到：

```mlir
toy.print %5 : tensor<*xf64> loc(...)
```

这里我们已经将大部分的格式精简到了最基本的要素，并且变得可读性更强。要提供自定义汇编格式，操作可以重写 `hasCustomAssemblyFormat` 字段（用于 C++ 格式），或重写 `assemblyFormat` 字段（用于声明式格式）。让我们先看 C++ 变体，因为声明式格式在内部映射到的就是这种形式。

```tablegen
/// Consider a stripped definition of `toy.print` here.
def PrintOp : Toy_Op<"print"> {
  let arguments = (ins F64Tensor:$input);

  // Divert the printer and parser to `parse` and `print` methods on our operation,
  // to be implemented in the .cpp file. More details on these methods is shown below.
  let hasCustomAssemblyFormat = 1;
}
```

打印机和解析器的 C++ 实现如下所示：

```c++
/// The 'OpAsmPrinter' class is a stream that will allows for formatting
/// strings, attributes, operands, types, etc.
void PrintOp::print(mlir::OpAsmPrinter &printer) {
  printer << "toy.print " << op.input();
  printer.printOptionalAttrDict(op.getAttrs());
  printer << " : " << op.input().getType();
}

/// The 'OpAsmParser' class provides a collection of methods for parsing
/// various punctuation, as well as attributes, operands, types, etc. Each of
/// these methods returns a `ParseResult`. This class is a wrapper around
/// `LogicalResult` that can be converted to a boolean `true` value on failure,
/// or `false` on success. This allows for easily chaining together a set of
/// parser rules. These rules are used to populate an `mlir::OperationState`
/// similarly to the `build` methods described above.
mlir::ParseResult PrintOp::parse(mlir::OpAsmParser &parser,
                                 mlir::OperationState &result) {
  // Parse the input operand, the attribute dictionary, and the type of the
  // input.
  mlir::OpAsmParser::UnresolvedOperand inputOperand;
  mlir::Type inputType;
  if (parser.parseOperand(inputOperand) ||
      parser.parseOptionalAttrDict(result.attributes) || parser.parseColon() ||
      parser.parseType(inputType))
    return mlir::failure();

  // Resolve the input operand to the type we parsed in.
  if (parser.resolveOperand(inputOperand, inputType, result.operands))
    return mlir::failure();

  return mlir::success();
}
```

在定义好 C++ 实现之后，让我们看看如何将其映射到 [声明式格式](../../DefiningDialects/Operations.md/#declarative-assembly-format)。声明式格式主要由三种不同的组件构成：

*   **指令（Directives）**
    -   一种内置函数类型，带有一组可选的参数。
*   **字面量（Literals）**
    -   由 \`\` 包围的关键字或标点符号。
*   **变量（Variables）**
    -   在操作本身上注册的实体，即参数（属性或操作数）、结果、后继等。在 `PrintOp` 的例子中，变量就是 `$input`。

我们的 C++ 格式的直接映射如下：

```tablegen
/// Consider a stripped definition of `toy.print` here.
def PrintOp : Toy_Op<"print"> {
  let arguments = (ins F64Tensor:$input);

  // In the following format we have two directives, `attr-dict` and `type`.
  // These correspond to the attribute dictionary and the type of a given
  // variable represectively.
  let assemblyFormat = "$input attr-dict `:` type($input)";
}
```

[声明式格式](../../DefiningDialects/Operations.md/#declarative-assembly-format) 还有更多有趣的功能，所以在用 C++ 实现自定义格式之前务必查看一下。在美化了我们的一些操作的格式之后，我们现在得到了一个更加可读的输出：

```mlir
module {
  toy.func @multiply_transpose(%arg0: tensor<*xf64>, %arg1: tensor<*xf64>) -> tensor<*xf64> {
    %0 = toy.transpose(%arg0 : tensor<*xf64>) to tensor<*xf64> loc("test/Examples/Toy/Ch2/codegen.toy":5:10)
    %1 = toy.transpose(%arg1 : tensor<*xf64>) to tensor<*xf64> loc("test/Examples/Toy/Ch2/codegen.toy":5:25)
    %2 = toy.mul %0, %1 : tensor<*xf64> loc("test/Examples/Toy/Ch2/codegen.toy":5:25)
    toy.return %2 : tensor<*xf64> loc("test/Examples/Toy/Ch2/codegen.toy":5:3)
  } loc("test/Examples/Toy/Ch2/codegen.toy":4:1)
  toy.func @main() {
    %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64> loc("test/Examples/Toy/Ch2/codegen.toy":9:17)
    %1 = toy.reshape(%0 : tensor<2x3xf64>) to tensor<2x3xf64> loc("test/Examples/Toy/Ch2/codegen.toy":9:3)
    %2 = toy.constant dense<[1.000000e+00, 2.000000e+00, 3.000000e+00, 4.000000e+00, 5.000000e+00, 6.000000e+00]> : tensor<6xf64> loc("test/Examples/Toy/Ch2/codegen.toy":10:17)
    %3 = toy.reshape(%2 : tensor<6xf64>) to tensor<2x3xf64> loc("test/Examples/Toy/Ch2/codegen.toy":10:3)
    %4 = toy.generic_call @multiply_transpose(%1, %3) : (tensor<2x3xf64>, tensor<2x3xf64>) -> tensor<*xf64> loc("test/Examples/Toy/Ch2/codegen.toy":11:11)
    %5 = toy.generic_call @multiply_transpose(%3, %1) : (tensor<2x3xf64>, tensor<2x3xf64>) -> tensor<*xf64> loc("test/Examples/Toy/Ch2/codegen.toy":12:11)
    toy.print %5 : tensor<*xf64> loc("test/Examples/Toy/Ch2/codegen.toy":13:3)
    toy.return loc("test/Examples/Toy/Ch2/codegen.toy":8:1)
  } loc("test/Examples/Toy/Ch2/codegen.toy":8:1)
} loc(unknown)
```

上面我们介绍了在 ODS 框架中定义操作的几个概念，但还有很多我们没有机会涉及的内容：区域（region）、可变参数（variadic operand）等。请查看 [完整规范](../../DefiningDialects/Operations.md) 以获取更多详细信息。

## 完整的 Toy 示例

现在我们可以生成我们的"Toy IR"了。你可以构建 `toyc-ch2` 并在上面的示例上亲自尝试：`toyc-ch2 test/Examples/Toy/Ch2/codegen.toy -emit=mlir -mlir-print-debuginfo`。我们还可以检查我们的 RoundTrip：`toyc-ch2 test/Examples/Toy/Ch2/codegen.toy -emit=mlir -mlir-print-debuginfo 2> codegen.mlir` 然后 `toyc-ch2 codegen.mlir -emit=mlir`。你还应该使用 `mlir-tblgen` 处理最终的 `.td` 定义文件并研究生成的 C++ 代码。

此时，MLIR 已经知道我们的 Toy 方言和操作了。在 [下一章](Ch_03_高层语言特定分析与变换.md) 中，我们将利用我们新的方言为 Toy 语言实现一些高层语言特定的分析和变换。
