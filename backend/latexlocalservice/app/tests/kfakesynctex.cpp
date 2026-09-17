#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int wmain(int argc, wchar_t** argv)
{
    if (argc < 4)
        return 9;
    std::ifstream syncTex(fs::current_path() / L"document.synctex.gz", std::ios::binary);
    std::string mode((std::istreambuf_iterator<char>(syncTex)),
        std::istreambuf_iterator<char>());
    std::ifstream pdf(fs::current_path() / L"document.pdf", std::ios::binary);
    std::string signature(5, '\0');
    pdf.read(signature.data(), static_cast<std::streamsize>(signature.size()));
    if (!syncTex || signature != "%PDF-")
        return 8;
    if (mode.starts_with("SLOW"))
    {
        Sleep(10000);
        return 0;
    }
    if (mode.starts_with("FAIL"))
        return 2;
    if (mode.starts_with("HUGE"))
    {
        std::cout << std::string(70000, 'x') << std::flush;
        return 0;
    }
    if (mode.starts_with("BAD"))
    {
        std::cout << "Input:C:\\\\outside.tex\nLine:1\nColumn:1\n";
        return 0;
    }
    const std::wstring command = argv[1];
    if (command == L"view" && argc == 6 && std::wstring(argv[2]) == L"-i" &&
        std::wstring(argv[4]) == L"-o")
    {
        std::cout << "SyncTeX result begin\nPage:3\nx:12.5\ny:44.25\nSyncTeX result end\n";
        return 0;
    }
    if (command == L"edit" && argc == 4 && std::wstring(argv[2]) == L"-o")
    {
        std::cout << "SyncTeX result begin\nInput:sections\\main.tex\nLine:17\nColumn:4\n"
            "SyncTeX result end\n";
        return 0;
    }
    return 7;
}
