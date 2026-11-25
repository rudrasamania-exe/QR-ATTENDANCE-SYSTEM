# QR Attendance System (C++17)

OOPS project by Rudra Samania (25/SET/BCSE/453) and Chirag Goyal (25/SET/BCSE/473).
Faculty: Mrs. Shobha Tyagi.

## Build
    g++ -std=c++17 -O2 main.cpp qrcodegen.cpp -o qr_attendance

## Run
    ./qr_attendance

## How it works
1. Register students (name + roll number).
2. Teacher starts a session: a random token is generated and shown as a QR code
   in the terminal and saved as `session_qr.svg` for the projector.
3. Student scans the QR; the decoded text plus their roll number is entered
   (option 3). The system checks: session active, QR not expired, roll number
   registered, token matches, not already marked.
4. Data is stored in `students.csv` and `attendance_log.csv`; a summary can be
   exported to `attendance_report.csv`.

## OOP concepts
Abstraction (Person), inheritance (Student, Teacher), polymorphism (virtual
role()/display()), encapsulation (Session, AttendanceManager), operator
overloading (<<), exceptions (AttendanceError), file handling, STL containers,
smart pointers.

QR encoding: Nayuki QR Code generator (MIT licence), included as qrcodegen.cpp/.hpp.
