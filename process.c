#include <gtk/gtk.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#define MAX_BUFFER 1024
#define WAIT_TIME 5
#define MAX_PROCESSES 10
#define WAIT_TIME_FOR_LEADER 10

typedef struct {
    int id;
    int socket_fd;
    struct sockaddr_in address;
    char ip[INET_ADDRSTRLEN];
    int port;
    int is_leader;
} Process;

Process this_process;

typedef struct {
    int socket_fd;
    struct sockaddr_in address;
} Connection;

Connection current_connection;

volatile gboolean election_response_received = FALSE;

Process all_processes[MAX_PROCESSES];
int num_all_processes = 0;

char current_leader_id[MAX_BUFFER];

void initialize_process(int id, const char *ip, int port);
void *listen_for_messages(void *arg);
void send_message_to_process(Process *proc, const char *message);
void initiate_election();
void on_start_stop_button_clicked(GtkButton *button, gpointer user_data);
void on_connect_button_clicked(GtkButton *button, gpointer user_data);
void on_initiate_election_button_clicked(GtkButton *button, gpointer user_data);
void handle_election_message(const char *message);
void handle_leader_announcement(const char *message);
int check_for_election_responses();
void announce_leadership();
void wait_for_leader_announcement();

int main(int argc, char *argv[]) {
    GtkBuilder *builder;
    GtkWidget *window;
    GtkWidget *start_stop_button, *connect_button, *initiate_election_button;
    GtkEntry *ip_entry, *port_entry;

    initialize_process(1, "127.0.0.1", 5000);

    pthread_t listen_thread;
    pthread_create(&listen_thread, NULL, listen_for_messages, NULL);

    gtk_init(&argc, &argv);

    builder = gtk_builder_new_from_file("GUI/Process_GUI.glade");

    window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    start_stop_button = GTK_WIDGET(gtk_builder_get_object(builder, "start_stop_button"));
    g_signal_connect(start_stop_button, "clicked", G_CALLBACK(on_start_stop_button_clicked), builder);

    connect_button = GTK_WIDGET(gtk_builder_get_object(builder, "connect_button"));
    g_signal_connect(connect_button, "clicked", G_CALLBACK(on_connect_button_clicked), builder);

    initiate_election_button = GTK_WIDGET(gtk_builder_get_object(builder, "initiate_election_button"));
    g_signal_connect(initiate_election_button, "clicked", G_CALLBACK(on_initiate_election_button_clicked), builder);

    // Pobieranie pól tekstowych
    ip_entry = GTK_ENTRY(gtk_builder_get_object(builder, "ip_entry"));
    port_entry = GTK_ENTRY(gtk_builder_get_object(builder, "port_entry"));

    // Ustawienie pól tekstowych jako aktywne
    gtk_editable_set_editable(GTK_EDITABLE(ip_entry), TRUE);
    gtk_editable_set_editable(GTK_EDITABLE(port_entry), TRUE);

    gtk_widget_show_all(window);

    gtk_main();

    return 0;
}

void initialize_process(int id, const char *ip, int port) {
    this_process.id = id;
    strncpy(this_process.ip, ip, INET_ADDRSTRLEN);
    this_process.port = port;
    this_process.is_leader = 0;

    // Utworzenie gniazda
    this_process.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (this_process.socket_fd < 0) {
        perror("Błąd przy tworzeniu gniazda");
        exit(EXIT_FAILURE);
    }

    // Ustawienie adresu IP i portu
    memset(&this_process.address, 0, sizeof(this_process.address));
    this_process.address.sin_family = AF_INET;
    this_process.address.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &this_process.address.sin_addr) <= 0) {
        perror("Błąd przy konwersji adresu IP");
        exit(EXIT_FAILURE);
    }

    // Bind
    if (bind(this_process.socket_fd, (struct sockaddr *)&this_process.address, sizeof(this_process.address)) < 0) {
        perror("Błąd przy bindowaniu gniazda");
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(this_process.socket_fd, SOMAXCONN) < 0) {
        perror("Błąd przy nasłuchiwaniu");
        exit(EXIT_FAILURE);
    }
}

void *listen_for_messages(void *arg) {
    char buffer[MAX_BUFFER];
    ssize_t received_bytes;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd;

    while (1) {
        // Akceptowanie nowego połączenia
        client_fd = accept(this_process.socket_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            perror("Błąd przy akceptowaniu połączenia");
            continue;
        }

        // Odbieranie wiadomości
        received_bytes = recv(client_fd, buffer, MAX_BUFFER - 1, 0);
        if (received_bytes > 0) {
            buffer[received_bytes] = '\0';
            printf("Otrzymano wiadomość: %s\n", buffer);

            // Obsługa otrzymanej wiadomości
            if (strstr(buffer, "ELECTION_") == buffer) {
                // Otrzymano wiadomość o elekcji
                handle_election_message(buffer);
            } else if (strstr(buffer, "LEADER_") == buffer) {
                // Otrzymano wiadomość o ogłoszeniu lidera
                handle_leader_announcement(buffer);
            } else if (strstr(buffer, "RESPONSE_") == buffer) {
                // Otrzymano odpowiedź na elekcję
                // Tutaj można dodać obsługę odpowiedzi na elekcję#define WAIT_TIME_FOR_LEADER 10 // Możesz dostosować czas oczekiwania według własnych potrzeb

            } else {
                // Otrzymano nieznany typ wiadomości
                printf("Otrzymano nieznany typ wiadomości: %s\n", buffer);
            }
        }

        close(client_fd);
    }

    return NULL;
}


void handle_election_message(const char *message) {
    int sender_id;
    if (sscanf(message, "ELECTION_%d", &sender_id) == 1) {
        printf("Otrzymano wiadomość o elekcji od procesu o ID: %d\n", sender_id);
    }
    // Wysyłanie odpowiedzi, jeśli nasze ID jest wyższe
    if (this_process.id > sender_id) {
        char response[MAX_BUFFER];
        sprintf(response, "RESPONSE_%d", this_process.id);
        // Wyszukaj odpowiedni proces i wyślij mu wiadomość
        for (int i = 0; i < num_all_processes; ++i) {
            if (all_processes[i].id == sender_id) {
                send_message_to_process(&all_processes[i], response);
                break;
            }
        }
    }
}


void handle_leader_announcement(const char *message) {
    // Implementacja reakcji na ogłoszenie lidera
    // Może to być np. zapisanie ID nowego lidera
    int leader_id;
    if (sscanf(message, "LEADER_%d", &leader_id) == 1) {
        // Zaktualizuj informacje o liderze
        sprintf(current_leader_id, "%d", leader_id); // Przypisanie ID lidera do current_leader_id
        printf("Nowy lider to: %s\n", current_leader_id);
    }
}


void send_message_to_process(Process *proc, const char *message) {
    if (send(proc->socket_fd, message, strlen(message), 0) < 0) {
        perror("Błąd wysyłania wiadomości");
    }
}


void initiate_election() {
    char message[MAX_BUFFER];
    sprintf(message, "ELECTION_%d", this_process.id);

    // Wysyłanie wiadomości do wszystkich procesów o wyższym ID
    for (int i = 0; i < num_all_processes; ++i) {
        if (all_processes[i].id > this_process.id) {
            send_message_to_process(&all_processes[i], message);
        }
    }

    // Oczekiwanie na odpowiedzi przez określony czas
    gboolean no_response = TRUE;
    for (int i = 0; i < WAIT_TIME; ++i) {
        // Sprawdzanie odpowiedzi
        if (check_for_election_responses()) {
            no_response = FALSE;
            break;
        }
        sleep(1); // Czekanie na odpowiedzi
    }

    // Jeśli nie otrzymaliśmy odpowiedzi, zostajemy liderem
    if (no_response) {
        this_process.is_leader = 1;
        // Ogłoszenie siebie liderem
        announce_leadership();
    } else {
        // Oczekiwanie na ogłoszenie nowego lidera
        wait_for_leader_announcement();
    }
}

void on_start_stop_button_clicked(GtkButton *button, gpointer user_data) {
    static gboolean is_active = FALSE;

    if (!is_active) {
        // Kod do uruchomienia procesu
        gtk_button_set_label(button, "Stop Procesu");
        is_active = TRUE;

        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(GTK_BUILDER(user_data), "process_status")), "Aktywny");

        // Aktualizacja labeli z ID, adresem IP i portem
        char process_id_str[MAX_BUFFER];
        char own_ip_str[MAX_BUFFER];
        char own_port_str[MAX_BUFFER];
        sprintf(process_id_str, "Identyfikator Procesu: %d", this_process.id);
        sprintf(own_ip_str, "Własny Adres IP: %s", this_process.ip);
        sprintf(own_port_str, "Port: %d", this_process.port);
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(GTK_BUILDER(user_data), "process_id")), process_id_str);
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(GTK_BUILDER(user_data), "own_ip")), own_ip_str);
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(GTK_BUILDER(user_data), "own_port")), own_port_str);
    } else {
        // Kod do zatrzymania procesu
        // Zamknij gniazdo jeśli jest otwarte
        if (current_connection.socket_fd != 0) {
            close(current_connection.socket_fd);
            current_connection.socket_fd = 0;
        }

        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(GTK_BUILDER(user_data), "process_status")), "Nieaktywny");

        // Wyczyść etykiety z ID, adresu IP i portu
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(GTK_BUILDER(user_data), "process_id")), "");
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(GTK_BUILDER(user_data), "own_ip")), "");
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(GTK_BUILDER(user_data), "own_port")), "");
    }

    is_active = !is_active;
}

void on_connect_button_clicked(GtkButton *button, gpointer user_data) {
    GtkBuilder *builder = GTK_BUILDER(user_data);
    GtkEntry *ip_entry = GTK_ENTRY(gtk_builder_get_object(builder, "ip_entry"));
    GtkEntry *port_entry = GTK_ENTRY(gtk_builder_get_object(builder, "port_entry"));
    const char *ip = gtk_entry_get_text(ip_entry);
    const char *port_str = gtk_entry_get_text(port_entry);
    int port = atoi(port_str);

    // Sprawdzenie, czy jesteśmy już połączeni
    if (current_connection.socket_fd != 0) {
        printf("Już połączono. Najpierw zamknij obecne połączenie.\n");
        return;
    }

    // Utworzenie gniazda TCP
    current_connection.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (current_connection.socket_fd < 0) {
        perror("Nie można utworzyć gniazda");
        return;
    }

    // Ustawienie adresu serwera
    memset(&current_connection.address, 0, sizeof(current_connection.address));
    current_connection.address.sin_family = AF_INET;
    current_connection.address.sin_port = htons(port);

    // Konwersja adresu IP do formatu binarnego
    if (inet_pton(AF_INET, ip, &current_connection.address.sin_addr) <= 0) {
        perror("Nieprawidłowy adres IP");
        close(current_connection.socket_fd);
        current_connection.socket_fd = 0;
        return;
    }

    // Nawiązywanie połączenia
    if (connect(current_connection.socket_fd, (struct sockaddr *)&current_connection.address, sizeof(current_connection.address)) < 0) {
        perror("Połączenie nie powiodło się");
        close(current_connection.socket_fd);
        current_connection.socket_fd = 0;
        return;
    }

    // Zaktualizuj GUI po pomyślnym połączeniu
    gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(GTK_BUILDER(user_data), "process_status")), "Aktywny");
    gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(GTK_BUILDER(user_data), "own_ip")), ip);
    gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(GTK_BUILDER(user_data), "own_port")), port_str);

    printf("Połączono z %s:%d\n", ip, port);
}

void on_initiate_election_button_clicked(GtkButton *button, gpointer user_data) {
    Process higher_processes[MAX_PROCESSES];
    int num_higher_processes = 0;

    // Wypełnianie listy procesów o wyższym ID
    for (int i = 0; i < num_all_processes; ++i) {
        if (all_processes[i].id > this_process.id) {
            higher_processes[num_higher_processes++] = all_processes[i];
        }
    }

    // Wysyłanie wiadomości wyboru do wszystkich procesów o wyższym ID
    for (int i = 0; i < num_higher_processes; ++i) {
        send_message_to_process(&higher_processes[i], "ELECTION");
    }

    // Ustawienie początkowego stanu na brak odpowiedzi
    gboolean no_response = TRUE;

    // Oczekiwanie na odpowiedzi przez określony czas
    for (int i = 0; i < WAIT_TIME; ++i) {
        // Sprawdzanie odpowiedzi
        if (check_for_election_responses()) {
            no_response = FALSE;
            break;
        }
        sleep(1); // Czekanie na odpowiedzi
    }

    // Jeśli nie otrzymaliśmy odpowiedzi, zostajemy liderem
    if (no_response) {
        this_process.is_leader = 1;
        // Ogłoszenie siebie liderem
        announce_leadership();
    } else {
        // Oczekiwanie na ogłoszenie nowego lidera
        wait_for_leader_announcement();
    }
    gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(GTK_BUILDER(user_data), "process_status")), "W trakcie elekcji");
}

gboolean check_for_election_responses() {
    // Sprawdzenie, czy otrzymano odpowiedź od jakiegokolwiek z procesów
    if (election_response_received) {
        // Resetowanie flagi, aby była gotowa na kolejne użycie
        election_response_received = FALSE;
        return TRUE;
    }
    return FALSE;
}

void announce_leadership() {
    char message[MAX_BUFFER];
    sprintf(message, "LEADER_%d", this_process.id);

    for (int i = 0; i < num_all_processes; ++i) {
        if (all_processes[i].id != this_process.id) {
            send_message_to_process(&all_processes[i], message);
        }
    }
}

void wait_for_leader_announcement() {
    int wait_time = WAIT_TIME_FOR_LEADER;

    while (wait_time > 0) {
        // Zakładamy, że gdzieś indziej w programie mamy dostęp do tej zmiennej
        extern char current_leader_id[MAX_BUFFER];

        if (strlen(current_leader_id) > 0) {
            printf("Nowy lider ogłoszony: %s\n", current_leader_id);
            break;
        }

        sleep(1);  // Oczekiwanie na sekundę
        wait_time--;
    }

    if (wait_time == 0) {
        printf("Nie otrzymano ogłoszenia lidera.\n");
        // Można tutaj dodać dodatkowe kroki, np. ponowne rozpoczęcie elekcji
    }
}
