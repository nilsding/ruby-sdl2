#ifdef HAVE_SDL_MIXER_H
#include "rubysdl2_internal.h"
#include <SDL_mixer.h>

static VALUE mMixer;
static VALUE cChunk;
static VALUE cMusic;

static VALUE playing_chunks = Qnil;
static VALUE playing_music = Qnil;

#define MIX_ERROR() do { HANDLE_ERROR(SDL_SetError(Mix_GetError())); } while(0)
#define HANDLE_MIX_ERROR(code) \
    do { if ((code) < 0) { MIX_ERROR(); } } while (0)

typedef struct Chunk {
    Mix_Chunk* chunk;
} Chunk;

typedef struct Music {
    Mix_Music* music;
} Music;

void Chunk_free(Chunk* c)
{
    if (c->chunk) 
        Mix_FreeChunk(c->chunk);
    free(c);
}

static VALUE Chunk_new(Mix_Chunk* chunk)
{
    Chunk* c = ALLOC(Chunk);
    c->chunk = chunk;
    return Data_Wrap_Struct(cChunk, 0, Chunk_free, c);
}

DEFINE_WRAPPER(Mix_Chunk, Chunk, chunk, cChunk, "SDL2::Mixer::Chunk");

void Music_free(Music* m)
{
    if (m->music) 
        Mix_FreeMusic(m->music);
    free(m);
}

static VALUE Music_new(Mix_Music* music)
{
    Music* c = ALLOC(Music);
    c->music = music;
    return Data_Wrap_Struct(cMusic, 0, Music_free, c);
}
                            
DEFINE_WRAPPER(Mix_Music, Music, music, cMusic, "SDL2::Mixer::Music");

VALUE Mixer_s_init(VALUE self, VALUE f)
{
    int flags = NUM2INT(f);
    if (Mix_Init(flags) & flags != flags) 
        rb_raise(eSDL2Error, "Couldn't initialize SDL_mixer");
    
    return Qnil;
}

static void check_channel(VALUE ch, int allow_minus_1)
{
    int channel = NUM2INT(ch);
    if (channel >= Mix_AllocateChannels(-1))
        rb_raise(rb_eArgError, "too large number of channel (%d)", channel);
    if (channel == -1 && !allow_minus_1 || channel < -1)
        rb_raise(rb_eArgError, "negative number of channel is not allowed");
}

static VALUE Mixer_s_open(int argc, VALUE* argv, VALUE self)
{
    VALUE freq, format, channels, chunksize;
    rb_scan_args(argc, argv, "04", &freq, &format, &channels, &chunksize);
    HANDLE_MIX_ERROR(Mix_OpenAudio((freq == Qnil) ? MIX_DEFAULT_FREQUENCY : NUM2INT(freq),
                                   (format == Qnil) ? MIX_DEFAULT_FORMAT : NUM2UINT(format),
                                   (channels == Qnil) ? 2 : NUM2INT(channels),
                                   (chunksize == Qnil) ? 1024 : NUM2INT(chunksize)));
    playing_chunks = rb_ary_new();
    return Qnil;
}

static VALUE Mixer_s_close(VALUE self)
{
    Mix_CloseAudio();
    return Qnil;
}

static VALUE Mixer_s_query(VALUE self)
{
    int frequency = 0, channels = 0, num_opened;
    Uint16 format = 0;

    num_opened = Mix_QuerySpec(&frequency, &format, &channels);
    return rb_ary_new3(4, INT2NUM(frequency), UINT2NUM(format),
                       INT2NUM(channels), INT2NUM(num_opened));
}

static void protect_playing_chunk_from_gc(int channel, VALUE chunk)
{
    rb_ary_store(playing_chunks, channel, chunk);
}

static VALUE Mixer_s_play_channel(int argc, VALUE* argv, VALUE self)
{
    VALUE channel, chunk, loops, ticks;
    int ch;
    rb_scan_args(argc, argv, "31", &channel, &chunk, &loops, &ticks);
    if (ticks == Qnil)
        ticks = INT2FIX(-1);
    check_channel(channel, 1);
    ch = Mix_PlayChannelTimed(NUM2INT(channel), Get_Mix_Chunk(chunk),
                              NUM2INT(loops), NUM2INT(ticks));
    HANDLE_MIX_ERROR(ch);
    protect_playing_chunk_from_gc(ch, chunk);
    return INT2FIX(ch);
}

static VALUE Mixer_s_fade_in_channel(int argc, VALUE* argv, VALUE self)
{
    VALUE channel, chunk, loops, ms, ticks;
    int ch;
    rb_scan_args(argc, argv, "41", &channel, &chunk, &loops, &ms, &ticks);
    if (ticks == Qnil)
        ticks = INT2FIX(-1);
    check_channel(channel, 1);
    ch = Mix_FadeInChannelTimed(NUM2INT(channel), Get_Mix_Chunk(chunk),
                                NUM2INT(loops), NUM2INT(ms), NUM2INT(ticks));
    HANDLE_MIX_ERROR(ch);
    protect_playing_chunk_from_gc(ch, chunk);
    return INT2FIX(ch);
}

static VALUE Mixer_s_pause(VALUE self, VALUE channel)
{
    check_channel(channel, 1);
    Mix_Pause(NUM2INT(channel));
    return Qnil;
}

static VALUE Mixer_s_resume(VALUE self, VALUE channel)
{
    check_channel(channel, 1);
    Mix_Resume(NUM2INT(channel));
    return Qnil;
}

static VALUE Mixer_s_halt_channel(VALUE self, VALUE channel)
{
    check_channel(channel, 1);
    Mix_HaltChannel(NUM2INT(channel));
    return Qnil;
}

static VALUE Mixer_s_expire_channel(VALUE self, VALUE channel, VALUE ticks)
{
    check_channel(channel, 1);
    Mix_ExpireChannel(NUM2INT(channel), NUM2INT(ticks));
    return Qnil;
}

static VALUE Mixer_s_fade_out_channel(VALUE self, VALUE channel, VALUE ms)
{
    check_channel(channel, 1);
    Mix_FadeOutChannel(NUM2INT(channel), NUM2INT(ms));
    return Qnil;
}

static VALUE Mixer_s_play_p(VALUE self, VALUE channel)
{
    check_channel(channel, 0);
    return INT2BOOL(Mix_Playing(NUM2INT(channel)));
}

static VALUE Mixer_s_pause_p(VALUE self, VALUE channel)
{
    check_channel(channel, 0);
    return INT2BOOL(Mix_Paused(NUM2INT(channel)));
}

static VALUE Mixer_s_fading(VALUE self, VALUE which)
{
    check_channel(which, 0);
    return INT2FIX(Mix_FadingChannel(NUM2INT(which)));
}

static VALUE Mixer_s_playing_chunk(VALUE self, VALUE channel)
{
    check_channel(channel, 0);
    return rb_ary_entry(playing_chunks, NUM2INT(channel));
}

static VALUE Mixer_s_play_music(VALUE self, VALUE music, VALUE loops)
{
    HANDLE_MIX_ERROR(Mix_PlayMusic(Get_Mix_Music(music), NUM2INT(loops)));
    return Qnil;
}

static VALUE Chunk_s_load(VALUE self, VALUE fname)
{
    Mix_Chunk* chunk = Mix_LoadWAV(StringValueCStr(fname));
    VALUE c;
    if (!chunk)
        MIX_ERROR();
    c = Chunk_new(chunk);
    rb_iv_set(c, "@filename", fname);
    return c;
}

static VALUE Chunk_s_decoders(VALUE self)
{
    int i;
    int num_decoders = Mix_GetNumChunkDecoders();
    VALUE ary = rb_ary_new();
    for (i=0; i < num_decoders; ++i)
        rb_ary_push(ary, rb_usascii_str_new_cstr(Mix_GetChunkDecoder(i)));
    return ary;
}

static VALUE Chunk_destroy(VALUE self)
{
    Chunk* c = Get_Chunk(self);
    if (c->chunk) Mix_FreeChunk(c->chunk);
    c->chunk = NULL;
    return Qnil;
}

static VALUE Chunk_volume(VALUE self)
{
    return INT2NUM(Mix_VolumeChunk(Get_Mix_Chunk(self), -1));
}

static VALUE Chunk_set_volume(VALUE self, VALUE vol)
{
    return INT2NUM(Mix_VolumeChunk(Get_Mix_Chunk(self), NUM2INT(vol)));
}

static VALUE Chunk_inspect(VALUE self)
{
    VALUE filename = rb_iv_get(self, "@filename");
    return rb_sprintf("<%s: filename=\"%s\" volume=%d>",
                      rb_obj_classname(self),
                      StringValueCStr(filename),
                      Mix_VolumeChunk(Get_Mix_Chunk(self), -1));
}

static VALUE Music_s_decoders(VALUE self)
{
    int num_decoders = Mix_GetNumMusicDecoders();
    int i;
    VALUE decoders = rb_ary_new2(num_decoders);
    for (i=0; i<num_decoders; ++i)
        rb_ary_push(decoders, utf8str_new_cstr(Mix_GetMusicDecoder(i)));
    return decoders;
}


static VALUE Music_s_load(VALUE self, VALUE fname)
{
    Mix_Music* music = Mix_LoadMUS(StringValueCStr(fname));
    VALUE mus;
    if (!music) MIX_ERROR();
    mus = Music_new(music);
    rb_iv_set(mus, "@filename", fname);
    return mus;
}

static VALUE Music_destroy(VALUE self)
{
    Music* c = Get_Music(self);
    if (c) Mix_FreeMusic(c->music);
    c->music = NULL;
    return Qnil;
}

static VALUE Music_inspect(VALUE self)
{
    VALUE filename = rb_iv_get(self, "@filename");
    return rb_sprintf("<%s: filename=\"%s\" type=%d>",
                      rb_obj_classname(self), StringValueCStr(filename),
                      Mix_GetMusicType(Get_Mix_Music(self)));
}

void rubysdl2_init_mixer(void)
{
    mMixer = rb_define_module_under(mSDL2, "Mixer");

    rb_define_module_function(mMixer, "init", Mixer_s_init, 1);
    rb_define_module_function(mMixer, "open", Mixer_s_open, -1);
    rb_define_module_function(mMixer, "close", Mixer_s_close, 0);
    rb_define_module_function(mMixer, "query", Mixer_s_query, 0);
    rb_define_module_function(mMixer, "play_channel", Mixer_s_play_channel, -1);
    rb_define_module_function(mMixer, "fade_in_channel", Mixer_s_fade_in_channel, -1);
    rb_define_module_function(mMixer, "pause", Mixer_s_pause, 1);
    rb_define_module_function(mMixer, "resume", Mixer_s_resume, 1);
    rb_define_module_function(mMixer, "halt_channel", Mixer_s_halt_channel, 1);
    rb_define_module_function(mMixer, "expire_channel", Mixer_s_expire_channel, 2);
    rb_define_module_function(mMixer, "fade_out_channel", Mixer_s_fade_out_channel, 2);
    rb_define_module_function(mMixer, "play?", Mixer_s_play_p, 1);
    rb_define_module_function(mMixer, "pause?", Mixer_s_pause_p, 1);
    rb_define_module_function(mMixer, "fading", Mixer_s_fading, 1);
    rb_define_module_function(mMixer, "playing_chunk", Mixer_s_playing_chunk, 1);
    rb_define_module_function(mMixer, "play_music", Mixer_s_play_music, 2);
    
    
#define DEFINE_MIX_INIT(t) \
    rb_define_const(mMixer, "INIT_" #t, UINT2NUM(MIX_INIT_##t))
    DEFINE_MIX_INIT(FLAC);
    DEFINE_MIX_INIT(MOD);
    DEFINE_MIX_INIT(MODPLUG);
    DEFINE_MIX_INIT(MP3);
    DEFINE_MIX_INIT(OGG);
    DEFINE_MIX_INIT(FLUIDSYNTH);

#define DEFINE_MIX_FORMAT(t) \
    rb_define_const(mMixer, "FORMAT_" #t, UINT2NUM(AUDIO_##t))
    DEFINE_MIX_FORMAT(U8);
    DEFINE_MIX_FORMAT(S8);
    DEFINE_MIX_FORMAT(U16LSB);
    DEFINE_MIX_FORMAT(S16LSB);
    DEFINE_MIX_FORMAT(U16MSB);
    DEFINE_MIX_FORMAT(S16MSB);
    DEFINE_MIX_FORMAT(U16SYS);
    DEFINE_MIX_FORMAT(S16SYS);
    rb_define_const(mMixer, "DEFAULT_FORMAT", UINT2NUM(MIX_DEFAULT_FORMAT));
    rb_define_const(mMixer, "DEFAULT_CHANNELS", INT2FIX(MIX_DEFAULT_CHANNELS));
    rb_define_const(mMixer, "MAX_VOLUME", INT2FIX(MIX_MAX_VOLUME));
    
    
    cChunk = rb_define_class_under(mMixer, "Chunk", rb_cObject);
    rb_undef_alloc_func(cChunk);
    rb_define_singleton_method(cChunk, "load", Chunk_s_load, 1);
    rb_define_singleton_method(cChunk, "decoders", Chunk_s_decoders, 0);
    rb_define_method(cChunk, "destroy", Chunk_destroy, 0);
    rb_define_method(cChunk, "destroy?", Chunk_destroy_p, 0);
    rb_define_method(cChunk, "volume", Chunk_volume, 0);
    rb_define_method(cChunk, "volume=", Chunk_set_volume, 1);
    rb_define_method(cChunk, "inspect", Chunk_inspect, 0);
    rb_define_attr(cChunk, "filename", 1, 0);
    
    cMusic = rb_define_class_under(mMixer, "Music", rb_cObject);
    rb_undef_alloc_func(cMusic);
    rb_define_singleton_method(cMusic, "decoders", Music_s_decoders, 0);
    rb_define_singleton_method(cMusic, "load", Music_s_load, 1);
    rb_define_method(cMusic, "destroy", Music_destroy, 0);
    rb_define_method(cMusic, "destroy?", Music_destroy_p, 0);
    rb_define_method(cMusic, "inspect", Music_inspect, 0);
    
    rb_gc_register_address(&playing_chunks);
    rb_gc_register_address(&playing_music);
}

#else /* HAVE_SDL_MIXER_H */
void rubysdl2_init_mixer(void)
{
}
#endif
