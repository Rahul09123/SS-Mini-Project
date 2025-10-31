#include "../includes/server.h"

void view_pending_loans(int sock)
{
    int fd;
    Loan loan;
    char buffer[4096] = {0};
    char temp_line[256];
    int found_loans = 0;

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

    strcat(buffer, "\n--- Loan Status Overview ---\n");
    strcat(buffer, "ID  | Customer | Amount   | Status      | Assigned To\n");
    strcat(buffer, "-------------------------------------------------------\n");

    while (read(fd, &loan, sizeof(Loan)) == sizeof(Loan))
    {
        if (loan.status == PENDING || loan.status == ASSIGNED)
        {
            const char* status_str = 
                (loan.status == PENDING) ? "PENDING" :
                (loan.status == ASSIGNED) ? "ASSIGNED" : "UNKNOWN";
            
            sprintf(temp_line, "%-3d | %-8d | %-9.2f | %-11s | %-d\n",
                    loan.loanID,
                    loan.customerUserID,
                    loan.amount,
                    status_str,
                    loan.assignedEmployeeID);
            strcat(buffer, temp_line);
            found_loans = 1;
        }
    }
    if (!found_loans)
    {
        strcat(buffer, "No pending or assigned loans found.\n");
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
    int emp_id;
    char buffer[1024];

    write_to_client(sock, "Enter Loan ID to assign to employee: ");
    read_from_client(sock, buffer, sizeof(buffer));
    loan_id_to_assign = atoi(buffer);

    write_to_client(sock, "Enter Employee ID to assign this loan to: ");
    read_from_client(sock, buffer, sizeof(buffer));
    emp_id = atoi(buffer);

    // Verify employee exists and is actually an employee
    int user_fd = open(USER_FILE, O_RDONLY);
    if (user_fd < 0) {
        write_to_client(sock, "Error: Cannot verify employee ID.\n");
        return;
    }

    long emp_offset = find_user_offset(user_fd, emp_id);
    if (emp_offset == -1) {
        write_to_client(sock, "Error: Employee ID not found.\n");
        close(user_fd);
        return;
    }

    User emp_user;
    lseek(user_fd, emp_offset, SEEK_SET);
    read(user_fd, &emp_user, sizeof(User));
    close(user_fd);

    if (emp_user.role != EMPLOYEE) {
        write_to_client(sock, "Error: Specified ID is not an employee.\n");
        return;
    }
    if (!emp_user.is_active) {
        write_to_client(sock, "Error: This employee account is deactivated.\n");
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
            if (loan.status != PENDING) {
                write_to_client(sock, "Error: Can only assign PENDING loans.\n");
            } else {
                loan.status = ASSIGNED;
                loan.assignedEmployeeID = emp_id;
                write_to_client(sock, "Loan assigned successfully.\n");
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
    int fd;
    Loan loan;

    // First show all loans assigned to this employee
    fd = open(LOAN_FILE, O_RDONLY);
    if (fd < 0) {
        write_to_client(sock, "Error: Cannot access loan data.\n");
        return;
    }

    write_to_client(sock, "\n--- Loans Assigned to You ---\n");
    write_to_client(sock, "ID  | Customer | Amount   | Status\n");
    write_to_client(sock, "----------------------------------------\n");

    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_RDLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    fcntl(fd, F_SETLKW, &lock);

    int found = 0;
    while (read(fd, &loan, sizeof(Loan)) == sizeof(Loan)) {
        if (loan.assignedEmployeeID == emp_user.userID && loan.status == ASSIGNED) {
            sprintf(buffer, "%-3d | %-8d | %-9.2f | ASSIGNED\n",
                    loan.loanID, loan.customerUserID, loan.amount);
            write_to_client(sock, buffer);
            found = 1;
        }
    }

    if (!found) {
        write_to_client(sock, "No loans are currently assigned to you.\n\n");
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);
        close(fd);
        return;
    }

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);

    // Process a specific loan
    write_to_client(sock, "\nEnter Loan ID to process: ");
    read_from_client(sock, buffer, sizeof(buffer));
    int loan_id = atoi(buffer);

    write_to_client(sock, "Choose action (3=Approve, 4=Reject): ");
    read_from_client(sock, buffer, sizeof(buffer));
    int action = atoi(buffer);

    if (action != 3 && action != 4) {
        write_to_client(sock, "Invalid action. Must be 3 (Approve) or 4 (Reject).\n");
        return;
    }

    fd = open(LOAN_FILE, O_RDWR);
    if (fd < 0) {
        write_to_client(sock, "Error: Cannot access loan data.\n");
        return;
    }

    long offset = find_loan_offset(fd, loan_id);
    if (offset == -1) {
        write_to_client(sock, "Loan ID not found.\n");
        close(fd);
        return;
    }

    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_start = offset;
    lock.l_len = sizeof(Loan);
    fcntl(fd, F_SETLKW, &lock);

    lseek(fd, offset, SEEK_SET);
    read(fd, &loan, sizeof(Loan));

    if (loan.assignedEmployeeID != emp_user.userID) {
        write_to_client(sock, "Error: This loan is not assigned to you.\n");
    }
    else if (loan.status != ASSIGNED) {
        write_to_client(sock, "Error: This loan is not in ASSIGNED status.\n");
    }
    else {
        loan.status = (action == 3) ? APPROVED : REJECTED;
        lseek(fd, offset, SEEK_SET);
        write(fd, &loan, sizeof(Loan));

        if (action == 3) { // Approved - process loan deposit
            int acc_fd = open(ACCOUNT_FILE, O_RDWR);
            if (acc_fd < 0) {
                write_to_client(sock, "CRITICAL: Loan approved but failed to open account file!\n");
            } else {
                long acc_offset = find_account_offset(acc_fd, loan.customerUserID);
                if (acc_offset == -1) {
                    write_to_client(sock, "CRITICAL: Loan approved but customer account not found!\n");
                } else {
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
                    write_to_client(sock, "Loan approved and funds deposited to account.\n");
                }
                close(acc_fd);
            }
        } else {
            write_to_client(sock, "Loan rejected.\n");
        }
    }

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
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
