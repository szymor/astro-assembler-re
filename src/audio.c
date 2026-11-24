/*
 * Audio: sound effects with the original DirectSound semantics, and the CD
 * audio the game expected, played from the soundtrack FLACs.
 *
 * Sound effects (original: dsutil-style sound objects, 0x401cc0..0x4022b0):
 *  - each sound id has N DirectSound buffers ("voices") and a current index;
 *  - play: use the current voice if idle; if it is busy, a 1-voice sound is
 *    not restarted, otherwise the index advances round-robin and that voice
 *    is cut off and restarted (0x401e20);
 *  - looping is only honoured for 1-voice sounds (0x401fa0);
 *  - pan/volume are set only when their flag is given and otherwise persist
 *    per voice, as DirectSound buffers keep them; units are hundredths of dB;
 *  - a voice that finishes or is stopped rewinds to the start.
 *
 * CD audio (original: MCI, Cdplayer.cpp): MCI_PLAY from a track without an end
 * position, i.e. playback continues through the following tracks to the end of
 * the disc. Track N is music/trackNN.flac (2..16).
 *
 * Everything is mixed by audio_mix(), which the frontend's audio callback
 * calls; state changes from the game thread happen under plat_audio_lock().
 * FLAC decoding: dr_flac (third_party/dr_flac).
 */
#include "astro.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "dr_flac.h"

#define MAX_SOUNDS 128
#define MAX_VOICES 8
#define FIRST_TRACK 2
#define LAST_TRACK 16

typedef struct {
    bool playing, loop;
    int pos;                 /* frame position in the sound's PCM */
    int pan, volume;         /* hundredths of dB, DirectSound conventions */
} voice_t;

typedef struct {
    float *pcm;              /* mono, MIX_RATE */
    int frames;
    int voices;
    int current;
    bool loaded;             /* part of the currently loaded sound set */
    voice_t v[MAX_VOICES];
} sound_t;

typedef struct {
    drflac *dec;
    FILE *file;
    int track;               /* 0 = stopped */
    float buf[2 * 4096];     /* decoded frames waiting to be mixed */
    int buf_frames, buf_pos;
    uint64_t track_pos;      /* frames consumed in the current track */
    int paused_track;
    uint64_t paused_pos;
} music_t;

static bool g_open;          /* audio output available */
static sound_t g_sounds[MAX_SOUNDS];
static int g_sound_count;
static music_t g_music;
static float g_sfx_gain = 1.0f, g_music_gain = 1.0f;

static float db100_to_gain(int db100) { return powf(10.0f, (float)db100 / 2000.0f); }

/* ---- FLAC (dr_flac reading through our own FILE*, UTF-8 safe) ------------- */

static size_t flac_read(void *ud, void *out, size_t n) { return fread(out, 1, n, (FILE *)ud); }

static drflac_bool32 flac_seek(void *ud, int offset, drflac_seek_origin origin)
{
    int whence = origin == DRFLAC_SEEK_CUR ? SEEK_CUR : origin == DRFLAC_SEEK_END ? SEEK_END : SEEK_SET;
    return fseek((FILE *)ud, offset, whence) == 0;
}

static drflac_bool32 flac_tell(void *ud, drflac_int64 *cursor)
{
    long pos = ftell((FILE *)ud);
    if (pos < 0) return DRFLAC_FALSE;
    *cursor = pos;
    return DRFLAC_TRUE;
}

static void music_close(music_t *m)
{
    if (m->dec) { drflac_close(m->dec); m->dec = NULL; }
    if (m->file) { fclose(m->file); m->file = NULL; }
    m->track = 0;
    m->buf_frames = m->buf_pos = 0;
}

/* Open a track (and seek); on failure the music simply stops. Caller holds the lock. */
static bool music_open(music_t *m, int track, uint64_t pos)
{
    music_close(m);
    if (track < FIRST_TRACK || track > LAST_TRACK) return false;
    char rel[64];
    snprintf(rel, sizeof rel, "music/track%02d.flac", track);
    m->file = assets_open(rel);
    m->dec = m->file ? drflac_open(flac_read, flac_seek, flac_tell, m->file, NULL) : NULL;
    if (!m->dec) {
        astro_log("no music: cannot open %s/%s", assets_dir(), rel);
        music_close(m);
        return false;
    }
    m->track = track;
    astro_log("music: CD track %d%s", track, pos ? " (resumed)" : "");
    m->track_pos = pos;
    if (pos && !drflac_seek_to_pcm_frame(m->dec, pos)) {
        astro_log("cannot seek %s to %llu", rel, (unsigned long long)pos);
        m->track_pos = 0;
    }
    return true;
}

/* Next decoded frame pair; continues into the following track like a CD. */
static bool music_next(music_t *m, float *l, float *r)
{
    while (m->track) {
        if (m->buf_pos < m->buf_frames) {
            *l = m->buf[2 * m->buf_pos];
            *r = m->buf[2 * m->buf_pos + 1];
            m->buf_pos++;
            m->track_pos++;
            return true;
        }
        m->buf_pos = 0;
        if (m->dec->channels == 2) {
            m->buf_frames = (int)drflac_read_pcm_frames_f32(m->dec, 4096, m->buf);
        } else {                                  /* mono (not used by the soundtrack) */
            float mono[4096];
            m->buf_frames = (int)drflac_read_pcm_frames_f32(m->dec, 4096, mono);
            for (int i = 0; i < m->buf_frames; i++) m->buf[2 * i] = m->buf[2 * i + 1] = mono[i];
        }
        if (m->buf_frames == 0) {                 /* end of track: on to the next, like a CD */
            int next = m->track + 1;
            if (next > LAST_TRACK) { music_close(m); return false; }
            music_open(m, next, 0);
        }
    }
    return false;
}

/* ---- WAV (PCM 8/16-bit) and resampling to the mix rate ----------------------- */

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/*
 * The effects are mono 8/16-bit PCM at 11025 or 22050 Hz, an exact quarter or
 * half of the mix rate. They are converted once to float at the mix rate with
 * linear interpolation, close to what DirectSound's software mixer did.
 */
static float *wav_to_float(const uint8_t *d, size_t size, int *frames_out)
{
    if (size < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return NULL;
    int channels = 0, rate = 0, bits = 0;
    const uint8_t *pcm = NULL;
    uint32_t pcm_len = 0;
    for (size_t p = 12; p + 8 <= size;) {
        uint32_t len = le32(d + p + 4);
        if (p + 8 + len > size) len = (uint32_t)(size - p - 8);
        if (!memcmp(d + p, "fmt ", 4) && len >= 16) {
            if (le16(d + p + 8) != 1) return NULL;            /* PCM only */
            channels = le16(d + p + 10);
            rate = (int)le32(d + p + 12);
            bits = le16(d + p + 22);
        } else if (!memcmp(d + p, "data", 4)) {
            pcm = d + p + 8;
            pcm_len = len;
        }
        p += 8 + len + (len & 1);
    }
    if (!pcm || channels < 1 || rate <= 0 || (bits != 8 && bits != 16)) return NULL;
    int bps = bits / 8 * channels, in_frames = (int)(pcm_len / (uint32_t)bps);
    if (in_frames <= 0) return NULL;
    float *in = malloc(sizeof(float) * (size_t)in_frames);
    if (!in) return NULL;
    for (int i = 0; i < in_frames; i++) {
        float sum = 0;
        for (int c = 0; c < channels; c++) {
            const uint8_t *s = pcm + (size_t)i * (size_t)bps + (size_t)c * (size_t)(bits / 8);
            sum += bits == 8 ? ((float)s[0] - 128.0f) / 128.0f : (float)(int16_t)le16(s) / 32768.0f;
        }
        in[i] = sum / (float)channels;
    }
    int out_frames = (int)((int64_t)in_frames * MIX_RATE / rate);
    float *out = malloc(sizeof(float) * (size_t)(out_frames > 0 ? out_frames : 1));
    if (!out) { free(in); return NULL; }
    for (int i = 0; i < out_frames; i++) {
        double pos = (double)i * rate / MIX_RATE;
        int k = (int)pos;
        float t = (float)(pos - k), a = in[k], b = k + 1 < in_frames ? in[k + 1] : in[k];
        out[i] = a + (b - a) * t;
    }
    free(in);
    *frames_out = out_frames;
    return out;
}

/* ---- mixer ------------------------------------------------------------------- */

/*
 * Soft limiter on the final mix. The original mixed the CD music in the sound
 * card's analog mixer, separately from the effects, so the two never clipped
 * against each other; here they share one digital sum.
 *
 * Below LIM_THRESHOLD the limiter does nothing (music alone never gets there:
 * its peaks at the default CD volume are 0.875). It delays the output by
 * LIM_LOOKAHEAD frames (1.45 ms) so it sees peaks coming: the gain ramps down
 * linearly to exactly what the loudest upcoming frame needs by the time that
 * frame is played (no overshoot, no clipping), then recovers over ~200 ms so
 * the change is not audible as a click.
 */
#define LIM_THRESHOLD 0.9f
#define LIM_LOOKAHEAD 64

static float g_lim_gain = 1.0f;
static float g_lim_buf[2 * LIM_LOOKAHEAD];   /* delayed samples (stereo)   */
static float g_lim_req[LIM_LOOKAHEAD];       /* gain each delayed frame needs */
static int g_lim_pos;                        /* oldest frame = next output  */

static void limit(float *buf, int frames)
{
    static float release;
    if (release == 0.0f) {
        release = expf(-1.0f / (0.200f * MIX_RATE));
        for (int i = 0; i < LIM_LOOKAHEAD; i++) g_lim_req[i] = 1.0f;
    }
    for (int i = 0; i < frames; i++) {
        /* take the oldest delayed frame out, put the new one in its place */
        float l = g_lim_buf[2 * g_lim_pos], r = g_lim_buf[2 * g_lim_pos + 1];
        float in_l = buf[2 * i], in_r = buf[2 * i + 1];
        float peak = fmaxf(fabsf(in_l), fabsf(in_r));
        g_lim_buf[2 * g_lim_pos] = in_l;
        g_lim_buf[2 * g_lim_pos + 1] = in_r;
        float req_out = g_lim_req[g_lim_pos];
        g_lim_req[g_lim_pos] = peak > LIM_THRESHOLD ? LIM_THRESHOLD / peak : 1.0f;
        g_lim_pos = (g_lim_pos + 1) % LIM_LOOKAHEAD;

        /* lowest gain needed by the frame being output or any frame within the
         * look-ahead, and how many frames away the first such frame is */
        float need = req_out;
        int dist = 0;
        for (int k = 0; k < LIM_LOOKAHEAD; k++) {
            float q = g_lim_req[(g_lim_pos + k) % LIM_LOOKAHEAD];
            if (q < need) { need = q; dist = k + 1; }
        }
        if (need < g_lim_gain)
            g_lim_gain -= (g_lim_gain - need) / (float)(dist + 1);   /* arrive exactly in time */
        else
            g_lim_gain = release * g_lim_gain + (1.0f - release) * need;
        if (g_lim_gain > req_out) g_lim_gain = req_out;             /* never above this frame's need */

        l *= g_lim_gain;
        r *= g_lim_gain;
        buf[2 * i] = l > 1.0f ? 1.0f : l < -1.0f ? -1.0f : l;      /* safety net only */
        buf[2 * i + 1] = r > 1.0f ? 1.0f : r < -1.0f ? -1.0f : r;
    }
}

/* Mix `frames` stereo frames; called from the frontend's audio callback. */
void audio_mix(float *out, int frames)
{
    while (frames > 0) {
        int n = frames > 1024 ? 1024 : frames;
        memset(out, 0, sizeof(float) * 2 * (size_t)n);
        for (int s = 0; s < g_sound_count; s++) {
            sound_t *snd = &g_sounds[s];
            if (!snd->loaded || !snd->pcm) continue;
            for (int k = 0; k < snd->voices; k++) {
                voice_t *v = &snd->v[k];
                if (!v->playing) continue;
                float g = db100_to_gain(v->volume) * g_sfx_gain, gl = g, gr = g;
                if (v->pan > 0) gl *= db100_to_gain(-v->pan);
                if (v->pan < 0) gr *= db100_to_gain(v->pan);
                for (int i = 0; i < n; i++) {
                    float x = snd->pcm[v->pos];
                    out[2 * i] += x * gl;
                    out[2 * i + 1] += x * gr;
                    if (++v->pos >= snd->frames) {
                        v->pos = 0;
                        if (!v->loop) { v->playing = false; break; }
                    }
                }
            }
        }
        for (int i = 0; i < n; i++) {
            float l, r;
            if (!music_next(&g_music, &l, &r)) break;
            out[2 * i] += l * g_music_gain;
            out[2 * i + 1] += r * g_music_gain;
        }
        limit(out, n);
        out += 2 * n;
        frames -= n;
    }
}

/* ---- public API -------------------------------------------------------------- */

#define SOUND_TABLE 0x46f1d0u     /* {char *name; int voices} per sound id */

bool audio_init(void)
{
    /* Decode every sound effect once, resampled to the mix rate. */
    for (int id = 0; id < MAX_SOUNDS; id++) {
        uint32_t name_va = rd32(SOUND_TABLE + 8u * (uint32_t)id);
        if (name_va < VA_DATA || name_va >= VA_DATA + 0xf2000u) break;
        sound_t *s = &g_sounds[id];
        s->voices = (int)rd32(SOUND_TABLE + 8u * (uint32_t)id + 4);
        if (s->voices < 1) s->voices = 1;
        if (s->voices > MAX_VOICES) s->voices = MAX_VOICES;
        char rel[96];
        snprintf(rel, sizeof rel, "sfx/%02d_%s.wav", id, (const char *)&mem[name_va]);
        size_t size = 0;
        uint8_t *file = assets_read(rel, &size);
        if (file) {
            s->pcm = wav_to_float(file, size, &s->frames);
            if (!s->pcm) astro_log("cannot decode %s", rel);
            free(file);
        }
        g_sound_count = id + 1;
    }
    g_open = plat_audio_open(MIX_RATE, audio_mix);
    if (!g_open) astro_log("no audio output; the game runs without sound");
    return g_open;
}

void audio_shutdown(void)
{
    if (!g_open) return;
    plat_audio_close();
    g_open = false;
    music_close(&g_music);
    for (int i = 0; i < g_sound_count; i++) { free(g_sounds[i].pcm); g_sounds[i].pcm = NULL; }
}

/* Sound_LoadSet / Sound_FreeSet: ids from a -1-terminated list in ASTRO.exe. */
void audio_load_set(uint32_t list_va, bool load)
{
    if (!g_open) return;
    plat_audio_lock();
    for (uint32_t p = list_va; (int32_t)rd32(p) != -1; p += 4) {
        int32_t id = (int32_t)rd32(p);
        if (id < 0 || id >= g_sound_count) continue;
        sound_t *s = &g_sounds[id];
        memset(s->v, 0, sizeof s->v);          /* fresh buffers: centre, full volume */
        s->current = 0;
        s->loaded = load;
    }
    plat_audio_unlock();
}

/* Sound_Play (0x407980) + DSnd_Play (0x401fa0) + DSnd_GetFreeBuffer (0x401e20). */
void audio_play(int id, unsigned flags, int pan, int volume)
{
    if (!g_open || id < 0 || id >= g_sound_count) return;
    plat_audio_lock();
    sound_t *s = &g_sounds[id];
    bool loop = flags & 1;
    if (s->loaded && (!loop || s->voices == 1)) {
        voice_t *v = &s->v[s->current];
        if (v->playing) {
            if (s->voices < 2) {
                v = NULL;                       /* already playing: not restarted */
            } else {
                s->current = (s->current + 1) % s->voices;
                v = &s->v[s->current];
                v->playing = false;             /* cut off and rewind */
                v->pos = 0;
            }
        }
        if (v) {
            if (flags & 2) v->pan = pan;
            if (flags & 4) v->volume = volume;
            v->loop = loop;
            v->playing = true;
        }
    }
    plat_audio_unlock();
}

/* Sound_Stop (0x407a20) / DSnd_StopAll: stop and rewind every voice. */
void audio_stop(int id)
{
    if (!g_open || id < 0 || id >= g_sound_count) return;
    plat_audio_lock();
    for (int k = 0; k < MAX_VOICES; k++) { g_sounds[id].v[k].playing = false; g_sounds[id].v[k].pos = 0; }
    plat_audio_unlock();
}

void audio_stop_set(uint32_t list_va)
{
    for (uint32_t p = list_va; (int32_t)rd32(p) != -1; p += 4) audio_stop((int)(int32_t)rd32(p));
}

/* Volumes: the original set the wave and CD-audio mixer levels with
 * auxSetVolume (0..0xffff); treated as linear amplitude here. */
void audio_set_volumes(unsigned sfx, unsigned cd)
{
    if (!g_open) return;
    plat_audio_lock();
    g_sfx_gain = (float)(sfx & 0xffff) / 65535.0f;
    g_music_gain = (float)(cd & 0xffff) / 65535.0f;
    plat_audio_unlock();
}

bool cd_play_track(int track)
{
    if (!g_open) return false;
    plat_audio_lock();
    bool ok = music_open(&g_music, track, 0);
    g_music.paused_track = 0;
    plat_audio_unlock();
    return ok;
}

void cd_stop(void)
{
    if (!g_open) return;
    plat_audio_lock();
    music_close(&g_music);
    g_music.paused_track = 0;
    plat_audio_unlock();
}

/* CD_Pause (0x401790): remember the position and stop. */
void cd_pause(void)
{
    if (!g_open) return;
    plat_audio_lock();
    if (g_music.track) {
        g_music.paused_track = g_music.track;
        g_music.paused_pos = g_music.track_pos;
        music_close(&g_music);
    }
    plat_audio_unlock();
}

/* CD_Resume (0x401830): play on from the remembered position. */
void cd_resume(void)
{
    if (!g_open) return;
    plat_audio_lock();
    if (g_music.paused_track) {
        music_open(&g_music, g_music.paused_track, g_music.paused_pos);
        g_music.paused_track = 0;
    }
    plat_audio_unlock();
}
