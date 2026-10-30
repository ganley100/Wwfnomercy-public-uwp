// UWP (Xbox One Dev Mode) platform layer: the pieces of the port that need WinRT instead of Win32.
// Built only with NMR_UWP (see uwp/README.md). The libraries reach these through the hooks declared in
// uwp/patches/*.patch (nmr_uwp_* and recompui::file::uwp), so none of them depends on C++/WinRT.
#if defined(NMR_UWP)

#include <cstdio>
#include <cstdint>
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
