// Moved header into includes/
#ifndef SERVER_H
#define SERVER_H

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
#include <pthread.h>

#define PORT 8080
#define MAX_CLIENTS 10

#define USER_FILE "users.dat"
#define ACCOUNT_FILE "accounts.dat"
#define LOAN_FILE "loans.dat"
#define TRANSACTION_FILE "transactions.dat"
#define FEEDBACK_FILE "feedback.dat"

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
    LOAN_DEPOSIT = 3,
    TRANSFER_SENT = 4,
    TRANSFER_RECEIVED = 5
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

typedef struct {
    int accountID;
    char message[1034];
} Feedback;

// Globals
extern pthread_spinlock_t login_lock;
extern int logged_in_users[MAX_CLIENTS];

// Utilities
int read_from_client(int sock, char *buffer, int size);
void write_to_client(int sock, const char *message);

// File helpers
long find_user_offset(int fd, int userID);
long find_account_offset(int fd, int account_no);
long find_loan_offset(int fd, int loan_id);
int get_next_user_id(int fd);
int get_next_account_no(int fd);
long get_next_loan_id(int fd);
long get_next_transaction_id(int fd);
void initialize_admin();

// Transactions
void log_transaction(int accountID, TransactionType type, float amount, float oldBalance, float newBalance);
void view_transactions(int sock, int account_no);
int transfer_funds(int sock, int from_account, int to_account, float amount);

// Feedback
void give_feedback(int accountID, const char *message);
void view_feedbacks(int sock);

// Loans
void view_pending_loans(int sock);
void assign_loan(int sock);
void employee_process_loan(int sock, User emp_user);
void customer_apply_loan(int sock, User user);

// Reusable functions & menus
int reusable_add_user(int sock, Role adder_role);
void reusable_add_bank_account(int sock, int new_account_no);
void reusable_modify_user(int sock, Role modifier_role, int target_userID);
void reusable_activate_deactivate_user(int sock, int choice);
void reusable_activate_deactivate_account(int sock, int choice);

void admin_menu(int sock, User admin_user);
void manager_menu(int sock, User mgr_user);
void employee_menu(int sock, User emp_user);
void customer_menu(int sock, User user, Account account);

// Client handler and main
void *handle_client(void *sock_ptr);

#endif // SERVER_H
