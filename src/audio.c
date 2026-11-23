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
 * Everything is mixed in one SDL audio stream callback; state changes from the
 * game thread happen under SDL_LockAudioStream.
 */
#include "astro.h"

#include <FLAC/stream_decoder.h>
#include <SDL3/SDL.h>
#include <math.h>

#define MIX_RATE 44100
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
    FLAC__StreamDecoder *dec;
    int track;               /* 0 = stopped */
    bool eos;
    float buf[2 * 16384];    /* decoded frames waiting to be mixed */
    int buf_frames, buf_pos;
    uint64_t track_pos;      /* frames consumed in the current track */
    int paused_track;
    uint64_t paused_pos;
} music_t;

static SDL_AudioStream *g_stream;
static sound_t g_sounds[MAX_SOUNDS];
static int g_sound_count;
static music_t g_music;
static float g_sfx_gain = 1.0f, g_music_gain = 1.0f;

static float db100_to_gain(int db100) { return powf(10.0f, (float)db100 / 2000.0f); }

/* ---- FLAC ------------------------------------------------------------------ */

static FLAC__StreamDecoderWriteStatus flac_write(const FLAC__StreamDecoder *d, const FLAC__Frame *frame,
                                                 const FLAC__int32 *const buffer[], void *ud)
{
    music_t *m = ud;
    unsigned n = frame->header.blocksize, ch = frame->header.channels, bps = frame->header.bits_per_sample;
    float scale = 1.0f / (float)(1u << (bps - 1));
    if (n > sizeof m->buf / sizeof m->buf[0] / 2) n = sizeof m->buf / sizeof m->buf[0] / 2;
    for (unsigned i = 0; i < n; i++) {
        m->buf[2 * i] = (float)buffer[0][i] * scale;
        m->buf[2 * i + 1] = (float)buffer[ch > 1 ? 1 : 0][i] * scale;
    }
    m->buf_frames = (int)n;
    m->buf_pos = 0;
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void flac_error(const FLAC__StreamDecoder *d, FLAC__StreamDecoderErrorStatus s, void *ud)
{
    SDL_Log("FLAC decode error: %s", FLAC__StreamDecoderErrorStatusString[s]);
}

static void music_close(music_t *m)
{
    if (m->dec) { FLAC__stream_decoder_delete(m->dec); m->dec = NULL; }
    m->track = 0;
    m->buf_frames = m->buf_pos = 0;
}

/* Open a track (and seek); on failure the music simply stops. Caller holds the lock. */
static bool music_open(music_t *m, int track, uint64_t pos)
{
    music_close(m);
    if (track < FIRST_TRACK || track > LAST_TRACK) return false;
    char rel[64];
    SDL_snprintf(rel, sizeof rel, "music/track%02d.flac", track);
    char path[1200];
    SDL_snprintf(path, sizeof path, "%s/%s", assets_dir(), rel);
    m->dec = FLAC__stream_decoder_new();
    if (!m->dec) return false;
    if (FLAC__stream_decoder_init_file(m->dec, path, flac_write, NULL, flac_error, m) !=
        FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        SDL_Log("no music: cannot open %s", path);
        music_close(m);
        return false;
    }
    m->track = track;
    m->eos = false;
    SDL_Log("music: CD track %d%s", track, pos ? " (resumed)" : "");
    m->track_pos = pos;
    if (pos && !FLAC__stream_decoder_seek_absolute(m->dec, pos)) {
        SDL_Log("cannot seek %s to %llu", rel, (unsigned long long)pos);
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
        m->buf_frames = m->buf_pos = 0;
        if (!FLAC__stream_decoder_process_single(m->dec) ||
            FLAC__stream_decoder_get_state(m->dec) == FLAC__STREAM_DECODER_END_OF_STREAM) {
            if (m->buf_frames) continue;          /* last frame decoded together with EOS */
            int next = m->track + 1;
            if (next > LAST_TRACK) { music_close(m); return false; }
            music_open(m, next, 0);
        }
    }
    return false;
}

/* ---- mixer ------------------------------------------------------------------- */

static void SDLCALL mix_callback(void *ud, SDL_AudioStream *stream, int additional, int total)
{
    static float out[2 * 1024];
    int frames = additional / (int)(2 * sizeof(float));
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
        for (int i = 0; i < 2 * n; i++) out[i] = out[i] > 1.0f ? 1.0f : out[i] < -1.0f ? -1.0f : out[i];
        SDL_PutAudioStreamData(stream, out, (int)(sizeof(float) * 2 * (size_t)n));
        frames -= n;
    }
}

/* ---- public API -------------------------------------------------------------- */

#define SOUND_TABLE 0x46f1d0u     /* {char *name; int voices} per sound id */

bool audio_init(void)
{
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, MIX_RATE};
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, mix_callback, NULL);
    if (!g_stream) {
        SDL_Log("no audio device: %s", SDL_GetError());
        return false;
    }
    /* Decode every sound effect once, resampled to the mix rate. */
    SDL_AudioSpec mono = {SDL_AUDIO_F32, 1, MIX_RATE};
    for (int id = 0; id < MAX_SOUNDS; id++) {
        uint32_t name_va = rd32(SOUND_TABLE + 8u * (uint32_t)id);
        if (name_va < VA_DATA || name_va >= VA_DATA + 0xf2000u) break;
        sound_t *s = &g_sounds[id];
        s->voices = (int)rd32(SOUND_TABLE + 8u * (uint32_t)id + 4);
        if (s->voices < 1) s->voices = 1;
        if (s->voices > MAX_VOICES) s->voices = MAX_VOICES;
        char rel[96];
        SDL_snprintf(rel, sizeof rel, "sfx/%02d_%s.wav", id, (const char *)&mem[name_va]);
        size_t size = 0;
        void *file = assets_read(rel, &size);
        if (file) {
            SDL_AudioSpec src;
            Uint8 *wav = NULL, *pcm = NULL;
            Uint32 wav_len = 0;
            int pcm_len = 0;
            if (SDL_LoadWAV_IO(SDL_IOFromConstMem(file, size), true, &src, &wav, &wav_len) &&
                SDL_ConvertAudioSamples(&src, wav, (int)wav_len, &mono, &pcm, &pcm_len)) {
                s->pcm = (float *)pcm;
                s->frames = pcm_len / (int)sizeof(float);
            } else {
                SDL_Log("cannot decode %s: %s", rel, SDL_GetError());
            }
            SDL_free(wav);
            SDL_free(file);
        }
        g_sound_count = id + 1;
    }
    SDL_ResumeAudioStreamDevice(g_stream);
    return true;
}

void audio_shutdown(void)
{
    if (!g_stream) return;
    SDL_DestroyAudioStream(g_stream);
    g_stream = NULL;
    music_close(&g_music);
    for (int i = 0; i < g_sound_count; i++) SDL_free(g_sounds[i].pcm);
}

/* Sound_LoadSet / Sound_FreeSet: ids from a -1-terminated list in ASTRO.exe. */
void audio_load_set(uint32_t list_va, bool load)
{
    if (!g_stream) return;
    SDL_LockAudioStream(g_stream);
    for (uint32_t p = list_va; (int32_t)rd32(p) != -1; p += 4) {
        int32_t id = (int32_t)rd32(p);
        if (id < 0 || id >= g_sound_count) continue;
        sound_t *s = &g_sounds[id];
        memset(s->v, 0, sizeof s->v);          /* fresh buffers: centre, full volume */
        s->current = 0;
        s->loaded = load;
    }
    SDL_UnlockAudioStream(g_stream);
}

/* Sound_Play (0x407980) + DSnd_Play (0x401fa0) + DSnd_GetFreeBuffer (0x401e20). */
void audio_play(int id, unsigned flags, int pan, int volume)
{
    if (!g_stream || id < 0 || id >= g_sound_count) return;
    SDL_LockAudioStream(g_stream);
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
    SDL_UnlockAudioStream(g_stream);
}

/* Sound_Stop (0x407a20) / DSnd_StopAll: stop and rewind every voice. */
void audio_stop(int id)
{
    if (!g_stream || id < 0 || id >= g_sound_count) return;
    SDL_LockAudioStream(g_stream);
    for (int k = 0; k < MAX_VOICES; k++) { g_sounds[id].v[k].playing = false; g_sounds[id].v[k].pos = 0; }
    SDL_UnlockAudioStream(g_stream);
}

void audio_stop_set(uint32_t list_va)
{
    for (uint32_t p = list_va; (int32_t)rd32(p) != -1; p += 4) audio_stop((int)(int32_t)rd32(p));
}

/* Volumes: the original set the wave and CD-audio mixer levels with
 * auxSetVolume (0..0xffff); treated as linear amplitude here. */
void audio_set_volumes(unsigned sfx, unsigned cd)
{
    if (!g_stream) return;
    SDL_LockAudioStream(g_stream);
    g_sfx_gain = (float)(sfx & 0xffff) / 65535.0f;
    g_music_gain = (float)(cd & 0xffff) / 65535.0f;
    SDL_UnlockAudioStream(g_stream);
}

bool cd_play_track(int track)
{
    if (!g_stream) return false;
    SDL_LockAudioStream(g_stream);
    bool ok = music_open(&g_music, track, 0);
    g_music.paused_track = 0;
    SDL_UnlockAudioStream(g_stream);
    return ok;
}

void cd_stop(void)
{
    if (!g_stream) return;
    SDL_LockAudioStream(g_stream);
    music_close(&g_music);
    g_music.paused_track = 0;
    SDL_UnlockAudioStream(g_stream);
}

/* CD_Pause (0x401790): remember the position and stop. */
void cd_pause(void)
{
    if (!g_stream) return;
    SDL_LockAudioStream(g_stream);
    if (g_music.track) {
        g_music.paused_track = g_music.track;
        g_music.paused_pos = g_music.track_pos;
        music_close(&g_music);
    }
    SDL_UnlockAudioStream(g_stream);
}

/* CD_Resume (0x401830): play on from the remembered position. */
void cd_resume(void)
{
    if (!g_stream) return;
    SDL_LockAudioStream(g_stream);
    if (g_music.paused_track) {
        music_open(&g_music, g_music.paused_track, g_music.paused_pos);
        g_music.paused_track = 0;
    }
    SDL_UnlockAudioStream(g_stream);
}
