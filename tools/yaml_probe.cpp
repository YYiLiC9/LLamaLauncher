#include <iostream>
#include <string>
#include "core/yaml.h"
#include "core/util.h"

int main() {
    std::wstring path =
        L"C:\\Users\\y1974\\AppData\\Roaming\\LlamaLauncher\\configs\\20260925-141157.yaml";
    std::wstring text = util::readTextFile(path);
    yaml::Node root;
    std::wstring err;
    bool ok = yaml::parse(text, root, err);

    const yaml::Node& params = root[L"params"];
    std::cout << "parse ok=" << ok << " rootSize=" << root.size()
              << " params.isSeq=" << params.isSeq()
              << " params.size=" << params.size() << std::endl;

    // name 是否取到（中文，只打长度）
    std::wstring nm = root.str(L"name");
    std::cout << "name len=" << nm.size() << std::endl;

    int n = 0;
    for (const yaml::Node& item : params.items()) {
        std::wstring flag = item.str(L"flag");
        std::wstring val = item.str(L"value");
        // 只输出 ASCII 安全内容：flag 转 char，值打长度
        std::string f;
        for (wchar_t c : flag) f.push_back((char)c);
        std::cout << "  [" << n << "] flag=" << f << " valueLen=" << val.size()
                  << " enabled=" << item.boolean(L"enabled", true) << std::endl;
        if (++n > 6) break;
    }
    return 0;
}
