// ***************************************************************************
//
// Program example for labs in subject Operating Systems
//
// Petr Olivka, Dept. of Computer Science, petr.olivka@vsb.cz, 2026
//
// Example of socket server.
//
// This program is example of socket server and it allows to connect and serve
// more clients. Every client is served by its own thread.
// The mandatory argument of program is port number for listening.
//
// ***************************************************************************

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cerrno>
#include <csignal>
#include <string>
#include <vector>
#include <pthread.h>
#include <unistd.h>
#include <poll.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define STR_CLOSE   "close"
#define STR_QUIT    "quit"
#define STR_LOCK    "lock:"
#define STR_UNLOCK  "unlock:"

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
// clients shared by all threads

struct ClientInfo
{
    int sock;
    std::string nick;
};

std::vector< ClientInfo > g_clients;    // connected clients
sem_t g_list_sem;                       // protects g_clients
sem_t g_pause_sem;                      // 0 while some client holds 'lock:'

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

// send text to all connected clients
void send_to_all( const std::string &t_text )
{
    sem_wait( &g_list_sem );
    for ( const ClientInfo &l_client : g_clients )
    {
        if ( write( l_client.sock, t_text.c_str(), t_text.size() ) < 0 )
        {
            log_msg( LOG_ERROR, "Unable to send data to client." );
        }
    }
    sem_post( &g_list_sem );
}

// ***************************************************************************
// thread serving one client

void *client_thread( void *t_par )
{
    int t_sock = ( intptr_t ) t_par;
    std::string l_nick;
    bool l_named = false;       // nick already entered
    bool l_locked = false;      // this client holds 'lock:'

    // add socket of this client to the list
    sem_wait( &g_list_sem );
    g_clients.push_back( { t_sock, "" } );
    sem_post( &g_list_sem );

    const char *l_ask = "Enter your nick: ";
    if ( write( t_sock, l_ask, strlen( l_ask ) ) < 0 )
    {
        log_msg( LOG_ERROR, "Unable to send data to client." );
    }

    while ( 1 )
    {
        char l_buf[ 256 ];

        int l_len = read( t_sock, l_buf, sizeof( l_buf ) - 1 );
        if ( l_len <= 0 )
        {
            log_msg( LOG_DEBUG, "Client closed socket or read failed." );
            break;
        }

        l_buf[ l_len ] = '\0';
        l_buf[ strcspn( l_buf, "\r\n" ) ] = '\0';   // remove new line
        std::string l_text = l_buf;

        // first message is nick
        if ( !l_named )
        {
            l_nick = l_text;
            l_named = true;

            sem_wait( &g_list_sem );
            for ( ClientInfo &l_client : g_clients )
            {
                if ( l_client.sock == t_sock )
                {
                    l_client.nick = l_nick;
                }
            }
            sem_post( &g_list_sem );

            log_msg( LOG_INFO, "Client '%s' entered the chat.", l_nick.c_str() );
            continue;
        }

        // close request?
        if ( !strncasecmp( l_buf, STR_CLOSE, strlen( STR_CLOSE ) ) )
        {
            log_msg( LOG_INFO, "Client '%s' sent 'close' request.", l_nick.c_str() );
            break;
        }

        if ( l_text == STR_LOCK )
        {
            if ( !l_locked )
            {
                sem_wait( &g_pause_sem );   // wait until nobody else holds the lock
                l_locked = true;
            }
            continue;
        }

        if ( l_text == STR_UNLOCK )
        {
            if ( l_locked )
            {
                l_locked = false;
                sem_post( &g_pause_sem );
            }
            continue;
        }

        std::string l_msg = l_nick + ": " + l_text + "\n";

        if ( l_locked )
        {
            send_to_all( l_msg );
        }
        else
        {
            sem_wait( &g_pause_sem );       // wait while another client holds 'lock:'
            send_to_all( l_msg );
            sem_post( &g_pause_sem );
        }
    }

    // client is leaving: release lock, remove from list, close socket
    if ( l_locked )
    {
        sem_post( &g_pause_sem );
    }

    sem_wait( &g_list_sem );
    for ( auto l_it = g_clients.begin(); l_it != g_clients.end(); ++l_it )
    {
        if ( l_it->sock == t_sock )
        {
            g_clients.erase( l_it );
            break;
        }
    }
    sem_post( &g_list_sem );

    close( t_sock );
    log_msg( LOG_INFO, "Client '%s' left.", l_nick.c_str() );

    pthread_exit( nullptr );
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

    // writing to socket closed by client must not kill the server
    signal( SIGPIPE, SIG_IGN );

    sem_init( &g_list_sem, 0, 1 );
    sem_init( &g_pause_sem, 0, 1 );

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

    // list of fd sources
    pollfd l_read_poll[ 2 ];

    l_read_poll[ 0 ].fd = STDIN_FILENO;
    l_read_poll[ 0 ].events = POLLIN;
    l_read_poll[ 1 ].fd = l_sock_listen;
    l_read_poll[ 1 ].events = POLLIN;

    // go! Main thread accepts clients and reads keyboard, each client has own thread
    while ( 1 )
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
            char l_buf[ 128 ];

            int l_len = read( STDIN_FILENO, l_buf, sizeof( l_buf ) );
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

            // request to quit?
            if ( !strncmp( l_buf, STR_QUIT, strlen( STR_QUIT ) ) )
            {
                log_msg( LOG_INFO, "Request to 'quit' entered.");
                close( l_sock_listen );
                exit( EXIT_SUCCESS );
            }

            // server text goes to all clients
            send_to_all( "Server: " + std::string( l_buf, l_len ) );
        }

        if ( l_read_poll[ 1 ].revents & POLLIN )
        { // new client?
            sockaddr_in l_rsa;
            socklen_t l_rsa_size = sizeof( l_rsa );

            // new connection
            int l_sock_client = accept( l_sock_listen, ( sockaddr * ) &l_rsa, &l_rsa_size );
            if ( l_sock_client < 0 )
            {
                log_msg( LOG_ERROR, "Unable to accept new client." );
                continue;
            }

            log_msg( LOG_INFO, "Client IP: '%s'  port: %d",
                             inet_ntoa( l_rsa.sin_addr ), ntohs( l_rsa.sin_port ) );

            // thread owns the socket and closes it when client leaves
            pthread_t l_thread_id;
            int l_err = pthread_create( &l_thread_id, nullptr, client_thread, ( void * ) ( intptr_t ) l_sock_client );
            if ( l_err != 0 )
            {
                log_msg( LOG_ERROR, "Unable to create thread for client." );
                close( l_sock_client );
            }
            else
            {
                pthread_detach( l_thread_id );
            }
        }
    }

    return 0;
} // main
