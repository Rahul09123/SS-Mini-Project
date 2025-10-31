// Minimal server main and handler that use modularized implementation files.
#include <pthread.h>
#include "includes/server.h"

// Globals for login management
pthread_spinlock_t login_lock;
int logged_in_users[MAX_CLIENTS];

void *handle_client(void *sock_ptr)
{
    int new_socket = *((int *)sock_ptr);
    free(sock_ptr);

    char buffer[1024], pass[64];
    int user_id;
    int logged_in_slot = -1;

    int user_fd = open(USER_FILE, O_RDONLY);
    if (user_fd < 0)
    {
        write_to_client(new_socket, "Server error: Cannot open user file.\n");
        close(new_socket);
        pthread_exit(NULL);
    }

    write_to_client(new_socket, "Welcome to Bank\n");
    write_to_client(new_socket, "Enter UserID: ");
    if (read_from_client(new_socket, buffer, sizeof(buffer)) <= 0)
    {
        close(user_fd);
        close(new_socket);
        pthread_exit(NULL);
    }
    user_id = atoi(buffer);

    write_to_client(new_socket, "Enter password: ");
    if (read_from_client(new_socket, pass, sizeof(pass)) <= 0)
    {
        close(user_fd);
        close(new_socket);
        pthread_exit(NULL);
    }

    long user_offset = find_user_offset(user_fd, user_id);
    if (user_offset == -1)
    {
        write_to_client(new_socket, "Invalid login: User not found.\n");
        close(user_fd);
        close(new_socket);
        pthread_exit(NULL);
    }

    User user;
    lseek(user_fd, user_offset, SEEK_SET);
    read(user_fd, &user, sizeof(User));

    if (strcmp(user.password, pass) != 0)
    {
        write_to_client(new_socket, "Invalid login: Incorrect password.\n");
        close(user_fd);
        close(new_socket);
        pthread_exit(NULL);
    }

    if (!user.is_active)
    {
        write_to_client(new_socket, "Login failed: User login is deactivated.\n");
        close(user_fd);
        close(new_socket);
        pthread_exit(NULL);
    }

    // Login slot management
    pthread_spin_lock(&login_lock);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (logged_in_users[i] == user.userID)
        {
            pthread_spin_unlock(&login_lock);
            write_to_client(new_socket, "Login failed: This user is already logged in elsewhere.\n");
            close(user_fd);
            close(new_socket);
            pthread_exit(NULL);
        }
    }
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (logged_in_users[i] == -1)
        {
            logged_in_users[i] = user.userID;
            logged_in_slot = i;
            break;
        }
    }
    pthread_spin_unlock(&login_lock);

    if (logged_in_slot == -1)
    {
        write_to_client(new_socket, "Login failed: Server is full. Please try again later.\n");
        close(user_fd);
        close(new_socket);
        pthread_exit(NULL);
    }

    write_to_client(new_socket, "Login successful!\n");

    if (user.role == ADMIN)
        admin_menu(new_socket, user);
    else if (user.role == MANAGER)
        manager_menu(new_socket, user);
    else if (user.role == EMPLOYEE)
        employee_menu(new_socket, user);
    else if (user.role == CUSTOMER)
    {
        int acc_fd = open(ACCOUNT_FILE, O_RDONLY);
        if (acc_fd < 0)
        {
            write_to_client(new_socket, "Error: Could not open bank account file.\n");
        }
        else
        {
            long acc_offset = find_account_offset(acc_fd, user.userID);
            if (acc_offset == -1)
            {
                write_to_client(new_socket, "Error: You are a customer but have no bank account.\n");
            }
            else
            {
                Account account;
                lseek(acc_fd, acc_offset, SEEK_SET);
                read(acc_fd, &account, sizeof(Account));
                customer_menu(new_socket, user, account);
            }
            close(acc_fd);
        }
    }

    // Logout
    pthread_spin_lock(&login_lock);
    if (logged_in_users[logged_in_slot] == user.userID)
        logged_in_users[logged_in_slot] = -1;
    pthread_spin_unlock(&login_lock);

    close(user_fd);
    close(new_socket);
    pthread_exit(NULL);
}

int main()
{
    int server_fd, new_socket;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    initialize_admin();

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    pthread_spin_init(&login_lock, 0);
    for (int i = 0; i < MAX_CLIENTS; i++)
        logged_in_users[i] = -1;

    printf("Bank Server started. Waiting for clients on port %d...\n", PORT);

    while (1)
    {
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0)
        {
            perror("accept");
            continue;
        }

        int *sock_ptr = malloc(sizeof(int));
        *sock_ptr = new_socket;
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, sock_ptr) != 0)
        {
            perror("pthread_create failed");
            free(sock_ptr);
            close(new_socket);
            continue;
        }
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}