#include "arco/runtime.hpp"
#include "arco/shell.hpp"
#include "arco/calling_convention.hpp"
#include "arco/utf16.hpp"
#include "arco_c_api.h"

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

std::string run_shell_capture(const std::string& code) {
    arco::Runtime runtime;
    arco::shell::register_shell_builtins(runtime);
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
        "#IMPORT \"commons\"\n"
        "router = Commons.Router()\n"
        "router = Commons.AddRoute(router, \"GET\", \"/communities/:id\", \"ShowCommunity\", \"Community page\")\n"
        "match = Commons.MatchRoute(router, \"get\", \"/communities/photo\")\n"
        "PRINT match.Ok\n"
        "PRINT match.Handler\n"
        "PRINT match.Params.id\n"
        "response = Commons.Text(200, \"ok\")\n"
        "PRINT Object.Get(response.Headers, \"Content-Type\")\n"
        "validation = Commons.Validation()\n"
        "validation = Commons.RequireField(validation, \"\", \"title\")\n"
        "validation = Commons.MaxLength(validation, \"abcdef\", \"body\", 3)\n"
        "PRINT validation.Ok\n"
        "PRINT LEN(validation.Errors)\n"
        "feed = Commons.Feed([Commons.FeedItem(\"post\", \"p1\", \"Hello\", \"From a community you joined\")], \"Local\", \"Chronological\")\n"
        "PRINT feed.CaughtUp\n"
        "PRINT Object.Get(feed.Items[0], \"Reason\")\n"
        "caught = Commons.CaughtUp()\n"
        "PRINT caught.Message\n"
        "report = Commons.Report(\"post:p1\", \"user:ada\", \"Spam\")\n"
        "action = Commons.ModerationAction(report, \"spam\", \"remove_content\", \"Matched spam rule\", \"mod:grace\")\n"
        "PRINT action.RuleId\n"
        "PRINT action.AppealAvailable\n"
        "audit = Commons.AuditEntry(\"mod:grace\", \"remove\", \"post:p1\", \"spam\")\n"
        "PRINT audit.Verb\n") == "TRUE\nShowCommunity\nphoto\ntext/plain; charset=utf-8\nFALSE\n2\nFALSE\nFrom a community you joined\nYou're caught up.\nspam\nTRUE\nremove\n", "imports commons framework helpers for routes, feeds, and moderation records");
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
        require(run_shell_capture(
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
            "restored.email = \"wanda.goodburger@arcology.example\"\n"
            "PRINT ArcoDB.Replace(db, schema, id, restored)\n"
            "PRINT ISNULL(ArcoDB.RecallBy(db, schema, \"email\", \"wanda@email.com\"))\n"
            "updatedByEmail = ArcoDB.RecallBy(db, schema, \"email\", \"wanda.goodburger@arcology.example\")\n"
            "PRINT updatedByEmail.email\n"
            "PRINT ArcoDB.Write(db)\n"
            "reopened = ArcoDB.Open(path)\n"
            "loadedSchema = ArcoDB.SchemaFor(reopened, \"Customer\")\n"
            "loaded = ArcoDB.Recall(reopened, loadedSchema, id)\n"
            "PRINT loaded.email\n"
            "reopenedByEmail = ArcoDB.RecallBy(reopened, loadedSchema, \"email\", \"wanda.goodburger@arcology.example\")\n"
            "PRINT reopenedByEmail.name\n"
            "info = ArcoDB.Inspect(reopened)\n"
            "PRINT info.Active\n"
            "PRINT info.Catalogs\n"
            "PRINT info.Dirty\n"
            "PRINT ArcoDB.Forget(reopened, id)\n"
            "PRINT ArcoDB.Count(reopened)\n"
            "PRINT ISNULL(ArcoDB.RecallBy(reopened, loadedSchema, \"email\", \"wanda.goodburger@arcology.example\"))\n"
            "beforeCompact = ArcoDB.Inspect(reopened)\n"
            "PRINT beforeCompact.Records\n"
            "PRINT beforeCompact.Tombstones\n"
            "compacted = ArcoDB.Compact(reopened)\n"
            "PRINT compacted.Removed\n"
            "afterCompact = ArcoDB.Inspect(reopened)\n"
            "PRINT afterCompact.Records\n"
            "PRINT afterCompact.Tombstones\n"
            "PRINT ISNULL(ArcoDB.Recall(reopened, loadedSchema, id))\n") == "1\nWanda\nwanda@email.com\nWanda\nTRUE\nTRUE\nwanda.goodburger@arcology.example\nTRUE\nwanda.goodburger@arcology.example\nWanda\n1\n2\nFALSE\nTRUE\n0\nTRUE\n1\n1\n1\n0\n0\nTRUE\n", "stores, recalls, catalogs, replaces, writes, reopens, forgets, and compacts ArcoDB objects");
        require(std::filesystem::exists(db_file), "writes ArcoDB file");
    }
    {
        const auto db_file = std::filesystem::temp_directory_path() / "arcology-v01a-runtime-test.arcodb";
        std::filesystem::remove(db_file);
        std::filesystem::remove(db_file.string() + ".journal");
        require(run_shell_capture(
            "#IMPORT \"arcology\"\n"
            "app = Arcology.Open(\"" + db_file.string() + "\")\n"
            "PRINT Arcology.Version()\n"
            "manifest = Arcology.Manifest()\n"
            "PRINT manifest.Name\n"
            "ada = Arcology.CreateUser(app, \"Ada\", \"Ada Lovelace\", \"Steward\", \"moderator\")\n"
            "grace = Arcology.CreateUser(app, \"Grace\", \"Grace Hopper\")\n"
            "photo = Arcology.CreateCommunity(app, \"Photography\", \"Photography\", \"Local photo walks\")\n"
            "PRINT ada.Ok\n"
            "PRINT grace.Value.Handle\n"
            "PRINT photo.Value.Slug\n"
            "joined = Arcology.JoinCommunity(app, \"Grace\", \"photography\")\n"
            "PRINT joined.Ok\n"
            "post = Arcology.Post(app, \"photography\", \"Grace\", \"Sunset walk\", \"Meet by the library\", [\"local\"])\n"
            "event = Arcology.Event(app, \"photography\", \"Open studio\", \"2026-08-12 18:00\", \"Community center\")\n"
            "feed = Arcology.FeedForUser(app, \"grace\")\n"
            "PRINT feed.Title\n"
            "PRINT feed.Explanation CONTAINS \"No hidden\"\n"
            "PRINT LEN(feed.Items)\n"
            "PRINT Object.Get(feed.Items[0], \"Reason\") CONTAINS \"photography\"\n"
            "report = Arcology.ReportContent(app, \"post:\" + STRING(post.Value.__id), \"Ada\", \"Spam\", \"test report\")\n"
            "PRINT report.Value.Status\n"
            "action = Arcology.Moderate(app, \"post:\" + STRING(post.Value.__id), \"spam\", \"remove_content\", \"transparent removal\", \"Ada\")\n"
            "PRINT action.Value.Action\n"
            "after = Arcology.CommunityFeed(app, \"photography\")\n"
            "PRINT LEN(after.Items)\n"
            "info = Arcology.Inspect(app)\n"
            "PRINT info.Users\n"
            "PRINT info.Communities\n"
            "PRINT info.Posts\n"
            "PRINT info.Reports\n"
            "PRINT Arcology.Save(app)\n"
            "reopened = Arcology.Open(\"" + db_file.string() + "\")\n"
            "loaded = Arcology.User(reopened, \"grace\")\n"
            "PRINT loaded.DisplayName\n") == "ARCOLOGY_0.1A\nThe Arcology Commons\nTRUE\ngrace\nphotography\nTRUE\nYour Commons\nTRUE\n2\nTRUE\nopen\nremove_content\n1\n2\n1\n1\n1\nTRUE\nGrace Hopper\n", "runs Arcology v0.1a community, feed, report, moderation, and persistence helpers");
        require(std::filesystem::exists(db_file), "writes Arcology ArcoDB file");
    }
    {
        const auto db_file = std::filesystem::temp_directory_path() / "arcology-static-runtime-test.arcodb";
        const auto site_dir = std::filesystem::temp_directory_path() / "arcology-static-runtime-test-site";
        std::filesystem::remove(db_file);
        std::filesystem::remove(db_file.string() + ".journal");
        std::filesystem::remove_all(site_dir);
        require(run_shell_capture(
            "#IMPORT \"arcology\"\n"
            "app = Arcology.Open(\"" + db_file.string() + "\")\n"
            "ignored = Arcology.CreateUser(app, \"Ada\", \"Ada Lovelace\")\n"
            "ignored = Arcology.CreateCommunity(app, \"Photography\", \"Photography\", \"Local <photos> & walks\")\n"
            "ignored = Arcology.JoinCommunity(app, \"Ada\", \"photography\")\n"
            "ignored = Arcology.Post(app, \"photography\", \"ada\", \"Sunset & shadows\", \"Meet <outside>\")\n"
            "result = Arcology.ExportSite(app, \"" + site_dir.string() + "\")\n"
            "PRINT result.Ok\n"
            "PRINT result.Files\n"
            "index = File.ReadText(Arcology.SitePath(result.Path, \"index.html\"))\n"
            "community = File.ReadText(Arcology.SitePath(result.Path, \"community-photography.html\"))\n"
            "style = File.ReadText(Arcology.SitePath(result.Path, \"style.css\"))\n"
            "PRINT index CONTAINS \"The Arcology Commons\"\n"
            "PRINT community CONTAINS \"Local &lt;photos&gt; &amp; walks\"\n"
            "PRINT community CONTAINS \"Sunset &amp; shadows\"\n"
            "PRINT style CONTAINS \"topbar\"\n") == "TRUE\n3\nTRUE\nTRUE\nTRUE\nTRUE\n", "exports Arcology v0.1a static HTML site");
        require(std::filesystem::exists(site_dir / "index.html"), "writes Arcology static index");
        std::filesystem::remove_all(site_dir);
    }
    {
        const auto db_file = std::filesystem::temp_directory_path() / "arcodb-compact-test.arcodb";
        std::filesystem::remove(db_file);
        std::filesystem::remove(db_file.string() + ".journal");
        require(run_shell_capture(
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
        require(run_shell_capture(
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
        require(run_shell_capture(
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
        require(run_shell_capture(
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

    arco::Runtime shell_runtime;
    arco::shell::set_color_enabled(false);
    arco::shell::register_shell_builtins(shell_runtime);
    std::ostringstream shell_output;
    shell_runtime.set_output(shell_output);
    const auto shell_result = shell_runtime.run_string("FUNCTION Shout(text)\nRETURN Upper(text)\nEND FUNCTION\nPRINT Shout(\"shell\")\nresult = RUN(\"printf shell-test\")\nPRINT result.Output\nPRINT result.ExitCode\nPRINT host.osname()\ncaps = System.Capabilities()\nPRINT caps.OS == Host.OSName()\nPRINT TYPEOF(caps.GUI)\nPRINT System.CommandExists(\"printf\")\nTRY\nSystem.Launch(\"definitely-not-a-real-arcosh-launch-command\")\nCATCH err\nPRINT err.Message CONTAINS \"command not found\"\nEND TRY\nPRINT TYPEOF(Host.Printers())\nPRINT TYPEOF(Printer.List())\nPRINT TYPEOF(Printer.Default())\nPRINT LEN(process.list()) > 0\nPRINT Process.Exists(\"definitely-not-a-real-arcosh-test-process\")\nPRINT file.exists(\"../readme.md\")\nFOR file IN File.Find(\"../*.md\")\nPRINT file CONTAINS \"readme\"\nNEXT\nPRINT help.topic(\"run\") CONTAINS \"ExitCode\"\nPRINT Help.Topic(\"if\") CONTAINS \"END IF\"\nPRINT Help.Topic(\"login\") CONTAINS \"login shell\"\nPRINT Help.Topic(\"sudo\") CONTAINS \"sudo -v\"\nPRINT Help.Topic(\"system\") CONTAINS \"System.Capabilities\"\nPRINT Help.Topic(\"printers\") CONTAINS \"Printer.PrintFile\"\nPRINT Help.Topic(\"search color\") CONTAINS \"colors\"\nPRINT TUI.Box(\"TEST\", \"alpha\\nbeta\") CONTAINS \"+\"\nPRINT TUI.ThemeBox(\"scroll\", \"QUEST\", \"Patch the server\") CONTAINS \"~\"\nPRINT TUI.ThemeRule(\"circuit\", \"BUS\", 20) CONTAINS \"BUS\"\nPRINT TUI.Scroll(\"Ancient Manual\", \"Read HELP tui\") CONTAINS \"Ancient Manual\"\nPRINT Array.Contains(TUI.ThemeNames(), \"scroll\")\nPRINT TUI.Rule(\"NEXT\", 20)\nPRINT TUI.Header(\"OPS\") CONTAINS \"ArcoSH terminal workspace\"\nPRINT TUI.Badge(\"ok\")\nPRINT TUI.Status(\"database\", \"ok\", \"reachable\")\nPRINT TUI.Progress(\"backup\", 5, 10, 10)\nPRINT TUI.List(\"Items\", [\"one\", \"two\"]) CONTAINS \"* one\"\nPRINT TUI.Menu(\"Menu\", [\"scan\", \"quit\"]) CONTAINS \"1. scan\"\nPRINT TUI.KeyValues(\"Host\", {\"OS\": Host.OSName(), \"Name\": Host.Hostname()}) CONTAINS \"OS\"\nPRINT TUI.Table([\"Service\", \"State\"], [[\"web\", \"ok\"], [\"db\", \"warn\"]]) CONTAINS \"Service\"\nPRINT TUI.Table([\"Name\", \"State\"], [{\"Name\": \"web\", \"State\": \"ok\"}]) CONTAINS \"web\"\nPRINT TUI.Clear() CONTAINS \"[2J\"\nPRINT TUI.Cursor(2, 3) CONTAINS \"[2;3H\"\nPRINT LEN(ArcoSH.ExecutablePath()) > 0\nFOR topic IN Help.Topics()\nPRINT topic CONTAINS \"basic\"\nNEXT\nPRINT arcosh.setprompt(\"{shell}:{cwd:short}:{status}> \")\nPRINT ArcoSH.GetPrompt()\nPRINT color.green(\"ok\")\nPRINT Color.Paint(\"warn\", \"yellow\")\n");
    require(shell_result.ok, shell_result.error);
    require(shell_output.str().find("SHELL") != std::string::npos, "runs user functions in shell runtime");
    require(shell_output.str().find("shell-test") != std::string::npos, "returns RUN output object");
    require(shell_output.str().find("{shell}:{cwd:short}:{status}> ") != std::string::npos, "configures shell prompt from ArcoBASIC");
    require(shell_output.str().find("ok") != std::string::npos, "prints uncolored color helper output when color is disabled");
    require(shell_output.str().find("NEXT") != std::string::npos, "renders TUI helpers from scripts");
    require(shell_output.str().find("[OK] database") != std::string::npos, "renders expanded TUI status helpers");
    require(shell_output.str().find("backup [#####.....] 50%") != std::string::npos, "renders TUI progress bars");
    require(shell_runtime.call_host_function("GUI.Backend", {}).is_string(), "reports the desktop GUI backend");
    require(shell_runtime.call_host_function("GUI.Available", {}).is_bool(), "reports desktop GUI availability");
    arco::Runtime network_runtime;
    const auto network_available = network_runtime.call_host_function("Network.Available", {});
    require(network_available.is_bool(), "reports network helper availability");
    require(network_runtime.call_host_function("Network.UrlEncode", {"hello world!"}).to_string() == "hello%20world%21", "URL-encodes text");
    require(network_runtime.call_host_function("Network.UrlDecode", {"hello%20world%21"}).to_string() == "hello world!", "URL-decodes text");
    arco::Value query_values(arco::Value::Object{{"q", "arco basic"}, {"page", 2}});
    require(network_runtime.call_host_function("Network.QueryString", {query_values}).to_string() == "page=2&q=arco%20basic", "builds query strings");
    const auto resolved_localhost = network_runtime.call_host_function("Net.ResolveDNS", {"localhost"});
    require(resolved_localhost.is_object(), "returns DNS response objects");
    require(resolved_localhost.get_property("Host").to_string() == "localhost", "preserves DNS host names");
    require(resolved_localhost.get_property("Addresses").is_array(), "returns DNS address arrays");
    if (network_available.truthy()) {
        const auto network_file = std::filesystem::temp_directory_path() / "arco-network-test.txt";
        write_text(network_file, "hello");
        const auto network_response = network_runtime.call_host_function("Network.Get", {"file://" + network_file.string()});
        require(network_response.is_object(), "returns network response objects");
        require(network_response.get_property("Ok").truthy(), "fetches local file URLs through networking");
        require(network_response.get_property("Body").to_string() == "hello", "returns network response bodies");
    }
#ifndef _WIN32
    {
        auto tcp_server = start_tcp_test_server();
        const auto connect_result = network_runtime.call_host_function("Net.TcpConnect", {"127.0.0.1", tcp_server.port});
        require(connect_result.is_object() && connect_result.get_property("Ok").truthy(), "connects TCP clients");
        const int client = static_cast<int>(connect_result.get_property("Client").as_number());
        const auto send_result = network_runtime.call_host_function("Net.TcpSend", {client, "ping"});
        require(send_result.get_property("Ok").truthy(), "sends TCP client data");
        const auto read_result = network_runtime.call_host_function("Net.TcpRead", {client, 64});
        require(read_result.get_property("Ok").truthy(), "reads TCP client data");
        require(read_result.get_property("Data").to_string() == "tcp-ok", "receives TCP server response");
        require(network_runtime.call_host_function("Net.TcpClose", {client}).truthy(), "closes TCP clients");
        tcp_server.thread.join();
    }
    {
        const auto site_dir = std::filesystem::temp_directory_path() / "arco-web-static-test";
        std::filesystem::remove_all(site_dir);
        std::filesystem::create_directories(site_dir);
        write_text(site_dir / "index.html", "<h1>Arcology test</h1>");
        write_text(site_dir / "style.css", "body{color:#202124}");
        const int port = free_loopback_port();
        arco::Value serve_result;
        std::thread server_thread([&] {
            arco::Runtime server_runtime;
            serve_result = server_runtime.call_host_function("Web.ServeStatic", {site_dir.string(), port, "127.0.0.1", 1});
        });
        const std::string response = http_get_loopback(port, "/");
        server_thread.join();
        require(serve_result.is_object() && serve_result.get_property("Ok").truthy(), "serves static HTTP roots");
        require(serve_result.get_property("Requests").as_number() == 1.0, "counts static HTTP requests");
        require(response.find("HTTP/1.1 200 OK") != std::string::npos, "returns HTTP 200 for static index");
        require(response.find("Content-Type: text/html; charset=utf-8") != std::string::npos, "returns HTML content type");
        require(response.find("Arcology test") != std::string::npos, "returns static file body");
        std::filesystem::remove_all(site_dir);
    }
#endif
    const auto listed_files = shell_runtime.call_host_function("File.List", {".."});
    require(listed_files.is_array() && !listed_files.as_array().empty() && listed_files.as_array()[0].is_object(), "lists structured directory entries");
    require(shell_runtime.call_host_function("Path.Home", {}).is_string(), "reports the user home directory");

    arco::Runtime mouse_runtime;
    arco::shell::register_shell_builtins(mouse_runtime);
    std::ostringstream mouse_output;
    mouse_runtime.set_output(mouse_output);
    const auto mouse_result = mouse_runtime.run_string(
        "event = TUI.ParseEvent(\"\033[<0;12;7M\")\n"
        "PRINT event.Type\nPRINT event.Action\nPRINT event.Button\nPRINT event.X\nPRINT event.Y\n"
        "PRINT TUI.HitTest(event, 10, 7, 5, 1)\n"
        "PRINT TUI.MouseEnable() CONTAINS \"?1006h\"\n"
        "PRINT TUI.MouseDisable() CONTAINS \"?1006l\"\n");
    require(mouse_result.ok, mouse_result.error);
    require(mouse_output.str() == "mouse\npress\nleft\n12\n7\nTRUE\nTRUE\nTRUE\n", "parses and hit-tests SGR mouse events");
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
    std::istringstream repl_input("HELP run\nHELP if\nHELP bitwise\nHELP try\nHELP tutorial\nHELP jobs\nHELP stdlib\nHELP doctor\nHELP tui\nHELP sudo\nHELP search profile\nCOLOR ON\nCOLOR OFF\nprintf bare-shell\nprinf recovered\noops printf\nEXIT\n");
    arco::shell::repl(shell_runtime, repl_input, repl_output, false);
    require(repl_output.str().find("RUN command helper") != std::string::npos, "prints REPL help topics");
    require(repl_output.str().find("IF statement") != std::string::npos, "prints syntax help topics");
    require(repl_output.str().find("Bitwise operations") != std::string::npos, "prints bitwise help topic");
    require(repl_output.str().find("TRY / CATCH") != std::string::npos, "prints try help topic");
    require(repl_output.str().find("Interactive tutorial") != std::string::npos, "prints tutorial help topic");
    require(repl_output.str().find("Background jobs") != std::string::npos, "prints jobs help topic");
    require(repl_output.str().find("Standard library modules") != std::string::npos, "prints stdlib help topic");
    require(repl_output.str().find("ArcoSH doctor") != std::string::npos, "prints doctor help topic");
    require(repl_output.str().find("ARCO MANUAL") != std::string::npos, "renders retro help panels");
    require(repl_output.str().find("Retro TUI helpers") != std::string::npos, "prints TUI help topic");
    require(repl_output.str().find("Elevated permissions") != std::string::npos, "prints sudo help topic");
    require(repl_output.str().find("Search: profile") != std::string::npos, "searches help topics");
    require(repl_output.str().find("bare-shell") != std::string::npos, "runs bare shell commands in REPL");
    require(repl_output.str().find("Unknown command") != std::string::npos, "reports unknown commands in REPL");
    require(repl_output.str().find("recovered") != std::string::npos, "recovers unknown commands with oops");

    std::ostringstream tutorial_output;
    std::istringstream tutorial_input(
        "\n"
        "hint\n"
        "skip\n"
        "name = \"admin\" : PRINT $\"hello {name}\"\n"
        "\n"
        "printf service-ok\n"
        "result = RUN(\"printf captured\") : PRINT result.Output : PRINT result.ExitCode\n"
        "\n"
        "PRINT File.Exists(\"readme.md\")\n"
        "\n"
        "PRINT Host.OSName()\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n");
    shell_runtime.set_output(tutorial_output);
    const auto tutorial_result = arco::shell::run_tutorial(shell_runtime, tutorial_input, tutorial_output, "");
    require(tutorial_result.ok, tutorial_result.error);
    require(std::filesystem::exists("../tutorials/arcosh_sysadmin.abas") || std::filesystem::exists("tutorials/arcosh_sysadmin.abas"), "ships tutorial as a viewable ArcoBASIC file");
    require(tutorial_output.str().find("ArcoSH Sysadmin Tutorial") != std::string::npos, "runs tutorial as ArcoBASIC");
    require(tutorial_output.str().find("hello admin") != std::string::npos, "tutorial executes ArcoBASIC practice");
    require(tutorial_output.str().find("hint, skip, quit") != std::string::npos, "tutorial exposes prompt controls");
    require(tutorial_output.str().find("Skipped.") != std::string::npos, "tutorial skip runs expected command");
    require(tutorial_output.str().find("service-ok") != std::string::npos, "tutorial executes shell practice");
    require(tutorial_output.str().find("host/process inspection") != std::string::npos, "tutorial covers expanded sysadmin material");
    require(tutorial_output.str().find("Tutorial complete") != std::string::npos, "tutorial reaches completion");

    std::ostringstream game_tutorial_output;
    std::istringstream game_tutorial_input(
        "\n"
        "secret = 7 : turns = 0 : PRINT $\"secret is {secret}\"\n"
        "guess = 4 : IF guess < 7 THEN PRINT \"too low\" ELSE PRINT \"not low\"\n"
        "\n"
        "\n"
        "\n"
        "3\n"
        "8\n"
        "7\n");
    shell_runtime.set_output(game_tutorial_output);
    const auto game_tutorial_result = arco::shell::run_tutorial(shell_runtime, game_tutorial_input, game_tutorial_output, "game");
    require(game_tutorial_result.ok, game_tutorial_result.error);
    require(game_tutorial_output.str().find("Game tutorial complete") != std::string::npos, "runs game tutorial");
    require(game_tutorial_output.str().find("correct in 3 turns") != std::string::npos, "game tutorial accepts interactive guesses");

    std::ostringstream tool_tutorial_output;
    const auto tutorial_home = std::filesystem::temp_directory_path() / "arcosh-tutorial-home";
    std::filesystem::remove_all(tutorial_home);
    std::filesystem::create_directories(tutorial_home);
    setenv("ARCOSH_HOME", tutorial_home.string().c_str(), 1);
    std::istringstream tool_tutorial_input(
        "\n"
        "report = Path.Join(ArcoSH.Home(), \"tutorial-report.txt\") : PRINT report\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n");
    shell_runtime.set_output(tool_tutorial_output);
    const auto tool_tutorial_result = arco::shell::run_tutorial(shell_runtime, tool_tutorial_input, tool_tutorial_output, "tool");
    require(tool_tutorial_result.ok, tool_tutorial_result.error);
    require(tool_tutorial_output.str().find("Tool tutorial complete") != std::string::npos, "runs tool tutorial");
    require(std::filesystem::exists(tutorial_home / "tutorial-report.txt"), "tool tutorial writes report");

    std::ostringstream adventure_hub_output;
    std::istringstream adventure_hub_input("\n");
    shell_runtime.set_output(adventure_hub_output);
    const auto adventure_hub_result = arco::shell::run_tutorial(shell_runtime, adventure_hub_input, adventure_hub_output, "adventure");
    require(adventure_hub_result.ok, adventure_hub_result.error);
    require(adventure_hub_output.str().find("ArcoAdventures ready") != std::string::npos, "runs ArcoAdventures hub tutorial");
    require(adventure_hub_output.str().find("adventure1") != std::string::npos, "ArcoAdventures hub lists missions");

    std::ostringstream adventure_badge_output;
    std::istringstream adventure_badge_input(
        "\n"
        "skip\n"
        "skip\n"
        "\n"
        "blue\n");
    shell_runtime.set_output(adventure_badge_output);
    const auto adventure_badge_result = arco::shell::run_tutorial(shell_runtime, adventure_badge_input, adventure_badge_output, "adventure1");
    require(adventure_badge_result.ok, adventure_badge_result.error);
    require(adventure_badge_output.str().find("Badge Bureau") != std::string::npos, "runs first ArcoAdventure");
    require(adventure_badge_output.str().find("Mission complete") != std::string::npos, "first ArcoAdventure completes");

    std::ostringstream adventure_snack_output;
    std::istringstream adventure_snack_input(
        "\n"
        "skip\n"
        "skip\n"
        "\n");
    shell_runtime.set_output(adventure_snack_output);
    const auto adventure_snack_result = arco::shell::run_tutorial(shell_runtime, adventure_snack_input, adventure_snack_output, "adventure2");
    require(adventure_snack_result.ok, adventure_snack_result.error);
    require(adventure_snack_output.str().find("Snackstorm") != std::string::npos, "runs second ArcoAdventure");
    require(adventure_snack_output.str().find("Mission complete") != std::string::npos, "second ArcoAdventure completes");

    std::ostringstream adventure_evidence_output;
    std::istringstream adventure_evidence_input(
        "\n"
        "skip\n"
        "\n");
    shell_runtime.set_output(adventure_evidence_output);
    const auto adventure_evidence_result = arco::shell::run_tutorial(shell_runtime, adventure_evidence_input, adventure_evidence_output, "adventure3");
    require(adventure_evidence_result.ok, adventure_evidence_result.error);
    require(adventure_evidence_output.str().find("Evidence Locker") != std::string::npos, "runs third ArcoAdventure");
    require(adventure_evidence_output.str().find("Mission complete") != std::string::npos, "third ArcoAdventure completes");
    require(std::filesystem::exists(tutorial_home / "silent-levels-capsule.acpy"), "third ArcoAdventure writes packed evidence capsule");

    std::ostringstream stopped_tutorial_output;
    std::istringstream stopped_tutorial_input(
        "\n"
        "PRINT \"hello admin\"\n"
        "name = \"admin\" : PRINT $\"hello {name}\"\n"
        "\n"
        "printf service-ok\n"
        "result = RUN(\"printf captured\") : PRINT result.Output : PRINT result.ExitCode\n"
        "\n"
        "PRINT File.Exists(\"readme.md\")\n"
        "\n"
        "PRINT Host.OSName()\n");
    shell_runtime.set_output(stopped_tutorial_output);
    const auto stopped_tutorial_result = arco::shell::run_tutorial(shell_runtime, stopped_tutorial_input, stopped_tutorial_output, "");
    require(stopped_tutorial_result.ok, stopped_tutorial_result.error);
    require(stopped_tutorial_output.str().find("Tutorial stopped.") != std::string::npos, "tutorial stops on input EOF instead of autofilling");
    require(stopped_tutorial_output.str().find("Tutorial complete") == std::string::npos, "tutorial does not complete after EOF");

    std::ostringstream quit_tutorial_output;
    std::istringstream quit_tutorial_input(
        "\n"
        "quit\n");
    shell_runtime.set_output(quit_tutorial_output);
    const auto quit_tutorial_result = arco::shell::run_tutorial(shell_runtime, quit_tutorial_input, quit_tutorial_output, "");
    require(quit_tutorial_result.ok, quit_tutorial_result.error);
    require(quit_tutorial_output.str().find("Tutorial stopped.") != std::string::npos, "tutorial quit command stops cleanly");
    require(quit_tutorial_output.str().find("Tutorial complete") == std::string::npos, "tutorial quit command does not complete lesson");

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
    const auto mod_source = std::filesystem::temp_directory_path() / "arcosh-test-mod.abas";
    write_text(mod_source, "PRINT \"mod loaded\"\nmod_value = \"from mod\"\n");
    setenv("ARCOSH_HOME", profile_home.string().c_str(), 1);

    arco::Runtime profile_runtime;
    arco::shell::register_shell_builtins(profile_runtime);
    std::ostringstream profile_output;
    profile_runtime.set_output(profile_output);
    const auto install_mod_result = profile_runtime.call_host_function("Mod.Install", {mod_source.string(), "test-mod"});
    require(install_mod_result.get_property("Name").to_string() == "test-mod", "installs user-space ArcoSH mods");
    require(profile_runtime.call_host_function("Mod.Activate", {"test-mod"}).get_property("Active").truthy(), "activates user-space ArcoSH mods");
    require(profile_runtime.call_host_function("Mod.List", {}).as_array().size() == 1, "lists installed user-space ArcoSH mods");
    const auto startup_result = arco::shell::load_startup(profile_runtime, profile_output, false);
    require(startup_result.ok, startup_result.error);
    require(std::filesystem::exists(profile_home / "plugins") && std::filesystem::exists(profile_home / "mods") && std::filesystem::exists(profile_home / "scripts"), "creates ArcoSH profile directories");
    require(profile_output.str().find("rc loaded") != std::string::npos, "loads rc from ARCOSH_HOME");
    require(profile_output.str().find("plugin loaded") != std::string::npos, "loads plugins from ARCOSH_HOME");
    require(profile_output.str().find("mod loaded") != std::string::npos, "loads active user-space mods from ARCOSH_HOME");

    const auto load_complete_script = std::filesystem::temp_directory_path() / "arcosh-load-complete.abas";
    write_text(load_complete_script, "PRINT \"complete\"\n");
    const std::string load_complete_prefix = load_complete_script.string().substr(0, load_complete_script.string().size() - 5);
    std::istringstream profile_repl_input("PRINT ArcoSH.Home()\ncomplete lo\ncomplete he\ncomplete help ed\ncomplete admin\ncomplete @adm\ncomplete RUN admin\ncomplete LOAD " + load_complete_prefix + "\nadmin-status first second\nsource " + (profile_home / "set-helper.abas").string() + " sourced\nPRINT helper_value\nalias hi=printf alias-ok\ntype hi\nwhich admin-status\nhi\nunalias hi\nEXIT\n");
    arco::shell::repl(profile_runtime, profile_repl_input, profile_output, false);
    require(profile_output.str().find(profile_home.string()) != std::string::npos, "exposes ArcoSH profile paths to scripts");
    require(profile_output.str().find("load") != std::string::npos, "completes LOAD command");
    require(profile_output.str().find("help") != std::string::npos, "completes shell builtins");
    require(profile_output.str().find("editing") != std::string::npos, "completes help topics");
    require(profile_output.str().find("admin-status") != std::string::npos, "completes profile scripts");
    require(profile_output.str().find("@admin-status") != std::string::npos, "completes profile scripts after @");
    require(profile_output.str().find(load_complete_script.string()) != std::string::npos, "completes script paths after LOAD");
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

    const auto launch_script = std::filesystem::temp_directory_path() / "arcosh-launch-script.abas";
    write_text(launch_script, "PRINT \"launch-script\"\nPRINT Script.Name\nFOR arg IN Args\nPRINT arg\nNEXT\n");
    std::ostringstream at_launch_output;
    std::istringstream at_launch_input("@" + launch_script.string() + " one \"two words\"\nEXIT\n");
    shell_runtime.set_output(at_launch_output);
    arco::shell::repl(shell_runtime, at_launch_input, at_launch_output, false);
    require(at_launch_output.str().find("launch-script") != std::string::npos, "runs scripts with @script.abas launch syntax");
    require(at_launch_output.str().find("arcosh-launch-script.abas") != std::string::npos, "sets Script.Name for @script launches");
    require(at_launch_output.str().find("one") != std::string::npos && at_launch_output.str().find("two words") != std::string::npos, "passes Args for @script launches");

    std::ostringstream run_launch_output;
    std::istringstream run_launch_input("RUN " + launch_script.string() + " three four\nEXIT\n");
    shell_runtime.set_output(run_launch_output);
    arco::shell::repl(shell_runtime, run_launch_input, run_launch_output, false);
    require(run_launch_output.str().find("launch-script") != std::string::npos, "runs scripts with RUN script.abas launch syntax");
    require(run_launch_output.str().find("three") != std::string::npos && run_launch_output.str().find("four") != std::string::npos, "passes Args for RUN script launches");

    const auto loaded_script = std::filesystem::temp_directory_path() / "arcosh-loaded-script.abas";
    write_text(loaded_script, "PRINT \"loaded-script\"\nPRINT Script.Name\n");
    std::ostringstream loaded_repl_output;
    std::istringstream loaded_repl_input("LOAD " + loaded_script.string() + "\nLIST\nRUN\nNEW\nLIST\nEXIT\n");
    shell_runtime.set_output(loaded_repl_output);
    arco::shell::repl(shell_runtime, loaded_repl_input, loaded_repl_output, false);
    require(loaded_repl_output.str().find("Loaded " + loaded_script.string()) != std::string::npos, "loads scripts into the REPL with LOAD");
    require(loaded_repl_output.str().find("PRINT \"loaded-script\"") != std::string::npos, "lists loaded scripts");
    require(loaded_repl_output.str().find("loaded-script") != std::string::npos, "runs loaded scripts with RUN");
    require(loaded_repl_output.str().find("arcosh-loaded-script.abas") != std::string::npos, "sets Script.Name for loaded scripts");

    std::ostringstream load_run_repl_output;
    std::istringstream load_run_repl_input("LOAD " + loaded_script.string() + "; RUN\nEXIT\n");
    shell_runtime.set_output(load_run_repl_output);
    arco::shell::repl(shell_runtime, load_run_repl_input, load_run_repl_output, false);
    require(load_run_repl_output.str().find("loaded-script") != std::string::npos, "supports LOAD script.abas; RUN shortcut");

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
        "CLASS Box\n"
        "Value = 5\n"
        "FUNCTION Double()\n"
        "RETURN SELF.Value * 2\n"
        "END FUNCTION\n"
        "END CLASS\n"
        "box = Box()\n"
        "PRINT box.Double()\n"
        "PRINT Twice(4)\n"
        "IF Twice(2) == 4 THEN\n"
        "PRINT \"block if\"\n"
        "END IF\n"
        "EXIT\n");
    shell_runtime.set_output(multiline_repl_output);
    arco::shell::repl(shell_runtime, multiline_repl_input, multiline_repl_output, false);
    require(multiline_repl_output.str().find("undefined variable: missing_value") != std::string::npos, "runs unnumbered multiline TRY blocks in REPL");
    require(multiline_repl_output.str().find("\n8\n") != std::string::npos, "runs unnumbered multiline FUNCTION blocks in REPL");
    require(multiline_repl_output.str().find("\n10\n") != std::string::npos, "runs unnumbered multiline CLASS blocks in REPL");
    require(multiline_repl_output.str().find("block if") != std::string::npos, "runs unnumbered multiline IF blocks in REPL");

    arco::Runtime limited;
    limited.set_limits({2});
    const auto limit_result = limited.run_string("WHILE TRUE\nPRINT 1\nWEND\n");
    require(!limit_result.ok, "enforces instruction limit");

    ArcoRuntime* c_runtime = arco_create_runtime();
    require(c_runtime != nullptr, "creates C runtime");
    require(arco_run_string(c_runtime, "PRINT \"C API\"\n") == 0, "runs through C API");
    arco_destroy_runtime(c_runtime);

    // Microsoft x64 calling convention (Packet WP-005, docs/systems/calling-conventions.md).
    require(arco::systems::assign_argument_locations(0).empty(), "zero arguments assigns no locations");
    {
        const auto one = arco::systems::assign_argument_locations(1);
        require(one.size() == 1 && one[0].in_register && one[0].register_name == "RCX",
                "first argument assigns to RCX");
    }
    {
        const auto two = arco::systems::assign_argument_locations(2);
        require(two.size() == 2 && two[0].register_name == "RCX" && two[1].register_name == "RDX",
                "two arguments assign to RCX, RDX in order");
    }
    {
        const auto four = arco::systems::assign_argument_locations(4);
        require(four.size() == 4 && four[0].register_name == "RCX" && four[1].register_name == "RDX" &&
                    four[2].register_name == "R8" && four[3].register_name == "R9",
                "four arguments assign to RCX, RDX, R8, R9");
    }
    {
        const auto five = arco::systems::assign_argument_locations(5);
        require(five.size() == 5 && !five[4].in_register && five[4].stack_offset_bytes == 40,
                "fifth argument spills to the stack at shadow-space-plus-return-address offset 40");
    }
    {
        const auto six = arco::systems::assign_argument_locations(6);
        require(!six[5].in_register && six[5].stack_offset_bytes == 48,
                "sixth argument follows the fifth at offset 48");
    }
    require(arco::systems::integer_return_register() == "RAX", "integer/pointer return register is RAX");
    require(arco::systems::kShadowSpaceBytes == 32, "shadow space is always 32 bytes");
    require(arco::systems::kStackAlignmentAtCallBytes == 16, "stack must be 16-byte aligned at CALL");
    {
        const auto& callee_saved = arco::systems::callee_saved_registers();
        const auto& caller_saved = arco::systems::caller_saved_registers();
        bool disjoint = true;
        for (const auto& reg : callee_saved) {
            for (const auto& other : caller_saved) {
                if (reg == other) {
                    disjoint = false;
                }
            }
        }
        require(disjoint, "callee-saved and caller-saved register sets do not overlap");
    }

    // UTF-16 constant encoding (Packet WP-007, docs/systems/utf16-encoding.md).
    {
        const auto units = arco::systems::encode_utf16_null_terminated("Hello from ArcoBASIC");
        const std::u16string expected = u"Hello from ArcoBASIC";
        require(units.size() == expected.size() + 1, "hello string encodes to 21 code units plus a null terminator");
        bool matches = true;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (units[i] != expected[i]) {
                matches = false;
            }
        }
        require(matches, "hello string's UTF-16 code units match their ASCII values exactly");
        require(units.back() == u'\0', "hello string's UTF-16 encoding ends with a null terminator");
    }
    {
        const auto empty_units = arco::systems::encode_utf16_null_terminated("");
        require(empty_units.size() == 1 && empty_units[0] == u'\0', "empty string encodes to just a null terminator");
    }
    {
        // U+00E9 (LATIN SMALL LETTER E WITH ACUTE), UTF-8: 0xC3 0xA9 -- a single UTF-16 code unit.
        const auto units = arco::systems::encode_utf16_null_terminated("\xC3\xA9");
        require(units.size() == 2 && units[0] == 0x00E9 && units[1] == u'\0',
                "a BMP character outside ASCII encodes to a single UTF-16 code unit");
    }
    {
        // U+1F600 (GRINNING FACE), UTF-8: 0xF0 0x9F 0x98 0x80 -- a UTF-16 surrogate pair.
        const auto units = arco::systems::encode_utf16_null_terminated("\xF0\x9F\x98\x80");
        require(units.size() == 3 && units[0] == 0xD83D && units[1] == 0xDE00 && units[2] == u'\0',
                "a character outside the BMP encodes to a correct UTF-16 surrogate pair");
    }
    const auto encoding_rejects = [](const std::string& text) {
        try {
            (void)arco::systems::encode_utf16_null_terminated(text);
            return false;
        } catch (const std::exception&) {
            return true;
        }
    };
    require(encoding_rejects(std::string("bad\0null", 8)), "embedded NUL byte is rejected");
    require(encoding_rejects("\xFF"), "an invalid UTF-8 lead byte is rejected");
    require(encoding_rejects("\xC3"), "a truncated UTF-8 sequence is rejected");
    require(encoding_rejects("\xC3\x28"), "an invalid UTF-8 continuation byte is rejected");
    require(encoding_rejects("\xED\xA0\x80"), "a UTF-8 encoding of a surrogate code point is rejected");

    return 0;
}
