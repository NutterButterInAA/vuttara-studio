#include "ObsRuntime.hpp"

#include <QDir>
#include <QFileInfo>

#include <windows.h>

namespace Vuttara {
namespace {

template<typename FunctionType>
bool resolveFunction(HMODULE library, const char* name, FunctionType& destination, QString* errorMessage)
{
    destination = reinterpret_cast<FunctionType>(GetProcAddress(library, name));
    if (destination != nullptr) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Required libobs export is missing: %1").arg(QString::fromLatin1(name));
    }
    return false;
}

using AddDllDirectoryFunction = void* (WINAPI*)(const wchar_t*);
using RemoveDllDirectoryFunction = BOOL (WINAPI*)(void*);

AddDllDirectoryFunction addDllDirectoryFunction()
{
    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    return kernel != nullptr
        ? reinterpret_cast<AddDllDirectoryFunction>(GetProcAddress(kernel, "AddDllDirectory"))
        : nullptr;
}

RemoveDllDirectoryFunction removeDllDirectoryFunction()
{
    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    return kernel != nullptr
        ? reinterpret_cast<RemoveDllDirectoryFunction>(GetProcAddress(kernel, "RemoveDllDirectory"))
        : nullptr;
}

}

ObsRuntime::~ObsRuntime()
{
    unload();
}

bool ObsRuntime::load(const QString& runtimeRoot, QString* errorMessage)
{
    unload();

    const QDir root(runtimeRoot);
    const QString binaryDirectory = root.filePath(QStringLiteral("bin/64bit"));
    const QString pluginDirectory = root.filePath(QStringLiteral("obs-plugins/64bit"));
    const QString dataDirectory = root.filePath(QStringLiteral("data"));
    const QString obsLibraryPath = QDir(binaryDirectory).filePath(QStringLiteral("obs.dll"));

    if (!QFileInfo::exists(obsLibraryPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Controlled libobs runtime is missing obs.dll: %1").arg(obsLibraryPath);
        }
        return false;
    }

    if (!QFileInfo::exists(QDir(binaryDirectory).filePath(QStringLiteral("libobs-d3d11.dll")))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Controlled libobs runtime is missing libobs-d3d11.dll.");
        }
        return false;
    }

    if (!QFileInfo::exists(QDir(pluginDirectory).filePath(QStringLiteral("image-source.dll")))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Controlled libobs runtime is missing image-source.dll.");
        }
        return false;
    }

    if (!QFileInfo::exists(QDir(pluginDirectory).filePath(QStringLiteral("win-wasapi.dll")))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Controlled libobs runtime is missing win-wasapi.dll.");
        }
        return false;
    }

    if (!QFileInfo::exists(QDir(pluginDirectory).filePath(QStringLiteral("win-capture.dll")))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Controlled libobs runtime is missing win-capture.dll.");
        }
        return false;
    }

    if (!QFileInfo::exists(QDir(pluginDirectory).filePath(QStringLiteral("obs-ffmpeg.dll")))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Controlled libobs runtime is missing obs-ffmpeg.dll.");
        }
        return false;
    }

    if (!QFileInfo::exists(QDir(pluginDirectory).filePath(QStringLiteral("obs-x264.dll")))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Controlled libobs runtime is missing obs-x264.dll.");
        }
        return false;
    }

    if (!QFileInfo::exists(QDir(binaryDirectory).filePath(QStringLiteral("obs-ffmpeg-mux.exe")))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Controlled libobs runtime is missing obs-ffmpeg-mux.exe.");
        }
        return false;
    }

    if (!QFileInfo::exists(QDir(binaryDirectory).filePath(QStringLiteral("libobs-winrt.dll")))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Controlled libobs runtime is missing libobs-winrt.dll.");
        }
        return false;
    }

    const auto addDirectory = addDllDirectoryFunction();
    if (addDirectory == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Windows AddDllDirectory support is unavailable.");
        }
        return false;
    }

    const std::wstring binaryDirectoryWide = QDir::toNativeSeparators(binaryDirectory).toStdWString();
    void* directoryCookie = addDirectory(binaryDirectoryWide.c_str());
    if (directoryCookie == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Windows could not register the controlled libobs DLL directory. Error code: %1")
                                .arg(static_cast<unsigned long>(GetLastError()));
        }
        return false;
    }

    const std::wstring obsLibraryWide = QDir::toNativeSeparators(obsLibraryPath).toStdWString();
    HMODULE library = LoadLibraryExW(
        obsLibraryWide.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (library == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Windows could not load obs.dll. Error code: %1")
                                .arg(static_cast<unsigned long>(GetLastError()));
        }
        if (const auto removeDirectory = removeDllDirectoryFunction(); removeDirectory != nullptr) {
            removeDirectory(directoryCookie);
        }
        return false;
    }

#define RESOLVE_OBS(name) \
    if (!resolveFunction(library, #name, api_.name, errorMessage)) { \
        FreeLibrary(library); \
        if (const auto removeDirectory = removeDllDirectoryFunction(); removeDirectory != nullptr) { \
            removeDirectory(directoryCookie); \
        } \
        api_ = {}; \
        return false; \
    }

    RESOLVE_OBS(base_set_log_handler);
    RESOLVE_OBS(obs_startup);
    RESOLVE_OBS(obs_shutdown);
    RESOLVE_OBS(obs_initialized);
    RESOLVE_OBS(obs_get_version_string);
    RESOLVE_OBS(obs_add_data_path);
    RESOLVE_OBS(obs_reset_video);
    RESOLVE_OBS(obs_reset_audio);
    RESOLVE_OBS(obs_open_module);
    RESOLVE_OBS(obs_init_module);
    RESOLVE_OBS(obs_post_load_modules);
    RESOLVE_OBS(obs_log_loaded_modules);
    RESOLVE_OBS(obs_data_create);
    RESOLVE_OBS(obs_data_set_string);
    RESOLVE_OBS(obs_data_set_int);
    RESOLVE_OBS(obs_data_set_bool);
    RESOLVE_OBS(obs_data_get_string);
    RESOLVE_OBS(obs_data_get_int);
    RESOLVE_OBS(obs_data_get_bool);
    RESOLVE_OBS(obs_data_release);
    RESOLVE_OBS(obs_enum_output_types);
    RESOLVE_OBS(obs_enum_encoder_types);
    RESOLVE_OBS(obs_enum_service_types);
    RESOLVE_OBS(obs_encoder_get_display_name);
    RESOLVE_OBS(obs_get_encoder_codec);
    RESOLVE_OBS(obs_encoder_defaults);
    RESOLVE_OBS(obs_video_encoder_create);
    RESOLVE_OBS(obs_audio_encoder_create);
    RESOLVE_OBS(obs_encoder_release);
    RESOLVE_OBS(obs_encoder_set_video);
    RESOLVE_OBS(obs_encoder_set_audio);
    RESOLVE_OBS(obs_encoder_set_scaled_size);
    RESOLVE_OBS(obs_encoder_set_frame_rate_divisor);
    RESOLVE_OBS(obs_encoder_get_last_error);
    RESOLVE_OBS(obs_get_video);
    RESOLVE_OBS(obs_get_audio);
    RESOLVE_OBS(obs_output_create);
    RESOLVE_OBS(obs_output_release);
    RESOLVE_OBS(obs_output_start);
    RESOLVE_OBS(obs_output_stop);
    RESOLVE_OBS(obs_output_force_stop);
    RESOLVE_OBS(obs_output_active);
    RESOLVE_OBS(obs_output_set_video_encoder);
    RESOLVE_OBS(obs_output_set_audio_encoder);
    RESOLVE_OBS(obs_output_get_last_error);
    RESOLVE_OBS(obs_output_get_total_bytes);
    RESOLVE_OBS(obs_output_get_frames_dropped);
    RESOLVE_OBS(obs_output_get_total_frames);
    RESOLVE_OBS(obs_output_get_congestion);
    RESOLVE_OBS(obs_output_get_connect_time_ms);
    RESOLVE_OBS(obs_output_reconnecting);
    RESOLVE_OBS(obs_output_set_reconnect_settings);
    RESOLVE_OBS(obs_output_set_service);
    RESOLVE_OBS(obs_service_create);
    RESOLVE_OBS(obs_service_release);
    RESOLVE_OBS(obs_service_apply_encoder_settings);
    RESOLVE_OBS(obs_scene_create);
    RESOLVE_OBS(obs_scene_release);
    RESOLVE_OBS(obs_scene_get_source);
    RESOLVE_OBS(obs_scene_add);
    RESOLVE_OBS(obs_sceneitem_get_source);
    RESOLVE_OBS(obs_sceneitem_remove);
    RESOLVE_OBS(obs_sceneitem_get_order_position);
    RESOLVE_OBS(obs_sceneitem_locked);
    RESOLVE_OBS(obs_sceneitem_set_locked);
    RESOLVE_OBS(obs_sceneitem_set_pos);
    RESOLVE_OBS(obs_sceneitem_set_rot);
    RESOLVE_OBS(obs_sceneitem_set_order);
    RESOLVE_OBS(obs_sceneitem_set_order_position);
    RESOLVE_OBS(obs_sceneitem_get_pos);
    RESOLVE_OBS(obs_sceneitem_get_rot);
    RESOLVE_OBS(obs_sceneitem_get_bounds_type);
    RESOLVE_OBS(obs_sceneitem_get_bounds);
    RESOLVE_OBS(obs_sceneitem_get_crop);
    RESOLVE_OBS(obs_sceneitem_set_crop);
    RESOLVE_OBS(obs_sceneitem_get_scale);
    RESOLVE_OBS(obs_sceneitem_set_scale);
    RESOLVE_OBS(obs_sceneitem_set_alignment);
    RESOLVE_OBS(obs_sceneitem_set_bounds_type);
    RESOLVE_OBS(obs_sceneitem_set_bounds_alignment);
    RESOLVE_OBS(obs_sceneitem_set_bounds);
    RESOLVE_OBS(obs_sceneitem_visible);
    RESOLVE_OBS(obs_sceneitem_set_visible);
    RESOLVE_OBS(obs_source_create);
    RESOLVE_OBS(obs_source_release);
    RESOLVE_OBS(obs_source_get_settings);
    RESOLVE_OBS(obs_source_get_width);
    RESOLVE_OBS(obs_source_get_height);
    RESOLVE_OBS(obs_source_update);
    RESOLVE_OBS(obs_source_get_name);
    RESOLVE_OBS(obs_source_set_name);
    RESOLVE_OBS(obs_source_video_render);
    RESOLVE_OBS(obs_source_set_volume);
    RESOLVE_OBS(obs_source_get_volume);
    RESOLVE_OBS(obs_source_muted);
    RESOLVE_OBS(obs_source_set_muted);
    RESOLVE_OBS(obs_set_output_source);
    RESOLVE_OBS(obs_fader_create);
    RESOLVE_OBS(obs_fader_destroy);
    RESOLVE_OBS(obs_fader_set_deflection);
    RESOLVE_OBS(obs_fader_get_deflection);
    RESOLVE_OBS(obs_fader_attach_source);
    RESOLVE_OBS(obs_fader_detach_source);
    RESOLVE_OBS(obs_volmeter_create);
    RESOLVE_OBS(obs_volmeter_destroy);
    RESOLVE_OBS(obs_volmeter_attach_source);
    RESOLVE_OBS(obs_volmeter_detach_source);
    RESOLVE_OBS(obs_volmeter_set_peak_meter_type);
    RESOLVE_OBS(obs_volmeter_get_nr_channels);
    RESOLVE_OBS(obs_volmeter_add_callback);
    RESOLVE_OBS(obs_volmeter_remove_callback);
    RESOLVE_OBS(obs_display_create);
    RESOLVE_OBS(obs_display_destroy);
    RESOLVE_OBS(obs_display_resize);
    RESOLVE_OBS(obs_display_add_draw_callback);
    RESOLVE_OBS(obs_display_remove_draw_callback);
    RESOLVE_OBS(gs_viewport_push);
    RESOLVE_OBS(gs_viewport_pop);
    RESOLVE_OBS(gs_projection_push);
    RESOLVE_OBS(gs_projection_pop);
    RESOLVE_OBS(gs_set_viewport);
    RESOLVE_OBS(gs_ortho);
    RESOLVE_OBS(gs_matrix_push);
    RESOLVE_OBS(gs_matrix_pop);
    RESOLVE_OBS(gs_matrix_identity);

#undef RESOLVE_OBS

    library_ = library;
    dllDirectoryCookie_ = directoryCookie;
    runtimeRoot_ = QDir::cleanPath(runtimeRoot);
    binaryDirectory_ = binaryDirectory;
    pluginDirectory_ = pluginDirectory;
    dataDirectory_ = dataDirectory;
    return true;
}

void ObsRuntime::unload()
{
    api_ = {};

    if (library_ != nullptr) {
        FreeLibrary(static_cast<HMODULE>(library_));
        library_ = nullptr;
    }

    if (dllDirectoryCookie_ != nullptr) {
        if (const auto removeDirectory = removeDllDirectoryFunction(); removeDirectory != nullptr) {
            removeDirectory(dllDirectoryCookie_);
        }
        dllDirectoryCookie_ = nullptr;
    }

    runtimeRoot_.clear();
    binaryDirectory_.clear();
    pluginDirectory_.clear();
    dataDirectory_.clear();
}

bool ObsRuntime::isLoaded() const
{
    return library_ != nullptr;
}

const ObsAbi::Api& ObsRuntime::api() const
{
    return api_;
}

const QString& ObsRuntime::runtimeRoot() const
{
    return runtimeRoot_;
}

const QString& ObsRuntime::binaryDirectory() const
{
    return binaryDirectory_;
}

const QString& ObsRuntime::pluginDirectory() const
{
    return pluginDirectory_;
}

const QString& ObsRuntime::dataDirectory() const
{
    return dataDirectory_;
}

QString ObsRuntime::versionString() const
{
    if (!isLoaded() || api_.obs_get_version_string == nullptr) {
        return {};
    }

    const char* version = api_.obs_get_version_string();
    return version != nullptr ? QString::fromUtf8(version) : QString{};
}

}
