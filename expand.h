/**********          address.h          **********/

/* return codes for address_expand */
#define	ADDRESS_FINAL			1
#define	ADDRESS_EXCLUDE			2

/* return codes for address_local */
#define	ADDRESS_LOCAL			3
#define	ADDRESS_NOT_LOCAL		4

/* return codes for address_local & address_expand */
#define	ADDRESS_SYSERROR		5

/* address types */
#define	ADDRESS_TYPE_EMAIL		0x0001
#ifdef HAVE_LDAP
#define	ADDRESS_TYPE_LDAP		0x0010
#define	ADDRESS_MASK_EXCLUSIVE		0x0100
#endif /* HAVE_LDAP */

struct expand {
    struct envelope		*exp_env;	/* original envelope */
    struct stab_entry		*exp_addr_list;	/* list of expanded addrs */
#ifdef HAVE_LDAP
    /* these variables are needed to do exclusive groups */
    struct exp_addr		*exp_addr_root;	/* root of address tree */
    struct exp_addr		*exp_addr_current; /* root of address tree */
#endif /* HAVE_LDAP */
};

struct exp_addr {
    char			*e_addr;	/* address string */
    int				e_addr_type;	/* address type */
    struct recipient		*e_addr_rcpt;	/* address error handle */
#ifdef HAVE_LDAP
    /* these variables are needed to do exclusive groups */
    int				e_addr_exclusive;
    struct exp_addr		*e_addr_parent;
    struct exp_addr		*e_addr_peer;
    struct exp_addr		*e_addr_child;
#endif /* HAVE_LDAP */
};

/* expand.c */
int	expand ___P(( struct host_q **, struct envelope * ));

/* address.c */
void expansion_stab_stdout( void * );
int add_address( struct expand *, char *, struct recipient *, int );
int address_local( char * );
int address_expand( struct expand *, struct exp_addr * );
