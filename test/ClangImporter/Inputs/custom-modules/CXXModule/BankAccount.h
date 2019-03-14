class BankAccount {
private:
    int balance;

public:
    BankAccount();
    BankAccount(int initialBalance);

    void deposit(int amount);
    bool withdraw(int amount);

    int getBalance() const;

    static int numberOfOpenAccounts();
};
