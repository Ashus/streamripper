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

/*****************************************************************************
 * Private functions
 *****************************************************************************/
static void print_usage (FILE* stream);
static void print_status (RIP_MANAGER_INFO *rmi);
static void catch_sig (int code);
static void parse_arguments (STREAM_PREFS *prefs, int argc, char **argv);
static void rip_callback (RIP_MANAGER_INFO* rmi, int message, void *data);
static void parse_extended_options (STREAM_PREFS *prefs, char *rule);
static void verify_splitpoint_rules (STREAM_PREFS *prefs);
static void print_to_console (char* fmt, ...);

/*****************************************************************************
 * Private variables
 *****************************************************************************/
static char m_buffer_chars[] = {'\\', '|', '/', '-', '*'}; /* for formating */
//static RIP_MANAGER_INFO 	m_curinfo; /* from the rip_manager callback */
static BOOL			m_started = FALSE;
static BOOL			m_alldone = FALSE;
static BOOL			m_got_sig = FALSE;
static BOOL			m_dont_print = FALSE;
static BOOL			m_print_stderr = FALSE;
time_t				m_stop_time = 0;

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
    SetConsoleOutputCP(CP_UTF8);
#endif
    sr_set_locale ();

    signal (SIGINT, catch_sig);
    signal (SIGTERM, catch_sig);

    parse_arguments (&prefs, argc, argv);

    print_to_console ("Connecting...\n");
    
    rip_manager_init ();

    /* Launch the ripping thread */
    if ((ret = rip_manager_start (&rmi, &prefs, rip_callback)) != SR_SUCCESS) {
	fprintf(stderr, "Couldn't connect to %s\n", prefs.url);
	exit(1);
    }

    /* 
     * The m_got_sig thing is because you can't call into a thread 
     * (i.e. rip_manager_stop) from a signal handler.. or at least not
     * in FreeBSD 3.4, i don't know about linux or NT.
     */
    while (!m_got_sig && !m_alldone) {
	sleep(1);
	time(&temp_time);
	if (m_stop_time && (temp_time >= m_stop_time)) {
	    print_to_console ("\nTime to stop is here, bailing\n");
	    break; 
	}	
    }

    print_to_console ("shutting down\n");

    rip_manager_stop (rmi);
    rip_manager_cleanup ();
    prefs_cleanup();
    free(rmi);
#ifdef SR_MODERN_WINDOWS
    g_strfreev(argv);
#endif

    return 0;
}

void
catch_sig(int code)
{
    print_to_console ("\n");
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

	if (m_print_stderr) {
	    vfprintf (stderr, fmt, argptr);
	    fflush (stderr);
	} else {
	    vfprintf (stdout, fmt, argptr);
	    fflush (stdout);
	}

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
    char filesize_str[64];
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

	    print_to_console ("[%14s] %.50s\r",
			      status_str,
			      rmi->callback_filename);
	    break;

	case RM_STATUS_RIPPING:
	    if (rmi->track_count < prefs->dropcount) {
		strcpy(status_str, "skipping...   ");
	    } else {
		strcpy(status_str, "ripping...    ");
	    }
	    format_byte_size(filesize_str, rmi->callback_filesize);
	    print_to_console ("[%14s] %.50s [%7s]\r",
			      status_str,
			      rmi->callback_filename,
			      filesize_str);
	    break;
	case RM_STATUS_RECONNECTING:
	    strcpy(status_str, "re-connecting..");
	    print_to_console ("[%14s]\r", status_str);
	    break;
	}
			
    }
    if (!printed_fullinfo) {
	print_to_console ("stream: %s\n"
			  "server name: %s\n",
			  rmi->streamname,
			  rmi->server_name);
	if (rmi->http_bitrate > 0) {
	    print_to_console ("declared bitrate: %d\n",
			      rmi->http_bitrate);
	}
	if (rmi->meta_interval != NO_META_INTERVAL) {
	    print_to_console ("meta interval: %d\n",
			      rmi->meta_interval);
	}
	if (GET_MAKE_RELAY(prefs->flags)) {
	    print_to_console ("relay port: %d\n"
			      "[%14s]\r",
			      prefs->relay_port,
			      "getting track name... ");
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
	fprintf(stderr, "\nerror %d [%s]\n", err->error_code, err->error_str);
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
    fprintf(stream, "  %-38s  %s\n", option, description);
}

static void
print_usage (FILE* stream)
{
    fputs("Usage: streamripper URL [OPTIONS]\n", stream);
    fputs("\nGeneral:\n", stream);
    print_usage_option(stream, "-h, --help", "Print this help and exit");
    print_usage_option(stream, "-v, --version", "Print the version and exit");
    fputs("\nConnection:\n", stream);
    print_usage_option(stream, "-c", "Disable automatic reconnection");
    print_usage_option(stream, "-m, --timeout SECONDS", "Set the stall timeout in SECONDS");
    print_usage_option(stream, "", "Default: 15; also --timeout=SECONDS");
    print_usage_option(stream, "-u USERAGENT", "Set the HTTP User-Agent");
    print_usage_option(stream, "-p URL", "Use an HTTP proxy");
    print_usage_option(stream, "--http10", "Request HTTP/1.0 instead of HTTP/1.1");
#if !defined (WIN32)
    print_usage_option(stream, "-I INTERFACE", "Use the specified network interface");
#endif
    fputs("\nTLS (certificate verification is enabled by default):\n", stream);
    print_usage_option(stream, "--tls-ca-file=PATH", "Add trusted CAs from a PEM file");
    print_usage_option(stream, "--insecure", "Disable certificate/hostname checks");
    print_usage_option(stream, "--tls-info", "Print TLS and verification details");
    fputs("\nRecording and output:\n", stream);
    print_usage_option(stream, "-d DIR", "Set the destination directory");
    print_usage_option(stream, "-D PATTERN", "Set the output path/name pattern");
    print_usage_option(stream, "-s", "Disable the per-stream subdirectory");
    print_usage_option(stream, "-q [START]", "Number default names (auto or START)");
    print_usage_option(stream, "-a [FILE]", "Also record a continuous file and CUE");
    print_usage_option(stream, "", "FILE defaults to a timestamped name");
    print_usage_option(stream, "-A", "Disable individual track files");
    print_usage_option(stream, "--wav", "Decode MP3 to 16-bit PCM WAV");
    print_usage_option(stream, "-l SECONDS", "Stop after SECONDS (default: forever)");
    print_usage_option(stream, "-M MEGABYTES", "Stop after receiving MEGABYTES");
    fputs("\nIncomplete files and duplicates:\n", stream);
    print_usage_option(stream, "-k COUNT", "Leave first COUNT tracks incomplete");
    print_usage_option(stream, "", "Default: 1 initial track");
    print_usage_option(stream, "-t, --keep-incomplete", "Version existing incomplete files");
    print_usage_option(stream, "-o POLICY", "Set the complete-file overwrite policy");
    print_usage_option(stream, "", "POLICY: always, never, larger, version");
    print_usage_option(stream, "", "Default: version");
    print_usage_option(stream, "-T", "Truncate incomplete duplicates");
    print_usage_option(stream, "--incomplete-min-duration=TIME", "Discard shorter incomplete files (off)");
    print_usage_option(stream, "", "TIME: seconds, Ns/Nm/Nh; 0 disables");
    fputs("\nMetadata and tags (default: ID3v2.3 on, ID3v1 off):\n", stream);
    print_usage_option(stream, "-w FILE", "Read metadata parsing/filter rules");
    print_usage_option(stream, "-E COMMAND", "Fetch metadata via an external command");
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
    print_usage_option(stream, "-r [[IP:]PORT]", "Enable the relay (default port: 8000)");
    print_usage_option(stream, "-R COUNT", "Max relay clients (default: 1; 0=all)");
    print_usage_option(stream, "-L FILE", "Write a relay playlist");
    print_usage_option(stream, "-z", "Disable scanning for a free relay port");
    fputs("\nText encoding:\n", stream);
    print_usage_option(stream, "--codeset-metadata=NAME", "Set the incoming metadata encoding");
    print_usage_option(stream, "--codeset-id3=NAME", "Set the ID3 text encoding");
    print_usage_option(stream, "--codeset-filesys=NAME", "Set the file-system encoding");
    print_usage_option(stream, "--codeset-relay=NAME", "Set the relay metadata encoding");
    fputs("\nLogging:\n", stream);
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
static u_long
parse_timeout(const char *value)
{
    char *end;
    guint64 timeout;
    errno = 0;
    timeout = g_ascii_strtoull(value, &end, 10);
    if (!g_ascii_isdigit(*value) || *end || errno == ERANGE || timeout > G_MAXINT) {
        fprintf(stderr, "Invalid timeout; use integer seconds from 0 to %d.\n", G_MAXINT);
        exit(EXIT_FAILURE);
    }
    return (u_long)timeout;
}

static void
parse_arguments (STREAM_PREFS* prefs, int argc, char **argv)
{
    int i;
    char *c;
    char *option;

    if (argc < 2) {
	print_usage (stdout);
	exit(2);
    }

    // Get URL
g_strlcpy (prefs->url, argv[1], MAX_URL_LEN);

    // Load prefs
    prefs_load ();
    prefs_get_stream_prefs (prefs, prefs->url);
    prefs_save ();

    // Parse arguments
    for (i = 1; i < argc; i++) {
	if (argv[i][0] != '-')
	    continue;

        /* Preserve ownership of the original Windows UTF-8 argument vector. */
        option = argv[i];
        if (!strcmp(option, "--help")) option = "-h";
        else if (!strcmp(option, "--timeout")) option = "-m";
        else if (!strcmp(option, "--keep-incomplete")) option = "-t";
        else if (!strcmp(option, "--no-id3")) option = "-i";
	c = strchr ("dDEfIklLmMopRuw", option[1]);
        if (c != NULL) {
            if ((i == (argc-1)) || (argv[i+1][0] == '-')) {
		fprintf(stderr, "option %s requires an argument\n", argv[i]);
		exit(1);
	    }
	}
	switch (option[1])
	{
	case 'a':
	    /* Create single file output + cue sheet */
	    OPT_FLAG_SET (prefs->flags, OPT_SINGLE_FILE_OUTPUT, 1);
	    prefs->showfile_pattern[0] = 0;
	    if (i == (argc-1) || argv[i+1][0] == '-')
		break;
	    i++;
g_strlcpy (prefs->showfile_pattern, argv[i], SR_MAX_PATH);
	    break;
	case 'A':
	    OPT_FLAG_SET (prefs->flags, OPT_INDIVIDUAL_TRACKS, 0);
	    break;
	case 'c':
	    OPT_FLAG_SET (prefs->flags, OPT_AUTO_RECONNECT, 0);
	    break;
	case 'd':
	    i++;
g_strlcpy(prefs->output_directory, argv[i], SR_MAX_PATH);
	    break;
	case 'D':
	    i++;
g_strlcpy(prefs->output_pattern, argv[i], SR_MAX_PATH);
	    break;
	case 'E':
	    OPT_FLAG_SET (prefs->flags, OPT_EXTERNAL_CMD, 1);
	    i++;
g_strlcpy(prefs->ext_cmd, argv[i], SR_MAX_PATH);
	    break;
	case 'f':
	    i++;
	    fprintf (stderr, "Error: -f dropstring option is obsolete. "
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
	    i++;
g_strlcpy(prefs->if_name, argv[i], SR_MAX_PATH);
	    break;
	case 'k':
	    i++;
	    prefs->dropcount = atoi(argv[i]);
	    break;
	case 'l':
	    i++;
	    time(&m_stop_time);
	    m_stop_time += atoi(argv[i]);
	    break;
	case 'L':
	    i++;
g_strlcpy(prefs->pls_file, argv[i], SR_MAX_PATH);
	    break;
	case 'm':
	    i++;
	    prefs->timeout = parse_timeout(argv[i]);
	    break;
 	case 'M':
 	    i++;
 	    prefs->maxMB_rip_size = atoi(argv[i]);
	    OPT_FLAG_SET(prefs->flags, OPT_CHECK_MAX_BYTES, 1);
 	    break;
	case 'o':
	    i++;
	    prefs->overwrite = string_to_overwrite_opt (argv[i]);
	    if (prefs->overwrite == OVERWRITE_UNKNOWN) {
		fprintf (stderr, "Error: -o option requires an argument\n");
		exit (1);
	    }
	    break;
	case 'p':
	    i++;
g_strlcpy(prefs->proxyurl, argv[i], MAX_URL_LEN);
	    break;
	case 'P':
	    i++;
	    fprintf (stderr, "Error: -P prefix option is obsolete. "
		     "Please use -D pattern instead.\n");
	    exit (1);
	case 'q':
	    OPT_FLAG_SET(prefs->flags, OPT_COUNT_FILES, 1);
	    prefs->count_start = -1;     /* -1 means auto-detect */
	    if (i == (argc-1) || argv[i+1][0] == '-')
		break;
	    i++;
	    prefs->count_start = atoi(argv[i]);
	    break;
	case 'r':
	    OPT_FLAG_SET(prefs->flags, OPT_MAKE_RELAY, 1);
	    if (i == (argc-1) || argv[i+1][0] == '-')
		break;
	    i++;
	    c = strstr(argv[i], ":");
	    if (NULL == c) {
	    	prefs->relay_port = atoi(argv[i]);
	    } else {
	    	*c = '\0';
g_strlcpy(prefs->relay_ip, argv[i], SR_MAX_PATH);
		prefs->relay_port = atoi(++c);
 	    }
	    break;
	case 'R':
	    i++;
	    prefs->max_connections = atoi(argv[i]);
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
	    i++;
g_strlcpy(prefs->useragent, argv[i], MAX_USERAGENT_STR);
	    break;
	case 'v':
	    printf("Streamripper %s\n", SRVERSION);
	    exit(0);
	case 'w':
	    i++;
g_strlcpy(prefs->rules_file, argv[i], SR_MAX_PATH);
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
    verify_splitpoint_rules (prefs);
    if (prefs->wav) {
        OPT_FLAG_SET(prefs->flags, OPT_ADD_ID3V1, 0);
        OPT_FLAG_SET(prefs->flags, OPT_ADD_ID3V2, 0);
    }

    /* Verify that first parameter is URL */
    if (argv[1][0] == '-') {
	fprintf(stderr, "*** The first parameter MUST be the URL\n\n");
	exit(2);
    }
}

static void
parse_extended_options (STREAM_PREFS* prefs, char* rule)
{
    int x,y;

    /* Version */
    if (!strcmp(rule,"version")) {
	printf("Streamripper %s\n", SRVERSION);
	exit(0);
    }

    /* Logging options */
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
            fprintf(stderr, "Invalid incomplete duration; use non-negative integer seconds, e.g. 30s, 10m, or 1h.\n");
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
            fprintf(stderr, "Invalid CA file path.\n");
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
    fprintf (stderr, "Can't parse command option: --%s\n", rule);
    exit (-1);
}

static void
verify_splitpoint_rules (STREAM_PREFS *prefs)
{
#if defined (commentout)
    /* This is still not complete, but the warning causes people to 
       wonder what is going on. */
    fprintf (stderr, "Warning: splitpoint sanity check not yet complete.\n");
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
