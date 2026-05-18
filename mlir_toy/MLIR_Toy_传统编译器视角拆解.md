# MLIR Toy Tutorial：传统编译器视角拆解

> 面向读者：懂传统编译器（Lex/Parse/AST → IR → 优化 → 代码生成），但对 MLIR 的 Dialect/Op/Region/TableGen 感到陌生的开发者。

---

## Ch1：Toy 语言与 AST

**你已有的知识：** 这是你最熟悉的部分——词法分析 + 递归下降解析 → AST。和 LLVM Kaleidoscope Tutorial 的前两章完全一样。

**新概念：零。** 唯一需要留意的是 Toy 语言的几个特点：
- 张量类型，秩 ≤ 2
- 函数是泛型的（参数 shape 未知）
- 调用时根据参数 shape 进行函数特化——这是后续内联和 shape inference 的动机

**一句话：** 这就是一个 toy 前端，和传统编译器的前端没有区别。

---

## Ch2：生成基础 MLIR —— 这是最大的认知障碍

### 2.1 先理解 IR 的形态

看一段生成的 MLIR：

```mlir
%t_tensor = "toy.transpose"(%tensor) {inplace = true}
    : (tensor<2x3xf64>) -> tensor<3x2xf64>
    loc("example/file/path":12:1)
```

用传统编译器的概念拆：

| MLIR 部分 | 类比 LLVM IR | 说明 |
|-----------|-------------|------|
| `%t_tensor` | `%t_tensor` | SSA 值，和 LLVM 的命名/匿名临时变量**完全一样** |
| `"toy.transpose"` | `transpose` 助记符 | 操作的名字，格式是 `方言名.操作名` |
| `(%tensor)` | 操作数 | 就是 SSA use，和 LLVM 的 `%y = add %x, 1` 中的 `%x` 一样 |
| `{inplace = true}` | 常量立即数/元数据 | 编译期已知的常量，不能是 SSA 值，有点像 LLVM 的 metadata |
| `: (input_type) -> result_type` | 类型标注 | MLIR 的类型系统是自定义的，这里是函数式写法 |
| `loc(...)` | `!dbg` metadata | 源码位置，唯一区别是 MLIR 里**每个 op 强制要有**，不能丢 |

**关键洞察：** MLIR 的通用格式就是 `%result = "dialect.opname"(operands) {attributes} : (input_types) -> (output_types) loc(source_location)`。这是一个**统一的语法外壳**，所有自定义的 op 都套在这个壳里。

### 2.2 Operation 的数据结构——比 LLVM Instruction 更抽象

传统编译器里，指令大概是这样：

```cpp
class Instruction {
  Opcode opcode;       // add, mul, call, ...
  Type *resultType;
  vector<Value*> operands;
};
```

MLIR 把 `Opcode` 换成了一个更通用的结构：

```cpp
// MLIR 的 Operation（对应 LLVM 的 Instruction）
class Operation {
  OperationName name;           // 等同于 Opcode，但是字符串
  vector<Value> operands;       // SSA 操作数
  vector<Value> results;        // SSA 结果（可以有多个）
  DictionaryAttr attrs;         // 编译期常量属性
  vector<Region> regions;       // 嵌套的子区域（这是 LLVM 没有的！）
  vector<Block> successorBlocks; // 分支目标（br/call 用）
  Location loc;                 // 源码位置，强制不可丢失
};
```

**几个重要的与传统编译器的差异：**

1. **Operation 可以有多个结果**：不像 LLVM 的 `add` 只有一个结果，一个 MLIR op 可以返回多个值，比如 `%a, %b = "my.dialect.foo"() -> (i32, f64)`
2. **Region 是 MLIR 最大的创新**：一个 Operation 内部可以嵌套另一个完整的控制流图。这意味着 `func.func` 自己就是一个有 Region 的 op，Region 里是函数的 body
3. **Attribute 是编译期常量**：对应 LLVM 的 `ConstantInt` 或 metadata（比如 `dense<1.0>`）

### 2.3 TableGen / ODS —— 别被名字吓到

这是传统编译器背景的人**最容易卡住的地方**。看这段：

```tablegen
def ConstantOp : Toy_Op<"constant"> {
  let arguments = (ins F64ElementsAttr:$value);
  let results = (outs F64Tensor);
}
```

**它到底是什么？** 它就是一个**编译时的 C++ 代码生成器**。

类比：你在 LLVM 里手写 `ConstantInt::Create(...)`，写 parser、printer、verifier。MLIR 说这些样板代码太多了，你写 TableGen 声明，我帮你生成。

```
TableGen 描述    →   编译时 mlir-tblgen 工具   →   生成 C++ 类
                                                  （包括构造、访问器、打印、解析、验证）
```

**所以 `.td` 文件不是运行时配置，它就是你 C++ 源码的一部分。** 类比 CMake 生成头文件，或者 protobuf 生成 `.pb.h`。

### 2.4 Op vs Operation

这其实很简单，就是**智能指针 vs 裸指针**：

| MLIR | 传统编译器类比 |
|------|-------------|
| `Operation` | `Instruction *` —— 通用的、不透明的指针 |
| `Op` (如 `ConstantOp`) | `llvm::dyn_cast<AddInst>(inst)` 获得的带类型信息的东西 |

`ConstantOp` **没有任何自己的数据字段**。所有数据都存在它包装的那个 `Operation` 里。`ConstantOp::value()` 只是一个快捷访问器，帮你从 `Operation` 的 attributes 字典里取出 `value` 属性的值。

### 2.5 自定义汇编格式 —— 就是自定义 parser/printer

通用格式 `"toy.transpose"(...) : (...) -> (...)` 太丑。自定义格式后：

```mlir
// 通用格式
%0 = "toy.transpose"(%arg0) : (tensor<*xf64>) -> tensor<*xf64>

// 自定义格式
%0 = toy.transpose(%arg0 : tensor<*xf64>) to tensor<*xf64>
```

这就是手写了一个 parser 和 printer。和你在传统编译器里给 IR 设计打印格式是一样的——只不过 MLIR 让你可以在 TableGen 里声明式地指定，不用手写 C++。

---

## Ch3：高层分析与变换 —— 就是 InstCombine

### 核心类比

| MLIR | LLVM |
|------|------|
| `RewritePattern` | `InstCombine` 的一条规则 |
| `matchAndRewrite()` | `visitAdd(BinaryOperator &I)` 的 body |
| `PatternRewriter` | `IRBuilder` + `replaceInstWithValue` |
| `Canonicalizer Pass` | `InstCombinePass` |
| `getCanonicalizationPatterns()` | 注册你的 combine 规则 |
| `hasCanonicalizer = 1` | 告诉框架 "这个 op 有 combine 规则" |

### 具体例子

```cpp
// MLIR: 消除 transpose(transpose(x)) → x
struct SimplifyRedundantTranspose : public OpRewritePattern<TransposeOp> {
  LogicalResult matchAndRewrite(TransposeOp op,
                                PatternRewriter &rewriter) const override {
    Value input = op.getOperand();
    TransposeOp inputOp = input.getDefiningOp<TransposeOp>();
    if (!inputOp) return failure();    // 没匹配上
    rewriter.replaceOp(op, {inputOp.getOperand()});  // 替换
    return success();
  }
};
```

如果用 LLVM IR 写等价的东西（伪代码）：

```cpp
// 消除 load(store(x)) → x（类似优化）
bool visitLoad(LoadInst &LI) {
  Value *ptr = LI.getPointerOperand();
  if (auto *SI = dyn_cast<StoreInst>(ptr->getDefiningOp())) {
    replaceInstWithValue(LI, SI->getValueOperand());
    return true;
  }
  return false;
}
```

**逻辑完全一样。** 区别只是 MLIR 用 `failure()/success()` 返回值而不是 bool，用 `rewriter.replaceOp()` 而不是直接用 `replaceInstWithValue`。

### DRR（声明式重写规则）

```tablegen
// C++ 写了十几行，DRR 一行:
def ReshapeReshapeOptPattern : Pat<(ReshapeOp(ReshapeOp $arg)),
                                   (ReshapeOp $arg)>;
```

这就是一个**模式匹配器生成器**：你描述模式树 → TableGen 生成 C++ 匹配代码。类比 LLVM 的 `PatternMatch.h` 里的 `m_Add(m_Value(X), m_Constant(C))` 的 DSL，只是 MLIR 把它做到了编译时代码生成层面。

### Pure trait

```tablegen
def TransposeOp : Toy_Op<"transpose", [Pure]> {...}
```

`Pure` trait 告诉 MLIR "这个 op 没有副作用"。等价于 LLVM IR 里 `add` 指令天然无副作用，而 `call` 有。在 MLIR 里，**每个自定义 op 默认被认为可能有副作用**（保守），除非你显式加 `Pure` trait。如果不加，Canonicalizer 不会帮你删除"死"的 TransposeOp。

---

## Ch4：Interfaces —— 就是虚函数表，但是给 Op/Dialect

### 问题

你有 20 种不同的 Dialect，每个都有自己的 `func`、`call`。你写了一个内联 pass，不想为每个 Dialect 写一遍。怎么让不同的 Dialect 用同一个内联算法？

传统编译器的答案：**虚函数 / 接口**。

```cpp
// 传统方法：
class Callable {
  virtual Region* getCallableRegion() = 0;
  virtual Value getCallee() = 0;
};
```

MLIR 的答案也一样，只不过起名叫 **Interface**：

```cpp
// MLIR 的 Interface 就是 C++ 的纯虚类
struct CallOpInterface {
  virtual CallInterfaceCallable getCallableForCallee() = 0;
  virtual operand_range getArgOperands() = 0;
};
```

**关键区别：** MLIR 的 Interface 也可以走 ODS（TableGen）生成，但本质就是虚函数。

### 两个层级

| 接口类型 | 挂载位置 | 类比 |
|---------|---------|------|
| **Operation Interface** | 挂在一个具体的 Op 上 | 这个 Op 实现了某个接口，比如 `CallOpInterface` |
| **Dialect Interface** | 挂在整个 Dialect 上 | 这个 Dialect 提供某个能力，比如 "我支持内联" |

### 内联的完整流程

```
1. ToyDialect 注册 ToyInlinerInterface
   → 告诉 MLIR："我 Toy 方言支持内联，内联规则如下"

2. GenericCallOp 加 CallOpInterface trait
   → 告诉 MLIR："我是个函数调用"
   FuncOp 加 FunctionOpInterface trait
   → 告诉 MLIR："我是个函数"

3. 在 PassManager 里加 createInlinerPass()
   → MLIR 的通用内联器跑起来：
     - 找到所有实现了 CallOpInterface 的 op（比如 toy.generic_call）
     - 找到它的 callee（通过 getCallableForCallee() 拿到函数符号）
     - 找到 callee 的 body（通过 getCallableRegion() 拿到 Region）
     - 把 body 拷贝到调用点，替换 return 值
     - 处理类型不匹配（插入 toy.cast）
   → 所有 Dialect 只要实现了这些接口，都能复用同一个内联算法
```

### Shape Inference

这是又一个用 Interface 的例子。定义一个 `ShapeInferenceOpInterface`，每个 Toy op 实现 `inferShapes()` 方法。Pass 遍历所有 op，调用接口方法推导形状。

```cpp
// 传统编译器：类型推断 pass
Type *TypeCheckPass::visitExpr(Expr *E) {
  if (auto *Add = dyn_cast<AddExpr>(E))
    return Add->LHS->getType();  // 算术运算返回操作数类型
}

// MLIR：Shape Inference Pass
void ShapeInferencePass::runOnOperation() {
  for (auto &op : function) {
    if (auto shapeOp = dyn_cast<ShapeInference>(op))
      shapeOp.inferShapes();  // 每个 op 自己知道怎么推导
  }
}
```

---

## Ch5：部分 Lowering —— 就是 IR 逐层降级

### 核心概念

**Lowering = 把一种 IR 翻译成另一种 IR。**

传统编译器典型是两步：AST → LLVM IR（或三地址码）。MLIR 的关键想法是**可以做很多次**，每层只降一点：

```
Toy IR  →  Affine IR  →  SCF IR  →  LLVM IR  →  LLVM native IR
```

### 为什么需要"部分"Lowering？

Toy 里有 `toy.print`，这玩意儿没法变成 Affine 循环——它需要调 printf。所以：
- `toy.transpose`、`toy.mul` → lowering 成 Affine 循环
- `toy.print` → **暂时不动**，等下个 pass 直接 lower 到 LLVM

这就是"部分 lowering"：只转换一部分 op，剩下的保持原样。

### DialectConversion 框架

```cpp
// 1. 定义"什么是合法的"
ConversionTarget target;
target.addLegalDialect<AffineDialect, ArithDialect, MemRefDialect>();
target.addIllegalDialect<ToyDialect>();     // Toy op 不能留
target.addDynamicallyLegalOp<PrintOp>(...); // 但 PrintOp 例外

// 2. 提供转换规则
patterns.add<TransposeOpLowering, MulOpLowering>(...);

// 3. 执行
applyPartialConversion(module, target, patterns);
```

**类比传统编译器：** 这就像是定义了一个 lowering pass，从 AST 到 IR，但声明"`printf` 调用不用降，留着给后端处理"。

### 一个具体的 Lowering 例子

`toy.transpose` → 变成 Affine 循环嵌套：

```cpp
// Toy IR:
%1 = toy.transpose(%0 : tensor<2x3xf64>) to tensor<3x2xf64>

// Lowering 后:
affine.for %i = 0 to 3 {
  affine.for %j = 0 to 2 {
    %v = affine.load %0[%j, %i]    // 反转索引读
    affine.store %v, %result[%i, %j] // 正序写
  }
}
```

**注意类型也变了：** `tensor<2x3xf64>` → `memref<2x3xf64>`。Tensor 是"抽象值"（SSA 值，不占内存），MemRef 是"具象缓冲区"（在内存里有地址）。这就像是高级语言的"值类型"变成 C 的"指针+长度"。

### 利用现有 Affine 优化

Lowering 后的代码有冗余 load/store。直接加已有的 Affine pass：

```cpp
pm.addPass(createLoopFusionPass());           // 合并相邻循环
pm.addPass(createAffineScalarReplacementPass()); // 消除冗余 load/store
```

**类比：** 传统编译器里 -O1 跑完你的 pass 后再跑已有的 GVN/DCE。MLIR 的优势是你可以把现有方言的优化 pass 直接插进来，不用自己写。

---

## Ch6：Lowering 到 LLVM 与 JIT

### 全量 Lowering

Ch5 是部分 lowering，Ch6 是全量 lowering——**没有任何 Toy 或 Affine op 剩下，全部换成 LLVM Dialect op 或标准 op**。

```cpp
// 全量 lowering 需要 TypeConverter：
LLVMTypeConverter typeConverter(&getContext());
// memref<2x3xf64> → { double*, i64, [2 x i64], [2 x i64] }
//   也就是         → 指针 + offset + sizes[2] + strides[2]
```

### PrintOp 的 Lowering

`toy.print` → `printf` 调用：

```cpp
// 传统编译器：生成 IR 时调用 printf
Function *printfFn = module->getOrInsertFunction("printf", ...);
builder.CreateCall(printfFn, {formatStr, value});

// MLIR 做一样的事：
auto printfRef = getOrInsertPrintf(rewriter, module, llvmDialect);
rewriter.create<LLVM::CallOp>(loc, printfRef, ValueRange{formatStr, input});
```

### 从 MLIR 到 LLVM IR

```cpp
// 一行调用：
auto llvmModule = mlir::translateModuleToLLVMIR(module);

// 然后就可以喂给 LLVM JIT 执行了
```

### JIT 执行

```cpp
auto engine = mlir::ExecutionEngine::create(module);
engine->invoke("main");
```

**类比：** 就是 LLVM 的 `EngineBuilder` + `addModule` + `getFunction("main")`，MLIR 帮你包了一层。

---

## Ch7：复合类型 —— 就是给 IR 加 struct

### 传统编译器里加 struct

你在 LLVM IR 里加 struct 支持：`StructType::get(elemTypes)`，然后处理 `getelementptr` 索引。MLIR 里做**完全一样的事**：

### 1. 定义 Type Storage（唯一样例存储）

```cpp
struct StructTypeStorage : public TypeStorage {
  using KeyTy = ArrayRef<Type>;      // 用元素类型列表做唯一键
  bool operator==(const KeyTy &key)  // 用来去重
  static hash_code hashKey(...)       // 用来哈希
};
```

**类比 LLVM：** `StructType::get(LLVMContext, {i32, f64})` 内部会去重——同一个 context 里相同元素类型的 struct 只存在一份。MLIR 的 `TypeStorage` 就是做这个去重机制的。

### 2. 定义 Type 类

```cpp
class StructType : public Type::TypeBase<StructType, Type, StructTypeStorage> {
  static StructType get(ArrayRef<Type> elementTypes) {
    return Base::get(ctx, elementTypes);  // 去重 + 构造
  }
};
```

### 3. 注册到 Dialect

```cpp
void ToyDialect::initialize() {
  addTypes<StructType>();  // 让 Toy Dialect 知道这个类型
}
```

### 4. 加 Parser/Printer

```cpp
Type ToyDialect::parseType(DialectAsmParser &parser) const {
  // 解析  !toy.struct<tensor<*xf64>, tensor<*xf64>>
}

void ToyDialect::printType(Type type, DialectAsmPrinter &printer) const {
  // 打印  !toy.struct<tensor<*xf64>, tensor<*xf64>>
}
```

### 5. 加新的 Op + 折叠规则

新增 `toy.struct_constant` 和 `toy.struct_access`，然后加 fold 方法：

```cpp
// struct_access(struct_constant {...}, 0) → 第一个元素的值
OpFoldResult StructAccessOp::fold(FoldAdaptor adaptor) {
  auto structAttr = dyn_cast<ArrayAttr>(adaptor.getInput());
  if (!structAttr) return nullptr;
  return structAttr[index];
}
```

**类比：** 传统编译器里 `getelementptr(@global_struct, 0, 0)` → 常量折叠直接算出地址。逻辑一样。

---

## 全流程总结

把 7 章串起来，Toy Compiler 的完整 pipeline：

```
Toy 源码
  │ Ch1: Lexer + Parser → AST
  ▼
  │ Ch2: AST → Toy Dialect MLIR
  ▼
  │ Ch3: Canonicalizer（transpose消除、reshape折叠）
  ▼
  │ Ch4: Inline + Shape Inference
  ▼
  │ Ch5: 部分 Lowering（Toy → Affine + MemRef），然后跑 Affine 优化
  ▼
  │ Ch6: 全量 Lowering（剩下所有 → LLVM Dialect）
  ▼
  │ Ch6: translateModuleToLLVMIR → LLVM IR
  ▼
  │ Ch6: JIT 执行
  ▼
  输出结果
```

## 一句话记住 MLIR

MLIR 只是一个让你自定义 IR 语言（Dialect）、自定义指令（Op）、自定义类型（Type）、然后逐层翻译（Lowering）的框架。其他所有概念——TableGen、Interface、Pattern Rewrite——都是为了减少样板代码而存在的工具，核心思想没有超出传统编译器的范畴。
