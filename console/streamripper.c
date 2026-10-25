/* streamripper.c
 * This little app should be seen as a demo for how to use the stremripper lib. 
 * The only file you need from the /lib dir is rip_mananger.h, and perhapes 
 * util.h (for stuff like formating the number of bytes).
 * 
 * the two functions of interest are main() for how to call rip_mananger_start
 * and rip_callback, which is a how you find out whats going on with the rip.
 * the rest of this file is really just boring command line parsing code.
 * and a signal handler for when the user hits CTRL+C
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#if WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include "srtypes.h"
#include "callback.h"
#include "rip_manager.h"
#include "prefs.h"
#include "mchar.h"
#include "filelib.h"
#include "debug.h"
#include "console_output.h"

/*****************************************************************************
 * Private functions
 *****************************************************************************/
static void print_usage (FILE* stream);
static void print_status (RIP_MANAGER_INFO *rmi);
static void catch_sig (int code);
static void parse_arguments (STREAM_PREFS *prefs, int argc, char **argv);
static void rip_callback (RIP_MANAGER_INFO* rmi, int message, void *data);
static void parse_extended_options (STREAM_PREFS *prefs, const char *rule);
static void verify_splitpoint_rules (STREAM_PREFS *prefs);
static void print_to_console (char* fmt, ...);

/*****************************************************************************
 * Private variables
 *****************************************************************************/
static char m_buffer_chars[] = {'\\', '|', '/', '-', '*'}; /* for formating */
//static RIP_MANAGER_INFO 	m_curinfo; /* from the rip_manager callback */
static BOOL			m_started = FALSE;
static BOOL			m_alldone = FALSE;
static volatile sig_atomic_t m_got_sig = FALSE;
static BOOL			m_dont_print = FALSE;
static BOOL			m_print_stderr = FALSE;
static int                      m_exit_code = EXIT_SUCCESS;
time_t				m_stop_time = 0;

#ifdef WIN32
static HANDLE m_stop_event;
static HANDLE m_shutdown_complete;

static BOOL WINAPI
console_control_handler(DWORD event)
{
    switch (event) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        SetEvent(m_stop_event);
        /* Returning from a close handler terminates the process immediately.
           Let the main thread finalize writers and apply incomplete retention. */
        WaitForSingleObject(m_shutdown_complete, INFINITE);
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

/* main()
 * parse the aguments, tell the rip_mananger to start, we get in rip
 * status from our rip_callback function. m_opt was set from parse args
 * and contains all the various things one can do with the rip_mananger
 * like a relay server, auto-reconnects, output dir's stuff like that.
 *
 * Notice the crappy while loop, this is because the streamripper lib 
 * is asyncrouns (spelling?) only. It probably should have a blocking 
 * call as well, but i needed this for window'd apps.
 */

int
main (int argc, char *argv[])
{
    int ret;
    time_t temp_time;
    STREAM_PREFS prefs;
    RIP_MANAGER_INFO *rmi = 0;

#ifdef SR_MODERN_WINDOWS
    argv = g_win32_get_command_line();
    argc = g_strv_length(argv);
#endif
    sr_set_locale ();

    signal (SIGINT, catch_sig);
    signal (SIGTERM, catch_sig);

    parse_arguments (&prefs, argc, argv);

    print_to_console ("Connecting...\n");
    
    rip_manager_init ();

#ifdef WIN32
    m_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    m_shutdown_complete = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!m_stop_event || !m_shutdown_complete ||
        !SetConsoleCtrlHandler(console_control_handler, TRUE)) {
        sr_console_printf(stderr, "Unable to install console shutdown handler.\n");
        return EXIT_FAILURE;
    }
#endif

    /* Launch the ripping thread */
    if ((ret = rip_manager_start (&rmi, &prefs, rip_callback)) != SR_SUCCESS) {
	sr_console_printf(stderr, "Couldn't connect to %s\n", prefs.url);
	exit(1);
    }

    /* 
     * The m_got_sig thing is because you can't call into a thread 
     * (i.e. rip_manager_stop) from a signal handler.. or at least not
     * in FreeBSD 3.4, i don't know about linux or NT.
     */
    while (!m_got_sig && !m_alldone) {
#ifdef WIN32
        if (WaitForSingleObject(m_stop_event, 1000) == WAIT_OBJECT_0) {
            m_got_sig = TRUE;
            break;
        }
#else
	sleep(1);
#endif
	time(&temp_time);
	if (m_stop_time && (temp_time >= m_stop_time)) {
	    print_to_console ("\nTime to stop is here, bailing\n");
	    break; 
	}	
    }

    print_to_console (m_got_sig ? "\nshutting down\n" : "shutting down\n");

    rip_manager_stop (rmi);
    rip_manager_cleanup ();
    prefs_cleanup();
    free(rmi);
#ifdef WIN32
    /* Event handles live until process exit so an active handler can use them. */
    SetEvent(m_shutdown_complete);
#endif
#ifdef SR_MODERN_WINDOWS
    g_strfreev(argv);
#endif

    return m_exit_code;
}

void
catch_sig(int code)
{
    if (!m_started)
        exit(2);
    m_got_sig = TRUE;
}

/* In 1.63 I have changed two things: sending output to stdout 
   instead of stderr, and adding fflush as per the sticky in the forum. */
static void
print_to_console (char* fmt, ...)
{
    va_list argptr;

    if (!m_dont_print) {
	va_start (argptr, fmt);

	sr_console_vprintf(m_print_stderr ? stderr : stdout, fmt, argptr);

	va_end (argptr);
    }
}

/* 
 * This is to handle RM_UPDATE messages from rip_callback(), and more
 * importantly the RIP_MANAGER_INFO struct. Most of the code here
 * is for handling the pretty formating stuff otherwise it could be
 * much smaller.
 */
void
print_status (RIP_MANAGER_INFO *rmi)
{
    STREAM_PREFS *prefs = rmi->prefs;
    char status_str[128];
    char duration_str[32];
    static int buffering_tick = 0;
    static BOOL printed_fullinfo = FALSE;

    if (m_dont_print)
	return;

    if (printed_fullinfo && rmi->callback_filename[0]) {

	switch(rmi->status)
	{
	case RM_STATUS_BUFFERING:
	    buffering_tick++;
	    if (buffering_tick == 5)
		buffering_tick = 0;

	    sprintf(status_str,"buffering - %c ",
		    m_buffer_chars[buffering_tick]);

            sr_console_status(m_print_stderr ? stderr : stdout, status_str,
                              rmi->callback_filename, NULL);
	    break;

	case RM_STATUS_RIPPING:
	    if (rmi->callback_filtered) {
		strcpy(status_str, "filtered...   ");
	    } else if (rmi->callback_incomplete) {
		strcpy(status_str, "incomplete... ");
	    } else {
		strcpy(status_str, "ripping...    ");
	    }
            if (!rmi->callback_duration_known) {
                guint64 bytes = rmi->callback_filesize;
                if (bytes < 1000)
                    g_snprintf(duration_str, sizeof(duration_str), "%" G_GUINT64_FORMAT "B", bytes);
                else if (bytes < 1000000)
                    g_snprintf(duration_str, sizeof(duration_str), "%" G_GUINT64_FORMAT "kB", bytes / 1000);
                else {
                    guint64 hundredths = (bytes + 5000) / 10000;
                    g_snprintf(duration_str, sizeof(duration_str), "%" G_GUINT64_FORMAT ",%02uMB",
                               hundredths / 100, (unsigned)(hundredths % 100));
                }
            } else {
                guint64 seconds = rmi->callback_duration_ms / 1000;
                if (seconds >= 3600)
                    g_snprintf(duration_str, sizeof(duration_str), "%" G_GUINT64_FORMAT ":%02u:%02u",
                               seconds / 3600, (unsigned)((seconds / 60) % 60), (unsigned)(seconds % 60));
                else
                    g_snprintf(duration_str, sizeof(duration_str), "%02u:%02u",
                               (unsigned)(seconds / 60), (unsigned)(seconds % 60));
            }
            sr_console_status(m_print_stderr ? stderr : stdout, status_str,
                              rmi->callback_filename, duration_str);
	    break;
	case RM_STATUS_RECONNECTING:
	    strcpy(status_str, "re-connecting..");
            sr_console_status(m_print_stderr ? stderr : stdout, status_str, "", NULL);
	    break;
	}
			
    }
    if (!printed_fullinfo) {
	print_to_console ("host: %s\n"
			  "server name: %s\n"
			  "stream: %s\n",
			  rmi->stream_host,
			  rmi->server_name,
			  rmi->http_info.have_icy_name ? rmi->streamname : "Unknown station");
	if (rmi->http_bitrate > 0) {
	    print_to_console ("declared bitrate: %d\n",
			      rmi->http_bitrate);
	}
	if (rmi->meta_interval != NO_META_INTERVAL) {
	    print_to_console ("meta interval: %d\n",
			      rmi->meta_interval);
	}
	if (rmi->relaylib_info.m_running) {
            if (prefs->relay_ip[0]) {
                print_to_console(strchr(prefs->relay_ip, ':') ? "relay: [%s]:%u\n" : "relay: %s:%u\n",
                                 prefs->relay_ip, prefs->relay_port);
            } else {
                print_to_console("relay port: %u\n", prefs->relay_port);
            }
	    print_to_console ("[%14s]\r", "getting track name... ");
	}

	printed_fullinfo = TRUE;
    }
}

/*
 * This will get called whenever anything interesting happens with the 
 * stream. Interesting are progress updates, error's, when the rip
 * thread stops (RM_DONE) starts (RM_START) and when we get a new track.
 *
 * for the most part this function just checks what kind of message we got
 * and prints out stuff to the screen.
 */
void
rip_callback (RIP_MANAGER_INFO* rmi, int message, void *data)
{
    ERROR_INFO *err;
    switch(message)
    {
    case RM_UPDATE:
	print_status (rmi);
	break;
    case RM_ERROR:
	err = (ERROR_INFO*)data;
        if (err->error_code == SR_ERROR_OUTPUT_DIRECTORY_LOCKED) m_exit_code = EXIT_FAILURE;
	sr_console_printf(stderr, "\nerror %d [%s]\n", err->error_code, err->error_str);
	m_alldone = TRUE;
	break;
    case RM_DONE:
	print_to_console ("bye..\n");
	m_alldone = TRUE;
	break;
    case RM_NEW_TRACK:
	print_to_console ("\n");
	break;
    case RM_STARTED:
	m_started = TRUE;
	break;
    }
}

/* Usage should be printed to stdout when it is not an error 
   http://www.gnu.org/prep/standards/standards.html */
static void
print_usage_option(FILE *stream, const char *option, const char *description)
{
    sr_console_printf(stream, "  %-38s  %s\n", option, description);
}

static void
print_usage (FILE* stream)
{
    fputs("Usage: streamripper URL [OPTIONS]\n", stream);
    fputs("Short-option aliases accept --name VALUE or --name=VALUE.\n", stream);
    fputs("\nGeneral:\n", stream);
    print_usage_option(stream, "-h, --help", "Print this help and exit");
    print_usage_option(stream, "-v, --version", "Print the version and exit");
    print_usage_option(stream, "--config FILE", "Read an explicit INI file (no auto-save)");
    fputs("\nConnection:\n", stream);
    print_usage_option(stream, "-4, --ipv4", "Use IPv4 for outgoing connections");
    print_usage_option(stream, "-6, --ipv6", "Use IPv6 for outgoing connections");
    print_usage_option(stream, "", "Default: automatic; -4/-6 conflict");
    print_usage_option(stream, "-c, --no-reconnect", "Disable automatic reconnection");
    print_usage_option(stream, "-m, --timeout SECONDS", "Set the stall timeout in SECONDS");
    print_usage_option(stream, "", "Default: 15 seconds");
    print_usage_option(stream, "-u, --user-agent USERAGENT", "Set the HTTP User-Agent");
    print_usage_option(stream, "-p, --proxy URL", "Use an HTTP or HTTPS proxy");
    print_usage_option(stream, "--http10", "Request HTTP/1.0 instead of HTTP/1.1");
    print_usage_option(stream, "--hls-variant=VALUE", "HLS: best (default), worst, or bits/s");
#if !defined (WIN32)
    print_usage_option(stream, "-I, --interface INTERFACE", "Use the specified network interface");
#endif
    fputs("\nTLS (certificate verification is enabled by default):\n", stream);
    print_usage_option(stream, "--tls-ca-file=PATH", "Add trusted CAs from a PEM file");
    print_usage_option(stream, "--insecure", "Disable certificate/hostname checks");
    print_usage_option(stream, "--tls-info", "Print TLS and verification details");
    fputs("\nRecording and output:\n", stream);
    print_usage_option(stream, "-d, --output-directory DIR", "Set the destination directory");
    print_usage_option(stream, "-D, --output-pattern PATTERN", "Set the output path/name pattern");
    print_usage_option(stream, "-s, --no-stream-directory", "Disable the per-stream subdirectory");
    print_usage_option(stream, "-q, --sequence [START]", "Number default names (auto or START)");
    print_usage_option(stream, "-a, --continuous-file [FILE]", "Also record a continuous file and CUE");
    print_usage_option(stream, "", "FILE defaults to a timestamped name");
    print_usage_option(stream, "-A, --no-individual-tracks", "Disable individual track files");
    print_usage_option(stream, "--wav", "Decode MP3 to 16-bit PCM WAV");
    print_usage_option(stream, "-l, --record-duration SECONDS", "Stop after SECONDS (default: forever)");
    print_usage_option(stream, "-M, --max-megabytes MEGABYTES", "Stop after receiving MEGABYTES");
    fputs("\nIncomplete files and duplicates:\n", stream);
    print_usage_option(stream, "-k, --skip-initial-tracks COUNT", "Leave first COUNT tracks incomplete");
    print_usage_option(stream, "", "Default: 1 initial track");
    print_usage_option(stream, "-t, --keep-incomplete", "Version existing incomplete files");
    print_usage_option(stream, "-o, --overwrite POLICY", "Set the complete-file overwrite policy");
    print_usage_option(stream, "", "POLICY: always, never, larger, version");
    print_usage_option(stream, "", "Default: version");
    print_usage_option(stream, "-T, --truncate-duplicates", "Truncate incomplete duplicates");
    print_usage_option(stream, "--incomplete-min-duration=TIME", "Discard shorter incomplete files (off)");
    print_usage_option(stream, "", "TIME: seconds, Ns/Nm/Nh; 0 disables");
    fputs("\nMetadata and tags (default: ID3v2.3 on, ID3v1 off):\n", stream);
    print_usage_option(stream, "-w, --metadata-rules FILE", "Read metadata parsing/filter rules");
    print_usage_option(stream, "--reload-rules", "Reload changed rules every 5 seconds");
    print_usage_option(stream, "-E, --external-metadata COMMAND", "Fetch metadata via an external command");
    print_usage_option(stream, "-i, --no-id3", "Disable both ID3v1 and ID3v2 tags");
    print_usage_option(stream, "--with-id3v1", "Enable ID3v1 tags");
    print_usage_option(stream, "--without-id3v2", "Disable ID3v2 tags");
    fputs("\nMP3 split points (times are in milliseconds):\n", stream);
    print_usage_option(stream, "--xs-none", "Disable silence searching");
    print_usage_option(stream, "--xs2", "Use the alternative silence detector");
    print_usage_option(stream, "--xs-offset=MSEC", "Shift the split relative to metadata");
    print_usage_option(stream, "--xs-padding=PREV:NEXT", "Pad previous/next recordings");
    print_usage_option(stream, "--xs-search-window=BEFORE:AFTER", "Set the silence search window");
    print_usage_option(stream, "--xs-silence-length=MSEC", "Set the expected silence length");
    fputs("\nRelay:\n", stream);
    print_usage_option(stream, "-r, --relay [[IP:]PORT]", "Enable the relay (default port: 8000)");
    print_usage_option(stream, "", "HLS/FLAC: ignored with a warning");
    print_usage_option(stream, "", "IPv6 bind: [ADDRESS]:PORT");
    print_usage_option(stream, "-R, --max-relay-clients COUNT", "Max relay clients (default: 1; 0=all)");
    print_usage_option(stream, "-L, --relay-playlist FILE", "Write a relay playlist");
    print_usage_option(stream, "-z, --no-port-scan", "Disable scanning for a free relay port");
    fputs("\nText encoding:\n", stream);
    print_usage_option(stream, "--codeset-metadata=NAME", "Set the incoming metadata encoding");
    print_usage_option(stream, "", "Default: AUTO (UTF-8, else locale)");
    print_usage_option(stream, "--codeset-id3=NAME", "Set the ID3 text encoding");
    print_usage_option(stream, "--codeset-filesys=NAME", "Set the file-system encoding");
    print_usage_option(stream, "--codeset-relay=NAME", "Set the relay metadata encoding");
    fputs("\nLogging:\n", stream);
    print_usage_option(stream, "--metadata-log", "Log ICY title changes in incomplete/metadata.log");
    print_usage_option(stream, "--quiet", "Hide recording status");
    print_usage_option(stream, "--stderr", "Write recording status to stderr");
    print_usage_option(stream, "--debug", "Write a debugging trace");
}

/* 
 * Bla, boring agument parsing crap, only reason i didn't use getopt
 * (which i did for an earlyer version) is because i couldn't find a good
 * port of it under Win32.. there probably is one, maybe i didn't look 
 * hard enough. 
 */
/* Map supported long aliases to the same handlers as the original short options. */
static const char *
normalize_option(char *input, char **argument)
{
    static const struct {
        const char *name;
        char short_option[3];
    } aliases[] = {
        {"help", "-h"},
        {"version", "-v"},
        {"no-reconnect", "-c"},
        {"ipv4", "-4"},
        {"ipv6", "-6"},
        {"timeout", "-m"},
        {"user-agent", "-u"},
        {"proxy", "-p"},
        {"interface", "-I"},
        {"output-directory", "-d"},
        {"output-pattern", "-D"},
        {"no-stream-directory", "-s"},
        {"sequence", "-q"},
        {"continuous-file", "-a"},
        {"no-individual-tracks", "-A"},
        {"record-duration", "-l"},
        {"max-megabytes", "-M"},
        {"skip-initial-tracks", "-k"},
        {"keep-incomplete", "-t"},
        {"overwrite", "-o"},
        {"truncate-duplicates", "-T"},
        {"metadata-rules", "-w"},
        {"external-metadata", "-E"},
        {"no-id3", "-i"},
        {"relay", "-r"},
        {"max-relay-clients", "-R"},
        {"relay-playlist", "-L"},
        {"no-port-scan", "-z"},
    };
    size_t i;
    if (input[0] != '-' || input[1] != '-') return input;
    for (i = 0; i < G_N_ELEMENTS(aliases); i++) {
        size_t length = strlen(aliases[i].name);
        if (strncmp(input + 2, aliases[i].name, length) ||
            (input[length + 2] && input[length + 2] != '=')) continue;
        if (input[length + 2] == '=') {
            if (!strchr("adDEIklLmMopqRruw", aliases[i].short_option[1])) {
                sr_console_printf(stderr, "option --%s does not take an argument\n", aliases[i].name);
                exit(EXIT_FAILURE);
            }
            *argument = input + length + 3;
        }
        return aliases[i].short_option;
    }
    return input;
}

static void
parse_relay_address(STREAM_PREFS *prefs, const char *value)
{
    const char *port = value;
    const char *host = NULL;
    size_t host_length = 0;
    char *end;
    guint64 number;
    if (*value == '[') {
        const char *closing = strchr(value + 1, ']');
        if (!closing || closing == value + 1 || (closing[1] && closing[1] != ':'))
            goto invalid;
        host = value + 1;
        host_length = closing - host;
        port = closing[1] ? closing + 2 : NULL;
    } else {
        const char *colon = strchr(value, ':');
        if (colon) {
            if (colon == value || strchr(colon + 1, ':')) goto invalid;
            host = value;
            host_length = colon - host;
            port = colon + 1;
        }
    }
    if (host_length >= sizeof(prefs->relay_ip)) goto invalid;
    if (port) {
        errno = 0;
        number = g_ascii_strtoull(port, &end, 10);
        if (!g_ascii_isdigit(*port) || *end || errno == ERANGE || number < 1 || number > 65535)
            goto invalid;
        prefs->relay_port = (u_short)number;
    }
    if (host) {
        memcpy(prefs->relay_ip, host, host_length);
        prefs->relay_ip[host_length] = 0;
    }
    return;
invalid:
    sr_console_printf(stderr, "Invalid relay address; use PORT, HOST:PORT, or [IPv6]:PORT (port 1-65535).\n");
    exit(EXIT_FAILURE);
}
static u_long
parse_timeout(const char *value)
{
    char *end;
    guint64 timeout;
    errno = 0;
    timeout = g_ascii_strtoull(value, &end, 10);
    if (!g_ascii_isdigit(*value) || *end || errno == ERANGE || timeout > G_MAXINT) {
        sr_console_printf(stderr, "Invalid timeout; use integer seconds from 0 to %d.\n", G_MAXINT);
        exit(EXIT_FAILURE);
    }
    return (u_long)timeout;
}

static void
parse_arguments (STREAM_PREFS* prefs, int argc, char **argv)
{
    int i;
    char *c;
    const char *option;
    char *argument;

    if (argc < 2) {
	print_usage (stdout);
	exit(2);
    }

    // Get URL
g_strlcpy (prefs->url, argv[1], MAX_URL_LEN);

    /* Configuration is opt-in; command-line options always take precedence. */
    const char *config_file = NULL;
    for (i = 2; i < argc; i++) {
        if (argv[i][0] != '-') continue;
        argument = NULL;
        option = normalize_option(argv[i], &argument);
        if (!strcmp(option, "--config") || g_str_has_prefix(option, "--config=")) {
            if (config_file) {
                sr_console_printf(stderr, "Option --config may only be specified once.\n");
                exit(EXIT_FAILURE);
            }
            if (option[8] == '=') config_file = option + 9;
            else if (i + 1 < argc && argv[i + 1][0] != '-') config_file = argv[++i];
            if (!config_file || !*config_file) {
                sr_console_printf(stderr, "Option --config requires a file path.\n");
                exit(EXIT_FAILURE);
            }
        } else if (!argument && strchr("dDEfIklLmMopRuwaqr", option[1]) &&
                   i + 1 < argc && argv[i + 1][0] != '-') {
            i++; /* Skip values so an option value cannot become a config path. */
        }
    }
    if (!prefs_load_file(config_file)) exit(EXIT_FAILURE);
    prefs_get_stream_prefs (prefs, prefs->url);

    // Parse arguments
    for (i = 1; i < argc; i++) {
	if (argv[i][0] != '-')
	    continue;
        if (!strcmp(argv[i], "--config")) { i++; continue; }
        if (g_str_has_prefix(argv[i], "--config=")) continue;

        /* Keep argv ownership intact; attached values point into its strings. */
        argument = NULL;
        option = normalize_option(argv[i], &argument);
        c = strchr("dDEfIklLmMopRuw", option[1]);
        if (c && !argument) {
            if (i == argc - 1 || argv[i + 1][0] == '-') {
                sr_console_printf(stderr, "option %s requires an argument\n", argv[i]);
                exit(EXIT_FAILURE);
            }
            argument = argv[++i];
        } else if (!argument && strchr("aqr", option[1]) &&
                   i + 1 < argc && argv[i + 1][0] != '-') {
            argument = argv[++i];
        }
	switch (option[1])
	{
        case '4':
        case '6':
            if (prefs->ip_version && prefs->ip_version != option[1] - '0') {
                sr_console_printf(stderr, "Options -4/--ipv4 and -6/--ipv6 are mutually exclusive.\n");
                exit(EXIT_FAILURE);
            }
            prefs->ip_version = option[1] - '0';
            break;
	case 'a':
	    /* Create single file output + cue sheet */
	    OPT_FLAG_SET (prefs->flags, OPT_SINGLE_FILE_OUTPUT, 1);
	    prefs->showfile_pattern[0] = 0;
	    if (!argument) break;
g_strlcpy (prefs->showfile_pattern, argument, SR_MAX_PATH);
	    break;
	case 'A':
	    OPT_FLAG_SET (prefs->flags, OPT_INDIVIDUAL_TRACKS, 0);
	    break;
	case 'c':
	    OPT_FLAG_SET (prefs->flags, OPT_AUTO_RECONNECT, 0);
	    break;
	case 'd':
g_strlcpy(prefs->output_directory, argument, SR_MAX_PATH);
	    break;
	case 'D':
g_strlcpy(prefs->output_pattern, argument, SR_MAX_PATH);
	    break;
	case 'E':
	    OPT_FLAG_SET (prefs->flags, OPT_EXTERNAL_CMD, 1);
g_strlcpy(prefs->ext_cmd, argument, SR_MAX_PATH);
	    break;
	case 'f':
	    sr_console_printf(stderr, "Error: -f dropstring option is obsolete. "
		     "Please use -w parse_rules instead.\n");
	    exit (1);
	case 'h':
	case '?':
	    print_usage (stdout);
            exit(0);
	    break;
	case 'i':
	    OPT_FLAG_SET(prefs->flags, OPT_ADD_ID3V1, 0);
	    OPT_FLAG_SET(prefs->flags, OPT_ADD_ID3V2, 0);
	    break;
	case 'I':
g_strlcpy(prefs->if_name, argument, SR_MAX_PATH);
	    break;
	case 'k':
	    prefs->dropcount = atoi(argument);
	    break;
	case 'l':
	    time(&m_stop_time);
	    m_stop_time += atoi(argument);
	    break;
	case 'L':
g_strlcpy(prefs->pls_file, argument, SR_MAX_PATH);
	    break;
	case 'm':
	    prefs->timeout = parse_timeout(argument);
	    break;
 	case 'M':
            prefs->maxMB_rip_size = atoi(argument);
	    OPT_FLAG_SET(prefs->flags, OPT_CHECK_MAX_BYTES, 1);
 	    break;
	case 'o':
	    prefs->overwrite = string_to_overwrite_opt (argument);
	    if (prefs->overwrite == OVERWRITE_UNKNOWN) {
		sr_console_printf(stderr, "Error: -o option requires an argument\n");
		exit (1);
	    }
	    break;
	case 'p':
g_strlcpy(prefs->proxyurl, argument, MAX_URL_LEN);
	    break;
	case 'P':
	    sr_console_printf(stderr, "Error: -P prefix option is obsolete. "
		     "Please use -D pattern instead.\n");
	    exit (1);
	case 'q':
	    OPT_FLAG_SET(prefs->flags, OPT_COUNT_FILES, 1);
	    prefs->count_start = -1;     /* -1 means auto-detect */
	    if (!argument) break;
	    prefs->count_start = atoi(argument);
	    break;
	case 'r':
	    OPT_FLAG_SET(prefs->flags, OPT_MAKE_RELAY, 1);
	    if (!argument) break;
            parse_relay_address(prefs, argument);
	    break;
	case 'R':
	    prefs->max_connections = atoi(argument);
	    break;
	case 's':
	    OPT_FLAG_SET(prefs->flags, OPT_SEPARATE_DIRS, 0);
	    break;
	case 't':
	    OPT_FLAG_SET(prefs->flags, OPT_KEEP_INCOMPLETE, 1);
	    break;
	case 'T':
	    OPT_FLAG_SET(prefs->flags, OPT_TRUNCATE_DUPS, 1);
	    break;
	case 'u':
g_strlcpy(prefs->useragent, argument, MAX_USERAGENT_STR);
	    break;
	case 'v':
	    printf("Streamripper %s\n", SRVERSION);
	    exit(0);
	case 'w':
g_strlcpy(prefs->rules_file, argument, SR_MAX_PATH);
	    break;
	case 'z':
	    OPT_FLAG_SET(prefs->flags, OPT_SEARCH_PORTS, 0);
	    prefs->max_port = prefs->relay_port+1000;
	    break;
	case '-':
	    parse_extended_options (prefs, &option[2]);
	    break;
	}
    }

    /* Need to verify that splitpoint rules were sane */
    if (prefs->reload_rules && (!prefs->rules_file[0] || GET_EXTERNAL_CMD(prefs->flags))) {
        sr_console_printf(stderr, "--reload-rules requires --metadata-rules FILE and ICY metadata.\n");
        exit(EXIT_FAILURE);
    }
    verify_splitpoint_rules (prefs);
    if (prefs->wav) {
        OPT_FLAG_SET(prefs->flags, OPT_ADD_ID3V1, 0);
        OPT_FLAG_SET(prefs->flags, OPT_ADD_ID3V2, 0);
    }

    /* Verify that first parameter is URL */
    if (argv[1][0] == '-') {
	sr_console_printf(stderr, "*** The first parameter MUST be the URL\n\n");
	exit(2);
    }
}

static void
parse_extended_options (STREAM_PREFS* prefs, const char* rule)
{
    int x,y;

    /* Version */
    if (!strcmp(rule,"version")) {
	printf("Streamripper %s\n", SRVERSION);
	exit(0);
    }

    /* Logging options */
    if (g_str_has_prefix(rule, "hls-variant=")) {
        const char *value = rule + strlen("hls-variant=");
        char *end;
        gint64 bandwidth = g_ascii_strtoll(value, &end, 10);
        if (!strcmp(value, "best")) prefs->hls_variant = 0;
        else if (!strcmp(value, "worst")) prefs->hls_variant = -1;
        else if (*value && !*end && bandwidth > 0 && bandwidth <= G_MAXINT)
            prefs->hls_variant = (int)bandwidth;
        else {
            sr_console_printf(stderr, "Invalid --hls-variant; use best, worst, or a positive bandwidth in bits/s.\n");
            exit(EXIT_FAILURE);
        }
        return;
    }
    if (!strcmp(rule, "reload-rules")) {
        prefs->reload_rules = TRUE;
        return;
    }
    if (!strcmp(rule, "metadata-log")) {
        prefs->metadata_log = TRUE;
        return;
    }
    if (!strcmp(rule,"debug")) {
	debug_enable();
	return;
    }
    if (!strcmp(rule,"stderr")) {
	m_print_stderr = TRUE;
	return;
    }
    if (!strcmp(rule,"quiet")) {
	m_dont_print = TRUE;
	return;
    }

    /* Splitpoint options */
    if (g_str_has_prefix(rule, "timeout=")) {
        prefs->timeout = parse_timeout(rule + strlen("timeout="));
        return;
    }
    if (g_str_has_prefix(rule, "incomplete-min-duration=")) {
        const char *value = rule + strlen("incomplete-min-duration=");
        char *end;
        guint64 amount, multiplier = 1000;
        errno = 0;
        amount = g_ascii_strtoull(value, &end, 10);
        if (*end == 'm' && !end[1]) multiplier *= 60;
        else if (*end == 'h' && !end[1]) multiplier *= 3600;
        else if (*end && !(*end == 's' && !end[1])) multiplier = 0;
        if (!g_ascii_isdigit(*value) || errno == ERANGE || !multiplier ||
            amount > G_MAXUINT64 / multiplier) {
            sr_console_printf(stderr, "Invalid incomplete duration; use non-negative integer seconds, e.g. 30s, 10m, or 1h.\n");
            exit(EXIT_FAILURE);
        }
        prefs->incomplete_min_duration_ms = amount * multiplier;
        return;
    }
    if (!strcmp(rule, "insecure")) {
        prefs->tls_insecure = TRUE;
        return;
    }
    if (!strcmp(rule, "tls-info")) {
        prefs->tls_info = TRUE;
        return;
    }
    if (!strcmp(rule, "http10")) {
        prefs->http10 = TRUE;
        return;
    }
    if (!strcmp(rule, "wav")) {
        prefs->wav = TRUE;
        return;
    }
    if (g_str_has_prefix(rule, "tls-ca-file=")) {
        const char *path = rule + strlen("tls-ca-file=");
        if (!*path || g_strlcpy(prefs->tls_ca_file, path, sizeof(prefs->tls_ca_file)) >= sizeof(prefs->tls_ca_file)) {
            sr_console_printf(stderr, "Invalid CA file path.\n");
            exit(EXIT_FAILURE);
        }
        return;
    }

    if ((!strcmp(rule,"xs-none"))
	|| (!strcmp(rule,"xs_none"))) {
	prefs->sp_opt.xs = 0;
	debug_printf ("Disable silence detection");
	return;
    }
    if (!strcmp(rule,"xs2")) {
	prefs->sp_opt.xs = 2;
	debug_printf ("Setting xs2\n");
	return;
    }
    if ((1==sscanf(rule,"xs-min-volume=%d",&x)) 
	|| (1==sscanf(rule,"xs_min_volume=%d",&x))) {
	prefs->sp_opt.xs_min_volume = x;
	debug_printf ("Setting minimum volume to %d\n",x);
	return;
    }
    if ((1==sscanf(rule,"xs-silence-length=%d",&x))
	|| (1==sscanf(rule,"xs_silence_length=%d",&x))) {
	prefs->sp_opt.xs_silence_length = x;
	debug_printf ("Setting silence length to %d\n",x);
	return;
    }
    if ((2==sscanf(rule,"xs-search-window=%d:%d",&x,&y))
	|| (2==sscanf(rule,"xs_search_window=%d:%d",&x,&y))) {
	prefs->sp_opt.xs_search_window_1 = x;
	prefs->sp_opt.xs_search_window_2 = y;
	debug_printf ("Setting search window to (%d:%d)\n",x,y);
	return;
    }
    if ((1==sscanf(rule,"xs-offset=%d",&x))
	|| (1==sscanf(rule,"xs_offset=%d",&x))) {
	prefs->sp_opt.xs_offset = x;
	debug_printf ("Setting silence offset to %d\n",x);
	return;
    }
    if ((2==sscanf(rule,"xs-padding=%d:%d",&x,&y))
	|| (2==sscanf(rule,"xs_padding=%d:%d",&x,&y))) {
	prefs->sp_opt.xs_padding_1 = x;
	prefs->sp_opt.xs_padding_2 = y;
	debug_printf ("Setting file output padding to (%d:%d)\n",x,y);
	return;
    }

    /* id3 options */
    if (!strcmp(rule,"with-id3v2")) {
	OPT_FLAG_SET(prefs->flags,OPT_ADD_ID3V2,1);
	return;
    }
    if (!strcmp(rule,"without-id3v2")) {
	OPT_FLAG_SET(prefs->flags,OPT_ADD_ID3V2,0);
	return;
    }
    if (!strcmp(rule,"with-id3v1")) {
	OPT_FLAG_SET(prefs->flags,OPT_ADD_ID3V1,1);
	return;
    }
    if (!strcmp(rule,"without-id3v1")) {
	OPT_FLAG_SET(prefs->flags,OPT_ADD_ID3V1,0);
	return;
    }

    /* codeset options */
    x = strlen("codeset-filesys=");
    if (!strncmp(rule,"codeset-filesys=",x)) {
g_strlcpy (prefs->cs_opt.codeset_filesys, &rule[x], MAX_CODESET_STRING);
	debug_printf ("Setting filesys codeset to %s\n",
		      prefs->cs_opt.codeset_filesys);
	return;
    }
    x = strlen("codeset-id3=");
    if (!strncmp(rule,"codeset-id3=",x)) {
g_strlcpy (prefs->cs_opt.codeset_id3, &rule[x], MAX_CODESET_STRING);
	debug_printf ("Setting id3 codeset to %s\n",
		      prefs->cs_opt.codeset_id3);
	return;
    }
    x = strlen("codeset-metadata=");
    if (!strncmp(rule,"codeset-metadata=",x)) {
g_strlcpy (prefs->cs_opt.codeset_metadata, &rule[x], MAX_CODESET_STRING);
	debug_printf ("Setting metadata codeset to %s\n",
		      prefs->cs_opt.codeset_metadata);
	return;
    }
    x = strlen("codeset-relay=");
    if (!strncmp(rule,"codeset-relay=",x)) {
g_strlcpy (prefs->cs_opt.codeset_relay, &rule[x], MAX_CODESET_STRING);
	debug_printf ("Setting relay codeset to %s\n",
		      prefs->cs_opt.codeset_relay);
	return;
    }

    /* All rules failed */
    sr_console_printf(stderr, "Can't parse command option: --%s\n", rule);
    exit (-1);
}

static void
verify_splitpoint_rules (STREAM_PREFS *prefs)
{
#if defined (commentout)
    /* This is still not complete, but the warning causes people to 
       wonder what is going on. */
    sr_console_printf(stderr, "Warning: splitpoint sanity check not yet complete.\n");
#endif
    
    /* xs_silence_length must be non-negative and divisible by two */
    if (prefs->sp_opt.xs_silence_length < 0) {
	prefs->sp_opt.xs_silence_length = 0;
    }
    if (prefs->sp_opt.xs_silence_length % 2) {
        prefs->sp_opt.xs_silence_length ++;
    }

    /* search_window values must be non-negative */
    if (prefs->sp_opt.xs_search_window_1 < 0) {
	prefs->sp_opt.xs_search_window_1 = 0;
    }
    if (prefs->sp_opt.xs_search_window_2 < 0) {
	prefs->sp_opt.xs_search_window_2 = 0;
    }

    /* if silence_length is 0, then search window should be zero */
    if (prefs->sp_opt.xs_silence_length == 0) {
	prefs->sp_opt.xs_search_window_1 = 0;
	prefs->sp_opt.xs_search_window_2 = 0;
    }

    /* search_window values must be longer than silence_length */
    if (prefs->sp_opt.xs_search_window_1 + prefs->sp_opt.xs_search_window_2
	    < prefs->sp_opt.xs_silence_length) {
	/* if this happens, disable search */
	prefs->sp_opt.xs_search_window_1 = 0;
	prefs->sp_opt.xs_search_window_2 = 0;
	prefs->sp_opt.xs_silence_length = 0;
    }

    /* search window lengths must be at least 1/2 of silence_length */
    if (prefs->sp_opt.xs_search_window_1 < prefs->sp_opt.xs_silence_length) {
	prefs->sp_opt.xs_search_window_1 = prefs->sp_opt.xs_silence_length;
    }
    if (prefs->sp_opt.xs_search_window_2 < prefs->sp_opt.xs_silence_length) {
	prefs->sp_opt.xs_search_window_2 = prefs->sp_opt.xs_silence_length;
    }
}
