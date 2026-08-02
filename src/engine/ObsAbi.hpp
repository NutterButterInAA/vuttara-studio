#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdarg>

namespace Vuttara::ObsAbi {

struct obs_display;
struct obs_scene;
struct obs_scene_item;
struct obs_source;
struct obs_module;
struct obs_data;
struct obs_fader;
struct obs_volmeter;
struct obs_output;
struct obs_encoder;
struct obs_service;
struct video_output;
struct audio_output;

using obs_display_t = obs_display;
using obs_scene_t = obs_scene;
using obs_sceneitem_t = obs_scene_item;
using obs_source_t = obs_source;
using obs_module_t = obs_module;
using obs_data_t = obs_data;
using obs_fader_t = obs_fader;
using obs_volmeter_t = obs_volmeter;
using obs_output_t = obs_output;
using obs_encoder_t = obs_encoder;
using obs_service_t = obs_service;
using video_t = video_output;
using audio_t = audio_output;

using log_handler_t = void (*)(int, const char*, va_list, void*);

enum video_format : int {
    VIDEO_FORMAT_NONE = 0,
    VIDEO_FORMAT_I420 = 1,
    VIDEO_FORMAT_NV12 = 2,
    VIDEO_FORMAT_YVYU = 3,
    VIDEO_FORMAT_YUY2 = 4,
    VIDEO_FORMAT_UYVY = 5,
    VIDEO_FORMAT_RGBA = 6,
    VIDEO_FORMAT_BGRA = 7,
    VIDEO_FORMAT_BGRX = 8,
};

enum video_colorspace : int {
    VIDEO_CS_DEFAULT = 0,
    VIDEO_CS_601 = 1,
    VIDEO_CS_709 = 2,
    VIDEO_CS_SRGB = 3,
    VIDEO_CS_2100_PQ = 4,
    VIDEO_CS_2100_HLG = 5,
};

enum video_range_type : int {
    VIDEO_RANGE_DEFAULT = 0,
    VIDEO_RANGE_PARTIAL = 1,
    VIDEO_RANGE_FULL = 2,
};

enum obs_scale_type : int {
    OBS_SCALE_DISABLE = 0,
    OBS_SCALE_POINT = 1,
    OBS_SCALE_BICUBIC = 2,
    OBS_SCALE_BILINEAR = 3,
    OBS_SCALE_LANCZOS = 4,
    OBS_SCALE_AREA = 5,
};

enum obs_order_movement : int {
    OBS_ORDER_MOVE_UP = 0,
    OBS_ORDER_MOVE_DOWN = 1,
    OBS_ORDER_MOVE_TOP = 2,
    OBS_ORDER_MOVE_BOTTOM = 3,
};

enum obs_bounds_type : int {
    OBS_BOUNDS_NONE = 0,
    OBS_BOUNDS_STRETCH = 1,
    OBS_BOUNDS_SCALE_INNER = 2,
    OBS_BOUNDS_SCALE_OUTER = 3,
    OBS_BOUNDS_SCALE_TO_WIDTH = 4,
    OBS_BOUNDS_SCALE_TO_HEIGHT = 5,
    OBS_BOUNDS_MAX_ONLY = 6,
};

enum obs_fader_type : int {
    OBS_FADER_CUBIC = 0,
    OBS_FADER_IEC = 1,
    OBS_FADER_LOG = 2,
};

enum obs_peak_meter_type : int {
    SAMPLE_PEAK_METER = 0,
    TRUE_PEAK_METER = 1,
};

enum speaker_layout : int {
    SPEAKERS_UNKNOWN = 0,
    SPEAKERS_MONO = 1,
    SPEAKERS_STEREO = 2,
};

enum gs_color_format : int {
    GS_UNKNOWN = 0,
    GS_A8 = 1,
    GS_R8 = 2,
    GS_RGBA = 3,
    GS_BGRX = 4,
    GS_BGRA = 5,
};

enum gs_zstencil_format : int {
    GS_ZS_NONE = 0,
};

struct vec2 {
    float x;
    float y;
};

struct obs_sceneitem_crop {
    int left;
    int top;
    int right;
    int bottom;
};

struct obs_video_info {
    const char* graphics_module;
    std::uint32_t fps_num;
    std::uint32_t fps_den;
    std::uint32_t base_width;
    std::uint32_t base_height;
    std::uint32_t output_width;
    std::uint32_t output_height;
    video_format output_format;
    std::uint32_t adapter;
    bool gpu_conversion;
    video_colorspace colorspace;
    video_range_type range;
    obs_scale_type scale_type;
};

struct obs_audio_info {
    std::uint32_t samples_per_sec;
    speaker_layout speakers;
};

struct gs_window {
    void* hwnd;
};

struct gs_init_data {
    gs_window window;
    std::uint32_t cx;
    std::uint32_t cy;
    std::uint32_t num_backbuffers;
    gs_color_format format;
    gs_zstencil_format zsformat;
    std::uint32_t adapter;
};

static_assert(sizeof(vec2) == 8, "vec2 ABI mismatch");
static_assert(sizeof(obs_video_info) == 56, "obs_video_info ABI mismatch for libobs 32.2.1 x64");
static_assert(offsetof(obs_video_info, output_format) == 32, "obs_video_info output_format offset mismatch");
static_assert(offsetof(obs_video_info, gpu_conversion) == 40, "obs_video_info gpu_conversion offset mismatch");
static_assert(offsetof(obs_video_info, colorspace) == 44, "obs_video_info colorspace offset mismatch");
static_assert(sizeof(obs_audio_info) == 8, "obs_audio_info ABI mismatch for libobs 32.2.1");
static_assert(sizeof(gs_init_data) == 32, "gs_init_data ABI mismatch for libobs 32.2.1 x64");

constexpr int MaxAudioChannels = 8;
using draw_callback_t = void (*)(void*, std::uint32_t, std::uint32_t);
using obs_volmeter_updated_t = void (*)(
    void*,
    const float[MaxAudioChannels],
    const float[MaxAudioChannels],
    const float[MaxAudioChannels]);

struct Api {
    void (*base_set_log_handler)(log_handler_t, void*) = nullptr;

    bool (*obs_startup)(const char*, const char*, void*) = nullptr;
    void (*obs_shutdown)() = nullptr;
    bool (*obs_initialized)() = nullptr;
    const char* (*obs_get_version_string)() = nullptr;
    void (*obs_add_data_path)(const char*) = nullptr;
    int (*obs_reset_video)(obs_video_info*) = nullptr;
    bool (*obs_reset_audio)(const obs_audio_info*) = nullptr;
    int (*obs_open_module)(obs_module_t**, const char*, const char*) = nullptr;
    bool (*obs_init_module)(obs_module_t*) = nullptr;
    void (*obs_post_load_modules)() = nullptr;
    void (*obs_log_loaded_modules)() = nullptr;

    obs_data_t* (*obs_data_create)() = nullptr;
    void (*obs_data_set_string)(obs_data_t*, const char*, const char*) = nullptr;
    void (*obs_data_set_int)(obs_data_t*, const char*, long long) = nullptr;
    void (*obs_data_set_bool)(obs_data_t*, const char*, bool) = nullptr;
    const char* (*obs_data_get_string)(obs_data_t*, const char*) = nullptr;
    long long (*obs_data_get_int)(obs_data_t*, const char*) = nullptr;
    bool (*obs_data_get_bool)(obs_data_t*, const char*) = nullptr;
    void (*obs_data_release)(obs_data_t*) = nullptr;

    bool (*obs_enum_output_types)(std::size_t, const char**) = nullptr;
    bool (*obs_enum_encoder_types)(std::size_t, const char**) = nullptr;
    bool (*obs_enum_service_types)(std::size_t, const char**) = nullptr;
    const char* (*obs_encoder_get_display_name)(const char*) = nullptr;
    const char* (*obs_get_encoder_codec)(const char*) = nullptr;
    obs_data_t* (*obs_encoder_defaults)(const char*) = nullptr;
    obs_encoder_t* (*obs_video_encoder_create)(const char*, const char*, obs_data_t*, obs_data_t*) = nullptr;
    obs_encoder_t* (*obs_audio_encoder_create)(const char*, const char*, obs_data_t*, std::size_t, obs_data_t*) = nullptr;
    void (*obs_encoder_release)(obs_encoder_t*) = nullptr;
    void (*obs_encoder_set_video)(obs_encoder_t*, video_t*) = nullptr;
    void (*obs_encoder_set_audio)(obs_encoder_t*, audio_t*) = nullptr;
    void (*obs_encoder_set_scaled_size)(obs_encoder_t*, std::uint32_t, std::uint32_t) = nullptr;
    bool (*obs_encoder_set_frame_rate_divisor)(obs_encoder_t*, std::uint32_t) = nullptr;
    const char* (*obs_encoder_get_last_error)(obs_encoder_t*) = nullptr;
    video_t* (*obs_get_video)() = nullptr;
    audio_t* (*obs_get_audio)() = nullptr;

    obs_output_t* (*obs_output_create)(const char*, const char*, obs_data_t*, obs_data_t*) = nullptr;
    void (*obs_output_release)(obs_output_t*) = nullptr;
    bool (*obs_output_start)(obs_output_t*) = nullptr;
    void (*obs_output_stop)(obs_output_t*) = nullptr;
    void (*obs_output_force_stop)(obs_output_t*) = nullptr;
    bool (*obs_output_active)(const obs_output_t*) = nullptr;
    void (*obs_output_set_video_encoder)(obs_output_t*, obs_encoder_t*) = nullptr;
    void (*obs_output_set_audio_encoder)(obs_output_t*, obs_encoder_t*, std::size_t) = nullptr;
    const char* (*obs_output_get_last_error)(obs_output_t*) = nullptr;
    std::uint64_t (*obs_output_get_total_bytes)(const obs_output_t*) = nullptr;
    int (*obs_output_get_frames_dropped)(const obs_output_t*) = nullptr;
    int (*obs_output_get_total_frames)(const obs_output_t*) = nullptr;
    float (*obs_output_get_congestion)(obs_output_t*) = nullptr;
    int (*obs_output_get_connect_time_ms)(obs_output_t*) = nullptr;
    bool (*obs_output_reconnecting)(const obs_output_t*) = nullptr;
    void (*obs_output_set_reconnect_settings)(obs_output_t*, int, int) = nullptr;
    void (*obs_output_set_service)(obs_output_t*, obs_service_t*) = nullptr;

    obs_service_t* (*obs_service_create)(const char*, const char*, obs_data_t*, obs_data_t*) = nullptr;
    void (*obs_service_release)(obs_service_t*) = nullptr;
    void (*obs_service_apply_encoder_settings)(obs_service_t*, obs_data_t*, obs_data_t*) = nullptr;

    obs_scene_t* (*obs_scene_create)(const char*) = nullptr;
    void (*obs_scene_release)(obs_scene_t*) = nullptr;
    obs_source_t* (*obs_scene_get_source)(const obs_scene_t*) = nullptr;
    obs_sceneitem_t* (*obs_scene_add)(obs_scene_t*, obs_source_t*) = nullptr;

    obs_source_t* (*obs_sceneitem_get_source)(const obs_sceneitem_t*) = nullptr;
    void (*obs_sceneitem_remove)(obs_sceneitem_t*) = nullptr;
    int (*obs_sceneitem_get_order_position)(obs_sceneitem_t*) = nullptr;
    bool (*obs_sceneitem_locked)(const obs_sceneitem_t*) = nullptr;
    bool (*obs_sceneitem_set_locked)(obs_sceneitem_t*, bool) = nullptr;
    void (*obs_sceneitem_set_pos)(obs_sceneitem_t*, const vec2*) = nullptr;
    void (*obs_sceneitem_set_rot)(obs_sceneitem_t*, float) = nullptr;
    void (*obs_sceneitem_set_order)(obs_sceneitem_t*, obs_order_movement) = nullptr;
    void (*obs_sceneitem_set_order_position)(obs_sceneitem_t*, int) = nullptr;
    void (*obs_sceneitem_get_pos)(const obs_sceneitem_t*, vec2*) = nullptr;
    float (*obs_sceneitem_get_rot)(const obs_sceneitem_t*) = nullptr;
    obs_bounds_type (*obs_sceneitem_get_bounds_type)(const obs_sceneitem_t*) = nullptr;
    void (*obs_sceneitem_get_bounds)(const obs_sceneitem_t*, vec2*) = nullptr;
    void (*obs_sceneitem_get_crop)(const obs_sceneitem_t*, obs_sceneitem_crop*) = nullptr;
    void (*obs_sceneitem_set_crop)(obs_sceneitem_t*, const obs_sceneitem_crop*) = nullptr;
    void (*obs_sceneitem_get_scale)(const obs_sceneitem_t*, vec2*) = nullptr;
    void (*obs_sceneitem_set_scale)(obs_sceneitem_t*, const vec2*) = nullptr;
    void (*obs_sceneitem_set_alignment)(obs_sceneitem_t*, std::uint32_t) = nullptr;
    void (*obs_sceneitem_set_bounds_type)(obs_sceneitem_t*, obs_bounds_type) = nullptr;
    void (*obs_sceneitem_set_bounds_alignment)(obs_sceneitem_t*, std::uint32_t) = nullptr;
    void (*obs_sceneitem_set_bounds)(obs_sceneitem_t*, const vec2*) = nullptr;
    bool (*obs_sceneitem_visible)(const obs_sceneitem_t*) = nullptr;
    bool (*obs_sceneitem_set_visible)(obs_sceneitem_t*, bool) = nullptr;

    obs_source_t* (*obs_source_create)(const char*, const char*, obs_data_t*, obs_data_t*) = nullptr;
    void (*obs_source_release)(obs_source_t*) = nullptr;
    obs_data_t* (*obs_source_get_settings)(const obs_source_t*) = nullptr;
    std::uint32_t (*obs_source_get_width)(const obs_source_t*) = nullptr;
    std::uint32_t (*obs_source_get_height)(const obs_source_t*) = nullptr;
    void (*obs_source_update)(obs_source_t*, obs_data_t*) = nullptr;
    const char* (*obs_source_get_name)(const obs_source_t*) = nullptr;
    void (*obs_source_set_name)(obs_source_t*, const char*) = nullptr;
    void (*obs_source_video_render)(obs_source_t*) = nullptr;
    void (*obs_source_set_volume)(obs_source_t*, float) = nullptr;
    float (*obs_source_get_volume)(const obs_source_t*) = nullptr;
    bool (*obs_source_muted)(const obs_source_t*) = nullptr;
    void (*obs_source_set_muted)(obs_source_t*, bool) = nullptr;
    void (*obs_set_output_source)(std::uint32_t, obs_source_t*) = nullptr;

    obs_fader_t* (*obs_fader_create)(obs_fader_type) = nullptr;
    void (*obs_fader_destroy)(obs_fader_t*) = nullptr;
    bool (*obs_fader_set_deflection)(obs_fader_t*, float) = nullptr;
    float (*obs_fader_get_deflection)(obs_fader_t*) = nullptr;
    bool (*obs_fader_attach_source)(obs_fader_t*, obs_source_t*) = nullptr;
    void (*obs_fader_detach_source)(obs_fader_t*) = nullptr;

    obs_volmeter_t* (*obs_volmeter_create)(obs_fader_type) = nullptr;
    void (*obs_volmeter_destroy)(obs_volmeter_t*) = nullptr;
    bool (*obs_volmeter_attach_source)(obs_volmeter_t*, obs_source_t*) = nullptr;
    void (*obs_volmeter_detach_source)(obs_volmeter_t*) = nullptr;
    void (*obs_volmeter_set_peak_meter_type)(obs_volmeter_t*, obs_peak_meter_type) = nullptr;
    int (*obs_volmeter_get_nr_channels)(obs_volmeter_t*) = nullptr;
    void (*obs_volmeter_add_callback)(obs_volmeter_t*, obs_volmeter_updated_t, void*) = nullptr;
    void (*obs_volmeter_remove_callback)(obs_volmeter_t*, obs_volmeter_updated_t, void*) = nullptr;

    obs_display_t* (*obs_display_create)(const gs_init_data*, std::uint32_t) = nullptr;
    void (*obs_display_destroy)(obs_display_t*) = nullptr;
    void (*obs_display_resize)(obs_display_t*, std::uint32_t, std::uint32_t) = nullptr;
    void (*obs_display_add_draw_callback)(obs_display_t*, draw_callback_t, void*) = nullptr;
    void (*obs_display_remove_draw_callback)(obs_display_t*, draw_callback_t, void*) = nullptr;

    void (*gs_viewport_push)() = nullptr;
    void (*gs_viewport_pop)() = nullptr;
    void (*gs_projection_push)() = nullptr;
    void (*gs_projection_pop)() = nullptr;
    void (*gs_set_viewport)(int, int, int, int) = nullptr;
    void (*gs_ortho)(float, float, float, float, float, float) = nullptr;
    void (*gs_matrix_push)() = nullptr;
    void (*gs_matrix_pop)() = nullptr;
    void (*gs_matrix_identity)() = nullptr;
};

}
