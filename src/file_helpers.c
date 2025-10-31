#include "../includes/server.h"

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

long get_next_loan_id(int fd)
{
    Loan loan;
    long max_id = 0; // Start from 0, so first loan is 1

    lseek(fd, 0, SEEK_SET); // Rewind to the start of the file

    while (read(fd, &loan, sizeof(Loan)) == sizeof(Loan))
    {
        if (loan.loanID > max_id)
        {
            max_id = loan.loanID;
        }
    }
    return max_id + 1; // Return the next available ID
}

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
