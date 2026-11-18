/*
 * Minimal demo of mlvlib helpers (print shim + size literals).
 */

#include "mlvlib/helper.hpp"

using namespace mlvlib;

int main()
{
    println("mlvlib helper demo");
    println("  4_KB = {} bytes", 4_KB);
    println("  1_MB = {} bytes", 1_MB);
    return 0;
}
