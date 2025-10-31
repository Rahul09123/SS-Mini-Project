#include "../includes/server.h"

void view_pending_loans(int sock)
{
    int fd;
    Loan loan;
    char buffer[4096] = {0};
    char temp_line[256];
    int found_pending = 0;

    fd = open(LOAN_FILE, O_RDONLY);
    if (fd == -1)
    {
        perror("Error opening loan file");
        write_to_client(sock, "Error: Could not access loan data.\n");
        return;
    }

    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_RDLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    fcntl(fd, F_SETLKW, &lock);

    strcat(buffer, "\n--- Pending Loans ---\n");
    strcat(buffer, "ID  | Customer | Amount   | Status      | Assigned To\n");
    strcat(buffer, "-------------------------------------------------------\n");

    while (read(fd, &loan, sizeof(Loan)) == sizeof(Loan))
    {
        if (loan.status == PENDING)
        {
            sprintf(temp_line, "%-3d | %-8d | %-9.2f | %-11s | %-d\n",
                    loan.loanID,
                    loan.customerUserID,
                    loan.amount,
                    "PENDING",
                    loan.assignedEmployeeID);
            strcat(buffer, temp_line);
            found_pending = 1;
        }
    }
    if (!found_pending)
    {
        strcat(buffer, "No pending loans found.\n");
    }

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);

    write_to_client(sock, buffer);
}

void assign_loan(int sock)
{
    int fd;
    Loan loan;
    int loan_id_to_assign;
    int new_status_choice;
    LoanStatus new_status;
    char buffer[1024];

    write_to_client(sock, "Enter Loan ID to assign (Approve/Reject): ");
    read_from_client(sock, buffer, sizeof(buffer));
    loan_id_to_assign = atoi(buffer);

    write_to_client(sock, "Enter new status (3=Approve, 4=Reject): ");
    read_from_client(sock, buffer, sizeof(buffer));
    new_status_choice = atoi(buffer);

    if (new_status_choice == 3)
    {
        new_status = APPROVED;
    }
    else if (new_status_choice == 4)
    {
        new_status = REJECTED;
    }
    else
    {
        write_to_client(sock, "Invalid choice. Aborting.\n");
        return;
    }

    fd = open(LOAN_FILE, O_RDWR);
    if (fd == -1)
    {
        perror("Error opening loan file");
        write_to_client(sock, "Error: Could not access loan data.\n");
        return;
    }

    long offset = find_loan_offset(fd, loan_id_to_assign);

    if (offset != -1)
    {
        struct flock lock;
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = offset;
        lock.l_len = sizeof(Loan);

        fcntl(fd, F_SETLKW, &lock);

        lseek(fd, offset, SEEK_SET);
        read(fd, &loan, sizeof(Loan));

        if (loan.status == PENDING || loan.status == PROCESSING)
        {
            loan.status = new_status;

            if (new_status == APPROVED)
            {
                int acc_fd = open(ACCOUNT_FILE, O_RDWR);
                if (acc_fd < 0)
                {
                    write_to_client(sock, "CRITICAL: Loan approved but failed to open account file!\n");
                }
                else
                {
                    long acc_offset = find_account_offset(acc_fd, loan.customerUserID);
                    if (acc_offset == -1)
                    {
                        write_to_client(sock, "CRITICAL: Loan approved but customer account not found!\n");
                    }
                    else
                    {
                        struct flock acc_lock;
                        memset(&acc_lock, 0, sizeof(acc_lock));
                        acc_lock.l_type = F_WRLCK;
                        acc_lock.l_start = acc_offset;
                        acc_lock.l_len = sizeof(Account);
                        fcntl(acc_fd, F_SETLKW, &acc_lock);

                        Account acc;
                        lseek(acc_fd, acc_offset, SEEK_SET);
                        read(acc_fd, &acc, sizeof(Account));

                        float old_bal = acc.balance;
                        acc.balance += loan.amount;

                        lseek(acc_fd, acc_offset, SEEK_SET);
                        write(acc_fd, &acc, sizeof(Account));

                        log_transaction(acc.account_no, LOAN_DEPOSIT, loan.amount, old_bal, acc.balance);

                        acc_lock.l_type = F_UNLCK;
                        fcntl(acc_fd, F_SETLK, &acc_lock);
                        write_to_client(sock, "Loan funds deposited to account.\n");
                    }
                    close(acc_fd);
                }
            }

            lseek(fd, offset, SEEK_SET);
            write(fd, &loan, sizeof(Loan));

            write_to_client(sock, "Loan status updated successfully.\n");
        }
        else
        {
            write_to_client(sock, "Error: This loan is not pending or processing.\n");
        }

        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);
    }
    else
    {
        write_to_client(sock, "Error: Loan ID not found.\n");
    }

    close(fd);
}

void employee_process_loan(int sock, User emp_user)
{
    char buffer[1024];

    view_pending_loans(sock);

    write_to_client(sock, "Enter Loan ID to process/claim: ");
    read_from_client(sock, buffer, sizeof(buffer));
    int loan_id = atoi(buffer);

    int fd = open(LOAN_FILE, O_RDWR);
    if (fd < 0)
    {
        write_to_client(sock, "Server error: Cannot open loan file.\n");
        return;
    }

    long offset = find_loan_offset(fd, loan_id);
    if (offset == -1)
    {
        write_to_client(sock, "Loan ID not found.\n");
    }
    else
    {
        struct flock lock;
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK;
        lock.l_start = offset;
        lock.l_len = sizeof(Loan);
        fcntl(fd, F_SETLKW, &lock);

        Loan loan;
        lseek(fd, offset, SEEK_SET);
        read(fd, &loan, sizeof(Loan));

        if (loan.status == PENDING)
        {
            loan.status = PROCESSING;
            loan.assignedEmployeeID = emp_user.userID;
            lseek(fd, offset, SEEK_SET);
            write(fd, &loan, sizeof(Loan));
            sprintf(buffer, "Loan %d assigned to you for processing.\n", loan.loanID);
            write_to_client(sock, buffer);
        }
        else
        {
            write_to_client(sock, "This loan is not pending. It may already be processing or completed.\n");
        }

        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);
    }
    close(fd);
}

void customer_apply_loan(int sock, User user)
{
    char buffer[1024];
    write_to_client(sock, "Enter loan amount: ");
    read_from_client(sock, buffer, sizeof(buffer));
    float amount = atof(buffer);

    if (amount <= 0)
    {
        write_to_client(sock, "Invalid amount.\n");
        return;
    }

    int fd = open(LOAN_FILE, O_RDWR | O_CREAT, 0666);
    if (fd < 0)
    {
        write_to_client(sock, "Server error: Cannot open loan file.\n");
        return;
    }

    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_start = 0;
    lock.l_len = 0;
    fcntl(fd, F_SETLKW, &lock);

    Loan loan;
    loan.loanID = get_next_loan_id(fd);
    loan.customerUserID = user.userID;
    loan.amount = amount;
    loan.status = PENDING;
    loan.assignedEmployeeID = -1;

    lseek(fd, 0, SEEK_END);
    write(fd, &loan, sizeof(Loan));

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);

    sprintf(buffer, "Loan application for $%.2f submitted. Loan ID: %d\n", amount, loan.loanID);
    write_to_client(sock, buffer);
}
