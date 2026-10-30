// UWP (Xbox One Dev Mode) platform layer: the pieces of the port that need WinRT instead of Win32.
// Built only with NMR_UWP (see uwp/README.md). The libraries reach these through the hooks declared in
// uwp/patches/*.patch (nmr_uwp_* and recompui::file::uwp), so none of them depends on C++/WinRT.
#if defined(NMR_UWP)

#include <cstdio>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "SDL.h"

#include <winrt/base.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.UI.Core.h>

#include <windows.h>
#include <fileapifromapp.h>

#include "uwp_platform.h"

using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Pickers;
using namespace winrt::Windows::UI::Core;

extern SDL_Window* window;

namespace {
    using SingleCallback = std::function<void(bool, const std::filesystem::path&)>;
    using MultipleCallback = std::function<void(bool, const std::list<std::filesystem::path>&)>;

    // Picked files are copied here: a StorageFile from the picker is only readable through WinRT, but librecomp and
    // the mod installer read paths with the C runtime.
    constexpr wchar_t ImportFolderName[] = L"imports";

    // Runs fn on the CoreWindow's thread (pickers must be shown from it). Never blocks the caller.
    void run_on_ui_thread(std::function<void()> fn) {
        CoreDispatcher dispatcher = CoreApplication::MainView().CoreWindow().Dispatcher();
        if (dispatcher.HasThreadAccess()) {
            fn();
            return;
        }
        dispatcher.RunAsync(CoreDispatcherPriority::Normal, [fn = std::move(fn)]() { fn(); });
    }

    FileOpenPicker make_picker() {
        FileOpenPicker picker;
        picker.ViewMode(PickerViewMode::List);
        picker.SuggestedStartLocation(PickerLocationId::ComputerFolder);
        picker.FileTypeFilter().Append(L"*");
        return picker;
    }

    // Copies each picked file into LocalState/imports, then reports the copies' paths (or failure) to callback.
    void import_files(std::vector<StorageFile> files, MultipleCallback callback) {
        if (files.empty()) {
            callback(false, {});
            return;
        }
        ApplicationData::Current().LocalFolder().CreateFolderAsync(ImportFolderName, CreationCollisionOption::OpenIfExists).Completed(
            [files = std::move(files), callback = std::move(callback)](IAsyncOperation<StorageFolder> const& op, AsyncStatus status) {
                if (status != AsyncStatus::Completed) {
                    callback(false, {});
                    return;
                }
                StorageFolder folder = op.GetResults();
                struct State {
                    std::mutex mutex;
                    size_t remaining;
                    std::list<std::filesystem::path> paths;
                    MultipleCallback callback;
                };
                auto state = std::make_shared<State>();
                state->remaining = files.size();
                state->callback = callback;
                for (const StorageFile& file : files) {
                    file.CopyAsync(folder, file.Name(), NameCollisionOption::ReplaceExisting).Completed(
                        [state](IAsyncOperation<StorageFile> const& copy, AsyncStatus copy_status) {
                            std::unique_lock lock(state->mutex);
                            if (copy_status == AsyncStatus::Completed) {
                                state->paths.emplace_back(std::wstring(copy.GetResults().Path()));
                            }
                            else {
                                fprintf(stderr, "[uwp] copying a picked file into LocalState failed\n");
                            }
                            if (--state->remaining == 0) {
                                const bool ok = !state->paths.empty();
                                lock.unlock();
                                state->callback(ok, state->paths);
                            }
                        });
                }
            });
    }
}

// ---- hooks used by the patched libraries ----

extern "C" const wchar_t* nmr_uwp_local_folder() {
    static const std::wstring path{ ApplicationData::Current().LocalFolder().Path() };
    return path.c_str();
}

extern "C" const wchar_t* nmr_uwp_install_folder() {
    static const std::wstring path{ Package::Current().InstalledLocation().Path() };
    return path.c_str();
}

extern "C" void nmr_uwp_get_window_size(void*, uint32_t* width, uint32_t* height) {
    // SDL tracks the CoreWindow's size (in physical pixels) from its own thread, so it can be read from the renderer's.
    int w = 0, h = 0;
    if (window != nullptr) {
        SDL_GetWindowSize(window, &w, &h);
    }
    *width = (w > 0) ? uint32_t(w) : 1920u;
    *height = (h > 0) ? uint32_t(h) : 1080u;
}

// [uwp] ROM drop folders: D:\DevelopmentFiles\NoMercy (next to the other Dev Mode files, reachable from the Device
// Portal's file explorer) first, then \NoMercy on a USB drive (E: to Z:). The *FromApp file APIs go through the
// broadFileSystemAccess / removableStorage capabilities (Package.appxmanifest), which the plain CRT doesn't.
static void nmr_uwp_make_drop_folder(const std::wstring& folder) {
    if (!CreateDirectoryFromAppW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "[uwp] can't create %ls (err=%lu)\n", folder.c_str(), GetLastError());
        return;
    }
    const std::wstring note_path = folder + L"\\Put game here.txt";
    HANDLE note = CreateFile2FromAppW(note_path.c_str(), GENERIC_WRITE, 0, CREATE_NEW, nullptr);
    if (note != INVALID_HANDLE_VALUE) {
        static const char text[] =
            "Put game here.\r\n"
            "\r\n"
            "Copy your WWF No Mercy (USA, Rev 1) ROM into this NoMercy folder.\r\n"
            "Any .z64, .n64 or .v64 file works, for example nomercy.z64.\r\n"
            "Then start WWF No Mercy: Recompiled: it loads the ROM from here automatically.\r\n"
            "\r\n"
            "The app looks in D:\\DevelopmentFiles\\NoMercy first, then in a NoMercy folder on a USB drive.\r\n";
        DWORD written = 0;
        WriteFile(note, text, sizeof(text) - 1, &written, nullptr);
        CloseHandle(note);
        fprintf(stderr, "[uwp] created %ls\n", note_path.c_str());
    }
}

// Copies the first .z64/.n64/.v64 in folder to dest_path. Returns true if one was copied.
static bool nmr_uwp_copy_rom_from(const std::wstring& folder, const wchar_t* dest_path) {
    WIN32_FIND_DATAW fd{};
    const std::wstring pattern = folder + L"\\*";
    HANDLE find = FindFirstFileExFromAppW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool copied = false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        std::wstring ext = std::filesystem::path(fd.cFileName).extension().wstring();
        for (wchar_t& c : ext) c = towlower(c);
        if (ext != L".z64" && ext != L".n64" && ext != L".v64") {
            continue;
        }
        const std::wstring src = folder + L"\\" + fd.cFileName;
        if (CopyFileFromAppW(src.c_str(), dest_path, FALSE)) {
            fprintf(stderr, "[uwp] copied %ls into LocalState\n", src.c_str());
            copied = true;
        }
        else {
            fprintf(stderr, "[uwp] copying %ls failed (err=%lu)\n", src.c_str(), GetLastError());
        }
    } while (!copied && FindNextFileW(find, &fd));
    FindClose(find);
    return copied;
}

extern "C" bool nmr_uwp_rom_drop_folders(bool copy_rom, const wchar_t* dest_path) {
    std::vector<std::wstring> folders;

    // D:\DevelopmentFiles\NoMercy is always created, with its note.
    CreateDirectoryFromAppW(L"D:\\DevelopmentFiles", nullptr);
    folders.push_back(L"D:\\DevelopmentFiles\\NoMercy");
    nmr_uwp_make_drop_folder(folders.back());

    // USB drives: only drive letters that actually exist get a NoMercy folder and note.
    for (wchar_t letter = L'E'; letter <= L'Z'; letter++) {
        const wchar_t root[] = { letter, L':', L'\\', 0 };
        WIN32_FILE_ATTRIBUTE_DATA attrs{};
        if (!GetFileAttributesExFromAppW(root, GetFileExInfoStandard, &attrs)) {
            continue;
        }
        folders.push_back(std::wstring{ letter, L':', L'\\' } + L"NoMercy");
        nmr_uwp_make_drop_folder(folders.back());
    }

    if (!copy_rom) {
        return false;
    }
    for (const std::wstring& folder : folders) {
        if (nmr_uwp_copy_rom_from(folder, dest_path)) {
            return true;
        }
    }
    return false;
}

namespace recompui::file::uwp {
    std::filesystem::path get_local_folder() {
        return std::filesystem::path{ nmr_uwp_local_folder() };
    }

    void open_file_dialog(SingleCallback callback) {
        run_on_ui_thread([callback]() {
            make_picker().PickSingleFileAsync().Completed([callback](IAsyncOperation<StorageFile> const& op, AsyncStatus status) {
                StorageFile file = (status == AsyncStatus::Completed) ? op.GetResults() : nullptr;
                if (file == nullptr) {
                    callback(false, {});
                    return;
                }
                import_files({ file }, [callback](bool ok, const std::list<std::filesystem::path>& paths) {
                    callback(ok, ok ? paths.front() : std::filesystem::path{});
                });
            });
        });
    }

    void open_file_dialog_multiple(MultipleCallback callback) {
        run_on_ui_thread([callback]() {
            make_picker().PickMultipleFilesAsync().Completed([callback](IAsyncOperation<IVectorView<StorageFile>> const& op, AsyncStatus status) {
                std::vector<StorageFile> files;
                if (status == AsyncStatus::Completed) {
                    for (const StorageFile& file : op.GetResults()) {
                        files.push_back(file);
                    }
                }
                import_files(std::move(files), callback);
            });
        });
    }
}

#endif
