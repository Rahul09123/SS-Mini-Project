#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>
#include <errno.h>
#include <time.h>
#include <threads.h>
#include <bits/pthreadtypes.h>

#define PORT 8080
#define MAX_CLIENTS 10

#define USER_FILE "users.dat"
#define ACCOUNT_FILE "accounts.dat"
#define LOAN_FILE "loans.dat"
#define TRANSACTION_FILE "transactions.dat"
#define FEEDBACK "feedback.dat"

// --- Globals for login management ---
pthread_spinlock_t login_lock; // applying spin lock for the multiple user logins
int logged_in_users[MAX_CLIENTS];

// Role-based access
typedef enum
{
    CUSTOMER = 1,
    ADMIN = 2,
    EMPLOYEE = 3,
    MANAGER = 4
} Role;
typedef enum
{
    PENDING = 1,
    PROCESSING = 2,
    APPROVED = 3,
    REJECTED = 4
} LoanStatus;
typedef enum
{
    DEPOSIT = 1,
    WITHDRAWAL = 2,
    LOAN_DEPOSIT = 3
} TransactionType;

// User struct: For (Admin, Manager, Employee, Customer)
typedef struct
{
    int userID; // used for searching
    char name[50];
    char password[20];
    Role role;
    int is_active; // 1 = active, 0 = inactive
} User;

// Account for custormer only
typedef struct
{
    int account_no;
    float balance;
    int is_active; // For manager to activate/deactivate
} Account;

// Loan Structure
typedef struct
{
    int loanID;
    int customerUserID; // This is the User.userID of the customer
    float amount;
    LoanStatus status;
    int assignedEmployeeID; // -1 if unassigned
} Loan;

// Transaction Structure
typedef struct
{
    long transactionID;
    int accountID; // This is the Account.account_no
    TransactionType type;
    float amount;
    float oldBalance;
    float newBalance;
    time_t timestamp;
} Transaction;

typedef struct{
    int accountID;
    char message[1034];
} Feedback;


// --- Function Declarations ---
void *handle_client(void *sock);
void customer_menu(int sock, User user, Account account);
void admin_menu(int sock, User admin_user);
void employee_menu(int sock, User emp_user);
void manager_menu(int sock, User mgr_user);

void view_pending_loans(int sock);
void assign_loan(int sock);
void employee_process_loan(int sock, User emp_user);
void view_transactions(int sock, int account_no);
void customer_apply_loan(int sock, User user);
void view_feedback(int sock);

// File Helpers
long find_user_offset(int fd, int userID);
long find_account_offset(int fd, int account_no);
long find_loan_offset(int fd, int loan_id);
int get_next_user_id(int fd);
int get_next_account_no(int fd);
long get_next_loan_id(int fd);
long get_next_transaction_id(int fd);
void initialize_admin();
void log_transaction(int accountID, TransactionType type, float amount, float oldBalance, float newBalance);
void give_feedback(int accountID,char* message);

// Reusable Functions
int reusable_add_user(int sock, Role adder_role);
void reusable_add_bank_account(int sock, int new_account_no);
void reusable_modify_user(int sock, Role modifier_role, int target_userID);
void reusable_activate_deactivate_user(int sock, int choice);
void reusable_activate_deactivate_account(int sock, int choice);

// ============================
// Helper Functions
// ============================

// read the data from client
int read_from_client(int sock, char *buffer, int size)
{
    memset(buffer, 0, size);
    int bytes_read = read(sock, buffer, size - 1);
    if (bytes_read > 0)
        buffer[strcspn(buffer, "\r\n")] = 0;
    return bytes_read; // <-- ADD THIS
}

// write the data to client
void write_to_client(int sock, const char *message)
{
    write(sock, message, strlen(message));
}

// Finds a user in users.dat
long find_user_offset(int fd, int userID)
{
    User user;
    long offset = 0;
    lseek(fd, 0, SEEK_SET);
    while (read(fd, &user, sizeof(User)) == sizeof(User))
    {
        if (user.userID == userID)
            return offset;
        offset += sizeof(User);
    }
    return -1;
}

// Finds an account in accounts.dat
long find_account_offset(int fd, int account_no)
{
    Account acc;
    long offset = 0;
    lseek(fd, 0, SEEK_SET);
    while (read(fd, &acc, sizeof(Account)) == sizeof(Account))
    {
        if (acc.account_no == account_no)
            return offset;
        offset += sizeof(Account);
    }
    return -1;
}

// Finds a loan in loans.dat
long find_loan_offset(int fd, int loan_id)
{
    Loan loan;
    long offset = 0;
    lseek(fd, 0, SEEK_SET);
    while (read(fd, &loan, sizeof(Loan)) == sizeof(Loan))
    {
        if (loan.loanID == loan_id)
            return offset;
        offset += sizeof(Loan);
    }
    return -1;
}

// Gets next ID from users.dat
int get_next_user_id(int fd)
{
    User user;
    int max_no = 1000;
    lseek(fd, 0, SEEK_SET);
    while (read(fd, &user, sizeof(User)) == sizeof(User))
    {
        if (user.userID > max_no)
            max_no = user.userID;
    }
    return max_no + 1;
}

// Gets next ID from accounts.dat
int get_next_account_no(int fd)
{
    Account acc;
    int max_no = 5000; // Start customer accounts from 5000
    lseek(fd, 0, SEEK_SET);
    while (read(fd, &acc, sizeof(Account)) == sizeof(Account))
    {
        if (acc.account_no > max_no)
            max_no = acc.account_no;
    }
    return max_no + 1;
}

// Finds the next available loan ID
long get_next_loan_id(int fd)
{
    Loan loan;
    long max_id = 0; // Start from 0, so first loan is 1

    // lseek() is a system call to move the file pointer
    lseek(fd, 0, SEEK_SET); // Rewind to the start of the file

    // Read every record
    while (read(fd, &loan, sizeof(Loan)) == sizeof(Loan))
    {
        if (loan.loanID > max_id)
        {
            max_id = loan.loanID;
        }
    }
    return max_id + 1; // Return the next available ID
}

// Finds the next available transaction ID
long get_next_transaction_id(int fd)
{
    Transaction trans;
    long max_id = 0; // Start from 0, so first transaction is 1

    lseek(fd, 0, SEEK_SET); // Rewind to the start of the file

    while (read(fd, &trans, sizeof(Transaction)) == sizeof(Transaction))
    {
        if (trans.transactionID > max_id)
        {
            max_id = trans.transactionID;
        }
    }
    return max_id + 1;
}

// Atomically logs a new transaction to the transaction file
void log_transaction(int accountID, TransactionType type, float amount, float oldBalance, float newBalance)
{
    // Open for Read/Write to be able to get next ID and append
    int fd = open(TRANSACTION_FILE, O_RDWR | O_CREAT, 0666);
    if (fd < 0)
    {
        perror("Failed to open transaction log");
        return;
    }

    // --- ACQUIRE LOCK ---
    // where two threads try to get the next ID and append at the same time.
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;    // Exclusive Write Lock
    lock.l_whence = SEEK_SET; // From the beginning
    lock.l_start = 0;         //...of the file
    lock.l_len = 0;           //...for the whole file

    // F_SETLKW: Blocking lock, will wait until it's acquired
    fcntl(fd, F_SETLKW, &lock);
    // 1. Get the next available ID (while locked)
    long next_trans_id = get_next_transaction_id(fd);

    // 2. Create the transaction record
    Transaction trans = {
        .transactionID = next_trans_id,
        .accountID = accountID,
        .type = type,
        .amount = amount,
        .oldBalance = oldBalance,
        .newBalance = newBalance,
        .timestamp = time(NULL) // Get current system time
    };

    // 3. Go to the end of the file to append
    lseek(fd, 0, SEEK_END);

    // 4. Write the new transaction
    write(fd, &trans, sizeof(Transaction));

    // 5. Release the lock
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);

    close(fd);
}

void give_feedback(int accountId, char* message) {
    int fd = open(FEEDBACK, O_RDWR | O_CREAT, 0666);
    if (fd < 0)
    {
        perror("Failed to open feedback file\n");
        return;
    }
    // --- ACQUIRE LOCK ---
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    fcntl(fd, F_SETLKW, &lock);
    
    // --- START FIX ---
    Feedback fb;
    fb.accountID = accountId;
    // You must copy the string, not assign the pointer
    strncpy(fb.message, message, sizeof(fb.message) - 1);
    fb.message[sizeof(fb.message) - 1] = '\0'; // Ensure null-termination
    // --- END FIX ---

    lseek(fd, 0, SEEK_END);
    write(fd, &fb, sizeof(Feedback));

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);
}


// Creates a default admin USER if users.dat is empty
void initialize_admin()
{
    int fd = open(USER_FILE, O_RDWR | O_CREAT, 0666);
    if (fd < 0)
    {
        perror("Failed to open user file for init");
        return;
    }

    if (lseek(fd, 0, SEEK_END) == 0)
    {
        User admin_user = {1000, "Admin User", "admin123", ADMIN, 1};
        write(fd, &admin_user, sizeof(User));
        printf("Default admin user created. (User: 1000, Pass: admin123)\n");
    }
    close(fd);
}

// ============================
// Reusable Functions
// ============================

// Adds a user to users.dat. Returns the new UserID on success, -1 on failure.
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

// Adds a new bank account to accounts.dat
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

    // Check if account already exists
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

// Modifies a user in users.dat
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

// Activates/Deactivates a USER LOGIN in users.dat (for Admin)
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

// Activates/Deactivates a BANK ACCOUNT in accounts.dat (for Manager)
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
// ===================================
// Feedback Function
// ===================================

void view_feedback(int sock) {
    int fd = open(FEEDBACK, O_RDONLY);
    if (fd < 0) {
        write_to_client(sock, "Error: Could not open feedback file.\n");
        return;
    }

    // Apply a shared read lock
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_RDLCK;
    lock.l_start = 0;
    lock.l_len = 0;
    fcntl(fd, F_SETLKW, &lock);

    Feedback fb;
    char buffer[8192] = {0}; // Large buffer for all feedback
    char line[1100]; // Message size + prefix
    int found = 0;

    strcat(buffer, "\n--- Customer Feedback ---\n");

    while(read(fd, &fb, sizeof(Feedback)) == sizeof(Feedback)) {
        found = 1;
        sprintf(line, "From Account %d: %s\n---\n", fb.accountID, fb.message);
        // Prevent buffer overflow in the rare case of too much feedback
        if (strlen(buffer) + strlen(line) < sizeof(buffer) - 1) {
            strcat(buffer, line);
        }
    }

    if (!found) {
        strcat(buffer, "No feedback submitted yet.\n");
    }

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);

    write_to_client(sock, buffer);
}

// ===================================
// Loan & Transaction Functions
// ===================================
 // Reads the loan file and sends a list of pending loans to the client.
 // (Used by Manager and Employee)
 
void view_pending_loans(int sock)
{
    int fd;
    Loan loan;
    char buffer[4096] = {0}; // Buffer to build the response
    char temp_line[256];
    int found_pending = 0;

    fd = open(LOAN_FILE, O_RDONLY);
    if (fd == -1)
    {
        perror("Error opening loan file");
        write_to_client(sock, "Error: Could not access loan data.\n");
        return;
    }

    // Apply a shared read lock to the whole file
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_RDLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;             // Lock the entire file
    fcntl(fd, F_SETLKW, &lock); // Wait for the lock

    strcat(buffer, "\n--- Pending Loans ---\n");
    strcat(buffer, "ID  | Customer | Amount   | Status      | Assigned To\n");
    strcat(buffer, "-------------------------------------------------------\n");

    // Read each loan record
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

    // Unlock the file
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);

    write_to_client(sock, buffer);
}

   // Allows the manager to approve or reject a loan.
void assign_loan(int sock)
{
    int fd;
    Loan loan;
    int loan_id_to_assign;
    int new_status_choice;
    LoanStatus new_status;
    int found = 0;
    char buffer[1024];

    // 1. Get Loan ID from manager
    write_to_client(sock, "Enter Loan ID to assign (Approve/Reject): ");
    read_from_client(sock, buffer, sizeof(buffer));
    loan_id_to_assign = atoi(buffer);

    // 2. Get new status from manager
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

    // 3. Find and update the loan
    long offset = find_loan_offset(fd, loan_id_to_assign);

    if (offset != -1)
    {
        // We found it, now lock this specific record
        struct flock lock;
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = offset;
        lock.l_len = sizeof(Loan);

        fcntl(fd, F_SETLKW, &lock); // Wait for lock

        // Read the record
        lseek(fd, offset, SEEK_SET);
        read(fd, &loan, sizeof(Loan));

        // Check if loan is in a state that can be assigned
        if (loan.status == PENDING || loan.status == PROCESSING)
        {

            loan.status = new_status;

            // If approved, we need to deposit the money into the customer's account
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
                        // Lock the bank account
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

                        // Log this deposit
                        log_transaction(acc.account_no, LOAN_DEPOSIT, loan.amount, old_bal, acc.balance);

                        // Unlock account
                        acc_lock.l_type = F_UNLCK;
                        fcntl(acc_fd, F_SETLK, &acc_lock);
                        write_to_client(sock, "Loan funds deposited to account.\n");
                    }
                    close(acc_fd);
                }
            }

            // Seek back and write the updated loan record
            lseek(fd, offset, SEEK_SET);
            write(fd, &loan, sizeof(Loan));

            write_to_client(sock, "Loan status updated successfully.\n");
        }
        else
        {
            write_to_client(sock, "Error: This loan is not pending or processing.\n");
        }
        // Unlock the loan record
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);
    }
    else
    {
        write_to_client(sock, "Error: Loan ID not found.\n");
    }

    close(fd);
}

 // Allows an employee to claim a PENDING loan for processing.
void employee_process_loan(int sock, User emp_user)
{
    char buffer[1024];

    // 1. Show  pending loans
    view_pending_loans(sock);

    // 2. Ask which one to claim
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


 //Views all transactions for a specific account.

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
    char buffer[8192] = {0}; // Large buffer for many transactions
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

            // Format timestamp
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

  //customer to apply for a new loan.

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
    fcntl(fd, F_SETLKW, &lock); // Lock entire file

    Loan loan;
    loan.loanID = get_next_loan_id(fd);
    loan.customerUserID = user.userID;
    loan.amount = amount;
    loan.status = PENDING;
    loan.assignedEmployeeID = -1; // Unassigned

    lseek(fd, 0, SEEK_END);
    write(fd, &loan, sizeof(Loan));

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);

    sprintf(buffer, "Loan application for $%.2f submitted. Loan ID: %d\n", amount, loan.loanID);
    write_to_client(sock, buffer);
}

// ============================
// Admin Menu
// ============================
void admin_menu(int sock, User admin_user)
{
    char buffer[1024];
    while (1)
    {
        write_to_client(sock, "\n--- Admin Menu ---\n1. Add User\n2. Deactivate User Login\n3. Activate User Login\n4. Modify User\n5. Search User\n6. Add Bank Account for Customer\n7. Exit\nChoice: ");
        int choice;
        if (read_from_client(sock, buffer, sizeof(buffer)) <= 0)
            choice = 7; // Force exit on disconnect
        else
        {
            choice = atoi(buffer);
        }
        if (choice == 7)
            break;

        if (choice == 1)
        { // Add User

            // Step 1: Add the user (Admin can set any role)
            int new_user_id = reusable_add_user(sock, ADMIN);

            if (new_user_id == -1)
            {
                continue; // reusable_add_user already printed an error
            }

            // Step 2: Check the role of the user we just created
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

            // Step 3: If they are a customer, automatically create their bank account
            if (new_user.role == CUSTOMER)
            {
                write_to_client(sock, "New user is a Customer. Proceeding to create bank account...\n");
                reusable_add_bank_account(sock, new_user_id);
            }
            // --- END MODIFIED LOGIC ---
        }
        else if (choice == 2)
        { // Deactivate User
            reusable_activate_deactivate_user(sock, 2);
        }
        else if (choice == 3)
        { // Activate User
            reusable_activate_deactivate_user(sock, 3);
        }
        else if (choice == 4)
        {                                          // Modify User [cite: 43]
            reusable_modify_user(sock, ADMIN, -1); // -1 signifies modifying OTHERS
        }
        else if (choice == 5)
        { // Search User
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
        { // Add Bank Account
            write_to_client(sock, "Enter Customer UserID to create account for: ");
            read_from_client(sock, buffer, sizeof(buffer));
            int user_id = atoi(buffer);
            // TODO: Check if user_id is a CUSTOMER
            reusable_add_bank_account(sock, user_id);
        }
    }
}
// ============================
// Manager Menu
// ============================
void manager_menu(int sock, User mgr_user)
{
    char buffer[1024];
    while (1)
    {
        write_to_client(sock, "\n--- Manager Menu ---\n1. Activate Customer Account\n2. Deactivate Customer Account\n3. View Pending Loans\n4. Assign (Approve/Reject) Loan\n5. View Feedback\n6. Exit\nChoice: ");
        int choice;
        if (read_from_client(sock, buffer, sizeof(buffer)) <= 0)
            choice = 6; // Force exit on disconnect
        else
        {
            choice = atoi(buffer);
        }
        if (choice == 6)
            break;

        if (choice == 1)
        {                                                  // Activate Account
            reusable_activate_deactivate_account(sock, 3); // 3 for Activate
        }
        else if (choice == 2)
        {                                                  // Deactivate Account
            reusable_activate_deactivate_account(sock, 2); // 2 for Deactivate
        }
        else if (choice == 3)
        { // View Pending Loans
            view_pending_loans(sock);
        }
        else if (choice == 4)
        { // Assign Loan
            assign_loan(sock);
        }
        else if(choice == 5){
            view_feedback(sock);
        }
        else
        {
            write_to_client(sock, "Invalid choice.\n");
        }
    }
}

// ============================
// Employee Menu
// ============================
void employee_menu(int sock, User emp_user)
{
    char buffer[1024];
    while (1)
    {
        write_to_client(sock, "\n--- Employee Menu ---\n1. Add New Customer\n2. Modify Customer Details\n3. Process (Claim) Loan\n4. View Customer Transactions\n5. View Feedback\n6. Exit\nChoice: ");
        int choice;
        if (read_from_client(sock, buffer, sizeof(buffer)) <= 0)
            choice = 6; // Force exit on disconnect
        else
        {
            choice = atoi(buffer);
        }
        if (choice == 6)
            break;

        if (choice == 1)
        { // Add New Customer
            int new_user_id = reusable_add_user(sock, EMPLOYEE);
            if (new_user_id != -1)
            {
                write_to_client(sock, "Now, creating bank account for new user...\n");
                reusable_add_bank_account(sock, new_user_id);
            }
        }
        else if (choice == 2)
        { // Modify Customer Details
            reusable_modify_user(sock, EMPLOYEE, -1);
        }
        else if (choice == 3)
        { // Process Loan
            employee_process_loan(sock, emp_user);
        }
        else if (choice == 4)
        { // View Customer Transactions
            write_to_client(sock, "Enter Customer Account Number: ");
            read_from_client(sock, buffer, sizeof(buffer));
            int acc_no = atoi(buffer);
            view_transactions(sock, acc_no);
        }
        else if(choice == 5){
            view_feedback(sock);
        }
        else
        {
            write_to_client(sock, "Invalid choice.\n");
        }
    }
}

// ============================
// Customer Menu
// ============================
void customer_menu(int sock, User user, Account account)
{
    char buffer[1024];
    int choice;
    while (1)
    {
        // Refresh account data in case of balance change from loan
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
                        "8. View Feedback\n"
                        "9. Exit\n"
                        "Choice: ",
                user.name, account.account_no);
        write_to_client(sock, buffer);

        if (read_from_client(sock, buffer, sizeof(buffer)) <= 0)
        {
            choice = 9; // Force exit on disconnect
        }
        else
        {
            choice = atoi(buffer);
            if (choice == 9)
                break;

            // Check if bank account is active
            if (!account.is_active && choice != 4 && choice != 9)
            { // Can still change password or exit
                write_to_client(sock, "Your bank account is deactivated. Please contact a manager.\n");
                continue;
            }

            if (choice == 1 || choice == 2 || choice == 3 || choice == 5)
            { // Bank Account operations
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

                // Re-read account data
                lseek(fd, offset, SEEK_SET);
                read(fd, &account, sizeof(Account));

                if (choice == 1)
                { // Deposit
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
                { // Withdraw
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
                { // Balance Enquiry
                    sprintf(buffer, "Your current balance: %.2f\n", account.balance);
                    write_to_client(sock, buffer);
                }
                else if (choice == 5)
                { // View Details
                    sprintf(buffer, "Account No: %d\nBalance: %.2f\nJoint: %s\nActive: %s\n",
                            account.account_no,
                            account.balance,
                            account.is_active ? "Yes" : "No");
                    write_to_client(sock, buffer);
                }
                lock.l_type = F_UNLCK;
                fcntl(fd, F_SETLK, &lock);
                close(fd);
            }
            else if (choice == 4)
            { // Password Change
                reusable_modify_user(sock, CUSTOMER, user.userID);
            }
            else if (choice == 6)
            { // Apply for Loan
                customer_apply_loan(sock, user);
            }
            else if (choice == 7)
            { // View My Transactions
                view_transactions(sock, account.account_no);
            }
            else if(choice == 8){
                // Give Feedback
                write_to_client(sock, "Enter your feedback (max 1024 chars): ");
                if (read_from_client(sock, buffer, sizeof(buffer)) > 0) {
                     give_feedback(account.account_no, buffer);
                     write_to_client(sock, "Feedback submitted. Thank you!\n");
                } else {
                    write_to_client(sock, "Feedback cancelled.\n");
                }
            }
            else
            {
                write_to_client(sock, "Invalid choice.\n");
            }
        }
    }
}

// ============================
// Client Handler
// ============================
void *handle_client(void *sock)
{
    int new_socket = *((int *)sock);
    free(sock);
    char buffer[1024], pass[20];
    int user_id;
    int logged_in_slot = -1; // Track which slot this user claimed

    // Login with user file
    int user_fd = open(USER_FILE, O_RDONLY);
    if (user_fd < 0)
    {
        write_to_client(new_socket, "Server error: Cannot open user file.\n");
        close(new_socket);
        pthread_exit(NULL);
    }

    write_to_client(new_socket, "Welcome to IIITB Bank\n");

    // --- Check read_from_client for disconnect ---
    write_to_client(new_socket, "Enter UserID: ");
    if (read_from_client(new_socket, buffer, sizeof(buffer)) <= 0)
    {
        printf("Client disconnected before sending UserID.\n");
        close(user_fd);
        close(new_socket);
        pthread_exit(NULL);
    }
    user_id = atoi(buffer);

    // --- Check read_from_client for disconnect ---
    write_to_client(new_socket, "Enter password: ");
    if (read_from_client(new_socket, pass, sizeof(pass)) <= 0)
    {
        printf("Client disconnected before sending password.\n");
        close(user_fd);
        close(new_socket);
        pthread_exit(NULL);
    }

    long user_offset = find_user_offset(user_fd, user_id);
    if (user_offset == -1)
    {
        write_to_client(new_socket, "Invalid login: User not found.\n");
    }
    else
    {
        User user;
        lseek(user_fd, user_offset, SEEK_SET);
        read(user_fd, &user, sizeof(User));

        if (strcmp(user.password, pass) == 0)
        {
            if (!user.is_active)
            {
                write_to_client(new_socket, "Login failed: User login is deactivated.\n");
            }
            else
            {

                // --- Spinlock Login Check ---
                int already_logged_in = 0;

                // 1. Lock the global list
                pthread_spin_lock(&login_lock);

                // 2. Check if user is already in the list
                for (int i = 0; i < MAX_CLIENTS; i++)
                {
                    if (logged_in_users[i] == user.userID)
                    {
                        already_logged_in = 1;
                        break;
                    }
                }

                if (already_logged_in)
                {
                    // 3a. User is found, unlock and reject login
                    pthread_spin_unlock(&login_lock);
                    write_to_client(new_socket, "Login failed: This user is already logged in elsewhere.\n");
                }
                else
                {
                    // 3b. User is not found, try to find a free slot
                    for (int i = 0; i < MAX_CLIENTS; i++)
                    {
                        if (logged_in_users[i] == -1)
                        {
                            logged_in_users[i] = user.userID; // Claim the slot
                            logged_in_slot = i;               // Save which slot we took
                            break;
                        }
                    }
                    // 4. Unlock the list
                    pthread_spin_unlock(&login_lock);

                    if (logged_in_slot == -1)
                    {
                        // 5a. No free slots, reject login
                        write_to_client(new_socket, "Login failed: Server is full. Please try again later.\n");
                    }
                    else
                    {
                        // 5b. Slot was found! Proceed with login.
                        write_to_client(new_socket, "Login successful!\n");
                        if (user.role == ADMIN)
                        {
                            admin_menu(new_socket, user);
                        }
                        else if (user.role == MANAGER)
                        {
                            manager_menu(new_socket, user);
                        }
                        else if (user.role == EMPLOYEE)
                        {
                            employee_menu(new_socket, user);
                        }
                        else if (user.role == CUSTOMER)
                        {
                            // --- CUSTOMER: FIND BANK ACCOUNT ---
                            int acc_fd = open(ACCOUNT_FILE, O_RDONLY);
                            if (acc_fd < 0)
                            {
                                write_to_client(new_socket, "Error: Could not open bank account file.\n");
                            }
                            else
                            {
                                long acc_offset = find_account_offset(acc_fd, user.userID); // Find account matching userID
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

                        // --- Logout Logic ---
                        // after the user exit the system
                        pthread_spin_lock(&login_lock);
                        if (logged_in_users[logged_in_slot] == user.userID)
                        {                                         
                            logged_in_users[logged_in_slot] = -1; // Free the slot
                        }
                        pthread_spin_unlock(&login_lock);
                        printf("User %d session ended.\n", user.userID);
                    }
                }
            }
        }
        else
        {
            write_to_client(new_socket, "Invalid login: Incorrect password.\n");
        }
    }

    close(user_fd);
    printf("Client disconnected.\n");
    close(new_socket);
    pthread_exit(NULL);
}

// ============================
// Main Server
// ============================
int main()
{
    int server_fd, new_socket;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    initialize_admin(); // Creates admin user in users.dat

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0)
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
    // ... (inside main()) ...
    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    // --- Initialize spinlock ---
    pthread_spin_init(&login_lock, 0); // Replaced pthread_mutex_init
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        logged_in_users[i] = -1; // -1 means slot is free
    }

    printf("Bank Server started. Waiting for clients on port %d...\n", PORT);

    while (1)
    {
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0)
        {
            perror("accept");
            continue;
        }

        printf("New connection accepted.\n");

        int *sock_ptr = malloc(sizeof(int));
        *sock_ptr = new_socket;
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, sock_ptr) != 0)
        {
            perror("pthread_create failed");
            free(sock_ptr);
            close(new_socket);
        }
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}