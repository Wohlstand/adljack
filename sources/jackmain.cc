//          Copyright Jean Pierre Cimalando 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "jackmain.h"
#include "common.h"
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#if !defined(_WIN32)
#    include <syslog.h>
#endif

static std::string program_title = "ADLjack";

static int process(jack_nframes_t nframes, void *user_data)
{
    const Audio_Context &ctx = *(Audio_Context *)user_data;

    void *midi = jack_port_get_buffer(ctx.midiport, nframes);
    float *left = (float *)jack_port_get_buffer(ctx.outport[0], nframes);
    float *right = (float *)jack_port_get_buffer(ctx.outport[1], nframes);

    for (jack_nframes_t i = 0; i < nframes; ++i) {
        jack_midi_event_t event;
        if (jack_midi_event_get(&event, midi, i) == 0)
            play_midi(event.buffer, event.size);
    }

    generate_outputs(left, right, nframes, 1);
    return 0;
}

static int setup_audio(const char *client_name, Audio_Context &ctx)
{
    jack_client_t *client(jack_client_open("ADLjack", JackNoStartServer, nullptr));
    if (!client) {
        fprintf(stderr, "Error creating Jack client.\n");
        return 1;
    }
    ctx.client.reset(client);

    ::program_title = std::string("ADLjack") + " [" + jack_get_client_name(client) + "]";

    ctx.midiport = jack_port_register(client, "MIDI", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput|JackPortIsTerminal, 0);
    ctx.outport[0] = jack_port_register(client, "left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
    ctx.outport[1] = jack_port_register(client, "right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);

    if (!ctx.midiport || !ctx.outport[0] || !ctx.outport[1]) {
        fprintf(stderr, "Error creating Jack ports.\n");
        return 1;
    }

    jack_nframes_t bufsize = jack_get_buffer_size(client);
    unsigned samplerate = jack_get_sample_rate(client);
    fprintf(stderr, "Jack client \"%s\" fs=%u bs=%u\n",
            jack_get_client_name(client), samplerate, bufsize);

    if (!initialize_player(arg_player_type, samplerate, arg_nchip, arg_bankfile, arg_emulator))
        return 1;

    jack_set_process_callback(client, process, &ctx);
    return 0;
}

#if defined(ADLJACK_USE_NSM)
static bool session_is_open = false;

int session_open(const char *name, const char *display_name, const char *client_id, char **out_msg, void *user_data)
{
    Audio_Context &ctx = *(Audio_Context *)user_data;

    if (::session_is_open)
        return 1;

    if (setup_audio(client_id, ctx) != 0)
        return 1;

    jack_client_t *client = ctx.client.get();
    jack_activate(client);
    player_ready();

    /* TODO */

    ::session_is_open = true;
    return 0;
}

int session_save(char **out_msg, void *user_data)
{
    Audio_Context &ctx = *(Audio_Context *)user_data;

    /* TODO */

    return 0;
}

int session_main(const char *url, const char *progname, Audio_Context &ctx)
{
    nsm_client_u nsm;
    nsm.reset(nsm_new());
    nsm_set_open_callback(nsm.get(), &session_open, &ctx);
    nsm_set_save_callback(nsm.get(), &session_save, &ctx);
    if (nsm_init(nsm.get(), url) != 0) {
        fprintf(stderr, "Error initializing session management.");
        return 1;
    }
    ctx.nsm = nsm.get();

    //
    nsm_send_announce(nsm.get(), "ADLjack", "", progname);

    //
    auto idle_proc =
        [](void *user_data) {
            Audio_Context &ctx = *(Audio_Context *)user_data;
            nsm_check_nowait(ctx.nsm);
        };
    interface_exec(+idle_proc, &ctx);

    //
    nsm.reset();
    if (jack_client_t *client = ctx.client.get())
        jack_deactivate(client);

    return 0;
}
#endif

int audio_main(const char *progname)
{
    Audio_Context ctx;

#if defined(ADLJACK_USE_NSM)
    if (const char *nsm_url = getenv("NSM_URL"))
        return session_main(nsm_url, progname, ctx);
#endif

    if (setup_audio("ADLjack", ctx) != 0)
        return 1;

    jack_client_t *client = ctx.client.get();
    jack_activate(client);
    player_ready();

    //
    interface_exec(nullptr, nullptr);

    //
    jack_deactivate(client);

    return 0;
}

static void usage()
{
    generic_usage("adljack", "");
}

std::string get_program_title()
{
    return ::program_title;
}

int main(int argc, char *argv[])
{
    for (int c; (c = generic_getopt(argc, argv, "", usage)) != -1;) {
        switch (c) {
        default:
            usage();
            return 1;
        }
    }

    if (argc != optind)
        return 1;

#if !defined(_WIN32)
    openlog("ADLjack", 0, LOG_USER);
#endif

    handle_signals();
    return audio_main(argv[0]);
}
