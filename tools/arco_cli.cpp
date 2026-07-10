#include "arco/runtime.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: arco_cli <script.bas>\n";
        return 2;
    }

    std::ifstream input(argv[1]);
    if (!input) {
        std::cerr << "could not open " << argv[1] << '\n';
        return 1;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();

    arco::Runtime runtime;
    const auto result = runtime.run_string(buffer.str());
    if (!result.ok) {
        std::cerr << result.error << '\n';
        return 1;
    }
    return 0;
}

