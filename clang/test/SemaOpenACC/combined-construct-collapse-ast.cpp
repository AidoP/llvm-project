
// RUN: %clang_cc1 %s -fopenacc -ast-dump | FileCheck %s

// Test this with PCH.
// RUN: %clang_cc1 %s -fopenacc -emit-pch -o %t %s
// RUN: %clang_cc1 %s -fopenacc -include-pch %t -ast-dump-all | FileCheck %s
#ifndef PCH_HELPER
#define PCH_HELPER

struct S {
  constexpr S(){};
  constexpr operator auto() {return 1;}
};

void NormalUses() {
  // CHECK: FunctionDecl{{.*}}NormalUses
  // CHECK-NEXT: CompoundStmt

#pragma acc parallel loop collapse(1)
  for(int i = 0; i < 5; ++i);
  // CHECK-NEXT: OpenACCCombinedConstruct{{.*}} parallel loop
  // CHECK-NEXT: collapse clause
  // CHECK-NEXT: ConstantExpr{{.*}}'int'
  // CHECK-NEXT: value: Int 1
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 1
  // CHECK-NEXT: ForStmt
  // CHECK-NEXT: DeclStmt
  // CHECK-NEXT: VarDecl{{.*}} i 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 0
  // CHECK-NEXT: <<<NULL>>>
  // CHECK-NEXT: BinaryOperator{{.*}}'<'
  // CHECK-NEXT: ImplicitCastExpr
  // CHECK-NEXT: DeclRefExpr{{.*}}'i' 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 5
  // CHECK-NEXT: UnaryOperator{{.*}}++
  // CHECK-NEXT: DeclRefExpr{{.*}}'i' 'int'
  // CHECK-NEXT: NullStmt

#pragma acc serial loop collapse(force:S{})
  for(int i = 0; i < 5; ++i);
  // CHECK-NEXT: OpenACCCombinedConstruct{{.*}} serial loop
  // CHECK-NEXT: collapse clause
  // CHECK-NEXT: ConstantExpr{{.*}}'int'
  // CHECK-NEXT: value: Int 1
  // CHECK-NEXT: ImplicitCastExpr{{.*}} 'int' <UserDefinedConversion>
  // CHECK-NEXT: CXXMemberCallExpr{{.*}} 'int'
  // CHECK-NEXT: MemberExpr{{.*}} .operator auto
  // CHECK-NEXT: MaterializeTemporaryExpr{{.*}}'S' lvalue
  // CHECK-NEXT: CXXTemporaryObjectExpr{{.*}}'S' 'void ()' list
  // CHECK-NEXT: ForStmt
  // CHECK-NEXT: DeclStmt
  // CHECK-NEXT: VarDecl{{.*}} i 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 0
  // CHECK-NEXT: <<<NULL>>>
  // CHECK-NEXT: BinaryOperator{{.*}}'<'
  // CHECK-NEXT: ImplicitCastExpr
  // CHECK-NEXT: DeclRefExpr{{.*}}'i' 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 5
  // CHECK-NEXT: UnaryOperator{{.*}}++
  // CHECK-NEXT: DeclRefExpr{{.*}}'i' 'int'
  // CHECK-NEXT: NullStmt
}

template<typename T, unsigned Value>
void TemplUses() {
  // CHECK: FunctionTemplateDecl{{.*}}TemplUses
  // CHECK-NEXT: TemplateTypeParmDecl {{.*}} referenced typename depth 0 index 0 T
  // CHECK-NEXT: NonTypeTemplateParmDecl{{.*}} referenced 'unsigned int' depth 0 index 1 Value
  // CHECK-NEXT: FunctionDecl{{.*}} TemplUses 'void ()'
  // CHECK-NEXT: CompoundStmt

#pragma acc parallel loop collapse(Value)
  for(int i = 0; i < 5; ++i)
    for(int j = 0; j < 5; ++j);
  // CHECK-NEXT: OpenACCCombinedConstruct{{.*}} parallel loop
  // CHECK-NEXT: collapse clause
  // CHECK-NEXT: DeclRefExpr{{.*}} 'unsigned int' NonTypeTemplateParm{{.*}} 'Value'
  // CHECK-NEXT: ForStmt
  // CHECK-NEXT: DeclStmt
  // CHECK-NEXT: VarDecl{{.*}} i 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 0
  // CHECK-NEXT: <<<NULL>>>
  // CHECK-NEXT: BinaryOperator{{.*}}'<'
  // CHECK-NEXT: ImplicitCastExpr
  // CHECK-NEXT: DeclRefExpr{{.*}}'i' 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 5
  // CHECK-NEXT: UnaryOperator{{.*}}++
  // CHECK-NEXT: DeclRefExpr{{.*}}'i' 'int'
  // CHECK-NEXT: ForStmt
  // CHECK-NEXT: DeclStmt
  // CHECK-NEXT: VarDecl{{.*}} j 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 0
  // CHECK-NEXT: <<<NULL>>>
  // CHECK-NEXT: BinaryOperator{{.*}}'<'
  // CHECK-NEXT: ImplicitCastExpr
  // CHECK-NEXT: DeclRefExpr{{.*}}'j' 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 5
  // CHECK-NEXT: UnaryOperator{{.*}}++
  // CHECK-NEXT: DeclRefExpr{{.*}}'j' 'int'
  // CHECK-NEXT: NullStmt

#pragma acc kernels loop collapse(force:T{} + S{})
  for(int i = 0; i < 5; ++i)
    for(int j = 0; j < 5; ++j);
  // CHECK-NEXT: OpenACCCombinedConstruct{{.*}} kernels loop
  // CHECK-NEXT: collapse clause
  // CHECK-NEXT: BinaryOperator {{.*}}'+'
  // CHECK-NEXT: CXXUnresolvedConstructExpr{{.*}}'T' 'T' list
  // CHECK-NEXT: InitListExpr
  // CHECK-NEXT: CXXTemporaryObjectExpr{{.*}}'S' 'void ()' list
  // CHECK-NEXT: ForStmt
  // CHECK-NEXT: DeclStmt
  // CHECK-NEXT: VarDecl{{.*}} i 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 0
  // CHECK-NEXT: <<<NULL>>>
  // CHECK-NEXT: BinaryOperator{{.*}}'<'
  // CHECK-NEXT: ImplicitCastExpr
  // CHECK-NEXT: DeclRefExpr{{.*}}'i' 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 5
  // CHECK-NEXT: UnaryOperator{{.*}}++
  // CHECK-NEXT: DeclRefExpr{{.*}}'i' 'int'
  // CHECK-NEXT: ForStmt
  // CHECK-NEXT: DeclStmt
  // CHECK-NEXT: VarDecl{{.*}} j 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 0
  // CHECK-NEXT: <<<NULL>>>
  // CHECK-NEXT: BinaryOperator{{.*}}'<'
  // CHECK-NEXT: ImplicitCastExpr
  // CHECK-NEXT: DeclRefExpr{{.*}}'j' 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 5
  // CHECK-NEXT: UnaryOperator{{.*}}++
  // CHECK-NEXT: DeclRefExpr{{.*}}'j' 'int'
  // CHECK-NEXT: NullStmt

  // Instantiation:
  // CHECK-NEXT: FunctionDecl{{.*}} used TemplUses 'void ()' implicit_instantiation
  // CHECK-NEXT: TemplateArgument type 'S'
  // CHECK-NEXT: RecordType{{.*}} 'S'
  // CHECK-NEXT: CXXRecord{{.*}} 'S'
  // CHECK-NEXT: TemplateArgument integral '2U'
  // CHECK-NEXT: CompoundStmt

  // CHECK-NEXT: OpenACCCombinedConstruct{{.*}} parallel loop
  // CHECK-NEXT: collapse clause
  // CHECK-NEXT: ConstantExpr{{.*}}'unsigned int'
  // CHECK-NEXT: value: Int 2
  // CHECK-NEXT: SubstNonTypeTemplateParmExpr{{.*}} 'unsigned int'
  // CHECK-NEXT: NonTypeTemplateParmDecl
  // CHECK-NEXT: IntegerLiteral {{.*}} 'unsigned int' 2
  // CHECK-NEXT: ForStmt
  // CHECK-NEXT: DeclStmt
  // CHECK-NEXT: VarDecl{{.*}} i 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 0
  // CHECK-NEXT: <<<NULL>>>
  // CHECK-NEXT: BinaryOperator{{.*}}'<'
  // CHECK-NEXT: ImplicitCastExpr
  // CHECK-NEXT: DeclRefExpr{{.*}}'i' 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 5
  // CHECK-NEXT: UnaryOperator{{.*}}++
  // CHECK-NEXT: DeclRefExpr{{.*}}'i' 'int'
  // CHECK-NEXT: ForStmt
  // CHECK-NEXT: DeclStmt
  // CHECK-NEXT: VarDecl{{.*}} j 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 0
  // CHECK-NEXT: <<<NULL>>>
  // CHECK-NEXT: BinaryOperator{{.*}}'<'
  // CHECK-NEXT: ImplicitCastExpr
  // CHECK-NEXT: DeclRefExpr{{.*}}'j' 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 5
  // CHECK-NEXT: UnaryOperator{{.*}}++
  // CHECK-NEXT: DeclRefExpr{{.*}}'j' 'int'
  // CHECK-NEXT: NullStmt

  // CHECK-NEXT: OpenACCCombinedConstruct{{.*}} kernels loop
  // CHECK-NEXT: collapse clause
  // CHECK-NEXT: ConstantExpr{{.*}}'int'
  // CHECK-NEXT: value: Int 2
  // CHECK-NEXT: BinaryOperator {{.*}}'+'
  // CHECK-NEXT: ImplicitCastExpr{{.*}} 'int' <UserDefinedConversion>
  // CHECK-NEXT: CXXMemberCallExpr{{.*}} 'int'
  // CHECK-NEXT: MemberExpr{{.*}} .operator auto
  // CHECK-NEXT: MaterializeTemporaryExpr{{.*}}'S' lvalue
  // CHECK-NEXT: CXXTemporaryObjectExpr{{.*}}'S' 'void ()' list
  // CHECK-NEXT: ImplicitCastExpr{{.*}} 'int' <UserDefinedConversion>
  // CHECK-NEXT: CXXMemberCallExpr{{.*}} 'int'
  // CHECK-NEXT: MemberExpr{{.*}} .operator auto
  // CHECK-NEXT: MaterializeTemporaryExpr{{.*}}'S' lvalue
  // CHECK-NEXT: CXXTemporaryObjectExpr{{.*}}'S' 'void ()' list
  // CHECK-NEXT: ForStmt
  // CHECK-NEXT: DeclStmt
  // CHECK-NEXT: VarDecl{{.*}} i 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 0
  // CHECK-NEXT: <<<NULL>>>
  // CHECK-NEXT: BinaryOperator{{.*}}'<'
  // CHECK-NEXT: ImplicitCastExpr
  // CHECK-NEXT: DeclRefExpr{{.*}}'i' 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 5
  // CHECK-NEXT: UnaryOperator{{.*}}++
  // CHECK-NEXT: DeclRefExpr{{.*}}'i' 'int'
  // CHECK-NEXT: ForStmt
  // CHECK-NEXT: DeclStmt
  // CHECK-NEXT: VarDecl{{.*}} j 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 0
  // CHECK-NEXT: <<<NULL>>>
  // CHECK-NEXT: BinaryOperator{{.*}}'<'
  // CHECK-NEXT: ImplicitCastExpr
  // CHECK-NEXT: DeclRefExpr{{.*}}'j' 'int'
  // CHECK-NEXT: IntegerLiteral{{.*}} 'int' 5
  // CHECK-NEXT: UnaryOperator{{.*}}++
  // CHECK-NEXT: DeclRefExpr{{.*}}'j' 'int'
  // CHECK-NEXT: NullStmt

}

void Inst() {
  TemplUses<S, 2>();
}

#endif // PCH_HELPER
