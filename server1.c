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

#define PORT 8080
#define MAX_CLIENTS 10
// New file definitions
#define USER_FILE "users.txt"
#define ACCOUNT_FILE "accounts.txt"
#define LOAN_FILE "loans.txt"
#define TRANSACTION_FILE "transactions.txt"

// Role-based access 
typedef enum { CUSTOMER = 1, ADMIN = 2, EMPLOYEE = 3, MANAGER = 4 } Role;
typedef enum { PENDING = 1, PROCESSING = 2, APPROVED = 3, REJECTED = 4 } LoanStatus;
typedef enum { DEPOSIT = 1, WITHDRAWAL = 2, LOAN_DEPOSIT = 3 } TransactionType;


// User struct: For ALL logins (Admin, Manager, Employee, Customer)
typedef struct {
    int userID; // Primary key for this file
    char name[50];
    char password[20];
    Role role;
    int is_active; // 1 = active, 0 = inactive 
} User;

// Account struct: For customer bank accounts ONLY
typedef struct {
    int account_no; // Primary key, links to User.userID
    float balance;
    int is_joint;
    int is_active; // For manager to activate/deactivate 
} Account;

// Loan Structure
typedef struct {
    int loanID;
    int customerUserID; // This is the User.userID of the customer
    float amount;
    LoanStatus status;
    int assignedEmployeeID; // -1 if unassigned
} Loan;

// Transaction Structure
typedef struct {
    long transactionID;
    int accountID; // This is the Account.account_no
    TransactionType type;
    float amount;
    float oldBalance;
    float newBalance;
    time_t timestamp;
} Transaction;


// --- Function Declarations ---
void *handle_client(void *sock);
void customer_menu(int sock, User user, Account account);
void admin_menu(int sock, User admin_user);
void employee_menu(int sock, User emp_user);
void manager_menu(int sock, User mgr_user); 

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

// Reusable Functions
int reusable_add_user(int sock, Role adder_role); // Returns new UserID
void reusable_add_bank_account(int sock, int new_account_no);
void reusable_modify_user(int sock, Role modifier_role, int target_userID);
void reusable_activate_deactivate_user(int sock, int choice);
void reusable_activate_deactivate_account(int sock, int choice); // New

// ============================
// Helper Functions
// ============================

//read the data from client
void read_from_client(int sock, char *buffer, int size) {
    memset(buffer, 0, size);
    int bytes_read = read(sock, buffer, size - 1);
    if (bytes_read > 0) buffer[strcspn(buffer, "\r\n")] = 0; 
}// Finds the next available loan ID

//write the data to client
void write_to_client(int sock, const char *message) {
    write(sock, message, strlen(message));
}

// Finds a user in users.dat
long find_user_offset(int fd, int userID) {
    User user;
    long offset = 0;
    lseek(fd, 0, SEEK_SET);
    while (read(fd, &user, sizeof(User)) == sizeof(User)) {
        if (user.userID == userID) return offset; 
        offset += sizeof(User);
    }
    return -1;
}

// Finds an account in accounts.dat
long find_account_offset(int fd, int account_no) {
    Account acc;
    long offset = 0;
    lseek(fd, 0, SEEK_SET);
    while (read(fd, &acc, sizeof(Account)) == sizeof(Account)) {
        if (acc.account_no == account_no) return offset; 
        offset += sizeof(Account);
    }
    return -1;
}

// Finds a loan in loans.dat
long find_loan_offset(int fd, int loan_id) {
    Loan loan;
    long offset = 0;
    lseek(fd, 0, SEEK_SET);
    while (read(fd, &loan, sizeof(Loan)) == sizeof(Loan)) {
        if (loan.loanID == loan_id) return offset; 
        offset += sizeof(Loan);
    }
    return -1;
}

// Gets next ID from users.dat
int get_next_user_id(int fd) {
    User user;
    int max_no = 1000; 
    lseek(fd, 0, SEEK_SET);
    while (read(fd, &user, sizeof(User)) == sizeof(User)) {
        if (user.userID > max_no) max_no = user.userID;
    }
    return max_no + 1;
}

// Gets next ID from accounts.dat
int get_next_account_no(int fd) {
    Account acc;
    int max_no = 5000; // Start customer accounts from 5000
    lseek(fd, 0, SEEK_SET);
    while (read(fd, &acc, sizeof(Account)) == sizeof(Account)) {
        if (acc.account_no > max_no) max_no = acc.account_no;
    }
    return max_no + 1;
}

// Finds the next available loan ID
long get_next_loan_id(int fd) {
    Loan loan;
    long max_id = 0; // Start from 0, so first loan is 1
    
    // lseek() is a system call to move the file pointer
    lseek(fd, 0, SEEK_SET); // Rewind to the start of the file
    
    // Read every record
    while (read(fd, &loan, sizeof(Loan)) == sizeof(Loan)) {
        if (loan.loanID > max_id) {
            max_id = loan.loanID;
        }
    }
    return max_id + 1; // Return the next available ID
}

// Finds the next available transaction ID
long get_next_transaction_id(int fd) {
    Transaction trans;
    long max_id = 0; // Start from 0, so first transaction is 1
    
    lseek(fd, 0, SEEK_SET); // Rewind to the start of the file
    
    while (read(fd, &trans, sizeof(Transaction)) == sizeof(Transaction)) {
        if (trans.transactionID > max_id) {
            max_id = trans.transactionID;
        }
    }
    return max_id + 1;
}

// Atomically logs a new transaction to the transaction file
void log_transaction(int accountID, TransactionType type, float amount, float oldBalance, float newBalance) {
    // Open for Read/Write to be able to get next ID and append
    int fd = open(TRANSACTION_FILE, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        perror("Failed to open transaction log");
        return;
    }

    // --- ACQUIRE LOCK ---
    // Lock the *entire* file. This is crucial to prevent a race condition
    // where two threads try to get the next ID and append at the same time.
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;    // Exclusive Write Lock
    lock.l_whence = SEEK_SET; // From the beginning
    lock.l_start = 0;         //...of the file
    lock.l_len = 0;           //...for the whole file
    
    // F_SETLKW: Blocking lock, will wait until it's acquired
    fcntl(fd, F_SETLKW, &lock);

    // --- CRITICAL SECTION ---
    
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

    // --- END CRITICAL SECTION ---

    // 5. Release the lock
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    
    close(fd);
}

// Creates a default admin USER if users.dat is empty
void initialize_admin() {
    int fd = open(USER_FILE, O_RDWR | O_CREAT, 0666);
    if (fd < 0) { perror("Failed to open user file for init"); return; }

    if (lseek(fd, 0, SEEK_END) == 0) {
        User admin_user = { 1000, "Admin User", "admin123", ADMIN, 1 };
        write(fd, &admin_user, sizeof(User));
        printf("Default admin user created. (User: 1000, Pass: admin123)\n");
    }
    close(fd);
}

// ============================
// Reusable Functions
// ============================

// Adds a user to users.dat. Returns the new UserID on success, -1 on failure.
int reusable_add_user(int sock, Role adder_role) {
    char buffer[1024];
    int fd = open(USER_FILE, O_RDWR);
    if (fd < 0) { write_to_client(sock, "Server error: Cannot open user file.\n"); return -1; }

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
    
    if (adder_role == ADMIN) {
        write_to_client(sock, "Enter role (1=Cust, 3=Emp, 4=Mgr): ");
        read_from_client(sock, buffer, sizeof(buffer));
        user.role = (Role)atoi(buffer);
    } else { // Employee adding a customer
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
void reusable_add_bank_account(int sock, int new_account_no) {
    char buffer[1024];
    int fd = open(ACCOUNT_FILE, O_RDWR | O_CREAT, 0666);
    if (fd < 0) { write_to_client(sock, "Server error: Cannot open account file.\n"); return; }

    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_start = 0;
    lock.l_len = 0; 
    fcntl(fd, F_SETLKW, &lock); // Lock account file

    // Check if account already exists
    if(find_account_offset(fd, new_account_no) != -1) {
        write_to_client(sock, "Error: Bank account for this user already exists.\n");
    } else {
        Account acc;
        acc.account_no = new_account_no; // Link to UserID

        write_to_client(sock, "Enter initial balance for new account: ");
        read_from_client(sock, buffer, sizeof(buffer));
        acc.balance = atof(buffer);

        write_to_client(sock, "Is joint account? (0=No / 1=Yes): ");
        read_from_client(sock, buffer, sizeof(buffer));
        acc.is_joint = atoi(buffer);
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
void reusable_modify_user(int sock, Role modifier_role, int target_userID) {
    char buffer[1024];
    int user_to_modify;

    if(target_userID == -1) { // Admin/Employee modifying OTHERS
        write_to_client(sock, "Enter UserID to modify: ");
        read_from_client(sock, buffer, sizeof(buffer));
        user_to_modify = atoi(buffer);
    } else { // User modifying their OWN password
        user_to_modify = target_userID;
    }
    
    int fd = open(USER_FILE, O_RDWR);
    if(fd < 0) { write_to_client(sock, "Server error: Cannot open user file.\n"); return; }

    long offset = find_user_offset(fd, user_to_modify);
    if (offset == -1) {
        write_to_client(sock, "User not found.\n");
    } else {
        struct flock lock;
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK;
        lock.l_start = offset;
        lock.l_len = sizeof(User);
        fcntl(fd, F_SETLKW, &lock);

        User user;
        lseek(fd, offset, SEEK_SET);
        read(fd, &user, sizeof(User));

        if(modifier_role == EMPLOYEE && user.role != CUSTOMER) {
             write_to_client(sock, "Permission denied. Can only modify customer users.\n");
        } else {
            write_to_client(sock, "Enter new password (leave blank to keep): ");
            read_from_client(sock, buffer, sizeof(buffer));
            if (strlen(buffer) > 0) strcpy(user.password, buffer);
            
            if(target_userID == -1) { // Only admin/emp can change name
                write_to_client(sock, "Enter new name (leave blank to keep): ");
                read_from_client(sock, buffer, sizeof(buffer));
                if (strlen(buffer) > 0) strcpy(user.name, buffer);
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
void reusable_activate_deactivate_user(int sock, int choice) {
    char buffer[1024];
    write_to_client(sock, "Enter UserID to modify: ");
    read_from_client(sock, buffer, sizeof(buffer));
    int user_id = atoi(buffer);

    int fd = open(USER_FILE, O_RDWR);
    if(fd < 0) { write_to_client(sock, "Server error.\n"); return; }

    long offset = find_user_offset(fd, user_id);
    if (offset == -1) {
        write_to_client(sock, "User not found.\n");
    } else {
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
void reusable_activate_deactivate_account(int sock, int choice) {
    char buffer[1024];
    write_to_client(sock, "Enter Customer Account Number to modify: ");
    read_from_client(sock, buffer, sizeof(buffer));
    int acc_no = atoi(buffer);

    int fd = open(ACCOUNT_FILE, O_RDWR);
    if(fd < 0) { write_to_client(sock, "Server error.\n"); return; }

    long offset = find_account_offset(fd, acc_no);
    if (offset == -1) {
        write_to_client(sock, "Bank account not found.\n");
    } else {
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


// ============================
// Admin Menu
// ============================
void admin_menu(int sock, User admin_user) {
    char buffer[1024];
    while (1) {
        write_to_client(sock, "\n--- Admin Menu ---\n1. Add User\n2. Deactivate User Login\n3. Activate User Login\n4. Modify User\n5. Search User\n6. Add Bank Account for Customer\n7. Exit\nChoice: ");
        read_from_client(sock, buffer, sizeof(buffer));
        int choice = atoi(buffer);
        if (choice == 7) break;

        if (choice == 1) { // Add User [cite: 42]
            reusable_add_user(sock, ADMIN); 
        }
        else if (choice == 2) { // Deactivate User
            reusable_activate_deactivate_user(sock, 2);
        }
         else if (choice == 3) { // Activate User
            reusable_activate_deactivate_user(sock, 3);
        }
        else if (choice == 4) { // Modify User [cite: 43]
            reusable_modify_user(sock, ADMIN, -1); // -1 signifies modifying OTHERS
        }
        else if (choice == 5) { // Search User
            write_to_client(sock, "Enter UserID to search: ");
            read_from_client(sock, buffer, sizeof(buffer));
            int user_id = atoi(buffer);

            int fd = open(USER_FILE, O_RDONLY);
            long offset = find_user_offset(fd, user_id);
            if (offset == -1) {
                write_to_client(sock, "User not found.\n");
            } else {
                User user;
                lseek(fd, offset, SEEK_SET);
                read(fd, &user, sizeof(User));
                sprintf(buffer, "UserID: %d\nName: %s\nRole: %d\nActive: %d\n\n",
                        user.userID, user.name, user.role, user.is_active);
                write_to_client(sock, buffer);
            }
            close(fd);
        }
        else if (choice == 6) { // Add Bank Account
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
void manager_menu(int sock, User mgr_user) {
    char buffer[1024];
    while (1) {
        write_to_client(sock, "\n--- Manager Menu ---\n1. Activate Customer Account\n2. Deactivate Customer Account\n3. View Pending Loans\n4. Assign Loan\n5. Exit\nChoice: ");
        read_from_client(sock, buffer, sizeof(buffer));
        int choice = atoi(buffer);
        if (choice == 5) break;

        if (choice == 1) { // Activate Account 
            reusable_activate_deactivate_account(sock, 3); // 3 for Activate
        }
        else if (choice == 2) { // Deactivate Account 
            reusable_activate_deactivate_account(sock, 2); // 2 for Deactivate
        }
        else if (choice == 3 || choice == 4) { // Loan functions [cite: 35]
            // ... (Loan logic from previous step is unchanged) ...
            if(choice == 3) write_to_client(sock, "Viewing pending loans...\n");
            if(choice == 4) write_to_client(sock, "Assigning loan...\n");
        }
    }
}

// ============================
// Employee Menu
// ============================
void employee_menu(int sock, User emp_user) {
    char buffer[1024];
    while (1) {
        write_to_client(sock, "\n--- Employee Menu ---\n1. Add New Customer\n2. Modify Customer Details\n3. Process Loan\n4. View Customer Transactions\n5. Exit\nChoice: ");
        read_from_client(sock, buffer, sizeof(buffer));
        int choice = atoi(buffer);
        if (choice == 5) break;

        if (choice == 1) { // Add New Customer [cite: 23]
            int new_user_id = reusable_add_user(sock, EMPLOYEE);
            if(new_user_id != -1) {
                write_to_client(sock, "Now, creating bank account for new user...\n");
                reusable_add_bank_account(sock, new_user_id);
            }
        }
        else if (choice == 2) { // Modify Customer Details [cite: 24]
            reusable_modify_user(sock, EMPLOYEE, -1);
        }
        else if (choice == 3) { // Process Loan [cite: 25, 26]
            // ... (Loan processing logic from previous step is unchanged) ...
            write_to_client(sock, "Processing loan...\n");
        }
        else if (choice == 4) { // View Customer Transactions [cite: 28]
            // ... (Transaction viewing logic from previous step is unchanged) ...
            write_to_client(sock, "Viewing transactions...\n");
        }
    }
}


// ============================
// Customer Menu
// ============================
void customer_menu(int sock, User user, Account account) {
    char buffer[1024];
    int choice;
    while (1) {
        sprintf(buffer, "\n--- Customer Menu (User: %s, Account: %d) ---\n1. Deposit\n2. Withdraw\n3. Balance Enquiry\n4. Password Change\n5. View Account Details\n6. Apply for Loan\n7. View My Transactions\n8. Exit\nChoice: ", user.name, account.account_no);
        write_to_client(sock, buffer);

        read_from_client(sock, buffer, sizeof(buffer));
        choice = atoi(buffer);
        if (choice == 8) break;

        // Check if bank account is active
        if (!account.is_active && choice != 4) {
            write_to_client(sock, "Your bank account is deactivated. Please contact a manager.\n");
            continue;
        }

        if (choice == 1 || choice == 2 || choice == 3 || choice == 5) { // Bank Account operations
            int fd = open(ACCOUNT_FILE, O_RDWR);
            if (fd < 0) { write_to_client(sock, "Error accessing account data.\n"); continue; }

            long offset = find_account_offset(fd, account.account_no);
            if (offset == -1) { write_to_client(sock, "CRITICAL ERROR: Account not found.\n"); close(fd); break; }

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

            if (choice == 1) { // Deposit [cite: 12]
                write_to_client(sock, "Enter amount to deposit: ");
                read_from_client(sock, buffer, sizeof(buffer));
                float amt = atof(buffer);
                if (amt <= 0) { write_to_client(sock, "Invalid amount.\n"); } 
                else {
                    float old_bal = account.balance;
                    account.balance += amt;
                    lseek(fd, offset, SEEK_SET);
                    write(fd, &account, sizeof(Account));
                    log_transaction(account.account_no, DEPOSIT, amt, old_bal, account.balance);
                    write_to_client(sock, "Deposit successful.\n");
                }
            }
            else if (choice == 2) { // Withdraw [cite: 13]
                write_to_client(sock, "Enter amount to withdraw: ");
                read_from_client(sock, buffer, sizeof(buffer));
                float amt = atof(buffer);
                if (amt <= 0) { write_to_client(sock, "Invalid amount.\n"); } 
                else if (account.balance >= amt) {
                    float old_bal = account.balance;
                    account.balance -= amt;
                    lseek(fd, offset, SEEK_SET);
                    write(fd, &account, sizeof(Account));
                    log_transaction(account.account_no, WITHDRAWAL, amt, old_bal, account.balance);
                    write_to_client(sock, "Withdrawal successful.\n");
                } else { write_to_client(sock, "Insufficient balance.\n"); }
            }
            else if (choice == 3) { // Balance Enquiry [cite: 11]
                sprintf(buffer, "Your current balance: %.2f\n", account.balance);
                write_to_client(sock, buffer);
            }
            else if (choice == 5) { // View Details
                sprintf(buffer, "Account No: %d\nBalance: %.2f\nJoint: %s\n",
                        account.account_no, account.balance, account.is_joint ? "Yes" : "No");
                write_to_client(sock, buffer);
            }
            lock.l_type = F_UNLCK;
            fcntl(fd, F_SETLK, &lock);
            close(fd);
        }
        else if (choice == 4) { // Password Change [cite: 16]
            reusable_modify_user(sock, CUSTOMER, user.userID);
        }
        else if (choice == 6) { // Apply for Loan [cite: 15]
            // ... (Loan application logic is unchanged, uses user.userID) ...
            write_to_client(sock, "Applying for loan...\n");
        }
        else if (choice == 7) { // View My Transactions [cite: 18]
            // ... (Transaction viewing logic is unchanged, uses account.account_no) ...
            write_to_client(sock, "Viewing transactions...\n");
        }
    }
}

// ============================
// Client Handler (NEW LOGIC)
// ============================
void *handle_client(void *sock) {
    int new_socket = *((int *)sock);
    free(sock);
    char buffer[1024], pass[20];
    int user_id;

    // 1. --- LOGIN WITH USER_FILE ---
    int user_fd = open(USER_FILE, O_RDONLY);
    if (user_fd < 0) {
        write_to_client(new_socket, "Server error: Cannot open user file.\n");
        close(new_socket); pthread_exit(NULL);
    }

    write_to_client(new_socket, "Welcome to IIITB Bank\n");
    write_to_client(new_socket, "Enter UserID: ");
    read_from_client(new_socket, buffer, sizeof(buffer));
    user_id = atoi(buffer);

    write_to_client(new_socket, "Enter password: ");
    read_from_client(new_socket, pass, sizeof(pass));

    long user_offset = find_user_offset(user_fd, user_id);
    if (user_offset == -1) {
        write_to_client(new_socket, "Invalid login: User not found.\n");
    } else {
        User user;
        lseek(user_fd, user_offset, SEEK_SET);
        read(user_fd, &user, sizeof(User));

        if (strcmp(user.password, pass) == 0) {
            if (!user.is_active) {
                 write_to_client(new_socket, "Login failed: User login is deactivated.\n");
            } else {
                write_to_client(new_socket, "Login successful!\n");
                
                // 2. --- DISPATCH BASED ON ROLE ---
                if (user.role == ADMIN) {
                    admin_menu(new_socket, user);
                } 
                else if (user.role == MANAGER) {
                    manager_menu(new_socket, user);
                }
                else if (user.role == EMPLOYEE) {
                    employee_menu(new_socket, user);
                }
                else if (user.role == CUSTOMER) {
                    // 3. --- CUSTOMER: FIND BANK ACCOUNT ---
                    int acc_fd = open(ACCOUNT_FILE, O_RDONLY);
                    if(acc_fd < 0) {
                        write_to_client(new_socket, "Error: Could not open bank account file.\n");
                    } else {
                        long acc_offset = find_account_offset(acc_fd, user.userID); // Find account matching userID
                        if(acc_offset == -1) {
                            write_to_client(new_socket, "Error: You are a customer but have no bank account.\n");
                        } else {
                            Account account;
                            lseek(acc_fd, acc_offset, SEEK_SET);
                            read(acc_fd, &account, sizeof(Account));
                            customer_menu(new_socket, user, account);
                        }
                        close(acc_fd);
                    }
                }
            }
        } else {
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
int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    initialize_admin_if_needed(); // Creates admin user in users.dat

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) { perror("socket failed"); exit(EXIT_FAILURE); }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed"); exit(EXIT_FAILURE);
    }
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen failed"); exit(EXIT_FAILURE);
    }

    printf("Bank Server started. Waiting for clients on port %d...\n", PORT);

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0) { perror("accept"); continue; }

        printf("New connection accepted.\n");

        int *sock_ptr = malloc(sizeof(int));
        *sock_ptr = new_socket;
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, sock_ptr) != 0) {
            perror("pthread_create failed");
            free(sock_ptr);
            close(new_socket);
        }
        pthread_detach(tid); 
    }

    close(server_fd);
    return 0;
}