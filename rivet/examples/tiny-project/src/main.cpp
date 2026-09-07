#include "math.hpp"
#include <cstdio>

int main() {
    int result = add(2, 3);
    std::printf("%d\n", result);
    return result == 5 ? 0 : 1;
}
