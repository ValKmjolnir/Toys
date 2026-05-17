# 第7章：为 Toy 添加复合类型

[TOC]

在[上一章](Ch_06_Lowering至LLVM与代码生成.md)中，我们演示了从 Toy 前端到 LLVM IR 的端到端编译流程。在本章中，我们将扩展 Toy 语言以支持一种新的复合 `struct` 类型。

## 在 Toy 中定义 `struct`

我们需要定义的第一件事是这种类型在 `toy` 源码语言中的接口。Toy 中 `struct` 类型的一般语法如下：

```toy
# A struct is defined by using the `struct` keyword followed by a name.
struct MyStruct {
  # Inside of the struct is a list of variable declarations without initializers
  # or shapes, which may also be other previously defined structs.
  var a;
  var b;
}
```

现在结构体可以在函数中作为变量或参数使用，只需使用结构体的名称而不是 `var`。结构体的成员通过 `.` 访问运算符访问。`struct` 类型的值可以用复合初始化器初始化，或用 `{}` 包围的其他初始化器的逗号分隔列表初始化。示例如下：

```toy
struct Struct {
  var a;
  var b;
}

# User defined generic function may operate on struct types as well.
def multiply_transpose(Struct value) {
  # We can access the elements of a struct via the '.' operator.
  return transpose(value.a) * transpose(value.b);
}

def main() {
  # We initialize struct values using a composite initializer.
  Struct value = {[[1, 2, 3], [4, 5, 6]], [[1, 2, 3], [4, 5, 6]]};

  # We pass these arguments to functions like we do with variables.
  var c = multiply_transpose(value);
  print(c);
}
```

## 在 MLIR 中定义 `struct`

在 MLIR 中，我们也需要为结构体类型定义一个表示。MLIR 没有提供完全符合我们需求的类型，因此我们需要定义自己的类型。我们将简单地将 `struct` 定义为包含一组元素类型的未命名容器。`struct` 的名称及其元素仅在我们的 `toy` 编译器的 AST 中有用，因此我们不需要在 MLIR 表示中编码它。

### 定义类型类

#### 定义类型类

如[第2章](Ch_02_生成基础MLIR.md)中所述，MLIR 中的 [`Type`](../../LangRef.md/#type-system) 对象是值类型的，并依赖于具有一个内部存储对象来保存类型的实际数据。`Type` 类本身充当内部 `TypeStorage` 对象的简单包装器，该对象在 `MLIRContext` 实例中是唯一化的。当构造一个 `Type` 时，我们内部只是在构造并唯一化一个存储类的实例。

当定义一个新的包含参数化数据的 `Type`（例如 `struct` 类型，它需要额外信息来保存元素类型）时，我们需要提供一个派生的存储类。没有任何额外数据的 `singleton` 类型（例如 [`index` 类型](../../Dialects/Builtin.md/#indextype)）不需要存储类，使用默认的 `TypeStorage`。

##### 定义存储类

类型存储对象包含了构造和唯一化类型实例所需的所有数据。派生的存储类必须继承自基类 `mlir::TypeStorage`，并提供一组别名和钩子，这些别名和钩子将由 `MLIRContext` 用于唯一化。以下是我们的 `struct` 类型的存储实例的定义，每个必要的要求都在内联中详细说明：

```c++
/// This class represents the internal storage of the Toy `StructType`.
struct StructTypeStorage : public mlir::TypeStorage {
  /// The `KeyTy` is a required type that provides an interface for the storage
  /// instance. This type will be used when uniquing an instance of the type
  /// storage. For our struct type, we will unique each instance structurally on
  /// the elements that it contains.
  using KeyTy = llvm::ArrayRef<mlir::Type>;

  /// A constructor for the type storage instance.
  StructTypeStorage(llvm::ArrayRef<mlir::Type> elementTypes)
      : elementTypes(elementTypes) {}

  /// Define the comparison function for the key type with the current storage
  /// instance. This is used when constructing a new instance to ensure that we
  /// haven't already uniqued an instance of the given key.
  bool operator==(const KeyTy &key) const { return key == elementTypes; }

  /// Define a hash function for the key type. This is used when uniquing
  /// instances of the storage.
  /// Note: This method isn't necessary as both llvm::ArrayRef and mlir::Type
  /// have hash functions available, so we could just omit this entirely.
  static llvm::hash_code hashKey(const KeyTy &key) {
    return llvm::hash_value(key);
  }

  /// Define a construction function for the key type from a set of parameters.
  /// These parameters will be provided when constructing the storage instance
  /// itself, see the `StructType::get` method further below.
  /// Note: This method isn't necessary because KeyTy can be directly
  /// constructed with the given parameters.
  static KeyTy getKey(llvm::ArrayRef<mlir::Type> elementTypes) {
    return KeyTy(elementTypes);
  }

  /// Define a construction method for creating a new instance of this storage.
  /// This method takes an instance of a storage allocator, and an instance of a
  /// `KeyTy`. The given allocator must be used for *all* necessary dynamic
  /// allocations used to create the type storage and its internal.
  static StructTypeStorage *construct(mlir::TypeStorageAllocator &allocator,
                                      const KeyTy &key) {
    // Copy the elements from the provided `KeyTy` into the allocator.
    llvm::ArrayRef<mlir::Type> elementTypes = allocator.copyInto(key);

    // Allocate the storage instance and construct it.
    return new (allocator.allocate<StructTypeStorage>())
        StructTypeStorage(elementTypes);
  }

  /// The following field contains the element types of the struct.
  llvm::ArrayRef<mlir::Type> elementTypes;
};
```

##### 定义类型类

存储类定义好后，我们可以为用户可见的 `StructType` 类添加定义。这是我们将实际交互的类。

```c++
/// This class defines the Toy struct type. It represents a collection of
/// element types. All derived types in MLIR must inherit from the CRTP class
/// 'Type::TypeBase'. It takes as template parameters the concrete type
/// (StructType), the base class to use (Type), and the storage class
/// (StructTypeStorage).
class StructType : public mlir::Type::TypeBase<StructType, mlir::Type,
                                               StructTypeStorage> {
public:
  /// Inherit some necessary constructors from 'TypeBase'.
  using Base::Base;

  /// Create an instance of a `StructType` with the given element types. There
  /// *must* be at least one element type.
  static StructType get(llvm::ArrayRef<mlir::Type> elementTypes) {
    assert(!elementTypes.empty() && "expected at least 1 element type");

    // Call into a helper 'get' method in 'TypeBase' to get a uniqued instance
    // of this type. The first parameter is the context to unique in. The
    // parameters after are forwarded to the storage instance.
    mlir::MLIRContext *ctx = elementTypes.front().getContext();
    return Base::get(ctx, elementTypes);
  }

  /// Returns the element types of this struct type.
  llvm::ArrayRef<mlir::Type> getElementTypes() {
    // 'getImpl' returns a pointer to the internal storage instance.
    return getImpl()->elementTypes;
  }

  /// Returns the number of element type held by this struct.
  size_t getNumElementTypes() { return getElementTypes().size(); }
};
```

我们在 `ToyDialect` 的初始化器中注册这个类型，方式与注册操作类似：

```c++
void ToyDialect::initialize() {
  addTypes<StructType>();
}
```

（这里需要注意的重要一点是，在注册类型时，存储类的定义必须是可见的。）

有了这些，我们现在可以在从 Toy 生成 MLIR 时使用我们的 `StructType`。详见 examples/toy/Ch7/mlir/MLIRGen.cpp。

### 暴露给 ODS

在定义了一个新类型之后，我们应该让 ODS 框架意识到我们的类型，这样我们就可以在操作定义中使用它并在方言中自动生成实用工具。一个简单的示例如下：

```tablegen
// Provide a definition for the Toy StructType for use in ODS. This allows for
// using StructType in a similar way to Tensor or MemRef. We use `DialectType`
// to demarcate the StructType as belonging to the Toy dialect.
def Toy_StructType :
    DialectType<Toy_Dialect, CPred<"isa<StructType>($_self)">,
                "Toy struct type">;

// Provide a definition of the types that are used within the Toy dialect.
def Toy_Type : AnyTypeOf<[F64Tensor, Toy_StructType]>;
```

### 解析与打印

此时我们可以在 MLIR 生成和变换中使用我们的 `StructType`，但我们还不能输出或解析 `.mlir`。为此，我们需要为 `StructType` 的实例添加解析和打印支持。这可以通过重写 `ToyDialect` 上的 `parseType` 和 `printType` 方法来完成。当类型暴露给 ODS（如前一节所述）时，这些方法的声明会自动提供。

```c++
class ToyDialect : public mlir::Dialect {
public:
  /// Parse an instance of a type registered to the toy dialect.
  mlir::Type parseType(mlir::DialectAsmParser &parser) const override;

  /// Print an instance of a type registered to the toy dialect.
  void printType(mlir::Type type,
                 mlir::DialectAsmPrinter &printer) const override;
};
```

这些方法接受一个高层解析器或打印器的实例，允许轻松实现必要的功能。在深入实现之前，让我们考虑一下我们希望在打印的 IR 中为 `struct` 类型使用的语法。如 [MLIR 语言参考](../../LangRef.md/#dialect-types) 中所述，方言类型通常表示为：`! dialect-namespace < type-data >`，在特定条件下可以有一个 pretty 形式。我们的 `Toy` 解析器和打印器的职责是提供 `type-data` 部分。我们将把 `StructType` 定义为以下形式：

```
  struct-type ::= `struct` `<` type (`,` type)* `>`
```

#### 解析

解析器的实现如下所示：

```c++
/// Parse an instance of a type registered to the toy dialect.
mlir::Type ToyDialect::parseType(mlir::DialectAsmParser &parser) const {
  // Parse a struct type in the following form:
  //   struct-type ::= `struct` `<` type (`,` type)* `>`

  // NOTE: All MLIR parser function return a ParseResult. This is a
  // specialization of LogicalResult that auto-converts to a `true` boolean
  // value on failure to allow for chaining, but may be used with explicit
  // `mlir::failed/mlir::succeeded` as desired.

  // Parse: `struct` `<`
  if (parser.parseKeyword("struct") || parser.parseLess())
    return Type();

  // Parse the element types of the struct.
  SmallVector<mlir::Type, 1> elementTypes;
  do {
    // Parse the current element type.
    SMLoc typeLoc = parser.getCurrentLocation();
    mlir::Type elementType;
    if (parser.parseType(elementType))
      return nullptr;

    // Check that the type is either a TensorType or another StructType.
    if (!isa<mlir::TensorType, StructType>(elementType)) {
      parser.emitError(typeLoc, "element type for a struct must either "
                                "be a TensorType or a StructType, got: ")
          << elementType;
      return Type();
    }
    elementTypes.push_back(elementType);

    // Parse the optional: `,`
  } while (succeeded(parser.parseOptionalComma()));

  // Parse: `>`
  if (parser.parseGreater())
    return Type();
  return StructType::get(elementTypes);
}
```

#### 打印

打印器的实现如下所示：

```c++
/// Print an instance of a type registered to the toy dialect.
void ToyDialect::printType(mlir::Type type,
                           mlir::DialectAsmPrinter &printer) const {
  // Currently the only toy type is a struct type.
  StructType structType = type.cast<StructType>();

  // Print the struct type according to the parser format.
  printer << "struct<";
  llvm::interleaveComma(structType.getElementTypes(), printer);
  printer << '>';
}
```

在继续之前，让我们看一个快速示例来展示我们现在拥有的功能：

```toy
struct Struct {
  var a;
  var b;
}

def multiply_transpose(Struct value) {
}
```

会生成以下内容：

```mlir
module {
  toy.func @multiply_transpose(%arg0: !toy.struct<tensor<*xf64>, tensor<*xf64>>) {
    toy.return
  }
}
```

### 在 `StructType` 上操作

现在 `struct` 类型已经定义，并且我们可以通过 IR 进行 round-trip。下一步是添加在我们的操作中使用它的支持。

#### 更新现有操作

我们的一些现有操作，例如 `ReturnOp`，需要更新以处理 `Toy_StructType`。

```tablegen
def ReturnOp : Toy_Op<"return", [Terminator, HasParent<"FuncOp">]> {
  ...
  let arguments = (ins Variadic<Toy_Type>:$input);
  ...
}
```

#### 添加新的 `Toy` 操作

除了现有操作外，我们还将添加一些新操作，这些操作将提供对 `structs` 的更具体处理。

##### `toy.struct_constant`

这个新操作物化一个结构体的常量值。在我们当前的建模中，我们只使用一个 [数组属性（array attribute）](../../Dialects/Builtin.md/#arrayattr)，其中包含每个 `struct` 元素的一组常量值。

```mlir
  %0 = toy.struct_constant [
    dense<[[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]> : tensor<2x3xf64>
  ] : !toy.struct<tensor<*xf64>>
```

##### `toy.struct_access`

这个新操作物化一个 `struct` 值的第 N 个元素。

```mlir
  // Using %0 from above
  %1 = toy.struct_access %0[0] : !toy.struct<tensor<*xf64>> -> tensor<*xf64>
```

有了这些操作，我们可以重新审视我们的原始示例：

```toy
struct Struct {
  var a;
  var b;
}

# User defined generic function may operate on struct types as well.
def multiply_transpose(Struct value) {
  # We can access the elements of a struct via the '.' operator.
  return transpose(value.a) * transpose(value.b);
}

def main() {
  # We initialize struct values using a composite initializer.
  Struct value = {[[1, 2, 3], [4, 5, 6]], [[1, 2, 3], [4, 5, 6]]};

  # We pass these arguments to functions like we do with variables.
  var c = multiply_transpose(value);
  print(c);
}
```

最终获得一个完整的 MLIR 模块：

```mlir
module {
  toy.func @multiply_transpose(%arg0: !toy.struct<tensor<*xf64>, tensor<*xf64>>) -> tensor<*xf64> {
    %0 = toy.struct_access %arg0[0] : !toy.struct<tensor<*xf64>, tensor<*xf64>> -> tensor<*xf64>
    %1 = toy.transpose(%0 : tensor<*xf64>) to tensor<*xf64>
    %2 = toy.struct_access %arg0[1] : !toy.struct<tensor<*xf64>, tensor<*xf64>> -> tensor<*xf64>
    %3 = toy.transpose(%2 : tensor<*xf64>) to tensor<*xf64>
    %4 = toy.mul %1, %3 : tensor<*xf64>
    toy.return %4 : tensor<*xf64>
  }
  toy.func @main() {
    %0 = toy.struct_constant [
      dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>,
      dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
    ] : !toy.struct<tensor<*xf64>, tensor<*xf64>>
    %1 = toy.generic_call @multiply_transpose(%0) : (!toy.struct<tensor<*xf64>, tensor<*xf64>>) -> tensor<*xf64>
    toy.print %1 : tensor<*xf64>
    toy.return
  }
}
```

#### 优化 `StructType` 上的操作

现在我们有一些操作在 `StructType` 上操作，我们也有了许多新的常量折叠机会。

内联之后，上一节中的 MLIR 模块看起来像这样：

```mlir
module {
  toy.func @main() {
    %0 = toy.struct_constant [
      dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>,
      dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
    ] : !toy.struct<tensor<*xf64>, tensor<*xf64>>
    %1 = toy.struct_access %0[0] : !toy.struct<tensor<*xf64>, tensor<*xf64>> -> tensor<*xf64>
    %2 = toy.transpose(%1 : tensor<*xf64>) to tensor<*xf64>
    %3 = toy.struct_access %0[1] : !toy.struct<tensor<*xf64>, tensor<*xf64>> -> tensor<*xf64>
    %4 = toy.transpose(%3 : tensor<*xf64>) to tensor<*xf64>
    %5 = toy.mul %2, %4 : tensor<*xf64>
    toy.print %5 : tensor<*xf64>
    toy.return
  }
}
```

我们有多个 `toy.struct_access` 操作访问一个 `toy.struct_constant`。如[第3章](Ch_03_高层语言特定分析与变换.md)（FoldConstantReshape）所述，我们可以通过在操作定义上设置 `hasFolder` 位并提供 `*Op::fold` 方法的定义为这些 `toy` 操作添加折叠器。

```c++
/// Fold constants.
OpFoldResult ConstantOp::fold(FoldAdaptor adaptor) { return value(); }

/// Fold struct constants.
OpFoldResult StructConstantOp::fold(FoldAdaptor adaptor) {
  return value();
}

/// Fold simple struct access operations that access into a constant.
OpFoldResult StructAccessOp::fold(FoldAdaptor adaptor) {
  auto structAttr = dyn_cast_or_null<mlir::ArrayAttr>(adaptor.getInput());
  if (!structAttr)
    return nullptr;

  size_t elementIndex = index().getZExtValue();
  return structAttr[elementIndex];
}
```

为了确保 MLIR 在折叠我们的 `Toy` 操作时生成正确的常量操作（即 `TensorType` 用 `ConstantOp`，`StructType` 用 `StructConstant`），我们需要为方言钩子 `materializeConstant` 提供一个重写。这允许通用的 MLIR 操作在必要时为 `Toy` 方言创建常量。

```c++
mlir::Operation *ToyDialect::materializeConstant(mlir::OpBuilder &builder,
                                                 mlir::Attribute value,
                                                 mlir::Type type,
                                                 mlir::Location loc) {
  if (isa<StructType>(type))
    return StructConstantOp::create(builder, loc, type,
                                            cast<mlir::ArrayAttr>(value));
  return ConstantOp::create(builder, loc, type,
                                    cast<mlir::DenseElementsAttr>(value));
}
```

有了这些，我们现在可以生成能够 lowering 到 LLVM 的代码，而无需对我们的流水线进行任何更改。

```mlir
module {
  toy.func @main() {
    %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
    %1 = toy.transpose(%0 : tensor<2x3xf64>) to tensor<3x2xf64>
    %2 = toy.mul %1, %1 : tensor<3x2xf64>
    toy.print %2 : tensor<3x2xf64>
    toy.return
  }
}
```

你可以构建 `toyc-ch7` 并亲自尝试：`toyc-ch7 test/Examples/Toy/Ch7/struct-codegen.toy -emit=mlir`。关于定义自定义类型的更多细节可以在 [DefiningAttributesAndTypes](../../DefiningDialects/AttributesAndTypes.md) 中找到。
