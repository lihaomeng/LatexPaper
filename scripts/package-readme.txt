LightOverLeaf Full green-directory distribution / Windows 10/11 x64

Run LightOverLeaf.exe directly from this directory.
Keep all bundled executables, DLLs, locales, application resources and runtime/miktex in place.

This is the only supported edition. It includes the source-built MiKTeX runtime,
does not use a system TeX installation, and never extracts an application payload
when it starts. It does not invoke 7-Zip, tar, CMD or PowerShell at runtime.

Normal application data, MiKTeX caches, logs, build snapshots, PDF files and
SyncTeX files are still written to application-data or project build locations.
These files are runtime state, not extracted application files.

Missing TeX packages are not downloaded silently while compiling. Third-party
notices are in licenses/. Test this unsigned development distribution on a clean,
offline supported Windows machine before external release.
