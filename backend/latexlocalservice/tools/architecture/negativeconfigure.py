"""Prove a real target_link_libraries violation fails CMake Configure."""
from pathlib import Path
import subprocess
import sys
import tempfile

service = Path(__file__).resolve().parents[2]
root = service.parents[1]
with tempfile.TemporaryDirectory(prefix='lol-architecture-') as directory:
    fixture = Path(directory)
    (fixture / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.21)
project(InvalidArchitecture LANGUAGES NONE)
set(PROJECT_SOURCE_DIR "{service.as_posix()}")
set(LOL_REPO_ROOT "{root.as_posix()}")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include("{service.as_posix()}/cmake/ArchitectureRules.cmake")
add_library(lol_document_domain INTERFACE)
lol_register(lol_document_domain domain document)
add_library(lol_platform_qt INTERFACE)
lol_register(lol_platform_qt platform qt)
target_link_libraries(lol_document_domain INTERFACE lol_platform_qt)
lol_export_architecture()
''', encoding='utf-8')
    result = subprocess.run(['cmake', '-S', str(fixture), '-B', str(fixture / 'build')],
                            capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=45)
    message = result.stdout + result.stderr
    expected = 'Forbidden dependency: lol_document_domain -> lol_platform_qt'
    if result.returncode == 0 or expected not in message:
        print(message)
        sys.exit('Invalid target graph did not fail for the expected reason')
    print('Real CMake configure rejected Domain -> Qt platform')
