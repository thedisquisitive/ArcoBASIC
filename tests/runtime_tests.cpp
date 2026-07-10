#include "arco/runtime.hpp"
#include "arco/shell.hpp"
#include "arco_c_api.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path);
    output << text;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string run_capture(const std::string& code) {
    arco::Runtime runtime;
    std::ostringstream output;
    runtime.set_output(output);
    const auto result = runtime.run_string(code);
    require(result.ok, result.error);
    return output.str();
}

} // namespace

int main() {
    require(run_capture("PRINT \"HELLO\"\n") == "HELLO\n", "prints strings");
    require(run_capture("PRINT \"a\\nb\"\nPRINT \"col\\tvalue\"\nPRINT \"quote: \\\"ok\\\"\"\nPRINT \"slash: \\\\\"\n") == "a\nb\ncol\tvalue\nquote: \"ok\"\nslash: \\\n", "decodes string escape sequences");
    require(run_capture("name = \"Ada\"\nparts = 3\nPRINT $\"{name} has {parts} parts\"\nPRINT $\"math {1 + 2}\"\nPRINT $\"date {LEN(DATE()) > 0}\"\nPRINT $\"literal {{braces}}\"\n") == "Ada has 3 parts\nmath 3\ndate TRUE\nliteral {braces}\n", "interpolates strings with expressions");
    require(run_capture("10 x = 0\n20 WHILE x < 3\n30 x = x + 1\n40 PRINT x\n50 WEND\n") == "1\n2\n3\n", "runs line-numbered scripts");
    require(run_capture("10 x = 0\n20 PRINT x\n30 x += 1\n40 IF x >= 3 THEN GOTO 60\n50 GOTO 20\n60 PRINT \"done\"\n") == "0\n1\n2\ndone\n", "runs classic GOTO line-number loops");
    require(run_capture("10 PRINT \"before\"\n20 STOP\n30 PRINT \"after\"\n") == "before\n", "stops current program with STOP");
    require(run_capture("LET x = 2 + 3 * 4\nPRINT x\n") == "14\n", "evaluates arithmetic");
    require(run_capture("x = 10\nIF x == 10 THEN PRINT \"TEN\"\nPRINT \"DONE\"\n") == "TEN\nDONE\n", "runs single-line IF with double equals");
    require(run_capture("x = 1 : y = 2 : PRINT x + y\nIF y == 2 THEN PRINT \"two\" : PRINT \"again\"\n") == "3\ntwo\nagain\n", "runs colon-separated statements");
    require(run_capture("FUNCTION Sum(a, b)\nRETURN a + b\nEND FUNCTION\nPRINT Sum(2, 3)\n") == "5\n", "runs user functions");
    require(run_capture("FUNCTION Greet(name, punctuation = \"!\")\nRETURN $\"Hello {name}{punctuation}\"\nEND FUNCTION\nPRINT Greet(\"Ada\")\nPRINT Greet(\"Grace\", \"?\")\n") == "Hello Ada!\nHello Grace?\n", "runs function default parameters");
    require(run_capture("FUNCTION AddOffset(x, offset = x + 1)\nRETURN x + offset\nEND FUNCTION\nPRINT AddOffset(4)\n") == "9\n", "evaluates function defaults in call scope");
    require(run_capture("x = 10\nFUNCTION Change(x)\nx += 5\nRETURN x\nEND FUNCTION\nPRINT Change(1)\nPRINT x\n") == "6\n10\n", "keeps function parameters local");
    require(run_capture("FUNCTION PersonName(person)\nRETURN person.Name\nEND FUNCTION\nPRINT PersonName({\"Name\": \"Ada\"})\n") == "Ada\n", "reads object parameters in functions");
    require(run_capture("REM top-level comment\n10 REM numbered comment\n20 PRINT \"comments ok\"\n") == "comments ok\n", "runs BASIC REM comments and numbered comment lines");
    require(run_capture("IF 3 > 2 THEN\nPRINT \"YES\"\nELSE\nPRINT \"NO\"\nEND IF\n") == "YES\n", "runs IF branch");
    require(run_capture("FOR i = 1 TO 3\nPRINT i\nNEXT\n") == "1\n2\n3\n", "runs FOR loops");
    require(run_capture("FOR item IN [\"a\", \"b\"]\nPRINT item\nNEXT\n") == "a\nb\n", "runs FOR IN loops");
    require(run_capture("person = {\"Name\": \"Ada\", \"Age\": 36}\nPRINT person.Name\n") == "Ada\n", "reads object properties");
    require(run_capture("items = [10, 20, 30]\nPRINT items[1]\nitems[1] = 99\nPRINT items[1]\nPRINT items\n") == "20\n99\n[10, 99, 30]\n", "reads and writes array indexes");
    require(run_capture("items = [1, 2]\nPRINT Array.Push(items, 3)\nPRINT items\nPRINT Array.Pop(items)\nPRINT items\nPRINT Array.Find(items, 2)\nPRINT Array.Reverse(items)\nPRINT Array.Join([\"a\", \"b\", \"c\"], \":\")\nPRINT Array.Contains([3, 1, 2], 1)\nPRINT Array.Sort([3, 1, 2])\n") == "3\n[1, 2, 3]\n3\n[1, 2]\n1\n[2, 1]\na:b:c\nTRUE\n[1, 2, 3]\n", "runs array helper functions");
    require(run_capture("person = {\"Name\": \"Ada\", \"Role\": \"Admin\"}\nPRINT Object.Has(person, \"Name\")\nPRINT Object.Get(person, \"Missing\", \"fallback\")\ncopy = Object.Set(person, \"Role\", \"Operator\")\nPRINT copy.Role\nPRINT person.Role\nPRINT Array.Contains(Object.Keys(person), \"Name\")\n") == "TRUE\nfallback\nOperator\nAdmin\nTRUE\n", "runs object helper functions");
    require(run_capture("PRINT Time.Timestamp() > 0\nPRINT LEN(Time.Now()) > 0\nSleep(0)\nPRINT \"awake\"\n") == "TRUE\nTRUE\nawake\n", "runs time and sleep helpers");
    require(run_capture("person = {\"Name\": \"Ada\"}\nperson.Name = \"Grace\"\nPRINT person.Name\n") == "Grace\n", "writes object properties");
    require(run_capture("items = [1, 2, 3]\nPRINT items\nPRINT 2 IN items\n") == "[1, 2, 3]\nTRUE\n", "handles arrays");
    require(run_capture("PRINT LEN([1, 2, 3])\nPRINT Upper(\"basic\")\nPRINT \"abc\" CONTAINS \"b\"\n") == "3\nBASIC\nTRUE\n", "runs core helper functions");
    require(run_capture("FUNCTION MixedName(value)\nRETURN upper(value)\nEND FUNCTION\nPRINT mixedname(\"case\")\nPRINT LOWER(\"CASE\")\nPRINT len([1, 2])\nPRINT string(123)\n") == "CASE\ncase\n2\n123\n", "calls core and user functions case-insensitively");
    require(run_capture("PRINT TYPEOF(NULL)\nPRINT TYPEOF([1])\nPRINT ISNULL(NULL)\nPRINT NUMBER(\"42\") + 1\nPRINT STRING(123)\n") == "Null\nArray\nTRUE\n43\n123\n", "runs type and conversion helpers");
    require(run_capture("PRINT String.Trim(\"  hello  \")\nPRINT String.Split(\"a,b,c\", \",\")\nPRINT String.Replace(\"a-b-c\", \"-\", \"+\")\nPRINT String.Contains(\"abcdef\", \"cd\")\nPRINT String.StartsWith(\"abcdef\", \"abc\")\nPRINT String.EndsWith(\"abcdef\", \"def\")\nPRINT String.Lines(\"a\\nb\")\nPRINT Format(\"{0}:{1}\", \"left\", 42)\n") == "hello\n[a, b, c]\na+b+c\nTRUE\nTRUE\nTRUE\n[a, b]\nleft:42\n", "runs string helper functions");
    require(run_capture("TRY\nPRINT missing_value\nCATCH err\nPRINT err.Message\nEND TRY\nPRINT \"after\"\n") == "undefined variable: missing_value\nafter\n", "catches runtime errors");
    require(run_capture("x = 10\nx += 5\nx -= 3\nx *= 2\nx /= 4\nPRINT x\n") == "6\n", "runs arithmetic compound assignment");
    require(run_capture("x = 6\nx &= 3\nPRINT x\nx |= 8\nPRINT x\nx ^= 2\nPRINT x\nx <<= 1\nPRINT x\nx >>= 2\nPRINT x\n") == "2\n10\n8\n16\n4\n", "runs bitwise compound assignment");
    require(run_capture("PRINT 6 & 3\nPRINT 6 | 3\nPRINT 6 ^ 3\nPRINT ~6\nPRINT 1 << 4\nPRINT 16 >> 2\n") == "2\n7\n5\n-7\n16\n4\n", "runs symbolic bitwise operators");
    require(run_capture("PRINT 6 BITAND 3\nPRINT 6 BITOR 3\nPRINT 6 BITXOR 3\nPRINT BITNOT 6\nPRINT 1 SHL 4\nPRINT 16 SHR 2\n") == "2\n7\n5\n-7\n16\n4\n", "runs word bitwise operators");
    require(run_capture("PRINT Bit.And(6, 3)\nPRINT Bit.Or(6, 3)\nPRINT Bit.Xor(6, 3)\nPRINT Bit.Not(6)\nPRINT Bit.ShiftLeft(1, 4)\nPRINT Bit.ShiftRight(16, 2)\n") == "2\n7\n5\n-7\n16\n4\n", "runs readable bit helpers");
    require(run_capture("PRINT %10101010\nPRINT 0b10101010\nPRINT &HFF\nPRINT 0xFF\n") == "170\n170\n255\n255\n", "runs binary and hex numeric literals");
    require(run_capture("PRINT SHIFT(1, 5)\nPRINT SHIFT(32, -2)\nPRINT BIT(8, 3)\nPRINT SETBIT(0, 4)\nPRINT CLEARBIT(31, 4)\nPRINT TOGGLEBIT(0, 2)\n") == "32\n8\nTRUE\n16\n15\n4\n", "runs human friendly bit helpers");
    require(run_capture("PRINT BitsToString(27)\nPRINT BitsToString(27, 8)\nPRINT BitsToBinary(3, 4)\nPRINT StringToBits(\"11011\")\nPRINT BITCOUNT(15)\nPRINT HexToString(255)\nPRINT StringToHex(\"FF\")\nPRINT BytesToHex([164, 241, 44, 157])\nPRINT HexToBytes(\"0A0B\")\n") == "11011\n00011011\n0011\n27\n4\nFF\n255\nA4F12C9D\n[10, 11]\n", "runs bit conversion helpers");
    require(run_capture("FLAGS FileAttributes\nReadOnly = SHIFT(1, 0)\nHidden = SHIFT(1, 1)\nSystem = SHIFT(1, 2)\nEND FLAGS\nattrs = 0\nattrs ADD FileAttributes.Hidden\nPRINT attrs HAS FileAttributes.Hidden\nattrs TOGGLE FileAttributes.Hidden\nPRINT attrs HAS FileAttributes.Hidden\nattrs ADD FileAttributes.System\nattrs REMOVE FileAttributes.System\nPRINT attrs\n") == "TRUE\nFALSE\n0\n", "runs flag blocks and flag operations");
    require(run_capture("#DEFINE DEBUG\n#DEFINE MASK_READ 0b0001\n#IFDEF DEBUG\nPRINT \"debug\"\n#ELSE\nPRINT \"release\"\n#ENDIF\n#IFNDEF MISSING\nPRINT MASK_READ\n#ENDIF\n#IFDEF MISSING\n#ERROR \"inactive error\"\n#ENDIF\n@EXPERIMENTAL(\"next symbol\")\nPRINT \"attr ok\"\n") == "debug\n1\nattr ok\n", "runs directives, defines, conditionals, and attributes");
    require(run_capture("x = 0\nWHILE x < 3\nx = x + 1\nPRINT x\nWEND\n") == "1\n2\n3\n", "runs WHILE loops");

    const auto import_path = std::filesystem::temp_directory_path() / "arco-import-test.abas";
    write_text(import_path, "FUNCTION ImportedValue()\nRETURN \"from import\"\nEND FUNCTION\n");
    require(run_capture("#IMPORT \"" + import_path.string() + "\"\nPRINT ImportedValue()\n") == "from import\n", "executes imported source files");
    require(run_capture("#IMPORT \"text\"\nPRINT Text.IsBlank(\"   \")\nPRINT text.join([\"a\", \"b\"], \":\")\n") == "TRUE\na:b\n", "imports stdlib modules by name and calls functions case-insensitively");

    arco::Runtime directive_runtime;
    std::ostringstream directive_output;
    directive_runtime.set_output(directive_output);
    const auto directive_result = directive_runtime.run_string("#VERSION \"1.0.0\"\n#AUTHOR \"Daedalus\"\n#DESCRIPTION \"Backup utility\"\n#TARGET windows, linux\n#REQUIRE filesystem.read\n#FEATURE unsafe\n#STRICT ON\n#EXPERIMENTAL \"API subject to change\"\n#DEPRECATED \"Use newer script\"\n#WARNING \"Legacy code path\"\n#TODO \"Replace temporary parser\"\n#NOTE \"Windows requires elevation\"\n#IMPORT \"" + import_path.string() + "\"\n#PACK 1\n#ALIGN 4\n#ENDIAN little\nPRINT \"metadata\"\n");
    require(directive_result.ok, directive_result.error);
    const auto& metadata = directive_runtime.compile_metadata();
    require(metadata.version == "1.0.0" && metadata.author == "Daedalus" && metadata.description == "Backup utility", "records directive metadata");
    require(metadata.targets.size() == 2 && metadata.requirements.size() == 1 && metadata.features.size() == 1, "records target, requirement, and feature directives");
    require(metadata.strict && metadata.experimental && metadata.deprecated, "records directive flags");
    require(metadata.warnings.size() >= 2 && metadata.todos.size() == 1 && metadata.notes.size() >= 2 && metadata.imports.size() == 1, "records warning, todo, note, and import directives");
    require(metadata.pack == "1" && metadata.align == "4" && metadata.endian == "little", "records binary layout directives");

    arco::Runtime error_runtime;
    const auto error_result = error_runtime.run_string("#ERROR \"Unsupported target\"\nPRINT \"no\"\n");
    require(!error_result.ok && error_result.error == "Unsupported target", "reports active #ERROR directives");

    arco::Runtime syntax_runtime;
    const auto syntax_result = syntax_runtime.run_string("x\n");
    require(!syntax_result.ok, "reports syntax errors");
    require(syntax_result.error.find("line 1, column 2: expected '=' after variable name") != std::string::npos, "keeps syntax error headline");
    require(syntax_result.error.find("x\n ^") != std::string::npos, "adds source line and caret to syntax errors");
    const auto lexer_result = syntax_runtime.run_string("PRINT \"unterminated\n");
    require(!lexer_result.ok, "reports lexer errors");
    require(lexer_result.error.find("unterminated string at line 1") != std::string::npos, "keeps lexer error headline");
    require(lexer_result.error.find("PRINT \"unterminated") != std::string::npos, "adds source line to lexer errors");
    const auto runtime_error_result = syntax_runtime.run_string("PRINT missing_value\n");
    require(!runtime_error_result.ok, "reports runtime errors");
    require(runtime_error_result.error.find("undefined variable: missing_value") != std::string::npos, "keeps runtime error headline");
    require(runtime_error_result.error.find("runtime error at line 1, column 1") != std::string::npos, "adds runtime source location");
    require(runtime_error_result.error.find("PRINT missing_value\n^") != std::string::npos, "adds source line and caret to runtime errors");

    arco::Runtime shell_runtime;
    arco::shell::set_color_enabled(false);
    arco::shell::register_shell_builtins(shell_runtime);
    std::ostringstream shell_output;
    shell_runtime.set_output(shell_output);
    const auto shell_result = shell_runtime.run_string("FUNCTION Shout(text)\nRETURN Upper(text)\nEND FUNCTION\nPRINT Shout(\"shell\")\nresult = RUN(\"printf shell-test\")\nPRINT result.Output\nPRINT result.ExitCode\nPRINT host.osname()\nPRINT LEN(process.list()) > 0\nPRINT Process.Exists(\"definitely-not-a-real-arcosh-test-process\")\nPRINT file.exists(\"../readme.md\")\nFOR file IN File.Find(\"../*.md\")\nPRINT file CONTAINS \"readme\"\nNEXT\nPRINT help.topic(\"run\") CONTAINS \"ExitCode\"\nPRINT Help.Topic(\"if\") CONTAINS \"END IF\"\nFOR topic IN Help.Topics()\nPRINT topic CONTAINS \"basic\"\nNEXT\nPRINT arcosh.setprompt(\"{shell}:{cwd:short}:{status}> \")\nPRINT ArcoSH.GetPrompt()\nPRINT color.green(\"ok\")\nPRINT Color.Paint(\"warn\", \"yellow\")\n");
    require(shell_result.ok, shell_result.error);
    require(shell_output.str().find("SHELL") != std::string::npos, "runs user functions in shell runtime");
    require(shell_output.str().find("shell-test") != std::string::npos, "returns RUN output object");
    require(shell_output.str().find("{shell}:{cwd:short}:{status}> ") != std::string::npos, "configures shell prompt from ArcoBASIC");
    require(shell_output.str().find("ok") != std::string::npos, "prints uncolored color helper output when color is disabled");
    require(shell_output.str().find("TRUE") != std::string::npos, "registers shell builtins");

    const auto helper_dir = std::filesystem::temp_directory_path() / "arcosh-helper-tests";
    std::filesystem::remove_all(helper_dir);
    arco::Runtime helper_runtime;
    arco::shell::register_shell_builtins(helper_runtime);
    std::ostringstream helper_output;
    helper_runtime.set_output(helper_output);
    const auto helper_result = helper_runtime.run_string(
        "Directory.Create(\"" + helper_dir.string() + "\")\n"
        "path = Path.Join(\"" + helper_dir.string() + "\", \"sample.txt\")\n"
        "File.WriteText(path, $\"one\\n\")\n"
        "File.AppendText(path, $\"two {Path.BaseName(path)}\\n\")\n"
        "PRINT File.ReadText(path)\n"
        "PRINT Directory.Exists(\"" + helper_dir.string() + "\")\n"
        "PRINT Path.BaseName(path)\n"
        "PRINT Path.DirName(path)\n"
        "PRINT Path.Extension(path)\n");
    require(helper_result.ok, helper_result.error);
    require(helper_output.str().find("one\ntwo sample.txt") != std::string::npos, "writes and appends interpolated files from shell helpers");
    require(helper_output.str().find("sample.txt") != std::string::npos, "runs path helper functions");

    arco::Runtime exit_runtime;
    arco::shell::register_shell_builtins(exit_runtime);
    std::ostringstream exit_output;
    exit_runtime.set_output(exit_output);
    const auto exit_result = exit_runtime.run_string("x = 10\nIF x == 10 THEN ExitTheProgram(7)\nPRINT \"after\"\n");
    require(exit_result.ok && exit_result.exited && exit_result.exit_code == 7, "exits from single-line IF");
    require(exit_output.str().find("after") == std::string::npos, "stops execution after exit");

    arco::Runtime goto_exit_runtime;
    arco::shell::register_shell_builtins(goto_exit_runtime);
    std::ostringstream goto_exit_output;
    goto_exit_runtime.set_output(goto_exit_output);
    const auto goto_exit_result = goto_exit_runtime.run_string("10 x = 0\n20 PRINT x\n30 x += 1\n40 IF x >= 3 THEN ExitTheProgram(1)\n50 GOTO 20\n");
    require(goto_exit_result.ok && goto_exit_result.exited && goto_exit_result.exit_code == 1, "supports GOTO loops ending with ExitTheProgram");
    require(goto_exit_output.str() == "0\n1\n2\n", "prints values before GOTO loop exit");

    arco::shell::set_color_enabled(true);
    require(arco::shell::colorize("ok", "green").find("\033[32m") != std::string::npos, "emits ANSI colors when enabled");

    arco::Runtime color_runtime;
    arco::shell::register_shell_builtins(color_runtime);
    std::ostringstream color_output;
    color_runtime.set_output(color_output);
    const auto color_result = color_runtime.run_string("result = RUN(\"grep ArcoBASIC ../CMakeLists.txt\")\nPRINT result.Output\n");
    require(color_result.ok, color_result.error);
    require(color_output.str().find("\033[") != std::string::npos, "forces colors for external grep");

    arco::shell::set_color_enabled(false);
    arco::Runtime plain_runtime;
    arco::shell::register_shell_builtins(plain_runtime);
    std::ostringstream plain_output;
    plain_runtime.set_output(plain_output);
    const auto plain_result = plain_runtime.run_string("result = RUN(\"grep ArcoBASIC ../CMakeLists.txt\")\nPRINT result.Output\n");
    require(plain_result.ok, plain_result.error);
    require(plain_output.str().find("\033[") == std::string::npos, "keeps external command output plain when color disabled");

    std::ostringstream repl_output;
    std::istringstream repl_input("HELP run\nHELP if\nHELP bitwise\nHELP try\nHELP tutorial\nHELP jobs\nHELP stdlib\nHELP doctor\nCOLOR ON\nCOLOR OFF\nprintf bare-shell\nprinf recovered\noops printf\nEXIT\n");
    arco::shell::repl(shell_runtime, repl_input, repl_output, false);
    require(repl_output.str().find("RUN command helper") != std::string::npos, "prints REPL help topics");
    require(repl_output.str().find("IF statement") != std::string::npos, "prints syntax help topics");
    require(repl_output.str().find("Bitwise operations") != std::string::npos, "prints bitwise help topic");
    require(repl_output.str().find("TRY / CATCH") != std::string::npos, "prints try help topic");
    require(repl_output.str().find("Interactive tutorial") != std::string::npos, "prints tutorial help topic");
    require(repl_output.str().find("Background jobs") != std::string::npos, "prints jobs help topic");
    require(repl_output.str().find("Standard library modules") != std::string::npos, "prints stdlib help topic");
    require(repl_output.str().find("ArcoSH doctor") != std::string::npos, "prints doctor help topic");
    require(repl_output.str().find("bare-shell") != std::string::npos, "runs bare shell commands in REPL");
    require(repl_output.str().find("Unknown command") != std::string::npos, "reports unknown commands in REPL");
    require(repl_output.str().find("recovered") != std::string::npos, "recovers unknown commands with oops");

    std::ostringstream tutorial_output;
    std::istringstream tutorial_input(
        "\n"
        "PRINT \"hello admin\"\n"
        "printf service-ok\n"
        "result = RUN(\"printf captured\") : PRINT result.Output : PRINT result.ExitCode\n"
        "PRINT File.Exists(\"readme.md\")\n"
        "PRINT Host.OSName()\n"
        "\n"
        "\n"
        "\n");
    shell_runtime.set_output(tutorial_output);
    const auto tutorial_result = arco::shell::run_tutorial(shell_runtime, tutorial_input, tutorial_output, "");
    require(tutorial_result.ok, tutorial_result.error);
    require(std::filesystem::exists("../tutorials/arcosh_sysadmin.abas") || std::filesystem::exists("tutorials/arcosh_sysadmin.abas"), "ships tutorial as a viewable ArcoBASIC file");
    require(tutorial_output.str().find("ArcoSH sysadmin tutorial") != std::string::npos, "runs tutorial as ArcoBASIC");
    require(tutorial_output.str().find("hello admin") != std::string::npos, "tutorial executes ArcoBASIC practice");
    require(tutorial_output.str().find("service-ok") != std::string::npos, "tutorial executes shell practice");
    require(tutorial_output.str().find("Tutorial complete") != std::string::npos, "tutorial reaches completion");

    std::ostringstream stopped_tutorial_output;
    std::istringstream stopped_tutorial_input(
        "\n"
        "PRINT \"hello admin\"\n"
        "printf service-ok\n"
        "result = RUN(\"printf captured\") : PRINT result.Output : PRINT result.ExitCode\n"
        "PRINT File.Exists(\"readme.md\")\n"
        "PRINT Host.OSName()\n");
    shell_runtime.set_output(stopped_tutorial_output);
    const auto stopped_tutorial_result = arco::shell::run_tutorial(shell_runtime, stopped_tutorial_input, stopped_tutorial_output, "");
    require(stopped_tutorial_result.ok, stopped_tutorial_result.error);
    require(stopped_tutorial_output.str().find("Tutorial stopped.") != std::string::npos, "tutorial stops on input EOF instead of autofilling");
    require(stopped_tutorial_output.str().find("Tutorial complete") == std::string::npos, "tutorial does not complete after EOF");

    std::ostringstream shell_state_output;
    setenv("ARCOSH_EXPAND_VALUE", "expanded-ok", 1);
    std::istringstream shell_state_input("pwd\ncd ..\npwd\ncd -\npwd\nexport ARCOSH_TEST_VALUE=$ARCOSH_EXPAND_VALUE\nPRINT ENV(\"ARCOSH_TEST_VALUE\")\nprintf ${ARCOSH_TEST_VALUE}\nARCOSH_TEMP_PREFIX=prefix-ok sh -c 'printf $ARCOSH_TEMP_PREFIX'\nenv\nfalse\nprintf status-$?\nunset ARCOSH_TEST_VALUE\nPRINT ENV(\"ARCOSH_TEST_VALUE\")\nPRINT \"$ARCOSH_TEST_VALUE\"\nEXIT\n");
    shell_runtime.set_output(shell_state_output);
    arco::shell::repl(shell_runtime, shell_state_input, shell_state_output, false);
    require(shell_state_output.str().find("expanded-ok\n") != std::string::npos, "persists exported environment variables with expansion");
    require(shell_state_output.str().find("prefix-ok") != std::string::npos, "runs shell commands with assignment prefixes");
    require(shell_state_output.str().find("ARCOSH_TEST_VALUE=expanded-ok") != std::string::npos, "lists environment with env builtin");
    require(shell_state_output.str().find("status-1") != std::string::npos, "expands last status with $?");
    require(shell_state_output.str().find("$ARCOSH_TEST_VALUE") != std::string::npos, "does not expand variables inside ArcoBASIC source lines");

    const auto redirection_file = std::filesystem::temp_directory_path() / "arcosh-redirection-test.txt";
    std::filesystem::remove(redirection_file);
    std::ostringstream shell_pipeline_output;
    std::istringstream shell_pipeline_input(
        "printf pipe-ok | grep pipe\n"
        "printf redir-ok > " + redirection_file.string() + "\n"
        "cat < " + redirection_file.string() + "\n"
        "sleep 1 &\n"
        "jobs\n"
        "fg\n"
        "jobs -c\n"
        "sleep 5 &\n"
        "jobs\n"
        "kill\n"
        "jobs\n"
        "jobs -c\n"
        "true &\n"
        "disown\n"
        "jobs\n"
        "EXIT\n");
    shell_runtime.set_output(shell_pipeline_output);
    arco::shell::repl(shell_runtime, shell_pipeline_input, shell_pipeline_output, false);
    require(shell_pipeline_output.str().find("pipe-ok") != std::string::npos, "runs bare command pipelines");
    require(shell_pipeline_output.str().find("redir-ok") != std::string::npos, "runs bare command redirection");
    require(shell_pipeline_output.str().find("[") != std::string::npos && shell_pipeline_output.str().find("sleep 1") != std::string::npos, "tracks background jobs");
    require(shell_pipeline_output.str().find("Sent signal") != std::string::npos, "terminates background jobs");
    require(shell_pipeline_output.str().find("disowned") != std::string::npos, "disowns background jobs");
    require(read_text(redirection_file).find("redir-ok") != std::string::npos, "writes redirected command output");

    arco::Runtime job_runtime;
    arco::shell::register_shell_builtins(job_runtime);
    std::ostringstream job_output;
    job_runtime.set_output(job_output);
    const auto job_result = job_runtime.run_string(
        "job = ArcoSH.StartJob(\"true\")\n"
        "PRINT job.Running\n"
        "PRINT LEN(ArcoSH.Jobs()) > 0\n"
        "PRINT ArcoSH.WaitJob(job.Id)\n"
        "job2 = ArcoSH.StartJob(\"sleep 5\")\n"
        "PRINT ArcoSH.KillJob(job2.Id)\n"
        "PRINT ArcoSH.WaitJob(job2.Id) >= 128\n");
    require(job_result.ok, job_result.error);
    require(job_output.str() == "TRUE\nTRUE\n0\nTRUE\nTRUE\n", "runs script-level ArcoSH job helpers");

    const auto stdlib_file = std::filesystem::temp_directory_path() / "arcosh-stdlib-test.txt";
    std::filesystem::remove(stdlib_file);
    arco::Runtime stdlib_runtime;
    arco::shell::register_shell_builtins(stdlib_runtime);
    std::ostringstream stdlib_output;
    stdlib_runtime.set_output(stdlib_output);
    const auto stdlib_result = stdlib_runtime.run_string(
        "#IMPORT \"sysadmin\"\n"
        "PRINT Shell.Output(\"printf stdlib\")\n"
        "PRINT SysAdmin.CommandExists(\"printf\")\n"
        "Files.WriteLines(\"" + stdlib_file.string() + "\", [\"one\", \"two\"])\n"
        "Files.AppendLine(\"" + stdlib_file.string() + "\", \"three\")\n"
        "PRINT Array.Join(Files.ReadLines(\"" + stdlib_file.string() + "\"), \":\")\n"
        "SysAdmin.AppendLog(\"" + stdlib_file.string() + "\", \"logged\")\n"
        "PRINT File.ReadText(\"" + stdlib_file.string() + "\") CONTAINS \"logged\"\n");
    require(stdlib_result.ok, stdlib_result.error);
    require(stdlib_output.str() == "stdlib\nTRUE\none:twothree\nTRUE\n", "runs stdlib shell/sysadmin modules");

    const auto profile_home = std::filesystem::temp_directory_path() / "arcosh-test-profile";
    std::filesystem::remove_all(profile_home);
    std::filesystem::create_directories(profile_home / "plugins");
    std::filesystem::create_directories(profile_home / "scripts");
    write_text(profile_home / "rc.abas", "PRINT \"rc loaded\"\nprofile_value = \"from rc\"\n");
    write_text(profile_home / "plugins" / "10-plugin.abas", "PRINT \"plugin loaded\"\nplugin_value = \"from plugin\"\n");
    write_text(profile_home / "scripts" / "admin-status.abas", "PRINT profile_value\nPRINT plugin_value\nFOR arg IN Args\nPRINT arg\nNEXT\n");
    write_text(profile_home / "set-helper.abas", "helper_value = Args[0]\n");
    setenv("ARCOSH_HOME", profile_home.string().c_str(), 1);

    arco::Runtime profile_runtime;
    arco::shell::register_shell_builtins(profile_runtime);
    std::ostringstream profile_output;
    profile_runtime.set_output(profile_output);
    const auto startup_result = arco::shell::load_startup(profile_runtime, profile_output, false);
    require(startup_result.ok, startup_result.error);
    require(std::filesystem::exists(profile_home / "plugins") && std::filesystem::exists(profile_home / "scripts"), "creates ArcoSH profile directories");
    require(profile_output.str().find("rc loaded") != std::string::npos, "loads rc from ARCOSH_HOME");
    require(profile_output.str().find("plugin loaded") != std::string::npos, "loads plugins from ARCOSH_HOME");

    std::istringstream profile_repl_input("PRINT ArcoSH.Home()\ncomplete he\ncomplete help ed\ncomplete admin\nadmin-status first second\nsource " + (profile_home / "set-helper.abas").string() + " sourced\nPRINT helper_value\nalias hi=printf alias-ok\ntype hi\nwhich admin-status\nhi\nunalias hi\nEXIT\n");
    arco::shell::repl(profile_runtime, profile_repl_input, profile_output, false);
    require(profile_output.str().find(profile_home.string()) != std::string::npos, "exposes ArcoSH profile paths to scripts");
    require(profile_output.str().find("help") != std::string::npos, "completes shell builtins");
    require(profile_output.str().find("editing") != std::string::npos, "completes help topics");
    require(profile_output.str().find("admin-status") != std::string::npos, "completes profile scripts");
    require(profile_output.str().find("from rc") != std::string::npos && profile_output.str().find("from plugin") != std::string::npos, "runs reusable scripts from profile scripts directory");
    require(profile_output.str().find("first") != std::string::npos && profile_output.str().find("second") != std::string::npos, "passes Args to reusable scripts");
    require(profile_output.str().find("sourced") != std::string::npos, "sources scripts into current runtime with args");
    require(profile_output.str().find("is an alias") != std::string::npos, "describes aliases with type");
    require(profile_output.str().find((profile_home / "scripts" / "admin-status.abas").string()) != std::string::npos, "finds profile scripts with which");
    require(profile_output.str().find("alias-ok") != std::string::npos, "runs shell aliases");

    const auto init_home = std::filesystem::temp_directory_path() / "arcosh-init-profile";
    std::filesystem::remove_all(init_home);
    setenv("ARCOSH_HOME", init_home.string().c_str(), 1);
    std::ostringstream init_output;
    const auto init_result = arco::shell::init_profile(init_output);
    require(init_result.ok, init_result.error);
    require(std::filesystem::exists(init_home / "rc.abas"), "initializes default rc profile");
    require(std::filesystem::exists(init_home / "scripts" / "hello.abas"), "initializes default scripts directory");
    require(std::filesystem::exists(init_home / "scripts" / "sysinfo.abas"), "initializes sysinfo profile script");

    std::ostringstream doctor_output;
    const int doctor_code = arco::shell::doctor(doctor_output);
    require(doctor_code == 0, "doctor exits successfully when stdlib is available");
    require(doctor_output.str().find("ArcoSH doctor") != std::string::npos, "doctor prints header");
    require(doctor_output.str().find("stdlib import") != std::string::npos, "doctor checks stdlib imports");

    const auto cli_rc = std::filesystem::temp_directory_path() / "arcosh-cli-rc.abas";
    const auto cli_out = std::filesystem::temp_directory_path() / "arcosh-cli-out.txt";
    const auto cli_err = std::filesystem::temp_directory_path() / "arcosh-cli-err.txt";
    write_text(cli_rc, "PRINT \"explicit rc loaded\"\n");
    std::filesystem::remove(cli_out);
    std::filesystem::remove(cli_err);
    const std::string cli_command = "./arcosh --safe --rc " + cli_rc.string() + " -c \"printf cli-ok\" > " + cli_out.string() + " 2> " + cli_err.string();
    require(std::system(cli_command.c_str()) == 0, "runs arcosh with --safe and explicit --rc");
    const std::string cli_text = read_text(cli_out);
    require(cli_text.find("explicit rc loaded") != std::string::npos && cli_text.find("cli-ok") != std::string::npos, "loads explicit rc and command in CLI safe mode");

    const auto doctor_out = std::filesystem::temp_directory_path() / "arcosh-doctor-out.txt";
    const auto doctor_err = std::filesystem::temp_directory_path() / "arcosh-doctor-err.txt";
    std::filesystem::remove(doctor_out);
    std::filesystem::remove(doctor_err);
    const std::string doctor_command = "ARCOSH_HOME=" + init_home.string() + " ./arcosh --doctor > " + doctor_out.string() + " 2> " + doctor_err.string();
    require(std::system(doctor_command.c_str()) == 0, "runs arcosh --doctor");
    require(read_text(doctor_out).find("ArcoSH doctor") != std::string::npos, "CLI doctor prints report");

    std::ostringstream history_output;
    std::istringstream history_input("PRINT \"hist-one\"\nhistory\nhistory clear\nhistory\nEXIT\n");
    profile_runtime.set_output(history_output);
    arco::shell::repl(profile_runtime, history_input, history_output, false);
    require(history_output.str().find("PRINT \"hist-one\"") != std::string::npos, "lists REPL command history");
    require(history_output.str().find("History cleared") != std::string::npos, "clears REPL command history");

    std::ostringstream history_save_output;
    std::istringstream history_save_input("PRINT \"persisted-history\"\nEXIT\n");
    profile_runtime.set_output(history_save_output);
    arco::shell::repl(profile_runtime, history_save_input, history_save_output, false);
    require(std::filesystem::exists(profile_home / "history"), "writes persistent history file");
    require(std::filesystem::file_size(profile_home / "history") > 0, "persistent history file is not empty");

    std::ostringstream numbered_repl_output;
    std::istringstream numbered_repl_input("10 x = 0\n20 WHILE x < 2\n30 x = x + 1\n40 PRINT x\n50 WEND\nLIST\nRUN\nNEW\nLIST\nEXIT\n");
    shell_runtime.set_output(numbered_repl_output);
    arco::shell::repl(shell_runtime, numbered_repl_input, numbered_repl_output, false);
    require(numbered_repl_output.str().find("10 x = 0") != std::string::npos, "lists line-numbered REPL programs");
    require(numbered_repl_output.str().find("\n1\n") != std::string::npos && numbered_repl_output.str().find("\n2\n") != std::string::npos, "runs line-numbered REPL programs");

    std::ostringstream goto_repl_output;
    std::istringstream goto_repl_input("10 x = 0\n20 PRINT x\n30 x += 1\n40 IF x >= 3 THEN STOP\n50 GOTO 20\nRUN\nPRINT \"still here\"\nEXIT\n");
    shell_runtime.set_output(goto_repl_output);
    const int goto_repl_code = arco::shell::repl(shell_runtime, goto_repl_input, goto_repl_output, false);
    require(goto_repl_code == 0, "STOP returns control to REPL");
    require(goto_repl_output.str() == "0\n1\n2\nstill here\n", "runs numbered GOTO loop and continues after STOP");

    std::ostringstream multiline_repl_output;
    std::istringstream multiline_repl_input(
        "TRY\n"
        "PRINT missing_value\n"
        "CATCH err\n"
        "PRINT err.Message\n"
        "END TRY\n"
        "FUNCTION Twice(x)\n"
        "RETURN x * 2\n"
        "END FUNCTION\n"
        "PRINT Twice(4)\n"
        "IF Twice(2) == 4 THEN\n"
        "PRINT \"block if\"\n"
        "END IF\n"
        "EXIT\n");
    shell_runtime.set_output(multiline_repl_output);
    arco::shell::repl(shell_runtime, multiline_repl_input, multiline_repl_output, false);
    require(multiline_repl_output.str().find("undefined variable: missing_value") != std::string::npos, "runs unnumbered multiline TRY blocks in REPL");
    require(multiline_repl_output.str().find("\n8\n") != std::string::npos, "runs unnumbered multiline FUNCTION blocks in REPL");
    require(multiline_repl_output.str().find("block if") != std::string::npos, "runs unnumbered multiline IF blocks in REPL");

    arco::Runtime limited;
    limited.set_limits({2});
    const auto limit_result = limited.run_string("WHILE TRUE\nPRINT 1\nWEND\n");
    require(!limit_result.ok, "enforces instruction limit");

    ArcoRuntime* c_runtime = arco_create_runtime();
    require(c_runtime != nullptr, "creates C runtime");
    require(arco_run_string(c_runtime, "PRINT \"C API\"\n") == 0, "runs through C API");
    arco_destroy_runtime(c_runtime);

    return 0;
}
