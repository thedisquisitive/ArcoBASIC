#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"

cd "$SOURCE_DIR"

# Every fixture below runs as a prebuilt Rivet ArcoCapsule (fission/build/rivet_test_capsules.abas)
# instead of `ArcoFission compile-run`, so this suite no longer recompiles the shared Fission
# frontend from source once per fixture (~50x redundant compiles previously -- the dominant cost
# of a full run). This one `rivet build` is a real no-op (a few seconds) whenever nothing the
# fixtures depend on changed, and rebuilds only the fixtures actually affected by an edit,
# correctly, via ArcoCapsuleTarget's transitive #IMPORT closure fingerprinting.
"$SOURCE_DIR/build-rivet/rivet" build >/tmp/fission_substrate_rivet_build.log

output="$("$SOURCE_DIR/build-rivet/fission/tests/core_smoke")"

grep -q "^resolved$" <<<"$output"
grep -q "^language.brainfuck -> sir.to.amir -> target.linux-x86_64$" <<<"$output"
grep -q "^Executable.Linux.X86_64$" <<<"$output"
grep -q "^elf:amir:sir:+\\.$" <<<"$output"
test "$(grep -c "^TRUE$" <<<"$output")" = "3"
grep -q "^FALSE$" <<<"$output"

kit_output="$("$SOURCE_DIR/build-rivet/fission/tests/language_authoring_kit_smoke")"

test "$(grep -c "^FALSE$" <<<"$kit_output")" = "2"
grep -q "^TRUE$" <<<"$kit_output"
grep -q "^language.brainfuck-kit -> sir.to.amir.kit -> target.linux-x86_64.kit$" <<<"$kit_output"
grep -q "^Executable.Linux.X86_64$" <<<"$kit_output"
grep -q "^elf:amir:sir:+$" <<<"$kit_output"

module_output="$("$SOURCE_DIR/build-rivet/fission/tests/module_smoke")"

grep -q "^build-rivet/fission/modules/fission-arcobasic-frontend$" <<<"$module_output"
test "$(grep -c "^TRUE$" <<<"$module_output")" = "9"
grep -q "^fission-module-stdio-v0.1$" <<<"$module_output"
grep -q "^SIR$" <<<"$module_output"
grep -q "^module-demo.abas$" <<<"$module_output"
grep -q "^module-pipeline.abas$" <<<"$module_output"
grep -q "^FissionSir+ArcoCompy$" <<<"$module_output"
test "$(grep -c "^1$" <<<"$module_output")" = "4"

arcobasic_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_language_smoke")"

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

sir_output="$("$SOURCE_DIR/build-rivet/fission/tests/sir_builder_smoke")"

grep -q "^FALSE$" <<<"$sir_output"
grep -q "^SIR sir-smoke v0.1$" <<<"$sir_output"
grep -q "^  Print$" <<<"$sir_output"
grep -q "^    Unary(Operator=-)$" <<<"$sir_output"
grep -q "^      Binary(Operator=+)$" <<<"$sir_output"
grep -q "^        Read(Name=left)$" <<<"$sir_output"
grep -q "^        Literal(Value=1)$" <<<"$sir_output"

lexer_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_lexer_smoke")"

grep -q "^14$" <<<"$lexer_output"
grep -q "^Keyword:PRINT@1:1$" <<<"$lexer_output"
grep -q "^Identifier:hello@1:7$" <<<"$lexer_output"
grep -q "^Identifier:value@2:1$" <<<"$lexer_output"
grep -q "^Symbol:=@2:7$" <<<"$lexer_output"
grep -q "^Keyword:IF@4:1$" <<<"$lexer_output"
grep -q "^Symbol:>=@4:10$" <<<"$lexer_output"
grep -q "^Number:1@4:13$" <<<"$lexer_output"
grep -q "^EOF:@4:19$" <<<"$lexer_output"

preprocess_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_preprocess_smoke")"

grep -q "^FALSE$" <<<"$preprocess_output"
grep -q "^PRINT \"debug\"$" <<<"$preprocess_output"
grep -q "^PRINT 2$" <<<"$preprocess_output"
grep -q "^PRINT 1$" <<<"$preprocess_output"
grep -q "^DEBUG$" <<<"$preprocess_output"

preprocess_compile_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_preprocess_compile_smoke")"

grep -q "^TRUE$" <<<"$preprocess_compile_output"
grep -q "^PRINT \"debug\"$" <<<"$preprocess_compile_output"
grep -q "^SIR preprocess-compile.abas v0.1$" <<<"$preprocess_compile_output"
grep -q "^    Literal(Value=debug, LiteralKind=String)$" <<<"$preprocess_compile_output"

preprocess_error_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_preprocess_error_smoke")"

grep -q "^FALSE$" <<<"$preprocess_error_output"
grep -q "^TRUE$" <<<"$preprocess_error_output"
grep -q "FISSION_ARCOBASIC_ACTIVE_ERROR_DIRECTIVE" <<<"$preprocess_error_output"

include_metadata_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_include_metadata_smoke")"

grep -q "^TRUE$" <<<"$include_metadata_output"
grep -q "^FALSE$" <<<"$include_metadata_output"
grep -q "^1.2.3$" <<<"$include_metadata_output"
grep -q "^Arcology$" <<<"$include_metadata_output"
grep -q "^careful$" <<<"$include_metadata_output"
grep -q "^PRINT included$" <<<"$include_metadata_output"
grep -q "^PRINT after$" <<<"$include_metadata_output"
grep -q "^SIR include-metadata-smoke.abas v0.1$" <<<"$include_metadata_output"

parser_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_parser_smoke")"

grep -q "^FALSE$" <<<"$parser_output"
grep -q "^ArcoBASIC AST parser-smoke.abas$" <<<"$parser_output"
grep -q "^  Assign(Name=value)$" <<<"$parser_output"
grep -q "^    Binary(Operator=+)$" <<<"$parser_output"
grep -q "^      Binary(Operator=\\*)$" <<<"$parser_output"
grep -q "^  If$" <<<"$parser_output"
grep -q "^  While$" <<<"$parser_output"

expression_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_expression_smoke")"

test "$(grep -c "^FALSE$" <<<"$expression_output")" = "2"
grep -q "^ArcoBASIC AST expression-smoke.abas$" <<<"$expression_output"
grep -q "^    Unary(Operator=NOT)$" <<<"$expression_output"
grep -q "^    Unary(Operator=!)$" <<<"$expression_output"
grep -q "^    Literal(Value=NULL)$" <<<"$expression_output"
grep -q "^    Binary(Operator=\\*)$" <<<"$expression_output"
grep -q "^      Unary(Operator=-)$" <<<"$expression_output"
grep -q "^SIR expression-smoke.abas v0.1$" <<<"$expression_output"
grep -q "^    Literal(Value=TRUE)$" <<<"$expression_output"

function_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_function_smoke")"

test "$(grep -c "^FALSE$" <<<"$function_output")" = "2"
grep -q "^ArcoBASIC AST function-smoke.abas$" <<<"$function_output"
grep -q "^  Function(Name=add, Arguments=left,right)$" <<<"$function_output"
grep -q "^      Return$" <<<"$function_output"
grep -q "^  Assign(Name=value)$" <<<"$function_output"
grep -q "^    CallExpr$" <<<"$function_output"
grep -q "^      Read(Name=add)$" <<<"$function_output"
grep -q "^SIR function-smoke.abas v0.1$" <<<"$function_output"

postfix_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_postfix_smoke")"

test "$(grep -c "^FALSE$" <<<"$postfix_output")" = "2"
grep -q "^ArcoBASIC AST postfix-smoke.abas$" <<<"$postfix_output"
grep -q "^    MemberRead(Name=Name)$" <<<"$postfix_output"
grep -q "^      IndexRead$" <<<"$postfix_output"
grep -q "^        MemberRead(Name=Items)$" <<<"$postfix_output"
grep -q "^SIR postfix-smoke.abas v0.1$" <<<"$postfix_output"

literal_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_literal_smoke")"

test "$(grep -c "^FALSE$" <<<"$literal_output")" = "2"
grep -q "^ArcoBASIC AST literal-smoke.abas$" <<<"$literal_output"
grep -q "^    Array$" <<<"$literal_output"
grep -q "^    Object$" <<<"$literal_output"
grep -q "^      Field(Name=Name)$" <<<"$literal_output"
grep -q "^SIR literal-smoke.abas v0.1$" <<<"$literal_output"
grep -q "^    MemberRead(Name=Name)$" <<<"$literal_output"

let_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_let_smoke")"

test "$(grep -c "^FALSE$" <<<"$let_output")" = "2"
grep -q "^ArcoBASIC AST let-smoke.abas$" <<<"$let_output"
grep -q "^      Declare(Name=mapSize, Type=U64)$" <<<"$let_output"
grep -q "^      Declare(Name=mapSizeAddress, Type=PTR)$" <<<"$let_output"
grep -q "^      Declare(Name=status, Type=)$" <<<"$let_output"
grep -q "^SIR let-smoke.abas v0.1$" <<<"$let_output"

legacy_operator_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_legacy_operator_smoke")"

test "$(grep -c "^FALSE$" <<<"$legacy_operator_output")" = "2"
grep -q "^ArcoBASIC AST legacy-operator-smoke.abas$" <<<"$legacy_operator_output"
grep -q "^    Binary(Operator==)$" <<<"$legacy_operator_output"
grep -q "^    Binary(Operator=\\\\)$" <<<"$legacy_operator_output"
grep -q "^  Set$" <<<"$legacy_operator_output"
grep -q "^SIR legacy-operator-smoke.abas v0.1$" <<<"$legacy_operator_output"

string_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_string_smoke")"

grep -q "^String:hello@1:7$" <<<"$string_output"
grep -q "^String$" <<<"$string_output"
grep -q "^TRUE$" <<<"$string_output"
grep -q "^InterpolatedString:name@3:7$" <<<"$string_output"
grep -q "^FALSE$" <<<"$string_output"
grep -q "^SIR string-smoke.abas v0.1$" <<<"$string_output"
grep -q "^    Literal(Value=hello, LiteralKind=String)$" <<<"$string_output"
grep -q "^    Literal(Value=name, LiteralKind=InterpolatedString)$" <<<"$string_output"

directive_decl_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_directive_decl_smoke")"

test "$(grep -c "^FALSE$" <<<"$directive_decl_output")" = "2"
grep -q "^ArcoBASIC AST directive-decl-smoke.abas$" <<<"$directive_decl_output"
grep -q "^  Import(Path=text, Alias=Txt)$" <<<"$directive_decl_output"
grep -q "^  Directive(Name=DESCRIPTION, Value=Demo)$" <<<"$directive_decl_output"
grep -q "^  Const(Name=Limit)$" <<<"$directive_decl_output"
grep -q "^  Declare(Name=count, Type=U64)$" <<<"$directive_decl_output"
grep -q "^    CallExpr$" <<<"$directive_decl_output"
grep -q "^SIR directive-decl-smoke.abas v0.1$" <<<"$directive_decl_output"
grep -q "^  Import(Path=text, Alias=Txt)$" <<<"$directive_decl_output"

decimal_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_decimal_smoke")"

test "$(grep -c "^FALSE$" <<<"$decimal_output")" = "2"
grep -q "^Number:0.07@1:19$" <<<"$decimal_output"
grep -q "^Number:0.08@1:25$" <<<"$decimal_output"
grep -q "^ArcoBASIC AST decimal-smoke.abas$" <<<"$decimal_output"
grep -q "^      Literal(Value=0.07)$" <<<"$decimal_output"
grep -q "^      Literal(Value=1.0)$" <<<"$decimal_output"
grep -q "^SIR decimal-smoke.abas v0.1$" <<<"$decimal_output"

class_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_class_smoke")"

test "$(grep -c "^FALSE$" <<<"$class_output")" = "2"
grep -q "^ArcoBASIC AST class-smoke.abas$" <<<"$class_output"
grep -q "^  Class(Name=Counter)$" <<<"$class_output"
grep -q "^    Constructor(Arguments=start:U64)$" <<<"$class_output"
grep -q "^    Function(Name=Next, Arguments=)$" <<<"$class_output"
grep -q "^          Set$" <<<"$class_output"
grep -q "^            MemberRead(Name=value)$" <<<"$class_output"
grep -q "^SIR class-smoke.abas v0.1$" <<<"$class_output"
grep -q "^  Class(Name=Counter)$" <<<"$class_output"

access_interface_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_access_interface_smoke")"

test "$(grep -c "^FALSE$" <<<"$access_interface_output")" = "2"
grep -q "^ArcoBASIC AST access-interface-smoke.abas$" <<<"$access_interface_output"
grep -q "^  Interface(Name=Named)$" <<<"$access_interface_output"
grep -q "^  Modifier(Name=PUBLIC)$" <<<"$access_interface_output"
grep -q "^    Modifier(Name=ABSTRACT)$" <<<"$access_interface_output"
grep -q "^      Class(Name=Widget, Extends=Base, Implements=Named)$" <<<"$access_interface_output"
grep -q "^        Modifier(Name=PRIVATE)$" <<<"$access_interface_output"
grep -q "^SIR access-interface-smoke.abas v0.1$" <<<"$access_interface_output"

call_statement_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_call_statement_smoke")"

test "$(grep -c "^FALSE$" <<<"$call_statement_output")" = "2"
grep -q "^ArcoBASIC AST call-statement-smoke.abas$" <<<"$call_statement_output"
grep -q "^      ExprStmt$" <<<"$call_statement_output"
grep -q "^  ExprStmt$" <<<"$call_statement_output"
grep -q "^  Set$" <<<"$call_statement_output"
grep -q "^SIR call-statement-smoke.abas v0.1$" <<<"$call_statement_output"

control_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_control_smoke")"

test "$(grep -c "^FALSE$" <<<"$control_output")" = "2"
grep -q "^ArcoBASIC AST control-smoke.abas$" <<<"$control_output"
grep -q "^  ForRange(Name=i)$" <<<"$control_output"
grep -q "^  ForEach(Name=item)$" <<<"$control_output"
grep -q "^      LoopControl(Action=CONTINUE, Target=FOR)$" <<<"$control_output"
grep -q "^  Try(Catch=err)$" <<<"$control_output"
grep -q "^      Throw$" <<<"$control_output"
grep -q "^SIR control-smoke.abas v0.1$" <<<"$control_output"

callable_comprehension_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_callable_comprehension_smoke")"

test "$(grep -c "^FALSE$" <<<"$callable_comprehension_output")" = "2"
grep -q "^ArcoBASIC AST callable-comprehension-smoke.abas$" <<<"$callable_comprehension_output"
grep -q "^    AddressOf(Name=Demo.Run)$" <<<"$callable_comprehension_output"
grep -q "^    Copy$" <<<"$callable_comprehension_output"
grep -q "^    ArrayComprehension(Name=i)$" <<<"$callable_comprehension_output"
grep -q "^SIR callable-comprehension-smoke.abas v0.1$" <<<"$callable_comprehension_output"

do_bitwise_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_do_bitwise_smoke")"

test "$(grep -c "^FALSE$" <<<"$do_bitwise_output")" = "2"
grep -q "^ArcoBASIC AST do-bitwise-smoke.abas$" <<<"$do_bitwise_output"
grep -q "^  Do(PreMode=WHILE, PostMode=)$" <<<"$do_bitwise_output"
grep -q "^  Do(PreMode=, PostMode=UNTIL)$" <<<"$do_bitwise_output"
grep -q "^        Binary(Operator=SHR)$" <<<"$do_bitwise_output"
grep -q "^    Binary(Operator=CONTAINS)$" <<<"$do_bitwise_output"
grep -q "^SIR do-bitwise-smoke.abas v0.1$" <<<"$do_bitwise_output"

type_compound_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_type_compound_smoke")"

test "$(grep -c "^FALSE$" <<<"$type_compound_output")" = "2"
grep -q "^ArcoBASIC AST type-compound-smoke.abas$" <<<"$type_compound_output"
grep -q "^  Function(Name=Next, Arguments=value:U64, Returns=U64)$" <<<"$type_compound_output"
grep -q "^      Assign(Name=value)$" <<<"$type_compound_output"
grep -q "^        Binary(Operator=+)$" <<<"$type_compound_output"
grep -q "^  Set$" <<<"$type_compound_output"
grep -q "^    MemberRead(Name=total)$" <<<"$type_compound_output"
grep -q "^SIR type-compound-smoke.abas v0.1$" <<<"$type_compound_output"

logic_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_logic_smoke")"

test "$(grep -c "^FALSE$" <<<"$logic_output")" = "2"
grep -q "^ArcoBASIC AST logic-smoke.abas$" <<<"$logic_output"
grep -q "^    Binary(Operator=ANDALSO)$" <<<"$logic_output"
grep -q "^      Binary(Operator=OR)$" <<<"$logic_output"
grep -q "^        Binary(Operator=%)$" <<<"$logic_output"
grep -q "^    Binary(Operator=MOD)$" <<<"$logic_output"
grep -q "^SIR logic-smoke.abas v0.1$" <<<"$logic_output"

named_defaults_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_named_defaults_smoke")"

test "$(grep -c "^FALSE$" <<<"$named_defaults_output")" = "2"
grep -q "^ArcoBASIC AST named-defaults-smoke.abas$" <<<"$named_defaults_output"
grep -q "^  Function(Name=clamp, Arguments=value:U64,minimum:U64=0,maximum:U64=255, Returns=U64)$" <<<"$named_defaults_output"
grep -q "^      NamedArg(Name=value)$" <<<"$named_defaults_output"
grep -q "^      NamedArg(Name=maximum)$" <<<"$named_defaults_output"
grep -q "^SIR named-defaults-smoke.abas v0.1$" <<<"$named_defaults_output"

single_line_if_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_single_line_if_smoke")"

test "$(grep -c "^FALSE$" <<<"$single_line_if_output")" = "2"
grep -q "^ArcoBASIC AST single-line-if-smoke.abas$" <<<"$single_line_if_output"
grep -q "^  If$" <<<"$single_line_if_output"
grep -q "^      If$" <<<"$single_line_if_output"
grep -q "^SIR single-line-if-smoke.abas v0.1$" <<<"$single_line_if_output"
grep -q "^      Branch$" <<<"$single_line_if_output"

semantic_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_semantic_smoke")"

grep -q "^FALSE$" <<<"$semantic_output"
grep -q "^ArcoBASIC Semantics semantic-smoke.abas$" <<<"$semantic_output"
grep -q "^  Import module::Txt$" <<<"$semantic_output"
grep -q "^  Variable module::count AS U64$" <<<"$semantic_output"
grep -q "^  Function module::Add AS U64$" <<<"$semantic_output"
grep -q "^  Parameter module::Add::right AS U64 = 1$" <<<"$semantic_output"
grep -q "^  Class module::Box$" <<<"$semantic_output"

semantic_diagnostics_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_semantic_diagnostics_smoke")"

grep -q "^FALSE$" <<<"$semantic_diagnostics_output"
grep -q "^TRUE$" <<<"$semantic_diagnostics_output"
grep -q "FISSION_ARCOBASIC_DUPLICATE_SYMBOL" <<<"$semantic_diagnostics_output"

semantic_reference_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_semantic_reference_smoke")"

grep -q "^TRUE$" <<<"$semantic_reference_output"
grep -q "^FALSE$" <<<"$semantic_reference_output"
grep -q "^4$" <<<"$semantic_reference_output"
grep -q "^5$" <<<"$semantic_reference_output"
grep -q "^    Call module::Paint::Helper -> Resolved module::Helper$" <<<"$semantic_reference_output"
grep -q "^    Read module::Paint::GUI -> External$" <<<"$semantic_reference_output"
grep -q "^    Read module::Paint::missing -> Unresolved$" <<<"$semantic_reference_output"

strict_semantic_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_strict_semantic_smoke")"

grep -q "^FALSE$" <<<"$strict_semantic_output"
grep -q "^TRUE$" <<<"$strict_semantic_output"
grep -q "FISSION_ARCOBASIC_UNRESOLVED_SYMBOL" <<<"$strict_semantic_output"
grep -q "^    Read module::Paint::GUI -> External$" <<<"$strict_semantic_output"
grep -q "^    Read module::Paint::missing -> Unresolved$" <<<"$strict_semantic_output"

shared_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_shared_smoke")"

test "$(grep -c "^FALSE$" <<<"$shared_output")" = "2"
grep -q "^ArcoBASIC AST shared-smoke.abas$" <<<"$shared_output"
grep -q "^      Modifier(Name=SHARED)$" <<<"$shared_output"
grep -q "^        Function(Name=Issue, Arguments=prefix:String, Returns=String)$" <<<"$shared_output"
grep -q "^SIR shared-smoke.abas v0.1$" <<<"$shared_output"

diagnostics_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_diagnostics_smoke")"

grep -q "^FALSE$" <<<"$diagnostics_output"
grep -q "^SIR$" <<<"$diagnostics_output"
grep -q "^TRUE$" <<<"$diagnostics_output"
grep -q "FISSION_ARCOBASIC_UNSUPPORTED_STATEMENT" <<<"$diagnostics_output"

reveal_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_reveal_smoke")"

grep -q "^ArcoBASIC AST reveal.abas$" <<<"$reveal_output"
grep -q "^SIR reveal.abas v0.1$" <<<"$reveal_output"
grep -q "^PIPELINE RESOLVED$" <<<"$reveal_output"
grep -q "^language.arcobasic: Source.arcobasic -> SIR$" <<<"$reveal_output"

real_timer_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_real_timer_parse_smoke")"

test "$(grep -c "^FALSE$" <<<"$real_timer_output")" = "2"
grep -q "^14$" <<<"$real_timer_output"

real_project_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_real_project_parse_smoke")"

grep -q "^10$" <<<"$real_project_output"
grep -q "^0$" <<<"$real_project_output"

fission_self_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_fission_self_parse_smoke")"

grep -q "^91$" <<<"$fission_self_output"
grep -q "^0$" <<<"$fission_self_output"

fission_self_semantic_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_fission_self_semantic_smoke")"

grep -q "^91$" <<<"$fission_self_semantic_output"
grep -q "^138$" <<<"$fission_self_semantic_output"
grep -q "^2876$" <<<"$fission_self_semantic_output"
grep -q "^FALSE$" <<<"$fission_self_semantic_output"

import_semantic_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_import_semantic_smoke")"

grep -q "^FALSE$" <<<"$import_semantic_output"
grep -q "^2$" <<<"$import_semantic_output"
grep -q "^1$" <<<"$import_semantic_output"
grep -q "^TRUE$" <<<"$import_semantic_output"
grep -q "FISSION_ARCOBASIC_IMPORT_UNRESOLVED" <<<"$import_semantic_output"

program_symbol_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_program_symbol_smoke")"

grep -q "^FALSE$" <<<"$program_symbol_output"
grep -q "^8$" <<<"$program_symbol_output"
grep -q "^Const /tmp/fission_program_symbol_library.abas::module::Answer$" <<<"$program_symbol_output"
grep -q "^Class /tmp/fission_program_symbol_library.abas::module::Box$" <<<"$program_symbol_output"
grep -q "^Field /tmp/fission_program_symbol_library.abas::module::Box::Value AS U64$" <<<"$program_symbol_output"
grep -q "^Function /tmp/fission_program_symbol_library.abas::module::MakeBox AS Box$" <<<"$program_symbol_output"

cross_file_semantic_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_cross_file_semantic_smoke")"

grep -q "^FALSE$" <<<"$cross_file_semantic_output"
grep -q "^2$" <<<"$cross_file_semantic_output"
grep -q "^1$" <<<"$cross_file_semantic_output"
grep -q "^4$" <<<"$cross_file_semantic_output"
grep -q "^2$" <<<"$cross_file_semantic_output"
grep -q "^    Call /tmp/fission_cross_file_main.abas::module::helper -> ResolvedImport /tmp/fission_cross_file_library.abas::module::helper$" <<<"$cross_file_semantic_output"
grep -q "^    Read /tmp/fission_cross_file_main.abas::module::sharedValue -> ResolvedImport /tmp/fission_cross_file_library.abas::module::sharedValue$" <<<"$cross_file_semantic_output"

signature_semantic_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_signature_semantic_smoke")"

grep -q "^FALSE$" <<<"$signature_semantic_output"
grep -q "^clamp(value:U64,minimum:U64=0,maximum:U64=255) AS U64$" <<<"$signature_semantic_output"
grep -q "^3$" <<<"$signature_semantic_output"
grep -q "^value$" <<<"$signature_semantic_output"
grep -q "^U64$" <<<"$signature_semantic_output"
grep -q "^0$" <<<"$signature_semantic_output"
grep -q "^255$" <<<"$signature_semantic_output"
grep -q "^/tmp/fission_signature_semantic.abas::module::clamp clamp(value:U64,minimum:U64=0,maximum:U64=255) AS U64$" <<<"$signature_semantic_output"

binding_semantic_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_binding_semantic_smoke")"

grep -q "^FALSE$" <<<"$binding_semantic_output"
grep -q "^3$" <<<"$binding_semantic_output"
grep -q "^/tmp/fission_binding_library.abas::module::twice$" <<<"$binding_semantic_output"
grep -q "^Function$" <<<"$binding_semantic_output"
grep -q "^U64$" <<<"$binding_semantic_output"
grep -q "^twice(value:U64) AS U64$" <<<"$binding_semantic_output"
grep -q "^Call /tmp/fission_binding_main.abas::module::twice => Function /tmp/fission_binding_library.abas::module::twice AS U64 \\[twice(value:U64) AS U64\\]$" <<<"$binding_semantic_output"

sir_semantic_output="$("$SOURCE_DIR/build-rivet/fission/tests/sir_semantic_artifact_smoke")"

grep -q "^FALSE$" <<<"$sir_semantic_output"
grep -q "^program$" <<<"$sir_semantic_output"
grep -q "^4$" <<<"$sir_semantic_output"
test "$(grep -c "^TRUE$" <<<"$sir_semantic_output")" = "3"

amir_output="$("$SOURCE_DIR/build-rivet/fission/tests/sir_to_amir_smoke")"

test "$(grep -c "^FALSE$" <<<"$amir_output")" = "2"
grep -q "^A-MIR MODULE \"amir-smoke.abas\"$" <<<"$amir_output"
grep -q "^FUNCTION Main RETURNS I32$" <<<"$amir_output"
grep -q "^    %t0 := CALL Runtime.Args$" <<<"$amir_output"
grep -q "^    STORE Args, %t0$" <<<"$amir_output"
grep -q "^    DECLARE_FUNCTION Sum params=a AS U64,b AS U64 returns=U64$" <<<"$amir_output"
grep -q "^    %t7 :BOOL := INT.CMP_EQ %t5, %t6 \[,\]$" <<<"$amir_output"
grep -q "^    BRANCH %t7, IfThen0, IfElse1$" <<<"$amir_output"
grep -q "^    %t10 := CALL Sum %t8 %t9$" <<<"$amir_output"
grep -q "^    %t14 :BOOL := INT.CMP_LT_UNSIGNED %t12, %t13 \[,\]$" <<<"$amir_output"
grep -q "^    %t17 := + %t15, %t16$" <<<"$amir_output"
grep -q "^FUNCTION Sum(a AS U64, b AS U64) RETURNS U64$" <<<"$amir_output"
grep -q "^    %t3 :U64 := INT.ADD %t1, %t2 \[U64,U64\]$" <<<"$amir_output"
grep -q "^    RETURN U64 %t3$" <<<"$amir_output"

amir_for_output="$("$SOURCE_DIR/build-rivet/fission/tests/sir_to_amir_for_smoke")"

test "$(grep -c "^FALSE$" <<<"$amir_for_output")" = "2"
grep -q "^A-MIR MODULE \"amir-for-smoke.abas\"$" <<<"$amir_for_output"
grep -q "^    STORE i, %t1$" <<<"$amir_for_output"
grep -q "^    STORE __fission_for_end0, %t2$" <<<"$amir_for_output"
grep -q "^    STORE __fission_for_step1, %t3$" <<<"$amir_for_output"
grep -q "^BLOCK ForCond2$" <<<"$amir_for_output"
grep -q "^    BRANCH %t6, ForCondPos3, ForCondNeg4$" <<<"$amir_for_output"
grep -q "^BLOCK ForCondPos3$" <<<"$amir_for_output"
grep -q "^    %t9 := <= %t7, %t8$" <<<"$amir_for_output"
grep -q "^    BRANCH %t9, ForBody5, ForEnd7$" <<<"$amir_for_output"
grep -q "^BLOCK ForCondNeg4$" <<<"$amir_for_output"
grep -q "^    %t12 := >= %t10, %t11$" <<<"$amir_for_output"
grep -q "^BLOCK ForBody5$" <<<"$amir_for_output"
grep -q "^BLOCK ForInc6$" <<<"$amir_for_output"
grep -q "^    %t16 := + %t14, %t15$" <<<"$amir_for_output"
grep -q "^BLOCK ForEnd7$" <<<"$amir_for_output"
grep -q "^    %t17 := CONST \"hello\"$" <<<"$amir_for_output"

amir_loops2_output="$("$SOURCE_DIR/build-rivet/fission/tests/sir_to_amir_loops2_smoke")"

test "$(grep -c "^FALSE$" <<<"$amir_loops2_output")" = "2"
grep -q "^    STORE __fission_each_items0, %t5$" <<<"$amir_loops2_output"
grep -q "^    STORE __fission_each_index1, %t6$" <<<"$amir_loops2_output"
grep -q "^BLOCK ForEachCond2$" <<<"$amir_loops2_output"
grep -q "^    %t9 := CALL LEN %t8$" <<<"$amir_loops2_output"
grep -q "^    BRANCH %t10, ForEachBody3, ForEachEnd5$" <<<"$amir_loops2_output"
grep -q "^BLOCK ForEachBody3$" <<<"$amir_loops2_output"
grep -q "^    %t13 := INDEX %t11, %t12$" <<<"$amir_loops2_output"
grep -q "^BLOCK ForEachInc4$" <<<"$amir_loops2_output"
grep -q "^BLOCK ForEachEnd5$" <<<"$amir_loops2_output"
grep -q "^BLOCK DoBody6$" <<<"$amir_loops2_output"
grep -q "^    %t24 :BOOL := INT.CMP_LT_UNSIGNED %t22, %t23 \[,\]$" <<<"$amir_loops2_output"
grep -q "^    BRANCH %t24, DoBody6, DoEnd7$" <<<"$amir_loops2_output"
grep -q "^BLOCK DoEnd7$" <<<"$amir_loops2_output"

# This one runs the produced bytecode through the real legacy VM
# (`ArcoFission run`) and checks its actual printed output -- true
# end-to-end functional verification (Source.arcobasic -> Fission SIR ->
# Fission A-MIR -> Fission bytecode -> real execution), not just a
# rendered-text comparison the way the A-MIR checks above are.
bytecode_full_output="$("$SOURCE_DIR/build-rivet/fission/tests/amir_to_bytecode_smoke")"

test "$(grep -c "^FALSE$" <<<"$bytecode_full_output")" = "3"
grep -q "^ARCOFISSION BYTECODE$" <<<"$bytecode_full_output"
grep -q "^FUNCTION Sum RETURNS U64$" <<<"$bytecode_full_output"
bytecode_smoke_arcof="$(mktemp /tmp/fission_bytecode_smoke.XXXXXX.arcof)"
trap 'rm -f "$bytecode_smoke_arcof"' EXIT
sed -n '/^ARCOFISSION BYTECODE$/,$p' <<<"$bytecode_full_output" > "$bytecode_smoke_arcof"
bytecode_run_output="$("$SOURCE_DIR/build-rivet/ArcoFission" run "$bytecode_smoke_arcof")"
expected_bytecode_run_output="$(printf '1\n2\n3\nhello\n10')"
test "$bytecode_run_output" = "$expected_bytecode_run_output"

bytecode_array_full_output="$("$SOURCE_DIR/build-rivet/fission/tests/amir_bytecode_array_smoke")"

test "$(grep -c "^FALSE$" <<<"$bytecode_array_full_output")" = "3"
grep -q "^12 INDEX %t7 %t5 %t6$" <<<"$bytecode_array_full_output"
grep -q "^5 STORE_INDEX L1 %t9 %t8$" <<<"$bytecode_array_full_output"
bytecode_array_smoke_arcof="$(mktemp /tmp/fission_bytecode_array_smoke.XXXXXX.arcof)"
trap 'rm -f "$bytecode_smoke_arcof" "$bytecode_array_smoke_arcof"' EXIT
sed -n '/^ARCOFISSION BYTECODE$/,$p' <<<"$bytecode_array_full_output" > "$bytecode_array_smoke_arcof"
bytecode_array_run_output="$("$SOURCE_DIR/build-rivet/ArcoFission" run "$bytecode_array_smoke_arcof")"
expected_bytecode_array_run_output="$(printf '2\n9')"
test "$bytecode_array_run_output" = "$expected_bytecode_array_run_output"

bytecode_object_try_full_output="$("$SOURCE_DIR/build-rivet/fission/tests/amir_bytecode_object_try_smoke")"

test "$(grep -c "^FALSE$" <<<"$bytecode_object_try_full_output")" = "3"
grep -q "^11 OBJECT %t2 Name:%t1$" <<<"$bytecode_object_try_full_output"
grep -q "^5 STORE_INDEX L1 %t4 %t3$" <<<"$bytecode_object_try_full_output"
grep -q "^15 TRY_BEGIN Catch0 err$" <<<"$bytecode_object_try_full_output"
grep -q "^23 THROW %t8$" <<<"$bytecode_object_try_full_output"
grep -q "^16 TRY_END$" <<<"$bytecode_object_try_full_output"
bytecode_object_try_smoke_arcof="$(mktemp /tmp/fission_bytecode_object_try_smoke.XXXXXX.arcof)"
trap 'rm -f "$bytecode_smoke_arcof" "$bytecode_array_smoke_arcof" "$bytecode_object_try_smoke_arcof"' EXIT
sed -n '/^ARCOFISSION BYTECODE$/,$p' <<<"$bytecode_object_try_full_output" > "$bytecode_object_try_smoke_arcof"
bytecode_object_try_run_output="$("$SOURCE_DIR/build-rivet/ArcoFission" run "$bytecode_object_try_smoke_arcof")"
expected_bytecode_object_try_run_output="$(printf 'b\nboom\nafter')"
test "$bytecode_object_try_run_output" = "$expected_bytecode_object_try_run_output"

bytecode_class_full_output="$("$SOURCE_DIR/build-rivet/fission/tests/amir_bytecode_class_smoke")"

test "$(grep -c "^FALSE$" <<<"$bytecode_class_full_output")" = "3"
grep -q "^18 DECLARE_CLASS Counter$" <<<"$bytecode_class_full_output"
grep -q "^FUNCTION Counter.Init RETURNS VALUE$" <<<"$bytecode_class_full_output"
grep -q "^P0 SELF$" <<<"$bytecode_class_full_output"
grep -q "^20 RETURN VALUE nothing$" <<<"$bytecode_class_full_output"
grep -q "^FUNCTION Counter.Increment RETURNS U64$" <<<"$bytecode_class_full_output"
grep -q "^FUNCTION Counter.__new RETURNS VALUE$" <<<"$bytecode_class_full_output"
grep -q "^11 OBJECT %t12$" <<<"$bytecode_class_full_output"
grep -q "^FUNCTION Counter RETURNS VALUE$" <<<"$bytecode_class_full_output"
grep -q "^8 CALL_VALUE %t20 Counter %t19$" <<<"$bytecode_class_full_output"
grep -q "^8 CALL_VALUE %t21 c.Increment$" <<<"$bytecode_class_full_output"
bytecode_class_smoke_arcof="$(mktemp /tmp/fission_bytecode_class_smoke.XXXXXX.arcof)"
trap 'rm -f "$bytecode_smoke_arcof" "$bytecode_array_smoke_arcof" "$bytecode_object_try_smoke_arcof" "$bytecode_class_smoke_arcof"' EXIT
sed -n '/^ARCOFISSION BYTECODE$/,$p' <<<"$bytecode_class_full_output" > "$bytecode_class_smoke_arcof"
bytecode_class_run_output="$("$SOURCE_DIR/build-rivet/ArcoFission" run "$bytecode_class_smoke_arcof")"
expected_bytecode_class_run_output="$(printf '6\n6')"
test "$bytecode_class_run_output" = "$expected_bytecode_class_run_output"

self_loop_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_loop_semantic_smoke")"

grep -q "^TRUE$" <<<"$self_loop_output"
grep -q "^FALSE$" <<<"$self_loop_output"
grep -q "^  Variable module::item$" <<<"$self_loop_output"
grep -q "^  Variable module::index$" <<<"$self_loop_output"
grep -q "^    Read module::item -> Resolved module::item$" <<<"$self_loop_output"
grep -q "^    Read module::index -> Resolved module::index$" <<<"$self_loop_output"

catch_semantic_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_catch_semantic_smoke")"

grep -q "^TRUE$" <<<"$catch_semantic_output"
grep -q "^FALSE$" <<<"$catch_semantic_output"
grep -q "^  Variable module::err$" <<<"$catch_semantic_output"
grep -q "^    Read module::err -> Resolved module::err$" <<<"$catch_semantic_output"

self_semantic_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_self_semantic_smoke")"

grep -q "^TRUE$" <<<"$self_semantic_output"
grep -q "^FALSE$" <<<"$self_semantic_output"
grep -q "^5$" <<<"$self_semantic_output"
grep -q "^  Field module::Counter::Value$" <<<"$self_semantic_output"
grep -q "^    Field module::Counter::Value -> Resolved module::Counter::Value$" <<<"$self_semantic_output"
grep -q "^    Call module::Counter::Current -> Resolved module::Counter::Current$" <<<"$self_semantic_output"

os_stdlib_output="$("$SOURCE_DIR/build-rivet/fission/tests/arcobasic_os_stdlib_parse_smoke")"

grep -q "^25$" <<<"$os_stdlib_output"
grep -q "^0$" <<<"$os_stdlib_output"

# fission/tests/substrate_programs/stack_sum.abas is a real ArcoBASIC program compiled entirely by
# the Fission Compiler Substrate (via fission/cli/compile_to_bytecode.abas, not legacy ArcoFission)
# and packaged into a standalone ArcoCapsule by FissionSubstrateCapsuleTarget (rivet/stdlib/
# rivet.abas), built above by the same `rivet build` this script already runs. Run the resulting
# ELF64 directly -- no ArcoFission CLI involved at all -- and check its output against legacy
# ArcoFission's own `compile-run` on the identical source as the equivalence-testing oracle.
stack_sum_output="$("$SOURCE_DIR/build/fission-substrate-programs/stack_sum")"
expected_stack_sum_output="$(printf '5\n55\n25\n4')"
test "$stack_sum_output" = "$expected_stack_sum_output"
oracle_stack_sum_output="$("$ARCOFISSION" compile-run "$SOURCE_DIR/fission/tests/substrate_programs/stack_sum.abas")"
test "$stack_sum_output" = "$oracle_stack_sum_output"

# fission/tests/native_programs/hello_native.abas is compiled entirely by the Fission Compiler
# Substrate's own native x86-64 codegen (fission/amir/lower_x86_64.abas) via
# fission/cli/compile_to_x86_64.abas + the system `as`/`ld` toolchain -- NO embedded bytecode VM,
# and no legacy ArcoFission involvement in the compilation itself (only as the source of the
# equivalence-testing oracle below). Built above by the same `rivet build` this script already
# runs (FissionNativeCapsuleTarget, rivet/stdlib/rivet.abas).
hello_native_output="$("$SOURCE_DIR/build/fission-native-programs/hello_native")"
expected_hello_native_output="Hello, native!"
test "$hello_native_output" = "$expected_hello_native_output"
oracle_hello_native_output="$("$ARCOFISSION" compile-run "$SOURCE_DIR/fission/tests/native_programs/hello_native.abas")"
test "$hello_native_output" = "$oracle_hello_native_output"

# fission/tests/native_programs/fizzbuzz.abas exercises native codegen's Phase 2 construct set
# together (FOR-range, arithmetic, MOD, comparisons, nested IF, PRINT of both string literals and
# computed integers) rather than a single-construct probe.
fizzbuzz_native_output="$("$SOURCE_DIR/build/fission-native-programs/fizzbuzz")"
expected_fizzbuzz_output="$(printf '1\n2\nFizz\n4\nBuzz\nFizz\n7\n8\nFizz\nBuzz\n11\nFizz\n13\n14\nFizzBuzz\n16\n17\nFizz\n19\nBuzz')"
test "$fizzbuzz_native_output" = "$expected_fizzbuzz_output"
oracle_fizzbuzz_output="$("$ARCOFISSION" compile-run "$SOURCE_DIR/fission/tests/native_programs/fizzbuzz.abas")"
test "$fizzbuzz_native_output" = "$oracle_fizzbuzz_output"

# fission/tests/native_programs/fibonacci.abas exercises native codegen's Phase 3 construct set:
# real user-declared FUNCTIONs, a genuine SysV-shaped calling convention (arguments in
# %rdi/%rsi/..., return value in %rax, independent stack frames per call), and real recursion.
fibonacci_native_output="$("$SOURCE_DIR/build/fission-native-programs/fibonacci")"
expected_fibonacci_output="$(printf '0\n1\n1\n2\n3\n5\n8\n13\n21\n34\n55\n89\n144\n144')"
test "$fibonacci_native_output" = "$expected_fibonacci_output"
oracle_fibonacci_output="$("$ARCOFISSION" compile-run "$SOURCE_DIR/fission/tests/native_programs/fibonacci.abas")"
test "$fibonacci_native_output" = "$oracle_fibonacci_output"

# fission/tests/native_programs/many_params.abas exercises real SysV stack-passed arguments (a
# 10-parameter function, 4 more than the 6 SysV passes in registers) plus deep recursion with a
# 2-parameter accumulator.
many_params_native_output="$("$SOURCE_DIR/build/fission-native-programs/many_params")"
expected_many_params_output="$(printf '10\n20\n30\n40\n50\n60\n70\n80\n90\n100\n10100\n5050')"
test "$many_params_native_output" = "$expected_many_params_output"
oracle_many_params_output="$("$ARCOFISSION" compile-run "$SOURCE_DIR/fission/tests/native_programs/many_params.abas")"
test "$many_params_native_output" = "$oracle_many_params_output"

# fission/tests/native_programs/strings.abas exercises native codegen's Phase 4 construct set:
# real string variables (assign, PRINT, reassign, copy one into another), mixed with ordinary
# numeric variables and a loop.
strings_native_output="$("$SOURCE_DIR/build/fission-native-programs/strings")"
expected_strings_output="$(printf 'Hello, \nWorld\nUniverse\nWorld\nLap\n1\nLap\n2\nLap\n3')"
test "$strings_native_output" = "$expected_strings_output"
oracle_strings_output="$("$ARCOFISSION" compile-run "$SOURCE_DIR/fission/tests/native_programs/strings.abas")"
test "$strings_native_output" = "$oracle_strings_output"

echo "fission_substrate_core_smoke: all checks passed"
