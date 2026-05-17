# 第6章：Lowering 至 LLVM 与代码生成

[TOC]

在[上一章](Ch_05_部分Lowering至低层Dialect.md)中，我们介绍了[方言转换（Dialect Conversion）](../../DialectConversion.md)框架，并将许多 `Toy` 操作部分 lowering 到仿射循环嵌套以进行优化。在本章中，我们将最终 lowering 到 LLVM 以进行代码生成。

## Lowering 至 LLVM

对于这次 lowering，我们将再次使用方言转换框架来完成繁重的工作。不过，这一次我们将执行到 [LLVM 方言](../../Dialects/LLVM.md)的完整转换。值得庆幸的是，我们已经 lowering 了除一个之外的所有 `toy` 操作，最后一个就是 `toy.print`。在进入 LLVM 转换之前，让我们先 lowering `toy.print` 操作。我们将把此操作 lowering 到一个非仿射的循环嵌套，该循环嵌套为每个元素调用 `printf`。请注意，由于方言转换框架支持[传递性 lowering（transitive lowering）](../../../getting_started/Glossary.md/#transitive-lowering)，我们不需要直接生成 LLVM 方言中的操作。传递性 lowering 意味着转换框架可以应用多个模式来完全合法化一个操作。在这个例子中，我们生成一个结构化循环嵌套，而不是 LLVM 方言中的分支形式。只要我们有从循环操作到 LLVM 的 lowering，转换仍然会成功。

在 lowering 过程中，我们可以获取或构建 printf 的声明：

```c++
/// Return a symbol reference to the printf function, inserting it into the
/// module if necessary.
static FlatSymbolRefAttr getOrInsertPrintf(PatternRewriter &rewriter,
                                           ModuleOp module,
                                           LLVM::LLVMDialect *llvmDialect) {
  auto *context = module.getContext();
  if (module.lookupSymbol<LLVM::LLVMFuncOp>("printf"))
    return SymbolRefAttr::get(context, "printf");

  // Create a function declaration for printf, the signature is:
  //   * `i32 (i8*, ...)`
  auto llvmI32Ty = IntegerType::get(context, 32);
  auto llvmI8PtrTy =
      LLVM::LLVMPointerType::get(IntegerType::get(context, 8));
  auto llvmFnType = LLVM::LLVMFunctionType::get(llvmI32Ty, llvmI8PtrTy,
                                                /*isVarArg=*/true);

  // Insert the printf function into the body of the parent module.
  PatternRewriter::InsertionGuard insertGuard(rewriter);
  rewriter.setInsertionPointToStart(module.getBody());
  LLVM::LLVMFuncOp::create(rewriter, module.getLoc(), "printf", llvmFnType);
  return SymbolRefAttr::get(context, "printf");
}
```

既然 printf 操作的 lowering 已经定义，我们可以指定此次 lowering 所需的组件。这些组件与[上一章](Ch_05_部分Lowering至低层Dialect.md)中定义的组件大致相同。

### 转换目标

对于这次转换，除了顶层模块之外，我们将把一切都 lowering 到 LLVM 方言。

```c++
  mlir::ConversionTarget target(getContext());
  target.addLegalDialect<mlir::LLVMDialect>();
  target.addLegalOp<mlir::ModuleOp>();
```

### 类型转换器

这次 lowering 还将把当前操作的 MemRef 类型转换为 LLVM 中的表示。为执行此转换，我们使用 TypeConverter 作为 lowering 的一部分。此转换器指定一种类型如何映射到另一种类型。鉴于我们现在正在执行涉及基本块参数的更复杂的 lowering，这是必要的。因为我们没有任何特定于 Toy 方言的类型需要 lowering，默认转换器足以满足我们的用例。

```c++
  LLVMTypeConverter typeConverter(&getContext());
```

### 转换模式

现在转换目标已经定义，我们需要提供用于 lowering 的模式。在此编译阶段，我们拥有 `toy`、`affine`、`arith` 和 `std` 操作的组合。幸运的是，`affine`、`arith` 和 `std` 方言已经提供了将它们转换为 LLVM 方言所需的模式集。这些模式依赖[传递性 lowering](../../../getting_started/Glossary.md/#transitive-lowering)，允许在多个阶段中 lowering IR。

```c++
  mlir::RewritePatternSet patterns(&getContext());
  mlir::populateAffineToStdConversionPatterns(patterns, &getContext());
  mlir::cf::populateSCFToControlFlowConversionPatterns(patterns, &getContext());
  mlir::arith::populateArithToLLVMConversionPatterns(typeConverter,
                                                          patterns);
  mlir::populateFuncToLLVMConversionPatterns(typeConverter, patterns);
  mlir::cf::populateControlFlowToLLVMConversionPatterns(patterns, &getContext());

  // The only remaining operation, to lower from the `toy` dialect, is the
  // PrintOp.
  patterns.add<PrintOpLowering>(&getContext());
```

### 完整 Lowering

我们希望完全 lowering 到 LLVM，因此我们使用 `FullConversion`。这确保了转换后只留下合法的操作。

```c++
  mlir::ModuleOp module = getOperation();
  if (mlir::failed(mlir::applyFullConversion(module, target, patterns)))
    signalPassFailure();
```

回顾我们当前的工作示例：

```mlir
toy.func @main() {
  %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
  %2 = toy.transpose(%0 : tensor<2x3xf64>) to tensor<3x2xf64>
  %3 = toy.mul %2, %2 : tensor<3x2xf64>
  toy.print %3 : tensor<3x2xf64>
  toy.return
}
```

我们现在可以 lowering 到 LLVM 方言，产生以下代码：

```mlir
llvm.func @free(!llvm<"i8*">)
llvm.func @printf(!llvm<"i8*">, ...) -> i32
llvm.func @malloc(i64) -> !llvm<"i8*">
llvm.func @main() {
  %0 = llvm.mlir.constant(1.000000e+00 : f64) : f64
  %1 = llvm.mlir.constant(2.000000e+00 : f64) : f64

  ...

^bb16:
  %221 = llvm.extractvalue %25[0] : !llvm<"{ double*, i64, [2 x i64], [2 x i64] }">
  %222 = llvm.mlir.constant(0 : index) : i64
  %223 = llvm.mlir.constant(2 : index) : i64
  %224 = llvm.mul %214, %223 : i64
  %225 = llvm.add %222, %224 : i64
  %226 = llvm.mlir.constant(1 : index) : i64
  %227 = llvm.mul %219, %226 : i64
  %228 = llvm.add %225, %227 : i64
  %229 = llvm.getelementptr %221[%228] : (!llvm."double*">, i64) -> !llvm<"f64*">
  %230 = llvm.load %229 : !llvm<"double*">
  %231 = llvm.call @printf(%207, %230) : (!llvm<"i8*">, f64) -> i32
  %232 = llvm.add %219, %218 : i64
  llvm.br ^bb15(%232 : i64)

  ...

^bb18:
  %235 = llvm.extractvalue %65[0] : !llvm<"{ double*, i64, [2 x i64], [2 x i64] }">
  %236 = llvm.bitcast %235 : !llvm<"double*"> to !llvm<"i8*">
  llvm.call @free(%236) : (!llvm<"i8*">) -> ()
  %237 = llvm.extractvalue %45[0] : !llvm<"{ double*, i64, [2 x i64], [2 x i64] }">
  %238 = llvm.bitcast %237 : !llvm<"double*"> to !llvm<"i8*">
  llvm.call @free(%238) : (!llvm<"i8*">) -> ()
  %239 = llvm.extractvalue %25[0] : !llvm<"{ double*, i64, [2 x i64], [2 x i64] }">
  %240 = llvm.bitcast %239 : !llvm<"double*"> to !llvm<"i8*">
  llvm.call @free(%240) : (!llvm<"i8*">) -> ()
  llvm.return
}
```

关于 lowering 到 LLVM 方言的更深入细节，请参见 [LLVM IR Target](../../TargetLLVMIR.md)。

## 代码生成：从 MLIR 中出来

至此，我们已经非常接近代码生成阶段了。我们可以生成 LLVM 方言中的代码，所以现在只需导出到 LLVM IR 并设置一个 JIT 来运行它。

### 生成 LLVM IR

现在我们的模块仅由 LLVM 方言中的操作组成，我们可以导出到 LLVM IR。要以编程方式完成此操作，我们可以调用以下实用工具：

```c++
  std::unique_ptr<llvm::Module> llvmModule = mlir::translateModuleToLLVMIR(module);
  if (!llvmModule)
    /* ... an error was encountered ... */
```

将我们的模块导出到 LLVM IR 会生成：

```llvm
define void @main() {
  ...

102:
  %103 = extractvalue { double*, i64, [2 x i64], [2 x i64] } %8, 0
  %104 = mul i64 %96, 2
  %105 = add i64 0, %104
  %106 = mul i64 %100, 1
  %107 = add i64 %105, %106
  %108 = getelementptr double, double* %103, i64 %107
  %109 = memref.load double, double* %108
  %110 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([4 x i8], [4 x i8]* @frmt_spec, i64 0, i64 0), double %109)
  %111 = add i64 %100, 1
  cf.br label %99

  ...

115:
  %116 = extractvalue { double*, i64, [2 x i64], [2 x i64] } %24, 0
  %117 = bitcast double* %116 to i8*
  call void @free(i8* %117)
  %118 = extractvalue { double*, i64, [2 x i64], [2 x i64] } %16, 0
  %119 = bitcast double* %118 to i8*
  call void @free(i8* %119)
  %120 = extractvalue { double*, i64, [2 x i64], [2 x i64] } %8, 0
  %121 = bitcast double* %120 to i8*
  call void @free(i8* %121)
  ret void
}
```

如果我们对生成的 LLVM IR 启用优化，我们可以将其大幅精简：

```llvm
define void @main()
  %0 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([4 x i8], [4 x i8]* @frmt_spec, i64 0, i64 0), double 1.000000e+00)
  %1 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([4 x i8], [4 x i8]* @frmt_spec, i64 0, i64 0), double 1.600000e+01)
  %putchar = tail call i32 @putchar(i32 10)
  %2 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([4 x i8], [4 x i8]* @frmt_spec, i64 0, i64 0), double 4.000000e+00)
  %3 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([4 x i8], [4 x i8]* @frmt_spec, i64 0, i64 0), double 2.500000e+01)
  %putchar.1 = tail call i32 @putchar(i32 10)
  %4 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([4 x i8], [4 x i8]* @frmt_spec, i64 0, i64 0), double 9.000000e+00)
  %5 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([4 x i8], [4 x i8]* @frmt_spec, i64 0, i64 0), double 3.600000e+01)
  %putchar.2 = tail call i32 @putchar(i32 10)
  ret void
}
```

完整的 LLVM IR 导出代码可以在 `examples/toy/Ch6/toyc.cpp` 的 `dumpLLVMIR()` 函数中找到：

```c++

int dumpLLVMIR(mlir::ModuleOp module) {
  // Translate the module, that contains the LLVM dialect, to LLVM IR. Use a
  // fresh LLVM IR context. (Note that LLVM is not thread-safe and any
  // concurrent use of a context requires external locking.)
  llvm::LLVMContext llvmContext;
  auto llvmModule = mlir::translateModuleToLLVMIR(module, llvmContext);
  if (!llvmModule) {
    llvm::errs() << "Failed to emit LLVM IR\n";
    return -1;
  }

  // Initialize LLVM targets.
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  mlir::ExecutionEngine::setupTargetTriple(llvmModule.get());

  /// Optionally run an optimization pipeline over the llvm module.
  auto optPipeline = mlir::makeOptimizingTransformer(
      /*optLevel=*/EnableOpt ? 3 : 0, /*sizeLevel=*/0,
      /*targetMachine=*/nullptr);
  if (auto err = optPipeline(llvmModule.get())) {
    llvm::errs() << "Failed to optimize LLVM IR " << err << "\n";
    return -1;
  }
  llvm::errs() << *llvmModule << "\n";
  return 0;
}
```

### 设置 JIT

设置 JIT 以运行包含 LLVM 方言的模块可以使用 `mlir::ExecutionEngine` 基础设施完成。这是一个围绕 LLVM JIT 的实用包装器，它接受 `.mlir` 作为输入。设置 JIT 的完整代码可以在 `Ch6/toyc.cpp` 的 `runJit()` 函数中找到：

```c++
int runJit(mlir::ModuleOp module) {
  // Initialize LLVM targets.
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();

  // An optimization pipeline to use within the execution engine.
  auto optPipeline = mlir::makeOptimizingTransformer(
      /*optLevel=*/EnableOpt ? 3 : 0, /*sizeLevel=*/0,
      /*targetMachine=*/nullptr);

  // Create an MLIR execution engine. The execution engine eagerly JIT-compiles
  // the module.
  auto maybeEngine = mlir::ExecutionEngine::create(module,
      /*llvmModuleBuilder=*/nullptr, optPipeline);
  assert(maybeEngine && "failed to construct an execution engine");
  auto &engine = maybeEngine.get();

  // Invoke the JIT-compiled function.
  auto invocationResult = engine->invoke("main");
  if (invocationResult) {
    llvm::errs() << "JIT invocation failed\n";
    return -1;
  }

  return 0;
}
```

你可以在构建目录中试验它：

```shell
$ echo 'def main() { print([[1, 2], [3, 4]]); }' | ./bin/toyc-ch6 -emit=jit
1.000000 2.000000
3.000000 4.000000
```

你还可以使用 `-emit=mlir`、`-emit=mlir-affine`、`-emit=mlir-llvm` 和 `-emit=llvm` 来比较涉及的各个 IR 层级。也请尝试像 [`--mlir-print-ir-after-all`](../../PassManagement.md/#ir-printing) 这样的选项来追踪 IR 在流水线中的演变过程。

本章节使用的示例代码可以在 test/Examples/Toy/Ch6/llvm-lowering.mlir 中找到。

到目前为止，我们一直在使用原始数据类型。在[下一章](Ch_07_为Toy添加复合类型.md)中，我们将添加一个复合 `struct` 类型。
