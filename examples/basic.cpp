import std;
import mcpplibs.xpkg;

int main() {
    std::println("=== mcpplibs.xpkg basic example ===");
    mcpplibs::xpkg::Package p;
    p.name = "example";
    std::println("Package name: {}", p.name);
    return 0;
}
