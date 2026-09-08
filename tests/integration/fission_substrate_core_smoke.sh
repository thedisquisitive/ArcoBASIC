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
grep -q "^12$" <<<"$arcobasic_output"
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

control_output="$("$ARCOFISSION" compile-run fission/tests/arcobasic_control_smoke.abas)"

test "$(grep -c "^FALSE$" <<<"$control_output")" = "2"
grep -q "^ArcoBASIC AST control-smoke.abas$" <<<"$control_output"
grep -q "^  ForRange(Name=i)$" <<<"$control_output"
grep -q "^  ForEach(Name=item)$" <<<"$control_output"
grep -q "^      LoopControl(Action=CONTINUE, Target=FOR)$" <<<"$control_output"
grep -q "^  Try(Catch=err)$" <<<"$control_output"
grep -q "^      Throw$" <<<"$control_output"
grep -q "^SIR control-smoke.abas v0.1$" <<<"$control_output"

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
grep -q "^    Binary(Operator=OR)$" <<<"$logic_output"
grep -q "^      Binary(Operator=ANDALSO)$" <<<"$logic_output"
grep -q "^          Binary(Operator=%)$" <<<"$logic_output"
grep -q "^    Binary(Operator=MOD)$" <<<"$logic_output"
grep -q "^SIR logic-smoke.abas v0.1$" <<<"$logic_output"

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

echo "fission_substrate_core_smoke: all checks passed"
