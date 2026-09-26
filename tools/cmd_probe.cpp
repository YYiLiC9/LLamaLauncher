#include <iostream>
#include <string>
#include "core/store.h"
#include "core/yaml.h"
#include "core/util.h"

// Feed the real parser output through the real buildDisplayCommand so a
// dropped command tail can be pinned on the loader or on buildArgs.
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::wstring path = util::toUtf16(std::string(argv[1]));
    std::wstring text = util::readTextFile(path);

    yaml::Node root;
    std::wstring err;
    if (!yaml::parse(text, root, err)) return 1;

    store::Config cfg;
    cfg.id = L"probe";
    const yaml::Node& params = root[L"params"];
    for (const yaml::Node& item : params.items()) {
        store::Param p;
        p.flag = item.str(L"flag");
        p.value = item.str(L"value");
        p.group = item.str(L"group", L"custom");
        p.desc = item.str(L"desc");
        p.custom = item.boolean(L"custom", false);
        p.enabled = item.boolean(L"enabled", true);
        if (!p.flag.empty()) cfg.params.push_back(std::move(p));
    }
    std::cout << "loaded params: " << cfg.params.size() << std::endl;

    std::wstring cmd = store::buildDisplayCommand(cfg, L"llama-server.exe");
    std::cout << "cmd chars: " << cmd.size() << std::endl;
    std::cout << "has -fa:   " << (cmd.find(L"-fa") != std::wstring::npos) << std::endl;
    std::cout << "has -ngl:  " << (cmd.find(L"-ngl") != std::wstring::npos) << std::endl;
    std::cout << "has -ctk:  " << (cmd.find(L"-ctk") != std::wstring::npos) << std::endl;
    std::cout << "has jinja: " << (cmd.find(L"--jinja") != std::wstring::npos) << std::endl;
    std::wcout << cmd << std::endl;
    return 0;
}
