#ifndef TRAVERSO_TYPES_H
#define TRAVERSO_TYPES_H

#include <QString>
#include <QStringList>


/**
 * Type used to represent sample frame counts.
 */
typedef uint32_t     nframes_t;


/**
 * Type used to represent the value of free running
 * monotonic clock with units of microseconds.
 */
typedef long trav_time_t;

typedef unsigned long channel_t;

typedef float audio_sample_t;


/**
 *  A port has a set of flags that are formed by AND-ing together the
 *  desired values from the list below. The flags "PortIsInput" and
 *  "PortIsOutput" are mutually exclusive and it is an error to use
 *  them both.
 */
enum PortFlags {

     /**
	 * if PortIsInput is set, then the port can receive
	 * data.
      */
	PortIsInput = 0x1,

     /**
  * if PortIsOutput is set, then data can be read from
  * the port.
      */
 PortIsOutput = 0x2,

     /**
  * if PortIsPhysical is set, then the port corresponds
  * to some kind of physical I/O connector.
      */
 PortIsPhysical = 0x4,

     /**
  * if PortCanMonitor is set, then a call to
  * jack_port_request_monitor() makes sense.
  *
  * Precisely what this means is dependent on the client. A typical
  * result of it being called with TRUE as the second argument is
  * that data that would be available from an output port (with
  * PortIsPhysical set) is sent to a physical output connector
  * as well, so that it can be heard/seen/whatever.
  *
  * Clients that do not control physical interfaces
  * should never create ports with this bit set.
      */
 PortCanMonitor = 0x8,

     /**
      * PortIsTerminal means:
  *
  *	for an input port: the data received by the port
  *                    will not be passed on or made
  *		           available at any other port
  *
  * for an output port: the data available at the port
  *                    does not originate from any other port
  *
  * Audio synthesizers, I/O hardware interface clients, HDR
  * systems are examples of clients that would set this flag for
  * their ports.
      */
 PortIsTerminal = 0x10
};


#if defined(_MSC_VER) || defined(__MINGW32__)
#  include <time.h>
#ifndef _TIMEVAL_DEFINED /* also in winsock[2].h */
#define _TIMEVAL_DEFINED
struct timeval {
   long tv_sec;
   long tv_usec;
};
#endif /* _TIMEVAL_DEFINED */
#else
#  include <sys/time.h>
#endif


#if defined(_MSC_VER) || defined(__MINGW32__)

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int gettimeofday(struct timeval* tp, void* tzp) {
	DWORD t;
	t = timeGetTime();
	tp->tv_sec = t / 1000;
	tp->tv_usec = t % 1000;
	/* 0 indicates that the call succeeded. */
	return 0;
}
	
typedef uint8_t            u_int8_t;

#ifdef __cplusplus
}
#endif

#endif

#if defined (RELAYTOOL_PRESENT)

#define RELAYTOOL_JACK \
	extern int libjack_is_present;\
 	extern int libjack_symbol_is_present(char *s);

// since wavpack is not commonly available yet, we link to it statically for now
// and set it to available!
/*#define RELAYTOOL_WAVPACK \
	extern int libwavpack_is_present;\
	extern int libwavpack_symbol_is_present(char *s); */
#define RELAYTOOL_WAVPACK \
	static const int libwavpack_is_present=1;\
	static int __attribute__((unused)) libwavpack_symbol_is_present(char *) { return 1; }

#define RELAYTOOL_FLAC \
	extern int libFLAC_is_present;\
 	extern int libFLAC_symbol_is_present(char *s);


#define	RELAYTOOL_MAD \
	extern int libmad_is_present;\
	extern int libmad_symbol_is_present(char *s);

#define RELAYTOOL_OGG \
	extern int libogg_is_present;\
	extern int libogg_symbol_is_present(char *s);

#define RELAYTOOL_VORBIS \
	extern int libvorbis_is_present;\
	extern int libvorbis_symbol_is_present(char *s);

#define RELAYTOOL_VORBISFILE \
	extern int libvorbisfile_is_present;\
	extern int libvorbisfile_symbol_is_present(char *s);

#define RELAYTOOL_VORBISENC \
	extern int libvorbisenc_is_present;\
	extern int libvorbisenc_symbol_is_present(char *s);

#define RELAYTOOL_MP3LAME \
	extern int libmp3lame_is_present;\
	extern int libmp3lame_symbol_is_present(char *s);

#else


#define RELAYTOOL_JACK \
 	static const int libjack_is_present=1;\
	static int __attribute__((unused)) libjack_symbol_is_present(char *) { return 1; }

#define RELAYTOOL_WAVPACK \
	static const int libwavpack_is_present=1;\
	static int __attribute__((unused)) libwavpack_symbol_is_present(char *) { return 1; }

#define RELAYTOOL_FLAC \
	static const int libFLAC_is_present=1;\
	static int __attribute__((unused)) libFLAC_symbol_is_present(char *) { return 1; }

#define RELAYTOOL_MAD \
	static const int libmad_is_present=1;\
	static int __attribute__((unused)) libmad_symbol_is_present(char *) { return 1; }

#define RELAYTOOL_OGG \
	static const int libogg_is_present=1;\
	static int __attribute__((unused)) libogg_symbol_is_present(char *) { return 1; }

#define RELAYTOOL_VORBIS \
	static const int libvorbis_is_present=1;\
	static int __attribute__((unused)) libvorbis_symbol_is_present(char *) { return 1; }

#define RELAYTOOL_VORBISFILE \
	static const int libvorbisfile_is_present=1;\
	static int __attribute__((unused)) libvorbisfile_symbol_is_present(char *) { return 1; }

#define RELAYTOOL_VORBISENC \
	static const int libvorbisenc_is_present=1;\
	static int __attribute__((unused)) libvorbisenc_symbol_is_present(char *) { return 1; }

#define RELAYTOOL_MP3LAME \
	static const int libmp3lame_is_present=1;\
	static int __attribute__((unused)) libmp3lame_symbol_is_present(char *) { return 1; }

#endif // endif RELAYTOOL_PRESENT


#define PROFILE_START auto starttime = TTimeRef::get_microseconds_since_epoch();
#define PROFILE_END(args...) auto processtime = (TTimeRef::get_microseconds_since_epoch() - starttime); printf("Process time for %s: %ld useconds\n\n", args, processtime);

#endif // endif TRAVERSO_TYPES_H

//eof
