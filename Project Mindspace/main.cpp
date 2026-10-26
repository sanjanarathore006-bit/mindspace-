// =====================================================================
//  MINDSPACE - A Private Mental Health Support System (C++17)
//  DSA used : Trie (phrase matching), Max-Heap (priority_queue),
//             unordered_map (lookups), Sliding window (deque), Sorting
//  OOP used : Abstraction, Inheritance, Polymorphism, Encapsulation,
//             Operator overloading, File handling, Smart pointers
//  NOTE     : Student prototype. NOT a clinical tool. Use fictional data.
//  Build    : g++ -std=c++17 main.cpp -o mindspace
//  Run      : .\mindspace          (tests: .\mindspace test)
//  Website  : g++ -std=c++17 -DWITH_WEB -Ithird_party main.cpp -o mindspace_web -static -pthread -lws2_32
//             .\mindspace_web web      then open http://localhost:8080
// =====================================================================
#ifdef WITH_WEB          // website mode: g++ ... -DWITH_WEB -Ithird_party
#define NOMINMAX
#include "httplib.h"
#include "json.hpp"
#include <mutex>
#include <random>
#include <chrono>
#endif
#include <iostream>
#include <string>
#include <vector>
#include <deque>
#include <queue>
#include <unordered_map>
#include <memory>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cctype>
#include <algorithm>
#include <functional>
#include <cstdlib>
using namespace std;

// ---------------------------------------------------------------------
//  Basic types and helpers
// ---------------------------------------------------------------------
enum class Severity { None = 0, Low = 1, Medium = 2, High = 3 };
enum class Status { Created, Queued, Sent, Acknowledged, Reviewed, Closed };
enum class Sharing { Private = 0, Shared = 1 };
enum class Notify { None, AskPatient, AlertCounselor };

string sevName(Severity s) {
    switch (s) {
        case Severity::Low: return "LOW";
        case Severity::Medium: return "MEDIUM";
        case Severity::High: return "HIGH";
        default: return "NONE";
    }
}
string statusName(Status s) {
    static const char* n[] = {"Created", "Queued", "Sent", "Acknowledged", "Reviewed", "Closed"};
    return n[static_cast<int>(s)];
}
string timeStr(time_t t) {
    char buf[32];
    tm* lt = localtime(&t);
    strftime(buf, sizeof buf, "%d-%m-%Y %H:%M", lt);
    return buf;
}
string hashPw(const string& salt, const string& pw) {  // demo hash; bcrypt/Argon2 in real use
    return to_string(hash<string>{}(salt + "#" + pw));
}
string clean(string s) {  // keep the data file format safe
    for (char& c : s) if (c == '|' || c == '\n' || c == '\r') c = '/';
    return s;
}
vector<string> splitBar(const string& line) {  // keeps empty fields
    vector<string> out; string cur;
    for (char c : line) { if (c == '|') { out.push_back(cur); cur.clear(); } else cur += c; }
    out.push_back(cur);
    return out;
}
string normalize(const string& s) {  // lowercase, punctuation -> single spaces
    string out; bool lastSpace = true;
    for (unsigned char c : s) {
        if (isalnum(c)) { out += static_cast<char>(tolower(c)); lastSpace = false; }
        else if (!lastSpace) { out += ' '; lastSpace = true; }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// ---------------------------------------------------------------------
//  Simple records
// ---------------------------------------------------------------------
struct MoodRecord {
    int value; time_t time; string note;
};
ostream& operator<<(ostream& os, const MoodRecord& m) {  // operator overloading
    return os << timeStr(m.time) << "  mood " << m.value << "/10" << (m.note.empty() ? "" : "  (" + m.note + ")");
}

struct JournalEntry {
    int id; int ownerId; string text; time_t time; Sharing sharing;
};
ostream& operator<<(ostream& os, const JournalEntry& e) {
    return os << "#" << e.id << "  " << timeStr(e.time) << "  ["
              << (e.sharing == Sharing::Shared ? "Shared" : "Private") << "]  " << e.text;
}

// ---------------------------------------------------------------------
//  Sliding window: moods of the last 7 days (O(1) amortized per update)
// ---------------------------------------------------------------------
class MoodTrend {
    deque<MoodRecord> window;
    long sum = 0;
    static constexpr long WINDOW_SECONDS = 7L * 24 * 3600;
public:
    void add(const MoodRecord& r) {
        window.push_back(r);
        sum += r.value;
        refresh(r.time);
    }
    void refresh(time_t now) {
        while (!window.empty() && window.front().time < now - WINDOW_SECONDS) {
            sum -= window.front().value;
            window.pop_front();
        }
    }
    double average() const { return window.empty() ? 0.0 : static_cast<double>(sum) / window.size(); }
    size_t count() const { return window.size(); }
    const deque<MoodRecord>& records() const { return window; }
};

// ---------------------------------------------------------------------
//  User hierarchy (Abstraction + Inheritance + Encapsulation)
// ---------------------------------------------------------------------
class User {
protected:
    int id; string pseudonym; string salt; string pwHash;
public:
    User(int i, string p, string s, string h) : id(i), pseudonym(move(p)), salt(move(s)), pwHash(move(h)) {}
    virtual ~User() = default;
    virtual string role() const = 0;  // pure virtual -> abstract class
    int getId() const { return id; }
    const string& getPseudonym() const { return pseudonym; }
    const string& getSalt() const { return salt; }
    const string& getHash() const { return pwHash; }
    bool checkPassword(const string& pw) const { return hashPw(salt, pw) == pwHash; }
};

class Patient : public User {
    vector<MoodRecord> moods;
    MoodTrend trend;
public:
    using User::User;
    string role() const override { return "patient"; }
    void addMood(const MoodRecord& m) { moods.push_back(m); trend.add(m); }
    const vector<MoodRecord>& allMoods() const { return moods; }
    MoodTrend& getTrend() { return trend; }
};

class Counselor : public User {
public:
    using User::User;
    string role() const override { return "counselor"; }
};

// ---------------------------------------------------------------------
//  Calming activities (Polymorphism through virtual functions)
// ---------------------------------------------------------------------
class Activity {
public:
    virtual ~Activity() = default;
    virtual string name() const = 0;
    virtual void run() const = 0;
};
class BreathingExercise : public Activity {
public:
    string name() const override { return "Breathing exercise (4-4-6)"; }
    void run() const override {
        cout << "\n  Sit comfortably. Repeat this 5 times:\n"
             << "    1. Breathe IN through your nose for 4 seconds\n"
             << "    2. HOLD for 4 seconds\n"
             << "    3. Breathe OUT slowly through your mouth for 6 seconds\n"
             << "  Let your shoulders drop with every breath out.\n";
    }
};
class GroundingExercise : public Activity {
public:
    string name() const override { return "Grounding exercise (5-4-3-2-1)"; }
    void run() const override {
        cout << "\n  Look around you and name:\n"
             << "    5 things you can SEE\n    4 things you can TOUCH\n"
             << "    3 things you can HEAR\n    2 things you can SMELL\n"
             << "    1 thing you can TASTE\n  This brings your attention back to the present.\n";
    }
};

// ---------------------------------------------------------------------
//  Trie-based phrase detector (explainable rules, simple negation check)
// ---------------------------------------------------------------------
struct Match { string ruleId; string phrase; int weight; };
struct ScanResult {
    Severity severity = Severity::None;
    int score = 0;
    vector<Match> matches;     // rules that fired
    vector<Match> suppressed;  // matched but negated ("not hopeless")
};

class CrisisDetector {
    struct Node {
        unordered_map<char, unique_ptr<Node>> next;
        bool end = false; int weight = 0; string rule, phrase;
    };
    unique_ptr<Node> root = make_unique<Node>();

    void insert(const string& phrase, int weight, const string& rule) {
        Node* cur = root.get();
        for (char c : phrase) {
            auto& n = cur->next[c];
            if (!n) n = make_unique<Node>();
            cur = n.get();
        }
        cur->end = true; cur->weight = weight; cur->rule = rule; cur->phrase = phrase;
    }
    // longest whole-word phrase starting at index i
    bool matchAt(const string& t, size_t i, Match& m, size_t& len) const {
        const Node* cur = root.get(); bool found = false;
        for (size_t j = i; j < t.size(); ++j) {
            auto it = cur->next.find(t[j]);
            if (it == cur->next.end()) break;
            cur = it->second.get();
            bool boundary = (j + 1 == t.size()) || t[j + 1] == ' ';
            if (cur->end && boundary) { m = {cur->rule, cur->phrase, cur->weight}; len = j - i + 1; found = true; }
        }
        return found;
    }
    // is one of the 3 words before position i a negation word?
    static bool negatedBefore(const string& t, size_t i) {
        static const char* neg[] = {"not", "no", "never", "t", "dont", "cannot"};
        vector<string> words; string cur;
        for (size_t k = 0; k < i; ++k) {
            if (t[k] == ' ') { if (!cur.empty()) words.push_back(cur); cur.clear(); } else cur += t[k];
        }
        if (!cur.empty()) words.push_back(cur);
        int checked = 0;
        for (int k = static_cast<int>(words.size()) - 1; k >= 0 && checked < 3; --k, ++checked)
            for (const char* n : neg) if (words[k] == n) return true;
        return false;
    }
public:
    CrisisDetector() {
        // Synthetic demo rules: strong = 5, moderate = 2, mild = 1
        insert("kill myself", 5, "R01");      insert("end my life", 5, "R02");
        insert("want to die", 5, "R03");      insert("suicide", 5, "R04");
        insert("hurt myself", 5, "R05");      insert("self harm", 5, "R06");
        insert("no reason to live", 5, "R07");insert("can t go on", 2, "R08");
        insert("no one cares", 2, "R09");     insert("hate myself", 2, "R10");
        insert("hopeless", 1, "R11");         insert("worthless", 1, "R12");
        insert("give up", 1, "R13");          insert("feel empty", 1, "R14");
        insert("panic attack", 1, "R15");     insert("very anxious", 1, "R16");
        insert("so lonely", 1, "R17");
    }
    ScanResult scan(const string& raw) const {
        string t = normalize(raw);
        ScanResult r;
        for (size_t i = 0; i < t.size(); ++i) {
            if (i != 0 && t[i - 1] != ' ') continue;  // only at word starts
            Match m; size_t len = 0;
            if (matchAt(t, i, m, len)) {
                if (negatedBefore(t, i)) r.suppressed.push_back(m);
                else { r.matches.push_back(m); r.score += m.weight; }
                i += len - 1;
            }
        }
        r.severity = r.score >= 5 ? Severity::High : r.score >= 3 ? Severity::Medium
                   : r.score >= 1 ? Severity::Low : Severity::None;
        return r;
    }
};

// ---------------------------------------------------------------------
//  Alerts: max-heap priority queue + lifecycle
// ---------------------------------------------------------------------
struct Alert {
    int id; int patientId; string pseudonym; Severity severity;
    time_t created; Status status; string rules;  // rule IDs only, never entry text
    bool operator<(const Alert& o) const {        // used by the max-heap
        if (severity != o.severity) return severity < o.severity;  // higher severity first
        return id > o.id;                                            // older alert first on ties
    }
};
ostream& operator<<(ostream& os, const Alert& a) {
    return os << "#" << a.id << "  [" << sevName(a.severity) << "]  patient: " << a.pseudonym
              << "  rules: " << a.rules << "  created: " << timeStr(a.created) << "  status: " << statusName(a.status);
}

class AlertManager {
    priority_queue<Alert> heap;      // only Queued alerts
    unordered_map<int, Alert> all;   // lookup by id: O(1) average
    int nextId = 1;
public:
    int raise(int pid, const string& pseud, Severity s, const string& rules, time_t t) {
        Alert a{nextId++, pid, pseud, s, t, Status::Created, rules};
        all[a.id] = a;
        a.status = Status::Queued; all[a.id].status = Status::Queued;
        heap.push(a);                // O(log n)
        return a.id;
    }
    void restore(const Alert& a) {
        all[a.id] = a;
        if (a.status == Status::Queued) heap.push(a);
        nextId = max(nextId, a.id + 1);
    }
    vector<Alert> queuedInOrder() const {
        auto copy = heap; vector<Alert> v;
        while (!copy.empty()) { v.push_back(copy.top()); copy.pop(); }
        return v;
    }
    vector<Alert> others() const {   // not queued, sorted by id
        vector<Alert> v;
        for (auto& kv : all) if (kv.second.status != Status::Queued) v.push_back(kv.second);
        sort(v.begin(), v.end(), [](const Alert& a, const Alert& b) { return a.id < b.id; });
        return v;
    }
    vector<Alert> everything() const {
        vector<Alert> v;
        for (auto& kv : all) v.push_back(kv.second);
        sort(v.begin(), v.end(), [](const Alert& a, const Alert& b) { return a.id < b.id; });
        return v;
    }
    bool popNext(Alert& out) {       // most urgent first
        if (heap.empty()) return false;
        int id = heap.top().id; heap.pop();
        all[id].status = Status::Sent;
        out = all[id];
        return true;
    }
    bool advance(int id, string& msg) {
        auto it = all.find(id);
        if (it == all.end()) { msg = "No such alert."; return false; }
        Status& s = it->second.status;
        if (s == Status::Queued) { msg = "Still queued. Use 'Take next urgent alert' first."; return false; }
        if (s == Status::Closed) { msg = "Already closed."; return false; }
        s = static_cast<Status>(static_cast<int>(s) + 1);
        msg = "Alert #" + to_string(id) + " is now: " + statusName(s);
        return true;
    }
};

// ---------------------------------------------------------------------
//  Consent rules (what the counselor may receive)
// ---------------------------------------------------------------------
struct ConsentPolicy {
    static Notify decide(Severity s) {
        if (s == Severity::High) return Notify::AlertCounselor;  // minimal alert, no text
        if (s == Severity::Low || s == Severity::Medium) return Notify::AskPatient;
        return Notify::None;
    }
};
bool canRead(const User& u, const JournalEntry& e) {  // access control
    if (u.role() == "patient") return u.getId() == e.ownerId;
    if (u.role() == "counselor") return e.sharing == Sharing::Shared;
    return false;
}

// ---------------------------------------------------------------------
//  Application (CLI)
// ---------------------------------------------------------------------
class WebApi;   // website layer (only built with -DWITH_WEB)

class App {
    friend class WebApi;
    unordered_map<int, unique_ptr<User>> users;
    unordered_map<string, int> byPseudonym;
    vector<JournalEntry> entries;
    AlertManager alerts;
    CrisisDetector detector;
    int nextUserId = 1, nextEntryId = 1;
    User* current = nullptr;
    const string DATA = "mindspace_data.txt";

    // ---------- input helpers ----------
    static string ask(const string& prompt) {
        cout << prompt; string s;
        if (!getline(cin, s)) { cout << "\n"; exit(0); }
        return s;
    }
    static int askInt(const string& prompt, int lo, int hi) {
        while (true) {
            string s = ask(prompt);
            try { int v = stoi(s); if (v >= lo && v <= hi) return v; } catch (...) {}
            cout << "  Please enter a number from " << lo << " to " << hi << ".\n";
        }
    }
    static bool yesNo(const string& prompt) {
        string s = ask(prompt + " (y/n): ");
        return !s.empty() && (s[0] == 'y' || s[0] == 'Y');
    }

    // ---------- storage ----------
    void save() const {
        ofstream f(DATA);
        for (auto& kv : users) {
            const User& u = *kv.second;
            f << "U|" << u.role() << "|" << u.getId() << "|" << u.getPseudonym() << "|" << u.getSalt() << "|" << u.getHash() << "\n";
            if (u.role() == "patient")
                for (auto& m : static_cast<const Patient&>(u).allMoods())
                    f << "M|" << u.getId() << "|" << m.value << "|" << m.time << "|" << clean(m.note) << "\n";
        }
        for (auto& e : entries)
            f << "E|" << e.id << "|" << e.ownerId << "|" << e.time << "|" << static_cast<int>(e.sharing) << "|" << clean(e.text) << "\n";
        for (auto& a : alerts.everything())
            f << "A|" << a.id << "|" << a.patientId << "|" << a.pseudonym << "|" << static_cast<int>(a.severity)
              << "|" << a.created << "|" << static_cast<int>(a.status) << "|" << a.rules << "\n";
    }
    void load() {
        ifstream f(DATA);
        if (!f) return;
        string line;
        while (getline(f, line)) {
            auto p = splitBar(line);
            try {
                if (p[0] == "U" && p.size() >= 6) {
                    int id = stoi(p[2]);
                    unique_ptr<User> u;
                    if (p[1] == "patient") u = make_unique<Patient>(id, p[3], p[4], p[5]);
                    else u = make_unique<Counselor>(id, p[3], p[4], p[5]);
                    byPseudonym[p[3]] = id; users[id] = move(u);
                    nextUserId = max(nextUserId, id + 1);
                } else if (p[0] == "M" && p.size() >= 5) {
                    auto it = users.find(stoi(p[1]));
                    if (it != users.end() && it->second->role() == "patient")
                        static_cast<Patient&>(*it->second).addMood({stoi(p[2]), static_cast<time_t>(stoll(p[3])), p[4]});
                } else if (p[0] == "E" && p.size() >= 6) {
                    JournalEntry e{stoi(p[1]), stoi(p[2]), p[5], static_cast<time_t>(stoll(p[3])), static_cast<Sharing>(stoi(p[4]))};
                    entries.push_back(e); nextEntryId = max(nextEntryId, e.id + 1);
                } else if (p[0] == "A" && p.size() >= 8) {
                    Alert a{stoi(p[1]), stoi(p[2]), p[3], static_cast<Severity>(stoi(p[4])),
                            static_cast<time_t>(stoll(p[5])), static_cast<Status>(stoi(p[6])), p[7]};
                    alerts.restore(a);
                }
            } catch (...) { /* skip a damaged line */ }
        }
    }

    // ---------- accounts ----------
    void seedCounselor() {
        if (byPseudonym.count("counselor1")) return;
        string salt = to_string(rand());
        int id = nextUserId++;
        users[id] = make_unique<Counselor>(id, "counselor1", salt, hashPw(salt, "counselor123"));
        byPseudonym["counselor1"] = id;
        save();
    }
    void registerPatient() {
        cout << "\n--- Register (use a pseudonym, not your real name) ---\n";
        string name = ask("Pseudonym: ");
        if (name.empty() || name.find('|') != string::npos || name.find(' ') != string::npos) {
            cout << "  Pseudonym must not be empty and must not contain spaces or '|'.\n"; return;
        }
        if (byPseudonym.count(name)) { cout << "  That pseudonym is taken.\n"; return; }
        string pw = ask("Password (min 4 characters): ");
        if (pw.size() < 4) { cout << "  Password too short.\n"; return; }
        cout << "\n  CONSENT: Your entries are PRIVATE by default. Only entries you mark 'Share' are visible\n"
             << "  to the counselor. If a HIGH-severity phrase is detected, a minimal alert (your pseudonym,\n"
             << "  severity and time - NO entry text) is sent to the counselor and you are told immediately.\n"
             << "  This is a student prototype, not a medical service.\n";
        if (!yesNo("  Do you accept?")) { cout << "  Registration cancelled.\n"; return; }
        string salt = to_string(rand());
        int id = nextUserId++;
        users[id] = make_unique<Patient>(id, name, salt, hashPw(salt, pw));
        byPseudonym[name] = id;
        save();
        cout << "  Registered. You can log in now.\n";
    }
    void login() {
        cout << "\n--- Login ---\n";
        string name = ask("Pseudonym: "), pw = ask("Password: ");
        auto it = byPseudonym.find(name);
        if (it == byPseudonym.end() || !users[it->second]->checkPassword(pw)) {
            cout << "  Wrong pseudonym or password.\n"; return;
        }
        current = users[it->second].get();
        cout << "  Welcome, " << current->getPseudonym() << ".\n";
        if (current->role() == "patient") patientMenu(static_cast<Patient&>(*current));
        else counselorMenu();
        current = nullptr;
    }

    // ---------- patient side ----------
    void helpline() const {
        cout << "\n  If you are in distress, please talk to someone now: Tele-MANAS (India) 14416.\n"
             << "  If you are in immediate danger, call your local emergency number.\n";
    }
    void exercises() const {
        vector<unique_ptr<Activity>> list;       // polymorphism: base-class pointers
        list.push_back(make_unique<BreathingExercise>());
        list.push_back(make_unique<GroundingExercise>());
        cout << "\nCalming activities:\n";
        for (size_t i = 0; i < list.size(); ++i) cout << "  " << i + 1 << ". " << list[i]->name() << "\n";
        int c = askInt("Choose (0 to go back): ", 0, static_cast<int>(list.size()));
        if (c > 0) list[c - 1]->run();
    }
    void moodCheckin(Patient& p) {
        int v = askInt("\nHow are you feeling right now? (1 = very low, 10 = very good): ", 1, 10);
        string note = ask("Short note (optional): ");
        p.addMood({v, time(nullptr), note});
        save();
        cout << "  Mood saved.\n";
    }
    void showTrend(Patient& p) {
        p.getTrend().refresh(time(nullptr));
        auto& t = p.getTrend();
        cout << "\n--- Last 7 days ---\n";
        if (t.count() == 0) { cout << "  No check-ins in the last 7 days.\n"; return; }
        for (auto& m : t.records()) cout << "  " << timeStr(m.time) << "  " << string(m.value, '#') << " " << m.value << "\n";
        cout << "  Check-ins: " << t.count() << "   Average mood: " << t.average() << "/10\n"
             << "  (This is a reflection summary of what you entered, not a health assessment.)\n";
    }
    void writeEntry(Patient& p) {
        cout << "\n--- New journal entry ---\n";
        string text = ask("Write what is on your mind:\n> ");
        if (text.empty()) { cout << "  Nothing written.\n"; return; }
        cout << "Who can see this entry?\n  1. Only me (Private)\n  2. Share with my counselor\n";
        Sharing sh = askInt("Choose: ", 1, 2) == 2 ? Sharing::Shared : Sharing::Private;
        entries.push_back({nextEntryId++, p.getId(), text, time(nullptr), sh});
        ScanResult r = detector.scan(text);
        cout << "  Entry saved as " << (sh == Sharing::Shared ? "SHARED" : "PRIVATE") << ".\n";

        if (!r.suppressed.empty() && r.severity == Severity::None)
            cout << "  (A phrase appeared in a negated context, so nothing was flagged. Rules can misread context.)\n";
        if (r.severity != Severity::None) {
            cout << "\n  Some words in your entry matched our support rules (this is not a diagnosis):\n";
            for (auto& m : r.matches) cout << "    - rule " << m.ruleId << ": \"" << m.phrase << "\"\n";
            string ids;
            for (auto& m : r.matches) ids += (ids.empty() ? "" : ",") + m.ruleId;
            Notify n = ConsentPolicy::decide(r.severity);
            if (n == Notify::AlertCounselor) {
                alerts.raise(p.getId(), p.getPseudonym(), r.severity, ids, time(nullptr));
                cout << "  A minimal alert (no entry text) was sent to the counselor, as you agreed at registration.\n";
            } else if (n == Notify::AskPatient) {
                if (yesNo("  Would you like to let the counselor know you could use support?")) {
                    alerts.raise(p.getId(), p.getPseudonym(), r.severity, ids, time(nullptr));
                    cout << "  Done. The counselor received a minimal alert (no entry text).\n";
                }
            }
            helpline();
            if (yesNo("\n  Try a calming exercise now?")) exercises();
        }
        save();
    }
    void myEntries(Patient& p) {
        cout << "\n--- My entries ---\n";
        bool any = false;
        for (auto& e : entries) if (canRead(p, e)) { cout << "  " << e << "\n"; any = true; }
        if (!any) { cout << "  No entries yet.\n"; return; }
        int id = askInt("Enter an entry # to switch Private/Shared (0 to go back): ", 0, 1000000);
        if (id == 0) return;
        for (auto& e : entries)
            if (e.id == id && e.ownerId == p.getId()) {   // owner check
                e.sharing = e.sharing == Sharing::Shared ? Sharing::Private : Sharing::Shared;
                cout << "  Entry #" << id << " is now " << (e.sharing == Sharing::Shared ? "SHARED" : "PRIVATE") << ".\n";
                save(); return;
            }
        cout << "  Entry not found.\n";
    }
    void patientMenu(Patient& p) {
        while (true) {
            cout << "\n===== MindSpace (patient: " << p.getPseudonym() << ") =====\n"
                 << " 1. Mood check-in\n 2. My 7-day mood trend\n 3. Write a journal entry\n"
                 << " 4. My entries (and sharing)\n 5. Calming exercises\n 6. Helpline information\n 7. Logout\n";
            switch (askInt("Choose: ", 1, 7)) {
                case 1: moodCheckin(p); break;
                case 2: showTrend(p); break;
                case 3: writeEntry(p); break;
                case 4: myEntries(p); break;
                case 5: exercises(); break;
                case 6: helpline(); break;
                case 7: return;
            }
        }
    }

    // ---------- counselor side ----------
    void viewAlerts() const {
        cout << "\n--- Alerts (most urgent first) ---\n";
        auto q = alerts.queuedInOrder();
        if (q.empty()) cout << "  No queued alerts.\n";
        for (auto& a : q) cout << "  " << a << "\n";
        auto o = alerts.others();
        if (!o.empty()) { cout << "--- Already handled / in progress ---\n"; for (auto& a : o) cout << "  " << a << "\n"; }
    }
    void takeNext() {
        Alert a;
        if (!alerts.popNext(a)) { cout << "  No queued alerts.\n"; return; }
        cout << "\n  Most urgent alert:\n    " << a << "\n"
             << "  The alert contains no journal text. Patient shared entries are under option 4.\n";
        save();
    }
    void updateStatus() {
        viewAlerts();
        int id = askInt("Alert # to move to the next status (0 to go back): ", 0, 1000000);
        if (id == 0) return;
        string msg; alerts.advance(id, msg);
        cout << "  " << msg << "\n";
        save();
    }
    void sharedEntries() const {
        cout << "\n--- Entries patients chose to share ---\n";
        bool any = false;
        for (auto& e : entries) {
            if (!canRead(*current, e)) continue;
            auto it = users.find(e.ownerId);
            cout << "  " << (it != users.end() ? it->second->getPseudonym() : "?") << ": " << e << "\n";
            any = true;
        }
        if (!any) cout << "  Nothing shared yet.\n";
    }
    void counselorMenu() {
        while (true) {
            cout << "\n===== MindSpace (counselor dashboard) =====\n"
                 << " 1. View alerts\n 2. Take next urgent alert\n 3. Update alert status\n"
                 << " 4. View shared entries\n 5. Logout\n";
            switch (askInt("Choose: ", 1, 5)) {
                case 1: viewAlerts(); break;
                case 2: takeNext(); break;
                case 3: updateStatus(); break;
                case 4: sharedEntries(); break;
                case 5: return;
            }
        }
    }
public:
    void init() {
        srand(static_cast<unsigned>(time(nullptr)));
        load(); seedCounselor();
    }
    void run() {
        init();
        cout << "=====================================================\n"
             << "  MINDSPACE - a private mental health support system\n"
             << "  Student prototype. Not a medical or emergency service.\n"
             << "  Demo counselor login: counselor1 / counselor123\n"
             << "=====================================================\n";
        while (true) {
            cout << "\n1. Register (patient)\n2. Login\n3. Exit\n";
            switch (askInt("Choose: ", 1, 3)) {
                case 1: registerPatient(); break;
                case 2: login(); break;
                case 3: cout << "Take care. Goodbye.\n"; return;
            }
        }
    }
};

#ifdef WITH_WEB
// ---------------------------------------------------------------------
//  Website layer: thin HTTP/JSON routes over the SAME core classes.
//  No new business rules here - it calls the detector, ConsentPolicy,
//  AlertManager and canRead() exactly like the console version.
// ---------------------------------------------------------------------
using json = nlohmann::json;

class WebApi {
    App& app;
    mutex mtx;                              // one request at a time (simple and safe)
    unordered_map<string, int> sessions;    // token -> user id

    static void reply(httplib::Response& res, const json& j, int code = 200) {
        res.status = code;
        res.set_content(j.dump(), "application/json");
    }
    static void fail(httplib::Response& res, int code, const string& msg) { reply(res, {{"error", msg}}, code); }

    string newToken() {
        static mt19937_64 rng = [] {
            uint64_t seed = static_cast<uint64_t>(chrono::high_resolution_clock::now().time_since_epoch().count());
            try { random_device rd; seed ^= (static_cast<uint64_t>(rd()) << 32) ^ rd(); }
            catch (...) { /* some Windows builds cannot read random_device: clock seed is enough for a demo */ }
            return mt19937_64(seed);
        }();
        char buf[40]; snprintf(buf, sizeof buf, "%016llx%016llx",
            static_cast<unsigned long long>(rng()), static_cast<unsigned long long>(rng()));
        return buf;
    }
    User* auth(const httplib::Request& req) {
        auto it = sessions.find(req.get_header_value("X-Token"));
        if (it == sessions.end()) return nullptr;
        auto u = app.users.find(it->second);
        return u == app.users.end() ? nullptr : u->second.get();
    }
    Patient* patient(const httplib::Request& req, httplib::Response& res) {
        User* u = auth(req);
        if (!u) { fail(res, 401, "Please log in."); return nullptr; }
        if (u->role() != "patient") { fail(res, 403, "Patients only."); return nullptr; }
        return static_cast<Patient*>(u);
    }
    User* counselor(const httplib::Request& req, httplib::Response& res) {
        User* u = auth(req);
        if (!u) { fail(res, 401, "Please log in."); return nullptr; }
        if (u->role() != "counselor") { fail(res, 403, "Counselors only."); return nullptr; }
        return u;
    }
    static json body(const httplib::Request& req) {
        json b = json::parse(req.body, nullptr, false);
        return b.is_discarded() || !b.is_object() ? json::object() : b;
    }
    static json alertJson(const Alert& a) {
        return {{"id", a.id}, {"severity", sevName(a.severity)}, {"pseudonym", a.pseudonym},
                {"rules", a.rules}, {"created", timeStr(a.created)}, {"status", statusName(a.status)}};
    }
    static json entryJson(const JournalEntry& e) {
        return {{"id", e.id}, {"time", timeStr(e.time)}, {"shared", e.sharing == Sharing::Shared}, {"text", e.text}};
    }
    static string rulesOf(const ScanResult& r) {
        string ids; for (auto& m : r.matches) ids += (ids.empty() ? "" : ",") + m.ruleId; return ids;
    }
    JournalEntry* findEntry(int id) {
        for (auto& e : app.entries) if (e.id == id) return &e;
        return nullptr;
    }

public:
    explicit WebApi(App& a) : app(a) {}

    void mount(httplib::Server& svr) {
        // ---------- accounts ----------
        svr.Post("/api/register", [this](const httplib::Request& req, httplib::Response& res) {
            lock_guard<mutex> lk(mtx);
            json b = body(req);
            string name = b.value("pseudonym", ""), pw = b.value("password", "");
            if (!b.value("consent", false)) return fail(res, 400, "Please accept the consent to register.");
            if (name.empty() || name.find_first_of("| ") != string::npos) return fail(res, 400, "Pseudonym must not be empty or contain spaces.");
            if (app.byPseudonym.count(name)) return fail(res, 409, "That pseudonym is taken.");
            if (pw.size() < 4) return fail(res, 400, "Password must be at least 4 characters.");
            string salt = to_string(rand());
            int id = app.nextUserId++;
            app.users[id] = make_unique<Patient>(id, name, salt, hashPw(salt, pw));
            app.byPseudonym[name] = id;
            app.save();
            reply(res, {{"ok", true}});
        });
        svr.Post("/api/login", [this](const httplib::Request& req, httplib::Response& res) {
            lock_guard<mutex> lk(mtx);
            json b = body(req);
            auto it = app.byPseudonym.find(b.value("pseudonym", ""));
            if (it == app.byPseudonym.end() || !app.users[it->second]->checkPassword(b.value("password", "")))
                return fail(res, 401, "Wrong pseudonym or password.");
            string t = newToken();
            sessions[t] = it->second;
            reply(res, {{"token", t}, {"role", app.users[it->second]->role()}, {"pseudonym", app.users[it->second]->getPseudonym()}});
        });
        svr.Post("/api/logout", [this](const httplib::Request& req, httplib::Response& res) {
            lock_guard<mutex> lk(mtx);
            sessions.erase(req.get_header_value("X-Token"));
            reply(res, {{"ok", true}});
        });

        // ---------- patient: mood ----------
        svr.Post("/api/mood", [this](const httplib::Request& req, httplib::Response& res) {
            lock_guard<mutex> lk(mtx);
            Patient* p = patient(req, res); if (!p) return;
            json b = body(req);
            int v = b.value("value", 0);
            if (v < 1 || v > 10) return fail(res, 400, "Mood must be from 1 to 10.");
            p->addMood({v, time(nullptr), b.value("note", "")});
            app.save();
            reply(res, {{"ok", true}});
        });
        svr.Get("/api/trend", [this](const httplib::Request& req, httplib::Response& res) {
            lock_guard<mutex> lk(mtx);
            Patient* p = patient(req, res); if (!p) return;
            p->getTrend().refresh(time(nullptr));
            json recs = json::array();
            for (auto& m : p->getTrend().records()) recs.push_back({{"time", timeStr(m.time)}, {"value", m.value}, {"note", m.note}});
            reply(res, {{"count", p->getTrend().count()}, {"average", p->getTrend().average()}, {"records", recs}});
        });

        // ---------- patient: journal ----------
        svr.Post("/api/entry", [this](const httplib::Request& req, httplib::Response& res) {
            lock_guard<mutex> lk(mtx);
            Patient* p = patient(req, res); if (!p) return;
            json b = body(req);
            string text = b.value("text", "");
            if (text.empty()) return fail(res, 400, "Write something first.");
            Sharing sh = b.value("share", false) ? Sharing::Shared : Sharing::Private;
            int id = app.nextEntryId++;
            app.entries.push_back({id, p->getId(), text, time(nullptr), sh});
            ScanResult r = app.detector.scan(text);
            json matches = json::array();
            for (auto& m : r.matches) matches.push_back({{"rule", m.ruleId}, {"phrase", m.phrase}});
            string action = "none";
            Notify n = ConsentPolicy::decide(r.severity);
            if (n == Notify::AlertCounselor) {
                app.alerts.raise(p->getId(), p->getPseudonym(), r.severity, rulesOf(r), time(nullptr));
                action = "alerted";
            } else if (n == Notify::AskPatient) action = "ask";
            app.save();
            reply(res, {{"entryId", id}, {"shared", sh == Sharing::Shared}, {"severity", sevName(r.severity)},
                        {"matches", matches}, {"action", action},
                        {"negated", r.severity == Severity::None && !r.suppressed.empty()}});
        });
        svr.Post(R"(/api/entry/(\d+)/contact)", [this](const httplib::Request& req, httplib::Response& res) {
            lock_guard<mutex> lk(mtx);
            Patient* p = patient(req, res); if (!p) return;
            JournalEntry* e = findEntry(stoi(req.matches[1]));
            if (!e || e->ownerId != p->getId()) return fail(res, 404, "Entry not found.");
            ScanResult r = app.detector.scan(e->text);
            if (r.severity == Severity::None) return fail(res, 400, "Nothing to send.");
            app.alerts.raise(p->getId(), p->getPseudonym(), r.severity, rulesOf(r), time(nullptr));
            app.save();
            reply(res, {{"ok", true}});
        });
        svr.Get("/api/entries", [this](const httplib::Request& req, httplib::Response& res) {
            lock_guard<mutex> lk(mtx);
            Patient* p = patient(req, res); if (!p) return;
            json arr = json::array();
            for (auto& e : app.entries) if (canRead(*p, e)) arr.push_back(entryJson(e));
            reply(res, {{"entries", arr}});
        });
        svr.Post(R"(/api/entry/(\d+)/toggle)", [this](const httplib::Request& req, httplib::Response& res) {
            lock_guard<mutex> lk(mtx);
            Patient* p = patient(req, res); if (!p) return;
            JournalEntry* e = findEntry(stoi(req.matches[1]));
            if (!e || e->ownerId != p->getId()) return fail(res, 404, "Entry not found.");
            e->sharing = e->sharing == Sharing::Shared ? Sharing::Private : Sharing::Shared;
            app.save();
            reply(res, {{"shared", e->sharing == Sharing::Shared}});
        });

        // ---------- counselor ----------
        svr.Get("/api/alerts", [this](const httplib::Request& req, httplib::Response& res) {
            lock_guard<mutex> lk(mtx);
            if (!counselor(req, res)) return;
            json q = json::array(), o = json::array();
            for (auto& a : app.alerts.queuedInOrder()) q.push_back(alertJson(a));
            for (auto& a : app.alerts.others()) o.push_back(alertJson(a));
            reply(res, {{"queued", q}, {"others", o}});
        });
        svr.Post("/api/alerts/next", [this](const httplib::Request& req, httplib::Response& res) {
            lock_guard<mutex> lk(mtx);
            if (!counselor(req, res)) return;
            Alert a;
            if (!app.alerts.popNext(a)) return fail(res, 404, "No queued alerts.");
            app.save();
            reply(res, {{"alert", alertJson(a)}});
        });
        svr.Post(R"(/api/alerts/(\d+)/advance)", [this](const httplib::Request& req, httplib::Response& res) {
            lock_guard<mutex> lk(mtx);
            if (!counselor(req, res)) return;
            string msg;
            if (!app.alerts.advance(stoi(req.matches[1]), msg)) return fail(res, 400, msg);
            app.save();
            reply(res, {{"message", msg}});
        });
        svr.Get("/api/shared", [this](const httplib::Request& req, httplib::Response& res) {
            lock_guard<mutex> lk(mtx);
            User* c = counselor(req, res); if (!c) return;
            json arr = json::array();
            for (auto& e : app.entries) {
                if (!canRead(*c, e)) continue;       // only entries the patient chose to share
                json j = entryJson(e);
                auto it = app.users.find(e.ownerId);
                j["pseudonym"] = it != app.users.end() ? it->second->getPseudonym() : "?";
                arr.push_back(j);
            }
            reply(res, {{"entries", arr}});
        });
    }
};

int runWeb(int port) {
    App app; app.init();
    WebApi api(app);
    httplib::Server svr;
    api.mount(svr);
    svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
        string what = "unknown";
        try { if (ep) rethrow_exception(ep); } catch (const exception& e) { what = e.what(); } catch (...) {}
        res.status = 500;
        res.set_content(json{{"error", "Server error: " + what}}.dump(), "application/json");
    });
    svr.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        if (req.path.rfind("/api/", 0) == 0 && res.body.empty())
            res.set_content(json{{"error", "Request failed (" + to_string(res.status) + ") for " + req.path}}.dump(), "application/json");
    });
    if (!svr.set_mount_point("/", "./web"))
        cout << "WARNING: ./web folder not found. Run this from the project folder.\n";
    cout << "MindSpace website running.  Open:  http://localhost:" << port << "\n"
         << "Demo counselor login: counselor1 / counselor123      (Ctrl+C to stop)\n";
    if (!svr.listen("127.0.0.1", port)) { cout << "Could not start the server (is port " << port << " already in use?).\n"; return 1; }
    return 0;
}
#endif   // WITH_WEB

// ---------------------------------------------------------------------
//  Built-in tests: run with  .\mindspace test
// ---------------------------------------------------------------------
static int passed = 0, failed = 0;
#define CHECK(cond, name) do { if (cond) { ++passed; cout << "  PASS  " << name << "\n"; } \
                               else { ++failed; cout << "  FAIL  " << name << "\n"; } } while (0)

int runTests() {
    cout << "Running MindSpace tests...\n";
    time_t now = time(nullptr);

    { // heap order: severity first, older first on ties
        AlertManager am;
        am.raise(1, "a", Severity::Low, "R11", now);
        am.raise(2, "b", Severity::High, "R03", now);
        am.raise(3, "c", Severity::Medium, "R09", now);
        am.raise(4, "d", Severity::High, "R01", now);
        Alert x; vector<int> order;
        while (am.popNext(x)) order.push_back(x.id);
        CHECK((order == vector<int>{2, 4, 3, 1}), "heap pops High(older), High(newer), Medium, Low");
    }
    { // alert lifecycle
        AlertManager am; int id = am.raise(1, "a", Severity::High, "R03", now);
        string m; Alert x;
        CHECK(!am.advance(id, m), "cannot advance an alert that is still queued");
        am.popNext(x);
        bool ok = am.advance(id, m) && am.advance(id, m) && am.advance(id, m);
        CHECK(ok && !am.advance(id, m), "Sent -> Acknowledged -> Reviewed -> Closed, then stops");
    }
    { // detector
        CrisisDetector d;
        CHECK(d.scan("I want to kill myself").severity == Severity::High, "strong phrase -> HIGH");
        CHECK(d.scan("I feel hopeless and worthless").severity == Severity::Low, "mild phrases -> LOW");
        CHECK(d.scan("No one cares and I hate myself").severity == Severity::Medium, "moderate phrases -> MEDIUM");
        CHECK(d.scan("I am not hopeless today").severity == Severity::None, "negated phrase is not flagged");
        CHECK(d.scan("Had a great day, played cricket!").severity == Severity::None, "normal text -> NONE");
        CHECK(d.scan("I DON'T want to die").severity == Severity::None, "don't + phrase is suppressed");
    }
    { // sliding window
        MoodTrend t;
        t.add({2, now - 10L * 24 * 3600, ""});
        t.add({6, now - 1L * 24 * 3600, ""});
        t.add({8, now, ""});
        CHECK(t.count() == 2 && t.average() == 7.0, "7-day window drops the 10-day-old record");
    }
    { // consent rules
        CHECK(ConsentPolicy::decide(Severity::None) == Notify::None, "consent: none -> nothing");
        CHECK(ConsentPolicy::decide(Severity::Low) == Notify::AskPatient, "consent: low -> ask patient");
        CHECK(ConsentPolicy::decide(Severity::Medium) == Notify::AskPatient, "consent: medium -> ask patient");
        CHECK(ConsentPolicy::decide(Severity::High) == Notify::AlertCounselor, "consent: high -> minimal alert");
    }
    { // privacy and access control
        Patient a(1, "anu", "s", "h"), b(2, "ben", "s", "h"); Counselor c(3, "couns", "s", "h");
        JournalEntry priv{1, 1, "secret kill myself text", now, Sharing::Private};
        JournalEntry shared{2, 1, "shared text", now, Sharing::Shared};
        CHECK(canRead(a, priv), "owner can read own private entry");
        CHECK(!canRead(b, priv) && !canRead(b, shared), "another patient cannot read it");
        CHECK(!canRead(c, priv), "counselor cannot read a private entry");
        CHECK(canRead(c, shared), "counselor can read a shared entry");
        AlertManager am; am.raise(1, "anu", Severity::High, "R01", now);
        ostringstream os; os << am.everything()[0];
        CHECK(os.str().find("secret") == string::npos && os.str().find("kill myself") == string::npos,
              "alert output never contains entry text");
    }
    { // password hashing
        string salt = "abc"; Patient p(1, "x", salt, hashPw(salt, "pass1234"));
        CHECK(p.checkPassword("pass1234") && !p.checkPassword("wrong"), "salted password check");
    }
    cout << "\nPassed: " << passed << "   Failed: " << failed << "\n";
    return failed == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc > 1 && string(argv[1]) == "test") return runTests();
#ifdef WITH_WEB
    if (argc > 1 && string(argv[1]) == "web") return runWeb(argc > 2 ? atoi(argv[2]) : 8080);
#endif
    App app;
    app.run();
    return 0;
}