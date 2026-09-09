#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"

cd "$SOURCE_DIR"

output="$("$ARCOFISSION" compile-run fission/tests/core_smoke.abas)"

grep -q "^resolved$" <<<"$output"
grep -q "^language.brainfuck -> sir.to.amir -> target.linux-x86_64$" <<<"$output"
grep -q "^Executable.Linux.X86_64$" <<<"$output"
grep -q "^elf:amir:sir:+\\.$" <<<"$output"
test "$(grep -c "^TRUE$" <<<"$output")" = "3"
grep -q "^FALSE$" <<<"$output"

kit_output="$("$ARCOFISSION" compile-run fission/tests/language_authoring_kit_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$kit_output")" = "2"
grep -q "^TRUE$" <<<"$kit_output"
grep -q "^language.brainfuck-kit -> sir.to.amir.kit -> target.linux-x86_64.kit$" <<<"$kit_output"
grep -q "^Executable.Linux.X86_64$" <<<"$kit_output"
grep -q "^elf:amir:sir:+$" <<<"$kit_output"

module_output="$("$ARCOFISSION" compile-run fission/tests/module_smoke.abas)"

grep -q "^build-rivet/fission/modules/fission-arcobasic-frontend$" <<<"$module_output"
test "$(grep -c "^TRUE$" <<<"$module_output")" = "3"
grep -q "^fission-module-stdio-v0.1$" <<<"$module_output"

arcobasic_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_language_smoke.abas)"

grep -q "^TRUE$" <<<"$arcobasic_output"
grep -q "^language.arcobasic -> pass.sir.to.amir.arcobasic-smoke -> target.linux-x86_64.arcobasic-smoke$" <<<"$arcobasic_output"
grep -q "^Executable.Linux.X86_64$" <<<"$arcobasic_output"
grep -q "^elf:amir:SIR hello.abas v0.1$" <<<"$arcobasic_output"
grep -q "^ArcoBASIC AST hello.abas$" <<<"$arcobasic_output"
grep -q "^  Assign(Name=value)$" <<<"$arcobasic_output"
grep -q "^SIR hello.abas v0.1$" <<<"$arcobasic_output"
grep -q "^  Assign(Name=value)$" <<<"$arcobasic_output"
grep -q "^  Print$" <<<"$arcobasic_output"
grep -q "^    Read(Name=value)$" <<<"$arcobasic_output"
grep -q "^11$" <<<"$arcobasic_output"
test "$(grep -c "^TRUE$" <<<"$arcobasic_output")" = "3"
grep -q "^13$" <<<"$arcobasic_output"
grep -q "^9$" <<<"$arcobasic_output"
grep -q "^7$" <<<"$arcobasic_output"

sir_output="$("$ARCOFISSION" compile-run fission/tests/sir_builder_smoke.abas)"

grep -q "^FALSE$" <<<"$sir_output"
grep -q "^SIR sir-smoke v0.1$" <<<"$sir_output"
grep -q "^  Print$" <<<"$sir_output"
grep -q "^    Unary(Operator=-)$" <<<"$sir_output"
grep -q "^      Binary(Operator=+)$" <<<"$sir_output"
grep -q "^        Read(Name=left)$" <<<"$sir_output"
grep -q "^        Literal(Value=1)$" <<<"$sir_output"

lexer_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_lexer_smoke.abas)"

grep -q "^14$" <<<"$lexer_output"
grep -q "^Keyword:PRINT@1:1$" <<<"$lexer_output"
grep -q "^Identifier:hello@1:7$" <<<"$lexer_output"
grep -q "^Identifier:value@2:1$" <<<"$lexer_output"
grep -q "^Symbol:=@2:7$" <<<"$lexer_output"
grep -q "^Keyword:IF@4:1$" <<<"$lexer_output"
grep -q "^Symbol:>=@4:10$" <<<"$lexer_output"
grep -q "^Number:1@4:13$" <<<"$lexer_output"
grep -q "^EOF:@4:19$" <<<"$lexer_output"

preprocess_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_preprocess_smoke.abas)"

grep -q "^FALSE$" <<<"$preprocess_output"
grep -q "^PRINT \"debug\"$" <<<"$preprocess_output"
grep -q "^PRINT 2$" <<<"$preprocess_output"
grep -q "^PRINT 1$" <<<"$preprocess_output"
grep -q "^DEBUG$" <<<"$preprocess_output"

preprocess_compile_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_preprocess_compile_smoke.abas)"

grep -q "^TRUE$" <<<"$preprocess_compile_output"
grep -q "^PRINT \"debug\"$" <<<"$preprocess_compile_output"
grep -q "^SIR preprocess-compile.abas v0.1$" <<<"$preprocess_compile_output"
grep -q "^    Literal(Value=debug, LiteralKind=String)$" <<<"$preprocess_compile_output"

preprocess_error_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_preprocess_error_smoke.abas)"

grep -q "^FALSE$" <<<"$preprocess_error_output"
grep -q "^TRUE$" <<<"$preprocess_error_output"
grep -q "FISSION_ARCOBASIC_ACTIVE_ERROR_DIRECTIVE" <<<"$preprocess_error_output"

include_metadata_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_include_metadata_smoke.abas)"

grep -q "^TRUE$" <<<"$include_metadata_output"
grep -q "^FALSE$" <<<"$include_metadata_output"
grep -q "^1.2.3$" <<<"$include_metadata_output"
grep -q "^Arcology$" <<<"$include_metadata_output"
grep -q "^careful$" <<<"$include_metadata_output"
grep -q "^PRINT included$" <<<"$include_metadata_output"
grep -q "^PRINT after$" <<<"$include_metadata_output"
grep -q "^SIR include-metadata-smoke.abas v0.1$" <<<"$include_metadata_output"

parser_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_parser_smoke.abas)"

grep -q "^FALSE$" <<<"$parser_output"
grep -q "^ArcoBASIC AST parser-smoke.abas$" <<<"$parser_output"
grep -q "^  Assign(Name=value)$" <<<"$parser_output"
grep -q "^    Binary(Operator=+)$" <<<"$parser_output"
grep -q "^      Binary(Operator=\\*)$" <<<"$parser_output"
grep -q "^  If$" <<<"$parser_output"
grep -q "^  While$" <<<"$parser_output"

expression_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_expression_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$expression_output")" = "2"
grep -q "^ArcoBASIC AST expression-smoke.abas$" <<<"$expression_output"
grep -q "^    Unary(Operator=NOT)$" <<<"$expression_output"
grep -q "^    Literal(Value=NULL)$" <<<"$expression_output"
grep -q "^    Binary(Operator=\\*)$" <<<"$expression_output"
grep -q "^      Unary(Operator=-)$" <<<"$expression_output"
grep -q "^SIR expression-smoke.abas v0.1$" <<<"$expression_output"
grep -q "^    Literal(Value=TRUE)$" <<<"$expression_output"

function_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_function_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$function_output")" = "2"
grep -q "^ArcoBASIC AST function-smoke.abas$" <<<"$function_output"
grep -q "^  Function(Name=add, Arguments=left,right)$" <<<"$function_output"
grep -q "^      Return$" <<<"$function_output"
grep -q "^  Assign(Name=value)$" <<<"$function_output"
grep -q "^    CallExpr$" <<<"$function_output"
grep -q "^      Read(Name=add)$" <<<"$function_output"
grep -q "^SIR function-smoke.abas v0.1$" <<<"$function_output"

postfix_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_postfix_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$postfix_output")" = "2"
grep -q "^ArcoBASIC AST postfix-smoke.abas$" <<<"$postfix_output"
grep -q "^    MemberRead(Name=Name)$" <<<"$postfix_output"
grep -q "^      IndexRead$" <<<"$postfix_output"
grep -q "^        MemberRead(Name=Items)$" <<<"$postfix_output"
grep -q "^SIR postfix-smoke.abas v0.1$" <<<"$postfix_output"

literal_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_literal_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$literal_output")" = "2"
grep -q "^ArcoBASIC AST literal-smoke.abas$" <<<"$literal_output"
grep -q "^    Array$" <<<"$literal_output"
grep -q "^    Object$" <<<"$literal_output"
grep -q "^      Field(Name=Name)$" <<<"$literal_output"
grep -q "^SIR literal-smoke.abas v0.1$" <<<"$literal_output"
grep -q "^    MemberRead(Name=Name)$" <<<"$literal_output"

let_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_let_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$let_output")" = "2"
grep -q "^ArcoBASIC AST let-smoke.abas$" <<<"$let_output"
grep -q "^      Declare(Name=mapSize, Type=U64)$" <<<"$let_output"
grep -q "^      Declare(Name=mapSizeAddress, Type=PTR)$" <<<"$let_output"
grep -q "^      Declare(Name=status, Type=)$" <<<"$let_output"
grep -q "^SIR let-smoke.abas v0.1$" <<<"$let_output"

legacy_operator_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_legacy_operator_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$legacy_operator_output")" = "2"
grep -q "^ArcoBASIC AST legacy-operator-smoke.abas$" <<<"$legacy_operator_output"
grep -q "^    Binary(Operator==)$" <<<"$legacy_operator_output"
grep -q "^    Binary(Operator=\\\\)$" <<<"$legacy_operator_output"
grep -q "^  Set$" <<<"$legacy_operator_output"
grep -q "^SIR legacy-operator-smoke.abas v0.1$" <<<"$legacy_operator_output"

string_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_string_smoke.abas)"

grep -q "^String:hello@1:7$" <<<"$string_output"
grep -q "^String$" <<<"$string_output"
grep -q "^TRUE$" <<<"$string_output"
grep -q "^InterpolatedString:name@3:7$" <<<"$string_output"
grep -q "^FALSE$" <<<"$string_output"
grep -q "^SIR string-smoke.abas v0.1$" <<<"$string_output"
grep -q "^    Literal(Value=hello, LiteralKind=String)$" <<<"$string_output"
grep -q "^    Literal(Value=name, LiteralKind=InterpolatedString)$" <<<"$string_output"

directive_decl_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_directive_decl_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$directive_decl_output")" = "2"
grep -q "^ArcoBASIC AST directive-decl-smoke.abas$" <<<"$directive_decl_output"
grep -q "^  Import(Path=text, Alias=Txt)$" <<<"$directive_decl_output"
grep -q "^  Directive(Name=DESCRIPTION, Value=Demo)$" <<<"$directive_decl_output"
grep -q "^  Const(Name=Limit)$" <<<"$directive_decl_output"
grep -q "^  Declare(Name=count, Type=U64)$" <<<"$directive_decl_output"
grep -q "^    CallExpr$" <<<"$directive_decl_output"
grep -q "^SIR directive-decl-smoke.abas v0.1$" <<<"$directive_decl_output"
grep -q "^  Import(Path=text, Alias=Txt)$" <<<"$directive_decl_output"

decimal_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_decimal_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$decimal_output")" = "2"
grep -q "^Number:0.07@1:19$" <<<"$decimal_output"
grep -q "^Number:0.08@1:25$" <<<"$decimal_output"
grep -q "^ArcoBASIC AST decimal-smoke.abas$" <<<"$decimal_output"
grep -q "^      Literal(Value=0.07)$" <<<"$decimal_output"
grep -q "^      Literal(Value=1.0)$" <<<"$decimal_output"
grep -q "^SIR decimal-smoke.abas v0.1$" <<<"$decimal_output"

class_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_class_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$class_output")" = "2"
grep -q "^ArcoBASIC AST class-smoke.abas$" <<<"$class_output"
grep -q "^  Class(Name=Counter)$" <<<"$class_output"
grep -q "^    Constructor(Arguments=start:U64)$" <<<"$class_output"
grep -q "^    Function(Name=Next, Arguments=)$" <<<"$class_output"
grep -q "^          Set$" <<<"$class_output"
grep -q "^            MemberRead(Name=value)$" <<<"$class_output"
grep -q "^SIR class-smoke.abas v0.1$" <<<"$class_output"
grep -q "^  Class(Name=Counter)$" <<<"$class_output"

access_interface_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_access_interface_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$access_interface_output")" = "2"
grep -q "^ArcoBASIC AST access-interface-smoke.abas$" <<<"$access_interface_output"
grep -q "^  Interface(Name=Named)$" <<<"$access_interface_output"
grep -q "^  Modifier(Name=PUBLIC)$" <<<"$access_interface_output"
grep -q "^    Modifier(Name=ABSTRACT)$" <<<"$access_interface_output"
grep -q "^      Class(Name=Widget, Extends=Base, Implements=Named)$" <<<"$access_interface_output"
grep -q "^        Modifier(Name=PRIVATE)$" <<<"$access_interface_output"
grep -q "^SIR access-interface-smoke.abas v0.1$" <<<"$access_interface_output"

call_statement_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_call_statement_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$call_statement_output")" = "2"
grep -q "^ArcoBASIC AST call-statement-smoke.abas$" <<<"$call_statement_output"
grep -q "^      ExprStmt$" <<<"$call_statement_output"
grep -q "^  ExprStmt$" <<<"$call_statement_output"
grep -q "^  Set$" <<<"$call_statement_output"
grep -q "^SIR call-statement-smoke.abas v0.1$" <<<"$call_statement_output"

control_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_control_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$control_output")" = "2"
grep -q "^ArcoBASIC AST control-smoke.abas$" <<<"$control_output"
grep -q "^  ForRange(Name=i)$" <<<"$control_output"
grep -q "^  ForEach(Name=item)$" <<<"$control_output"
grep -q "^      LoopControl(Action=CONTINUE, Target=FOR)$" <<<"$control_output"
grep -q "^  Try(Catch=err)$" <<<"$control_output"
grep -q "^      Throw$" <<<"$control_output"
grep -q "^SIR control-smoke.abas v0.1$" <<<"$control_output"

callable_comprehension_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_callable_comprehension_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$callable_comprehension_output")" = "2"
grep -q "^ArcoBASIC AST callable-comprehension-smoke.abas$" <<<"$callable_comprehension_output"
grep -q "^    AddressOf(Name=Demo.Run)$" <<<"$callable_comprehension_output"
grep -q "^    Copy$" <<<"$callable_comprehension_output"
grep -q "^    ArrayComprehension(Name=i)$" <<<"$callable_comprehension_output"
grep -q "^SIR callable-comprehension-smoke.abas v0.1$" <<<"$callable_comprehension_output"

do_bitwise_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_do_bitwise_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$do_bitwise_output")" = "2"
grep -q "^ArcoBASIC AST do-bitwise-smoke.abas$" <<<"$do_bitwise_output"
grep -q "^  Do(PreMode=WHILE, PostMode=)$" <<<"$do_bitwise_output"
grep -q "^  Do(PreMode=, PostMode=UNTIL)$" <<<"$do_bitwise_output"
grep -q "^        Binary(Operator=SHR)$" <<<"$do_bitwise_output"
grep -q "^    Binary(Operator=CONTAINS)$" <<<"$do_bitwise_output"
grep -q "^SIR do-bitwise-smoke.abas v0.1$" <<<"$do_bitwise_output"

type_compound_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_type_compound_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$type_compound_output")" = "2"
grep -q "^ArcoBASIC AST type-compound-smoke.abas$" <<<"$type_compound_output"
grep -q "^  Function(Name=Next, Arguments=value:U64, Returns=U64)$" <<<"$type_compound_output"
grep -q "^      Assign(Name=value)$" <<<"$type_compound_output"
grep -q "^        Binary(Operator=+)$" <<<"$type_compound_output"
grep -q "^  Set$" <<<"$type_compound_output"
grep -q "^    MemberRead(Name=total)$" <<<"$type_compound_output"
grep -q "^SIR type-compound-smoke.abas v0.1$" <<<"$type_compound_output"

logic_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_logic_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$logic_output")" = "2"
grep -q "^ArcoBASIC AST logic-smoke.abas$" <<<"$logic_output"
grep -q "^    Binary(Operator=ANDALSO)$" <<<"$logic_output"
grep -q "^      Binary(Operator=OR)$" <<<"$logic_output"
grep -q "^        Binary(Operator=%)$" <<<"$logic_output"
grep -q "^    Binary(Operator=MOD)$" <<<"$logic_output"
grep -q "^SIR logic-smoke.abas v0.1$" <<<"$logic_output"

named_defaults_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_named_defaults_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$named_defaults_output")" = "2"
grep -q "^ArcoBASIC AST named-defaults-smoke.abas$" <<<"$named_defaults_output"
grep -q "^  Function(Name=clamp, Arguments=value:U64,minimum:U64=0,maximum:U64=255, Returns=U64)$" <<<"$named_defaults_output"
grep -q "^      NamedArg(Name=value)$" <<<"$named_defaults_output"
grep -q "^      NamedArg(Name=maximum)$" <<<"$named_defaults_output"
grep -q "^SIR named-defaults-smoke.abas v0.1$" <<<"$named_defaults_output"

single_line_if_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_single_line_if_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$single_line_if_output")" = "2"
grep -q "^ArcoBASIC AST single-line-if-smoke.abas$" <<<"$single_line_if_output"
grep -q "^  If$" <<<"$single_line_if_output"
grep -q "^      If$" <<<"$single_line_if_output"
grep -q "^SIR single-line-if-smoke.abas v0.1$" <<<"$single_line_if_output"
grep -q "^      Branch$" <<<"$single_line_if_output"

semantic_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_semantic_smoke.abas)"

grep -q "^FALSE$" <<<"$semantic_output"
grep -q "^ArcoBASIC Semantics semantic-smoke.abas$" <<<"$semantic_output"
grep -q "^  Import module::Txt$" <<<"$semantic_output"
grep -q "^  Variable module::count AS U64$" <<<"$semantic_output"
grep -q "^  Function module::Add AS U64$" <<<"$semantic_output"
grep -q "^  Parameter module::Add::right AS U64 = 1$" <<<"$semantic_output"
grep -q "^  Class module::Box$" <<<"$semantic_output"

semantic_diagnostics_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_semantic_diagnostics_smoke.abas)"

grep -q "^FALSE$" <<<"$semantic_diagnostics_output"
grep -q "^TRUE$" <<<"$semantic_diagnostics_output"
grep -q "FISSION_ARCOBASIC_DUPLICATE_SYMBOL" <<<"$semantic_diagnostics_output"

semantic_reference_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_semantic_reference_smoke.abas)"

grep -q "^TRUE$" <<<"$semantic_reference_output"
grep -q "^FALSE$" <<<"$semantic_reference_output"
grep -q "^4$" <<<"$semantic_reference_output"
grep -q "^5$" <<<"$semantic_reference_output"
grep -q "^    Call module::Paint::Helper -> Resolved module::Helper$" <<<"$semantic_reference_output"
grep -q "^    Read module::Paint::GUI -> External$" <<<"$semantic_reference_output"
grep -q "^    Read module::Paint::missing -> Unresolved$" <<<"$semantic_reference_output"

strict_semantic_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_strict_semantic_smoke.abas)"

grep -q "^FALSE$" <<<"$strict_semantic_output"
grep -q "^TRUE$" <<<"$strict_semantic_output"
grep -q "FISSION_ARCOBASIC_UNRESOLVED_SYMBOL" <<<"$strict_semantic_output"
grep -q "^    Read module::Paint::GUI -> External$" <<<"$strict_semantic_output"
grep -q "^    Read module::Paint::missing -> Unresolved$" <<<"$strict_semantic_output"

shared_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_shared_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$shared_output")" = "2"
grep -q "^ArcoBASIC AST shared-smoke.abas$" <<<"$shared_output"
grep -q "^      Modifier(Name=SHARED)$" <<<"$shared_output"
grep -q "^        Function(Name=Issue, Arguments=prefix:String, Returns=String)$" <<<"$shared_output"
grep -q "^SIR shared-smoke.abas v0.1$" <<<"$shared_output"

diagnostics_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_diagnostics_smoke.abas)"

grep -q "^FALSE$" <<<"$diagnostics_output"
grep -q "^SIR$" <<<"$diagnostics_output"
grep -q "^TRUE$" <<<"$diagnostics_output"
grep -q "FISSION_ARCOBASIC_UNSUPPORTED_STATEMENT" <<<"$diagnostics_output"

reveal_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_reveal_smoke.abas)"

grep -q "^ArcoBASIC AST reveal.abas$" <<<"$reveal_output"
grep -q "^SIR reveal.abas v0.1$" <<<"$reveal_output"
grep -q "^PIPELINE RESOLVED$" <<<"$reveal_output"
grep -q "^language.arcobasic: Source.arcobasic -> SIR$" <<<"$reveal_output"

real_timer_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_real_timer_parse_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$real_timer_output")" = "2"
grep -q "^14$" <<<"$real_timer_output"

real_project_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_real_project_parse_smoke.abas)"

grep -q "^6$" <<<"$real_project_output"
grep -q "^0$" <<<"$real_project_output"

echo "fission_substrate_core_smoke: all checks passed"
