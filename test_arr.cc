#include <iostream>
#include <string>
int main() {
    std::string s;
    int empty = 5;
    s += "0123456789"[empty];
    std::cout << s << std::endl;
    return 0;
}
