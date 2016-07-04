#include "Sound.h"
#include "../util/Logger.h"
#include "../util/OptionsDB.h"
#include "../util/Directories.h"

#ifdef FREEORION_MACOSX
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/alc.h>
#include <AL/al.h>
#endif

#include <map>

#include <boost/scoped_array.hpp>

#include <vorbis/vorbisfile.h>

class Sound::SoundImpl
{
public:
    SoundImpl();  ///< ctor.
    ~SoundImpl(); ///< dotr.

    /** Enables(disables) the sound system if enable is true(false).. */
    void Enable(bool enable);

    /**Initialize OpenAL*/
    void InitOpenAL();

    /**Shutdown OpenAL.*/
    void ShutdownOpenAL();

    /** Plays a music file.  The file will be played in an infinitve loop if \a loop is < 0, and it will be played \a
        loops + 1 times otherwise. */
    void PlayMusic(const boost::filesystem::path& path, int loops = 0);

    /** Stops playing music. */
    void StopMusic();

    /** Plays a sound file. */
    void PlaySound(const boost::filesystem::path& path, bool is_ui_sound = false);

    /** Frees the cached sound data associated with the filename. */
    void FreeSound(const boost::filesystem::path& path);

    /** Frees all cached sound data. */
    void FreeAllSounds();

    /** Sets the music volume from 0 (muted) to 255 (full volume); \a vol is range-adjusted. */
    void SetMusicVolume(int vol);

    /** Sets the UI sounds volume from 0 (muted) to 255 (full volume); \a vol is range-adjusted. */
    void SetUISoundsVolume(int vol);

    /** Does the work that must be done by the sound system once per frame. */
    void DoFrame();

    bool UISoundsTemporarilyDisabled() const;

    /**Increment the temporary disabled depth.*/
    void IncDisabledCount();

    /** Decrease the temporary disabled depth.*/
    void DecDisabledCount();

private:
    static const int NUM_SOURCES = 16; // The number of sources for OpenAL to create. Should be 2 or more.
    static const int NUM_MUSIC_BUFFERS = 2; // The number of music buffers.

    ALuint                            m_sources[NUM_SOURCES]; ///< OpenAL sound sources. The first one is used for music
    int                               m_music_loops;          ///< the number of loops of the current music to play (< 0 for loop forever)
    std::string                       m_music_name;           ///< the name of the currently-playing music file
    std::map<std::string, ALuint>     m_buffers;              ///< the currently-cached (and possibly playing) sounds, if any; keyed on filename
    ALuint                            m_music_buffers[NUM_MUSIC_BUFFERS];     ///< two additional buffers for music. statically defined as they'll be changed many times.
    OggVorbis_File                    m_ogg_file;             ///< the currently open ogg file
    ALenum                            m_ogg_format;           ///< mono or stereo
    ALsizei                           m_ogg_freq;             ///< sampling frequency
    unsigned int                      m_temporary_disable_count; ///< Count of the number of times sound was disabled. Sound is enabled when this is zero.
    /** m_initialized indicates if the sound system has been initialized.
        The system will not be initialized if both sound effects and
        music are disabled or if initialization failed. */
    bool                              m_initialized;
};

namespace {
    const int BUFFER_SIZE = 409600; // The size of the buffer we read music data into.

#ifdef FREEORION_WIN32
    int _fseek64_wrap(FILE *f, ogg_int64_t off, int whence) {
        if (!f)
            return -1;
        return fseek(f,off,whence);
    }
#endif

    int RefillBuffer(OggVorbis_File* ogg_file, ALenum ogg_format, ALsizei ogg_freq,
                     ALuint bufferName, ogg_int64_t buffer_size, int& loops)
    {
        ALenum m_openal_error;
        int endian = 0; /// 0 for little-endian (x86), 1 for big-endian (ppc)
        int bitStream,bytes,bytes_new;
        boost::scoped_array<char> array(new char[buffer_size]);
        bytes = 0;

        if (alcGetCurrentContext() != 0) {
            /* First, let's fill up the buffer. We need the loop, as ov_read treats (buffer_size - bytes) to read as a suggestion only */
            do {
                bytes_new = ov_read(ogg_file, &array[bytes],(buffer_size - bytes), endian, 2, 1, &bitStream);
                bytes += bytes_new;
                if (bytes_new == 0) {
                    if (loops != 0) {   // enter here if we need to play the same file again
                        if (loops > 0)
                            loops--;
                        ov_time_seek(ogg_file,0.0); // rewind to beginning
                    }
                    else
                        break;
                }
            } while ((buffer_size - bytes) > 4096);
            if (bytes > 0) {
                alBufferData(bufferName, ogg_format, array.get(), static_cast < ALsizei > (bytes), ogg_freq);
                m_openal_error = alGetError();
                if (m_openal_error != AL_NONE)
                    ErrorLogger() << "RefillBuffer: OpenAL ERROR: " << alGetString(m_openal_error);
            } else {
                ov_clear(ogg_file); // the app might think we still have something to play.
                return 1;
            }
            return 0;
        }
        return 1;
    }
}

////////////////////////////////////////////////////////////
// TempUISoundDisabler
////////////////////////////////////////////////////////////
Sound::TempUISoundDisabler::TempUISoundDisabler()
{ Sound::GetSound().pimpl->IncDisabledCount(); }

Sound::TempUISoundDisabler::~TempUISoundDisabler()
{ Sound::GetSound().pimpl->DecDisabledCount(); }

////////////////////////////////////////////////////////////
// Sound
////////////////////////////////////////////////////////////
Sound& Sound::GetSound()
{
    static Sound sound;
    return sound;
}

Sound::Sound() :
    pimpl(new SoundImpl)
{}

// Require here because SoundImpl is defined in this file.
Sound::~Sound() {}

void Sound::Enable(bool enable)
{ pimpl->Enable(enable); }

void Sound::PlayMusic(const boost::filesystem::path& path, int loops /* = 0*/)
{ pimpl->PlayMusic(path, loops); }

void Sound::StopMusic()
{ pimpl->StopMusic(); }

void Sound::PlaySound(const boost::filesystem::path& path, bool is_ui_sound/* = false*/)
{ pimpl->PlaySound(path, is_ui_sound); }

void Sound::FreeSound(const boost::filesystem::path& path)
{ pimpl->FreeSound(path); }

void Sound::FreeAllSounds()
{ pimpl->FreeAllSounds(); }

void Sound::SetMusicVolume(int vol)
{ pimpl->SetMusicVolume(vol); }

void Sound::SetUISoundsVolume(int vol)
{ pimpl->SetUISoundsVolume(vol); }

void Sound::DoFrame()
{ pimpl->DoFrame(); }



////////////////////////////////////////////////////////////
// SoundImpl
////////////////////////////////////////////////////////////

Sound::SoundImpl::SoundImpl() :
    m_sources(),
    m_music_loops(0),
    m_music_name(),
    m_buffers(),
    m_music_buffers(),
    m_ogg_file(),
    m_ogg_format(),
    m_ogg_freq(),
    m_temporary_disable_count(0),
    m_initialized(false)
{
    if (!GetOptionsDB().Get<bool>("UI.sound.enabled") && !GetOptionsDB().Get<bool>("UI.sound.music-enabled"))
        return;

    Enable(true);
}

Sound::SoundImpl::~SoundImpl() {
    Enable(false);
}

void Sound::SoundImpl::Enable(bool enable) {
    if (enable == m_initialized)
        return;

    if (enable) {
        InitOpenAL();
    } else {

        StopMusic();

        ShutdownOpenAL();

        m_initialized = false;
    }

    //Reset playing audio
    m_music_loops = 0;
    m_buffers.clear();
    m_temporary_disable_count = 0;

    DebugLogger() << "Audio " << (m_initialized ? "enabled." : "diabled.");

}

    /// Initialize OpenAl and return true on success.
void Sound::SoundImpl::InitOpenAL() {
    ALCcontext *m_context;
    ALCdevice *m_device;
    ALenum error_code;

    m_device = alcOpenDevice(0);    /* currently only select the default output device - usually a NULL-terminated
                                     * string desctribing a device can be passed here (of type ALchar*) */
    if (m_device == 0) {
        ErrorLogger() << "Unable to initialise OpenAL device: " << alGetString(alGetError()) << "\n";
        m_initialized = false;
        return;
    } else {
        m_context = alcCreateContext(m_device, 0);   // instead of 0 we can pass a ALCint* pointing to a set of
        // attributes (ALC_FREQUENCY, ALC_REFRESH and ALC_SYNC)

        if ((m_context != 0) && (alcMakeContextCurrent(m_context) == AL_TRUE)) {
            alListenerf(AL_GAIN,1.0);
            alGetError(); // clear possible previous errors (just to be certain)
            alGenSources(NUM_SOURCES, m_sources);
            error_code = alGetError();
            if(error_code != AL_NO_ERROR) {
                ErrorLogger() << "Unable to create OpenAL sources: " << alGetString(error_code) << "\n" << "Disabling OpenAL sound system!\n";
                alcMakeContextCurrent(0);
                alcDestroyContext(m_context);
                m_initialized = false;
                return;
            } else {
                alGetError();
                alGenBuffers(NUM_MUSIC_BUFFERS, m_music_buffers);
                error_code = alGetError();
                if (error_code != AL_NO_ERROR) {
                    ErrorLogger() << "Unable to create OpenAL buffers: " << alGetString(error_code) << "\n" << "Disabling OpenAL sound system!\n";
                    alDeleteBuffers(NUM_MUSIC_BUFFERS, m_music_buffers);
                    alcMakeContextCurrent(0);
                    alcDestroyContext(m_context);
                    m_initialized = false;
                    return;
                } else {
                    for (int i = 0; i < NUM_SOURCES; ++i) {
                        alSourcei(m_sources[i], AL_SOURCE_RELATIVE, AL_TRUE);
                    }
                    DebugLogger() << "OpenAL initialized. Version "
                                  << alGetString(AL_VERSION)
                                  << "Renderer "
                                  << alGetString(AL_RENDERER)
                                  << "Vendor "
                                  << alGetString(AL_VENDOR) << "\n"
                                  << "Extensions: "
                                  << alGetString(AL_EXTENSIONS) << "\n";
                }
            }
        } else {
            ErrorLogger() << "Unable to create OpenAL context : " << alGetString(alGetError()) << "\n";
            m_initialized = false;
            return;
        }
    }
    m_initialized = true;
}

void Sound::SoundImpl::ShutdownOpenAL() {

    if (alcGetCurrentContext() != 0) {
        alDeleteSources(NUM_SOURCES, m_sources); // Automatically stops currently playing sources

        alDeleteBuffers(NUM_MUSIC_BUFFERS, m_music_buffers);
        for (std::map<std::string, ALuint>::iterator itr = m_buffers.begin(); itr != m_buffers.end(); ++itr)
            alDeleteBuffers(1, &itr->second );
    }

    ALCcontext* context = alcGetCurrentContext();
    if (context != 0) {
        ALCdevice* device = alcGetContextsDevice(context);
        alcMakeContextCurrent(0);
        alcDestroyContext(context);
        if (device != 0)
            alcCloseDevice(device);
    }
}


void Sound::SoundImpl::PlayMusic(const boost::filesystem::path& path, int loops /* = 0*/) {
    if (!m_initialized)
        return;

    ALenum m_openal_error;
    std::string filename = PathString(path);
    FILE* m_f = 0;
    vorbis_info* vorbis_info;
    m_music_loops = 0;

#ifdef FREEORION_WIN32
    ov_callbacks callbacks = {

    (size_t (*)(void *, size_t, size_t, void *))  fread,

    (int (*)(void *, ogg_int64_t, int))           _fseek64_wrap,

    (int (*)(void *))                             fclose,

    (long (*)(void *))                            ftell

    };
#endif

    if (alcGetCurrentContext() != 0)
    {
        if (m_music_name.size() > 0)
            StopMusic();
       
        if ((m_f = fopen(filename.c_str(), "rb")) != 0) // make sure we CAN open it
        {
#ifdef FREEORION_WIN32
            if (!(ov_test_callbacks(m_f, &m_ogg_file, 0, 0, callbacks))) // check if it's a proper ogg
#else
            if (!(ov_test(m_f, &m_ogg_file, 0, 0))) // check if it's a proper ogg
#endif
            {
                ov_test_open(&m_ogg_file); // it is, now fully open the file
                /* now we need to take some info we will need later */
                vorbis_info = ov_info(&m_ogg_file, -1);
                if (vorbis_info->channels == 1)
                    m_ogg_format = AL_FORMAT_MONO16;
                else
                    m_ogg_format = AL_FORMAT_STEREO16;
                m_ogg_freq = vorbis_info->rate;
                m_music_loops = loops;
                /* fill up the buffers and queue them up for the first time */
                if (!RefillBuffer(&m_ogg_file, m_ogg_format, m_ogg_freq, m_music_buffers[0], BUFFER_SIZE, m_music_loops))
                {
                    alSourceQueueBuffers(m_sources[0],1,&m_music_buffers[0]); // queue up the buffer if we manage to fill it
                    if (!RefillBuffer(&m_ogg_file, m_ogg_format, m_ogg_freq, m_music_buffers[1], BUFFER_SIZE, m_music_loops))
                    {
                        alSourceQueueBuffers(m_sources[0],1,&m_music_buffers[1]);
                        m_music_name = filename; // yup, we're playing something that takes up more than 2 buffers
                    }
                    else
                    {
                        m_music_name.clear();  // m_music_name.clear() must always be called before ov_clear. Otherwise
                    }
                    alSourcePlay(m_sources[0]); // play if at least one buffer is queued
                }
                else
                {
                    m_music_name.clear();  // m_music_name.clear() must always be called before ov_clear. Otherwise
                }
            }
            else
            {
                ErrorLogger() << "PlayMusic: unable to open file " << filename.c_str() << " possibly not a .ogg vorbis file. Aborting\n";
                m_music_name.clear(); //just in case
                ov_clear(&m_ogg_file);
            }
        }
        else
            ErrorLogger() << "PlayMusic: unable to open file " << filename.c_str() << " I/O Error. Aborting\n";
    }
    m_openal_error = alGetError();
    if (m_openal_error != AL_NONE)
        ErrorLogger() << "PlayMusic: OpenAL ERROR: " << alGetString(m_openal_error);
}

void Sound::SoundImpl::StopMusic()
{
    if (!m_initialized)
        return;

    if (alcGetCurrentContext() != 0)
    {
        alSourceStop(m_sources[0]);
        if (m_music_name.size() > 0)
        {
            m_music_name.clear();  // do this to avoid music being re-started by other functions
            ov_clear(&m_ogg_file); // and unload the file for good measure. the file itself is closed now, don't re-close it again
        }
        alSourcei(m_sources[0], AL_BUFFER, 0);
    }
}

void Sound::SoundImpl::PlaySound(const boost::filesystem::path& path, bool is_ui_sound/* = false*/)
{
    if (!GetOptionsDB().Get<bool>("UI.sound.enabled") || (is_ui_sound && UISoundsTemporarilyDisabled()))
        return;

    Enable(true);

    std::string filename = PathString(path);
    ALuint current_buffer;
    ALenum source_state;
    ALsizei ogg_freq;
    FILE *file = 0;
    int m_i;
    bool found_buffer = false;
    bool found_source = false;

#ifdef FREEORION_WIN32
    ov_callbacks callbacks = {

    (size_t (*)(void *, size_t, size_t, void *))  fread,

    (int (*)(void *, ogg_int64_t, int))           _fseek64_wrap,

    (int (*)(void *))                             fclose,

    (long (*)(void *))                            ftell

    };
#endif

    if (alcGetCurrentContext() != 0)
    {
        /* First check if the sound data of the file we want to play is already buffered somewhere */
        std::map<std::string, ALuint>::iterator it = m_buffers.find(filename);
        if (it != m_buffers.end()) {
            current_buffer = it->second;
            found_buffer = true;
        } else {
            if ((file = fopen(filename.c_str(), "rb")) != 0) // make sure we CAN open it
            {
                OggVorbis_File ogg_file;
                vorbis_info *vorbis_info;
                ALenum ogg_format;
#ifdef FREEORION_WIN32
                if (!(ov_test_callbacks(file, &ogg_file, 0, 0, callbacks))) // check if it's a proper ogg
#else
                if (!(ov_test(file, &ogg_file, 0, 0))) // check if it's a proper ogg
#endif
                {
                    ov_test_open(&ogg_file); // it is, now fully open the file
                    /* now we need to take some info we will need later */
                    vorbis_info = ov_info(&ogg_file, -1);
                    if (vorbis_info->channels == 1)
                        ogg_format = AL_FORMAT_MONO16;
                    else
                        ogg_format = AL_FORMAT_STEREO16;
                    ogg_freq = vorbis_info->rate;
                    ogg_int64_t byte_size = ov_pcm_total(&ogg_file, -1) * vorbis_info->channels * 2;
                    if (byte_size <= 1024 * 1024 * 1024) {
                        /* fill up the buffers and queue them up for the first time */
                        ALuint sound_handle;
                        alGenBuffers(1, &sound_handle);

                        int loop = 0;

                        RefillBuffer(&ogg_file, ogg_format, ogg_freq, sound_handle, byte_size, loop);

                        current_buffer = sound_handle;
                        found_buffer = true;
                        m_buffers.insert(std::make_pair(filename, sound_handle));
                    }
                    else
                    {
                        ErrorLogger() << "PlaySound: unable to open file " << filename.c_str() << " too big to buffer. Aborting\n";
                    }
                    ov_clear(&ogg_file);
                }
                else
                {
                    ErrorLogger() << "PlaySound: unable to open file " << filename.c_str() << " possibly not a .ogg vorbis file. Aborting\n";
                }
            }
        }
        if (found_buffer) {
            /* Now that we have the buffer, we need to find a source to send it to */
            for (m_i = 1; m_i < NUM_SOURCES; ++m_i) {   // as we're playing sounds we start at 1. 0 is reserved for music
                alGetSourcei(m_sources[m_i],AL_SOURCE_STATE,&source_state);
                if ((source_state != AL_PLAYING) && (source_state != AL_PAUSED)) {
                    found_source = true;
                    alSourcei(m_sources[m_i], AL_BUFFER, current_buffer);
                    alSourcePlay(m_sources[m_i]);
                    break; // so that the sound won't block all the sources
                }
            }
            if (!found_source)
                ErrorLogger() << "PlaySound: Could not find aviable source - playback aborted\n";
        }
        source_state = alGetError();
        if (source_state != AL_NONE)
            ErrorLogger() << "PlaySound: OpenAL ERROR: " << alGetString(source_state);
            /* it's important to check for errors, as some functions won't work properly if
             * they're called when there is a unchecked previous error. */
    }
}

void Sound::SoundImpl::FreeSound(const boost::filesystem::path& path) {
    if (!m_initialized)
        return;

    ALenum m_openal_error;
    std::string filename = PathString(path);
    std::map<std::string, ALuint>::iterator it = m_buffers.find(filename);

    if (it != m_buffers.end()) {
        alDeleteBuffers(1, &(it->second));
        m_openal_error = alGetError();
        if (m_openal_error != AL_NONE)
            ErrorLogger() << "FreeSound: OpenAL ERROR: " << alGetString(m_openal_error);
        else
            m_buffers.erase(it); /* we don't erase if there was an error, as the buffer may not have been
                                    removed - potential memory leak */
    }
}

void Sound::SoundImpl::FreeAllSounds() {
    if (!m_initialized)
        return;

    ALenum m_openal_error;

    for (std::map<std::string, ALuint>::iterator it = m_buffers.begin();
         it != m_buffers.end();)
    {
        alDeleteBuffers(1, &(it->second));
        m_openal_error = alGetError();
        if (m_openal_error != AL_NONE) {
            ErrorLogger() << "FreeAllSounds: OpenAL ERROR: " << alGetString(m_openal_error);
            ++it;
        } else {
            std::map<std::string, ALuint>::iterator temp = it;
            ++it;
            m_buffers.erase(temp);  // invalidates erased iterator only
        }
    }
}

void Sound::SoundImpl::SetMusicVolume(int vol) {
    if (!m_initialized)
        return;

    ALenum m_openal_error;

    /* normalize value, then apply to all sound sources */
    vol = std::max(0, std::min(vol, 255));
    GetOptionsDB().Set<int>("UI.sound.music-volume", vol);
    if (alcGetCurrentContext() != 0)
    {
        alSourcef(m_sources[0],AL_GAIN, ((ALfloat) vol)/255.0);
        /* it is highly unlikely that we'll get an error here but better safe than sorry */
        m_openal_error = alGetError();
        if (m_openal_error != AL_NONE)
            ErrorLogger() << "PlaySound: OpenAL ERROR: " << alGetString(m_openal_error);
    }
}

void Sound::SoundImpl::SetUISoundsVolume(int vol) {
    if (!m_initialized)
        return;

    ALenum m_openal_error;

    /* normalize value, then apply to all sound sources */
    vol = std::max(0, std::min(vol, 255));
    GetOptionsDB().Set<int>("UI.sound.volume", vol);
    if (alcGetCurrentContext() != 0) {
        for (int it = 1; it < NUM_SOURCES; ++it)
            alSourcef(m_sources[it],AL_GAIN, ((ALfloat) vol)/255.0);
        /* it is highly unlikely that we'll get an error here but better safe than sorry */
        m_openal_error = alGetError();
        if (m_openal_error != AL_NONE)
            ErrorLogger() << "PlaySound: OpenAL ERROR: " << alGetString(m_openal_error);
    }
}

void Sound::SoundImpl::DoFrame() {
    if (!m_initialized)
        return;

    ALint    state;
    int      num_buffers_processed;

    if ((alcGetCurrentContext() != 0) && (m_music_name.size() > 0)) {
        alGetSourcei(m_sources[0],AL_BUFFERS_PROCESSED,&num_buffers_processed);
        while (num_buffers_processed > 0) {
            ALuint buffer_name_yay;
            alSourceUnqueueBuffers (m_sources[0], 1, &buffer_name_yay);
            if (RefillBuffer(&m_ogg_file, m_ogg_format, m_ogg_freq, buffer_name_yay, BUFFER_SIZE, m_music_loops))
            {
                m_music_name.clear();  // m_music_name.clear() must always be called before ov_clear. Otherwise
                break; /// this happens if RefillBuffer returns 1, meaning it encountered EOF and the file shouldn't be repeated
            }
            alSourceQueueBuffers(m_sources[0],1,&buffer_name_yay);
            num_buffers_processed--;
        }
        alGetSourcei(m_sources[0], AL_SOURCE_STATE, &state);
        if (state == AL_STOPPED)  /// this may happen if the source plays all its buffers before we manage to refill them
            alSourcePlay(m_sources[0]);
    }
} 

bool Sound::SoundImpl::UISoundsTemporarilyDisabled() const
{ return m_temporary_disable_count > 0; }

void Sound::SoundImpl::IncDisabledCount()
{ ++m_temporary_disable_count; }

void Sound::SoundImpl::DecDisabledCount()
{ --m_temporary_disable_count; }
