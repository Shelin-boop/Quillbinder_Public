# Packaging: building the downloads, per system

The program's source code is one for every system (`src`, `plugins`, `addons`, `resources`); what differs per system is
how the downloads are built and checked. That is here, in one folder per system:

| Folder | What is in it |
|--------|---------------|
| `linux/` | The AppImage (`build-appimage.sh`), the program update (`build-update.sh`), the add-ons (`build-addons.sh`), the full version (`build-full-appimage.sh`), the end-to-end checks with the real AppImage (`e2e-appimage.sh`, `e2e-old-version.sh`), the desktop file, the AppStream text, the file type, and the licenses of the AppImage's own runtime (`licenses/`). |
| `windows/` | The build on GitHub's Windows machine (`win-ci.sh`, started by `.github/workflows/windows.yml`), the Windows add-ons (`win-addons.sh`), whisper.cpp for Speech Transcription (`win-whisper.sh`), the spelling dictionaries (`win-dictionaries.sh`), the checks of the program and the downloads (`win-check.sh`, `win-check-layout.sh`), the setup programs and their checks (`quillbinder-setup.iss` for Inno Setup, `win-setup.sh`; 0.90.4), the tests there (`win-tests.sh`), and the license list of the Windows program (`collect-licenses-windows.py`). |
| `android/` | Planned. |

Shared by all systems: the key that signs the packages (`quillbinder-signing-public.pem`, here), and in `tools/` the
package maker (`make-package.py`), the release text (`release-notes.py`), the download page (`download-page.py`), the
publishing scripts, the legal check and the test runner. `packaging/tools/` is a download folder of the AppImage
build (not part of the source).
