#include <windows.h>
#include <fstream>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) return 9;
    std::ifstream input(argv[argc - 1], std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::cout.put(static_cast<char>(0xff));
    if (std::wstring(argv[0]).find(L"latexmk.exe") != std::wstring::npos)
        std::cout << "\nlauncher:latexmk";
    std::cout << "\nmain.tex:3: fake diagnostic\n"
        "main.tex:4: LaTeX Warning: fake warning\n" << std::flush;
    if (content.find("LARGE_INVALID") != std::string::npos)
    {
        const std::string invalid(360000, static_cast<char>(0xff));
        std::cout.write(invalid.data(), static_cast<std::streamsize>(invalid.size())) << std::flush;
    }
    if (content.find("SLOW") != std::string::npos) Sleep(5000);
    if (content.find("ERROR") != std::string::npos) return 2;
    std::ofstream pdf("main.pdf", std::ios::binary); pdf << "%PDF-1.4\n%%EOF\n";
    std::ofstream synctex("main.synctex.gz", std::ios::binary);
    synctex << "LOLSYNC1\nF|main.tex|1|1|10|10\n";
    return 0;
}
