#include "config.h"

#ifdef HAVE_LIBSSL
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#endif /* HAVE_LIBSSL */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/param.h>

#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <netdb.h>
#include <unistd.h>
#include <syslog.h>
#include <sysexits.h>
#include <utime.h>

#include <snet.h>

#include "denser.h"
#include "envelope.h"
#include "mx.h"
#include "ll.h"
#include "simta.h"

#define SIMREVERSE_EXIT_VALID		0
#define SIMREVERSE_EXIT_INVALID		1
#define SIMREVERSE_EXIT_ERROR		2

    int
main( int argc, char *argv[])
{
    int			rc;
    struct in_addr	addr;

    if ( argc < 1 ) {
	fprintf( stderr, "Usage: %s address\n", argv[ 0 ]);
	exit( EX_USAGE );
    }

    rc = inet_pton( AF_INET, argv[ 1 ], &addr );
    if ( rc < 0 ) {
	perror( "inet_pton" );
	exit( SIMREVERSE_EXIT_ERROR );
    } else if ( rc == 0 ) {
	fprintf( stderr, "%s: invalid address\n", argv[ 1 ] );
	exit( SIMREVERSE_EXIT_ERROR );
    }

    switch ( check_reverse( argv[ 1 ], &addr )) {
    case 0:
	printf( "valid reverse\n" );
	exit( SIMREVERSE_EXIT_VALID );

    case 1:
	printf( "invalid reverse\n" );
	exit( SIMREVERSE_EXIT_INVALID );

    default:
	if ( simta_dnsr == NULL ) {
	    perror( "dnsr_new" );
	} else {
	    fprintf( stderr, "check_reverse: %s\n",
		dnsr_err2string( dnsr_errno( simta_dnsr )));
	}
	exit( SIMREVERSE_EXIT_ERROR );
    }
}
