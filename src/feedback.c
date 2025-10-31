#include "../includes/server.h"

void give_feedback(int accountId, const char *message)
{
    int fd = open(FEEDBACK_FILE, O_RDWR | O_CREAT, 0666);
    if (fd < 0)
    {
        perror("Failed to open feedback file");
        return;
    }

    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    if (fcntl(fd, F_SETLKW, &lock) == -1)
    {
        perror("Failed to lock feedback file");
        close(fd);
        return;
    }

    Feedback fb;
    fb.accountID = accountId;
    memset(fb.message, 0, sizeof(fb.message));
    if (message)
        strncpy(fb.message, message, sizeof(fb.message) - 1);

    lseek(fd, 0, SEEK_END);
    if (write(fd, &fb, sizeof(Feedback)) != sizeof(Feedback))
    {
        perror("Failed to write feedback record");
    }

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);
}

void view_feedbacks(int sock)
{
    int fd = open(FEEDBACK_FILE, O_RDONLY);
    if (fd < 0)
    {
        write_to_client(sock, "No feedbacks found or cannot open feedback file.\n");
        return;
    }

    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_RDLCK;
    lock.l_start = 0;
    lock.l_len = 0;
    fcntl(fd, F_SETLKW, &lock);

    Feedback fb;
    char buffer[8192];
    char line[2048];
    int found = 0;
    buffer[0] = '\0';
    strcat(buffer, "\n--- Feedbacks ---\n");
    strcat(buffer, "Account | Message\n");
    strcat(buffer, "-------------------------------------------------------------\n");

    while (read(fd, &fb, sizeof(Feedback)) == sizeof(Feedback))
    {
        found = 1;
        snprintf(line, sizeof(line), "%-7d | %s\n", fb.accountID, fb.message);
        if (strlen(buffer) + strlen(line) < sizeof(buffer) - 1)
            strcat(buffer, line);
    }

    if (!found)
    {
        strcat(buffer, "No feedbacks recorded.\n");
    }

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);

    write_to_client(sock, buffer);
}
