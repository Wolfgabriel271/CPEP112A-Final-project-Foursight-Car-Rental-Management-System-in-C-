#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
/* zint used as CLI tool, no header needed */
#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#include <shellapi.h>
#include <direct.h>
#endif
#ifndef NO_CURL
#include <curl/curl.h>
#endif
#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#endif

/* [UPGRADE] Configuration Constants */
#define CONFIG_FILE "config.txt"
#define LATE_FEE_PER_HOUR 50.0 /* PHP 50 per hour late */

#define MAX_CARS 400
#define MAX_CUSTOMERS 1000
#define MAX_RENTALS 2000
#define MAX_USERS 50
#define BARCODE_LEN 64
#define PLATE_LEN 24
#define MODEL_LEN 80
#define NAME_LEN 100
#define LICENSE_LEN 40
#define CONTACT_LEN 40
#define USERNAME_LEN 40
#define PASSWORD_LEN 80 /* increased: SHA256: prefix(7) + 64 hex chars + null */
#define EMAIL_LEN 160
#define FILENAME_LEN 260

/* Text DB filenames */
#define DB_INVENTORY_FILE "inventory.txt"
#define DB_CUSTOMERS_FILE "customers.txt"
#define DB_RENTALS_FILE "rentals.txt"
#define DB_USERS_FILE "users.txt"
#define RECEIPTS_DIR "receipts"
#define BARCODES_DIR "barcodes"

#ifdef _WIN32
#define CLEAR_CMD "cls"
#else
#define CLEAR_CMD "clear"
#endif

#ifdef _WIN32
#define STRICMP _stricmp
#else
#define STRICMP strcasecmp
#endif

/* [SECURITY] SHA-256 hash length (hex string = 64 chars + null) */
#define SHA256_HASH_LEN 65
#define MAX_LOGIN_ATTEMPTS 3
#define LOCKOUT_SECONDS 300   /* 5 minutes */
#define HASHED_FLAG "SHA256:" /* prefix stored in users.txt to mark hashed passwords */

/* [UPGRADE] Global Config Variables */
char CONFIG_EMAIL[EMAIL_LEN] = "";
char CONFIG_PASSWORD[PASSWORD_LEN] = "";
char CONFIG_SMTP[128] = "smtp.gmail.com";
int CONFIG_PORT = 465;

/* Data structures */
typedef struct
{
    char barcode[BARCODE_LEN];
    char plateNumber[PLATE_LEN];
    char model[MODEL_LEN];
    double pricePerDay;
    int available;
    int totalRentedCount;
    int id;
} Car;

typedef struct
{
    int id;
    char name[NAME_LEN];
    char licenseNo[LICENSE_LEN];
    char contact[CONTACT_LEN];
    char email[EMAIL_LEN];
} Customer;

typedef struct
{
    int id;
    char barcode[BARCODE_LEN];
    char plateNumber[PLATE_LEN];
    char model[MODEL_LEN];
    char customerName[NAME_LEN];
    char licenseNo[LICENSE_LEN];
    int quantity;
    int days;
    double totalAmount;
    time_t rentTime;
    int returned;
    time_t returnTime;
    double lateFee;
    /* [DATE] Explicit date fields */
    char startDate[12];          /* YYYY-MM-DD */
    char expectedReturnDate[12]; /* YYYY-MM-DD */
    int warningIssued;           /* 1 if approaching-return warning shown */
} RentalRecord;

typedef struct
{
    char username[USERNAME_LEN];
    char password[PASSWORD_LEN];
    int isAdmin;
    int id;
} User;

/* Rental Item for bulk transactions */
typedef struct
{
    int carIndex;
    int quantity;
    int days;
    double subtotal;
} RentalItem;

/* [SECURITY] Login attempt tracking */
typedef struct
{
    char username[USERNAME_LEN];
    int attempts;
    time_t lockoutUntil;
} LoginAttempt;

LoginAttempt loginAttempts[MAX_USERS];
int loginAttemptCount = 0;

/* Global arrays */
Car inventory[MAX_CARS];
int carCount = 0;
int nextCarId = 1001;
Customer customers[MAX_CUSTOMERS];
int customerCount = 0;
int nextCustomerId = 1;
RentalRecord rentals[MAX_RENTALS];
int rentalCount = 0;
int nextRentalId = 1;
User users[MAX_USERS];
int userCount = 0;
int nextUserId = 1;
int terminalSupportsColor = 0;

/* Color codes */
enum
{
    CLR_RESET = 0,
    CLR_RED,
    CLR_GREEN,
    CLR_YELLOW,
    CLR_BLUE,
    CLR_MAGENTA,
    CLR_CYAN,
    CLR_WHITE,
    /* [UI] Bright/bold variants */
    CLR_BRIGHT_RED,
    CLR_BRIGHT_GREEN,
    CLR_BRIGHT_YELLOW,
    CLR_BRIGHT_MAGENTA,
    CLR_BRIGHT_CYAN,
    CLR_BRIGHT_WHITE,
    CLR_BOLD
};

/* ============= FUNCTION PROTOTYPES ============= */
/* Core system functions */
void enableTerminalColors();
void setColor(int color);
void resetColor();
void safeInput(char *buf, int maxlen, const char *prompt);
void flushInput();
void pressEnterToContinue();
void clearScreen();
int fileExists(const char *filename);
int createDirectoriesIfNeeded(const char *path);
void ensureReceiptsDir();
void printBanner();
int showLoginHeader();

/* [UPGRADE] Config Functions */
void loadConfig();
int isValidEmail(const char *email);

/* [SECURITY] Security Functions */
void sha256_hex(const char *input, char *out);
int verifyPassword(const char *stored, const char *input);
void migratePasswordsToHash();
int isLockedOut(const char *username);
void recordFailedAttempt(const char *username);
void resetAttempts(const char *username);

/* [VALIDATION] Input Validation Functions */
int validateNotEmpty(const char *val, const char *fieldName);
int validateTextOnly(const char *val, const char *fieldName);
int validateDouble(const char *val, double *out, double minVal, double maxVal, const char *fieldName);
int validateInt(const char *val, int *out, int minVal, int maxVal, const char *fieldName);
int validatePlate(const char *val);
int validateLicense(const char *val);
int validateContact(const char *val);
int validateUsername(const char *val);
int validatePassword(const char *val);
int inputDouble(double *out, double minVal, double maxVal, const char *prompt);
int inputInt(int *out, int minVal, int maxVal, const char *prompt);
void inputText(char *buf, int maxlen, const char *prompt, int allowEmpty);

/* [DATE] Date Functions */
void getTodayStr(char *buf);
int isLeapYear(int y);
int daysInMonth(int m, int y);
int validateDateStr(const char *s);
void addDaysToDate(const char *startDate, int days, char *out);
int daysBetweenDates(const char *from, const char *to);
int inputDateTyped(char *buf, const char *prompt);
int inputDateCalendar(char *buf, const char *prompt);
int inputDate(char *buf, const char *prompt);
double calcLateFee(const char *expectedReturn, const char *actualReturn, double pricePerDay);
void checkApproachingReturns();

/* Database functions */
void saveInventory();
void loadInventory();
void saveCustomers();
void loadCustomers();
void saveRentals();
void loadRentals();
void saveUsers();
void loadUsers();

/* Default data */
void addDefaultCars();
void addDefaultUsers();
void initializeDefaultDataIfEmpty();
void initializeSystem();

/* Search functions */
int findCarByBarcode(const char *barcode);
int findCarByPlate(const char *plate);
int findCustomerByLicense(const char *license);
int findCustomerByName(const char *name);
int findUserByUsername(const char *username);

/* Email functions */
size_t curl_payload_read(void *ptr, size_t size, size_t nmemb, void *userp);
int sendEmailLibCurl(const char *toAddress, const char *subject, const char *body);

/* Barcode functions */
void printAsciiBarcode(const char *code, FILE *out);
void generateBarcodePNG(const char *code, const char *outputFile);
void generateRentalBarcodePNG(int rentalId, const char *barcodeData);
void autoOpenFile(const char *filename);

/* Receipt functions */
void autoPrintReceipt(const char *filename);
void printReceiptFromRental(const RentalRecord *r);
int saveSimpleReceipt(int rentalId, const char *customerName, const char *license,
                      const char *model, const char *plate, const char *barcode,
                      int qty, int days, double total);
void saveBulkReceipt(int transactionId,
                     const char *custName,
                     const char *custLicense,
                     const char *custContact,
                     const char *custEmail,
                     const char *cashierName,
                     RentalItem items[],
                     int itemCount,
                     double total,
                     double cash,
                     double change,
                     int firstRentalId,
                     int lastRentalId);
void reprintReceiptMenu();

/* Login and main interface */
int login(char *loggedUser, int *isAdmin);
void mainDashboard(const char *loggedUser, int isAdmin);

/* Inventory management */
void showInventoryListDetailed();
void viewInventoryInteractive();
void showQuickInventory();

/* Customer management */
int selectCustomerMenu();
void listCustomers();
void addCustomer();
void editCustomer();
void deleteCustomer();
void searchCustomer();
void customerMenu();

/* SIMPLIFIED RENTAL SYSTEM - as mentor requested */
void rentCarMenu(const char *cashierName);
int selectRentalByMenu();
void returnCar();

/* Payment/Billing */
void paymentBillingMenu();

/* Reports */
void reportInventory();
void reportCustomers();
void reportRentals();
void reportsMenu();

/* Settings */
void changePassword(const char *currentUser);
void settingsMenu(const char *currentUser);

/* Admin user management */
void userManagementMenu(const char *currentUser);
void addUserMenu();
void viewUsersMenu();
void deleteUserMenu();
void changeUserPasswordMenu();
void toggleAdminMenu();
int selectUserMenu();

/* ============= FUNCTION IMPLEMENTATIONS ============= */
void enableTerminalColors()
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE)
    {
        terminalSupportsColor = 0;
        return;
    }
    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode))
    {
        terminalSupportsColor = 0;
        return;
    }
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (SetConsoleMode(hOut, dwMode))
        terminalSupportsColor = 1;
    else
        terminalSupportsColor = 0;
#else
    terminalSupportsColor = 1;
#endif
}

void setColor(int color)
{
    if (!terminalSupportsColor)
        return;
    switch (color)
    {
    case CLR_RED:
        printf("\x1b[31m");
        break;
    case CLR_GREEN:
        printf("\x1b[32m");
        break;
    case CLR_YELLOW:
        printf("\x1b[33m");
        break;
    case CLR_BLUE:
        printf("\x1b[34m");
        break;
    case CLR_MAGENTA:
        printf("\x1b[35m");
        break;
    case CLR_CYAN:
        printf("\x1b[36m");
        break;
    case CLR_WHITE:
        printf("\x1b[37m");
        break;
    /* [UI] Bright variants */
    case CLR_BRIGHT_RED:
        printf("\x1b[91m");
        break;
    case CLR_BRIGHT_GREEN:
        printf("\x1b[92m");
        break;
    case CLR_BRIGHT_YELLOW:
        printf("\x1b[93m");
        break;
    case CLR_BRIGHT_MAGENTA:
        printf("\x1b[95m");
        break;
    case CLR_BRIGHT_CYAN:
        printf("\x1b[96m");
        break;
    case CLR_BRIGHT_WHITE:
        printf("\x1b[97m");
        break;
    case CLR_BOLD:
        printf("\x1b[1m");
        break;
    default:
        printf("\x1b[0m");
        break;
    }
}

void resetColor()
{
    if (!terminalSupportsColor)
        return;
    printf("\x1b[0m");
}

/* ============= [UI] DISPLAY HELPERS ============= */
#define UI_WIDTH 60

/* Print a centered line padded to UI_WIDTH */
void uiCenterLine(const char *text, int color)
{
    int len = (int)strlen(text);
    int pad = (UI_WIDTH - len) / 2;
    if (pad < 0)
        pad = 0;
    setColor(color);
    printf("%*s%s\n", pad, "", text);
    resetColor();
}

/* Print a full-width horizontal rule */
void uiRule(int color)
{
    setColor(color);
    int i;
    for (i = 0; i < UI_WIDTH; i++)
        putchar('-');
    putchar('\n');
    resetColor();
}

/* Print a section header: rule + centered title + rule */
void uiHeader(const char *title, int titleColor, int ruleColor)
{
    uiRule(ruleColor);
    uiCenterLine(title, titleColor);
    uiRule(ruleColor);
}

/* Print a menu option with magenta number and white label */
void uiOption(int num, const char *label)
{
    setColor(CLR_BRIGHT_MAGENTA);
    printf("  [%d]", num);
    resetColor();
    setColor(CLR_BRIGHT_WHITE);
    printf(" %s\n", label);
    resetColor();
}

/* Print a labeled field in a stat row */
void uiStatLine(const char *label, const char *value, int valColor)
{
    setColor(CLR_YELLOW);
    printf("  %-20s", label);
    setColor(valColor);
    printf("%s\n", value);
    resetColor();
}

/* Print a prompt with cyan arrow */
void uiPrompt(const char *msg)
{
    setColor(CLR_BRIGHT_CYAN);
    printf("\n  >> ");
    resetColor();
    setColor(CLR_BRIGHT_WHITE);
    printf("%s", msg);
    resetColor();
}

/* Print a success message */
void uiSuccess(const char *msg)
{
    setColor(CLR_BRIGHT_GREEN);
    printf("\n  [OK] %s\n", msg);
    resetColor();
}

/* Print an error message */
void uiError(const char *msg)
{
    setColor(CLR_BRIGHT_RED);
    printf("\n  [!] %s\n", msg);
    resetColor();
}

/* Print a warning message */
void uiWarn(const char *msg)
{
    setColor(CLR_BRIGHT_YELLOW);
    printf("\n  [~] %s\n", msg);
    resetColor();
}

/* Print a blank line spacer */
void uiSpacer() { printf("\n"); }

void safeInput(char *buf, int maxlen, const char *prompt)
{
    if (prompt)
        printf("%s", prompt);
    if (!fgets(buf, maxlen, stdin))
    {
        buf[0] = 0;
        return;
    }
    buf[strcspn(buf, "\n")] = 0;
}

void flushInput()
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;
}

void pressEnterToContinue()
{
    uiSpacer();
    setColor(CLR_BRIGHT_MAGENTA);
    printf("  Press Enter to continue...");
    resetColor();
    fflush(stdout);
    getchar();
}

void clearScreen()
{
    system(CLEAR_CMD);
}

int fileExists(const char *filename)
{
    struct stat st;
    return (stat(filename, &st) == 0);
}

int createDirectoriesIfNeeded(const char *path)
{
#ifdef _WIN32
    return (_mkdir(path) == 0 || errno == EEXIST);
#else
    return (mkdir(path, 0755) == 0 || errno == EEXIST);
#endif
}

void ensureReceiptsDir()
{
    createDirectoriesIfNeeded(RECEIPTS_DIR);
}

void printBanner()
{
    uiSpacer();
    uiRule(CLR_BRIGHT_CYAN);
    uiCenterLine("FOURSIGHT CAR RENTALS", CLR_BRIGHT_YELLOW);
    uiCenterLine("Management System", CLR_BRIGHT_WHITE);
    uiRule(CLR_BRIGHT_CYAN);
    uiSpacer();
}

int showLoginHeader()
{
    int choice = 0;
    clearScreen();
    uiSpacer();
    uiSpacer();
    uiRule(CLR_BRIGHT_YELLOW);
    uiCenterLine("FOURSIGHT CAR RENTALS", CLR_BRIGHT_YELLOW);
    uiCenterLine("Management System v2.0", CLR_BRIGHT_WHITE);
    uiRule(CLR_BRIGHT_YELLOW);
    uiSpacer();
    uiCenterLine("Login As", CLR_BRIGHT_CYAN);
    uiSpacer();
    uiOption(1, "Admin");
    uiOption(2, "Cashier");
    uiSpacer();
    while (1)
    {
        uiPrompt("Choose option (1-2): ");
        if (scanf("%d", &choice) != 1)
        {
            flushInput();
            uiError("Invalid input. Please enter 1 or 2.");
            continue;
        }
        flushInput();
        if (choice == 1 || choice == 2)
            return choice;
        uiError("Please choose 1 for Admin or 2 for Cashier.");
    }
    return 0;
}

/* [UPGRADE] Configuration Loading */
void loadConfig()
{
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp)
    {
        fp = fopen(CONFIG_FILE, "w");
        if (fp)
        {
            fprintf(fp, "EMAIL=your_email@gmail.com\n");
            fprintf(fp, "PASSWORD=your_16char_app_password\n");
            fprintf(fp, "SMTP=smtp.gmail.com\n");
            fprintf(fp, "PORT=465\n");
            fclose(fp);
        }
        setColor(CLR_YELLOW);
        printf("WARNING: config.txt created. Please edit it with your Gmail App Password.\n");
        resetColor();
        pressEnterToContinue();
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp))
    {
        /* Strip \r\n */
        line[strcspn(line, "\r\n")] = 0;

        if (strncmp(line, "EMAIL=", 6) == 0)
            strncpy(CONFIG_EMAIL, line + 6, EMAIL_LEN - 1);
        else if (strncmp(line, "PASSWORD=", 9) == 0)
            strncpy(CONFIG_PASSWORD, line + 9, PASSWORD_LEN - 1);
        else if (strncmp(line, "SMTP=", 5) == 0)
            strncpy(CONFIG_SMTP, line + 5, 127);
        else if (strncmp(line, "PORT=", 5) == 0)
            CONFIG_PORT = atoi(line + 5);
    }
    fclose(fp);
}

/* [UPGRADE] Basic Email Validation */
int isValidEmail(const char *email)
{
    int atFound = 0;
    int dotFound = 0;
    for (int i = 0; email[i]; i++)
    {
        if (email[i] == '@')
            atFound = 1;
        if (email[i] == '.' && atFound)
            dotFound = 1;
    }
    return (atFound && dotFound);
}

/* ============= [VALIDATION] INPUT VALIDATION ============= */

/* Check non-empty */
int validateNotEmpty(const char *val, const char *fieldName)
{
    if (!val || val[0] == '\0')
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s cannot be empty.", fieldName);
        uiError(msg);
        return 0;
    }
    return 1;
}

/* Check letters/spaces only (names) */
int validateTextOnly(const char *val, const char *fieldName)
{
    if (!validateNotEmpty(val, fieldName))
        return 0;
    for (int i = 0; val[i]; i++)
    {
        if (!isalpha((unsigned char)val[i]) && val[i] != ' ' && val[i] != '-' && val[i] != '.')
        {
            char msg[128];
            snprintf(msg, sizeof(msg), "%s must contain letters only.", fieldName);
            uiError(msg);
            return 0;
        }
    }
    return 1;
}

/* Parse and range-check a double from string */
int validateDouble(const char *val, double *out, double minVal, double maxVal, const char *fieldName)
{
    if (!validateNotEmpty(val, fieldName))
        return 0;
    char *end;
    double d = strtod(val, &end);
    if (end == val || *end != '\0')
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s must be a valid number.", fieldName);
        uiError(msg);
        return 0;
    }
    if (d < minVal || d > maxVal)
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s must be between %.2f and %.2f.", fieldName, minVal, maxVal);
        uiError(msg);
        return 0;
    }
    *out = d;
    return 1;
}

/* Parse and range-check an int from string */
int validateInt(const char *val, int *out, int minVal, int maxVal, const char *fieldName)
{
    if (!validateNotEmpty(val, fieldName))
        return 0;
    char *end;
    long l = strtol(val, &end, 10);
    if (end == val || *end != '\0')
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s must be a whole number.", fieldName);
        uiError(msg);
        return 0;
    }
    if (l < minVal || l > maxVal)
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s must be between %d and %d.", fieldName, minVal, maxVal);
        uiError(msg);
        return 0;
    }
    *out = (int)l;
    return 1;
}

/* Plate: alphanumeric + dashes, 3-20 chars */
int validatePlate(const char *val)
{
    if (!validateNotEmpty(val, "Plate Number"))
        return 0;
    int len = (int)strlen(val);
    if (len < 3 || len > 20)
    {
        uiError("Plate number must be 3-20 characters.");
        return 0;
    }
    for (int i = 0; val[i]; i++)
    {
        if (!isalnum((unsigned char)val[i]) && val[i] != '-')
        {
            uiError("Plate number must contain only letters, numbers, and dashes.");
            return 0;
        }
    }
    return 1;
}

/* License: alphanumeric, 5-20 chars */
int validateLicense(const char *val)
{
    if (!validateNotEmpty(val, "License Number"))
        return 0;
    int len = (int)strlen(val);
    if (len < 5 || len > 20)
    {
        uiError("License number must be 5-20 characters.");
        return 0;
    }
    for (int i = 0; val[i]; i++)
    {
        if (!isalnum((unsigned char)val[i]) && val[i] != '-')
        {
            uiError("License number must contain only letters, numbers, and dashes.");
            return 0;
        }
    }
    return 1;
}

/* Contact: digits, +, dashes, spaces, 7-15 chars */
int validateContact(const char *val)
{
    if (!validateNotEmpty(val, "Contact Number"))
        return 0;
    int len = (int)strlen(val);
    if (len < 7 || len > 15)
    {
        uiError("Contact number must be 7-15 digits.");
        return 0;
    }
    for (int i = 0; val[i]; i++)
    {
        if (!isdigit((unsigned char)val[i]) && val[i] != '+' && val[i] != '-' && val[i] != ' ')
        {
            uiError("Contact number must contain only digits, +, or dashes.");
            return 0;
        }
    }
    return 1;
}

/* Username: alphanumeric + underscore, 3-30 chars */
int validateUsername(const char *val)
{
    if (!validateNotEmpty(val, "Username"))
        return 0;
    int len = (int)strlen(val);
    if (len < 3 || len > 30)
    {
        uiError("Username must be 3-30 characters.");
        return 0;
    }
    for (int i = 0; val[i]; i++)
    {
        if (!isalnum((unsigned char)val[i]) && val[i] != '_')
        {
            uiError("Username must contain only letters, numbers, and underscores.");
            return 0;
        }
    }
    return 1;
}

/* Password: at least 6 chars */
int validatePassword(const char *val)
{
    if (!validateNotEmpty(val, "Password"))
        return 0;
    if ((int)strlen(val) < 6)
    {
        uiError("Password must be at least 6 characters.");
        return 0;
    }
    return 1;
}

/* Prompt + read a double with retry loop */
int inputDouble(double *out, double minVal, double maxVal, const char *prompt)
{
    char buf[64];
    int attempts = 0;
    while (attempts < 3)
    {
        uiPrompt(prompt);
        safeInput(buf, sizeof(buf), NULL);
        if (validateDouble(buf, out, minVal, maxVal, prompt))
            return 1;
        attempts++;
        if (attempts < 3)
        {
            char msg[64];
            snprintf(msg, sizeof(msg), "Try again (%d/3)...", attempts + 1);
            uiWarn(msg);
        }
    }
    uiError("Too many invalid attempts. Cancelling.");
    return 0;
}

/* Prompt + read an int with retry loop */
int inputInt(int *out, int minVal, int maxVal, const char *prompt)
{
    char buf[32];
    int attempts = 0;
    while (attempts < 3)
    {
        uiPrompt(prompt);
        safeInput(buf, sizeof(buf), NULL);
        if (validateInt(buf, out, minVal, maxVal, prompt))
            return 1;
        attempts++;
        if (attempts < 3)
        {
            char msg[64];
            snprintf(msg, sizeof(msg), "Try again (%d/3)...", attempts + 1);
            uiWarn(msg);
        }
    }
    uiError("Too many invalid attempts. Cancelling.");
    return 0;
}

/* Prompt + read text, optionally allow empty */
void inputText(char *buf, int maxlen, const char *prompt, int allowEmpty)
{
    int attempts = 0;
    while (1)
    {
        uiPrompt(prompt);
        safeInput(buf, maxlen, NULL);
        if (allowEmpty || buf[0] != '\0')
            return;
        attempts++;
        if (attempts >= 3)
        {
            uiError("Field cannot be empty. Cancelling.");
            buf[0] = '\0';
            return;
        }
        uiWarn("This field cannot be empty. Try again.");
    }
}

/* ============= [DATE] DATE SYSTEM ============= */

void getTodayStr(char *buf)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    snprintf(buf, 16, "%04d-%02d-%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
}

int isLeapYear(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int daysInMonth(int m, int y)
{
    int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && isLeapYear(y))
        return 29;
    return d[m - 1];
}

/* Validate YYYY-MM-DD string */
int validateDateStr(const char *s)
{
    if (!s || strlen(s) != 10)
    {
        uiError("Date must be in YYYY-MM-DD format.");
        return 0;
    }
    if (s[4] != '-' || s[7] != '-')
    {
        uiError("Date must be in YYYY-MM-DD format.");
        return 0;
    }
    for (int i = 0; i < 10; i++)
        if (i != 4 && i != 7 && !isdigit((unsigned char)s[i]))
        {
            uiError("Date must be in YYYY-MM-DD format.");
            return 0;
        }
    int y = atoi(s), m = atoi(s + 5), d = atoi(s + 8);
    if (m < 1 || m > 12)
    {
        uiError("Month must be 01-12.");
        return 0;
    }
    if (d < 1 || d > daysInMonth(m, y))
    {
        uiError("Invalid day for that month.");
        return 0;
    }
    if (y < 2000 || y > 2100)
    {
        uiError("Year must be between 2000 and 2100.");
        return 0;
    }
    return 1;
}

/* Add N days to a YYYY-MM-DD date string */
void addDaysToDate(const char *startDate, int days, char *out)
{
    int y = atoi(startDate), m = atoi(startDate + 5), d = atoi(startDate + 8);
    d += days;
    while (d > daysInMonth(m, y))
    {
        d -= daysInMonth(m, y);
        m++;
        if (m > 12)
        {
            m = 1;
            y++;
        }
    }
    snprintf(out, 12, "%04d-%02d-%02d", y, m, d);
}

/* Days between two YYYY-MM-DD strings (positive = to is after from) */
int daysBetweenDates(const char *from, const char *to)
{
    struct tm tf = {0}, tt = {0};
    tf.tm_year = atoi(from) - 1900;
    tf.tm_mon = atoi(from + 5) - 1;
    tf.tm_mday = atoi(from + 8);
    tt.tm_year = atoi(to) - 1900;
    tt.tm_mon = atoi(to + 5) - 1;
    tt.tm_mday = atoi(to + 8);
    time_t tf_t = mktime(&tf), tt_t = mktime(&tt);
    return (int)((tt_t - tf_t) / 86400);
}

/* Method 1: Type date manually */
int inputDateTyped(char *buf, const char *prompt)
{
    char today[12];
    getTodayStr(today);
    for (int t = 0; t < 3; t++)
    {
        setColor(CLR_BRIGHT_CYAN);
        printf("\n  >> %s (Today: %s, format YYYY-MM-DD): ", prompt, today);
        resetColor();
        safeInput(buf, 12, NULL);
        /* Allow "today" shortcut */
        if (strcmp(buf, "today") == 0 || buf[0] == '\0')
        {
            strncpy(buf, today, 12);
            return 1;
        }
        if (validateDateStr(buf))
            return 1;
        if (t < 2)
            uiWarn("Try again.");
    }
    uiError("Too many invalid attempts.");
    return 0;
}

/* Method 2: Calendar picker */
int inputDateCalendar(char *buf, const char *prompt)
{
    char today[12];
    getTodayStr(today);
    int y = atoi(today), m = atoi(today + 5);

    static const char *months[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"};
    static const char *days_hdr = " Su Mo Tu We Th Fr Sa";

    uiSpacer();
    setColor(CLR_BRIGHT_YELLOW);
    printf("  === CALENDAR PICKER: %s ===\n", prompt);
    resetColor();
    uiWarn("Use commands: n=next month, p=prev month, +/-=year, or type a day number.");

    while (1)
    {
        uiSpacer();
        setColor(CLR_BRIGHT_YELLOW);
        printf("  %s %d\n", months[m - 1], y);
        resetColor();
        setColor(CLR_CYAN);
        printf("  %s\n", days_hdr);
        resetColor();

        /* Calculate first weekday (Zeller simplified) */
        struct tm first = {0};
        first.tm_year = y - 1900;
        first.tm_mon = m - 1;
        first.tm_mday = 1;
        mktime(&first);
        int startWday = first.tm_wday; /* 0=Sun */

        int dim = daysInMonth(m, y);
        int col = startWday;
        printf("  ");
        for (int i = 0; i < startWday; i++)
            printf("   ");
        for (int day = 1; day <= dim; day++)
        {
            /* Highlight today */
            int isToday = (day == atoi(today + 8) && m == atoi(today + 5) && y == atoi(today));
            if (isToday)
                setColor(CLR_BRIGHT_GREEN);
            printf("%3d", day);
            if (isToday)
                resetColor();
            col++;
            if (col % 7 == 0 && day != dim)
                printf("\n  ");
        }
        printf("\n");

        uiSpacer();
        setColor(CLR_BRIGHT_CYAN);
        printf("  >> Enter day (1-%d), n/p=month, +/-=year, q=cancel: ", dim);
        resetColor();

        char inp[16];
        safeInput(inp, sizeof(inp), NULL);

        if (strcmp(inp, "q") == 0)
            return 0;
        else if (strcmp(inp, "n") == 0)
        {
            m++;
            if (m > 12)
            {
                m = 1;
                y++;
            }
        }
        else if (strcmp(inp, "p") == 0)
        {
            m--;
            if (m < 1)
            {
                m = 12;
                y--;
            }
        }
        else if (strcmp(inp, "+") == 0)
            y++;
        else if (strcmp(inp, "-") == 0)
            y--;
        else
        {
            int day = atoi(inp);
            if (day >= 1 && day <= dim)
            {
                snprintf(buf, 12, "%04d-%02d-%02d", y, m, day);
                return 1;
            }
            uiError("Invalid day. Try again.");
        }
    }
}

/* Combined: ask user to type or use calendar */
int inputDate(char *buf, const char *prompt)
{
    uiSpacer();
    setColor(CLR_BRIGHT_MAGENTA);
    printf("  Date Input: %s\n", prompt);
    resetColor();
    uiOption(1, "Type date manually (YYYY-MM-DD)");
    uiOption(2, "Calendar picker");
    uiSpacer();
    uiPrompt("Choose (1-2): ");

    char ch[4];
    safeInput(ch, sizeof(ch), NULL);
    if (ch[0] == '2')
        return inputDateCalendar(buf, prompt);
    return inputDateTyped(buf, prompt);
}

/* Calculate late fee: hourly + daily components */
double calcLateFee(const char *expectedReturn, const char *actualReturn, double pricePerDay)
{
    int daysLate = daysBetweenDates(expectedReturn, actualReturn);
    if (daysLate <= 0)
        return 0.0;
    /* Daily penalty = 1.5x daily rate per overdue day */
    double dailyPenalty = pricePerDay * 1.5 * daysLate;
    /* Hourly penalty = PHP 50/hour for first 24hrs */
    double hourlyPenalty = (daysLate == 1) ? 50.0 * 24 : 50.0 * 24 * daysLate;
    /* Use whichever is higher (fairer to customer) */
    return (dailyPenalty > hourlyPenalty) ? dailyPenalty : hourlyPenalty;
}

/* Check all active rentals for approaching return date (within 1 day) */
void checkApproachingReturns()
{
    char today[12];
    getTodayStr(today);
    int warnings = 0;
    for (int i = 0; i < rentalCount; i++)
    {
        if (rentals[i].returned)
            continue;
        if (rentals[i].expectedReturnDate[0] == '\0')
            continue;
        int daysLeft = daysBetweenDates(today, rentals[i].expectedReturnDate);
        if (daysLeft <= 1 && daysLeft >= 0 && !rentals[i].warningIssued)
        {
            if (warnings == 0)
            {
                uiSpacer();
                uiRule(CLR_BRIGHT_YELLOW);
                uiCenterLine("RETURN DATE WARNINGS", CLR_BRIGHT_YELLOW);
                uiRule(CLR_BRIGHT_YELLOW);
            }
            setColor(CLR_BRIGHT_YELLOW);
            if (daysLeft == 0)
                printf("  [!] RENTAL #%d (%s) is due TODAY! Customer: %s\n",
                       rentals[i].id, rentals[i].model, rentals[i].customerName);
            else
                printf("  [~] RENTAL #%d (%s) is due TOMORROW. Customer: %s\n",
                       rentals[i].id, rentals[i].model, rentals[i].customerName);
            resetColor();
            rentals[i].warningIssued = 1;
            warnings++;
        }
        else if (daysLeft < 0 && !rentals[i].warningIssued)
        {
            setColor(CLR_BRIGHT_RED);
            printf("  [!] OVERDUE: RENTAL #%d (%s) was due %d day(s) ago! Customer: %s\n",
                   abs(daysLeft), rentals[i].model, rentals[i].id, rentals[i].customerName);
            resetColor();
            rentals[i].warningIssued = 1;
            warnings++;
        }
    }
    if (warnings > 0)
    {
        saveRentals();
        pressEnterToContinue();
    }
}

/* ============= DATABASE FUNCTIONS ============= */
void saveInventory()
{
    FILE *fp = fopen(DB_INVENTORY_FILE, "w");
    if (!fp)
        return;
    fprintf(fp, "# Inventory file\n");
    fprintf(fp, "CARCOUNT:%d\nNEXTID:%d\n", carCount, nextCarId);
    for (int i = 0; i < carCount; i++)
    {
        Car *c = &inventory[i];
        fprintf(fp, "ID|%d|BARCODE|%s|PLATE|%s|MODEL|%s|PRICE|%.2f|AVAIL|%d|RENTED|%d\n",
                c->id, c->barcode, c->plateNumber, c->model, c->pricePerDay, c->available, c->totalRentedCount);
    }
    fclose(fp);
}

void loadInventory()
{
    carCount = 0;
    nextCarId = 1001;
    FILE *fp = fopen(DB_INVENTORY_FILE, "r");
    if (!fp)
    {
        carCount = 0;
        return;
    }
    char line[1024];
    while (fgets(line, sizeof(line), fp))
    {
        if (strncmp(line, "NEXTID:", 7) == 0)
        {
            nextCarId = atoi(line + 7);
            if (nextCarId < 1001)
                nextCarId = 1001;
        }
        else if (strncmp(line, "ID|", 3) == 0)
        {
            Car c = {0};
            char tmp[1024];
            strncpy(tmp, line, sizeof(tmp) - 1);
            char *tok = strtok(tmp, "|");
            tok = strtok(NULL, "|");
            if (tok)
                c.id = atoi(tok);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                strncpy(c.barcode, tok, BARCODE_LEN - 1);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                strncpy(c.plateNumber, tok, PLATE_LEN - 1);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                strncpy(c.model, tok, MODEL_LEN - 1);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                c.pricePerDay = atof(tok);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                c.available = atoi(tok);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                c.totalRentedCount = atoi(tok);
            if (c.id >= nextCarId)
                nextCarId = c.id + 1;
            if (carCount < MAX_CARS)
                inventory[carCount++] = c;
        }
    }
    fclose(fp);
}

void saveCustomers()
{
    FILE *fp = fopen(DB_CUSTOMERS_FILE, "w");
    if (!fp)
        return;
    fprintf(fp, "# Customers file\n");
    fprintf(fp, "CUSTOMERS:%d\nNEXTID:%d\n", customerCount, nextCustomerId);
    for (int i = 0; i < customerCount; i++)
    {
        Customer *c = &customers[i];
        fprintf(fp, "ID|%d|NAME|%s|LICENSE|%s|CONTACT|%s|EMAIL|%s\n",
                c->id, c->name, c->licenseNo, c->contact, c->email);
    }
    fclose(fp);
}

void loadCustomers()
{
    customerCount = 0;
    nextCustomerId = 1;
    FILE *fp = fopen(DB_CUSTOMERS_FILE, "r");
    if (!fp)
        return;
    char line[1024];
    while (fgets(line, sizeof(line), fp))
    {
        if (strncmp(line, "NEXTID:", 7) == 0)
        {
            nextCustomerId = atoi(line + 7);
            if (nextCustomerId < 1)
                nextCustomerId = 1;
        }
        else if (strncmp(line, "ID|", 3) == 0)
        {
            Customer c = {0};
            char tmp[1024];
            strncpy(tmp, line, sizeof(tmp) - 1);
            char *tok = strtok(tmp, "|");
            tok = strtok(NULL, "|");
            if (tok)
                c.id = atoi(tok);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                strncpy(c.name, tok, NAME_LEN - 1);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                strncpy(c.licenseNo, tok, LICENSE_LEN - 1);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                strncpy(c.contact, tok, CONTACT_LEN - 1);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                strncpy(c.email, tok, EMAIL_LEN - 1);
            if (c.id >= nextCustomerId)
                nextCustomerId = c.id + 1;
            if (customerCount < MAX_CUSTOMERS)
                customers[customerCount++] = c;
        }
    }
    fclose(fp);
}

void saveRentals()
{
    FILE *fp = fopen(DB_RENTALS_FILE, "w");
    if (!fp)
        return;
    fprintf(fp, "# Rentals file\n");
    fprintf(fp, "RENTALS:%d\nNEXTID:%d\n", rentalCount, nextRentalId);
    for (int i = 0; i < rentalCount; i++)
    {
        RentalRecord *r = &rentals[i];
        fprintf(fp,
                "ID|%d|BARCODE|%s|PLATE|%s|MODEL|%s|CUST|%s|LICENSE|%s|QTY|%d|DAYS|%d|AMT|%.2f|RENTTIME|%ld|RETURNED|%d|RETURNTIME|%ld|LATEFEE|%.2f|STARTDATE|%s|EXPRETURN|%s|WARNED|%d\n",
                r->id, r->barcode, r->plateNumber, r->model, r->customerName, r->licenseNo,
                r->quantity, r->days, r->totalAmount, (long)r->rentTime, r->returned,
                (long)r->returnTime, r->lateFee,
                r->startDate[0] ? r->startDate : "N/A",
                r->expectedReturnDate[0] ? r->expectedReturnDate : "N/A",
                r->warningIssued);
    }
    fclose(fp);
}

void loadRentals()
{
    rentalCount = 0;
    nextRentalId = 1;
    FILE *fp = fopen(DB_RENTALS_FILE, "r");
    if (!fp)
        return;
    char line[2048];
    while (fgets(line, sizeof(line), fp))
    {
        if (strncmp(line, "NEXTID:", 7) == 0)
        {
            nextRentalId = atoi(line + 7);
            if (nextRentalId < 1)
                nextRentalId = 1;
        }
        else if (strncmp(line, "ID|", 3) == 0)
        {
            RentalRecord r = {0};
            char tmp[2048];
            strncpy(tmp, line, sizeof(tmp) - 1);
            char *tok = strtok(tmp, "|");
            tok = strtok(NULL, "|");
            if (tok)
                r.id = atoi(tok);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                strncpy(r.barcode, tok, BARCODE_LEN - 1);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                strncpy(r.plateNumber, tok, PLATE_LEN - 1);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                strncpy(r.model, tok, MODEL_LEN - 1);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                strncpy(r.customerName, tok, NAME_LEN - 1);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                strncpy(r.licenseNo, tok, LICENSE_LEN - 1);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                r.quantity = atoi(tok);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                r.days = atoi(tok);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                r.totalAmount = atof(tok);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                r.rentTime = (time_t)atol(tok);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                r.returned = atoi(tok);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                r.returnTime = (time_t)atol(tok);
            /* [UPGRADE] Load Late Fee */
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                r.lateFee = atof(tok);
            /* [DATE] Load date fields */
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok && strcmp(tok, "N/A") != 0)
                strncpy(r.startDate, tok, 11);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok && strcmp(tok, "N/A") != 0)
                strncpy(r.expectedReturnDate, tok, 11);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                r.warningIssued = atoi(tok);

            if (r.id >= nextRentalId)
                nextRentalId = r.id + 1;
            if (rentalCount < MAX_RENTALS)
                rentals[rentalCount++] = r;
        }
    }
    fclose(fp);
}

void saveUsers()
{
    FILE *fp = fopen(DB_USERS_FILE, "w");
    if (!fp)
        return;
    fprintf(fp, "# Users file\n");
    fprintf(fp, "USERS:%d\nNEXTID:%d\n", userCount, nextUserId);
    for (int i = 0; i < userCount; i++)
    {
        User *u = &users[i];
        fprintf(fp, "ID|%d|USERNAME|%s|PASSWORD|%s|ADMIN|%d\n",
                u->id, u->username, u->password, u->isAdmin);
    }
    fclose(fp);
}

void loadUsers()
{
    userCount = 0;
    nextUserId = 1;
    FILE *fp = fopen(DB_USERS_FILE, "r");
    if (!fp)
        return;
    char line[1024];
    while (fgets(line, sizeof(line), fp))
    {
        if (strncmp(line, "NEXTID:", 7) == 0)
        {
            nextUserId = atoi(line + 7);
            if (nextUserId < 1)
                nextUserId = 1;
        }
        else if (strncmp(line, "ID|", 3) == 0)
        {
            User u = {0};
            char tmp[1024];
            strncpy(tmp, line, sizeof(tmp) - 1);
            char *tok = strtok(tmp, "|");
            tok = strtok(NULL, "|");
            if (tok)
                u.id = atoi(tok);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                strncpy(u.username, tok, USERNAME_LEN - 1);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                strncpy(u.password, tok, PASSWORD_LEN - 1);
            tok = strtok(NULL, "|");
            tok = strtok(NULL, "|");
            if (tok)
                u.isAdmin = atoi(tok);
            if (u.id >= nextUserId)
                nextUserId = u.id + 1;
            if (userCount < MAX_USERS)
                users[userCount++] = u;
        }
    }
    fclose(fp);
}

/* ============= DEFAULT DATA ============= */
void addDefaultCars()
{
    struct
    {
        const char *plate;
        const char *model;
        double rate;
        int avail;
    } d[] = {
        {"CAR-001", "Toyota Vios", 1800, 60}, {"CAR-002", "Honda Civic", 2500, 60}, {"CAR-003", "Mitsubishi Mirage", 1600, 60}, {"CAR-004", "Ford Ranger", 3200, 60}, {"CAR-005", "Hyundai Tucson", 2800, 60}, {"CAR-006", "Kia Picanto", 1500, 60}, {"CAR-007", "Nissan Almera", 1700, 60}, {"CAR-008", "Suzuki Ertiga", 2000, 60}, {"CAR-009", "Toyota Fortuner", 3500, 60}, {"CAR-010", "Honda BR-V", 2700, 60}, {"CAR-011", "Mazda 3 Sedan", 2600, 60}, {"CAR-012", "Chevrolet Trailblazer", 3300, 60}, {"CAR-013", "Ford Everest", 3400, 60}, {"CAR-014", "Mitsubishi Montero", 3600, 60}, {"CAR-015", "Toyota Innova", 2800, 60}, {"CAR-016", "Suzuki Swift", 1800, 60}, {"CAR-017", "Nissan Terra", 3400, 60}, {"CAR-018", "Hyundai Accent", 1600, 60}, {"CAR-019", "Isuzu mu-X", 3700, 60}, {"CAR-020", "Toyota Hilux", 3200, 60}, {"CAR-021", "BMW 320i", 5500, 60}, {"CAR-022", "BMW X5", 7200, 60}, {"CAR-023", "Mercedes C200", 6000, 60}, {"CAR-024", "Mercedes GLC", 7200, 60}, {"CAR-025", "Audi A4", 5800, 60}, {"CAR-026", "Audi Q7", 7500, 60}, {"CAR-027", "Volkswagen Golf", 4000, 60}, {"CAR-028", "Volkswagen Touareg", 6900, 60}, {"CAR-029", "Porsche Macan", 8900, 60}, {"CAR-030", "Porsche Cayenne", 11000, 60}};
    int n = sizeof(d) / sizeof(d[0]);
    for (int i = 0; i < n && carCount < MAX_CARS; i++)
    {
        Car c = {0};
        c.id = nextCarId++;
        snprintf(c.barcode, BARCODE_LEN, "%d", c.id);
        strncpy(c.plateNumber, d[i].plate, PLATE_LEN - 1);
        strncpy(c.model, d[i].model, MODEL_LEN - 1);
        c.pricePerDay = d[i].rate;
        c.available = d[i].avail;
        c.totalRentedCount = 0;
        inventory[carCount++] = c;
    }
}

void addDefaultUsers()
{
    struct
    {
        const char *username;
        const char *password;
        int isAdmin;
    } d[] = {
        {"admin", "admin123", 1},
        {"cashier", "cashier123", 0}};
    int n = sizeof(d) / sizeof(d[0]);
    for (int i = 0; i < n && userCount < MAX_USERS; i++)
    {
        User u = {0};
        u.id = nextUserId++;
        strncpy(u.username, d[i].username, USERNAME_LEN - 1);
        strncpy(u.password, d[i].password, PASSWORD_LEN - 1);
        u.isAdmin = d[i].isAdmin;
        users[userCount++] = u;
    }
}

void initializeDefaultDataIfEmpty()
{
    if (!fileExists(DB_USERS_FILE) || userCount == 0)
    {
        if (!fileExists(DB_USERS_FILE))
        {
            FILE *fu = fopen(DB_USERS_FILE, "w");
            if (fu)
            {
                fprintf(fu, "# Users file\nUSERS:0\nNEXTID:1\n");
                fclose(fu);
            }
        }
        if (userCount == 0)
        {
            addDefaultUsers();
            saveUsers();
        }
    }
    if (!fileExists(DB_INVENTORY_FILE) || carCount == 0)
    {
        carCount = 0;
        nextCarId = 1001;
        addDefaultCars();
        saveInventory();
    }
    if (!fileExists(DB_CUSTOMERS_FILE) || customerCount == 0)
    {
        customerCount = 0;
        nextCustomerId = 1;
        saveCustomers();
    }
    if (!fileExists(DB_RENTALS_FILE) || rentalCount == 0)
    {
        rentalCount = 0;
        nextRentalId = 1;
        saveRentals();
    }
    ensureReceiptsDir();
}

void initializeSystem()
{
    enableTerminalColors();
    loadConfig(); /* [UPGRADE] Load Config */
#ifndef NO_CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
    loadUsers();
    loadInventory();
    loadCustomers();
    loadRentals();
    initializeDefaultDataIfEmpty();
    migratePasswordsToHash(); /* [SECURITY] Auto-migrate plain passwords to SHA-256 */
}

/* ============= SEARCH FUNCTIONS ============= */
int findCarByBarcode(const char *barcode)
{
    for (int i = 0; i < carCount; i++)
        if (strcmp(inventory[i].barcode, barcode) == 0)
            return i;
    return -1;
}

int findCarByPlate(const char *plate)
{
    for (int i = 0; i < carCount; i++)
        if (STRICMP(inventory[i].plateNumber, plate) == 0)
            return i;
    return -1;
}

int findCustomerByLicense(const char *license)
{
    for (int i = 0; i < customerCount; i++)
        if (STRICMP(customers[i].licenseNo, license) == 0)
            return i;
    return -1;
}

int findCustomerByName(const char *name)
{
    for (int i = 0; i < customerCount; i++)
        if (STRICMP(customers[i].name, name) == 0)
            return i;
    return -1;
}

int findUserByUsername(const char *username)
{
    for (int i = 0; i < userCount; i++)
        if (strcmp(users[i].username, username) == 0)
            return i;
    return -1;
}

/* ============= EMAIL FUNCTIONS ============= */
#ifndef NO_CURL
size_t curl_payload_read(void *ptr, size_t size, size_t nmemb, void *userp)
{
    const char **payload_text = (const char **)userp;
    const char *data = *payload_text;
    if (!data)
        return 0;
    size_t room = size * nmemb;
    size_t len = strlen(data);
    if (len > room)
        len = room;
    memcpy(ptr, data, len);
    *payload_text += len;
    return len;
}

int sendEmailLibCurl(const char *toAddress, const char *subject, const char *body)
{
    if (!toAddress || strlen(toAddress) < 5)
        return 0;
    if (strlen(CONFIG_PASSWORD) < 5 ||
        strcmp(CONFIG_PASSWORD, "your_app_password_here") == 0 ||
        strcmp(CONFIG_PASSWORD, "your_16char_app_password") == 0)
    {
        setColor(CLR_RED);
        printf("Email Error: Please update PASSWORD in config.txt\n");
        resetColor();
        return 0;
    }

    /* Build email payload */
    char payload[9000];
    char fromHeader[256];
    snprintf(fromHeader, sizeof(fromHeader), "FOURSIGHT CAR RENTALS <%s>", CONFIG_EMAIL);
    snprintf(payload, sizeof(payload),
             "From: %s\r\n"
             "To: <%s>\r\n"
             "Subject: %s\r\n"
             "MIME-Version: 1.0\r\n"
             "Content-Type: text/plain; charset=utf-8\r\n"
             "\r\n"
             "%s\r\n",
             fromHeader, toAddress, subject, body);

    const char *payload_ptr = payload;
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        setColor(CLR_RED);
        printf("Email Error: Could not initialize curl.\n");
        resetColor();
        return 0;
    }

    char mailfrom[256], rcpt[256];
    snprintf(mailfrom, sizeof(mailfrom), "<%s>", CONFIG_EMAIL);
    snprintf(rcpt,     sizeof(rcpt),     "<%s>", toAddress);

    struct curl_slist *recipients = NULL;
    recipients = curl_slist_append(recipients, rcpt);

    /* smtps port 465 - confirmed working via CMD curl test */
    curl_easy_setopt(curl, CURLOPT_URL,                 "smtps://smtp.gmail.com:465");
    curl_easy_setopt(curl, CURLOPT_USE_SSL,             (long)CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,      0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,      0L);
    curl_easy_setopt(curl, CURLOPT_USERNAME,            CONFIG_EMAIL);
    curl_easy_setopt(curl, CURLOPT_PASSWORD,            CONFIG_PASSWORD);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM,           mailfrom);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT,           recipients);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION,        curl_payload_read);
    curl_easy_setopt(curl, CURLOPT_READDATA,            &payload_ptr);
    curl_easy_setopt(curl, CURLOPT_UPLOAD,              1L);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,    (curl_off_t)strlen(payload));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,      15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,             45L);
    curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 0L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK)
    {
        setColor(CLR_BRIGHT_GREEN);
        printf("  Email sent successfully to %s\n", toAddress);
        resetColor();
        return 1;
    }

    setColor(CLR_RED);
    printf("  Email failed: %s\n", curl_easy_strerror(res));
    printf("  Make sure Windscribe VPN is ON then try again.\n");
    resetColor();
    return 0;
}
#else
/* Stub: email not available without libcurl */
size_t curl_payload_read(void *ptr, size_t size, size_t nmemb, void *userp)
{
    (void)ptr;
    (void)size;
    (void)nmemb;
    (void)userp;
    return 0;
}
int sendEmailLibCurl(const char *toAddress, const char *subject, const char *body)
{
    (void)toAddress;
    (void)subject;
    (void)body;
    setColor(CLR_YELLOW);
    printf("Email feature not available (libcurl not installed).\n");
    resetColor();
    return 0;
}
#endif /* NO_CURL */

/* ============= BARCODE FUNCTIONS ============= */
void printAsciiBarcode(const char *code, FILE *out)
{
    const char *patterns[10] = {
        "|| ||",    // 0
        "| |||",    // 1
        "||| |",    // 2
        "|| |||",   // 3
        "| || ||",  // 4
        "|||| ",    // 5
        " ||||",    // 6
        "||| ||",   // 7
        "|| || ||", // 8
        "|||||"     // 9
    };
    fprintf(out, "\nBARCODE VISUAL:\n");
    for (int i = 0; code[i]; i++)
    {
        if (isdigit((unsigned char)code[i]))
        {
            int d = code[i] - '0';
            fprintf(out, "%s ", patterns[d]);
        }
        else
        {
            fprintf(out, "%c ", code[i]);
        }
    }
    fprintf(out, "\n%s\n", code);
}

void generateBarcodePNG(const char *code, const char *outputFile)
{
    if (!code || !outputFile)
        return;
    char command[512];
    snprintf(command, sizeof(command), "zint -o \"%s\" -d \"%s\" --barcode=20 --scale=3 --quiet", outputFile, code);
    int rc = system(command);
    if (rc != 0)
    {
        setColor(CLR_YELLOW);
        printf("Note: Zint barcode generation returned %d. Make sure zint is in PATH.\n", rc);
        resetColor();
    }
}

void autoOpenFile(const char *filename)
{
#ifdef _WIN32
    if (fileExists(filename))
        ShellExecuteA(NULL, "open", filename, NULL, NULL, SW_SHOWNORMAL);
#endif
}

/* ============= RECEIPT FUNCTIONS ============= */
void autoPrintReceipt(const char *filename)
{
#ifdef _WIN32
    char command[512];
    snprintf(command, sizeof(command), "notepad /p \"%s\"", filename);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, 3000);
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
#else
    /* [UPGRADE] Linux Printing Support */
    char command[512];
    snprintf(command, sizeof(command), "lp \"%s\"", filename);
    system(command);
#endif
}

void printReceiptFromRental(const RentalRecord *r)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    ensureReceiptsDir();
    char filename[FILENAME_LEN];
    snprintf(filename, sizeof(filename), "%s/receipt_%d.txt", RECEIPTS_DIR, r->id);
    FILE *fp = fopen(filename, "w");
    if (!fp)
        return;
    fprintf(fp, "========================================\n");
    fprintf(fp, "  █───────  ██████  █   █ ██████\n");
    fprintf(fp, "  █═══════ █══════█ █   █ █══════█\n");
    fprintf(fp, "  █████    █   █   █   █ █████═══\n");
    fprintf(fp, "  █════    █   █   █   █ █══════█\n");
    fprintf(fp, "  █        █████═══ █████═══ █  █\n");
    fprintf(fp, "  ╚════       ╚══════   ╚══════ ╚══╚\n");
    fprintf(fp, "      FOURSIGHT CAR RENTALS\n");
    fprintf(fp, "========================================\n");
    fprintf(fp, "Date: %02d/%02d/%04d  Time: %02d:%02d:%02d\n",
            t->tm_mday, t->tm_mon + 1, t->tm_year + 1900, t->tm_hour, t->tm_min, t->tm_sec);
    fprintf(fp, "Rental ID: %d\nCustomer: %s\nLicense: %s\n", r->id, r->customerName, r->licenseNo);
    fprintf(fp, "Car: %s (%s)\nBarcode: %s\n", r->model, r->plateNumber, r->barcode);
    printAsciiBarcode(r->barcode, fp);
    fprintf(fp, "Qty: %d  Days: %d\n", r->quantity, r->days);
    fprintf(fp, "Total: PHP %.2f\n", r->totalAmount);
    if (r->lateFee > 0)
    {
        fprintf(fp, "Late Fee: PHP %.2f\n", r->lateFee);
        fprintf(fp, "Grand Total: PHP %.2f\n", r->totalAmount + r->lateFee);
    }
    fprintf(fp, "Returned: %s\n", r->returned ? "Yes" : "No");
    fprintf(fp, "========================================\n");
    fclose(fp);
    setColor(CLR_YELLOW);
    printf("Receipt saved to %s\n", filename);
    resetColor();
    autoPrintReceipt(filename);
}

int saveSimpleReceipt(int rentalId, const char *customerName, const char *license,
                      const char *model, const char *plate, const char *barcode,
                      int qty, int days, double total)
{
    ensureReceiptsDir();
    char txtfile[FILENAME_LEN];
    snprintf(txtfile, sizeof(txtfile), "%s/receipt_simple_%d.txt", RECEIPTS_DIR, rentalId);
    FILE *fp = fopen(txtfile, "w");
    if (!fp)
        return 0;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(fp, "========================================\n");
    fprintf(fp, "  █───────  ██████  █   █ ██████\n");
    fprintf(fp, "  █═══════ █══════█ █   █ █══════█\n");
    fprintf(fp, "  █████    █   █   █   █ █████═══\n");
    fprintf(fp, "  █════    █   █   █   █ █══════█\n");
    fprintf(fp, "  █        █████═══ █████═══ █  █\n");
    fprintf(fp, "  ╚════       ╚══════   ╚══════ ╚══╚\n");
    fprintf(fp, "      FOURSIGHT CAR RENTALS\n");
    fprintf(fp, "========================================\n");
    fprintf(fp, "Date: %02d/%02d/%04d %02d:%02d:%02d\n",
            t->tm_mday, t->tm_mon + 1, t->tm_year + 1900,
            t->tm_hour, t->tm_min, t->tm_sec);
    fprintf(fp, "Rental ID: %d\n", rentalId);
    fprintf(fp, "Customer: %s\nLicense: %s\n", customerName, license);
    fprintf(fp, "Car: %s (%s)\n", model, plate);
    fprintf(fp, "Barcode: %s\n", barcode);
    fprintf(fp, "Qty: %d  Days: %d\n", qty, days);
    fprintf(fp, "Total: PHP %.2f\n", total);
    fprintf(fp, "========================================\n");
    printAsciiBarcode(barcode, fp);
    fclose(fp);
    autoPrintReceipt(txtfile);
    setColor(CLR_YELLOW);
    printf("Saved receipt: %s\n", txtfile);
    resetColor();
    return 1;
}

/* NEW FUNCTION: Save bulk receipt with multiple items */
void saveBulkReceipt(int transactionId,
                     const char *custName,
                     const char *custLicense,
                     const char *custContact,
                     const char *custEmail,
                     const char *cashierName,
                     RentalItem items[],
                     int itemCount,
                     double total,
                     double cash,
                     double change,
                     int firstRentalId,
                     int lastRentalId)
{
    ensureReceiptsDir();
    char filename[FILENAME_LEN];
    snprintf(filename, sizeof(filename), "%s/receipt_bulk_%d.txt", RECEIPTS_DIR, transactionId);
    FILE *fp = fopen(filename, "w");
    if (!fp)
        return;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    time_t returnTime = now + (24 * 60 * 60); // 24 hours from now
    /* Print header similar to your first picture */
    fprintf(fp, "FOURSIGHT CAR RENTALS\n");
    fprintf(fp, "Date: %02d/%02d/%04d %02d:%02d:%02d\n",
            t->tm_mday, t->tm_mon + 1, t->tm_year + 1900,
            t->tm_hour, t->tm_min, t->tm_sec);
    fprintf(fp, "Cashier: %s\n", cashierName);
    fprintf(fp, "--- Customer: %s\n", custName);
    fprintf(fp, "License: %s\n", custLicense);
    fprintf(fp, "Contact: %s\n", custContact);
    fprintf(fp, "Email: %s\n", custEmail);
    fprintf(fp, "---\n");
    fprintf(fp, "=== RENTAL ITEMS ===\n");
    fprintf(fp, "Car Model    Plate    Qty    Days   Subtotal\n");
    fprintf(fp, "---\n");
    /* Print each rental item */
    for (int i = 0; i < itemCount; i++)
    {
        Car *car = &inventory[items[i].carIndex];
        fprintf(fp, "%-20s %-10s %-6d %-6d PHP %-8.2f\n",
                car->model, car->plateNumber,
                items[i].quantity, items[i].days,
                items[i].subtotal);
    }
    fprintf(fp, "\n");
    fprintf(fp, "TOTAL AMOUNT:    PHP    %.2f\n", total);
    fprintf(fp, "CASH PAID:    PHP    %.2f\n", cash);
    fprintf(fp, "CHANGE:    PHP    %.2f\n", change);
    fprintf(fp, "===========\n");
    fprintf(fp, "Rental IDs: %d to %d\n", firstRentalId, lastRentalId);
    fprintf(fp, "Total Items Rented: %d\n", itemCount);
    fprintf(fp, "Thank you for choosing FOURSIGHT Car Rentals!\n");
    fprintf(fp, "Please return all cars by: %s", ctime(&returnTime));
    fprintf(fp, "============================\n");
    /* Generate and list barcodes for each rental item */
    fprintf(fp, "=== BARCODES FOR EACH RENTAL ===\n");
    for (int i = 0; i < itemCount; i++)
    {
        Car *car = &inventory[items[i].carIndex];
        fprintf(fp, "Rental ID %d: %s - %s\n",
                firstRentalId + i, car->model, car->barcode);
        printAsciiBarcode(car->barcode, fp);
        fprintf(fp, "\n");
    }
    fclose(fp);
    /* Generate barcode PNGs for each rental item */
    autoPrintReceipt(filename);
    setColor(CLR_GREEN);
    printf("Bulk receipt saved: %s\n", filename);
    printf("Generated %d individual barcode PNGs\n", itemCount);
    resetColor();
}

void reprintReceiptMenu()
{
    if (rentalCount == 0)
    {
        printf("No rentals found.\n");
        pressEnterToContinue();
        return;
    }
    printf("\n==== RENTAL LIST ====\n");
    printf("%-5s %-20s %-12s %-10s\n", "No.", "Customer", "License", "Returned");
    printf("----------------------------------------\n");
    for (int i = 0; i < rentalCount; i++)
    {
        printf("%-5d %-20s %-12s %-10s\n",
               i + 1,
               rentals[i].customerName,
               rentals[i].licenseNo,
               rentals[i].returned ? "YES" : "NO");
    }
    printf("\nSelect rental number (0 to cancel): ");
    int choice;
    if (scanf("%d", &choice) != 1)
    {
        flushInput();
        return;
    }
    flushInput();
    if (choice <= 0 || choice > rentalCount)
        return;
    printReceiptFromRental(&rentals[choice - 1]);
    pressEnterToContinue();
}

/* ============= [SECURITY] SHA-256 IMPLEMENTATION ============= */
/* Pure C SHA-256 - no external library needed */
typedef struct
{
    unsigned int state[8];
    unsigned long long count;
    unsigned char buf[64];
} SHA256_CTX;

static const unsigned int K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static void sha256_transform(SHA256_CTX *ctx, const unsigned char *data)
{
    unsigned int a, b, c, d, e, f, g, h, t1, t2, m[64];
    int i;
    for (i = 0; i < 16; i++)
        m[i] = ((unsigned int)data[i * 4] << 24) | ((unsigned int)data[i * 4 + 1] << 16) |
               ((unsigned int)data[i * 4 + 2] << 8) | (unsigned int)data[i * 4 + 3];
    for (; i < 64; i++)
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];
    for (i = 0; i < 64; i++)
    {
        t1 = h + EP1(e) + CH(e, f, g) + K[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx)
{
    ctx->count = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(SHA256_CTX *ctx, const unsigned char *data, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++)
    {
        ctx->buf[ctx->count % 64] = data[i];
        ctx->count++;
        if (ctx->count % 64 == 0)
            sha256_transform(ctx, ctx->buf);
    }
}

static void sha256_final(SHA256_CTX *ctx, unsigned char *hash)
{
    unsigned long long count = ctx->count;
    unsigned char len_bytes[8];
    unsigned long long bit_count = count * 8;
    ctx->buf[count % 64] = 0x80;
    count++;
    if (count % 64 > 56)
    {
        while (count % 64 != 0)
        {
            ctx->buf[count % 64] = 0x00;
            count++;
        }
        sha256_transform(ctx, ctx->buf);
    }
    while (count % 64 != 56)
    {
        ctx->buf[count % 64] = 0x00;
        count++;
    }
    len_bytes[0] = (unsigned char)(bit_count >> 56);
    len_bytes[1] = (unsigned char)(bit_count >> 48);
    len_bytes[2] = (unsigned char)(bit_count >> 40);
    len_bytes[3] = (unsigned char)(bit_count >> 32);
    len_bytes[4] = (unsigned char)(bit_count >> 24);
    len_bytes[5] = (unsigned char)(bit_count >> 16);
    len_bytes[6] = (unsigned char)(bit_count >> 8);
    len_bytes[7] = (unsigned char)(bit_count);
    memcpy(ctx->buf + 56, len_bytes, 8);
    sha256_transform(ctx, ctx->buf);
    int i;
    for (i = 0; i < 8; i++)
    {
        hash[i * 4] = (ctx->state[i] >> 24) & 0xff;
        hash[i * 4 + 1] = (ctx->state[i] >> 16) & 0xff;
        hash[i * 4 + 2] = (ctx->state[i] >> 8) & 0xff;
        hash[i * 4 + 3] = ctx->state[i] & 0xff;
    }
}

/* Hash a string and return hex string in out (must be SHA256_HASH_LEN bytes) */
void sha256_hex(const char *input, char *out)
{
    SHA256_CTX ctx;
    unsigned char hash[32];
    sha256_init(&ctx);
    sha256_update(&ctx, (const unsigned char *)input, strlen(input));
    sha256_final(&ctx, hash);
    int i;
    for (i = 0; i < 32; i++)
        snprintf(out + i * 2, 3, "%02x", hash[i]);
    out[64] = '\0';
}

/* Returns 1 if password matches stored hash or plain (migration) */
int verifyPassword(const char *stored, const char *input)
{
    if (strncmp(stored, HASHED_FLAG, strlen(HASHED_FLAG)) == 0)
    {
        /* stored is hashed — compare hash of input */
        char inputHash[SHA256_HASH_LEN];
        sha256_hex(input, inputHash);
        return strcmp(stored + strlen(HASHED_FLAG), inputHash) == 0;
    }
    /* plain text (legacy) — direct compare */
    return strcmp(stored, input) == 0;
}

/* Hash all plain-text passwords in users array and save (one-time migration) */
void migratePasswordsToHash()
{
    int migrated = 0;
    int i;
    for (i = 0; i < userCount; i++)
    {
        if (strncmp(users[i].password, HASHED_FLAG, strlen(HASHED_FLAG)) != 0)
        {
            /* Plain text — hash it */
            char hash[SHA256_HASH_LEN];
            sha256_hex(users[i].password, hash);
            char newPass[PASSWORD_LEN];
            snprintf(newPass, PASSWORD_LEN, "%s%s", HASHED_FLAG, hash);
            strncpy(users[i].password, newPass, PASSWORD_LEN - 1);
            migrated++;
        }
    }
    if (migrated > 0)
    {
        saveUsers();
        setColor(CLR_GREEN);
        printf("[SECURITY] %d password(s) migrated to SHA-256 hashing.\n", migrated);
        resetColor();
    }
}

/* ============= [SECURITY] LOGIN LOCKOUT ============= */
LoginAttempt *getLockoutRecord(const char *username)
{
    int i;
    for (i = 0; i < loginAttemptCount; i++)
        if (strcmp(loginAttempts[i].username, username) == 0)
            return &loginAttempts[i];
    if (loginAttemptCount < MAX_USERS)
    {
        strncpy(loginAttempts[loginAttemptCount].username, username, USERNAME_LEN - 1);
        loginAttempts[loginAttemptCount].attempts = 0;
        loginAttempts[loginAttemptCount].lockoutUntil = 0;
        return &loginAttempts[loginAttemptCount++];
    }
    return NULL;
}

int isLockedOut(const char *username)
{
    LoginAttempt *rec = getLockoutRecord(username);
    if (!rec)
        return 0;
    if (rec->lockoutUntil == 0)
        return 0;
    if (time(NULL) < rec->lockoutUntil)
        return 1;
    /* Lockout expired — reset */
    rec->attempts = 0;
    rec->lockoutUntil = 0;
    return 0;
}

void recordFailedAttempt(const char *username)
{
    LoginAttempt *rec = getLockoutRecord(username);
    if (!rec)
        return;
    rec->attempts++;
    if (rec->attempts >= MAX_LOGIN_ATTEMPTS)
    {
        rec->lockoutUntil = time(NULL) + LOCKOUT_SECONDS;
        setColor(CLR_RED);
        printf("Account locked for %d minutes due to too many failed attempts.\n",
               LOCKOUT_SECONDS / 60);
        resetColor();
    }
    else
    {
        setColor(CLR_YELLOW);
        printf("Warning: %d of %d attempts used.\n", rec->attempts, MAX_LOGIN_ATTEMPTS);
        resetColor();
    }
}

void resetAttempts(const char *username)
{
    LoginAttempt *rec = getLockoutRecord(username);
    if (rec)
    {
        rec->attempts = 0;
        rec->lockoutUntil = 0;
    }
}

/* ============= LOGIN FUNCTION ============= */
int login(char *loggedUser, int *isAdmin)
{
    char username[USERNAME_LEN], password[PASSWORD_LEN];
    int roleChoice = 0;
    clearScreen();
    roleChoice = showLoginHeader();
    if (roleChoice == 0)
        return 0;
    printBanner();
    printf("\nType 'exit' as username to quit.\n");
    printf("Username: ");
    if (scanf("%39s", username) != 1)
        return 0;
    if (strcmp(username, "exit") == 0)
        return -1;
    printf("Password: ");
#ifdef _WIN32
    int i = 0;
    char ch;
    while (i < PASSWORD_LEN - 1)
    {
        ch = _getch();
        if (ch == '\r' || ch == '\n')
            break;
        if (ch == '\b')
        {
            if (i > 0)
            {
                i--;
                printf("\b \b");
            }
        }
        else if (ch >= 32 && ch <= 126)
        {
            password[i++] = ch;
            putchar('*');
        }
    }
    password[i] = '\0';
    printf("\n");
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int i = 0;
    int ch;
    while (i < PASSWORD_LEN - 1 && (ch = getchar()) != '\n' && ch != EOF)
    {
        if (ch == 127)
        {
            if (i > 0)
            {
                i--;
                printf("\b \b");
            }
        }
        else
        {
            password[i++] = ch;
            putchar('*');
        }
    }
    password[i] = '\0';
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");
#endif
    flushInput();
    int idx = findUserByUsername(username);
    if (idx == -1)
    {
        recordFailedAttempt(username);
        return 0;
    }

    /* [SECURITY] Check lockout */
    if (isLockedOut(username))
    {
        LoginAttempt *rec = getLockoutRecord(username);
        long remaining = (long)(rec->lockoutUntil - time(NULL));
        setColor(CLR_RED);
        printf("Account is locked. Try again in %ld minute(s) and %ld second(s).\n",
               remaining / 60, remaining % 60);
        resetColor();
        return 0;
    }

    if (roleChoice == 1 && users[idx].isAdmin == 0)
    {
        setColor(CLR_RED);
        printf("Selected Admin but account is not admin.\n");
        resetColor();
        return 0;
    }
    if (roleChoice == 2 && users[idx].isAdmin == 1)
    {
        setColor(CLR_RED);
        printf("Selected Cashier but account is admin.\n");
        resetColor();
        return 0;
    }

    /* [SECURITY] Use hash-aware password verification */
    if (verifyPassword(users[idx].password, password))
    {
        resetAttempts(username);
        strncpy(loggedUser, username, USERNAME_LEN - 1);
        *isAdmin = users[idx].isAdmin;
        return 1;
    }
    recordFailedAttempt(username);
    return 0;
}

/* ============= INVENTORY FUNCTIONS ============= */
void showInventoryListDetailed()
{
    clearScreen();
    setColor(CLR_CYAN);
    printf("\n========================================\n");
    printf("            CAR INVENTORY\n");
    printf("========================================\n");
    resetColor();
    if (carCount == 0)
    {
        setColor(CLR_YELLOW);
        printf("No cars in inventory.\n");
        resetColor();
        return;
    }
    printf("%-6s %-10s %-28s %-12s %-10s\n", "ID", "Plate", "Model", "Rate/Day", "Available");
    printf("-------------------------------------------------------------------------------\n");
    for (int i = 0; i < carCount; i++)
    {
        printf("%-6d %-10s %-28s PHP %-8.2f %8d\n",
               inventory[i].id,
               inventory[i].plateNumber,
               inventory[i].model,
               inventory[i].pricePerDay,
               inventory[i].available);
        printAsciiBarcode(inventory[i].barcode, stdout);
        printf("-------------------------------------------------------------------------------\n");
    }
}

void viewInventoryInteractive()
{
    int ch;
    do
    {
        showInventoryListDetailed();
        printf("\nInventory Menu:\n1. Add Car\n2. Edit Car\n3. Delete Car\n4. Search Car\n5. Back\nChoice: ");
        if (scanf("%d", &ch) != 1)
        {
            flushInput();
            printf("Invalid input.\n");
            pressEnterToContinue();
            continue;
        }
        flushInput();
        if (ch == 1)
        {
            if (carCount >= MAX_CARS)
            {
                uiError("Inventory is full.");
                pressEnterToContinue();
                continue;
            }
            Car c = {0};
            c.id = nextCarId++;

            /* [VALIDATION] Plate */
            char platebuf[PLATE_LEN];
            int plateOk = 0;
            for (int t = 0; t < 3; t++)
            {
                inputText(platebuf, PLATE_LEN, "Plate Number: ", 0);
                if (!validatePlate(platebuf))
                {
                    uiWarn("Try again.");
                    continue;
                }
                if (findCarByPlate(platebuf) != -1)
                {
                    uiError("A car with that plate already exists.");
                    continue;
                }
                strncpy(c.plateNumber, platebuf, PLATE_LEN - 1);
                plateOk = 1;
                break;
            }
            if (!plateOk)
            {
                nextCarId--;
                pressEnterToContinue();
                continue;
            }

            /* [VALIDATION] Model */
            inputText(c.model, MODEL_LEN, "Car Model: ", 0);
            if (c.model[0] == '\0')
            {
                nextCarId--;
                continue;
            }

            /* [VALIDATION] Price */
            if (!inputDouble(&c.pricePerDay, 1.0, 999999.0, "Price Per Day (PHP): "))
            {
                nextCarId--;
                pressEnterToContinue();
                continue;
            }

            /* [VALIDATION] Units */
            if (!inputInt(&c.available, 1, 9999, "Available Units: "))
            {
                nextCarId--;
                pressEnterToContinue();
                continue;
            }

            c.totalRentedCount = 0;
            snprintf(c.barcode, BARCODE_LEN, "%d", c.id);
            inventory[carCount++] = c;
            saveInventory();
            ensureReceiptsDir();
            char pngfile[FILENAME_LEN];
            snprintf(pngfile, sizeof(pngfile),
                     "%s/car_barcode_%s.png", RECEIPTS_DIR, c.barcode);
            generateBarcodePNG(c.barcode, pngfile);
            uiSuccess("Car added successfully.");
            printf("\n  ID: %d  Plate: %s  Model: %s  Price: PHP %.2f  Avail: %d\n",
                   c.id, c.plateNumber, c.model, c.pricePerDay, c.available);
            printAsciiBarcode(c.barcode, stdout);
            pressEnterToContinue();
        }
        else if (ch == 2)
        {
            char key[BARCODE_LEN];
            safeInput(key, BARCODE_LEN, "Enter plate or barcode to edit: ");
            int idx = findCarByBarcode(key);
            if (idx == -1)
                idx = findCarByPlate(key);
            if (idx == -1)
            {
                setColor(CLR_RED);
                printf("Car not found.\n");
                resetColor();
                pressEnterToContinue();
                continue;
            }
            Car *p = &inventory[idx];
            printf("Editing %s (%s)\n", p->model, p->plateNumber);
            char tmp[128];
            safeInput(tmp, PLATE_LEN, "New plate (leave empty to keep): ");
            if (tmp[0])
                strncpy(p->plateNumber, tmp, PLATE_LEN - 1);
            safeInput(tmp, MODEL_LEN, "New model (leave empty to keep): ");
            if (tmp[0])
                strncpy(p->model, tmp, MODEL_LEN - 1);
            printf("New price per day (0 to keep): ");
            double pr;
            if (scanf("%lf", &pr) == 1)
            {
                if (pr > 0)
                    p->pricePerDay = pr;
            }
            flushInput();
            printf("New available units (negative to keep): ");
            int av;
            if (scanf("%d", &av) == 1)
            {
                if (av >= 0)
                    p->available = av;
            }
            flushInput();
            saveInventory();
            setColor(CLR_GREEN);
            printf("Car updated.\n");
            resetColor();
            printf("\nUpdated Car:\n");
            printf("ID: %d Plate: %s Model: %s Price: PHP %.2f Avail: %d\n",
                   p->id, p->plateNumber, p->model, p->pricePerDay, p->available);
            printAsciiBarcode(p->barcode, stdout);
            pressEnterToContinue();
        }
        else if (ch == 3)
        {
            char key[BARCODE_LEN];
            safeInput(key, BARCODE_LEN, "Enter plate or barcode to delete: ");
            int idx = findCarByBarcode(key);
            if (idx == -1)
                idx = findCarByPlate(key);
            if (idx == -1)
            {
                setColor(CLR_RED);
                printf("Car not found.\n");
                resetColor();
                pressEnterToContinue();
                continue;
            }
            printf("Are you sure you want to delete %s (%s)? (y/n): ",
                   inventory[idx].model, inventory[idx].plateNumber);
            char c = getchar();
            flushInput();
            if (c == 'y' || c == 'Y')
            {
                for (int i = idx; i < carCount - 1; i++)
                    inventory[i] = inventory[i + 1];
                carCount--;
                saveInventory();
                setColor(CLR_GREEN);
                printf("Car deleted.\n");
                resetColor();
            }
            else
            {
                printf("Delete cancelled.\n");
            }
            pressEnterToContinue();
        }
        else if (ch == 4)
        {
            char key[BARCODE_LEN];
            safeInput(key, BARCODE_LEN, "Enter plate or barcode to search: ");
            int idx = findCarByBarcode(key);
            if (idx == -1)
                idx = findCarByPlate(key);
            if (idx == -1)
            {
                setColor(CLR_YELLOW);
                printf("No matching car found.\n");
                resetColor();
                pressEnterToContinue();
                continue;
            }
            Car *p = &inventory[idx];
            setColor(CLR_CYAN);
            printf("\nFound Car:\n");
            resetColor();
            printf("ID: %d\nBarcode: %s\nPlate: %s\nModel: %s\nPrice/Day: PHP %.2f\nAvailable: %d\nTotal Rented: %d\n",
                   p->id, p->barcode, p->plateNumber, p->model, p->pricePerDay, p->available, p->totalRentedCount);
            printAsciiBarcode(p->barcode, stdout);
            pressEnterToContinue();
        }
        else if (ch == 5)
            break;
        else
        {
            printf("Invalid choice.\n");
            pressEnterToContinue();
        }
    } while (1);
}

void showQuickInventory()
{
    clearScreen();
    printf("\n=== Quick Inventory ===\n");
    printf("%-6s %-10s %-25s %-10s\n", "ID", "Plate", "Model", "Available");
    printf("---------------------------------------------------\n");
    for (int i = 0; i < carCount; i++)
    {
        printf("%-6d %-10s %-25s %8d\n",
               inventory[i].id,
               inventory[i].plateNumber,
               inventory[i].model,
               inventory[i].available);
    }
    printf("---------------------------------------------------\n");
}

/* ============= CUSTOMER FUNCTIONS ============= */
void listCustomers()
{
    clearScreen();
    setColor(CLR_CYAN);
    printf("\n========================================\n");
    printf("           CUSTOMER DATABASE\n");
    printf("========================================\n");
    resetColor();
    if (customerCount == 0)
    {
        setColor(CLR_YELLOW);
        printf("No customers found.\n");
        resetColor();
        return;
    }
    printf("%-6s %-28s %-18s %-18s %-30s\n",
           "ID", "Name", "License No.", "Contact", "Email");
    printf("-------------------------------------------------------------------------------------------\n");
    for (int i = 0; i < customerCount; i++)
    {
        printf("%-6d %-28s %-18s %-18s %-30s\n",
               customers[i].id,
               customers[i].name,
               customers[i].licenseNo,
               customers[i].contact,
               customers[i].email);
    }
    printf("-------------------------------------------------------------------------------------------\n");
}

void addCustomer()
{
    if (customerCount >= MAX_CUSTOMERS)
    {
        uiError("Customer database full!");
        pressEnterToContinue();
        return;
    }
    Customer c = {0};
    c.id = nextCustomerId++;

    /* [VALIDATION] Name */
    inputText(c.name, NAME_LEN, "Customer Name: ", 0);
    if (c.name[0] == '\0')
    {
        nextCustomerId--;
        return;
    }

    /* [VALIDATION] License */
    char licbuf[LICENSE_LEN];
    int licOk = 0;
    for (int t = 0; t < 3; t++)
    {
        inputText(licbuf, LICENSE_LEN, "License No.: ", 0);
        if (!validateLicense(licbuf))
        {
            uiWarn("Try again.");
            continue;
        }
        if (findCustomerByLicense(licbuf) != -1)
        {
            uiError("A customer with that license already exists.");
            continue;
        }
        strncpy(c.licenseNo, licbuf, LICENSE_LEN - 1);
        licOk = 1;
        break;
    }
    if (!licOk)
    {
        nextCustomerId--;
        uiError("Cancelled.");
        pressEnterToContinue();
        return;
    }

    /* [VALIDATION] Contact */
    char conbuf[CONTACT_LEN];
    for (int t = 0; t < 3; t++)
    {
        inputText(conbuf, CONTACT_LEN, "Contact Number: ", 0);
        if (validateContact(conbuf))
        {
            strncpy(c.contact, conbuf, CONTACT_LEN - 1);
            break;
        }
        if (t == 2)
        {
            nextCustomerId--;
            uiError("Cancelled.");
            pressEnterToContinue();
            return;
        }
        uiWarn("Try again.");
    }

    /* [VALIDATION] Email */
    char emabuf[EMAIL_LEN];
    inputText(emabuf, EMAIL_LEN, "Email Address: ", 1);
    if (emabuf[0] != '\0' && !isValidEmail(emabuf))
        uiWarn("Invalid email format — saved anyway.");
    strncpy(c.email, emabuf, EMAIL_LEN - 1);

    customers[customerCount++] = c;
    saveCustomers();
    uiSuccess("Customer added successfully!");
    pressEnterToContinue();
}

void editCustomer()
{
    int idx = selectCustomerMenu();
    if (idx == -1)
        return;
    Customer *c = &customers[idx];
    setColor(CLR_BRIGHT_CYAN);
    printf("\n  Editing: %s (%s)\n", c->name, c->licenseNo);
    resetColor();
    uiWarn("Leave blank to keep current value.");

    /* Name */
    char tmp[NAME_LEN];
    inputText(tmp, NAME_LEN, "New name (blank=keep): ", 1);
    if (tmp[0])
        strncpy(c->name, tmp, NAME_LEN - 1);

    /* Contact */
    char conbuf[CONTACT_LEN];
    inputText(conbuf, CONTACT_LEN, "New contact (blank=keep): ", 1);
    if (conbuf[0])
    {
        if (validateContact(conbuf))
            strncpy(c->contact, conbuf, CONTACT_LEN - 1);
        else
            uiWarn("Invalid contact — keeping old value.");
    }

    /* Email */
    char emabuf[EMAIL_LEN];
    inputText(emabuf, EMAIL_LEN, "New email (blank=keep): ", 1);
    if (emabuf[0])
    {
        if (!isValidEmail(emabuf))
            uiWarn("Invalid email format — keeping old value.");
        else
            strncpy(c->email, emabuf, EMAIL_LEN - 1);
    }

    saveCustomers();
    uiSuccess("Customer updated.");
    pressEnterToContinue();
}

void deleteCustomer()
{
    int idx = selectCustomerMenu();
    if (idx == -1)
        return;
    printf("Delete %s (%s)? (y/n): ", customers[idx].name, customers[idx].licenseNo);
    char c = getchar();
    flushInput();
    if (c == 'y' || c == 'Y')
    {
        for (int i = idx; i < customerCount - 1; i++)
            customers[i] = customers[i + 1];
        customerCount--;
        saveCustomers();
        printf("Customer deleted.\n");
    }
    pressEnterToContinue();
}

void searchCustomer()
{
    char key[LICENSE_LEN];
    safeInput(key, LICENSE_LEN, "Enter License No.: ");
    int idx = findCustomerByLicense(key);
    if (idx == -1)
    {
        setColor(CLR_YELLOW);
        printf("Customer not found.\n");
        resetColor();
        pressEnterToContinue();
        return;
    }
    Customer *c = &customers[idx];
    setColor(CLR_CYAN);
    printf("\nCustomer Found:\n");
    resetColor();
    printf("ID: %d\nName: %s\nLicense: %s\nContact: %s\nEmail: %s\n",
           c->id, c->name, c->licenseNo, c->contact, c->email);
    pressEnterToContinue();
}

void customerMenu()
{
    int ch;
    do
    {
        clearScreen();
        uiSpacer();
        uiHeader("CUSTOMER MENU", CLR_BRIGHT_YELLOW, CLR_YELLOW);
        uiSpacer();
        uiOption(1, "View Customers");
        uiOption(2, "Add Customer");
        uiOption(3, "Edit Customer");
        uiOption(4, "Delete Customer");
        uiOption(5, "Search Customer");
        uiOption(6, "Back");
        uiSpacer();
        uiPrompt("Enter choice: ");
        if (scanf("%d", &ch) != 1)
        {
            flushInput();
            continue;
        }
        flushInput();
        if (ch == 1)
        {
            listCustomers();
            pressEnterToContinue();
        }
        else if (ch == 2)
            addCustomer();
        else if (ch == 3)
            editCustomer();
        else if (ch == 4)
            deleteCustomer();
        else if (ch == 5)
            searchCustomer();
        else if (ch == 6)
            break;
        else
        {
            uiError("Invalid choice.");
            pressEnterToContinue();
        }
    } while (1);
}

int selectCustomerMenu()
{
    listCustomers();
    if (customerCount == 0)
    {
        printf("No customers available.\n");
        return -1;
    }
    printf("Select customer (0 to cancel): ");
    int choice;
    if (scanf("%d", &choice) != 1)
    {
        flushInput();
        return -1;
    }
    flushInput();
    if (choice == 0)
        return -1;
    if (choice < 1 || choice > customerCount)
        return -1;
    return choice - 1;
}

/* ============= SIMPLIFIED RENTAL SYSTEM WITH BULK SUPPORT ============= */
void rentCarMenu(const char *cashierName)
{
    clearScreen();
    setColor(CLR_BLUE);
    printf("========================================\n");
    printf("      RENT A CAR - BULK RENTAL SUPPORT\n");
    printf("========================================\n");
    resetColor();
    printf("Cashier: %s\n", cashierName);
    /* Step 1: Select or create customer */
    int customerIndex = -1;
    printf("1. Select existing customer\n");
    printf("2. Create new customer\n");
    printf("Choice: ");
    int choice;
    if (scanf("%d", &choice) != 1)
    {
        flushInput();
        printf("Invalid choice.\n");
        pressEnterToContinue();
        return;
    }
    flushInput();
    char custName[NAME_LEN] = "";
    char custLicense[LICENSE_LEN] = "";
    char custContact[CONTACT_LEN] = "";
    char custEmail[EMAIL_LEN] = "";
    if (choice == 1)
    {
        customerIndex = selectCustomerMenu();
        if (customerIndex == -1)
        {
            printf("No customer selected.\n");
            pressEnterToContinue();
            return;
        }
        strncpy(custName, customers[customerIndex].name, NAME_LEN - 1);
        strncpy(custLicense, customers[customerIndex].licenseNo, LICENSE_LEN - 1);
        strncpy(custContact, customers[customerIndex].contact, CONTACT_LEN - 1);
        strncpy(custEmail, customers[customerIndex].email, EMAIL_LEN - 1);
        printf("Selected customer: %s\n", custName);
    }
    else if (choice == 2)
    {
        if (customerCount >= MAX_CUSTOMERS)
        {
            setColor(CLR_RED);
            printf("Customer database full!\n");
            resetColor();
            pressEnterToContinue();
            return;
        }
        Customer c = {0};
        c.id = nextCustomerId++;
        safeInput(c.name, NAME_LEN, "Customer Name: ");
        safeInput(c.licenseNo, LICENSE_LEN, "License No.: ");
        safeInput(c.contact, CONTACT_LEN, "Contact Number: ");
        safeInput(c.email, EMAIL_LEN, "Email Address: ");
        if (!isValidEmail(c.email))
        {
            setColor(CLR_YELLOW);
            printf("Warning: Invalid email format.\n");
            resetColor();
        }
        customers[customerCount++] = c;
        saveCustomers();
        customerIndex = customerCount - 1;
        strncpy(custName, c.name, NAME_LEN - 1);
        strncpy(custLicense, c.licenseNo, LICENSE_LEN - 1);
        strncpy(custContact, c.contact, CONTACT_LEN - 1);
        strncpy(custEmail, c.email, EMAIL_LEN - 1);
        setColor(CLR_GREEN);
        printf("New customer created: %s\n", custName);
        resetColor();
    }
    else
    {
        printf("Invalid choice.\n");
        pressEnterToContinue();
        return;
    }
    /* Step 2: Bulk rental process */
    RentalItem rentalItems[MAX_CARS]; /* Array to store multiple rental items */
    int itemCount = 0;
    double totalAmount = 0.0;
    char addMore = 'y';
    do
    {
        /* Show available cars */
        showQuickInventory();
        /* Select car */
        char barcode[BARCODE_LEN];
        printf("\nEnter barcode of car to rent: ");
        safeInput(barcode, BARCODE_LEN, "");
        int carIndex = findCarByBarcode(barcode);
        if (carIndex == -1)
        {
            setColor(CLR_RED);
            printf("Car not found with barcode: %s\n", barcode);
            resetColor();
            pressEnterToContinue();
            continue;
        }
        Car *selectedCar = &inventory[carIndex];
        if (selectedCar->available <= 0)
        {
            setColor(CLR_RED);
            printf("Sorry, %s is not available for rent.\n", selectedCar->model);
            resetColor();
            pressEnterToContinue();
            continue;
        }
        /* Check if this car is already in the rental list */
        int existingItem = -1;
        for (int i = 0; i < itemCount; i++)
        {
            if (rentalItems[i].carIndex == carIndex)
            {
                existingItem = i;
                break;
            }
        }
        /* Get rental details */
        int quantity = 1;
        int days = 1;
        if (existingItem == -1)
        {
            /* [VALIDATION] Quantity */
            char qprompt[128];
            snprintf(qprompt, sizeof(qprompt), "How many units of %s? (1-%d): ",
                     selectedCar->model, selectedCar->available);
            if (!inputInt(&quantity, 1, selectedCar->available, qprompt))
            {
                pressEnterToContinue();
                continue;
            }
        }
        else
        {
            int maxExtra = selectedCar->available - rentalItems[existingItem].quantity;
            setColor(CLR_BRIGHT_CYAN);
            printf("  Already in list: %d units. Max additional: %d\n",
                   rentalItems[existingItem].quantity, maxExtra);
            resetColor();
            if (maxExtra <= 0)
            {
                uiError("No additional units available.");
                pressEnterToContinue();
                continue;
            }
            char qprompt[80];
            snprintf(qprompt, sizeof(qprompt), "How many additional units? (1-%d): ", maxExtra);
            if (!inputInt(&quantity, 1, maxExtra, qprompt))
            {
                pressEnterToContinue();
                continue;
            }
        }
        /* [VALIDATION] Days */
        if (!inputInt(&days, 1, 365, "How many days to rent? (1-365): "))
        {
            pressEnterToContinue();
            continue;
        }
        /* Calculate subtotal */
        double subtotal = selectedCar->pricePerDay * quantity * days;
        if (existingItem == -1)
        {
            /* Add new item */
            if (itemCount >= MAX_CARS)
            {
                printf("Cannot add more items. Maximum rental items reached.\n");
                pressEnterToContinue();
                continue;
            }
            rentalItems[itemCount].carIndex = carIndex;
            rentalItems[itemCount].quantity = quantity;
            rentalItems[itemCount].days = days;
            rentalItems[itemCount].subtotal = subtotal;
            itemCount++;
            totalAmount += subtotal;
            setColor(CLR_GREEN);
            printf("Added %d units of %s for %d days (PHP %.2f)\n",
                   quantity, selectedCar->model, days, subtotal);
            resetColor();
        }
        else
        {
            /* Update existing item */
            rentalItems[existingItem].quantity += quantity;
            rentalItems[existingItem].days = days; /* Update days for all units */
            rentalItems[existingItem].subtotal = selectedCar->pricePerDay *
                                                 rentalItems[existingItem].quantity * days;
            /* Recalculate total */
            totalAmount = 0;
            for (int i = 0; i < itemCount; i++)
            {
                totalAmount += rentalItems[i].subtotal;
            }
            setColor(CLR_GREEN);
            printf("Updated %s to %d units for %d days (PHP %.2f)\n",
                   selectedCar->model, rentalItems[existingItem].quantity,
                   days, rentalItems[existingItem].subtotal);
            resetColor();
        }
        /* Show current rental summary */
        printf("\n=== CURRENT RENTAL SUMMARY ===\n");
        printf("%-25s %-10s %-6s %-12s\n", "Car Model", "Quantity", "Days", "Subtotal");
        printf("------------------------------------------------------------\n");
        for (int i = 0; i < itemCount; i++)
        {
            Car *car = &inventory[rentalItems[i].carIndex];
            printf("%-25s %-10d %-6d PHP %-8.2f\n",
                   car->model, rentalItems[i].quantity,
                   rentalItems[i].days, rentalItems[i].subtotal);
        }
        printf("------------------------------------------------------------\n");
        printf("TOTAL: PHP %.2f\n", totalAmount);
        /* Ask if user wants to add another car */
        if (itemCount < MAX_CARS)
        {
            printf("Add another car? (y/n): ");
            addMore = getchar();
            flushInput();
        }
        else
        {
            printf("Maximum items reached. Proceeding to payment.\n");
            addMore = 'n';
        }
    } while ((addMore == 'y' || addMore == 'Y') && itemCount < MAX_CARS);
    if (itemCount == 0)
    {
        printf("No cars selected. Rental cancelled.\n");
        pressEnterToContinue();
        return;
    }
    /* Step 3: Show final invoice */
    clearScreen();
    setColor(CLR_CYAN);
    printf("========================================\n");
    printf("               FINAL INVOICE\n");
    printf("========================================\n");
    resetColor();
    printf("Customer: %s\n", custName);
    printf("License: %s\n", custLicense);
    printf("Contact: %s\n", custContact);
    printf("\n=== RENTAL ITEMS ===\n");
    printf("%-25s %-12s %-10s %-6s %-12s\n",
           "Car Model", "Plate", "Quantity", "Days", "Subtotal");
    printf("----------------------------------------------------------------\n");
    for (int i = 0; i < itemCount; i++)
    {
        Car *car = &inventory[rentalItems[i].carIndex];
        printf("%-25s %-12s %-10d %-6d PHP %-8.2f\n",
               car->model, car->plateNumber,
               rentalItems[i].quantity, rentalItems[i].days,
               rentalItems[i].subtotal);
    }
    printf("----------------------------------------------------------------\n");
    printf("TOTAL AMOUNT: PHP %.2f\n", totalAmount);

    /* [DATE] Ask for start date and compute expected return */
    char startDate[12] = {0};
    uiSpacer();
    uiRule(CLR_BRIGHT_CYAN);
    uiCenterLine("RENTAL DATES", CLR_BRIGHT_CYAN);
    uiRule(CLR_BRIGHT_CYAN);
    if (!inputDate(startDate, "Rental Start Date"))
    {
        uiError("Date input cancelled. Rental cancelled.");
        pressEnterToContinue();
        return;
    }

    uiSpacer();
    setColor(CLR_BRIGHT_GREEN);
    printf("  Start Date: %s\n", startDate);
    resetColor();

    /* Show expected return date for each item */
    for (int i = 0; i < itemCount; i++)
    {
        char expRet[12];
        addDaysToDate(startDate, rentalItems[i].days, expRet);
        setColor(CLR_BRIGHT_YELLOW);
        printf("  Expected Return (%s): %s\n", inventory[rentalItems[i].carIndex].model, expRet);
        resetColor();
    }

    uiSpacer();
    uiPrompt("Confirm rental? (y/n): ");
    char confirm = getchar();
    flushInput();
    if (confirm != 'y' && confirm != 'Y')
    {
        uiWarn("Rental cancelled.");
        pressEnterToContinue();
        return;
    }
    /* Step 4: Process payment */
    double cash = 0.0;
    if (!inputDouble(&cash, totalAmount, 9999999.0, "Enter cash amount (PHP): "))
    {
        uiError("Payment cancelled.");
        pressEnterToContinue();
        return;
    }
    while (cash < totalAmount)
    {
        uiError("Insufficient cash. Try again.");
        if (!inputDouble(&cash, 0.0, 9999999.0, "Enter cash amount (PHP): "))
            return;
    }
    double change = cash - totalAmount;
    setColor(CLR_BRIGHT_GREEN);
    printf("\n  Change: PHP %.2f\n", change);
    resetColor();
    /* Step 5: Create rental records and update inventory */
    int firstRentalId = nextRentalId;
    for (int i = 0; i < itemCount; i++)
    {
        Car *car = &inventory[rentalItems[i].carIndex];
        if (rentalCount >= MAX_RENTALS)
        {
            uiError("Rental database full! Could not save all rentals.");
            break;
        }
        RentalRecord rental = {0};
        rental.id = nextRentalId++;
        strncpy(rental.barcode, car->barcode, BARCODE_LEN - 1);
        strncpy(rental.plateNumber, car->plateNumber, PLATE_LEN - 1);
        strncpy(rental.model, car->model, MODEL_LEN - 1);
        strncpy(rental.customerName, custName, NAME_LEN - 1);
        strncpy(rental.licenseNo, custLicense, LICENSE_LEN - 1);
        rental.quantity = rentalItems[i].quantity;
        rental.days = rentalItems[i].days;
        rental.totalAmount = rentalItems[i].subtotal;
        rental.rentTime = time(NULL);
        rental.returned = 0;
        rental.returnTime = 0;
        rental.lateFee = 0.0;
        rental.warningIssued = 0;
        /* [DATE] Store dates */
        strncpy(rental.startDate, startDate, 11);
        addDaysToDate(startDate, rentalItems[i].days, rental.expectedReturnDate);
        rentals[rentalCount++] = rental;
        car->available -= rentalItems[i].quantity;
        car->totalRentedCount += rentalItems[i].quantity;
    }
    saveRentals();
    saveInventory();
    /* Step 6: Generate receipts and barcodes */
    int lastRentalId = nextRentalId - 1;
    int transactionId = firstRentalId; /* Use first rental ID as transaction ID */
    /* Save bulk receipt (single receipt with all items) */
    saveBulkReceipt(transactionId,
                    custName,
                    custLicense,
                    custContact,
                    custEmail,
                    cashierName,
                    rentalItems,
                    itemCount,
                    totalAmount,
                    cash,
                    change,
                    firstRentalId,
                    lastRentalId);
    /* Also save individual receipts for each rental */
    for (int i = 0; i < itemCount; i++)
    {
        int rentalId = firstRentalId + i;
        Car *car = &inventory[rentalItems[i].carIndex];
        saveSimpleReceipt(rentalId, custName, custLicense, car->model, car->plateNumber, car->barcode, rentalItems[i].quantity, rentalItems[i].days, rentalItems[i].subtotal);
    }
    /* Step 7: Send email confirmation */
    if (strlen(custEmail) > 4)
    {
        char subject[256];
        snprintf(subject, sizeof(subject),
                 "Bulk Rental Confirmation - FOURSIGHT");
        char body[4096];
        char *bodyPtr = body;
        bodyPtr += snprintf(body, sizeof(body),
                            "Hello %s,\nThank you for choosing FOURSIGHT Car Rentals!\nBULK RENTAL DETAILS:\n------------------------------------------------\nTotal Items: %d\nTotal Amount: PHP %.2f\nRental Date: %s\nItems Rented:\n------------------------------------------------\n",
                            custName, itemCount, totalAmount,
                            ctime(&rentals[rentalCount - itemCount].rentTime));
        for (int i = 0; i < itemCount && (size_t)(bodyPtr - body) < (sizeof(body) - 200); i++)
        {
            Car *car = &inventory[rentalItems[i].carIndex];
            bodyPtr += snprintf(bodyPtr, sizeof(body) - (bodyPtr - body),
                                "%d. %s (%s)\n   Quantity: %d, Days: %d, Amount: PHP %.2f\n",
                                i + 1, car->model, car->plateNumber,
                                rentalItems[i].quantity, rentalItems[i].days,
                                rentalItems[i].subtotal);
        }
        bodyPtr += snprintf(bodyPtr, sizeof(body) - (bodyPtr - body),
                            "------------------------------------------------\nPlease keep this email for your records.\nDrive safely!\n");
        sendEmailLibCurl(custEmail, subject, body);
    }
    setColor(CLR_GREEN);
    printf("\nBulk rental completed successfully!\n");
    printf("Total Items: %d | Total Amount: PHP %.2f\n", itemCount, totalAmount);
    printf("Rental IDs: %d to %d\n",
           firstRentalId, lastRentalId);
    resetColor();
    pressEnterToContinue();
}

/* ============= RETURN CAR FUNCTION ============= */
void returnCar()
{
    clearScreen();
    uiSpacer();
    uiHeader("RETURN A RENTAL CAR", CLR_BRIGHT_YELLOW, CLR_YELLOW);
    uiSpacer();

    int id;
    if (!inputInt(&id, 1, 99999, "Enter Rental ID to return: "))
    {
        pressEnterToContinue();
        return;
    }

    int idx = -1;
    for (int i = 0; i < rentalCount; i++)
        if (rentals[i].id == id)
            idx = i;
    if (idx == -1)
    {
        uiError("Rental not found.");
        pressEnterToContinue();
        return;
    }
    RentalRecord *r = &rentals[idx];
    if (r->returned)
    {
        uiWarn("This rental has already been returned.");
        pressEnterToContinue();
        return;
    }

    /* Show rental details */
    uiSpacer();
    uiRule(CLR_CYAN);
    uiCenterLine("RENTAL DETAILS", CLR_BRIGHT_CYAN);
    uiRule(CLR_CYAN);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", r->id);
    uiStatLine("Rental ID:", buf, CLR_BRIGHT_WHITE);
    uiStatLine("Customer:", r->customerName, CLR_BRIGHT_WHITE);
    uiStatLine("Car:", r->model, CLR_BRIGHT_WHITE);
    uiStatLine("Plate:", r->plateNumber, CLR_BRIGHT_WHITE);
    uiStatLine("Start Date:", r->startDate[0] ? r->startDate : "N/A", CLR_BRIGHT_GREEN);
    uiStatLine("Expected Return:", r->expectedReturnDate[0] ? r->expectedReturnDate : "N/A", CLR_BRIGHT_YELLOW);
    uiSpacer();

    /* [DATE] Ask for actual return date */
    char actualReturn[12] = {0};
    if (!inputDate(actualReturn, "Actual Return Date"))
    {
        uiError("Date input cancelled.");
        pressEnterToContinue();
        return;
    }

    /* [DATE] Calculate late fee using dates */
    r->returnTime = time(NULL);
    r->lateFee = 0.0;
    double dailyRate = 0.0;
    int carIdx = findCarByBarcode(r->barcode);
    if (carIdx != -1)
    {
        dailyRate = inventory[carIdx].available >= 0 ? inventory[carIdx].pricePerDay : 0;
        inventory[carIdx].available += r->quantity;
    }

    if (r->expectedReturnDate[0] != '\0')
    {
        int daysLate = daysBetweenDates(r->expectedReturnDate, actualReturn);
        if (daysLate > 0)
        {
            /* Both hourly and daily penalties */
            double hourlyFee = 50.0 * 24 * daysLate;
            double dailyFee = dailyRate * 1.5 * daysLate;
            r->lateFee = (dailyFee > hourlyFee) ? dailyFee : hourlyFee;

            uiSpacer();
            uiRule(CLR_BRIGHT_RED);
            uiCenterLine("LATE RETURN DETECTED", CLR_BRIGHT_RED);
            uiRule(CLR_BRIGHT_RED);
            snprintf(buf, sizeof(buf), "%d day(s)", daysLate);
            uiStatLine("Days Overdue:", buf, CLR_BRIGHT_RED);
            snprintf(buf, sizeof(buf), "PHP %.2f", hourlyFee);
            uiStatLine("Hourly Penalty:", buf, CLR_YELLOW);
            snprintf(buf, sizeof(buf), "PHP %.2f", dailyFee);
            uiStatLine("Daily Penalty:", buf, CLR_YELLOW);
            snprintf(buf, sizeof(buf), "PHP %.2f (higher applied)", r->lateFee);
            uiStatLine("Late Fee Charged:", buf, CLR_BRIGHT_RED);
        }
        else
        {
            uiSuccess("Returned on time! No late fee.");
        }
    }
    else
    {
        /* Fallback: old hourly method */
        time_t expectedReturn = r->rentTime + (r->days * 24 * 3600);
        if (r->returnTime > expectedReturn)
        {
            double hoursLate = difftime(r->returnTime, expectedReturn) / 3600.0;
            r->lateFee = hoursLate * LATE_FEE_PER_HOUR;
            snprintf(buf, sizeof(buf), "PHP %.2f (%.1f hrs late)", r->lateFee, hoursLate);
            uiWarn(buf);
        }
    }

    r->returned = 1;
    saveRentals();
    saveInventory();

    /* Generate return receipt */
    char filename[FILENAME_LEN];
    snprintf(filename, sizeof(filename), "%s/return_%d.txt", RECEIPTS_DIR, r->id);
    FILE *fp = fopen(filename, "w");
    if (fp)
    {
        fprintf(fp, "========================================\n");
        fprintf(fp, "          FOURSIGHT CAR RENTALS\n");
        fprintf(fp, "           RETURN CONFIRMATION\n");
        fprintf(fp, "========================================\n");
        fprintf(fp, "Rental ID:      %d\n", r->id);
        fprintf(fp, "Customer:       %s\n", r->customerName);
        fprintf(fp, "License:        %s\n", r->licenseNo);
        fprintf(fp, "Car:            %s (%s)\n", r->model, r->plateNumber);
        fprintf(fp, "Qty Returned:   %d\n", r->quantity);
        fprintf(fp, "Start Date:     %s\n", r->startDate[0] ? r->startDate : "N/A");
        fprintf(fp, "Expected Back:  %s\n", r->expectedReturnDate[0] ? r->expectedReturnDate : "N/A");
        fprintf(fp, "Actual Return:  %s\n", actualReturn);
        fprintf(fp, "Return Time:    %s", ctime(&r->returnTime));
        if (r->lateFee > 0)
        {
            fprintf(fp, "Late Fee:       PHP %.2f\n", r->lateFee);
            fprintf(fp, "Grand Total:    PHP %.2f\n", r->totalAmount + r->lateFee);
        }
        else
            fprintf(fp, "Grand Total:    PHP %.2f\n", r->totalAmount);
        fprintf(fp, "========================================\n");
        fclose(fp);
    }
    autoPrintReceipt(filename);
    /* Send return confirmation email */
    int customerIdx = findCustomerByLicense(r->licenseNo);
    if (customerIdx != -1 && strlen(customers[customerIdx].email) > 4)
    {
        char subject[256];
        snprintf(subject, sizeof(subject), "Return Confirmation - FOURSIGHT (Rental #%d)", r->id);
        char body[2048];
        snprintf(body, sizeof(body),
                 "Hello %s,\nThis email confirms that your rented car has been returned successfully.\nCar Returned:\n%s (%s)\nQuantity: %d\nReturn Time: %s\n",
                 r->customerName, r->model, r->plateNumber,
                 r->quantity, ctime(&r->returnTime));
        if (r->lateFee > 0)
        {
            snprintf(body + strlen(body), sizeof(body) - strlen(body), "Late Fee Applied: PHP %.2f\n", r->lateFee);
        }
        snprintf(body + strlen(body), sizeof(body) - strlen(body), "Thank you for choosing FOURSIGHT Car Rentals!\n");
        sendEmailLibCurl(customers[customerIdx].email, subject, body);
    }
    setColor(CLR_GREEN);
    printf("Car returned successfully!\n");
    resetColor();
    pressEnterToContinue();
}

/* ============= PAYMENT/BILLING MENU ============= */
void paymentBillingMenu()
{
    int ch;
    do
    {
        clearScreen();
        setColor(CLR_BLUE);
        printf("========================================\n");
        printf("          PAYMENT / BILLING MENU\n");
        printf("========================================\n");
        resetColor();
        printf("1. Show Receipt by Rental ID\n");
        printf("2. Reprint Receipt\n");
        printf("3. Back\n");
        printf("Choice: ");
        if (scanf("%d", &ch) != 1)
        {
            flushInput();
            continue;
        }
        flushInput();
        if (ch == 1)
        {
            printf("Enter Rental ID: ");
            int id;
            if (scanf("%d", &id) != 1)
            {
                flushInput();
                continue;
            }
            flushInput();
            int idx = -1;
            for (int i = 0; i < rentalCount; i++)
                if (rentals[i].id == id)
                    idx = i;
            if (idx == -1)
            {
                printf("Receipt not found.\n");
                pressEnterToContinue();
                continue;
            }
            printReceiptFromRental(&rentals[idx]);
            pressEnterToContinue();
        }
        else if (ch == 2)
        {
            reprintReceiptMenu();
        }
        else if (ch == 3)
        {
            break;
        }
        else
        {
            printf("Invalid choice.\n");
            pressEnterToContinue();
        }
    } while (1);
}

/* ============= REPORTS FUNCTIONS ============= */
void reportInventory()
{
    clearScreen();
    printf("\n========== INVENTORY REPORT ==========\n");
    int totalUnits = 0;
    int totalCars = carCount;
    for (int i = 0; i < carCount; i++)
        totalUnits += inventory[i].available;
    printf("Total Car Types: %d\n", totalCars);
    printf("Total Available Units: %d\n", totalUnits);
    printf("%-12s %-25s %-12s %-10s\n", "Plate", "Model", "Rate/Day", "Available");
    printf("--------------------------------------------------------------\n");
    for (int i = 0; i < carCount; i++)
        printf("%-12s %-25s PHP %-8.2f %-10d\n",
               inventory[i].plateNumber,
               inventory[i].model,
               inventory[i].pricePerDay,
               inventory[i].available);
    printf("--------------------------------------------------------------\n");
}

void reportCustomers()
{
    clearScreen();
    printf("\n========== CUSTOMER REPORT ==========\n");
    printf("Total Customers: %d\n", customerCount);
    printf("%-6s %-25s %-18s %-18s\n", "ID", "Name", "License", "Contact");
    printf("--------------------------------------------------------------\n");
    for (int i = 0; i < customerCount; i++)
        printf("%-6d %-25s %-18s %-18s\n",
               customers[i].id, customers[i].name,
               customers[i].licenseNo, customers[i].contact);
    printf("--------------------------------------------------------------\n");
}

void reportRentals()
{
    clearScreen();
    printf("\n========== RENTAL REPORT ==========\n");
    printf("Total Rental Records: %d\n", rentalCount);
    printf("%-6s %-20s %-22s %-8s %-6s %-10s %-8s\n",
           "ID", "Customer", "Car", "Qty", "Days", "Amount", "Ret");
    printf("--------------------------------------------------------------------------------------\n");
    for (int i = 0; i < rentalCount; i++)
        printf("%-6d %-20s %-22s %-8d %-6d PHP %-8.2f %-8s\n",
               rentals[i].id, rentals[i].customerName, rentals[i].model,
               rentals[i].quantity, rentals[i].days,
               rentals[i].totalAmount,
               rentals[i].returned ? "Yes" : "No");
    printf("--------------------------------------------------------------------------------------\n");
}

void reportsMenu()
{
    int ch;
    do
    {
        clearScreen();
        uiSpacer();
        uiHeader("REPORTS MENU", CLR_BRIGHT_YELLOW, CLR_YELLOW);
        uiSpacer();
        uiOption(1, "Inventory Report");
        uiOption(2, "Customer Report");
        uiOption(3, "Rental Report");
        uiOption(4, "Back");
        uiSpacer();
        uiPrompt("Enter choice: ");
        if (scanf("%d", &ch) != 1)
        {
            flushInput();
            continue;
        }
        flushInput();
        if (ch == 1)
        {
            reportInventory();
            pressEnterToContinue();
        }
        else if (ch == 2)
        {
            reportCustomers();
            pressEnterToContinue();
        }
        else if (ch == 3)
        {
            reportRentals();
            pressEnterToContinue();
        }
        else if (ch == 4)
            break;
        else
        {
            uiError("Invalid choice.");
            pressEnterToContinue();
        }
    } while (1);
}

/* ============= SETTINGS FUNCTIONS ============= */
void changePassword(const char *currentUser)
{
    int idx = findUserByUsername(currentUser);
    if (idx == -1)
        return;
    char oldpass[PASSWORD_LEN], newpass[PASSWORD_LEN];
    uiPrompt("Enter Current Password: ");
    safeInput(oldpass, PASSWORD_LEN, NULL);
    /* [SECURITY] Use hash-aware verification */
    if (!verifyPassword(users[idx].password, oldpass))
    {
        uiError("Incorrect password.");
        pressEnterToContinue();
        return;
    }
    uiPrompt("Enter New Password: ");
    safeInput(newpass, PASSWORD_LEN, NULL);
    /* [SECURITY] Hash and store new password */
    char hash[SHA256_HASH_LEN];
    sha256_hex(newpass, hash);
    snprintf(users[idx].password, PASSWORD_LEN, "%s%s", HASHED_FLAG, hash);
    saveUsers();
    uiSuccess("Password changed successfully!");
    pressEnterToContinue();
}

void settingsMenu(const char *currentUser)
{
    int ch;
    do
    {
        clearScreen();
        uiSpacer();
        uiHeader("SETTINGS MENU", CLR_BRIGHT_YELLOW, CLR_YELLOW);
        uiSpacer();
        int idx = findUserByUsername(currentUser);
        int curIsAdmin = (idx != -1) ? users[idx].isAdmin : 0;
        uiOption(1, "Change Password");
        if (curIsAdmin)
        {
            uiOption(2, "Manage Users");
            uiOption(3, "Back");
        }
        else
            uiOption(2, "Back");
        uiSpacer();
        uiPrompt("Enter choice: ");
        if (scanf("%d", &ch) != 1)
        {
            flushInput();
            continue;
        }
        flushInput();
        if (ch == 1)
            changePassword(currentUser);
        else if (curIsAdmin && ch == 2)
            userManagementMenu(currentUser);
        else if ((curIsAdmin && ch == 3) || (!curIsAdmin && ch == 2))
            break;
        else
        {
            uiError("Invalid choice.");
            pressEnterToContinue();
        }
    } while (1);
}

/* ============= USER MANAGEMENT ============= */
void userManagementMenu(const char *currentUser)
{
    (void)currentUser;
    int choice;
    do
    {
        clearScreen();
        uiSpacer();
        uiHeader("USER MANAGEMENT", CLR_BRIGHT_YELLOW, CLR_YELLOW);
        uiSpacer();
        uiOption(1, "Add New User");
        uiOption(2, "View Users");
        uiOption(3, "Delete User");
        uiOption(4, "Change User Password");
        uiOption(5, "Toggle Admin Role");
        uiOption(0, "Back");
        uiSpacer();
        uiPrompt("Enter choice: ");
        if (scanf("%d", &choice) != 1)
        {
            flushInput();
            continue;
        }
        flushInput();
        switch (choice)
        {
        case 1:
            addUserMenu();
            break;
        case 2:
            viewUsersMenu();
            break;
        case 3:
            deleteUserMenu();
            break;
        case 4:
            changeUserPasswordMenu();
            break;
        case 5:
            toggleAdminMenu();
            break;
        case 0:
        default:
            break;
        }
    } while (choice != 0);
}

void addUserMenu()
{
    if (userCount >= MAX_USERS)
    {
        uiWarn("User limit reached. Cannot add more users.");
        pressEnterToContinue();
        return;
    }
    User u = {0};

    /* [VALIDATION] Username */
    char ubuf[USERNAME_LEN];
    int uOk = 0;
    for (int t = 0; t < 3; t++)
    {
        inputText(ubuf, USERNAME_LEN, "Username: ", 0);
        if (!validateUsername(ubuf))
        {
            uiWarn("Try again.");
            continue;
        }
        if (findUserByUsername(ubuf) != -1)
        {
            uiError("Username already exists.");
            continue;
        }
        strncpy(u.username, ubuf, USERNAME_LEN - 1);
        uOk = 1;
        break;
    }
    if (!uOk)
    {
        uiError("Cancelled.");
        pressEnterToContinue();
        return;
    }

    /* [VALIDATION] Password */
    char plainpass[PASSWORD_LEN];
    int pOk = 0;
    for (int t = 0; t < 3; t++)
    {
        inputText(plainpass, PASSWORD_LEN, "Password (min 6 chars): ", 0);
        if (validatePassword(plainpass))
        {
            pOk = 1;
            break;
        }
        uiWarn("Try again.");
    }
    if (!pOk)
    {
        uiError("Cancelled.");
        pressEnterToContinue();
        return;
    }

    /* [SECURITY] Hash before storing */
    char hash[SHA256_HASH_LEN];
    sha256_hex(plainpass, hash);
    snprintf(u.password, PASSWORD_LEN, "%s%s", HASHED_FLAG, hash);

    /* [VALIDATION] Role */
    char rolebuf[8];
    inputText(rolebuf, sizeof(rolebuf), "Role (1=Admin, 0=Cashier): ", 0);
    u.isAdmin = (rolebuf[0] == '1') ? 1 : 0;

    u.id = nextUserId++;
    users[userCount++] = u;
    saveUsers();
    uiSuccess("User created successfully!");
    pressEnterToContinue();
}

void viewUsersMenu()
{
    clearScreen();
    printf("=== Registered Users ===\n");
    for (int i = 0; i < userCount; i++)
    {
        printf("%d. %s  (%s)\n",
               users[i].id,
               users[i].username,
               users[i].isAdmin ? "ADMIN" : "CASHIER");
    }
    pressEnterToContinue();
}

int selectUserMenu()
{
    if (userCount == 0)
    {
        printf("No users found.\n");
        pressEnterToContinue();
        return -1;
    }
    printf("\n==== USERS ====\n");
    printf("%-5s %-15s %-6s\n", "No.", "Username", "Admin");
    printf("--------------------------\n");
    for (int i = 0; i < userCount; i++)
    {
        printf("%-5d %-15s %-6s\n",
               i + 1,
               users[i].username,
               users[i].isAdmin ? "YES" : "NO");
    }
    printf("\nSelect user (0 to cancel): ");
    int choice;
    if (scanf("%d", &choice) != 1)
    {
        flushInput();
        return -1;
    }
    flushInput();
    if (choice <= 0 || choice > userCount)
        return -1;
    return choice - 1;
}

void deleteUserMenu()
{
    int idx = selectUserMenu();
    if (idx == -1)
        return;
    printf("Delete user %s? (y/n): ", users[idx].username);
    char c = getchar();
    flushInput();
    if (c == 'y' || c == 'Y')
    {
        for (int i = idx; i < userCount - 1; i++)
            users[i] = users[i + 1];
        userCount--;
        saveUsers();
        printf("User deleted.\n");
    }
    pressEnterToContinue();
}

void changeUserPasswordMenu()
{
    int idx = selectUserMenu();
    if (idx == -1)
        return;
    char pass[PASSWORD_LEN];
    safeInput(pass, PASSWORD_LEN, "Enter new password: ");
    /* [SECURITY] Hash before storing */
    char hash[SHA256_HASH_LEN];
    sha256_hex(pass, hash);
    snprintf(users[idx].password, PASSWORD_LEN, "%s%s", HASHED_FLAG, hash);
    saveUsers();
    printf("Password updated.\n");
    pressEnterToContinue();
}

void toggleAdminMenu()
{
    int idx = selectUserMenu();
    if (idx == -1)
        return;
    users[idx].isAdmin = !users[idx].isAdmin;
    saveUsers();
    printf("Admin role updated.\n");
    pressEnterToContinue();
}

/* ============= MAIN DASHBOARD ============= */
void mainDashboard(const char *loggedUser, int isAdmin)
{
    int choice;
    do
    {
        clearScreen();
        /* [DATE] Check approaching/overdue returns on every load */
        checkApproachingReturns();
        uiSpacer();
        uiRule(CLR_BRIGHT_YELLOW);
        uiCenterLine("FOURSIGHT CAR RENTALS", CLR_BRIGHT_YELLOW);
        uiCenterLine("Main Dashboard", CLR_BRIGHT_WHITE);
        uiRule(CLR_BRIGHT_YELLOW);

        /* Logged in user */
        uiSpacer();
        setColor(CLR_BRIGHT_CYAN);
        printf("  Logged in as: ");
        setColor(CLR_BRIGHT_WHITE);
        printf("%s", loggedUser);
        setColor(CLR_BRIGHT_MAGENTA);
        printf("  [%s]\n", isAdmin ? "ADMIN" : "CASHIER");
        resetColor();

        /* Stats block */
        uiSpacer();
        uiRule(CLR_CYAN);
        uiCenterLine("System Overview", CLR_CYAN);
        uiRule(CLR_CYAN);

        int totalAvailable = 0, active = 0;
        double estimatedRevenue = 0.0;
        int lateReturns = 0;
        for (int i = 0; i < carCount; i++)
            totalAvailable += inventory[i].available;
        for (int i = 0; i < rentalCount; i++)
        {
            if (!rentals[i].returned)
                active++;
            estimatedRevenue += rentals[i].totalAmount + rentals[i].lateFee;
            if (rentals[i].returned && rentals[i].lateFee > 0)
                lateReturns++;
        }

        char buf[64];
        snprintf(buf, sizeof(buf), "%d  (Available: %d)", carCount, totalAvailable);
        uiStatLine("Total Cars:", buf, CLR_BRIGHT_GREEN);
        snprintf(buf, sizeof(buf), "%d", active);
        uiStatLine("Active Rentals:", buf, active > 0 ? CLR_BRIGHT_YELLOW : CLR_BRIGHT_GREEN);
        snprintf(buf, sizeof(buf), "%d", customerCount);
        uiStatLine("Customers:", buf, CLR_BRIGHT_WHITE);
        snprintf(buf, sizeof(buf), "PHP %.2f", estimatedRevenue);
        uiStatLine("Est. Revenue:", buf, CLR_BRIGHT_CYAN);
        snprintf(buf, sizeof(buf), "%d", lateReturns);
        uiStatLine("Late Returns:", buf, lateReturns > 0 ? CLR_BRIGHT_RED : CLR_BRIGHT_GREEN);

        /* Menu */
        uiSpacer();
        uiRule(CLR_MAGENTA);
        uiCenterLine("Menu", CLR_BRIGHT_MAGENTA);
        uiRule(CLR_MAGENTA);
        uiOption(1, "Car Inventory");
        uiOption(2, "Customers");
        uiOption(3, "Rent a Car");
        uiOption(4, "Return a Car");
        uiOption(5, "Payment / Billing");
        uiOption(6, "Reports");
        if (isAdmin)
        {
            uiOption(7, "Settings");
            uiOption(8, "Logout");
        }
        else
        {
            uiOption(7, "Logout");
        }
        uiSpacer();
        uiPrompt("Enter choice: ");

        if (scanf("%d", &choice) != 1)
        {
            flushInput();
            uiError("Invalid input. Please enter a number.");
            pressEnterToContinue();
            continue;
        }
        flushInput();
        if (isAdmin)
        {
            if (choice == 1)
                viewInventoryInteractive();
            else if (choice == 2)
                customerMenu();
            else if (choice == 3)
                rentCarMenu(loggedUser);
            else if (choice == 4)
                returnCar();
            else if (choice == 5)
                paymentBillingMenu();
            else if (choice == 6)
                reportsMenu();
            else if (choice == 7)
                settingsMenu(loggedUser);
            else if (choice == 8)
                break;
            else
            {
                uiError("Invalid choice.");
                pressEnterToContinue();
            }
        }
        else
        {
            if (choice == 1)
                viewInventoryInteractive();
            else if (choice == 2)
                customerMenu();
            else if (choice == 3)
                rentCarMenu(loggedUser);
            else if (choice == 4)
                returnCar();
            else if (choice == 5)
                paymentBillingMenu();
            else if (choice == 6)
                reportsMenu();
            else if (choice == 7)
                break;
            else
            {
                uiError("Invalid choice.");
                pressEnterToContinue();
            }
        }
    } while (1);
}

/* ============= MAIN FUNCTION ============= */
int main()
{
    initializeSystem();
    while (1)
    {
        char loggedUser[USERNAME_LEN] = {0};
        int isAdmin = 0;
        int status = login(loggedUser, &isAdmin);
        if (status == -1)
        {
            printf("Exiting program...\n");
            break;
        }
        else if (status == 0)
        {
            setColor(CLR_RED);
            printf("Login Failed.\n");
            resetColor();
            pressEnterToContinue();
        }
        else
        {
            mainDashboard(loggedUser, isAdmin);
        }
    }
#ifndef NO_CURL
    curl_global_cleanup();
#endif
    return 0;
}