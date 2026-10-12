/*
 * post_shader.h -- a filter over the game's picture: the built-in CRT, or a
 * libretro GLSL preset the player supplies (retro_shader.h).
 *
 * Only the game's letterboxed 4:3 rectangle is filtered. The rest of the window
 * (the letterbox, the menus, ImGui) is drawn as before, around it.
 *
 * With a filter on, the game is drawn OFFSCREEN first, into a source target this
 * file owns, and the filter reads that. A frontend does, per frame:
 *
 *     if (post_shader_active()) {
 *         post_shader_source_size(VIDEO_WIDTH, VIDEO_HEIGHT, rect_w, rect_h, &sw, &sh);
 *         post_shader_begin_source(sw, sh, &pass_action);
 *         game_frame_draw(..., 0, 0, sw, sh, lerp_t);
 *         post_shader_end_source();
 *         post_shader_prepare(rect_w, rect_h);          // outside any pass
 *     }
 *     sg_begin_pass(swapchain) ...
 *         post_shader_draw(ox, oy, rect_w, rect_h, fb_w, fb_h);   // in place of the game
 *
 * THE BUILT-IN CRT is Lost Judgment's own CRT filter, as YAMP ports it
 * (newyamp "YAMP - Claude", source/RenderWindow.cpp, LJ_CRT_PS_HLSL): one
 * scanline per Model 2 line (quartic ramp, at most 12.96% darker), one aperture
 * grille stripe per source column (^8 ramp, at most 1.68%), a Bayer 4x4 dither
 * on the scanline phase, four taps along the pixel derivative. Every ramp is
 * anchored to the PICTURE, not to the source texture, so it looks the same at
 * any render scale. Like YAMP, when the picture is shown less than 1080 lines
 * tall it is filtered into a 1080-line target first and scaled down, because
 * below that the 384 scanlines alias into broken segments. It runs on every
 * backend: HLSL for D3D11, GLSL for GL and WebGL.
 *
 * THE ARCADE is the same CRT over the owner's real Model 2B board's picture:
 * before the scanline, each colour goes through the board's tone curve,
 * c^POST_ARCADE_TONE. It was measured from the board's video, captured by VCS,
 * against this emulator's picture of the same frames (the title on white and
 * on black, the character grid; Pinboard #639): per channel, the board's level
 * is k * emu^0.76 within 3 levels of 255, with no shift of hue or saturation
 * (a fitted 3x3 colour matrix comes out diagonal). k, the capture's own level,
 * is left out: white stays white.
 *
 * A CUSTOM preset needs a GL backend (the web build and the Linux desktop); on
 * D3D11 post_shader_custom_supported() is false and loading one says why.
 */
#ifndef POST_SHADER_H
#define POST_SHADER_H

#include <stdbool.h>
#include <string.h>

#include "sokol_gfx.h"
#include "game_render.h"
#include "retro_shader.h"
#include "log.h"

typedef enum {
    POST_SHADER_OFF,
    POST_SHADER_CRT,      /* the built-in one */
    POST_SHADER_CUSTOM,   /* a libretro preset */
    POST_SHADER_ARCADE,   /* the built-in CRT with the real board's tone curve */
} post_shader_mode_t;

/* The board's tone curve: the exponent on each 0..1 channel. */
#define POST_ARCADE_TONE_GLSL "  c = pow(c, vec3(0.76));\n"
#define POST_ARCADE_TONE_HLSL "  c = pow(c, float3(0.76, 0.76, 0.76));\n"

/* The 1080-line floor below which the CRT is filtered large and scaled down. */
#define POST_SHADER_CRT_MIN_LINES 1080

static struct {
    bool               init;
    post_shader_mode_t mode;
    int                input_scale;    /* 0 = automatic (post_shader_source_size) */

    /* The game, drawn offscreen for the filter to read. */
    sg_image src_color, src_depth;
    sg_view  src_color_att, src_depth_att, src_tex;
    int      src_w, src_h;

    /* The built-in CRT, [0], and the Arcade, [1]: post_builtin(). */
    sg_shader   crt_shader[2];
    sg_pipeline crt_screen[2], crt_offscreen[2];
    sg_sampler  crt_sampler;
    sg_image    up_color;
    sg_view     up_att, up_tex;
    int         up_w, up_h;
    bool        up_used;               /* this frame went through the 1080-line target */

    /* The custom preset. */
    rs_preset_t preset;
    bool        custom_loaded;
    char        custom_path[RS_PATH];
    char        err[RS_ERR];

    bool        source_drawn;          /* a source was drawn this frame */
} g_post;

/* ---- The CRT's shaders ------------------------------------------------------ */

/* TONE is a line of GLSL that may change c, the source colour, before the
 * scanline and the grille: empty for the CRT, the board's curve for the Arcade. */
#define POST_CRT_FS_GLSL(TONE) \
    "#version 410\n" \
    "uniform sampler2D tex_smp;\n" \
    "in vec2 uv;\n" \
    "out vec4 frag_color;\n" \
    "const float kBayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,\n" \
    "                                   3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);\n" \
    /* p is picture space, top-left origin; a GL target keeps its bottom row first. */ \
    "vec3 crt_tap(vec2 p) {\n" \
    "  vec3 c = texture(tex_smp, vec2(p.x, 1.0 - p.y)).rgb;\n" \
    TONE \
    "  int xi = int(fract(floor(p.x * 2048.0 + 0.5) * 0.25) * 4.0) & 3;\n" \
    "  int yi = int(fract(floor(p.y * 1536.0 + 0.5) * 0.25) * 4.0) & 3;\n" \
    "  float dither = kBayer[yi * 4 + xi] / 255.0;\n" \
    "  float py = fract(dither + p.y * 384.0);\n" \
    "  float m4 = min(py, 0.6); m4 *= m4; m4 *= m4;\n" \
    "  float scan = 1.0 - m4;\n" \
    "  float px = fract(p.x * 512.0);\n" \
    "  float m8 = min(px, 0.6); m8 *= m8; m8 *= m8; m8 *= m8;\n" \
    "  float grille = clamp(1.0 - m8, 0.0, 1.0);\n" \
    "  return c * (scan * grille);\n" \
    "}\n" \
    "void main() {\n" \
    /* D3D's derivatives of this uv are positive; GL's y runs the other way. */ \
    "  vec2 d = vec2(abs(dFdx(uv.x)), abs(dFdy(uv.y)));\n" \
    "  vec3 s = crt_tap(uv) + crt_tap(uv + 0.25 * d) + crt_tap(uv + 0.5 * d) + crt_tap(uv + 0.75 * d);\n" \
    "  frag_color = vec4(s * 0.25, 1.0);\n" \
    "}\n"

static const char *post_crt_fs_glsl    = POST_CRT_FS_GLSL("");
static const char *post_arcade_fs_glsl = POST_CRT_FS_GLSL(POST_ARCADE_TONE_GLSL);

#define POST_CRT_FS_HLSL(TONE) \
    "Texture2D<float4> tex : register(t0);\n" \
    "SamplerState smp : register(s0);\n" \
    "struct fs_in { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n" \
    "static const float kBayer[16] = {\n" \
    "   0.0,  8.0,  2.0, 10.0,\n" \
    "  12.0,  4.0, 14.0,  6.0,\n" \
    "   3.0, 11.0,  1.0,  9.0,\n" \
    "  15.0,  7.0, 13.0,  5.0,\n" \
    "};\n" \
    "float3 crt_tap(float2 uv) {\n" \
    "  float3 c = tex.Sample(smp, uv).rgb;\n" \
    TONE \
    "  int xi = (int)(frac(round(uv.x * 2048.0) * 0.25) * 4.0);\n" \
    "  int yi = (int)(frac(round(uv.y * 1536.0) * 0.25) * 4.0);\n" \
    "  float dither = kBayer[yi * 4 + xi] / 255.0;\n" \
    "  float py = frac(dither + uv.y * 384.0);\n" \
    "  float m4 = min(py, 0.6); m4 *= m4; m4 *= m4;\n" \
    "  float scan = 1.0 - m4;\n" \
    "  float px = frac(uv.x * 512.0);\n" \
    "  float m8 = min(px, 0.6); m8 *= m8; m8 *= m8; m8 *= m8;\n" \
    "  float grille = saturate(1.0 - m8);\n" \
    "  return c * (scan * grille);\n" \
    "}\n" \
    "float4 main(fs_in inp) : SV_Target0 {\n" \
    "  float2 uvc = inp.uv;\n" \
    "  float2 d = float2(abs(ddx(uvc.x)), abs(ddy(uvc.y)));\n" \
    "  float3 s = crt_tap(uvc) + crt_tap(uvc + 0.25 * d) + crt_tap(uvc + 0.5 * d) + crt_tap(uvc + 0.75 * d);\n" \
    "  return float4(s * 0.25, 1.0);\n" \
    "}\n"

static const char *post_crt_fs_hlsl    = POST_CRT_FS_HLSL("");
static const char *post_arcade_fs_hlsl = POST_CRT_FS_HLSL(POST_ARCADE_TONE_HLSL);

static inline sg_pixel_format post_color_format(void) {
    sg_pixel_format f = sg_query_desc().environment.defaults.color_format;
    return f == SG_PIXELFORMAT_NONE ? SG_PIXELFORMAT_RGBA8 : f;
}

static inline sg_pixel_format post_depth_format(void) {
    sg_pixel_format f = sg_query_desc().environment.defaults.depth_format;
    return f == SG_PIXELFORMAT_NONE ? SG_PIXELFORMAT_DEPTH_STENCIL : f;
}

/* ---- Set-up ---------------------------------------------------------------------- */

/* Built-in filter i (0 the CRT, 1 the Arcade): its shader and its two pipelines. */
static inline void post_builtin_make(int i, sg_backend backend, const char *glsl, const char *hlsl) {
    sg_shader_desc d;
    memset(&d, 0, sizeof d);
    d.attrs[0].hlsl_sem_name  = "POSITION";
    d.attrs[0].hlsl_sem_index = 0;
    d.attrs[0].base_type      = SG_SHADERATTRBASETYPE_FLOAT;
    d.attrs[1].hlsl_sem_name  = "TEXCOORD";
    d.attrs[1].hlsl_sem_index = 0;
    d.attrs[1].base_type      = SG_SHADERATTRBASETYPE_FLOAT;
    d.views[0].texture.stage             = SG_SHADERSTAGE_FRAGMENT;
    d.views[0].texture.image_type        = SG_IMAGETYPE_2D;
    d.views[0].texture.sample_type       = SG_IMAGESAMPLETYPE_FLOAT;
    d.views[0].texture.hlsl_register_t_n = 0;
    d.samplers[0].stage             = SG_SHADERSTAGE_FRAGMENT;
    d.samplers[0].sampler_type      = SG_SAMPLERTYPE_FILTERING;
    d.samplers[0].hlsl_register_s_n = 0;
    d.texture_sampler_pairs[0].stage        = SG_SHADERSTAGE_FRAGMENT;
    d.texture_sampler_pairs[0].view_slot    = 0;
    d.texture_sampler_pairs[0].sampler_slot = 0;
    d.texture_sampler_pairs[0].glsl_name    = "tex_smp";
    d.label = i ? "post-arcade-shader" : "post-crt-shader";
    if (backend == SG_BACKEND_GLCORE || backend == SG_BACKEND_GLES3) {
        d.vertex_func.source   = game_render_glsl(backend, game_render_tile_vs_glsl, 0);
        d.fragment_func.source = game_render_glsl(backend, glsl, 1);
    } else if (backend == SG_BACKEND_D3D11) {
        d.vertex_func.source         = game_render_tile_vs_hlsl;
        d.vertex_func.d3d11_target   = "vs_4_0";
        d.fragment_func.source       = hlsl;
        d.fragment_func.d3d11_target = "ps_4_0";
    }
    g_post.crt_shader[i] = sg_make_shader(&d);

    sg_pipeline_desc p;
    memset(&p, 0, sizeof p);
    p.shader                   = g_post.crt_shader[i];
    p.primitive_type           = SG_PRIMITIVETYPE_TRIANGLES;
    p.layout.attrs[0].format   = SG_VERTEXFORMAT_FLOAT2;
    p.layout.attrs[0].offset   = offsetof(game_render_quad_vertex_t, x);
    p.layout.attrs[1].format   = SG_VERTEXFORMAT_FLOAT2;
    p.layout.attrs[1].offset   = offsetof(game_render_quad_vertex_t, u);
    p.layout.buffers[0].stride = sizeof(game_render_quad_vertex_t);
    p.label = i ? "post-arcade-screen" : "post-crt-screen";
    g_post.crt_screen[i] = sg_make_pipeline(&p);
    p.colors[0].pixel_format = post_color_format();
    p.depth.pixel_format     = SG_PIXELFORMAT_NONE;
    p.sample_count           = 1;
    p.label = i ? "post-arcade-offscreen" : "post-crt-offscreen";
    g_post.crt_offscreen[i] = sg_make_pipeline(&p);
}

/* After sg_setup and game_render_init. */
static inline void post_shader_init(void) {
    if (g_post.init) return;
    const sg_backend backend = sg_query_backend();
    post_builtin_make(0, backend, post_crt_fs_glsl, post_crt_fs_hlsl);
    post_builtin_make(1, backend, post_arcade_fs_glsl, post_arcade_fs_hlsl);

    /* LJ's own filtering: MIN_MAG_MIP_LINEAR, clamp. */
    g_post.crt_sampler = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .wrap_u     = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v     = SG_WRAP_CLAMP_TO_EDGE,
        .label      = "post-crt-sampler",
    });
    g_post.init = true;
}

static inline void post_free_source(void) {
    if (!g_post.src_w) return;
    sg_destroy_view(g_post.src_tex);
    sg_destroy_view(g_post.src_depth_att);
    sg_destroy_view(g_post.src_color_att);
    sg_destroy_image(g_post.src_depth);
    sg_destroy_image(g_post.src_color);
    g_post.src_w = g_post.src_h = 0;
}

static inline void post_free_up(void) {
    if (!g_post.up_w) return;
    sg_destroy_view(g_post.up_tex);
    sg_destroy_view(g_post.up_att);
    sg_destroy_image(g_post.up_color);
    g_post.up_w = g_post.up_h = 0;
}

static inline void post_shader_shutdown(void) {
    if (!g_post.init) return;
#ifdef RS_HAVE_GL
    rs_gl_destroy();
#endif
    rs_preset_free(&g_post.preset);
    post_free_source();
    post_free_up();
    sg_destroy_sampler(g_post.crt_sampler);
    for (int i = 0; i < 2; i++) {
        sg_destroy_pipeline(g_post.crt_offscreen[i]);
        sg_destroy_pipeline(g_post.crt_screen[i]);
        sg_destroy_shader(g_post.crt_shader[i]);
    }
    g_post.init = false;
}

/* ---- Choosing ------------------------------------------------------------------------ */

static inline bool post_shader_custom_supported(void) {
#ifdef RS_HAVE_GL
    return true;
#else
    return false;
#endif
}

static inline void post_shader_set_mode(post_shader_mode_t m) { g_post.mode = m; }
static inline post_shader_mode_t post_shader_mode(void)     { return g_post.mode; }
static inline const char *post_shader_error(void)            { return g_post.err; }
static inline bool post_shader_custom_loaded(void)           { return g_post.custom_loaded; }
static inline const char *post_shader_custom_name(void)      { return g_post.custom_loaded ? g_post.preset.name : ""; }
static inline const char *post_shader_custom_path(void)      { return g_post.custom_path; }
static inline void post_shader_set_input_scale(int n)        { g_post.input_scale = n < 0 ? 0 : n > 4 ? 4 : n; }
static inline int  post_shader_input_scale(void)             { return g_post.input_scale; }

/* The CRT or the Arcade: the built-in filters, which differ only in their tone. */
static inline bool post_builtin(void) {
    return g_post.mode == POST_SHADER_CRT || g_post.mode == POST_SHADER_ARCADE;
}

static inline bool post_shader_active(void) {
    if (!g_post.init) return false;
    if (post_builtin()) return true;
    return g_post.mode == POST_SHADER_CUSTOM && g_post.custom_loaded && post_shader_custom_supported();
}

/*
 * Load a .glslp / .glsl preset (from disk, or from the rs_vfs table the page
 * filled) and make it the filter. On failure the filter is left as it was and
 * post_shader_error() says what went wrong. Needs the GL context current: call
 * it from the frame thread.
 */
static inline bool post_shader_load(const char *path) {
    g_post.err[0] = '\0';
    if (!post_shader_custom_supported()) {
        snprintf(g_post.err, sizeof g_post.err,
                 "Custom shaders need the OpenGL renderer (the web and Linux builds). "
                 "This build draws with Direct3D 11; the built-in CRT filter works here.");
        return false;
    }
    static rs_preset_t fresh;
    if (!rs_preset_load(&fresh, path, g_post.err, sizeof g_post.err)) {
        LOG_WARN("shader: %s", g_post.err);
        return false;
    }
#ifdef RS_HAVE_GL
    rs_preset_free(&g_post.preset);
    g_post.preset = fresh;               /* takes over the sources and the LUTs */
    memset(&fresh, 0, sizeof fresh);
    g_post.custom_loaded = rs_gl_create(&g_post.preset);
    sg_reset_state_cache();
    if (!g_post.custom_loaded) {
        snprintf(g_post.err, sizeof g_post.err, "%s", g_rs_gl.err);
        LOG_WARN("shader: %s", g_post.err);
        rs_preset_free(&g_post.preset);
        return false;
    }
    snprintf(g_post.custom_path, sizeof g_post.custom_path, "%s", path);
    g_post.mode = POST_SHADER_CUSTOM;
    return true;
#else
    rs_preset_free(&fresh);
    return false;
#endif
}

static inline void post_shader_unload(void) {
#ifdef RS_HAVE_GL
    rs_gl_destroy();
    sg_reset_state_cache();
#endif
    rs_preset_free(&g_post.preset);
    g_post.custom_loaded = false;
    g_post.custom_path[0] = '\0';
    if (g_post.mode == POST_SHADER_CUSTOM) g_post.mode = POST_SHADER_OFF;
}

/* The custom preset's parameters, for a settings UI. */
static inline int post_shader_param_count(void) { return g_post.custom_loaded ? g_post.preset.n_params : 0; }
static inline rs_param_t *post_shader_param(int i) {
    return i >= 0 && i < post_shader_param_count() ? &g_post.preset.param[i] : NULL;
}
static inline bool post_shader_set_param(const char *name, float v) {
    rs_param_t *p = g_post.custom_loaded ? rs_preset_param(&g_post.preset, name) : NULL;
    if (!p) return false;
    p->value = v < p->min ? p->min : v > p->max ? p->max : v;
    return true;
}

/* ---- Per frame -------------------------------------------------------------------------- */

/*
 * The size of the picture the filter reads. An input scale N draws the game at
 * N x its own resolution. Automatic gives the CRT the rectangle it is shown in
 * (its ramps are anchored to the picture, so more source pixels only sharpen it)
 * and a custom preset the board's own resolution, which is what a libretro core
 * hands RetroArch and what CRT presets are written for.
 */
static inline void post_shader_source_size(int native_w, int native_h, int rect_w, int rect_h, int *sw, int *sh) {
    int n = g_post.input_scale;
    if (n > 0) { *sw = native_w * n; *sh = native_h * n; }
    else if (post_builtin()) { *sw = rect_w; *sh = rect_h; }
    else { *sw = native_w; *sh = native_h; }
    if (*sw < 1) *sw = 1;
    if (*sh < 1) *sh = 1;
    if (*sw > 4096) *sw = 4096;
    if (*sh > 4096) *sh = 4096;
}

/* Opens the offscreen pass the game is drawn into. */
static inline bool post_shader_begin_source(int sw, int sh, const sg_pass_action *action) {
    if (!post_shader_active()) return false;
    if (g_post.src_w != sw || g_post.src_h != sh) {
        post_free_source();
        g_post.src_color = sg_make_image(&(sg_image_desc){
            .usage = { .color_attachment = true }, .width = sw, .height = sh,
            .pixel_format = post_color_format(), .sample_count = 1, .label = "post-source" });
        g_post.src_depth = sg_make_image(&(sg_image_desc){
            .usage = { .depth_stencil_attachment = true }, .width = sw, .height = sh,
            .pixel_format = post_depth_format(), .sample_count = 1, .label = "post-source-depth" });
        g_post.src_color_att = sg_make_view(&(sg_view_desc){ .color_attachment.image = g_post.src_color });
        g_post.src_depth_att = sg_make_view(&(sg_view_desc){ .depth_stencil_attachment.image = g_post.src_depth });
        g_post.src_tex       = sg_make_view(&(sg_view_desc){ .texture.image = g_post.src_color });
        g_post.src_w = sw; g_post.src_h = sh;
    }
    sg_begin_pass(&(sg_pass){
        .action      = *action,
        .attachments = { .colors[0] = g_post.src_color_att, .depth_stencil = g_post.src_depth_att },
    });
    return true;
}

static inline void post_shader_end_source(void) {
    sg_end_pass();
    g_post.source_drawn = true;
}

static inline void post_crt_draw(const sg_pipeline pip[2]) {
    sg_apply_pipeline(pip[g_post.mode == POST_SHADER_ARCADE]);
    sg_apply_bindings(&(sg_bindings){
        .vertex_buffers[0] = g_game_render.quad_vbuf,
        .views[0]          = g_post.src_tex,
        .samplers[0]       = g_post.crt_sampler,
    });
    sg_draw(0, 6, 1);
}

/* Whatever the filter does offscreen, outside any pass. rect_w x rect_h is the
 * rectangle the picture will be shown in. */
static inline void post_shader_prepare(int rect_w, int rect_h) {
    g_post.up_used = false;
    if (!g_post.source_drawn || rect_w <= 0 || rect_h <= 0) return;
    if (post_builtin()) {
        if (rect_h >= POST_SHADER_CRT_MIN_LINES) return;   /* straight to the screen */
        const float up = (float)POST_SHADER_CRT_MIN_LINES / (float)rect_h;
        int uw = (int)((float)rect_w * up + 0.5f), uh = POST_SHADER_CRT_MIN_LINES;
        if (uw > 8192) uw = 8192;
        if (g_post.up_w != uw || g_post.up_h != uh) {
            post_free_up();
            g_post.up_color = sg_make_image(&(sg_image_desc){
                .usage = { .color_attachment = true }, .width = uw, .height = uh,
                .pixel_format = post_color_format(), .sample_count = 1, .label = "post-crt-1080" });
            g_post.up_att = sg_make_view(&(sg_view_desc){ .color_attachment.image = g_post.up_color });
            g_post.up_tex = sg_make_view(&(sg_view_desc){ .texture.image = g_post.up_color });
            g_post.up_w = uw; g_post.up_h = uh;
        }
        sg_begin_pass(&(sg_pass){
            .action = { .colors[0] = { .load_action = SG_LOADACTION_DONTCARE } },
            .attachments = { .colors[0] = g_post.up_att },
        });
        post_crt_draw(g_post.crt_offscreen);
        sg_end_pass();
        g_post.up_used = true;
    }
#ifdef RS_HAVE_GL
    else if (g_post.mode == POST_SHADER_CUSTOM && g_post.custom_loaded) {
        sg_gl_image_info info = sg_gl_query_image_info(g_post.src_color);
        rs_gl_prepare(info.tex[info.active_slot], g_post.src_w, g_post.src_h, rect_w, rect_h);
        sg_reset_state_cache();
    }
#endif
}

/* The filtered picture over (ox, oy, w, h) -- top-left origin, as
 * game_render_letterbox gives it -- inside the swapchain pass. */
static inline void post_shader_draw(int ox, int oy, int w, int h, int fb_w, int fb_h) {
    (void)fb_w;
    if (!g_post.source_drawn || w <= 0 || h <= 0) return;
    g_post.source_drawn = false;
    if (post_builtin()) {
        if (g_post.up_used) {
            game_render_draw_target(g_post.up_tex, true, ox, oy, w, h);
        } else {
            sg_apply_viewport(ox, oy, w, h, true);
            post_crt_draw(g_post.crt_screen);
        }
    }
#ifdef RS_HAVE_GL
    else if (g_post.mode == POST_SHADER_CUSTOM && g_post.custom_loaded) {
        rs_gl_final(ox, fb_h - oy - h, w, h);
        sg_reset_state_cache();
        /* What follows in this pass (menus, ImGui) expects the whole framebuffer. */
        sg_apply_viewport(0, 0, fb_w, fb_h, true);
    }
#endif
}

#endif /* POST_SHADER_H */
