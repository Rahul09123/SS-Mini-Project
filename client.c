#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h> 

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    
    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    
    // Connect to localhost
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/Address not supported");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return -1;
    }

    printf("Connected to Bank Server\n");

    
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        
        // Read up to (BUFFER_SIZE - 1) bytes
        int bytes = read(sock, buffer, sizeof(buffer) - 1);
        
        if (bytes <= 0) {
            // Server disconnected
            break; 
        }
        
        // Ensure the received data is null-terminated
        buffer[bytes] = '\0'; 
        printf("%s", buffer); 

        // Check if the server's message indicates a disconnect (e.g., login failure)
        if (strstr(buffer, "Invalid login") || strstr(buffer, "deactivated")) {
            break;
        }

        // Get user input from stdin
        char input[BUFFER_SIZE];
        memset(input, 0, sizeof(input));
        if (fgets(input, sizeof(input), stdin) == NULL) {
            // User pressed Ctrl+D
            break; 
        }

        // Remove trailing newline character from fgets
        input[strcspn(input, "\n")] = 0;

        
        write(sock, input, strlen(input));
        
    }

    printf("\nDisconnected from server.\n");
    close(sock);
    return 0;
}
