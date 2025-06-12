/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2010 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org

    This file written by Ryan C. Gordon (icculus@icculus.org)
*/
#include "SDL_config.h"

/* Output audio to PSL1GHT */

#include "SDL.h"


#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "SDL_psl1ghtaudio.h"

#include <stdio.h> // For FILE operations
#include <stdarg.h> // For va_list, va_start, va_end

static FILE* g_sdl_psl1ght_log_file = NULL;
static const char* g_sdl_psl1ght_log_path = "/dev_hdd0/game/SMAP02024/USRDIR/sdl_psl1ght_audio.log";

#define SHW64(X) (u32)(((u64)X)>>32), (u32)(((u64)X)&0xFFFFFFFF)

// Forward declaration for our logging function
static void LogDePrintf(const char *format, ...);
static void LogClose(void); // <-- ADD THIS LINE

// Ensure AUDIO_DEBUG is defined to enable logging.
// Comment this out to disable all deprintf logging.
#define AUDIO_DEBUG

#ifdef AUDIO_DEBUG
#define deprintf(...) LogDePrintf(__VA_ARGS__)
#else
#define deprintf(...) (void)0  // Cast to void to prevent unused variable warnings
#endif

static int
PSL1GHT_AUD_OpenDevice(_THIS, void *handle, const char *devname, int iscapture)
{
	deprintf( "PSL1GHT_AUD_OpenDevice(%08X.%08X, %s, %d)\n", SHW64(this), devname, iscapture);
    SDL_AudioFormat test_format = SDL_FirstAudioFormat(this->spec.format);
    int valid_datatype = 0;

    this->hidden = SDL_malloc(sizeof(*(this->hidden)));
    if (!this->hidden) {
        SDL_OutOfMemory();
        return 0;
    }
    SDL_memset(this->hidden, 0, (sizeof *this->hidden));


	// PS3 Libaudio only handles floats
    while ((!valid_datatype) && (test_format)) {
        this->spec.format = test_format;
        switch (test_format) {
        case AUDIO_F32MSB:
            valid_datatype = 1;
            break;
        default:
            test_format = SDL_NextAudioFormat();
            break;
        }
    }

    int ret=audioInit();

	//set some parameters we want
	//either 2 or 8 channel
	_params.numChannels = AUDIO_PORT_2CH;
	//8 16 or 32 block buffer
	_params.numBlocks = AUDIO_BLOCK_8;
	//extended attributes
	_params.attrib = 0;
	//sound level (1 is default)
	_params.level = 1;

	ret=audioPortOpen(&_params, &_portNum);
	deprintf("audioPortOpen: %d\n",ret);
	deprintf("  portNum: %d\n",_portNum);

	ret=audioGetPortConfig(_portNum, &_config);
	deprintf("audioGetPortConfig: %d\n",ret);
	deprintf("  readIndex: 0x%8X\n",_config.readIndex);
	deprintf("  status: %d\n",_config.status);
	deprintf("  channelCount: %ld\n",_config.channelCount);
	deprintf("  numBlocks: %ld\n",_config.numBlocks);
	deprintf("  portSize: %d\n",_config.portSize);
	deprintf("  audioDataStart: 0x%8X\n",_config.audioDataStart);

	// create an event queue that will tell when a block is read
	ret=audioCreateNotifyEventQueue( &_snd_queue, &_snd_queue_key);
	printf("audioCreateNotifyEventQueue: %d\n",ret);

	// Set it to the sprx
	ret = audioSetNotifyEventQueue(_snd_queue_key);
	printf("audioSetNotifyEventQueue: %d\n",ret);

	// clears the event queue
	ret = sysEventQueueDrain(_snd_queue);
	printf("sysEentQueueDrain: %d\n",ret);

	ret=audioPortStart(_portNum);
	deprintf("audioPortStart: %d\n",ret);

	_last_filled_buf = _config.numBlocks - 1;

	this->spec.format = test_format;
	this->spec.size = sizeof(float) * AUDIO_BLOCK_SAMPLES * _config.channelCount;
	this->spec.freq = 48000;
	this->spec.samples = AUDIO_BLOCK_SAMPLES;
	this->spec.channels = _config.channelCount;

    return ret == 0;
}

static void
PSL1GHT_AUD_PlayDevice(_THIS)
{
	deprintf( "PSL1GHT_AUD_PlayDevice(%08X.%08X)\n", SHW64(this));
	
    // TransferSoundData *sound = SDL_malloc(sizeof(TransferSoundData));
    // if (!sound) {
    //     SDL_OutOfMemory();
    // }

    // playGenericSound(this->hidden->mixbuf, this->hidden->mixlen);
}


static void
PSL1GHT_AUD_CloseDevice(_THIS)
{
	LogClose(); // Call the new log closing function first

	deprintf( "PSL1GHT_AUD_CloseDevice(%08X.%08X)\n", SHW64(this));
	int ret = 0;
	ret=audioPortStop(_portNum);
	deprintf("audioPortStop: %d\n",ret);
	ret=audioRemoveNotifyEventQueue(_snd_queue_key);
	deprintf("audioRemoveNotifyEventQueue: %d\n",ret);
	ret=audioPortClose(_portNum);
	deprintf("audioPortClose: %d\n",ret);
	ret=sysEventQueueDestroy(_snd_queue, 0);
	deprintf("sysEventQueueDestroy: %d\n",ret);
	ret=audioQuit();
	deprintf("audioQuit: %d\n",ret);

    SDL_free(this->hidden);
}

static Uint8 *
PSL1GHT_AUD_GetDeviceBuf(_THIS)
{
	
	//deprintf( "PSL1GHT_AUD_GetDeviceBuf(%08X.%08X) at %d ms\n", SHW64(this), SDL_GetTicks());

    //int playing = _config.readIndex;
    // int playing = *((u64*)(u64)_config.readIndex);
    int filling = (_last_filled_buf + 1) % _config.numBlocks;
	Uint8 * dma_buf = (Uint8 *)(u64)_config.audioDataStart;
	//deprintf( "\tWriting to buffer %d \n", filling);
	// deprintf( "\tbuffer address (%08X.%08X => %08X.%08X)\n", SHW64(_config.audioDataStart), SHW64(dma_buf));

	_last_filled_buf = filling;
    return (dma_buf + (filling * this->spec.size));
}

/* This function waits until it is possible to write a full sound buffer */
static void
PSL1GHT_WaitDevice(_THIS)
{
    /* We're in blocking mode, so there's nothing to do here */
	//deprintf( "ALSA_WaitDevice(%08X.%08X)\n", SHW64(this));
	
	sys_event_t event;
	s32 ret;
	deprintf("PSL1GHT_WaitDevice: Called at %u ms. Waiting for event with timeout %d us...\n", SDL_GetTicks(), 20 * 1000);
	ret = sysEventQueueReceive( _snd_queue, &event, 20 * 1000);
	deprintf("PSL1GHT_WaitDevice: sysEventQueueReceive returned %d at %u ms.\n", ret, SDL_GetTicks());
	if (ret == 0) { // Assuming 0 is success for sysEventQueueReceive
		deprintf("PSL1GHT_WaitDevice: Event received: source=0x%llx, data_1=0x%llx, data_2=0x%llx, data_3=0x%llx\n",
				 event.source, event.data_1, event.data_2, event.data_3);
	} else {
		deprintf("PSL1GHT_WaitDevice: No event received or error occurred.\n");
	}
}

static void LogInit() {
    if (g_sdl_psl1ght_log_file == NULL) {
        g_sdl_psl1ght_log_file = fopen(g_sdl_psl1ght_log_path, "a+"); // "a+" for append and read, creates if not exists
        if (g_sdl_psl1ght_log_file != NULL) {
            printf("PSL1GHT_AUD: Log file %s opened successfully for appending.\n", g_sdl_psl1ght_log_path);
            fprintf(g_sdl_psl1ght_log_file, "--- LogInit: PSL1GHT SDL Audio Log Initialized ---\n");
            fflush(g_sdl_psl1ght_log_file);
        } else {
            // Keep this printf for console feedback, as deprintf isn't set up yet
            printf("PSL1GHT_AUD: ERROR: Failed to open log file %s. Check path and permissions.\n", g_sdl_psl1ght_log_path);
        }
    }
    // Not logging if already open to keep console quiet during normal operation.
    // If it's already open, we just continue using it.
}

static void LogDePrintf(const char *format, ...) {
    va_list args_console;
    va_list args_file;

    // Print to console
    va_start(args_console, format);
    vprintf(format, args_console);
    va_end(args_console);

    // Print to log file
    if (g_sdl_psl1ght_log_file != NULL) {
        va_start(args_file, format);
        vfprintf(g_sdl_psl1ght_log_file, format, args_file);
        va_end(args_file);
        fflush(g_sdl_psl1ght_log_file); // Ensure it's written out immediately
    }
}

static void LogClose() {
    if (g_sdl_psl1ght_log_file != NULL) {
        fprintf(g_sdl_psl1ght_log_file, "--- LogClose: PSL1GHT SDL Audio Log Closed ---\n");
        fflush(g_sdl_psl1ght_log_file); // Ensure final messages are written
        fclose(g_sdl_psl1ght_log_file);
        g_sdl_psl1ght_log_file = NULL;
        // Optional: print to console that log was closed
        printf("PSL1GHT_AUD: Log file %s closed.\n", g_sdl_psl1ght_log_path);
    }
}

	static int
PSL1GHT_AUD_Init(SDL_AudioDriverImpl * impl)
{
	LogInit(); // Call the new log initialization function

	deprintf( "PSL1GHT_AUD_Init(%08X.%08X)\n", SHW64(impl));
	/* Set the function pointers */
	impl->OpenDevice = PSL1GHT_AUD_OpenDevice;
	impl->PlayDevice = PSL1GHT_AUD_PlayDevice;
    impl->WaitDevice = PSL1GHT_WaitDevice;
	impl->CloseDevice = PSL1GHT_AUD_CloseDevice;
	impl->GetDeviceBuf = PSL1GHT_AUD_GetDeviceBuf;

    /* and the capabilities */
    impl->OnlyHasDefaultOutputDevice = 1;

    return 1;   /* this audio target is available. */
}

AudioBootStrap PSL1GHTAUDIO_bootstrap = {
    "psl1ght", "SDL PSL1GHT audio driver", PSL1GHT_AUD_Init, 0       /*1? */
};

/* vi: set ts=4 sw=4 expandtab: */
