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

int transfer_funds(int sock, int from_account, int to_account, float amount)
{
    if (amount <= 0)
    {
        write_to_client(sock, "Invalid transfer amount.\n");
        return -1;
    }

    int fd = open(ACCOUNT_FILE, O_RDWR);
    if (fd < 0)
    {
        write_to_client(sock, "Error: Cannot access account data.\n");
        return -1;
    }

    // Find both accounts
    long from_offset = find_account_offset(fd, from_account);
    long to_offset = find_account_offset(fd, to_account);

    if (from_offset == -1 || to_offset == -1)
    {
        write_to_client(sock, "Error: One or both accounts not found.\n");
        close(fd);
        return -1;
    }

    // Lock both accounts (in order of offset to prevent deadlocks)
    struct flock lock1, lock2;
    memset(&lock1, 0, sizeof(lock1));
    memset(&lock2, 0, sizeof(lock2));

    lock1.l_type = F_WRLCK;
    lock1.l_whence = SEEK_SET;
    lock1.l_start = (from_offset < to_offset) ? from_offset : to_offset;
    lock1.l_len = sizeof(Account);

    lock2.l_type = F_WRLCK;
    lock2.l_whence = SEEK_SET;
    lock2.l_start = (from_offset < to_offset) ? to_offset : from_offset;
    lock2.l_len = sizeof(Account);

    // Acquire locks
    if (fcntl(fd, F_SETLKW, &lock1) == -1 || fcntl(fd, F_SETLKW, &lock2) == -1)
    {
        write_to_client(sock, "Error: Cannot lock accounts for transfer.\n");
        close(fd);
        return -1;
    }

    // Read accounts
    Account from_acc, to_acc;
    lseek(fd, from_offset, SEEK_SET);
    read(fd, &from_acc, sizeof(Account));
    lseek(fd, to_offset, SEEK_SET);
    read(fd, &to_acc, sizeof(Account));

    // Check account status
    if (!from_acc.is_active || !to_acc.is_active)
    {
        write_to_client(sock, "Error: One or both accounts are deactivated.\n");
        lock1.l_type = F_UNLCK;
        lock2.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock1);
        fcntl(fd, F_SETLK, &lock2);
        close(fd);
        return -1;
    }

    // Check sufficient balance
    if (from_acc.balance < amount)
    {
        write_to_client(sock, "Error: Insufficient balance for transfer.\n");
        lock1.l_type = F_UNLCK;
        lock2.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock1);
        fcntl(fd, F_SETLK, &lock2);
        close(fd);
        return -1;
    }

    // Perform transfer
    float from_old_bal = from_acc.balance;
    float to_old_bal = to_acc.balance;

    from_acc.balance -= amount;
    to_acc.balance += amount;

    // Write back updated accounts
    lseek(fd, from_offset, SEEK_SET);
    write(fd, &from_acc, sizeof(Account));
    lseek(fd, to_offset, SEEK_SET);
    write(fd, &to_acc, sizeof(Account));

    // Log transactions for both accounts
    log_transaction(from_acc.account_no, TRANSFER_SENT, amount, from_old_bal, from_acc.balance);
    log_transaction(to_acc.account_no, TRANSFER_RECEIVED, amount, to_old_bal, to_acc.balance);

    // Release locks
    lock1.l_type = F_UNLCK;
    lock2.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock1);
    fcntl(fd, F_SETLK, &lock2);
    close(fd);

    char buffer[1024];
    sprintf(buffer, "Successfully transferred %.2f from account %d to account %d.\n", 
            amount, from_account, to_account);
    write_to_client(sock, buffer);
    return 0;
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
