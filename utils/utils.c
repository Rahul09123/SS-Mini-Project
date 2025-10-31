#include "../includes/server.h"

// read the data from client
int read_from_client(int sock, char *buffer, int size)
{
    memset(buffer, 0, size);
    int bytes_read = read(sock, buffer, size - 1);
    if (bytes_read > 0)
        buffer[strcspn(buffer, "\r\n")] = 0;
    return bytes_read;
}

// write the data to client
void write_to_client(int sock, const char *message)
{
    write(sock, message, strlen(message));
}
