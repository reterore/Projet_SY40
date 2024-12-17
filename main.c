#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>


#define MAX_CLIENTS 5
#define MAX_ACCOUNTS 3
#define NUM_WORKER_THREADS 3
#define QUEUE_SIZE 20
volatile sig_atomic_t stop_program = 0; // Drapeau pour signaler l'arrêt


// Structures pour les comptes, clients et la banque
typedef struct {
    int account_id;
    double balance;
} Account;

typedef struct {
    int client_id;
    Account accounts[MAX_ACCOUNTS];
    int num_accounts;
} Client;

typedef struct {
    int action; // 0=deposit, 1=withdraw, 2=transfer, 3=check balance
    int source_client_id;
    int source_account_id;
    int target_client_id;
    int target_account_id;
    double amount;
} Message;

typedef struct {
    Client clients[MAX_CLIENTS];
    int num_clients;
    sem_t bank_semaphore;
} Bank;

Bank bank;

// File de messages
Message message_queue[QUEUE_SIZE];
int queue_front = 0, queue_rear = 0;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t queue_sem;


void handle_sigint(int sig) {
    printf("\nSIGINT reçu. Fermeture propre en cours...\n");
    stop_program = 1; // Définit le drapeau pour arrêter les threads
}

// Initialisation de la banque
void initialize_bank() {
    bank.num_clients = 0;
    sem_init(&bank.bank_semaphore, 0, 1);
    sem_init(&queue_sem, 0, 0);
}

void add_client(int client_id) {
    sem_wait(&bank.bank_semaphore);
    if (bank.num_clients < MAX_CLIENTS) {
        Client *client = &bank.clients[bank.num_clients++];
        client->client_id = client_id;
        client->num_accounts = 0;
    }
    sem_post(&bank.bank_semaphore);
}

void add_account(int client_id, int account_id, double initial_balance) {
    sem_wait(&bank.bank_semaphore);
    for (int i = 0; i < bank.num_clients; i++) {
        if (bank.clients[i].client_id == client_id) {
            if (bank.clients[i].num_accounts < MAX_ACCOUNTS) {
                Account *account = &bank.clients[i].accounts[bank.clients[i].num_accounts++];
                account->account_id = account_id;
                account->balance = initial_balance;
            }
        }
    }
    sem_post(&bank.bank_semaphore);
}

void enqueue_message(Message msg) {
    pthread_mutex_lock(&queue_mutex);
    if ((queue_rear + 1) % QUEUE_SIZE != queue_front) {
        message_queue[queue_rear] = msg;
        queue_rear = (queue_rear + 1) % QUEUE_SIZE;
        sem_post(&queue_sem);
    }
    pthread_mutex_unlock(&queue_mutex);
}

Message dequeue_message() {
    Message msg;
    sem_wait(&queue_sem);
    pthread_mutex_lock(&queue_mutex);
    msg = message_queue[queue_front];
    queue_front = (queue_front + 1) % QUEUE_SIZE;
    pthread_mutex_unlock(&queue_mutex);
    return msg;
}

void process_message(Message msg) {
    sem_wait(&bank.bank_semaphore);
    switch (msg.action) {
        case 0: // Deposit
            for (int i = 0; i < bank.num_clients; i++) {
                if (bank.clients[i].client_id == msg.source_client_id) {
                    for (int j = 0; j < bank.clients[i].num_accounts; j++) {
                        if (bank.clients[i].accounts[j].account_id == msg.source_account_id) {
                            bank.clients[i].accounts[j].balance += msg.amount;
                            printf("Client %d: Deposited %.2f into account %d\n", msg.source_client_id, msg.amount, msg.source_account_id);
                        }
                    }
                }
            }
            break;
        case 1: // Withdraw
            for (int i = 0; i < bank.num_clients; i++) {
                if (bank.clients[i].client_id == msg.source_client_id) {
                    for (int j = 0; j < bank.clients[i].num_accounts; j++) {
                        if (bank.clients[i].accounts[j].account_id == msg.source_account_id && bank.clients[i].accounts[j].balance >= msg.amount) {
                            bank.clients[i].accounts[j].balance -= msg.amount;
                            printf("Client %d: Withdrew %.2f from account %d\n", msg.source_client_id, msg.amount, msg.source_account_id);
                        }
                    }
                }
            }
            break;
        default:
            printf("Unknown action %d\n", msg.action);
    }
    sem_post(&bank.bank_semaphore);
}

void *worker_thread(void *arg) {
    while (!stop_program) {
        Message msg = dequeue_message();
        process_message(msg);
        sleep(1); // Simule un traitement
    }
    return NULL;
}

unsigned int thread_safe_rand(unsigned int *seed) {
    *seed = (*seed * 1103515245 + 12345) % 0x7fffffff;
    return *seed;
}

void *client_thread(void *arg) {
    int client_id = *((int *)arg);

    // Initialiser une graine spécifique pour chaque thread
    unsigned int thread_seed = time(NULL) + client_id;

    for (int i = 0; i < 5 && !stop_program; i++) {
        Message msg;
        msg.action = thread_safe_rand(&thread_seed) % 2; // 0 = deposit, 1 = withdraw
        msg.source_client_id = client_id;
        msg.source_account_id = (thread_safe_rand(&thread_seed) % MAX_ACCOUNTS) + 1;
        msg.amount = (thread_safe_rand(&thread_seed) % 1000) + 1; // Montant entre 1 et 1000
        msg.target_client_id = -1; // Non utilisé dans ce cas
        msg.target_account_id = -1;

        enqueue_message(msg);
        sleep(1);
    }
    return NULL;
}


int main() {
    srand(time(NULL));
    initialize_bank();
    struct sigaction sa;
    sa.sa_handler = handle_sigint;  // Associer la fonction handle_sigint au signal SIGINT
    sigemptyset(&sa.sa_mask);       // Ne bloque aucun autre signal pendant SIGINT
    sa.sa_flags = 0;                // Pas de flag spécial
    sigaction(SIGINT, &sa, NULL);   // Appliquer la configuratio

    if (sigaction(SIGINT, &sa, NULL) == -1) {  // Appliquer la configuration
        perror("Erreur sigaction");
        exit(EXIT_FAILURE);
    }
    printf("Appuyez sur Ctrl+C pour interrompre le programme.\n");

    // Ajouter des clients et comptes
    for (int i = 1; i <= MAX_CLIENTS; i++) {
        add_client(i);
        for (int j = 1; j <= MAX_ACCOUNTS; j++) {
            add_account(i, j, 1000 * j);
        }
    }

    pthread_t workers[NUM_WORKER_THREADS];
    pthread_t clients[MAX_CLIENTS];
    int client_ids[MAX_CLIENTS];

    // Créer les threads workers
    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        pthread_create(&workers[i], NULL, worker_thread, NULL);
    }

    // Créer les threads clients
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_ids[i] = i + 1;
        pthread_create(&clients[i], NULL, client_thread, &client_ids[i]);
    }

    // Attendre la fin des threads clients
    for (int i = 0; i < MAX_CLIENTS; i++) {
        pthread_join(clients[i], NULL);
    }

    // Les workers tournent indéfiniment (simule un serveur en écoute)
    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        pthread_cancel(workers[i]);
    }

    // Arrêter les threads workers
    stop_program = 1; // Signaler aux threads de s'arrêter
    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        sem_post(&queue_sem); // Débloquer les workers bloqués
        pthread_join(workers[i], NULL);
    }
    return 0;
}