#ifdef __STDC__
#define ___P(x)		x
#else /* __STDC__ */
#define ___P(x)		()
#endif /* __STDC__ */

#include "config.h"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/param.h>

#ifdef HAVE_LIBSSL
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#endif /* HAVE_LIBSSL */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <utime.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <syslog.h>

#include <snet.h>

#include "queue.h"
#include "envelope.h"
#include "bounce.h"
#include "expand.h"
#include "ll.h"
#include "simta.h"

int				simta_expand_debug = 0;


    /* return EXPAND_OK on success
     * return EXPAND_SYSERROR on syserror
     * return EXPAND_FATAL on fata errors (leaving fast files behind in error)
     * syslog errors
     */

    int
expand_and_deliver( struct host_q **hq_stab, struct envelope *unexpanded_env )
{
    syslog( LOG_DEBUG, "expand_and_deliver called" );

    switch ( expand( hq_stab, unexpanded_env )) {
	case 0:
	    q_runner( hq_stab );
	    if ( simta_fast_files > 0 ) {
		syslog( LOG_ERR, "expand_and_deliver fast file fatal error" );
		return( EXPAND_FATAL );
	    }
	    syslog( LOG_DEBUG, "expand_and_deliver returning OK" );
	    return( EXPAND_OK );

	default:
	    syslog( LOG_ERR, "expand_and_deliver expand value out of range" );
	case 1:
	case -1:
	    env_slow( unexpanded_env );
	    if ( simta_fast_files > 0 ) {
		syslog( LOG_ERR, "expand_and_deliver fast file fatal error" );
		return( EXPAND_FATAL );
	    }
	    syslog( LOG_DEBUG, "expand_and_deliver returning syserror" );
	    return( EXPAND_SYSERROR );
    }
}


    /* return 0 on success
     * return 1 on syserror
     * return -1 on fata errors (leaving fast files behind in error)
     * syslog errors
     */

    int
expand( struct host_q **hq_stab, struct envelope *unexpanded_env )
{
    struct expand		exp;
    struct recipient		*r;
    struct stab_entry		*i = NULL;
    struct exp_addr		*e_addr;
    char			*domain;
    SNET			*snet;
    struct message		*m;
    struct host_q		*hq;
    struct stab_entry		*host_stab = NULL;
    struct envelope		*env;
    struct envelope		*bounce_env;
    struct timeval              tv;
    int				return_value = 1;
    int				expanded_out = 0;
    int				fast_file_start;
    char			e_original[ MAXPATHLEN ];
    char			d_original[ MAXPATHLEN ];
    char			d_fast[ MAXPATHLEN ];

    syslog( LOG_DEBUG, "expand.starting" );

    memset( &exp, 0, sizeof( struct expand ));
    exp.exp_env = unexpanded_env;
    simta_rcpt_errors = 0;
    fast_file_start = simta_fast_files;

    /* call address_expand on each address in the expansion list.
     *
     * if an address is expandable, the address(es) that it expands to will
     * be added to the expansion list. These non-terminal addresses must
     * have their st_data set to NULL to specify that they are not to be
     * included in the terminal expansion list. 
     *
     * Any address in the expansion list who's st_data is not NULL is
     * considered a terminal address and will be written out as one
     * of the addresses in expanded envelope(s).
     */ 

    for ( r = unexpanded_env->e_rcpt; r != NULL; r = r->r_next ) {
	exp.exp_addr_parent = NULL;

	if ( add_address( &exp, r->r_rcpt, r, ADDRESS_TYPE_EMAIL ) != 0 ) {
	    /* add_address syslogs errors */
	    goto cleanup1;
	}

	for ( ; ; ) {
	    if ( i == NULL ) {
		i = exp.exp_addr_list;
	    } else if ( i->st_next == NULL ) {
		break;
	    } else {
		i = i->st_next;
	    }

	    e_addr = (struct exp_addr*)i->st_data;
	    exp.exp_addr_parent = e_addr;

	    switch ( address_expand( &exp, e_addr )) {
	    case ADDRESS_EXCLUDE:
		/* the address is not a terminal local address */
		free( i->st_data );
		i->st_data = NULL;
		break;

	    case ADDRESS_FINAL:
		/* the address is a terminal local address */
		break;

	    default:
		syslog( LOG_ERR,
			"expand.address_expand: out of bounds return" );
	    case ADDRESS_SYSERROR:
		goto cleanup1;
	    }
	}
    }

    sprintf( d_original, "%s/D%s", unexpanded_env->e_dir,
	    unexpanded_env->e_id );

    /* Create one expanded envelope for every host we expanded address for */
    for ( i = exp.exp_addr_list; i != NULL; i = i->st_next ) {
	if ( i->st_data == NULL ) {
	    /* not a terminal expansion, do not add */
	    continue;
	}

#ifdef HAVE_LDAP
	/* XXX prune exclusive groups the sender is not a member of */
	/* XXX Check to see that we only write out TYPE_EMAIL addresses? */
#endif /* HAVE_LDAP */


	if (( domain = strchr( i->st_key, '@' )) == NULL ) {
	    syslog( LOG_ERR, "expand.strchr: unreachable code" );
	    goto cleanup2;
	}
	domain++;

	if (( env = (struct envelope*)ll_lookup( host_stab, domain ))
		== NULL ) {
	    if ( strlen( domain ) > MAXHOSTNAMELEN ) {
		syslog( LOG_ERR, "expand.strlen: domain too long" );
		goto cleanup2;
	    }

	    /* Create envelope and add it to list */
	    if (( env = env_create( NULL )) == NULL ) {
		syslog( LOG_ERR, "expand.env_create: %m" );
		goto cleanup2;
	    }

	    if ( gettimeofday( &tv, NULL ) != 0 ) {
		syslog( LOG_ERR, "expand.gettimeofday: %m" );
		free( env );
		goto cleanup2;
	    }

	    /* fill in env */
	    env->e_dir = simta_dir_fast;
	    env->e_mail = unexpanded_env->e_mail;
	    strcpy( env->e_expanded, domain );
	    sprintf( env->e_id, "%lX.%lX", (unsigned long)tv.tv_sec,
			(unsigned long)tv.tv_usec );

	    /* Add env to host_stab */
	    if ( ll_insert( &host_stab, domain, env, NULL ) != 0 ) {
		syslog( LOG_ERR, "expand.ll_insert: %m" );
		free( env );
		goto cleanup2;
	    }
	}

	if (( r = (struct recipient *)malloc( sizeof( struct recipient )))
		== NULL ) {
	    syslog( LOG_ERR, "expand.malloc: %m" );
	    goto cleanup2;
	}

	if (( r->r_rcpt = strdup( i->st_key )) == NULL ) {
	    free( r );
	    syslog( LOG_ERR, "expand.strdup: %m" );
	    goto cleanup2;
	}

	r->r_next = env->e_rcpt;
	env->e_rcpt = r;
    }

    /* Write out all expanded envelopes and place them in to the host_q */
    for ( i = host_stab; i != NULL; i = i->st_next ) {
	env = i->st_data;

	if ( simta_expand_debug == 0 ) {
	    /* create message to put in host queue */
	    if (( m = message_create( env->e_id )) == NULL ) {
		/* message_create syslogs errors */
		/* XXX expansion terminal failure */
		return( 1 );
	    }

	    /* create all messages we are expanding in the FAST queue */
	    m->m_dir = simta_dir_fast;

	    /* find / create the expanded host queue */
	    if (( hq = host_q_lookup( hq_stab, env->e_expanded )) == NULL ) {
		/* host_q_lookup syslogs errors */
		/* XXX expansion terminal failure */
		return( 1 );
	    }

	    /* Dfile: link Dold_id simta_dir_fast/Dnew_id */
	    sprintf( d_fast, "%s/D%s", simta_dir_fast, env->e_id );

	    if ( link( d_original, d_fast ) != 0 ) {
		syslog( LOG_ERR, "expand: link %s %s: %m", d_original, d_fast );
		free( env );
		/* XXX expansion terminal failure */
		return( 1 );
	    }

	    /* Efile: write simta_dir_fast/Enew_id for all recipients at host */
	    if ( env_outfile( env, simta_dir_fast ) != 0 ) {
		/* env_outfile syslogs errors */
		/* XXX expansion terminal failure */
		/* XXX unlink dfile */
		return( 1 );
	    }

	    /* queue message "m" in host queue "hq" */
	    message_queue( hq, m );

	    /* env has corrected etime after disk access */
	    m->m_etime.tv_sec = env->e_etime.tv_sec;
	    m->m_env = env;

	} else {
	    env_stdout( env );
	    printf( "\n" );
	}
    }

    /* XXX what if no epanded envs?  mail loop? */

    if ( simta_expand_debug != 0 ) {
	return( 0 );
    }

    /* if there were any failed expansions, create a bounce message */
    if ( simta_rcpt_errors != 0 ) {
	/* Create bounces */
	if (( snet = snet_open( d_original, O_RDWR, 0, 1024 * 1024 ))
		== NULL ) {
	    syslog( LOG_ERR, "expand.snet_open %s: %m", d_original );
	    goto cleanup3;
	}

	if (( bounce_env = bounce( unexpanded_env, snet )) == NULL ) {
	    if ( snet_close( snet ) != 0 ) {
		syslog( LOG_ERR, "expand.snet_close: %m" );
	    }
	    goto cleanup3;
	}

	if ( snet_close( snet ) != 0 ) {
	    syslog( LOG_ERR, "expand.snet_close: %m" );
	    goto cleanup4;
	}
    }

    /* trunacte & delete unexpanded message */
    sprintf( e_original, "%s/E%s", unexpanded_env->e_dir,
	    unexpanded_env->e_id );

    if ( unexpanded_env->e_dir != simta_dir_fast ) {
	if ( truncate( e_original, (off_t)0 ) != 0 ) {
	    syslog( LOG_ERR, "expand.truncate %s: %m", e_original );
	    goto cleanup4;
	}
    }

    env_unlink( unexpanded_env );

    return_value = 0;
    goto cleanup2;

cleanup4:
    env_unlink( bounce_env );

cleanup3:
    /* unlink any expanded envelopes already written out */
    i = host_stab;
    while ( expanded_out > 0 ) {
	env = i->st_data;
	env_unlink( env );
	i = i->st_next;
	expanded_out--;
    }

    if ( simta_fast_files != fast_file_start ) {
	return( -1 );
    }

cleanup2:
    if ( bounce_env != NULL ) {
	env_free( bounce_env );
	free( bounce_env );
    }
    /* XXX free host_stab */

cleanup1:
    /* XXX walk & free memory in exp->exp_addr_list */

    return( return_value );
}
