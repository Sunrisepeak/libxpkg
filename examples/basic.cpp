#include <iostream>
#include <string>
import mcpplibs.xpkg;

int main() {
    std::cout << "=== mcpplibs.xpkg basic example ===" << std::endl;
    mcpplibs::xpkg::Package p;
    p.name = "example";
    std::cout << "Package name: " << p.name << std::endl;
    return 0;
}
