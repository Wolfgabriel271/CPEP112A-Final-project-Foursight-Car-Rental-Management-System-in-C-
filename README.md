# 🚗 Foursight Car Rentals

A console-based **Car Rental Management System** written in C. Foursight handles the full rental lifecycle — inventory, customers, rentals, returns, billing, and reporting — through a colored, menu-driven terminal interface, backed by a lightweight text-file database with no external DB server required.

---

## ✨ Features

- **Inventory Management** — add, view, and track cars (plate, model, daily rate, availability, rental count)
- **Customer Management** — add, edit, delete, and search customer records (name, license, contact, email)
- **Rentals (with bulk support)** — rent multiple cars in a single transaction, auto-calculated subtotals and totals
- **Returns & Late Fees** — automatic late-fee calculation based on expected vs. actual return date
- **Payment / Billing** — cash handling with change calculation and itemized receipts
- **Reports** — inventory, customer, and rental reports on demand
- **Barcode Generation** — per-rental barcodes rendered as PNG via the `zint` CLI tool
- **Receipts** — auto-generated text receipts with auto-print/auto-open support, plus reprint-from-history
- **Email Receipts** — optional SMTP delivery via `libcurl` (can be disabled at build time)
- **User Roles** — Admin and Cashier accounts with permission-gated menus
- **User Management (Admin)** — add/delete users, reset passwords, toggle admin role
- **Security** — SHA-256 password hashing, login attempt tracking, and timed lockouts after repeated failures
- **Input Validation** — strict validation for plates, licenses, contacts, emails, dates, and currency fields
- **Custom Date Engine** — leap-year aware date math for due dates, overdue detection, and "approaching return" warnings
- **Cross-Platform** — conditional builds for Windows (`conio.h`, `windows.h`) and POSIX (`termios.h`, `unistd.h`)

---

## 🛠️ Tech Stack

| Component        | Details                                  |
|-------------------|-------------------------------------------|
| Language          | C (C99+)                                 |
| Data Storage      | Flat text files (`inventory.txt`, `customers.txt`, `rentals.txt`, `users.txt`) |
| Security          | Custom SHA-256 implementation             |
| Barcodes          | [`zint`](https://www.zint.org.uk/) CLI tool |
| Email (optional)  | `libcurl` (SMTP)                          |
| Platforms         | Windows, Linux/macOS (POSIX)              |

---

## 📦 Prerequisites

- A C compiler (GCC, Clang, or MSVC)
- [`zint`](https://www.zint.org.uk/) installed and available on your system `PATH` (for barcode/receipt generation)
- `libcurl` development headers (only required if email receipts are enabled)

---

## ⚙️ Build Instructions

**Linux / macOS** (with email support, requires libcurl):
```bash
gcc Foursight-final-patch.c -o foursight -lcurl
```

**Linux / macOS** (without email support):
```bash
gcc Foursight-final-patch.c -o foursight -DNO_CURL
```

**Windows (MinGW)**:
```bash
gcc Foursight-final-patch.c -o foursight.exe -lcurl -lws2_32
```

> If `libcurl` isn't installed and you don't need email receipts, build with `-DNO_CURL` to skip that dependency entirely.

---

## ▶️ Usage

```bash
./foursight
```

1. Log in with an Admin or Cashier account (default accounts are seeded on first run if `users.txt` is empty).
2. Use the main dashboard to manage inventory, customers, rentals, returns, billing, and reports.
3. Admins get access to **Settings** and **User Management**; Cashiers see the core rental operations only.

On first launch, the program automatically creates the required data files and directories (`receipts/`, `barcodes/`) if they don't already exist.

---

## 🔐 Security Notes

- Passwords are never stored in plaintext — they're hashed with SHA-256 before being written to `users.txt`.
- Repeated failed logins trigger a temporary lockout (3 attempts / 5-minute cooldown) to slow down brute-force attempts.
- Older plaintext passwords (if present from a prior version) are automatically migrated to hashed form on load.

---

## 📁 Project Structure (runtime)

```
.
├── foursight                # compiled binary
├── config.txt                # SMTP/email config (optional)
├── inventory.txt             # car inventory DB
├── customers.txt             # customer DB
├── rentals.txt               # rental records DB
├── users.txt                 # user accounts DB
├── receipts/                 # generated receipt files
└── barcodes/                 # generated barcode PNGs
```

---

## 📌 Roadmap Ideas

- Migrate flat-file storage to SQLite for better data integrity at scale
- Add a GUI or web front-end on top of the existing core logic
- Export reports to CSV/PDF

---

## 📄 License

Specify your license here (e.g., MIT, GPL-3.0) — add a `LICENSE` file to the repo to make this official.
