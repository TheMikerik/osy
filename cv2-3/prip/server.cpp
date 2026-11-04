// ***************************************************************************
//
// Program example for labs in subject Operating Systems
//
// Petr Olivka, Dept. of Computer Science, petr.olivka@vsb.cz, 2026
//
// Example of socket server.
//
// This program is example of socket server and it allows to connect and serve
// the only one client.
// The mandatory argument of program is port number for listening.
//
// ***************************************************************************

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <semaphore.h>

#define STR_CLOSE   "close"
#define STR_QUIT    "quit"

#define N_PLAYERS   3
#define FIELD_SIZE  9

// ***************************************************************************
// shared memory

struct Shared
{
    char field[ FIELD_SIZE ][ FIELD_SIZE ];
    pid_t pid[ N_PLAYERS ];
    sem_t sem[ N_PLAYERS ];
};

Shared *g_sh = nullptr;

// ***************************************************************************
// log messages

#define LOG_ERROR               0       // errors
#define LOG_INFO                1       // information and notifications
#define LOG_DEBUG               2       // debug messages

// debug flag
int g_debug = LOG_INFO;

void log_msg( int t_log_level, const char *t_form, ... )
{
    const char *out_fmt[] = {
            "ERR: (%d-%s) %s\n",
            "INF: %s\n",
            "DEB: %s\n" };

    if ( t_log_level && t_log_level > g_debug )
    {
        return;
    }

    char l_buf[ 4096 ];
    va_list l_arg;
    va_start( l_arg, t_form );

    vsnprintf( l_buf, sizeof( l_buf ), t_form, l_arg );

    va_end( l_arg );

    switch ( t_log_level )
    {
        case LOG_INFO:
        case LOG_DEBUG:
            fprintf( stdout, out_fmt[ t_log_level ], l_buf );
            break;

        case LOG_ERROR:
            fprintf( stderr, out_fmt[ t_log_level ], errno, strerror( errno ), l_buf );
            break;
    }
}

// ***************************************************************************
// help

const char g_help[] =
    "\n"
    "  Socket server example.\n"
    "\n"
    "  Use: %s [-h] [-d] port_number\n"
    "\n"
    "    -d  debug mode \n"
    "    -h  this help\n"
    "\n"
    ;

// ***************************************************************************
// game

void send_board( int t_sock )
{
    dprintf( t_sock, "  A B C D E F G H I\n" );
    for ( int r = 0; r < FIELD_SIZE; r++ )
    {
        dprintf( t_sock, "%d", r + 1 );
        for ( int c = 0; c < FIELD_SIZE; c++ )
        {
            dprintf( t_sock, " %c", g_sh->field[ r ][ c ] );
        }
        dprintf( t_sock, "\n" );
    }
}

void pass_turn( int t_me )
{
    for ( int k = 1; k <= N_PLAYERS; k++ )
    {
        int next = ( t_me + k ) % N_PLAYERS;
        if ( g_sh->pid[ next ] )
        {
            sem_post( &g_sh->sem[ next ] );
            return;
        }
    }
}

void play( int t_sock, int t_me )
{
    send_board( t_sock );

    while ( 1 )
    {
        sem_wait( &g_sh->sem[ t_me ] );

        send_board( t_sock );
        dprintf( t_sock, "Na tahu jsi. Zadej tah (napr. A-5-Q):\n" );

        char l_buf[ 256 ];
        int l_len = read( t_sock, l_buf, sizeof( l_buf ) - 1 );
        if ( l_len > 0 )
        {
            l_buf[ l_len ] = 0;
        }

        if ( l_len <= 0 || !strncasecmp( l_buf, STR_CLOSE, strlen( STR_CLOSE ) ) )
        {
            g_sh->pid[ t_me ] = 0;
            pass_turn( t_me );
            break;
        }

        char l_col, l_val;
        int l_row;
        if ( sscanf( l_buf, "%c-%d-%c", &l_col, &l_row, &l_val ) == 3 )
        {
            g_sh->field[ l_row - 1 ][ l_col - 'A' ] = l_val;
        }

        send_board( t_sock );
        pass_turn( t_me );
    }

    close( t_sock );
    exit( EXIT_SUCCESS );
}

// ***************************************************************************

int main( int t_argc, char **t_argv )
{
    int l_port = 0;

    // parsing arguments
    for ( int inx = 1; inx < t_argc; inx++ )
    {
        if ( strcmp( t_argv[ inx ], "-d" ) == 0 )
        {
            g_debug = LOG_DEBUG;
        }

        if ( strcmp( t_argv[ inx ], "-h" ) == 0 )
        {
            printf( g_help, t_argv[ 0 ] );
            exit( EXIT_SUCCESS );
        }

        if ( *t_argv[ inx ] != '-' )
        {
            if ( l_port == 0 )
            {
                l_port = atoi( t_argv[ inx ] );
            }
        }
    }

    if ( l_port <= 0 )
    {
        log_msg( LOG_INFO, "Bad or missing port number %d!", l_port );
        printf( g_help, t_argv[ 0 ] );
        exit( EXIT_FAILURE );
    }

    log_msg( LOG_INFO, "Server will listen on port: %d.", l_port );

    // socket creation
    int l_sock_listen = socket( AF_INET, SOCK_STREAM, 0 );
    if ( l_sock_listen < 0 )
    {
        log_msg( LOG_ERROR, "Unable to create socket.");
        exit( EXIT_FAILURE );
    }

    in_addr l_addr_any = { INADDR_ANY };
    sockaddr_in l_srv_addr;
    l_srv_addr.sin_family = AF_INET;
    l_srv_addr.sin_port = htons( l_port );
    l_srv_addr.sin_addr = l_addr_any;

    // Enable the port number reusing
    int l_opt = 1;
    if ( setsockopt( l_sock_listen, SOL_SOCKET, SO_REUSEADDR, &l_opt, sizeof( l_opt ) ) < 0 )
    {
      log_msg( LOG_ERROR, "Unable to set socket option!" );
    }

    // assign port number to socket
    if ( bind( l_sock_listen, (const sockaddr * ) &l_srv_addr, sizeof( l_srv_addr ) ) < 0 )
    {
        log_msg( LOG_ERROR, "Bind failed!" );
        close( l_sock_listen );
        exit( EXIT_FAILURE );
    }

    // listenig on set port
    if ( listen( l_sock_listen, 1 ) < 0 )
    {
        log_msg( LOG_ERROR, "Unable to listen on given port!" );
        close( l_sock_listen );
        exit( EXIT_FAILURE );
    }

    log_msg( LOG_INFO, "Enter 'quit' to quit server." );

    // shared memory for field, players (PID) and semaphores
    g_sh = ( Shared * ) mmap( nullptr, sizeof( Shared ), PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_ANONYMOUS, -1, 0 );
    for ( int inx = 0; inx < N_PLAYERS; inx++ )
    {
        sem_init( &g_sh->sem[ inx ], 1, 0 );
    }
    memset( g_sh->field, '.', sizeof( g_sh->field ) );

    signal( SIGCHLD, SIG_IGN );
    signal( SIGPIPE, SIG_IGN );

    // go! Every client is served by its own process.
    while ( 1 )
    {
        // list of fd sources
        pollfd l_read_poll[ 2 ];

        l_read_poll[ 0 ].fd = STDIN_FILENO;
        l_read_poll[ 0 ].events = POLLIN;
        l_read_poll[ 1 ].fd = l_sock_listen;
        l_read_poll[ 1 ].events = POLLIN;

        // select from fds
        int l_poll = poll( l_read_poll, 2, -1 );

        if ( l_poll < 0 )
        {
            log_msg( LOG_ERROR, "Function poll failed!" );
            exit( EXIT_FAILURE );
        }

        if ( l_read_poll[ 0 ].revents & POLLIN )
        { // data on stdin
            char buf[ 128 ];

            int l_len = read( STDIN_FILENO, buf, sizeof( buf ) );
            if ( l_len == 0 )
            {
                log_msg( LOG_DEBUG, "Stdin closed." );
                exit( EXIT_SUCCESS );
            }
            if ( l_len < 0 )
            {
                log_msg( LOG_DEBUG, "Unable to read from stdin!" );
                exit( EXIT_FAILURE );
            }

            log_msg( LOG_DEBUG, "Read %d bytes from stdin", l_len );
            // request to quit?
            if ( !strncmp( buf, STR_QUIT, strlen( STR_QUIT ) ) )
            {
                log_msg( LOG_INFO, "Request to 'quit' entered.");
                close( l_sock_listen );
                exit( EXIT_SUCCESS );
            }
        }

        if ( l_read_poll[ 1 ].revents & POLLIN )
        { // new client?
            sockaddr_in l_rsa;
            socklen_t l_rsa_size = sizeof( l_rsa );
            int l_sock_client = accept( l_sock_listen, ( sockaddr * ) &l_rsa, &l_rsa_size );
            if ( l_sock_client < 0 )
            {
                log_msg( LOG_ERROR, "Unable to accept new client." );
                close( l_sock_listen );
                exit( EXIT_FAILURE );
            }
            log_msg( LOG_INFO, "Client IP: '%s'  port: %d",
                             inet_ntoa( l_rsa.sin_addr ), ntohs( l_rsa.sin_port ) );

            // first free position at the table (PID == 0)
            int l_me = 0;
            while ( l_me < N_PLAYERS && g_sh->pid[ l_me ] )
            {
                l_me++;
            }
            if ( l_me == N_PLAYERS )
            {
                close( l_sock_client );
                continue;
            }

            // the first player starts the game
            bool l_first = true;
            for ( int inx = 0; inx < N_PLAYERS; inx++ )
            {
                if ( g_sh->pid[ inx ] )
                {
                    l_first = false;
                }
            }
            if ( l_first )
            {
                sem_post( &g_sh->sem[ l_me ] );
            }

            pid_t l_pid = fork();
            if ( l_pid == 0 )
            {
                close( l_sock_listen );
                play( l_sock_client, l_me );
            }

            g_sh->pid[ l_me ] = l_pid;
            close( l_sock_client );
        }
    } // while ( 1 )

    return 0;
} // main
