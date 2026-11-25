// QR Attendance System - OOPS project (C++17)
// Authors: Rudra Samania (25/SET/BCSE/453), Chirag Goyal (25/SET/BCSE/473)
// Faculty: Mrs. Shobha Tyagi
//
// OOP concepts used: abstraction, inheritance, polymorphism (virtual functions),
// encapsulation, operator overloading, exception handling, file handling, STL.
//
// QR encoding uses the open-source Nayuki QR Code generator (MIT licence).

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "qrcodegen.hpp"

using namespace std;
using qrcodegen::QrCode;

// ---------------------------------------------------------------- helpers
static string nowText() {
    time_t t = time(nullptr);
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", localtime(&t));
    return buf;
}

static vector<string> splitCsv(const string &line) {
    vector<string> out;
    string cell;
    stringstream ss(line);
    while (getline(ss, cell, ',')) out.push_back(cell);
    return out;
}

static string readLine(const string &prompt) {
    cout << prompt;
    string s;
    getline(cin, s);
    return s;
}

// ---------------------------------------------------------------- exceptions
class AttendanceError : public runtime_error {
public:
    explicit AttendanceError(const string &m) : runtime_error(m) {}
};

// ---------------------------------------------------------------- Person hierarchy
class Person {                       // abstract base class
protected:
    string name;
    string id;
public:
    Person(string n, string i) : name(move(n)), id(move(i)) {}
    virtual ~Person() = default;
    const string &getName() const { return name; }
    const string &getId() const { return id; }
    virtual string role() const = 0;                 // pure virtual
    virtual void display(ostream &os) const {
        os << left << setw(10) << role() << setw(18) << id << name;
    }
};

class Student : public Person {
    int presentCount = 0;
public:
    Student(string n, string rollNo) : Person(move(n), move(rollNo)) {}
    string role() const override { return "Student"; }
    void markPresent() { ++presentCount; }
    int getPresent() const { return presentCount; }
    void setPresent(int p) { presentCount = p; }
    void display(ostream &os) const override {
        Person::display(os);
        os << "  (present " << presentCount << ")";
    }
};

class Teacher : public Person {
public:
    Teacher(string n, string empId) : Person(move(n), move(empId)) {}
    string role() const override { return "Teacher"; }
};

ostream &operator<<(ostream &os, const Person &p) { p.display(os); return os; }

// ---------------------------------------------------------------- QR wrapper
class QRImage {
    QrCode qr;
public:
    explicit QRImage(const string &text)
        : qr(QrCode::encodeText(text.c_str(), QrCode::Ecc::MEDIUM)) {}

    // Terminal rendering using two characters per module (square-ish look).
    void print(ostream &os) const {
        const int border = 2;
        for (int y = -border; y < qr.getSize() + border; ++y) {
            for (int x = -border; x < qr.getSize() + border; ++x)
                os << (qr.getModule(x, y) ? "\xE2\x96\x88\xE2\x96\x88" : "  ");
            os << '\n';
        }
    }

    // Save as SVG so it can be shown on a projector or phone.
    void saveSvg(const string &file) const {
        const int border = 4;
        ofstream f(file);
        if (!f) throw AttendanceError("Cannot write " + file);
        int dim = qr.getSize() + border * 2;
        f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" viewBox=\"0 0 "
          << dim << ' ' << dim << "\" stroke=\"none\">\n"
          << "<rect width=\"100%\" height=\"100%\" fill=\"#FFFFFF\"/>\n<path d=\"";
        for (int y = 0; y < qr.getSize(); ++y)
            for (int x = 0; x < qr.getSize(); ++x)
                if (qr.getModule(x, y))
                    f << "M" << (x + border) << "," << (y + border) << "h1v1h-1z ";
        f << "\" fill=\"#000000\"/>\n</svg>\n";
    }
};

// ---------------------------------------------------------------- Session
class Session {
    string subject;
    string token;
    chrono::steady_clock::time_point started;
    int validSeconds;
    bool active = true;
    vector<string> presentIds;
public:
    Session(string subj, int validSec) : subject(move(subj)), validSeconds(validSec) {
        random_device rd;
        mt19937_64 gen(rd());
        stringstream ss;
        ss << "QRATT|" << subject << '|' << hex << gen();
        token = ss.str();
        started = chrono::steady_clock::now();
    }
    const string &getToken() const { return token; }
    const string &getSubject() const { return subject; }
    bool isActive() const { return active; }
    void close() { active = false; }
    bool expired() const {
        auto el = chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - started);
        return el.count() > validSeconds;
    }
    bool alreadyMarked(const string &id) const {
        return find(presentIds.begin(), presentIds.end(), id) != presentIds.end();
    }
    void record(const string &id) { presentIds.push_back(id); }
    size_t count() const { return presentIds.size(); }
};

// ---------------------------------------------------------------- Manager
class AttendanceManager {
    vector<unique_ptr<Student>> students;
    unique_ptr<Teacher> teacher;
    unique_ptr<Session> session;
    const string studentFile = "students.csv";
    const string logFile = "attendance_log.csv";

    Student *find(const string &roll) {
        for (auto &s : students)
            if (s->getId() == roll) return s.get();
        return nullptr;
    }
public:
    explicit AttendanceManager(string teacherName)
        : teacher(make_unique<Teacher>(move(teacherName), "T-001")) { load(); }

    const Teacher &getTeacher() const { return *teacher; }

    void addStudent(const string &name, const string &roll) {
        if (name.empty() || roll.empty()) throw AttendanceError("Name and roll number are required.");
        if (find(roll)) throw AttendanceError("Roll number already registered.");
        students.push_back(make_unique<Student>(name, roll));
        save();
    }

    void startSession(const string &subject, int seconds) {
        if (session && session->isActive()) throw AttendanceError("A session is already running.");
        session = make_unique<Session>(subject, seconds);
        QRImage img(session->getToken());
        cout << "\nScan this QR to mark attendance (valid " << seconds << "s):\n\n";
        img.print(cout);
        img.saveSvg("session_qr.svg");
        cout << "\nToken : " << session->getToken()
             << "\nSaved : session_qr.svg (open on projector)\n";
    }

    // 'scanned' is the text decoded from the QR by the student's phone / scanner.
    void markAttendance(const string &roll, const string &scanned) {
        if (!session || !session->isActive()) throw AttendanceError("No active session.");
        if (session->expired()) { session->close(); throw AttendanceError("QR code expired."); }
        Student *s = find(roll);
        if (!s) throw AttendanceError("Unknown roll number.");
        if (scanned != session->getToken()) throw AttendanceError("Invalid QR code.");
        if (session->alreadyMarked(roll)) throw AttendanceError("Attendance already marked.");
        session->record(roll);
        s->markPresent();
        ofstream log(logFile, ios::app);
        log << nowText() << ',' << session->getSubject() << ',' << roll << ',' << s->getName() << '\n';
        save();
        cout << "Marked present: " << s->getName() << "\n";
    }

    void endSession() {
        if (!session) throw AttendanceError("No session to end.");
        session->close();
        cout << "Session '" << session->getSubject() << "' closed. Present: "
             << session->count() << " / " << students.size() << '\n';
    }

    void listStudents() const {
        cout << "\n" << *teacher << "\n";
        for (auto &s : students) cout << *s << '\n';   // polymorphic call
    }

    void report() const {
        cout << "\n" << left << setw(16) << "Roll No" << setw(20) << "Name" << "Classes attended\n";
        for (auto &s : students)
            cout << setw(16) << s->getId() << setw(20) << s->getName() << s->getPresent() << '\n';
    }

    void exportCsv(const string &file) const {
        ofstream f(file);
        if (!f) throw AttendanceError("Cannot write " + file);
        f << "RollNo,Name,Present\n";
        for (auto &s : students) f << s->getId() << ',' << s->getName() << ',' << s->getPresent() << '\n';
    }

    bool hasLiveSession() const { return session && session->isActive() && !session->expired(); }
    string liveToken() const { return session ? session->getToken() : ""; }

private:
    void save() const {
        ofstream f(studentFile);
        for (auto &s : students) f << s->getId() << ',' << s->getName() << ',' << s->getPresent() << '\n';
    }
    void load() {
        ifstream f(studentFile);
        string line;
        while (getline(f, line)) {
            auto c = splitCsv(line);
            if (c.size() < 3) continue;
            auto s = make_unique<Student>(c[1], c[0]);
            s->setPresent(stoi(c[2]));
            students.push_back(move(s));
        }
    }
};

// ---------------------------------------------------------------- UI
static void menu() {
    cout << "\n===== QR ATTENDANCE SYSTEM =====\n"
            "1. Register student\n"
            "2. Start class session (generate QR)\n"
            "3. Mark attendance (student scan)\n"
            "4. End session\n"
            "5. List people\n"
            "6. Attendance report\n"
            "7. Export report to CSV\n"
            "0. Exit\n";
}

int main() {
    AttendanceManager mgr("Mrs. Shobha Tyagi");
    while (true) {
        menu();
        string ch = readLine("Choice: ");
        try {
            if (ch == "1") {
                string n = readLine("Student name: ");
                string r = readLine("Roll number : ");
                mgr.addStudent(n, r);
                cout << "Registered.\n";
            } else if (ch == "2") {
                string subj = readLine("Subject code: ");
                string secs = readLine("QR validity in seconds [120]: ");
                mgr.startSession(subj, secs.empty() ? 120 : stoi(secs));
            } else if (ch == "3") {
                string r = readLine("Roll number: ");
                string t = readLine("Scanned QR text (paste decoded text): ");
                mgr.markAttendance(r, t);
            } else if (ch == "4") {
                mgr.endSession();
            } else if (ch == "5") {
                mgr.listStudents();
            } else if (ch == "6") {
                mgr.report();
            } else if (ch == "7") {
                mgr.exportCsv("attendance_report.csv");
                cout << "Saved attendance_report.csv\n";
            } else if (ch == "0") {
                break;
            } else {
                cout << "Invalid choice.\n";
            }
        } catch (const AttendanceError &e) {
            cout << "Error: " << e.what() << '\n';
        } catch (const exception &e) {
            cout << "Input error: " << e.what() << '\n';
        }
        if (!cin) break;
    }
    cout << "Goodbye.\n";
    return 0;
}
