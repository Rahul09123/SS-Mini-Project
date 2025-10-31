# Banking System with Multi-User Support

A multi-threaded banking management system with role-based access control, transaction management, and customer feedback system.

## Quick Start

1. Build the project:
```bash
# Build the server
make clean && make

# Build the client
gcc client.c -o client
```

2. Run the system:
```bash
# Terminal 1: Start server
./server

# Terminal 2: Start client
./client
```

3. Default admin login:
- UserID: `1`
- Password: `admin123`

## Step-by-Step Usage Guide

### 1. Creating Users (Admin)
1. Login as admin
2. Choose option 1 (Add User)
3. Enter details when prompted:
   - Name
   - Password
   - Role (1=Customer, 3=Employee, 4=Manager)
4. For customers, bank account is created automatically

### 2. Customer Operations
1. Login as customer
2. Available options:
   - Deposit/Withdraw money
   - Check balance
   - Apply for loan
   - Submit feedback
   - View transactions

### 3. Loan Process
1. Customer: Apply for loan
2. Manager: Approve/Reject loan
3. Employee: Process approved loans

### 4. Feedback System
1. Customer: Submit feedback
2. Admin/Manager/Employee: View all feedback

## System Architecture

### Components
```
server1.c        - Main server program
client.c         - Client interface
includes/        - Header files
src/            - Core functionality
utils/          - Helper functions
```

### Data Files
- users.dat: User accounts
- accounts.dat: Bank accounts
- transactions.dat: Transaction records
- loans.dat: Loan applications
- feedback.dat: Customer feedback

## Role Permissions

### Admin
- [x] User management
- [x] Account creation
- [x] View feedback
- [x] System monitoring

### Manager
- [x] Account activation
- [x] Loan approval
- [x] View feedback
- [x] Customer management

### Employee
- [x] Add customers
- [x] Process loans
- [x] View transactions
- [x] View feedback

### Customer
- [x] Banking operations
- [x] Loan applications
- [x] Submit feedback
- [x] View own transactions

## Security Features

- Thread-safe operations
- File-level locking
- Session management
- Role-based access
- Password protection
- Single login enforcement

## Example Workflows

### Creating a New Customer
```
Admin Login → Add User → Role 1 (Customer) → 
Create Account → Set Initial Balance
```

### Processing a Loan
```
Customer: Apply for Loan → 
Manager: Review & Approve → 
Employee: Process Loan
```

### Feedback Loop
```
Customer: Submit Feedback → 
Manager/Admin: View Feedback → 
Take Necessary Action
```

## Common Operations

### Deposit Money
1. Login as customer
2. Select option 1 (Deposit)
3. Enter amount
4. Confirm transaction

### Apply for Loan
1. Login as customer
2. Select option 6
3. Enter loan amount
4. Wait for approval

### Submit Feedback
1. Login as customer
2. Select option 8
3. Type feedback message
4. Submit

## Troubleshooting

### Login Issues
- Verify correct UserID
- Check account is active
- Ensure no duplicate login

### Transaction Failures
- Check sufficient balance
- Verify account status
- Confirm connection active

## Development Notes

### Building
- Uses GNU Make
- Requires GCC compiler
- POSIX thread support needed

### Testing
- Test with multiple clients
- Verify file locking
- Check concurrent access
- Validate all role operations

### Maintenance
- Regular backup of .dat files
- Monitor server logs
- Check disk space
- Verify file permissions

## Support

For technical issues:
1. Check server logs
2. Verify file permissions
3. Ensure proper compilation
4. Check network connectivity# SS-Project
