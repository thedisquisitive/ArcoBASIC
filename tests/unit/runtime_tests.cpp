#include "arco/runtime.hpp"
#include "arco/runtime_handles.hpp"
#include "arco/c/arco_c_api.h"

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

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

#ifndef _WIN32
struct TcpTestServer {
    int port = 0;
    std::thread thread;
};

TcpTestServer start_tcp_test_server() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    require(server_fd >= 0, "creates TCP test socket");
    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    require(bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "binds TCP test socket");
    require(listen(server_fd, 1) == 0, "listens on TCP test socket");

    socklen_t length = sizeof(address);
    require(getsockname(server_fd, reinterpret_cast<sockaddr*>(&address), &length) == 0, "reads TCP test socket port");

    TcpTestServer server;
    server.port = ntohs(address.sin_port);
    server.thread = std::thread([server_fd] {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd >= 0) {
            char buffer[128]{};
            recv(client_fd, buffer, sizeof(buffer), 0);
            const char response[] = "tcp-ok";
            send(client_fd, response, sizeof(response) - 1, 0);
            close(client_fd);
        }
        close(server_fd);
    });
    return server;
}

int free_loopback_port() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    require(server_fd >= 0, "creates free-port probe socket");
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    require(bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "binds free-port probe socket");
    socklen_t length = sizeof(address);
    require(getsockname(server_fd, reinterpret_cast<sockaddr*>(&address), &length) == 0, "reads free-port probe socket port");
    const int port = ntohs(address.sin_port);
    close(server_fd);
    return port;
}

std::string http_get_loopback(int port, const std::string& target) {
    int fd = -1;
    for (int attempt = 0; attempt < 50; ++attempt) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        require(fd >= 0, "creates HTTP test client socket");
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<uint16_t>(port));
        if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
            break;
        }
        close(fd);
        fd = -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    require(fd >= 0, "connects HTTP test client socket");
    const std::string request = "GET " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    send(fd, request.data(), request.size(), 0);
    std::string response;
    char buffer[1024]{};
    while (true) {
        const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            break;
        }
        response.append(buffer, static_cast<std::size_t>(count));
    }
    close(fd);
    return response;
}
#endif

} // namespace

int main() {
    {
        arco::RuntimeHandleTable handles;
        auto object = std::make_shared<int>(42);
        const auto surface = handles.create("SURFACE", object);
        const arco::Value value(surface);
        require(value.is_handle(), "runtime object handles are opaque Value variants");
        require(handles.valid(value.as_handle(), "SURFACE"), "fresh handle validates by type and generation");
        arco::Runtime runtime;
        require(runtime.value_matches_type(value, "SURFACE"), "runtime accepts a matching opaque handle type");
        require(runtime.value_matches_type(arco::Value(nullptr), "SURFACE"), "typed runtime objects accept their null value");
        require(!runtime.value_matches_type(value, "IMAGE"), "runtime rejects a handle used as another object type");
        const auto copied = value;
        require(arco::values_equal(value, copied), "handle assignment preserves object identity");
        require(handles.destroy(surface), "destroying an owned handle succeeds");
        require(!handles.valid(surface), "destroyed handles fail validation");
        require(!handles.destroy(surface), "destroying an already-destroyed handle fails safely");
    }
    require(run_capture("PRINT \"HELLO\"\n") == "HELLO\n", "prints strings");
    require(run_capture("PRINT \"a\\nb\"\nPRINT \"col\\tvalue\"\nPRINT \"quote: \\\"ok\\\"\"\nPRINT \"slash: \\\\\"\n") == "a\nb\ncol\tvalue\nquote: \"ok\"\nslash: \\\n", "decodes string escape sequences");
    require(run_capture("name = \"Ada\"\nparts = 3\nPRINT $\"{name} has {parts} parts\"\nPRINT $\"math {1 + 2}\"\nPRINT $\"date {LEN(DATE()) > 0}\"\nPRINT $\"literal {{braces}}\"\n") == "Ada has 3 parts\nmath 3\ndate TRUE\nliteral {braces}\n", "interpolates strings with expressions");
    require(run_capture("10 x = 0\n20 WHILE x < 3\n30 x = x + 1\n40 PRINT x\n50 WEND\n") == "1\n2\n3\n", "runs line-numbered scripts");
    require(run_capture("10 x = 0\n20 PRINT x\n30 x += 1\n40 IF x >= 3 THEN GOTO 60\n50 GOTO 20\n60 PRINT \"done\"\n") == "0\n1\n2\ndone\n", "runs classic GOTO line-number loops");
    require(run_capture("10 PRINT \"before\"\n20 STOP\n30 PRINT \"after\"\n") == "before\n", "stops current program with STOP");
    require(run_capture("LET x = 2 + 3 * 4\nPRINT x\n") == "14\n", "evaluates arithmetic");
    require(run_capture("PRINT 10 MOD 3\nPRINT 10 % 3\nPRINT 10 + 7 % 4 * 2\nPRINT 10.5 % 4\n") == "1\n1\n16\n2.5\n", "evaluates BASIC MOD and symbolic modulo expressions");
    require(run_capture("mod = 4\nPRINT mod\nFOR mod IN [1, 2]\nPRINT mod\nNEXT\n") == "4\n1\n2\n", "keeps mod usable as an identifier");
    require(run_capture("PRINT FALSE ANDALSO missing.Value\nPRINT TRUE ORELSE missing.Value\nPRINT FALSE && missing.Value\nPRINT TRUE || missing.Value\nPRINT TRUE && FALSE || TRUE\nPRINT TRUE && (FALSE || FALSE)\n") == "FALSE\nTRUE\nFALSE\nTRUE\nTRUE\nFALSE\n", "runs short-circuit boolean operators");
    require(run_capture("PRINT !TRUE\nPRINT !FALSE\nPRINT !0\nPRINT !\"\"\nPRINT !!\"ok\"\nPRINT 1 != 2\nPRINT NOT 6\n") == "FALSE\nTRUE\nTRUE\nTRUE\nTRUE\nTRUE\n-7\n", "runs symbolic boolean not and preserves NOT bitwise behavior");
    require(run_capture("PRINT 6 AND 3\nPRINT 6 OR 3\n") == "2\n7\n", "keeps AND and OR as bitwise operators");
    require(run_capture("x = 10\nIF x == 10 THEN PRINT \"TEN\"\nPRINT \"DONE\"\n") == "TEN\nDONE\n", "runs single-line IF with double equals");
    require(run_capture("x = 1 : y = 2 : PRINT x + y\nIF y == 2 THEN PRINT \"two\" : PRINT \"again\"\n") == "3\ntwo\nagain\n", "runs colon-separated statements");
    require(run_capture("guess = 4 : IF guess < 7 THEN PRINT \"too low\" ELSE PRINT \"not low\"\nguess = 8 : IF guess < 7 THEN PRINT \"too low\" ELSE PRINT \"not low\" : PRINT \"again\"\n") == "too low\nnot low\nagain\n", "runs single-line IF ELSE statements");
    require(run_capture("choice = 2\nSELECT CASE choice\nCASE 1\nPRINT \"one\"\nCASE 2, 3\nPRINT \"two or three\"\nCASE ELSE\nPRINT \"other\"\nEND SELECT\n") == "two or three\n", "runs SELECT CASE with multiple case values");
    require(run_capture("name = \"Ada\"\nSELECT CASE name\nCASE \"Grace\": PRINT \"compiler\"\nCASE \"Ada\": PRINT \"countess\"\nCASE ELSE: PRINT \"unknown\"\nEND SELECT\n") == "countess\n", "runs SELECT CASE with strings and colon-separated statements");
    require(run_capture("score = 87\nSELECT CASE score\nCASE 90 TO 100\nPRINT \"A\"\nCASE 80 TO 89\nPRINT \"B\"\nCASE ELSE\nPRINT \"again\"\nEND SELECT\nscore = 3\nSELECT CASE score\nCASE 5 TO 1\nPRINT \"reverse\"\nEND SELECT\n") == "B\nreverse\n", "runs SELECT CASE numeric ranges");
    require(run_capture("10 choice = 9\n20 SELECT CASE choice\n30 CASE 1\n40 PRINT \"one\"\n50 CASE ELSE\n60 PRINT \"other\"\n70 END SELECT\n") == "other\n", "runs line-numbered SELECT CASE blocks");
    require(run_capture("FUNCTION Sum(a, b)\nRETURN a + b\nEND FUNCTION\nPRINT Sum(2, 3)\n") == "5\n", "runs user functions");
    require(run_capture("FUNCTION Greet(name, punctuation = \"!\")\nRETURN $\"Hello {name}{punctuation}\"\nEND FUNCTION\nPRINT Greet(\"Ada\")\nPRINT Greet(\"Grace\", \"?\")\n") == "Hello Ada!\nHello Grace?\n", "runs function default parameters");
    require(run_capture("FUNCTION AddOffset(x, offset = x + 1)\nRETURN x + offset\nEND FUNCTION\nPRINT AddOffset(4)\n") == "9\n", "evaluates function defaults in call scope");
    require(run_capture("x = 10\nFUNCTION Change(x)\nx += 5\nRETURN x\nEND FUNCTION\nPRINT Change(1)\nPRINT x\n") == "6\n10\n", "keeps function parameters local");
    require(run_capture("FUNCTION PersonName(person)\nRETURN person.Name\nEND FUNCTION\nPRINT PersonName({\"Name\": \"Ada\"})\n") == "Ada\n", "reads object parameters in functions");
    require(run_capture("FOR i = 1 TO 6\nIF i == 2 THEN CONTINUE FOR\nIF i == 5 THEN EXIT FOR\nPRINT i\nNEXT\n") == "1\n3\n4\n", "runs EXIT FOR and CONTINUE FOR in numeric loops");
    require(run_capture("FOR item IN [1, 2, 3, 4]\nIF item == 2 THEN CONTINUE FOR\nIF item == 4 THEN EXIT FOR\nPRINT item\nNEXT\n") == "1\n3\n", "runs EXIT FOR and CONTINUE FOR in collection loops");
    require(run_capture("x = 0\nWHILE x < 5\nx += 1\nIF x == 2 THEN CONTINUE WHILE\nIF x == 4 THEN EXIT WHILE\nPRINT x\nWEND\nPRINT \"done\"\n") == "1\n3\ndone\n", "runs EXIT WHILE and CONTINUE WHILE");
    require(run_capture("FOR i = 1 TO 2\nx = 0\nWHILE x < 3\nx += 1\nIF x == 2 THEN EXIT WHILE\nPRINT STRING(i) + \":\" + STRING(x)\nWEND\nNEXT\n") == "1:1\n2:1\n", "targets EXIT WHILE to the nearest WHILE inside FOR");
    require(run_capture("x = 0\nDO WHILE x < 3\nx += 1\nPRINT x\nLOOP\n") == "1\n2\n3\n", "runs DO WHILE loops");
    require(run_capture("x = 0\nDO UNTIL x == 3\nx += 1\nPRINT x\nLOOP\n") == "1\n2\n3\n", "runs DO UNTIL loops");
    require(run_capture("x = 0\nDO\nx += 1\nPRINT x\nLOOP WHILE x < 3\n") == "1\n2\n3\n", "runs LOOP WHILE post-test loops");
    require(run_capture("x = 0\nDO\nx += 1\nPRINT x\nLOOP UNTIL x == 3\n") == "1\n2\n3\n", "runs LOOP UNTIL post-test loops");
    require(run_capture("x = 0\nDO\nx += 1\nIF x == 2 THEN CONTINUE DO\nIF x == 5 THEN EXIT DO\nPRINT x\nLOOP\nPRINT \"done\"\n") == "1\n3\n4\ndone\n", "runs EXIT DO and CONTINUE DO");
    require(run_capture("10 x = 0\n20 DO\n30 x += 1\n40 PRINT x\n50 LOOP UNTIL x == 2\n") == "1\n2\n", "runs line-numbered DO LOOP blocks");
    require(run_capture("FUNCTION Continue()\nRETURN \"callable\"\nEND FUNCTION\nPRINT Continue()\n") == "callable\n", "keeps Continue usable as a function name");
    require(run_capture(
        "CLASS Person\n"
        "Name = \"Ada\"\n"
        "Age = 36\n"
        "FUNCTION Label()\n"
        "RETURN SELF.Name + \":\" + STRING(SELF.Age)\n"
        "END FUNCTION\n"
        "FUNCTION Rename(nextName)\n"
        "SELF.Name = nextName\n"
        "RETURN SELF.Name\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "person = Person()\n"
        "PRINT person.Name\n"
        "PRINT person.Label()\n"
        "PRINT person.Rename(\"Grace\")\n"
        "PRINT person.Label()\n"
        "PRINT String.Trim(\" ok \")\n") == "Ada\nAda:36\nGrace\nGrace:36\nok\n", "runs class fields and methods");
    require(run_capture(
        "10 CLASS Person\n"
        "20 Name AS String = \"Ada\"\n"
        "30 Age AS Number =36\n"
        "40 FUNCTION Label() AS String\n"
        "50 RETURN SELF.Name + \":\" + STRING(SELF.Age)\n"
        "60 END FUNCTION\n"
        "70 END CLASS\n"
        "80 person = Person()\n"
        "90 PRINT person.Name\n"
        "100 PRINT person.Label()\n") == "Ada\nAda:36\n", "runs numbered class blocks with typed fields and methods");
    require(run_capture(
        "CLASS Counter\n"
        "Value = 0\n"
        "CONSTRUCTOR(start)\n"
        "SELF.Value = start\n"
        "END CONSTRUCTOR\n"
        "FUNCTION Increment(amount = 1)\n"
        "SELF.Value = SELF.Value + amount\n"
        "RETURN SELF.Value\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "counter = Counter(10)\n"
        "PRINT counter.Value\n"
        "PRINT counter.Increment()\n"
        "PRINT counter.Increment(5)\n") == "10\n11\n16\n", "runs class CONSTRUCTOR and method defaults");
    require(run_capture(
        "CLASS Stamp\n"
        "Value = \"unset\"\n"
        "CONSTRUCTOR()\n"
        "SELF.Value = \"ready\"\n"
        "END CONSTRUCTOR\n"
        "END CLASS\n"
        "stamp = Stamp()\n"
        "PRINT stamp.Value\n") == "ready\n", "runs zero-argument class CONSTRUCTOR");
    require(run_capture(
        "CLASS Animal\n"
        "Name = \"unknown\"\n"
        "SHARED Kingdom = \"animalia\"\n"
        "FUNCTION Init(name)\n"
        "SELF.Name = name\n"
        "END FUNCTION\n"
        "FUNCTION Speak()\n"
        "RETURN SELF.Name + \" makes a sound\"\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "CLASS Cat EXTENDS Animal\n"
        "Lives = 9\n"
        "FUNCTION Speak()\n"
        "RETURN SUPER.Speak() + \" and meows\"\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "cat = Cat(\"Miso\")\n"
        "PRINT cat.Name\n"
        "PRINT cat.Lives\n"
        "PRINT cat.Speak()\n"
        "PRINT Cat.Kingdom\n"
        "PRINT CLASSOF(cat)\n"
        "PRINT ISA(cat, \"Cat\")\n"
        "PRINT ISA(cat, \"Animal\")\n"
        "PRINT ISA(cat, \"Counter\")\n") == "Miso\n9\nMiso makes a sound and meows\nanimalia\nCat\nTRUE\nTRUE\nFALSE\n", "runs class inheritance, overrides, SUPER, CLASSOF, and ISA");
    require(run_capture(
        "CLASS Ticket\n"
        "SHARED NextId = 100\n"
        "Id = 0\n"
        "SHARED FUNCTION Issue()\n"
        "Ticket.NextId = Ticket.NextId + 1\n"
        "RETURN Ticket.NextId\n"
        "END FUNCTION\n"
        "FUNCTION Init()\n"
        "SELF.Id = Ticket.Issue()\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "a = Ticket()\n"
        "b = Ticket()\n"
        "PRINT Ticket.NextId\n"
        "PRINT Ticket.Issue()\n"
        "PRINT a.Id\n"
        "PRINT b.Id\n") == "102\n103\n101\n102\n", "runs SHARED class fields and methods");
    require(run_capture(
        "CLASS Vault\n"
        "PRIVATE Secret = \"alpha\"\n"
        "PRIVATE SHARED Opens = 0\n"
        "PRIVATE FUNCTION Reveal()\n"
        "RETURN SELF.Secret\n"
        "END FUNCTION\n"
        "PRIVATE SHARED FUNCTION CountOpen()\n"
        "Vault.Opens = Vault.Opens + 1\n"
        "RETURN Vault.Opens\n"
        "END FUNCTION\n"
        "PUBLIC FUNCTION Open()\n"
        "ignored = Vault.CountOpen()\n"
        "RETURN SELF.Reveal()\n"
        "END FUNCTION\n"
        "PUBLIC SHARED FUNCTION OpenCount()\n"
        "RETURN Vault.Opens\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "vault = Vault()\n"
        "PRINT vault.Open()\n"
        "PRINT Vault.OpenCount()\n"
        "TRY\n"
        "PRINT vault.Secret\n"
        "CATCH err\n"
        "PRINT String.Contains(err.Message, \"private field\")\n"
        "END TRY\n"
        "TRY\n"
        "PRINT vault.Reveal()\n"
        "CATCH err\n"
        "PRINT String.Contains(err.Message, \"private method\")\n"
        "END TRY\n"
        "TRY\n"
        "PRINT Vault.Opens\n"
        "CATCH err\n"
        "PRINT String.Contains(err.Message, \"private field\")\n"
        "END TRY\n"
        "TRY\n"
        "PRINT Vault.CountOpen()\n"
        "CATCH err\n"
        "PRINT String.Contains(err.Message, \"private method\")\n"
        "END TRY\n") == "alpha\n1\nTRUE\nTRUE\nTRUE\nTRUE\n", "enforces PUBLIC and PRIVATE class members");
    require(run_capture(
        "CLASS Machine\n"
        "PROTECTED Serial = \"M-7\"\n"
        "PROTECTED FUNCTION ProtectedLabel()\n"
        "RETURN SELF.Serial + \":core\"\n"
        "END FUNCTION\n"
        "PUBLIC FUNCTION Label()\n"
        "RETURN SELF.ProtectedLabel()\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "CLASS Robot EXTENDS Machine\n"
        "PUBLIC FUNCTION RobotLabel()\n"
        "RETURN SELF.ProtectedLabel() + \":robot\"\n"
        "END FUNCTION\n"
        "PUBLIC FUNCTION ReadSerial()\n"
        "RETURN SELF.Serial\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "robot = Robot()\n"
        "PRINT robot.Label()\n"
        "PRINT robot.RobotLabel()\n"
        "PRINT robot.ReadSerial()\n"
        "TRY\n"
        "PRINT robot.Serial\n"
        "CATCH err\n"
        "PRINT String.Contains(err.Message, \"protected field\")\n"
        "END TRY\n"
        "TRY\n"
        "PRINT robot.ProtectedLabel()\n"
        "CATCH err\n"
        "PRINT String.Contains(err.Message, \"protected method\")\n"
        "END TRY\n") == "M-7:core\nM-7:core:robot\nM-7\nTRUE\nTRUE\n", "enforces PROTECTED class members");
    require(run_capture(
        "INTERFACE Writer\n"
        "FUNCTION Write(text)\n"
        "FUNCTION Flush()\n"
        "END INTERFACE\n"
        "CLASS BufferWriter IMPLEMENTS Writer\n"
        "Text = \"\"\n"
        "FUNCTION Write(text)\n"
        "SELF.Text = SELF.Text + text\n"
        "RETURN SELF.Text\n"
        "END FUNCTION\n"
        "FUNCTION Flush()\n"
        "RETURN SELF.Text\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "writer = BufferWriter()\n"
        "PRINT writer.Write(\"a\")\n"
        "PRINT writer.Write(\"b\")\n"
        "PRINT writer.Flush()\n"
        "PRINT IMPLEMENTS(writer, \"Writer\")\n") == "a\nab\nab\nTRUE\n", "runs interfaces and IMPLEMENTS checks");
    {
        arco::Runtime interface_error_runtime;
        const auto interface_error = interface_error_runtime.run_string(
            "INTERFACE Runnable\n"
            "FUNCTION Execute()\n"
            "END INTERFACE\n"
            "CLASS Broken IMPLEMENTS Runnable\n"
            "END CLASS\n");
        require(!interface_error.ok && interface_error.error.find("Broken does not implement Runnable.Execute") != std::string::npos, "reports missing interface methods");
    }
    {
        arco::Runtime interface_signature_runtime;
        const auto interface_signature_error = interface_signature_runtime.run_string(
            "INTERFACE Sink\n"
            "FUNCTION Write(text AS String) AS Number\n"
            "END INTERFACE\n"
            "CLASS BadSink IMPLEMENTS Sink\n"
            "FUNCTION Write(text AS Number) AS Number\n"
            "RETURN text\n"
            "END FUNCTION\n"
            "END CLASS\n");
        require(!interface_signature_error.ok && interface_signature_error.error.find("BadSink.Write parameter text should be String") != std::string::npos, "reports interface parameter type mismatches");
    }
    {
        arco::Runtime interface_return_runtime;
        const auto interface_return_error = interface_return_runtime.run_string(
            "INTERFACE Source\n"
            "FUNCTION Read() AS String\n"
            "END INTERFACE\n"
            "CLASS BadSource IMPLEMENTS Source\n"
            "FUNCTION Read() AS Number\n"
            "RETURN 1\n"
            "END FUNCTION\n"
            "END CLASS\n");
        require(!interface_return_error.ok && interface_return_error.error.find("BadSource.Read should return String") != std::string::npos, "reports interface return type mismatches");
    }
    require(run_capture(
        "CLASS Shape\n"
        "ABSTRACT FUNCTION Area()\n"
        "FUNCTION Describe()\n"
        "RETURN \"shape\"\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "CLASS Square EXTENDS Shape\n"
        "Side = 4\n"
        "FUNCTION Area()\n"
        "RETURN SELF.Side * SELF.Side\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "square = Square()\n"
        "PRINT square.Describe()\n"
        "PRINT square.Area()\n"
        "TRY\n"
        "bad = Shape()\n"
        "CATCH err\n"
        "PRINT String.Contains(err.Message, \"abstract class\")\n"
        "END TRY\n") == "shape\n16\nTRUE\n", "runs abstract methods and concrete subclasses");
    require(run_capture(
        "FUNCTION DoubleIt(value AS Number) AS Number\n"
        "RETURN value * 2\n"
        "END FUNCTION\n"
        "PRINT DoubleIt(4)\n"
        "TRY\n"
        "PRINT DoubleIt(\"bad\")\n"
        "CATCH err\n"
        "PRINT String.Contains(err.Message, \"expects Number\")\n"
        "END TRY\n"
        "FUNCTION BadReturn() AS Number\n"
        "RETURN \"oops\"\n"
        "END FUNCTION\n"
        "TRY\n"
        "PRINT BadReturn()\n"
        "CATCH err\n"
        "PRINT String.Contains(err.Message, \"should return Number\")\n"
        "END TRY\n") == "8\nTRUE\nTRUE\n", "enforces typed function parameters and returns");
    require(run_capture(
        "INTERFACE Speaker\n"
        "FUNCTION Speak() AS String\n"
        "END INTERFACE\n"
        "CLASS Animal IMPLEMENTS Speaker\n"
        "CONSTRUCTOR(name AS String)\n"
        "SELF.Name = name\n"
        "END CONSTRUCTOR\n"
        "Name = \"\"\n"
        "FUNCTION Speak() AS String\n"
        "RETURN SELF.Name\n"
        "END FUNCTION\n"
        "FUNCTION Rename(name AS String) AS String\n"
        "SELF.Name = name\n"
        "RETURN SELF.Name\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "FUNCTION Describe(animal AS Animal) AS String\n"
        "RETURN animal.Speak()\n"
        "END FUNCTION\n"
        "FUNCTION UseSpeaker(speaker AS Speaker) AS String\n"
        "RETURN speaker.Speak()\n"
        "END FUNCTION\n"
        "pet = Animal(\"Miso\")\n"
        "PRINT Describe(pet)\n"
        "PRINT UseSpeaker(pet)\n"
        "PRINT pet.Rename(\"Nori\")\n"
        "TRY\n"
        "PRINT Describe(\"bad\")\n"
        "CATCH err\n"
        "PRINT String.Contains(err.Message, \"expects Animal\")\n"
        "END TRY\n"
        "TRY\n"
        "PRINT UseSpeaker({\"Name\": \"plain\"})\n"
        "CATCH err\n"
        "PRINT String.Contains(err.Message, \"expects Speaker\")\n"
        "END TRY\n"
        "TRY\n"
        "PRINT pet.Rename(42)\n"
        "CATCH err\n"
        "PRINT String.Contains(err.Message, \"expects String\")\n"
        "END TRY\n") == "Miso\nMiso\nNori\nTRUE\nTRUE\nTRUE\n", "enforces typed class and interface parameters");
    require(run_capture(
        "CLASS TypedBox\n"
        "Value AS Number = 1\n"
        "Name AS String\n"
        "SHARED Count AS Number = 0\n"
        "CONSTRUCTOR(name AS String)\n"
        "SELF.Name = name\n"
        "TypedBox.Count = TypedBox.Count + 1\n"
        "END CONSTRUCTOR\n"
        "FUNCTION SetValue(value AS Number) AS Number\n"
        "SELF.Value = value\n"
        "RETURN SELF.Value\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "box = TypedBox(\"alpha\")\n"
        "PRINT box.Name\n"
        "PRINT box.Value\n"
        "PRINT box.SetValue(7)\n"
        "PRINT TypedBox.Count\n"
        "TRY\n"
        "box.Value = \"bad\"\n"
        "CATCH err\n"
        "PRINT String.Contains(err.Message, \"expects Number\")\n"
        "END TRY\n"
        "TRY\n"
        "TypedBox.Count = \"bad\"\n"
        "CATCH err\n"
        "PRINT String.Contains(err.Message, \"expects Number\")\n"
        "END TRY\n") == "alpha\n1\n7\n1\nTRUE\nTRUE\n", "enforces typed class fields");
    {
        arco::Runtime bad_field_runtime;
        const auto bad_field = bad_field_runtime.run_string(
            "CLASS BrokenField\n"
            "Value AS Number = \"bad\"\n"
            "END CLASS\n"
            "item = BrokenField()\n");
        require(!bad_field.ok && bad_field.error.find("BrokenField.Value expects Number") != std::string::npos, "reports typed field default errors");
    }
    require(run_capture("REM top-level comment\n10 REM numbered comment\n20 PRINT \"comments ok\"\n") == "comments ok\n", "runs BASIC REM comments and numbered comment lines");
    require(run_capture("IF 3 > 2 THEN\nPRINT \"YES\"\nELSE\nPRINT \"NO\"\nEND IF\n") == "YES\n", "runs IF branch");
    require(run_capture("FOR i = 1 TO 3\nPRINT i\nNEXT\n") == "1\n2\n3\n", "runs FOR loops");
    require(run_capture("FOR item IN [\"a\", \"b\"]\nPRINT item\nNEXT\n") == "a\nb\n", "runs FOR IN loops");
    require(run_capture("person = {\"Name\": \"Ada\", \"Age\": 36}\nPRINT person.Name\n") == "Ada\n", "reads object properties");
    require(run_capture("items = [10, 20, 30]\nPRINT items[1]\nitems[1] = 99\nPRINT items[1]\nPRINT items\n") == "20\n99\n[10, 99, 30]\n", "reads and writes array indexes");
    require(run_capture("items = [1, 2]\nPRINT Array.Push(items, 3)\nPRINT items\nPRINT Array.Pop(items)\nPRINT items\nPRINT Array.Find(items, 2)\nPRINT Array.Reverse(items)\nPRINT Array.Join([\"a\", \"b\", \"c\"], \":\")\nPRINT Array.Contains([3, 1, 2], 1)\nPRINT Array.Sort([3, 1, 2])\n") == "3\n[1, 2, 3]\n3\n[1, 2]\n1\n[2, 1]\na:b:c\nTRUE\n[1, 2, 3]\n", "runs array helper functions");
    require(run_capture(
        "items = Array.New()\n"
        "PRINT Array.Empty(items)\n"
        "PRINT Array.Add(items, \"alpha\")\n"
        "PRINT Array.Append(items, 42)\n"
        "PRINT Array.Insert(items, 1, TRUE)\n"
        "PRINT items\n"
        "PRINT Array.First(items)\n"
        "PRINT Array.Last(items)\n"
        "PRINT Array.RemoveAt(items, 1)\n"
        "PRINT items\n"
        "PRINT Array.Remove(items, 42)\n"
        "PRINT items\n"
        "PRINT Array.Unshift(items, \"start\")\n"
        "PRINT Array.Shift(items)\n"
        "PRINT items\n"
        "PRINT Array.Extend(items, [\"beta\", \"gamma\"])\n"
        "PRINT items\n"
        "PRINT Array.Resize(items, 5, \"pad\")\n"
        "PRINT items\n"
        "PRINT Array.Resize(items, 2)\n"
        "PRINT items\n"
        "PRINT Array.Length(items)\n"
        "PRINT Array.Clear(items)\n"
        "PRINT Array.IsEmpty(items)\n"
        "PRINT Array.New(3, \"x\")\n") == "TRUE\n1\n2\n3\n[alpha, TRUE, 42]\nalpha\n42\nTRUE\n[alpha, 42]\nTRUE\n[alpha]\n2\nstart\n[alpha]\n3\n[alpha, beta, gamma]\n5\n[alpha, beta, gamma, pad, pad]\n2\n[alpha, beta]\n2\n0\nTRUE\n[x, x, x]\n", "runs vector-style array mutation helpers");
    require(run_capture(
        "CLASS Evidence\n"
        "Name AS String = \"\"\n"
        "CONSTRUCTOR(name AS String)\n"
        "SELF.Name = name\n"
        "END CONSTRUCTOR\n"
        "FUNCTION Label() AS String\n"
        "RETURN \"evidence:\" + SELF.Name\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "items = []\n"
        "ignored = Array.Add(items, 7)\n"
        "ignored = Array.Add(items, \"Kevin\")\n"
        "ignored = Array.Add(items, {\"Room\": \"Annex\"})\n"
        "ignored = Array.Add(items, Evidence(\"badge printer\"))\n"
        "ignored = Array.Add(items, [\"nested\", TRUE])\n"
        "room = items[2]\n"
        "evidence = items[3]\n"
        "nested = items[4]\n"
        "PRINT LEN(items)\n"
        "PRINT room.Room\n"
        "PRINT evidence.Label()\n"
        "PRINT nested[1]\n"
        "PRINT CLASSOF(evidence)\n") == "5\nAnnex\nevidence:badge printer\nTRUE\nEvidence\n", "arrays hold mixed values, objects, class instances, and nested arrays");
    require(run_capture("person = {\"Name\": \"Ada\", \"Role\": \"Admin\"}\nPRINT Object.Has(person, \"Name\")\nPRINT Object.Get(person, \"Missing\", \"fallback\")\ncopy = Object.Set(person, \"Role\", \"Operator\")\nPRINT copy.Role\nPRINT person.Role\nPRINT Array.Contains(Object.Keys(person), \"Name\")\n") == "TRUE\nfallback\nOperator\nAdmin\nTRUE\n", "runs object helper functions");
    require(run_capture(
        "person = {\n"
        "    \"Name\": \"Ada\",\n"
        "    \"Scores\": [\n"
        "        2,\n"
        "        3,\n"
        "    ],\n"
        "}\n"
        "PRINT person.Name\n"
        "PRINT person.Scores[1]\n"
        "PRINT MAX(\n"
        "    2,\n"
        "    9,\n"
        "    4,\n"
        ")\n") == "Ada\n3\n9\n", "runs multiline literals and argument lists");
    require(run_capture("PRINT Time.Timestamp() > 0\nPRINT LEN(Time.Now()) > 0\nSleep(0)\nPRINT \"awake\"\n") == "TRUE\nTRUE\nawake\n", "runs time and sleep helpers");
    require(run_capture("person = {\"Name\": \"Ada\"}\nperson.Name = \"Grace\"\nPRINT person.Name\n") == "Grace\n", "writes object properties");
    require(run_capture("items = [1, 2, 3]\nPRINT items\nPRINT 2 IN items\n") == "[1, 2, 3]\nTRUE\n", "handles arrays");
    require(run_capture("PRINT LEN([1, 2, 3])\nPRINT Upper(\"basic\")\nPRINT \"abc\" CONTAINS \"b\"\n") == "3\nBASIC\nTRUE\n", "runs core helper functions");
    require(run_capture("FUNCTION MixedName(value)\nRETURN upper(value)\nEND FUNCTION\nPRINT mixedname(\"case\")\nPRINT LOWER(\"CASE\")\nPRINT len([1, 2])\nPRINT string(123)\n") == "CASE\ncase\n2\n123\n", "calls core and user functions case-insensitively");
    require(run_capture("text = \"Aé猫\"\nPRINT String.Length(text)\nPRINT String.Slice(text, 1, 1)\nPRINT String.Slice(text, 2)\n") == "3\né\n猫\n", "slices UTF-8 strings by code point");
    require(run_capture("PRINT TYPEOF(NULL)\nPRINT TYPEOF([1])\nPRINT ISNULL(NULL)\nPRINT NUMBER(\"42\") + 1\nPRINT STRING(123)\n") == "Null\nArray\nTRUE\n43\n123\n", "runs type and conversion helpers");
    require(run_capture(
        "x = 10\n"
        "r = REF(x, \"Number\")\n"
        "PRINT CLASSOF(r)\n"
        "PRINT TYPEOF(r)\n"
        "PRINT r.TypeName\n"
        "PRINT r.Value\n"
        "r.Value = 25\n"
        "PRINT x\n"
        "TRY\n"
        "r.Value = \"bad\"\n"
        "CATCH err\n"
        "PRINT err.Message CONTAINS \"Number\"\n"
        "END TRY\n"
        "n = NULL\n"
        "nr = REF(n, \"String\")\n"
        "PRINT nr.Exists()\n"
        "PRINT ISNULL(nr.Value)\n"
        "nr.Value = \"ok\"\n"
        "PRINT n\n"
        "PRINT r.Exists()\n"
        "r.Clear()\n"
        "PRINT r.Exists()\n"
        "PRINT ISNULL(r.Value)\n"
        "CLASS Player\n"
        "Name AS String = \"Ada\"\n"
        "Score AS Number = 1\n"
        "END CLASS\n"
        "player = Player()\n"
        "playerRef = REF(player)\n"
        "playerRef.Value.Name = \"Grace\"\n"
        "playerRef.Value.Score = 7\n"
        "PRINT player.Name\n"
        "PRINT player.Score\n"
        "box = REF({\"Count\": 1})\n"
        "box.Value.Count = 2\n"
        "PRINT box.Value.Count\n"
        "box.Set({\"Count\": 3})\n"
        "PRINT box.Value.Count\n") == "REF\nReference\nNumber\n10\n25\nTRUE\nTRUE\nTRUE\nok\nTRUE\nFALSE\nTRUE\nGrace\n7\n2\n3\n", "runs safe typed REF references for variables and objects");
    require(run_capture("PRINT String.Trim(\"  hello  \")\nPRINT String.Split(\"a,b,c\", \",\")\nPRINT String.Replace(\"a-b-c\", \"-\", \"+\")\nPRINT String.Contains(\"abcdef\", \"cd\")\nPRINT String.IndexOf(\"abcdefabc\", \"abc\", 1)\nPRINT String.StartsWith(\"abcdef\", \"abc\")\nPRINT String.EndsWith(\"abcdef\", \"def\")\nPRINT String.Lines(\"a\\nb\")\nPRINT Format(\"{0}:{1}\", \"left\", 42)\n") == "hello\n[a, b, c]\na+b+c\nTRUE\n6\nTRUE\nTRUE\n[a, b]\nleft:42\n", "runs string helper functions");
    require(run_capture("PRINT String.Insert(\"abcd\", 2, \"XX\")\nPRINT String.Delete(\"abcd\", 1, 2)\nPRINT String.Join([\"a\", \"b\", \"c\"], \"|\")\ndoc = Document.New(\"hello\")\ndoc = Document.InsertText(doc, 5, \" world\")\nPRINT Document.Text(doc)\ndoc = Document.DeleteRange(doc, 5, 1)\nPRINT Document.LineAt(doc, 0)\ndoc = Document.ReplaceRange(doc, 5, 5, \" there\")\nPRINT Document.Text(doc)\npos = Document.LineColumnAt(Document.New(\"a\\nbc\"), 3)\nPRINT pos.Line\nPRINT pos.Column\nPRINT Document.OffsetAtLineColumn(Document.New(\"a\\nbc\"), 1, 1)\ndoc = Document.ApplyFormat(doc, 0, 5, {\"Bold\": TRUE, \"FontSize\": 22})\nPRINT LEN(Document.Runs(doc))\npacked = Document.Serialize(doc)\nround = Document.Parse(packed)\nPRINT Document.Text(round)\nPRINT Object.Get(Document.Runs(round)[0], \"Bold\")\nbytes = Bytes.New(3, 65)\nbytes = Bytes.SetU8(bytes, 1, 66)\nPRINT Bytes.GetU8(bytes, 1)\nPRINT Bytes.ToText(bytes)\nPRINT Bytes.Length(Bytes.FromText(\"abc\"))\n") == "abXXcd\nad\na|b|c\nhello world\nhelloworld\nhello there\n1\n1\n3\n1\nhello there\nTRUE\n66\nABA\n3\n", "runs text editing, document, and byte helpers");
    require(run_capture("doc = Document.New(\"hello there\")\ndoc = Document.ApplyFormat(doc, 0, 5, {\"Bold\": TRUE, \"FontSize\": 22, \"Align\": \"left\"})\ndoc = Document.ApplyFormat(doc, 5, 6, {\"Bold\": TRUE, \"FontSize\": 22, \"Align\": \"left\"})\nPRINT LEN(Document.Runs(doc))\nPRINT Object.Get(Document.Runs(doc)[0], \"Length\")\ndoc = Document.ApplyFormat(doc, 6, 5, {\"Italic\": TRUE, \"FontSize\": 18, \"Align\": \"right\"})\nPRINT LEN(Document.Runs(doc))\nPRINT Object.Get(Document.Runs(doc)[0], \"Length\")\ndoc = Document.InsertText(doc, 2, \"X\")\nPRINT Document.Text(doc)\nPRINT Object.Get(Document.Runs(doc)[0], \"Length\")\ndoc = Document.DeleteRange(doc, 1, 2)\nPRINT Document.Text(doc)\nPRINT Object.Get(Document.Runs(doc)[1], \"Start\")\npacked = Document.Serialize(doc)\nround = Document.Parse(packed)\nPRINT Object.Get(Document.Runs(round)[1], \"Align\")\n") == "1\n11\n2\n6\nheXllo there\n7\nhllo there\n5\nright\n", "normalizes document formatting runs across edits and persistence");
    require(run_capture("TRY\nPRINT missing_value\nCATCH err\nPRINT err.Message\nEND TRY\nPRINT \"after\"\n") == "undefined variable: missing_value\nafter\n", "catches runtime errors");
    require(run_capture(
        "FUNCTION Validate(value)\n"
        "IF value < 0 THEN\n"
        "THROW \"value must be non-negative\"\n"
        "END IF\n"
        "RETURN value\n"
        "END FUNCTION\n"
        "TRY\n"
        "PRINT Validate(-1)\n"
        "CATCH err\n"
        "PRINT err.Type\n"
        "PRINT err.Message\n"
        "END TRY\n"
        "TRY\n"
        "PRINT missing_again\n"
        "CATCH err\n"
        "PRINT err.Type\n"
        "END TRY\n") == "UserError\nvalue must be non-negative\nRuntimeError\n",
        "throws and classifies source-defined runtime errors");
    require(run_capture(
        "TRY\n"
        "TRY\n"
        "THROW \"inner\"\n"
        "CATCH inner\n"
        "PRINT inner.Message\n"
        "THROW \"outer\"\n"
        "END TRY\n"
        "CATCH outer\n"
        "PRINT outer.Type\n"
        "PRINT outer.Message\n"
        "END TRY\n") == "inner\nUserError\nouter\n",
        "propagates throws from catch bodies to outer handlers");
    arco::Runtime throw_errors;
    const auto non_string_throw = throw_errors.run_string("THROW 7\n");
    require(!non_string_throw.ok && non_string_throw.error.find("THROW message must be String; received Number") != std::string::npos,
            "rejects non-string THROW messages");
    const auto missing_throw = throw_errors.run_string("THROW\n");
    require(!missing_throw.ok && missing_throw.error.find("expected an expression after THROW") != std::string::npos,
            "requires a THROW expression");
    require(run_capture(
        "TRY\n"
        "THROW missing_throw_message\n"
        "CATCH err\n"
        "PRINT err.Type\n"
        "END TRY\n") == "RuntimeError\n",
        "preserves message-expression failures as runtime errors");
    const auto located_throw = throw_errors.run_string(
        "FUNCTION Fail()\n"
        "THROW \"located\"\n"
        "END FUNCTION\n"
        "Fail()\n");
    require(!located_throw.ok && located_throw.error.find("located") != std::string::npos &&
                located_throw.error.find("THROW \"located\"") != std::string::npos,
            "reports the executing THROW source location across a function call");
    const auto freestanding_throw = throw_errors.run_string("#RUNTIME NONE\nTHROW \"no hosted unwind\"\n");
    require(!freestanding_throw.ok && freestanding_throw.error.find("hosted runtime error unwinding") != std::string::npos,
            "rejects THROW under #RUNTIME NONE");
    require(run_capture("x = 10\nx += 5\nx -= 3\nx *= 2\nx /= 4\nPRINT x\n") == "6\n", "runs arithmetic compound assignment");
    require(run_capture("x = 6\nx &= 3\nPRINT x\nx |= 8\nPRINT x\nx ^= 2\nPRINT x\nx <<= 1\nPRINT x\nx >>= 2\nPRINT x\n") == "2\n10\n8\n16\n4\n", "runs bitwise compound assignment");
    require(run_capture("PRINT 6 & 3\nPRINT 6 | 3\nPRINT 6 ^ 3\nPRINT ~6\nPRINT 1 << 4\nPRINT 16 >> 2\n") == "2\n7\n5\n-7\n16\n4\n", "runs symbolic bitwise operators");
    require(run_capture("PRINT 6 BITAND 3\nPRINT 6 BITOR 3\nPRINT 6 BITXOR 3\nPRINT BITNOT 6\nPRINT 1 SHL 4\nPRINT 16 SHR 2\n") == "2\n7\n5\n-7\n16\n4\n", "runs word bitwise operators");
    require(run_capture("PRINT Bit.And(6, 3)\nPRINT Bit.Or(6, 3)\nPRINT Bit.Xor(6, 3)\nPRINT Bit.Not(6)\nPRINT Bit.ShiftLeft(1, 4)\nPRINT Bit.ShiftRight(16, 2)\n") == "2\n7\n5\n-7\n16\n4\n", "runs readable bit helpers");
    require(run_capture("PRINT ABS(SIN(PI())) < 0.000001\nPRINT COS(0)\nPRINT TAN(0)\nPRINT ATAN2(1, 0) > 1.57\nPRINT SQRT(81)\nPRINT FLOOR(3.9)\nPRINT CEIL(3.1)\nPRINT ROUND(3.5)\nPRINT ABS(-12)\nPRINT MIN(9, 3, 5)\nPRINT MAX(9, 3, 5)\nPRINT CLAMP(12, 0, 10)\nPRINT LERP(10, 20, 0.25)\nPRINT Math.Pow(2, 8)\nPRINT Object.Get(Math.Constants(), \"TAU\") > 6.28\n") == "TRUE\n1\n0\nTRUE\n9\n3\n4\n4\n12\n3\n9\n10\n12.5\n256\nTRUE\n", "runs core math helpers");
    require(run_capture("PRINT %10101010\nPRINT 0b10101010\nPRINT &HFF\nPRINT 0xFF\nPRINT 10 % 4\n") == "170\n170\n255\n255\n2\n", "runs binary and hex numeric literals alongside symbolic modulo");
    require(run_capture("PRINT SHIFT(1, 5)\nPRINT SHIFT(32, -2)\nPRINT BIT(8, 3)\nPRINT SETBIT(0, 4)\nPRINT CLEARBIT(31, 4)\nPRINT TOGGLEBIT(0, 2)\n") == "32\n8\nTRUE\n16\n15\n4\n", "runs human friendly bit helpers");
    require(run_capture("PRINT BitsToString(27)\nPRINT BitsToString(27, 8)\nPRINT BitsToBinary(3, 4)\nPRINT StringToBits(\"11011\")\nPRINT BITCOUNT(15)\nPRINT HexToString(255)\nPRINT StringToHex(\"FF\")\nPRINT BytesToHex([164, 241, 44, 157])\nPRINT HexToBytes(\"0A0B\")\n") == "11011\n00011011\n0011\n27\n4\nFF\n255\nA4F12C9D\n[10, 11]\n", "runs bit conversion helpers");
    require(run_capture("FLAGS FileAttributes\nReadOnly = SHIFT(1, 0)\nHidden = SHIFT(1, 1)\nSystem = SHIFT(1, 2)\nEND FLAGS\nattrs = 0\nattrs ADD FileAttributes.Hidden\nPRINT attrs HAS FileAttributes.Hidden\nattrs TOGGLE FileAttributes.Hidden\nPRINT attrs HAS FileAttributes.Hidden\nattrs ADD FileAttributes.System\nattrs REMOVE FileAttributes.System\nPRINT attrs\n") == "TRUE\nFALSE\n0\n", "runs flag blocks and flag operations");
    require(run_capture("#DEFINE DEBUG\n#DEFINE MASK_READ 0b0001\n#IFDEF DEBUG\nPRINT \"debug\"\n#ELSE\nPRINT \"release\"\n#ENDIF\n#IFNDEF MISSING\nPRINT MASK_READ\n#ENDIF\n#IFDEF MISSING\n#ERROR \"inactive error\"\n#ENDIF\n@EXPERIMENTAL(\"next symbol\")\nPRINT \"attr ok\"\n") == "debug\n1\nattr ok\n", "runs directives, defines, conditionals, and attributes");
    require(run_capture("x = 0\nWHILE x < 3\nx = x + 1\nPRINT x\nWEND\n") == "1\n2\n3\n", "runs WHILE loops");

    const auto import_path = std::filesystem::temp_directory_path() / "arco-import-test.abas";
    write_text(import_path, "FUNCTION ImportedValue()\nRETURN \"from import\"\nEND FUNCTION\nFUNCTION ImportedEcho(value AS String, suffix = \"!\")\nRETURN value + suffix\nEND FUNCTION\n");
    require(run_capture("#IMPORT \"" + import_path.string() + "\"\nPRINT ImportedValue()\n") == "from import\n", "executes imported source files");
    require(run_capture("#IMPORT \"" + import_path.string() + "\" AS Demo\nPRINT Demo.ImportedValue()\nPRINT demo.importedecho(\"ok\")\nPRINT Demo.ImportedEcho(\"ok\", \"?\")\n") == "from import\nok!\nok?\n", "imports source files through an alias namespace");
    require(run_capture("#IMPORT \"text\"\nPRINT Text.IsBlank(\"   \")\nPRINT text.join([\"a\", \"b\"], \":\")\n") == "TRUE\na:b\n", "imports stdlib modules by name and calls functions case-insensitively");
    require(run_capture("#IMPORT \"text\" AS Txt\nPRINT Txt.IsBlank(\"   \")\nPRINT txt.join([\"a\", \"b\"], \":\")\n") == "TRUE\na:b\n", "imports stdlib modules through alias namespaces");
    require(run_capture(
        "#IMPORT \"compy\"\n"
        "data = {\"Name\": \"Ada\", \"Items\": [1, \"two\", TRUE, NULL], \"Flags\": {\"Ready\": TRUE}}\n"
        "packed = ArcoCompy.Pack(data)\n"
        "restored = ArcoCompy.Unpack(packed)\n"
        "PRINT String.StartsWith(packed, \"ACPY1|\")\n"
        "PRINT restored.Name\n"
        "PRINT restored.Items[1]\n"
        "PRINT restored.Flags.Ready\n"
        "PRINT ISNULL(restored.Items[3])\n") == "TRUE\nAda\ntwo\nTRUE\nTRUE\n", "packs and unpacks nested values with ArcoCompy");
    require(run_capture(
        "#IMPORT \"compy\"\n"
        "good = ArcoCompy.TryUnpack(ArcoCompy.Pack([1, \"two\"]))\n"
        "PRINT good.Ok\n"
        "PRINT good.Value[1]\n"
        "badHeader = ArcoCompy.TryUnpack(\"NOPE|Z\")\n"
        "PRINT badHeader.Ok\n"
        "PRINT badHeader.Error CONTAINS \"header\"\n"
        "badString = ArcoCompy.TryUnpack(\"ACPY1|S99:Hi\")\n"
        "PRINT badString.Ok\n"
        "PRINT badString.Error CONTAINS \"string length\"\n"
        "trailing = ArcoCompy.TryUnpack(\"ACPY1|Tjunk\")\n"
        "PRINT trailing.Ok\n"
        "PRINT trailing.Error CONTAINS \"trailing\"\n"
        "tooDeep = ArcoCompy.TryUnpackWithLimits(\"ACPY1|A1:A1:Z\", 0, 100)\n"
        "PRINT tooDeep.Ok\n"
        "PRINT tooDeep.Error CONTAINS \"depth\"\n"
        "tooMany = ArcoCompy.TryUnpackWithLimits(\"ACPY1|A2:ZZ\", 64, 1)\n"
        "PRINT tooMany.Ok\n"
        "PRINT tooMany.Error CONTAINS \"count\"\n"
        "PRINT ISNULL(ArcoCompy.Unpack(\"ACPY1|S99:Hi\"))\n") == "TRUE\ntwo\nFALSE\nTRUE\nFALSE\nTRUE\nFALSE\nTRUE\nFALSE\nTRUE\nFALSE\nTRUE\nTRUE\n", "reports ArcoCompy corruption and unpack limits safely");
    require(run_capture(
        "#IMPORT \"compy\"\n"
        "CLASS SaveSlot\n"
        "Name AS String = \"\"\n"
        "Level AS Number = 1\n"
        "CONSTRUCTOR(name AS String, level AS Number)\n"
        "SELF.Name = name\n"
        "SELF.Level = level\n"
        "END CONSTRUCTOR\n"
        "FUNCTION Label() AS String\n"
        "RETURN SELF.Name + \"@\" + STRING(SELF.Level)\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "slot = SaveSlot(\"Miso\", 7)\n"
        "restored = ArcoCompy.Unpack(ArcoCompy.Pack(slot))\n"
        "PRINT CLASSOF(restored)\n"
        "PRINT ISA(restored, \"SaveSlot\")\n"
        "PRINT restored.Label()\n") == "SaveSlot\nTRUE\nMiso@7\n", "packs and unpacks class-backed objects with ArcoCompy");
    require(run_capture(
        "#IMPORT \"compydb\"\n"
        "CLASS Customer\n"
        "customerNumber AS Number = 0\n"
        "name AS String = \"\"\n"
        "email AS String = \"\"\n"
        "CONSTRUCTOR(customerNumber AS Number, name AS String, email AS String)\n"
        "SELF.customerNumber = customerNumber\n"
        "SELF.name = name\n"
        "SELF.email = email\n"
        "END CONSTRUCTOR\n"
        "FUNCTION Label() AS String\n"
        "RETURN STRING(SELF.customerNumber) + \":\" + SELF.name\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "schema = ArcoCompyDB.SchemaVersion(\"Customer\", [\"customerNumber\", \"name\", \"email\"], 1)\n"
        "customer = Customer(1042, \"Wanda\", \"wanda@email.com\")\n"
        "packed = ArcoCompyDB.PackRecord(schema, customer)\n"
        "PRINT String.StartsWith(packed, \"ACDB1|\")\n"
        "PRINT packed CONTAINS \"customerNumber\"\n"
        "result = ArcoCompyDB.TryUnpackRecord(schema, packed)\n"
        "PRINT result.Ok\n"
        "restored = result.Value\n"
        "PRINT CLASSOF(restored)\n"
        "PRINT restored.Label()\n"
        "PRINT restored.email\n"
        "bad = ArcoCompyDB.TryUnpackRecord(schema, \"ACPY1|Z\")\n"
        "PRINT bad.Ok\n"
        "PRINT bad.Error CONTAINS \"ACDB1\"\n") == "TRUE\nFALSE\nTRUE\nCustomer\n1042:Wanda\nwanda@email.com\nFALSE\nTRUE\n", "packs compact schema-aware ArcoCompyDB records");
    require(run_capture(
        "#IMPORT \"compydb\"\n"
        "oldSchema = ArcoCompyDB.SchemaVersion(\"Customer\", [\"customerNumber\", \"name\"], 1)\n"
        "newSchema = ArcoCompyDB.SchemaVersion(\"Customer\", [\"customerNumber\", \"name\", \"email\"], 2)\n"
        "newRecord = {\"customerNumber\": 1042, \"name\": \"Wanda\", \"email\": \"wanda@email.com\"}\n"
        "packed = ArcoCompyDB.PackRecord(newSchema, newRecord)\n"
        "oldResult = ArcoCompyDB.TryUnpackRecord(oldSchema, packed)\n"
        "PRINT oldResult.Ok\n"
        "oldValue = oldResult.Value\n"
        "PRINT oldValue.name\n"
        "PRINT oldValue.__unknown_values[0]\n"
        "repacked = ArcoCompyDB.PackRecord(oldSchema, oldValue)\n"
        "newResult = ArcoCompyDB.TryUnpackRecord(newSchema, repacked)\n"
        "PRINT newResult.Ok\n"
        "roundTrip = newResult.Value\n"
        "PRINT roundTrip.email\n") == "TRUE\nWanda\nwanda@email.com\nTRUE\nwanda@email.com\n", "preserves unknown ArcoCompyDB trailing values across older schemas");
    {
        const auto db_file = std::filesystem::temp_directory_path() / "arcodb-runtime-test.arcodb";
        std::filesystem::remove(db_file);
        require(run_capture(
            "#IMPORT \"arcodb\"\n"
            "path = \"" + db_file.string() + "\"\n"
            "db = ArcoDB.Open(path)\n"
            "schema = ArcoDB.SchemaVersion(db, \"Customer\", [\"customerNumber\", \"name\", \"email\"], 1)\n"
            "ignored = ArcoDB.Catalog(db, schema, \"email\")\n"
            "customer = {\"customerNumber\": 1042, \"name\": \"Wanda\", \"email\": \"wanda@email.com\"}\n"
            "id = ArcoDB.Keep(db, schema, customer)\n"
            "PRINT id\n"
            "restored = ArcoDB.Recall(db, schema, id)\n"
            "PRINT restored.name\n"
            "ignored = ArcoDB.Catalog(db, schema, \"customerNumber\")\n"
            "byNumber = ArcoDB.RecallBy(db, schema, \"customerNumber\", 1042)\n"
            "PRINT byNumber.email\n"
            "byEmail = ArcoDB.RecallBy(db, schema, \"email\", \"wanda@email.com\")\n"
            "PRINT byEmail.name\n"
            "restored.email = \"wanda.goodburger@example.test\"\n"
            "PRINT ArcoDB.Replace(db, schema, id, restored)\n"
            "PRINT ISNULL(ArcoDB.RecallBy(db, schema, \"email\", \"wanda@email.com\"))\n"
            "updatedByEmail = ArcoDB.RecallBy(db, schema, \"email\", \"wanda.goodburger@example.test\")\n"
            "PRINT updatedByEmail.email\n"
            "PRINT ArcoDB.Write(db)\n"
            "reopened = ArcoDB.Open(path)\n"
            "loadedSchema = ArcoDB.SchemaFor(reopened, \"Customer\")\n"
            "loaded = ArcoDB.Recall(reopened, loadedSchema, id)\n"
            "PRINT loaded.email\n"
            "reopenedByEmail = ArcoDB.RecallBy(reopened, loadedSchema, \"email\", \"wanda.goodburger@example.test\")\n"
            "PRINT reopenedByEmail.name\n"
            "info = ArcoDB.Inspect(reopened)\n"
            "PRINT info.Active\n"
            "PRINT info.Catalogs\n"
            "PRINT info.Dirty\n"
            "PRINT ArcoDB.Forget(reopened, id)\n"
            "PRINT ArcoDB.Count(reopened)\n"
            "PRINT ISNULL(ArcoDB.RecallBy(reopened, loadedSchema, \"email\", \"wanda.goodburger@example.test\"))\n"
            "beforeCompact = ArcoDB.Inspect(reopened)\n"
            "PRINT beforeCompact.Records\n"
            "PRINT beforeCompact.Tombstones\n"
            "compacted = ArcoDB.Compact(reopened)\n"
            "PRINT compacted.Removed\n"
            "afterCompact = ArcoDB.Inspect(reopened)\n"
            "PRINT afterCompact.Records\n"
            "PRINT afterCompact.Tombstones\n"
            "PRINT ISNULL(ArcoDB.Recall(reopened, loadedSchema, id))\n") == "1\nWanda\nwanda@email.com\nWanda\nTRUE\nTRUE\nwanda.goodburger@example.test\nTRUE\nwanda.goodburger@example.test\nWanda\n1\n2\nFALSE\nTRUE\n0\nTRUE\n1\n1\n1\n0\n0\nTRUE\n", "stores, recalls, catalogs, replaces, writes, reopens, forgets, and compacts ArcoDB objects");
        require(std::filesystem::exists(db_file), "writes ArcoDB file");
    }
    {
        const auto db_file = std::filesystem::temp_directory_path() / "arcodb-compact-test.arcodb";
        std::filesystem::remove(db_file);
        std::filesystem::remove(db_file.string() + ".journal");
        require(run_capture(
            "#IMPORT \"arcodb\"\n"
            "path = \"" + db_file.string() + "\"\n"
            "db = ArcoDB.Open(path)\n"
            "schema = ArcoDB.Schema(db, \"Customer\", [\"customerNumber\", \"name\", \"email\"])\n"
            "ignored = ArcoDB.Catalog(db, schema, \"email\")\n"
            "first = ArcoDB.Keep(db, schema, {\"customerNumber\": 1, \"name\": \"Wanda\", \"email\": \"wanda@email.com\"})\n"
            "second = ArcoDB.Keep(db, schema, {\"customerNumber\": 2, \"name\": \"Pell\", \"email\": \"pell@email.com\"})\n"
            "PRINT ArcoDB.Forget(db, first)\n"
            "before = ArcoDB.Inspect(db)\n"
            "PRINT before.Records\n"
            "PRINT before.Tombstones\n"
            "compacted = ArcoDB.Compact(db)\n"
            "PRINT compacted.Removed\n"
            "after = ArcoDB.Inspect(db)\n"
            "PRINT after.Records\n"
            "PRINT after.Tombstones\n"
            "survivor = ArcoDB.RecallBy(db, schema, \"email\", \"pell@email.com\")\n"
            "PRINT survivor.name\n"
            "PRINT ISNULL(ArcoDB.RecallBy(db, schema, \"email\", \"wanda@email.com\"))\n") == "TRUE\n2\n1\n1\n1\n0\nPell\nTRUE\n", "compacts tombstones and rebuilds ArcoDB catalogs for surviving records");
    }
    {
        const auto db_file = std::filesystem::temp_directory_path() / "arcodb-journal-test.arcodb";
        std::filesystem::remove(db_file);
        std::filesystem::remove(db_file.string() + ".journal");
        require(run_capture(
            "#IMPORT \"arcodb\"\n"
            "path = \"" + db_file.string() + "\"\n"
            "db = ArcoDB.Open(path)\n"
            "schema = ArcoDB.Schema(db, \"Customer\", [\"customerNumber\", \"name\"])\n"
            "id = ArcoDB.Keep(db, schema, {\"customerNumber\": 1042, \"name\": \"Wanda\"})\n"
            "PRINT ArcoDB.PrepareWrite(db)\n"
            "PRINT File.Exists(path)\n"
            "PRINT File.Exists(ArcoDB.JournalPath(path))\n"
            "recovered = ArcoDB.Open(path)\n"
            "loadedSchema = ArcoDB.SchemaFor(recovered, \"Customer\")\n"
            "loaded = ArcoDB.Recall(recovered, loadedSchema, id)\n"
            "PRINT loaded.name\n"
            "info = ArcoDB.Inspect(recovered)\n"
            "PRINT info.Journal CONTAINS \".journal\"\n"
            "PRINT File.Exists(path)\n"
            "PRINT String.Length(File.ReadText(ArcoDB.JournalPath(path))) == 0\n") == "TRUE\nFALSE\nTRUE\nWanda\nTRUE\nTRUE\nTRUE\n", "recovers ArcoDB from a prepared journal");
        require(std::filesystem::exists(db_file), "journal recovery writes ArcoDB file");
    }
    {
        const auto db_file = std::filesystem::temp_directory_path() / "arcodb-command-test.arcodb";
        std::filesystem::remove(db_file);
        std::filesystem::remove(db_file.string() + ".journal");
        require(run_capture(
            "#IMPORT \"arcodb\"\n"
            "CLASS SignInLog\n"
            "User AS String = \"\"\n"
            "SignedInAt AS String = \"\"\n"
            "SignedOutAt AS String = \"\"\n"
            "CONSTRUCTOR(user AS String, signedInAt AS String, signedOutAt AS String)\n"
            "SELF.User = user\n"
            "SELF.SignedInAt = signedInAt\n"
            "SELF.SignedOutAt = signedOutAt\n"
            "END CONSTRUCTOR\n"
            "SHARED FUNCTION whoLogQuery() AS ARCODBFUNCTION\n"
            "RETURN WhoLogQuery()\n"
            "END FUNCTION\n"
            "END CLASS\n"
            "FUNCTION IsoKey(moment)\n"
            "key = String.Replace(moment, \"-\", \"\")\n"
            "key = String.Replace(key, \"T\", \"\")\n"
            "key = String.Replace(key, \":\", \"\")\n"
            "RETURN NUMBER(key)\n"
            "END FUNCTION\n"
            "CLASS WhoLogQuery EXTENDS ARCODBFUNCTION\n"
            "CONSTRUCTOR()\n"
            "SELF.Name = \"who\"\n"
            "SELF.Description = \"who was signed in at a given time\"\n"
            "END CONSTRUCTOR\n"
            "FUNCTION Execute(db, args)\n"
            "momentKey = IsoKey(args[0])\n"
            "schema = ArcoDB.SchemaFor(db, \"SignInLog\")\n"
            "rows = []\n"
            "FOR log IN ArcoDB.Scan(db, schema)\n"
            "IF IsoKey(log.SignedInAt) <= momentKey AND IsoKey(log.SignedOutAt) >= momentKey THEN ignored = Array.Add(rows, log)\n"
            "NEXT\n"
            "RETURN {\"Ok\": TRUE, \"Error\": \"\", \"Rows\": rows}\n"
            "END FUNCTION\n"
            "END CLASS\n"
            "db = ArcoDB.Open(\"" + db_file.string() + "\")\n"
            "schema = ArcoDB.Schema(db, \"SignInLog\", [\"User\", \"SignedInAt\", \"SignedOutAt\"])\n"
            "PRINT ArcoDB.RegisterCommand(db, SignInLog.whoLogQuery())\n"
            "ignored = ArcoDB.Keep(db, schema, SignInLog(\"Ada\", \"2026-07-13T08:00\", \"2026-07-13T12:30\"))\n"
            "ignored = ArcoDB.Keep(db, schema, SignInLog(\"Grace\", \"2026-07-13T11:15\", \"2026-07-13T15:45\"))\n"
            "ignored = ArcoDB.Keep(db, schema, SignInLog(\"Miso\", \"2026-07-13T16:00\", \"2026-07-13T18:00\"))\n"
            "result = ArcoDB.RunCommand(db, \"who\", [\"2026-07-13T11:30\"])\n"
            "PRINT result.Ok\n"
            "PRINT LEN(result.Rows)\n"
            "first = result.Rows[0]\n"
            "second = result.Rows[1]\n"
            "PRINT first.User\n"
            "PRINT second.User\n"
            "PRINT LEN(ArcoDB.CommandNames(db))\n"
            "missing = ArcoDB.RunCommand(db, \"missing\", [])\n"
            "PRINT missing.Ok\n"
            "PRINT missing.Error CONTAINS \"Unknown\"\n"
            "info = ArcoDB.Inspect(db)\n"
            "PRINT info.Commands\n") == "TRUE\nTRUE\n2\nAda\nGrace\n1\nFALSE\nTRUE\n1\n", "registers and runs class-backed ArcoDB commands");
    }
    {
        const auto db_file = std::filesystem::temp_directory_path() / "arcodb-pointer-test.arcodb";
        std::filesystem::remove(db_file);
        std::filesystem::remove(db_file.string() + ".journal");
        require(run_capture(
            "#IMPORT \"arcodb\"\n"
            "CLASS Customer\n"
            "Name AS String = \"\"\n"
            "CONSTRUCTOR(name AS String)\n"
            "SELF.Name = name\n"
            "END CONSTRUCTOR\n"
            "END CLASS\n"
            "CLASS Order\n"
            "Number AS String = \"\"\n"
            "Customer = NULL\n"
            "CONSTRUCTOR(number AS String, customer)\n"
            "SELF.Number = number\n"
            "SELF.Customer = customer\n"
            "END CONSTRUCTOR\n"
            "FUNCTION CustomerName(db) AS String\n"
            "ptr = SELF.Customer\n"
            "customer = ptr.Resolve(db)\n"
            "IF ISNULL(customer) THEN RETURN \"missing\"\n"
            "RETURN customer.Name\n"
            "END FUNCTION\n"
            "END CLASS\n"
            "db = ArcoDB.Open(\"" + db_file.string() + "\")\n"
            "customerSchema = ArcoDB.Schema(db, \"Customer\", [\"Name\"])\n"
            "orderSchema = ArcoDB.Schema(db, \"Order\", [\"Number\", \"Customer\"])\n"
            "customerPtr = ArcoDB.PointerTo(db, customerSchema, Customer(\"Wanda\"))\n"
            "orderPtr = ArcoDB.PointerTo(db, orderSchema, Order(\"ORDER-1\", customerPtr))\n"
            "PRINT customerPtr.Label()\n"
            "PRINT orderPtr.Label()\n"
            "PRINT ArcoDB.PointerExists(db, customerPtr)\n"
            "order = orderPtr.Resolve(db)\n"
            "PRINT order.CustomerName(db)\n"
            "customer = ArcoDB.Resolve(db, customerPtr)\n"
            "PRINT customer.Name\n"
            "PRINT customerPtr.Forget(db)\n"
            "PRINT ArcoDB.PointerExists(db, customerPtr)\n"
            "orphan = orderPtr.Resolve(db)\n"
            "PRINT orphan.CustomerName(db)\n") == "Customer#1\nOrder#2\nTRUE\nWanda\nWanda\nTRUE\nFALSE\nmissing\n", "stores and resolves ArcoDB object pointers");
    }

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
    const auto trailing_escape_result = syntax_runtime.run_string("PRINT \"abc\\");
    require(!trailing_escape_result.ok && trailing_escape_result.error.find("unterminated string at line 1") != std::string::npos, "reports trailing string escapes without reading past input");
    const auto trailing_interpolated_escape_result = syntax_runtime.run_string("PRINT $\"abc\\");
    require(!trailing_interpolated_escape_result.ok && trailing_interpolated_escape_result.error.find("unterminated interpolated string at line 1") != std::string::npos, "reports trailing interpolated string escapes without reading past input");
    const auto runtime_error_result = syntax_runtime.run_string("PRINT missing_value\n");
    require(!runtime_error_result.ok, "reports runtime errors");
    require(runtime_error_result.error.find("undefined variable: missing_value") != std::string::npos, "keeps runtime error headline");
    require(runtime_error_result.error.find("runtime error at line 1, column 1") != std::string::npos, "adds runtime source location");
    require(runtime_error_result.error.find("PRINT missing_value\n^") != std::string::npos, "adds source line and caret to runtime errors");
    const auto loop_control_result = syntax_runtime.run_string("EXIT FOR\n");
    require(!loop_control_result.ok && loop_control_result.error.find("EXIT FOR outside FOR loop") != std::string::npos, "reports EXIT FOR outside loops");
    const auto do_control_result = syntax_runtime.run_string("EXIT DO\n");
    require(!do_control_result.ok && do_control_result.error.find("EXIT DO outside DO loop") != std::string::npos, "reports EXIT DO outside loops");
    require(run_capture("FOR i = 1 TO 3\nTRY\nCONTINUE FOR\nCATCH err\nPRINT \"caught\"\nEND TRY\nPRINT \"after\"\nNEXT\nPRINT \"done\"\n") == "done\n", "does not catch loop control in TRY blocks");

    arco::Runtime bit_vectors;
    std::ostringstream bit_vector_output;
    bit_vectors.set_output(bit_vector_output);
    const auto bit_vector_result = bit_vectors.run_string(
        "LET original AS BITVECTOR = BITS \"0001_1011\"\n"
        "copy = original\n"
        "flipped = Bits.Flip(copy, 0)\n"
        "PRINT LEN(original)\n"
        "PRINT original[3]\n"
        "PRINT Bits.Count(original)\n"
        "PRINT Bits.ToString(flipped)\n"
        "PRINT Bits.ToString(original)\n"
        "PRINT Bits.ToString(Bits.Set(original, 1, 1))\n"
        "PRINT Bits.ToString(Bits.Slice(original, 2, 4))\n"
        "PRINT Bits.ToString(Bits.Replace(original, 2, 3, BITS \"11\"))\n"
        "PRINT Bits.ToString(Bits.Reverse(original))\n"
        "PRINT Bits.ToString(original + BITS \"01\")\n"
        "PRINT Bits.ToString(Bits.FromArray([1, 0, 1]))\n"
        "PRINT Bits.ToArray(Bits.FromString(\"01\"))\n"
        "PRINT LEN(BITS \"\")\n");
    require(bit_vector_result.ok, "runs BITVECTOR literals and Bits.* operations");
    require(bit_vector_output.str() ==
                "8\n1\n4\n10011011\n00011011\n01011011\n0110\n0011011\n11011000\n0001101101\n101\n[0, 1]\n0\n",
            "preserves leading zeroes, immutable value semantics, and all BITVECTOR transformations");
    require(!bit_vectors.run_string("PRINT BITS \"0102\"\n").ok, "rejects invalid BITS literal characters");
    require(!bit_vectors.run_string("PRINT Bits.FromArray([0, 2])\n").ok, "rejects non-bit array elements");
    require(!bit_vectors.run_string("PRINT Bits.Get(BITS \"1\", 1)\n").ok, "bounds-checks BITVECTOR indexing");
    require(!bit_vectors.run_string("PRINT Bits.Get(BITS \"1\", 0.5)\n").ok, "requires integral BITVECTOR indices");
    require(!bit_vectors.run_string("#RUNTIME NONE\nPRINT BITS \"1\"\n").ok,
            "rejects BITVECTOR literals under #RUNTIME NONE until freestanding lowering exists");

    arco::Runtime slices;
    std::ostringstream slice_output;
    slices.set_output(slice_output);
    const auto slice_result = slices.run_string(
        "values = [0, 1, 2, 3, 4]\n"
        "PRINT values[1:4]\n"
        "PRINT values[:2]\n"
        "PRINT values[3:]\n"
        "PRINT values[-3:-1]\n"
        "PRINT values[::2]\n"
        "PRINT values[::-1]\n"
        "PRINT values[4:1:-2]\n"
        "PRINT \"Aé猫Z\"[1:3]\n"
        "PRINT \"Aé猫Z\"[::-1]\n"
        "PRINT Bits.ToString(BITS \"001101\"[1:5:2])\n"
        "outer = [[1], 2]\n"
        "copied = COPY outer\n"
        "copied[1] = 9\n"
        "copied[0][0] = 7\n"
        "PRINT outer\n"
        "PRINT copied\n"
        "record = {Name: \"Ada\", Nested: [1]}\n"
        "recordCopy = COPY record\n"
        "recordCopy.Name = \"Grace\"\n"
        "nestedCopy = recordCopy.Nested\n"
        "nestedCopy[0] = 8\n"
        "PRINT record.Name\n"
        "PRINT record.Nested[0]\n"
        "alias = values\n"
        "values[1:4] = [8, 9]\n"
        "PRINT values\n"
        "PRINT alias\n"
        "values[:0] = [-1]\n"
        "values[99:] = [10]\n"
        "PRINT values\n");
    require(slice_result.ok, "runs collection slices, COPY, and array slice assignment: " + slice_result.error);
    require(slice_output.str() ==
                "[1, 2, 3]\n[0, 1]\n[3, 4]\n[2, 3]\n[0, 2, 4]\n[4, 3, 2, 1, 0]\n[4, 2]\né猫\nZ猫éA\n01\n"
                "[[7], 2]\n[[7], 9]\nAda\n8\n[0, 8, 9, 4]\n[0, 8, 9, 4]\n[-1, 0, 8, 9, 4, 10]\n",
            "implements normalized slicing, Unicode code points, shallow copying, and resizing replacement");
    require(!slices.run_string("PRINT [1, 2][::0]\n").ok, "rejects a zero slice step");
    require(!slices.run_string("PRINT [1, 2][0.5:]\n").ok, "rejects non-integral slice bounds");
    require(!slices.run_string("value = 3\nPRINT value[:]\n").ok, "rejects invalid slice targets");
    require(!slices.run_string("values = [1, 2]\nvalues[:] = 3\n").ok,
            "requires an array replacement for slice assignment");
    require(!slices.run_string("values = [1, 2]\nvalues[::2] = [3]\n").ok,
            "rejects stepped slice assignment");
    const arco::Value copied_number = arco::shallow_copy_value(arco::Value(4));
    require(copied_number.is_number() && copied_number.as_number() == 4, "COPY leaves scalar values unchanged");

    arco::Runtime tuples;
    std::ostringstream tuple_output;
    tuples.set_output(tuple_output);
    const auto tuple_result = tuples.run_string(
        "FUNCTION Pair(a, b) AS TUPLE\n"
        "RETURN (a, b)\n"
        "END FUNCTION\n"
        "FUNCTION First(pair AS TUPLE)\n"
        "RETURN pair[0]\n"
        "END FUNCTION\n"
        "empty = ()\n"
        "single = (7,)\n"
        "pair = Pair(1, 2)\n"
        "PRINT empty\n"
        "PRINT single\n"
        "PRINT LEN(pair)\n"
        "PRINT pair[1]\n"
        "PRINT pair == (1, 2)\n"
        "PRINT pair[0:1]\n"
        "sum = 0\n"
        "FOR item IN pair\n"
        "sum += item\n"
        "NEXT\n"
        "PRINT sum\n"
        "PRINT First(pair)\n"
        "LET (left, right) = Pair(3, 4)\n"
        "PRINT left\n"
        "PRINT right\n"
        "(left, right) = (right, left)\n"
        "PRINT left\n"
        "PRINT right\n"
        "(left, right) = [8, 9]\n"
        "PRINT left\n"
        "PRINT right\n");
    require(tuple_result.ok, "runs tuple construction, returns, typing, iteration, slicing, and destructuring: " + tuple_result.error);
    require(tuple_output.str() == "()\n(7,)\n2\n2\nTRUE\n(1,)\n3\n1\n3\n4\n4\n3\n8\n9\n",
            "preserves tuple identity, grouping distinction, and atomic swaps");
    require(!tuples.run_string("value = (1, 2)\nvalue[0] = 9\n").ok, "rejects tuple indexed assignment");
    require(!tuples.run_string("value = (1, 2)\nArray.Add(value, 3)\n").ok, "rejects tuple use in array mutators");
    tuples.run_string("arityA = 11\narityB = 12\n");
    const auto tuple_arity_error = tuples.run_string("(arityA, arityB) = (1,)\n");
    require(!tuple_arity_error.ok && tuple_arity_error.error.find("expected 2, received 1") != std::string::npos,
            "reports exact destructuring arity mismatch");
    require(tuples.get_global("arityA").as_number() == 11 && tuples.get_global("arityB").as_number() == 12,
            "validates and captures destructuring before assigning any target");

    arco::Runtime callables;
    std::ostringstream callable_output;
    callables.set_output(callable_output);
    const auto callable_result = callables.run_string(
        "FUNCTION Score(item)\n"
        "RETURN item.Score\n"
        "END FUNCTION\n"
        "FUNCTION Plus(a, b = 2) AS Number\n"
        "RETURN a + b\n"
        "END FUNCTION\n"
        "FUNCTION Invoke(fn AS CALLABLE, value)\n"
        "RETURN fn(value)\n"
        "END FUNCTION\n"
        "CLASS Bias\n"
        "Offset = 0\n"
        "FUNCTION Key(item)\n"
        "RETURN item.Score + SELF.Offset\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "calls = []\n"
        "FUNCTION Track(value)\n"
        "Array.Add(calls, value)\n"
        "RETURN value\n"
        "END FUNCTION\n"
        "items = [{Name: \"b\", Score: 2}, {Name: \"a\", Score: 1}, {Name: \"c\", Score: 2}]\n"
        "key = ADDRESSOF Score\n"
        "plus = ADDRESSOF Plus\n"
        "PRINT TYPEOF(key)\n"
        "PRINT key({Score: 5})\n"
        "PRINT plus(3)\n"
        "PRINT Invoke(key, {Score: 7})\n"
        "ranked = Array.SortBy(items, key)\n"
        "first = ranked[0]\n"
        "second = ranked[1]\n"
        "third = ranked[2]\n"
        "PRINT first.Name\n"
        "PRINT second.Name\n"
        "PRINT third.Name\n"
        "descending = Array.SortBy(items, key, TRUE)\n"
        "descFirst = descending[0]\n"
        "descSecond = descending[1]\n"
        "descThird = descending[2]\n"
        "PRINT descFirst.Name\n"
        "PRINT descSecond.Name\n"
        "PRINT descThird.Name\n"
        "minItem = Array.MinBy(items, key)\n"
        "maxItem = Array.MaxBy(items, key)\n"
        "PRINT minItem.Name\n"
        "PRINT maxItem.Name\n"
        "Array.SortBy([3, 1, 2], ADDRESSOF Track)\n"
        "PRINT LEN(calls)\n"
        "bias = Bias()\n"
        "bias.Offset = 10\n"
        "biasKey = ADDRESSOF bias.Key\n"
        "PRINT biasKey({Score: 4})\n");
    require(callable_result.ok, "runs first-class CALLABLE values and keyed ordering: " + callable_result.error);
    require(callable_output.str() == "Callable\n5\n5\n7\na\nb\nc\nb\nc\na\na\nb\n3\n14\n",
            "implements callable invocation, default args, stable SortBy, extrema, and bound methods");
    require(!callables.run_string("PRINT ADDRESSOF Missing.Function\n").ok, "rejects unknown ADDRESSOF targets");
    require(!callables.run_string("Array.SortBy([1], \"Score\")\n").ok, "requires a CALLABLE sort key");
    require(!callables.run_string("FUNCTION Mixed(value)\nIF value == 1 THEN\nRETURN 1\nELSE\nRETURN \"two\"\nEND IF\nEND FUNCTION\nArray.SortBy([1, 2], ADDRESSOF Mixed)\n").ok,
            "rejects mixed key types for keyed ordering");
    require(!callables.run_string("FUNCTION Same(value)\nRETURN value\nEND FUNCTION\nPRINT Array.MinBy([], ADDRESSOF Same)\n").ok,
            "rejects extrema over empty arrays");

    arco::Runtime ranges;
    std::ostringstream range_output;
    ranges.set_output(range_output);
    const auto range_result = ranges.run_string(
        "squares = [i * i FOR i IN Range(1, 5)]\n"
        "PRINT squares\n"
        "evens = [j FOR j IN Range(7) IF j % 2 == 0]\n"
        "PRINT evens\n"
        "TRY\n"
        "PRINT j\n"
        "CATCH err\n"
        "PRINT err.Type\n"
        "END TRY\n"
        "values = [10, 20, 30]\n"
        "shifted = [value + 1 FOR value IN values IF value > 10]\n"
        "PRINT shifted\n"
        "r = Range(1, 7, 2)\n"
        "PRINT TYPEOF(r)\n"
        "PRINT r\n"
        "PRINT LEN(r)\n"
        "PRINT r[0]\n"
        "PRINT r[2]\n"
        "PRINT r CONTAINS 3\n"
        "PRINT 4 IN r\n"
        "total = 0\n"
        "FOR i IN Range(5)\n"
        "total += i\n"
        "NEXT\n"
        "PRINT total\n"
        "\n");
    require(range_result.ok, "runs RANGE values and array comprehensions: " + range_result.error);
    require(range_output.str() ==
                "[1, 4, 9, 16]\n[0, 2, 4, 6]\nRuntimeError\n[21, 31]\nRange\nRange(1, 7, 2)\n3\n1\n5\nTRUE\nFALSE\n10\n",
            "implements range length/indexing/membership/iteration and scoped comprehensions");
    require(!ranges.run_string("PRINT Range(1, 3, 0)\n").ok, "rejects zero range steps");
    require(!ranges.run_string("PRINT Range(1.5)\n").ok, "rejects non-integral range bounds");
    require(!ranges.run_string("PRINT [x FOR x IN 3]\n").ok, "rejects non-iterable comprehensions");

    arco::Runtime limited;
    limited.set_limits({2});
    const auto limit_result = limited.run_string("WHILE TRUE\nPRINT 1\nWEND\n");
    require(!limit_result.ok, "enforces instruction limit");

    arco::Runtime unauthorized_source_limit;
    const auto unauthorized_limit_result = unauthorized_source_limit.run_string(
        "#INSTRUCTION_LIMIT 1000\nPRINT \"no\"\n");
    require(!unauthorized_limit_result.ok &&
                unauthorized_limit_result.error.find("not authorized") != std::string::npos,
            "embedded runtimes reject source instruction-limit requests by default");

    arco::Runtime authorized_source_limit;
    authorized_source_limit.set_instruction_limit_policy(true);
    std::ostringstream authorized_limit_output;
    authorized_source_limit.set_output(authorized_limit_output);
    const auto authorized_limit_result = authorized_source_limit.run_string(
        "#INSTRUCTION_LIMIT 1000\nPRINT \"authorized\"\n");
    require(authorized_limit_result.ok && authorized_limit_output.str() == "authorized\n",
            "authorized hosts apply source instruction-limit requests");
    require(authorized_source_limit.compile_metadata().instruction_limit == 1000,
            "preserves #INSTRUCTION_LIMIT in compile metadata");

    arco::Runtime capped_source_limit;
    capped_source_limit.set_instruction_limit_policy(true, 500);
    const auto capped_limit_result = capped_source_limit.run_string(
        "#INSTRUCTION_LIMIT 1000\nPRINT \"no\"\n");
    require(!capped_limit_result.ok && capped_limit_result.error.find("exceeds host maximum 500") != std::string::npos,
            "host hard maximum rejects larger source requests without clamping");

    arco::Runtime overridden_source_limit;
    overridden_source_limit.set_instruction_limit_policy(true);
    overridden_source_limit.set_instruction_limit_override(1000);
    const auto overridden_limit_result = overridden_source_limit.run_string(
        "#INSTRUCTION_LIMIT 1\nPRINT \"override\"\n");
    require(overridden_limit_result.ok, "operator instruction-limit override supersedes the source request");

    for (const std::string bad_limit : {"", "0", "-1", "1.5", "name", "9007199254740992"}) {
        arco::Runtime malformed_limit;
        malformed_limit.set_instruction_limit_policy(true);
        const auto malformed_limit_result = malformed_limit.run_string(
            "#INSTRUCTION_LIMIT " + bad_limit + "\nPRINT \"no\"\n");
        require(!malformed_limit_result.ok && malformed_limit_result.error.find("#INSTRUCTION_LIMIT") != std::string::npos,
                "rejects malformed #INSTRUCTION_LIMIT operand: " + bad_limit);
    }
    arco::Runtime duplicate_limit;
    duplicate_limit.set_instruction_limit_policy(true);
    require(!duplicate_limit.run_string(
                "#INSTRUCTION_LIMIT 10\n#INSTRUCTION_LIMIT 20\nPRINT \"no\"\n").ok,
            "rejects duplicate #INSTRUCTION_LIMIT directives");
    arco::Runtime freestanding_limit;
    freestanding_limit.set_instruction_limit_policy(true);
    const auto freestanding_limit_result = freestanding_limit.run_string(
        "#INSTRUCTION_LIMIT 10\n#RUNTIME NONE\n");
    require(!freestanding_limit_result.ok && freestanding_limit_result.error.find("hosted execution") != std::string::npos,
            "rejects #INSTRUCTION_LIMIT under #RUNTIME NONE regardless of directive order");

    ArcoRuntime* c_runtime = arco_create_runtime();
    require(c_runtime != nullptr, "creates C runtime");
    require(arco_run_string(c_runtime, "PRINT \"C API\"\n") == 0, "runs through C API");
    arco_destroy_runtime(c_runtime);


    return 0;
}
