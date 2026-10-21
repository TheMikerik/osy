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
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <csignal>

#define STR_CLOSE   "close"
#define STR_QUIT    "quit"
#define PY_EXAMPLES 3       // number of examples before python3 is started

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

// ***************************************************************************
// child process: python3 calculator, its stdout goes directly to the socket

void run_python( int t_sock, const char *t_script, int t_len )
{
    int l_pipe[ 2 ];
    if ( pipe( l_pipe ) < 0 )
    {
        log_msg( LOG_ERROR, "Unable to create pipe." );
        return;
    }

    pid_t l_pid = fork();
    if ( l_pid < 0 )
    {
        log_msg( LOG_ERROR, "Unable to fork for python3." );
        close( l_pipe[ 0 ] );
        close( l_pipe[ 1 ] );
        return;
    }

    if ( l_pid == 0 )
    { // python3 process
        close( l_pipe[ 1 ] );
        dup2( l_pipe[ 0 ], STDIN_FILENO );
        close( l_pipe[ 0 ] );
        dup2( t_sock, STDOUT_FILENO );
        close( t_sock );
        execlp( "python3", "python3", NULL );
        log_msg( LOG_ERROR, "Unable to run python3." );
        exit( EXIT_FAILURE );
    }

    // client process: send examples to python3 and wait for it
    close( l_pipe[ 0 ] );
    if ( write( l_pipe[ 1 ], t_script, t_len ) < 0 )
    {
        log_msg( LOG_ERROR, "Unable to write to python3." );
    }
    close( l_pipe[ 1 ] );
    waitpid( l_pid, nullptr, 0 );
}

void serve_client( int t_sock )
{
    char l_buf[ 256 ];
    char l_script[ PY_EXAMPLES * 300 ];
    int l_count = 0;
    int l_script_len = 0;

    while ( 1 )
    {
        int l_len = read( t_sock, l_buf, sizeof( l_buf ) - 1 );
        if ( l_len == 0 )
        {
            log_msg( LOG_DEBUG, "Client closed socket!" );
            break;
        }
        if ( l_len < 0 )
        {
            log_msg( LOG_ERROR, "Unable to read data from client." );
            break;
        }
        log_msg( LOG_DEBUG, "Read %d bytes from client.", l_len );
        l_buf[ l_len ] = '\0';

        // close request?
        if ( !strncasecmp( l_buf, STR_CLOSE, strlen( STR_CLOSE ) ) )
        {
            log_msg( LOG_INFO, "Client sent 'close' request to close connection." );
            break;
        }

        // every line is one example, e.g. "4+5*8" -> print("4+5*8=",4+5*8)
        char *l_save;
        for ( char *l_tok = strtok_r( l_buf, "\r\n", &l_save ); l_tok; l_tok = strtok_r( nullptr, "\r\n", &l_save ) )
        {
            l_script_len += snprintf( l_script + l_script_len, sizeof( l_script ) - l_script_len,
                                      "print(\"%s=\",%s)\n", l_tok, l_tok );
            if ( ++l_count == PY_EXAMPLES )
            {
                run_python( t_sock, l_script, l_script_len );
                l_count = 0;
                l_script_len = 0;
            }
        }
    }

    close( t_sock );
}

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

    // finished children are reaped automatically
    signal( SIGCHLD, SIG_IGN );

    // go! Two loops inside.
    while ( 1 )
    {
        int l_sock_client = -1;

        // list of fd sources
        pollfd l_read_poll[ 2 ];

        l_read_poll[ 0 ].fd = STDIN_FILENO;
        l_read_poll[ 0 ].events = POLLIN;
        l_read_poll[ 1 ].fd = l_sock_listen;
        l_read_poll[ 1 ].events = POLLIN;

        while ( 1 ) // wait for new client
        {
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
                
                int l_len = read( STDIN_FILENO, buf, sizeof( buf) );
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
                int l_rsa_size = sizeof( l_rsa );
                // new connection
                l_sock_client = accept( l_sock_listen, ( sockaddr * ) &l_rsa, ( socklen_t * ) &l_rsa_size );
                if ( l_sock_client < 0 )
                {
                    log_msg( LOG_ERROR, "Unable to accept new client." );
                    close( l_sock_listen );
                    exit( EXIT_FAILURE );
                }
                uint l_lsa = sizeof( l_srv_addr );
                // my IP
                getsockname( l_sock_client, ( sockaddr * ) &l_srv_addr, &l_lsa );
                log_msg( LOG_INFO, "My IP: '%s'  port: %d",
                                 inet_ntoa( l_srv_addr.sin_addr ), ntohs( l_srv_addr.sin_port ) );
                // client IP
                getpeername( l_sock_client, ( sockaddr * ) &l_srv_addr, &l_lsa );
                log_msg( LOG_INFO, "Client IP: '%s'  port: %d",
                                 inet_ntoa( l_srv_addr.sin_addr ), ntohs( l_srv_addr.sin_port ) );

                fflush( stdout );
                pid_t l_pid = fork();
                if ( l_pid < 0 )
                {
                    log_msg( LOG_ERROR, "Unable to fork for client." );
                }
                else if ( l_pid == 0 )
                {
                    close( l_sock_listen );
                    serve_client( l_sock_client );
                    exit( EXIT_SUCCESS );
                }
                close( l_sock_client );

                break;
            }

        } // while wait for client
    } // while ( 1 )

    return 0;
} // main
