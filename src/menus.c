#include "../includes/server.h"

// Reusable Functions
int reusable_add_user(int sock, Role adder_role)
{
    char buffer[1024];
    int fd = open(USER_FILE, O_RDWR);
    if (fd < 0)
    {
        write_to_client(sock, "Server error: Cannot open user file.\n");
        return -1;
    }

    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_start = 0;
    lock.l_len = 0;
    fcntl(fd, F_SETLKW, &lock); // Lock user file

    User user;
    user.userID = get_next_user_id(fd);

    write_to_client(sock, "Enter name for new user: ");
    read_from_client(sock, user.name, sizeof(user.name));

    write_to_client(sock, "Enter password for new user: ");
    read_from_client(sock, user.password, sizeof(user.password));

    if (adder_role == ADMIN)
    {
        write_to_client(sock, "Enter role (1=Cust, 3=Emp, 4=Mgr): ");
        read_from_client(sock, buffer, sizeof(buffer));
        user.role = (Role)atoi(buffer);
    }
    else
    { // Employee adding a customer
        user.role = CUSTOMER;
    }
    user.is_active = 1; // Active by default

    lseek(fd, 0, SEEK_END);
    write(fd, &user, sizeof(User));

    sprintf(buffer, "User %d (%s) added successfully!\n", user.userID, user.name);
    write_to_client(sock, buffer);

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);
    return user.userID;
}

void reusable_add_bank_account(int sock, int new_account_no)
{
    char buffer[1024];
    int fd = open(ACCOUNT_FILE, O_RDWR | O_CREAT, 0666);
    if (fd < 0)
    {
        write_to_client(sock, "Server error: Cannot open account file.\n");
        return;
    }

    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_start = 0;
    lock.l_len = 0;
    fcntl(fd, F_SETLKW, &lock); // Lock account file

    if (find_account_offset(fd, new_account_no) != -1)
    {
        write_to_client(sock, "Error: Bank account for this user already exists.\n");
    }
    else
    {
        Account acc;
        acc.account_no = new_account_no; // Link to UserID

        write_to_client(sock, "Enter initial balance for new account: ");
        read_from_client(sock, buffer, sizeof(buffer));
        acc.balance = atof(buffer);

        acc.is_active = 1; // Active by default

        lseek(fd, 0, SEEK_END);
        write(fd, &acc, sizeof(Account));

        sprintf(buffer, "Bank account %d created successfully!\n", acc.account_no);
        write_to_client(sock, buffer);
    }

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);
}

void reusable_modify_user(int sock, Role modifier_role, int target_userID)
{
    char buffer[1024];
    int user_to_modify;

    if (target_userID == -1)
    { // Admin / Employee modifying Customer
        write_to_client(sock, "Enter UserID to modify: ");
        read_from_client(sock, buffer, sizeof(buffer));
        user_to_modify = atoi(buffer);
    }
    else
    { // User modifying their own password
        user_to_modify = target_userID;
    }
    int fd = open(USER_FILE, O_RDWR);
    if (fd < 0)
    {
        write_to_client(sock, "Server error: Cannot open user file.\n");
        return;
    }
    long offset = find_user_offset(fd, user_to_modify);
    if (offset == -1)
    {
        write_to_client(sock, "User not found.\n");
    }
    else
    {
        struct flock lock;
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK;
        lock.l_start = offset;
        lock.l_len = sizeof(User);
        fcntl(fd, F_SETLKW, &lock);

        User user;
        lseek(fd, offset, SEEK_SET);
        read(fd, &user, sizeof(User));

        if (modifier_role == EMPLOYEE && user.role != CUSTOMER)
        {
            write_to_client(sock, "Permission denied. Can only modify customer users.\n");
        }
        else
        {
            write_to_client(sock, "Enter new password (leave blank to keep): ");
            read_from_client(sock, buffer, sizeof(buffer));
            if (strlen(buffer) > 0)
                strcpy(user.password, buffer);

            if (target_userID == -1)
            { // Only admin/emp can change name
                write_to_client(sock, "Enter new name (leave blank to keep): ");
                read_from_client(sock, buffer, sizeof(buffer));
                if (strlen(buffer) > 0)
                    strcpy(user.name, buffer);
            }
            lseek(fd, offset, SEEK_SET);
            write(fd, &user, sizeof(User));
            write_to_client(sock, "User updated.\n");
        }
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);
    }
    close(fd);
}

void reusable_activate_deactivate_user(int sock, int choice)
{
    char buffer[1024];
    write_to_client(sock, "Enter UserID to modify: ");
    read_from_client(sock, buffer, sizeof(buffer));
    int user_id = atoi(buffer);

    int fd = open(USER_FILE, O_RDWR);
    if (fd < 0)
    {
        write_to_client(sock, "Server error.\n");
        return;
    }

    long offset = find_user_offset(fd, user_id);
    if (offset == -1)
    {
        write_to_client(sock, "User not found.\n");
    }
    else
    {
        struct flock lock;
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK;
        lock.l_start = offset;
        lock.l_len = sizeof(User);
        fcntl(fd, F_SETLKW, &lock);

        User user;
        lseek(fd, offset, SEEK_SET);
        read(fd, &user, sizeof(User));
        user.is_active = (choice == 2) ? 0 : 1;
        lseek(fd, offset, SEEK_SET);
        write(fd, &user, sizeof(User));

        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);
        write_to_client(sock, (choice == 2) ? "User login deactivated.\n" : "User login activated.\n");
    }
    close(fd);
}

void reusable_activate_deactivate_account(int sock, int choice)
{
    char buffer[1024];
    write_to_client(sock, "Enter Customer Account Number to modify: ");
    read_from_client(sock, buffer, sizeof(buffer));
    int acc_no = atoi(buffer);

    int fd = open(ACCOUNT_FILE, O_RDWR);
    if (fd < 0)
    {
        write_to_client(sock, "Server error.\n");
        return;
    }

    long offset = find_account_offset(fd, acc_no);
    if (offset == -1)
    {
        write_to_client(sock, "Bank account not found.\n");
    }
    else
    {
        struct flock lock;
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK;
        lock.l_start = offset;
        lock.l_len = sizeof(Account);
        fcntl(fd, F_SETLKW, &lock);

        Account acc;
        lseek(fd, offset, SEEK_SET);
        read(fd, &acc, sizeof(Account));
        acc.is_active = (choice == 2) ? 0 : 1;
        lseek(fd, offset, SEEK_SET);
        write(fd, &acc, sizeof(Account));

        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);
        write_to_client(sock, (choice == 2) ? "Bank account deactivated.\n" : "Bank account activated.\n");
    }
    close(fd);
}

// Menus
void admin_menu(int sock, User admin_user)
{
    char buffer[1024];
    while (1)
    {
        write_to_client(sock, "\n--- Admin Menu ---\n1. Add User\n2. Deactivate User\n3. Activate User\n4. Modify User\n5. Search User\n6. Add Bank Account for Customer\n7. View Feedbacks\n8. Exit\nChoice: ");
        int choice;
        if (read_from_client(sock, buffer, sizeof(buffer)) <= 0)
            choice = 8; // Force exit on disconnect
        else
            choice = atoi(buffer);
        if (choice == 8)
            break;

        if (choice == 1)
        {
            int new_user_id = reusable_add_user(sock, ADMIN);
            if (new_user_id == -1)
            {
                continue;
            }

            int fd = open(USER_FILE, O_RDONLY);
            if (fd < 0)
            {
                write_to_client(sock, "Server error: Cannot verify new user role.\n");
                continue;
            }

            long offset = find_user_offset(fd, new_user_id);
            if (offset == -1)
            {
                write_to_client(sock, "Server error: Cannot find new user.\n");
                close(fd);
                continue;
            }

            User new_user;
            lseek(fd, offset, SEEK_SET);
            read(fd, &new_user, sizeof(User));
            close(fd);

            if (new_user.role == CUSTOMER)
            {
                write_to_client(sock, "New user is a Customer. Proceeding to create bank account...\n");
                reusable_add_bank_account(sock, new_user_id);
            }
        }
        else if (choice == 2)
        {
            reusable_activate_deactivate_user(sock, 2);
        }
        else if (choice == 3)
        {
            reusable_activate_deactivate_user(sock, 3);
        }
        else if (choice == 4)
        {
            reusable_modify_user(sock, ADMIN, -1);
        }
        else if (choice == 5)
        {
            write_to_client(sock, "Enter UserID to search: ");
            read_from_client(sock, buffer, sizeof(buffer));
            int user_id = atoi(buffer);

            int fd = open(USER_FILE, O_RDONLY);
            if (fd < 0)
            {
                write_to_client(sock, "Server error: Cannot open user file.\n");
                continue;
            }

            long offset = find_user_offset(fd, user_id);
            if (offset == -1)
            {
                write_to_client(sock, "User not found.\n");
            }
            else
            {
                User user;
                lseek(fd, offset, SEEK_SET);
                read(fd, &user, sizeof(User));
                sprintf(buffer, "UserID: %d\nName: %s\nRole: %d\nActive: %d\n\n",
                        user.userID, user.name, user.role, user.is_active);
                write_to_client(sock, buffer);
            }
            close(fd);
        }
        else if (choice == 6)
        {
            write_to_client(sock, "Enter Customer UserID to create account for: ");
            read_from_client(sock, buffer, sizeof(buffer));
            int user_id = atoi(buffer);
            reusable_add_bank_account(sock, user_id);
        }
        else if (choice == 7)
        {
            view_feedbacks(sock);
        }
        else
        {
            write_to_client(sock, "Invalid choice.\n");
        }
    }
}

void manager_menu(int sock, User mgr_user)
{
    char buffer[1024];
    while (1)
    {
        write_to_client(sock, "\n--- Manager Menu ---\n1. Activate Customer Account\n2. Deactivate Customer Account\n3. View Pending Loans\n4. Assign (Approve/Reject) Loan\n5. View Feedbacks\n6. Exit\nChoice: ");
        int choice;
        if (read_from_client(sock, buffer, sizeof(buffer)) <= 0)
            choice = 6; // Force exit on disconnect
        else
            choice = atoi(buffer);
        if (choice == 6)
            break;

        if (choice == 1)
        {
            reusable_activate_deactivate_account(sock, 3);
        }
        else if (choice == 2)
        {
            reusable_activate_deactivate_account(sock, 2);
        }
        else if (choice == 3)
        {
            view_pending_loans(sock);
        }
        else if (choice == 4)
        {
            assign_loan(sock);
        }
        else if (choice == 5)
        {
            view_feedbacks(sock);
        }
        else
        {
            write_to_client(sock, "Invalid choice.\n");
        }
    }
}

void employee_menu(int sock, User emp_user)
{
    char buffer[1024];
    while (1)
    {
        write_to_client(sock, "\n--- Employee Menu ---\n1. Add New Customer\n2. Modify Customer Details\n3. Process (Claim) Loan\n4. View Pending Loans\n5. View Customer Transactions\n6. View Feedbacks\n7. Exit\nChoice: ");
        int choice;
        if (read_from_client(sock, buffer, sizeof(buffer)) <= 0)
            choice = 7; // Force exit on disconnect
        else
            choice = atoi(buffer);
        if (choice == 7)
            break;

        if (choice == 1)
        {
            int new_user_id = reusable_add_user(sock, EMPLOYEE);
            if (new_user_id != -1)
            {
                write_to_client(sock, "Now, creating bank account for new user...\n");
                reusable_add_bank_account(sock, new_user_id);
            }
        }
        else if (choice == 2)
        {
            reusable_modify_user(sock, EMPLOYEE, -1);
        }
        else if (choice == 3)
        {
            employee_process_loan(sock, emp_user);
        }
        else if (choice == 4)
        {
            view_pending_loans(sock);
        }
        else if (choice == 5)
        {
            write_to_client(sock, "Enter Customer Account Number: ");
            read_from_client(sock, buffer, sizeof(buffer));
            int acc_no = atoi(buffer);
            view_transactions(sock, acc_no);
        }
        else if (choice == 6)
        {
            view_feedbacks(sock);
        }
        else
        {
            write_to_client(sock, "Invalid choice.\n");
        }
    }
}

void customer_menu(int sock, User user, Account account)
{
    char buffer[1024];
    int choice;
    while (1)
    {
        int fd_acc_check = open(ACCOUNT_FILE, O_RDONLY);
        if (fd_acc_check > 0)
        {
            long acc_off = find_account_offset(fd_acc_check, account.account_no);
            if (acc_off != -1)
            {
                lseek(fd_acc_check, acc_off, SEEK_SET);
                read(fd_acc_check, &account, sizeof(Account));
            }
            close(fd_acc_check);
        }

        sprintf(buffer, "\n--- Customer Menu (User: %s, Account: %d) ---\n"
                        "1. Deposit\n"
                        "2. Withdraw\n"
                        "3. Balance Enquiry\n"
                        "4. Password Change\n"
                        "5. View Account Details\n"
                        "6. Apply for Loan\n"
                        "7. View My Transactions\n"
                        "8. Give Feedback\n"
                        "9. Transfer Funds\n"
                        "10. Exit\n"
                        "Choice: ",
                user.name, account.account_no);
        write_to_client(sock, buffer);

        if (read_from_client(sock, buffer, sizeof(buffer)) <= 0)
        {
            choice = 9;
        }
        else
        {
            choice = atoi(buffer);
            if (choice == 10)
                break;

            if (!account.is_active && choice != 4 && choice != 10 && choice != 8)
            {
                write_to_client(sock, "Your bank account is deactivated. Please contact a manager.\n");
                continue;
            }

            if (choice == 1 || choice == 2 || choice == 3 || choice == 5)
            {
                int fd = open(ACCOUNT_FILE, O_RDWR);
                if (fd < 0)
                {
                    write_to_client(sock, "Error accessing account data.\n");
                    continue;
                }

                long offset = find_account_offset(fd, account.account_no);
                if (offset == -1)
                {
                    write_to_client(sock, "CRITICAL ERROR: Account not found.\n");
                    close(fd);
                    break;
                }

                struct flock lock;
                memset(&lock, 0, sizeof(lock));
                lock.l_type = (choice == 1 || choice == 2) ? F_WRLCK : F_RDLCK;
                lock.l_whence = SEEK_SET;
                lock.l_start = offset;
                lock.l_len = sizeof(Account);
                fcntl(fd, F_SETLKW, &lock);

                lseek(fd, offset, SEEK_SET);
                read(fd, &account, sizeof(Account));

                if (choice == 1)
                {
                    write_to_client(sock, "Enter amount to deposit: ");
                    read_from_client(sock, buffer, sizeof(buffer));
                    float amt = atof(buffer);
                    if (amt <= 0)
                    {
                        write_to_client(sock, "Invalid amount.\n");
                    }
                    else
                    {
                        float old_bal = account.balance;
                        account.balance += amt;
                        lseek(fd, offset, SEEK_SET);
                        write(fd, &account, sizeof(Account));
                        log_transaction(account.account_no, DEPOSIT, amt, old_bal, account.balance);
                        write_to_client(sock, "Deposit successful.\n");
                    }
                }
                else if (choice == 2)
                {
                    write_to_client(sock, "Enter amount to withdraw: ");
                    read_from_client(sock, buffer, sizeof(buffer));
                    float amt = atof(buffer);
                    if (amt <= 0)
                    {
                        write_to_client(sock, "Invalid amount.\n");
                    }
                    else if (account.balance >= amt)
                    {
                        float old_bal = account.balance;
                        account.balance -= amt;
                        lseek(fd, offset, SEEK_SET);
                        write(fd, &account, sizeof(Account));
                        log_transaction(account.account_no, WITHDRAWAL, amt, old_bal, account.balance);
                        write_to_client(sock, "Withdrawal successful.\n");
                    }
                    else
                    {
                        write_to_client(sock, "Insufficient balance.\n");
                    }
                }
                else if (choice == 3)
                {
                    sprintf(buffer, "Your current balance: %.2f\n", account.balance);
                    write_to_client(sock, buffer);
                }
                else if (choice == 5)
                {
                    sprintf(buffer, "Account No: %d\nBalance: %.2f\nJoint: %s\nActive: %s\n",
                            account.account_no,
                            account.balance,
                            account.is_active ? "Yes" : "No",
                            account.is_active ? "Yes" : "No");
                    write_to_client(sock, buffer);
                }
                lock.l_type = F_UNLCK;
                fcntl(fd, F_SETLK, &lock);
                close(fd);
            }
            else if (choice == 4)
            {
                reusable_modify_user(sock, CUSTOMER, user.userID);
            }
            else if (choice == 6)
            {
                customer_apply_loan(sock, user);
            }
            else if (choice == 7)
            {
                view_transactions(sock, account.account_no);
            }
            else if (choice == 8)
            {
                write_to_client(sock, "Enter feedback message (single line): ");
                read_from_client(sock, buffer, sizeof(buffer));
                give_feedback(account.account_no, buffer);
                write_to_client(sock, "Thank you for your feedback.\n");
            }
            else if (choice == 9)
            {
                // Transfer funds
                write_to_client(sock, "Enter destination account number: ");
                read_from_client(sock, buffer, sizeof(buffer));
                int to_account = atoi(buffer);

                if (to_account == account.account_no)
                {
                    write_to_client(sock, "Cannot transfer to your own account.\n");
                }
                else
                {
                    write_to_client(sock, "Enter amount to transfer: ");
                    read_from_client(sock, buffer, sizeof(buffer));
                    float amount = atof(buffer);

                    transfer_funds(sock, account.account_no, to_account, amount);
                }
            }
            else
            {
                write_to_client(sock, "Invalid choice.\n");
            }
        }
    }
}
