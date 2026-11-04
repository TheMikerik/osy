// Zadání: Sdílený seznam úkolů se souborem

// Vytvořte server a klienta pro sdílený seznam úkolů, který se ukládá do souboru.

// Server

// Při startu vytvoří (nebo vyprázdní) soubor ukoly.txt a soubor log.txt.
// Každý připojený klient obslouží samostatné vlákno. Seznam připojených klientů je společný pro všechna vlákna.
// Po připojení server klientovi pošle výzvu Enter your nick: . Dokud klient nepošle nick, server nic dalšího neposílá.
// Po nicku server pošle klientovi celý seznam úkolů.
// Úkol má id, nick autora, stav (open / done) a text. Seznam se ukládá do ukoly.txt, každý úkol na jeden řádek ve tvaru id|nick|stav|text.
// Klient může posílat tyto příkazy:
// add-text: přidá úkol s novým id a stavem open. Odpověď: Added id N a celý seznam všem klientům.
// done-N: označí úkol N jako done. Odpověď: celý seznam všem klientům.
// del-N: smaže úkol N, ale jen jeho autor. Ostatním klientům server pošle Denied. Odpověď při úspěchu: celý seznam všem klientům.
// list: pošle celý seznam jen tomuto klientu.
// Každý přijatý příkaz (včetně neplatného) se zapíše do log.txt ve tvaru nick: příkaz.
// Text napsaný na klávesnici serveru se rozešle všem klientům jako Server: text a zapíše do log.txt.
// Přístup k oběma souborům i k seznamu klientů chrání anonymní semafor. Klient se odpojí tak, že ho vlákno odebere ze seznamu a zavře socket.
// Klient

// Stejné chování jako u předchozích úkolů.
// Čtení z klávesnice a čtení ze soketu jsou ve dvou vláknech. Funkce poll se nepoužívá.
// close klienta ukončí.




// Server:

// Při startu vytvoří prázdný ukoly.txt a vymaže log.txt.
// Po připojení pošle výzvu Enter your nick: . Po nicku pošle celý seznam úkolů.
// Příkazy:
// add-<text> přidá úkol. Odpověď všem: Added id N a seznam.
// done-N označí úkol jako hotový a rozešle seznam všem.
// del-N smaže úkol, jen pokud ho přidal ten samý klient. Jinak pošle jen tomu klientovi Denied.
// list pošle seznam jen tomuto klientovi.
// Každý úkol je v ukoly.txt jako id|nick|stav|text a soubor se po každé změně přepíše.
// Každý příkaz se zapíše do log.txt jako nick: příkaz. Text z klávesnice serveru se rozešle všem jako Server: text a zapíše do logu jako Server: text.
// Seznam klientů, úkoly i oba soubory chrání jeden anonymní semafor g_list_sem.

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

struct ClientInfo
{
    int sock;
    std::string nick;
};

struct Task
{
    int id;
    std::string nick;
    std::string state;
    std::string text;
};

std::vector< ClientInfo > g_clients;
std::vector< Task > g_tasks;
int g_next_id = 1;
sem_t g_list_sem;

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

void send_text( int t_sock, const std::string &t_text )
{
    if ( write( t_sock, t_text.c_str(), t_text.size() ) < 0 )
    {
        log_msg( LOG_ERROR, "Unable to send data to client." );
    }
}

void send_to_all( const std::string &t_text )
{
    for ( const ClientInfo &l_client : g_clients )
    {
        send_text( l_client.sock, t_text );
    }
}

std::string tasks_text()
{
    std::string l_text;
    for ( const Task &l_task : g_tasks )
    {
        l_text += std::to_string( l_task.id ) + "|" + l_task.nick + "|" + l_task.state + "|" + l_task.text + "\n";
    }
    return l_text;
}

void save_tasks()
{
    FILE *l_file = fopen( "ukoly.txt", "w" );
    if ( l_file == nullptr )
    {
        log_msg( LOG_ERROR, "Unable to open ukoly.txt." );
        return;
    }
    fputs( tasks_text().c_str(), l_file );
    fclose( l_file );
}

void log_line( const std::string &t_nick, const std::string &t_cmd )
{
    FILE *l_file = fopen( "log.txt", "a" );
    if ( l_file == nullptr )
    {
        log_msg( LOG_ERROR, "Unable to open log.txt." );
        return;
    }
    fprintf( l_file, "%s: %s\n", t_nick.c_str(), t_cmd.c_str() );
    fclose( l_file );
}

int find_task( int t_id )
{
    for ( int inx = 0; inx < ( int ) g_tasks.size(); inx++ )
    {
        if ( g_tasks[ inx ].id == t_id )
        {
            return inx;
        }
    }
    return -1;
}

void handle_command( int t_sock, const std::string &t_nick, const std::string &t_cmd )
{
    if ( t_cmd.compare( 0, 4, "add-" ) == 0 )
    {
        g_tasks.push_back( { g_next_id, t_nick, "open", t_cmd.substr( 4 ) } );
        save_tasks();
        send_to_all( "Added id " + std::to_string( g_next_id ) + "\n" + tasks_text() );
        g_next_id++;
    }
    else if ( t_cmd.compare( 0, 5, "done-" ) == 0 )
    {
        int l_inx = find_task( atoi( t_cmd.c_str() + 5 ) );
        if ( l_inx >= 0 )
        {
            g_tasks[ l_inx ].state = "done";
            save_tasks();
            send_to_all( tasks_text() );
        }
    }
    else if ( t_cmd.compare( 0, 4, "del-" ) == 0 )
    {
        int l_inx = find_task( atoi( t_cmd.c_str() + 4 ) );
        if ( l_inx >= 0 )
        {
            if ( g_tasks[ l_inx ].nick == t_nick )
            {
                g_tasks.erase( g_tasks.begin() + l_inx );
                save_tasks();
                send_to_all( tasks_text() );
            }
            else
            {
                send_text( t_sock, "Denied\n" );
            }
        }
    }
    else if ( t_cmd == "list" )
    {
        send_text( t_sock, tasks_text() );
    }

    log_line( t_nick, t_cmd );
}

void *client_thread( void *t_par )
{
    int t_sock = ( intptr_t ) t_par;
    std::string l_nick;
    bool l_named = false;

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
        l_buf[ strcspn( l_buf, "\r\n" ) ] = '\0';
        std::string l_text = l_buf;

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
            send_text( t_sock, tasks_text() );
            sem_post( &g_list_sem );

            log_msg( LOG_INFO, "Client '%s' entered the list.", l_nick.c_str() );
            continue;
        }

        if ( !strncasecmp( l_buf, STR_CLOSE, strlen( STR_CLOSE ) ) )
        {
            log_msg( LOG_INFO, "Client '%s' sent 'close' request.", l_nick.c_str() );
            break;
        }

        sem_wait( &g_list_sem );
        handle_command( t_sock, l_nick, l_text );
        sem_post( &g_list_sem );
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

    signal( SIGPIPE, SIG_IGN );
    sem_init( &g_list_sem, 0, 1 );

    save_tasks();
    FILE *l_log = fopen( "log.txt", "w" );
    if ( l_log == nullptr )
    {
        log_msg( LOG_ERROR, "Unable to create log.txt." );
        exit( EXIT_FAILURE );
    }
    fclose( l_log );

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

            std::string l_text( l_buf, l_len );
            if ( !l_text.empty() && l_text.back() == '\n' )
            {
                l_text.pop_back();
            }

            sem_wait( &g_list_sem );
            send_to_all( "Server: " + l_text + "\n" );
            log_line( "Server", l_text );
            sem_post( &g_list_sem );
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
