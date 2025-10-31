#include "../includes/server.h"

void log_transaction(int accountID, TransactionType type, float amount, float oldBalance, float newBalance)
{
    int fd = open(TRANSACTION_FILE, O_RDWR | O_CREAT, 0666);
    if (fd < 0)
    {
        perror("Failed to open transaction log");
        return;
    }

    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    fcntl(fd, F_SETLKW, &lock);

    long next_trans_id = get_next_transaction_id(fd);

    Transaction trans = {
        .transactionID = next_trans_id,
        .accountID = accountID,
        .type = type,
        .amount = amount,
        .oldBalance = oldBalance,
        .newBalance = newBalance,
        .timestamp = time(NULL)};

    lseek(fd, 0, SEEK_END);
    write(fd, &trans, sizeof(Transaction));

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);
}

void view_transactions(int sock, int account_no)
{
    int fd = open(TRANSACTION_FILE, O_RDONLY);
    if (fd < 0)
    {
        write_to_client(sock, "Error: Cannot open transaction history.\n");
        return;
    }

    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_RDLCK;
    lock.l_start = 0;
    lock.l_len = 0;
    fcntl(fd, F_SETLKW, &lock);

    Transaction trans;
    char buffer[8192] = {0};
    char line[512];
    int found = 0;

    sprintf(line, "\n--- Transaction History for Account %d ---\n", account_no);
    strcat(buffer, line);
    strcat(buffer, "ID    | Type         | Amount   | Old Bal  | New Bal  | Date & Time\n");
    strcat(buffer, "----------------------------------------------------------------------------------\n");

    while (read(fd, &trans, sizeof(Transaction)) == sizeof(Transaction))
    {
        if (trans.accountID == account_no)
        {
            found = 1;
            char type_str[20];
            if (trans.type == DEPOSIT)
                strcpy(type_str, "DEPOSIT");
            else if (trans.type == WITHDRAWAL)
                strcpy(type_str, "WITHDRAWAL");
            else if (trans.type == LOAN_DEPOSIT)
                strcpy(type_str, "LOAN_DEPOSIT");
            else
                strcpy(type_str, "UNKNOWN");

            char time_buf[30];
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&trans.timestamp));

            sprintf(line, "%-5ld | %-12s | %-9.2f | %-9.2f | %-9.2f | %s\n",
                    trans.transactionID,
                    type_str,
                    trans.amount,
                    trans.oldBalance,
                    trans.newBalance,
                    time_buf);
            strcat(buffer, line);
        }
    }

    if (!found)
    {
        strcat(buffer, "No transactions found for this account.\n");
    }

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);

    write_to_client(sock, buffer);
}
