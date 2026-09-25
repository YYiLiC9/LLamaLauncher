#include <iostream>
#include <string>
#include "core/yaml.h"
#include "core/util.h"

// 批次 1/2 修复的回归探针：只输出 ASCII 安全内容（长度/布尔/ASCII flag）。
// 用 cl 手动编译（环境拼装见 build_dev.sh），注意 heredoc 会吞反斜杠，
// 本文件必须用 Write 工具维护。
int main() {
    int failed = 0;
    auto check = [&](const char* what, bool good) {
        std::cout << (good ? "  PASS " : "  FAIL ") << what << std::endl;
        if (!good) ++failed;
    };

    // 1) 空值键 + 缩进块（批次 1 根因：params: 曾被当成内联标量）
    {
        yaml::Node root; std::wstring err;
        yaml::parse(L"params:\n  - flag: -m\n    value: abc\n  - flag: -c\n    value: 9\n", root, err);
        const yaml::Node& p = root[L"params"];
        check("empty-key block -> seq", p.isSeq() && p.size() == 2);
        check("  item0.value", p.at(0).str(L"value") == L"abc");
        check("  item1.value", p.at(1).str(L"value") == L"9");
    }
    // 2) 序列值后的注释（批次 2：此前注释文本会进入参数值）
    {
        yaml::Node root; std::wstring err;
        yaml::parse(L"items:\n  - flag: --port\n    value: 8080 # my port\n  - 9090 # plain\n", root, err);
        const yaml::Node& p = root[L"items"];
        check("seq item comment stripped", p.isSeq() && p.size() == 2);
        check("  map item value", p.at(0).str(L"value") == L"8080");
        check("  plain item value", p.at(1).asString() == L"9090");
    }
    // 3) 值中合法的 '#'（颜色/密钥类）不被误剥
    {
        yaml::Node root; std::wstring err;
        yaml::parse(L"items:\n  - value: \"#ff0000\"\n", root, err);
        const yaml::Node& p = root[L"items"];
        check("quoted # survives", p.isSeq() && p.at(0).str(L"value") == L"#ff0000");
    }
    // 4) 序列项块标量 "- key: |"（批次 2：此前块体恒为空）
    {
        yaml::Node root; std::wstring err;
        yaml::parse(L"items:\n  - notes: |\n      line1\n      line2\n  - flag: x\n", root, err);
        const yaml::Node& p = root[L"items"];
        check("seq block scalar -> map", p.isSeq() && p.size() == 2);
        std::wstring notes = p.at(0).str(L"notes");
        check("  block body", notes.find(L"line1") != std::wstring::npos &&
                                 notes.find(L"line2") != std::wstring::npos);
        check("  next item intact", p.at(1).str(L"flag") == L"x");
    }
    // 5) 映射键块标量回归（重构公共函数后不得退化）
    {
        yaml::Node root; std::wstring err;
        yaml::parse(L"notes: |\n  hello\n  world\nid: 7\n", root, err);
        std::wstring notes = root.str(L"notes");
        check("map block scalar", notes.find(L"hello") != std::wstring::npos &&
                                  notes.find(L"world") != std::wstring::npos);
        check("  following key intact", root.str(L"id") == L"7");
    }
    // 6) enabled/boolean 引号往返
    {
        yaml::Node root; std::wstring err;
        yaml::parse(L"params:\n  - flag: --mlock\n    value: \"on\"\n    enabled: \"false\"\n", root, err);
        const yaml::Node& p = root[L"params"];
        check("quoted boolean", p.at(0).boolean(L"enabled", true) == false);
        check("  quoted value", p.at(0).str(L"value") == L"on");
    }
    // 7) 空文件与纯注释
    {
        yaml::Node root; std::wstring err;
        yaml::parse(L"", root, err);
        check("empty doc", root.isNull() || root.size() == 0);
        yaml::parse(L"# only a comment\n", root, err);
        check("comment-only doc", root.isNull() || root.size() == 0);
    }
    // 8) 应用实际使用的形态
    {
        yaml::Node root; std::wstring err;
        yaml::parse(L"version: 1\nname: n\nnotes: \"\"\nparams:\n  - flag: -c\n    value: 8\n", root, err);
        check("app roundtrip shape", root[L"params"].isSeq() && root.str(L"name") == L"n");
    }

    std::cout << (failed == 0 ? "ALL PASS" : "FAILED") << " (" << failed << " failures)" << std::endl;
    return failed == 0 ? 0 : 1;
}
