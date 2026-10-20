// Test stand-in for vswhere.exe: names a Visual Studio with the x64 C++ tools (the folder the test creates)
#include <cstdio>
int main() {
    printf("C:\\FakeVS\\BuildTools\r\n");
    return 0;
}
