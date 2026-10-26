##-----------------------------------------------------------------------------
##  Welcome to Streamripper
##  This CMakeLists.txt file is given to the public domain.
##  
##  The rest of streamripper is available under the GNU GENERAL PUBLIC 
##  LICENSE, v2 or later.  See COPYING for details.
##-----------------------------------------------------------------------------
PROJECT (streamripper_lib)

SET (STREAMRIPPER_LIB_SRC
	callback.c callback.h
	console_output.c console_output.h
	cbuf3.c cbuf3.h
	../third_party/charset.c ../third_party/charset.h
	debug.c	debug.h
	errors.c errors.h
	external.c external.h
	filelib.c filelib.h
	file_preallocation.c file_preallocation.h
	framed_audio.c framed_audio.h
	findsep.c findsep.h
	findsep2.c
	http.c http.h
	../third_party/iconvert.c
	mchar.c mchar.h
	parse.c parse.h
	prefs.c prefs.h
	relaylib.c relaylib.h
	recording_lock.c recording_lock.h
	ripaac.c
	ripogg.c ripogg.h
	ripstream.c ripstream.h
	stream_capture.c stream_capture.h
	ogg_flac_output.c ogg_flac_output.h
	ripstream_mp3.c ripstream_mp3.h
	ripstream_wav.c ripstream_wav.h
	ripstream_ogg.c
	rip_manager.c rip_manager.h
	socklib.c socklib.h
	threadlib.c threadlib.h
	track_info.c track_info.h
	../third_party/utf8.c ../third_party/utf8.h

	../third_party/charmaps.h
	confw32.h
	../third_party/linux/list.h
	sr_compat.h
	srtypes.h
	../third_party/dirent/uce_dirent.h
	)

IF (NOT IBERTY_FOUND)
  SET (STREAMRIPPER_LIB_SRC
    ${STREAMRIPPER_LIB_SRC}
    ../third_party/libiberty/argv.c)
ENDIF (NOT IBERTY_FOUND)

ADD_LIBRARY (streamripper1 ${STREAMRIPPER_LIB_SRC})
INSTALL (TARGETS streamripper1 DESTINATION lib)
