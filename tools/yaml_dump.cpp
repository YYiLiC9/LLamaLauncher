#include <iostream>
#include <string>
#include "core/yaml.h"
#include "core/util.h"

// Dump every param flag/value the parser yields for a real config file, so a
// silently dropped entry can be told apart from a buildArgs bug.
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "usage: yaml_dump <config.yaml>" << std::endl;
        return 2;
    }
    const std::wstring path = util::toUtf16(std::string(argv[1]));
    std::wstring text = util::readTextFile(path);
    std::cout << "file chars: " << text.size() << std::endl;

    yaml::Node root;
    std::wstring err;
    if (!yaml::parse(text, root, err)) {
        std::cout << "PARSE FAILED: ";
        std::cout << util::toUtf8(err) << std::endl;
        return 1;
    }
    const yaml::Node& params = root[L"params"];
    if (!params.isSeq()) {
        std::cout << "params not a seq" << std::endl;
        return 1;
    }
    std::cout << "param count: " << params.size() << std::endl;
    for (const yaml::Node& item : params.items()) {
        std::cout << "  ["
                  << util::toUtf8(item.str(L"flag"))
                  << "] value='"
                  << util::toUtf8(item.str(L"value"))
                  << "' en="
                  << (item.boolean(L"enabled", true) ? 1 : 0)
                  << std::endl;
    }
    return 0;
}
