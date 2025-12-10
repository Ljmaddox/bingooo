#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <fstream>
#include <thread>
#include <mutex>
#include <algorithm>
#include <random>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <regex>
#include <chrono>
#include <iomanip>

using namespace std;

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ifaddrs.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close

//helpers
struct Square {
    string text;
    bool marked;
};

struct Board {
    string id;
    vector<Square> squares;
    string last_accessed_date;
    bool bingo_achieved;
};

struct LeaderboardEntry {
    string username;
    string timestamp;
    string board_id;
    vector<string> winning_squares;
};

map<string, Board> user_boards;
vector<LeaderboardEntry> leaderboard;
vector<string> phrases;
vector<string> blocked_words;
mutex data_mutex;
set<SOCKET> sse_clients;
mutex sse_mutex;

string get_date() {
    auto now = chrono::system_clock::now();
    auto time_t_now = chrono::system_clock::to_time_t(now);
    stringstream stringstr;
    stringstr << put_time(localtime(&time_t_now), "%Y-%m-%d");
    return stringstr.str();
}

string get_time() {
    auto now = chrono::system_clock::now();
    auto time_t_now = chrono::system_clock::to_time_t(now);
    stringstream stringstr;
    stringstr << put_time(localtime(&time_t_now), "%H:%M:%S");
    return stringstr.str();
}

string generate_user_id() {
    static random_device rd;
    static mt19937 gen(rd());
    static uniform_int_distribution<> dis(0, 15);
    static const char* hex = "0123456789abcdef";
    
    string uuid = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
    for (char& c : uuid) {
        if (c == 'x')
            c = hex[dis(gen)];
        else if (c == 'y')
            c = hex[(dis(gen) & 0x3) | 0x8];
    }
    return uuid;
}

void load_phrases() {
    ifstream file("db/phrases.txt");
    if (!file.is_open()) {
        phrases = {"Item 1", "Item 2", "Item 3", "Item 4", "Item 5", "Item 6", "Item 7", "Item 8", "Item 9", "Item 10", "Item 11", "Item 12", "Item 13", "Item 14", "Item 15", "Item 16", "Item 17", "Item 18", "Item 19", "Item 20", "Item 21", "Item 22", "Item 23", "Item 24", "Item 25"};
        return;
    }
    string line;
    while (getline(file, line)) {
        if (!line.empty())
            phrases.push_back(line);
    }
}

void load_blocked_words() {
    ifstream file("db/blocked.txt");
    if (!file.is_open())
        return;
    string line;
    while (getline(file, line)) {
        if (!line.empty())
            blocked_words.push_back(line);
    }
}

string censor_text(const string& text) {
    string result = text;
    for (const auto& word : blocked_words) {
        regex pattern(word, regex::icase);
        result = regex_replace(result, pattern, string(word.length(), '*'));
    }
    return result;
}

Board generate_board() {
    Board board;
    board.id = generate_user_id();
    board.bingo_achieved = false;
    board.last_accessed_date = get_date();
    
    auto pool = phrases;
    random_device rd;
    mt19937 g(rd());
    shuffle(pool.begin(), pool.end(), g);
    
    for (int i = 0; i < 25; i++) {
        Square sq;
        sq.text = pool[i % pool.size()];
        sq.marked = false;
        board.squares.push_back(sq);
    }
    
    return board;
}

bool check_bingo(const vector<Square>& squares, vector<int>& winning_indices) {
    vector<bool> marked;
    for (const auto& sq : squares)
        marked.push_back(sq.marked);
    
    for (int i = 0; i < 5; i++) {
        bool win = true;
        for (int j = 0; j < 5; j++) {
            if (!marked[i * 5 + j]){
                win = false;
                break;
            }
        }
        if (win) {
            for (int j = 0; j < 5; j++)
                winning_indices.push_back(i * 5 + j);
            return true;
        }
    }
    
    for (int i = 0; i < 5; i++) {
        bool win = true;
        for (int j = 0; j < 5; j++) {
            if (!marked[j * 5 + i]) {
                win = false;
                break;
            }
        }
        if (win) {
            for (int j = 0; j < 5; j++)
                winning_indices.push_back(j * 5 + i);
            return true;
        }
    }
    
    bool diag1 = true, diag2 = true;
    for (int i = 0; i < 5; i++) {
        if (!marked[i * 6])
            diag1 = false;
        if (!marked[(i + 1) * 4])
            diag2 = false;
    }
    if (diag1) {
        for (int i = 0; i < 5; i++)
            winning_indices.push_back(i * 6);
        return true;
    }
    if (diag2) {
        for (int i = 0; i < 5; i++)
            winning_indices.push_back((i + 1) * 4);
        return true;
    }
    
    return false;
}

string json_escape(const string& str) {
    string result;
    for (char c : str) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f"; 
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
        }
    }
    return result;
}

string board_to_json(const Board& board) {
    stringstream stringstr;
    stringstr << "{\"id\":\"" << board.id << "\",\"squares\":[";
    for (size_t i = 0; i < board.squares.size(); i++) {
        if (i > 0)
            stringstr << ",";
        stringstr << "{\"text\":\"" << json_escape(board.squares[i].text) << "\"," << "\"marked\":" << (board.squares[i].marked ? "true" : "false") << "}";
    }
    stringstr << "]}";
    return stringstr.str();
}

string leaderboard_to_json() {
    stringstream stringstr;
    stringstr << "[";
    for (size_t i = 0; i < leaderboard.size(); i++) {
        if (i > 0)
            stringstr << ",";
        const auto& entry = leaderboard[i];
        stringstr << "{\"username\":\"" << json_escape(entry.username) << "\","
           << "\"timestamp\":\"" << entry.timestamp << "\","
           << "\"board_id\":\"" << entry.board_id << "\","
           << "\"winning_squares\":[";
        for (size_t j = 0; j < entry.winning_squares.size(); j++) {
            if (j > 0)
                stringstr << ",";
            stringstr << "\"" << json_escape(entry.winning_squares[j]) << "\"";
        }
        stringstr << "]}";
    }
    stringstr << "]";
    return stringstr.str();
}

void notify_sse_clients(const string& message) {
    lock_guard<mutex> lock(sse_mutex);
    string data = "data: " + message + "\n\n";
    
    auto it = sse_clients.begin();
    while (it != sse_clients.end()) {
        if (send(*it, data.c_str(), data.length(), 0) == SOCKET_ERROR) {
            closesocket(*it);
            it = sse_clients.erase(it);
        } else {
            ++it;
        }
    }
}

string read_file(const string& path) {
    ifstream file(path, ios::binary);
    if (!file)
        return "";
    return string((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
}

void send_http_response(SOCKET client, int status, const string& content_type, const string& body) {
    string status_text = (status == 200) ? "OK" : (status == 404) ? "Not Found" : "Error";
    stringstream response;
    response << "HTTP/1.1 " << status << " " << status_text << "\r\n"
            << "Content-Type: " << content_type << "\r\n"
            << "Content-Length: " << body.length() << "\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "Connection: close\r\n\r\n"
            << body;
    
    string resp_str = response.str();
    send(client, resp_str.c_str(), resp_str.length(), 0);
}

void send_sse_response(SOCKET client) {
    string headers = "HTTP/1.1 200 OK\r\n" "Content-Type: text/event-stream\r\n" "Cache-Control: no-cache\r\n" "Connection: keep-alive\r\n" "Access-Control-Allow-Origin: *\r\n\r\n";
    send(client, headers.c_str(), headers.length(), 0);
    
    string initial = "data: {\"type\":\"connected\"}\n\n";
    send(client, initial.c_str(), initial.length(), 0);
    
    lock_guard<mutex> lock(sse_mutex);
    sse_clients.insert(client);
}

// Fixed JSON parsing
int extract_index_from_body(const string& body) {
    size_t index_pos = body.find("\"index\":");
    if (index_pos == string::npos) return -1;
    
    size_t start = index_pos + 8;
    size_t end = start;
    while (end < body.length() && isdigit(body[end])) {
        end++;
    }
    
    if (end == start) return -1;
    
    try {
        return stoi(body.substr(start, end - start));
    } catch (...) {
        return -1;
    }
}

void handle_client(SOCKET client_socket, const string& client_ip) {
    char buffer[4096] = {0};
    int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        closesocket(client_socket);
        return;
    }
    
    string request(buffer, bytes_read);
    istringstream request_stream(request);
    string method, path, version;
    request_stream >> method >> path >> version;
    
    string query_string;
    size_t query_pos = path.find('?');
    if (query_pos != string::npos) {
        query_string = path.substr(query_pos + 1);
        path = path.substr(0, query_pos);
    }
    
    string body;
    if (method == "POST") {
        size_t body_pos = request.find("\r\n\r\n");
        if (body_pos != string::npos) {
            body = request.substr(body_pos + 4);
        }
    }
    
    if (path == "/" || path == "/index.html") {
        string content = read_file("frontend/index.html");
        send_http_response(client_socket, 200, "text/html", content);
    }
    else if (path.find("/assets/") == 0) {
        string file_path = "frontend" + path;
        string content = read_file(file_path);
        string content_type = "text/plain";
        if (path.find(".css") != string::npos)
            content_type = "text/css";
        else if (path.find(".js") != string::npos)
            content_type = "application/javascript";
        else if (path.find(".png") != string::npos)
            content_type = "image/png";
        send_http_response(client_socket, content.empty() ? 404 : 200, content_type, content);
    }
    else if (path.find("/api/board/") == 0) {
        lock_guard<mutex> lock(data_mutex);
        
        bool new_board = query_string.find("NewBoard=true") != string::npos;
        bool reset = query_string.find("reset=true") != string::npos;
        
        if (new_board) {
            user_boards[client_ip] = generate_board();
        } else if (reset) {
            if (user_boards.count(client_ip)) {
                for (auto& sq : user_boards[client_ip].squares) {
                    sq.marked = false;
                }
            } else {
                user_boards[client_ip] = generate_board();
            }
        } else if (!user_boards.count(client_ip)) {
            user_boards[client_ip] = generate_board();
        }
        
        user_boards[client_ip].last_accessed_date = get_date();
        string json = "{\"board\":" + board_to_json(user_boards[client_ip]) + "}";
        send_http_response(client_socket, 200, "application/json", json);
    }
    else if (path == "/api/mark_square" && method == "POST") {
        lock_guard<mutex> lock(data_mutex);
        
        int index = extract_index_from_body(body);
        if (index < 0 || index >= 25 || !user_boards.count(client_ip)) {
            send_http_response(client_socket, 400, "application/json", "{\"error\":\"invalid request\"}");
            closesocket(client_socket);
            return;
        }
        
        auto& board = user_boards[client_ip];
        board.squares[index].marked = !board.squares[index].marked;
        
        vector<int> winning_indices;
        bool is_bingo = check_bingo(board.squares, winning_indices);
        
        stringstream response;
        response << "{\"board\":" << board_to_json(board) << "," << "\"bingo\":" << (is_bingo ? "true" : "false");
        
        if (is_bingo) {
            if (board.bingo_achieved) {
                response << ",\"info\":\"bingod\"";
            } else {
                response << ",\"info\":[\"bingo\",0]," << "\"winning_squares\":{\"indices\":[";
                for (size_t i = 0; i < winning_indices.size(); i++) {
                    if (i > 0)
                        response << ",";
                    response << winning_indices[i];
                }
                response << "],\"list\":[";
                for (size_t i = 0; i < winning_indices.size(); i++) {
                    if (i > 0)
                        response << ",";
                    response << "\"" << json_escape(board.squares[winning_indices[i]].text) << "\"";
                }
                response << "]}";
            }
        }
        
        response << "}";
        send_http_response(client_socket, 200, "application/json", response.str());
    }
    else if (path == "/api/submit_bingo" && method == "POST") {
        lock_guard<mutex> lock(data_mutex);
        
        size_t user_pos = body.find("\"username\":\"");
        if (user_pos == string::npos) {
            send_http_response(client_socket, 400, "application/json", "{\"error\":\"missing username\"}");
            closesocket(client_socket);
            return;
        }
        
        size_t user_start = user_pos + 12;
        size_t user_end = body.find("\"", user_start);
        string username = body.substr(user_start, user_end - user_start);
        username = censor_text(username);
        
        if (user_boards.count(client_ip)) {
            user_boards[client_ip].bingo_achieved = true;
        }
        
        LeaderboardEntry entry;
        entry.username = username;
        entry.timestamp = get_time();
        entry.board_id = user_boards.count(client_ip) ? user_boards[client_ip].id : "";
        
        size_t squares_pos = body.find("\"winning_squares\":");
        if (squares_pos != string::npos) {
            size_t list_pos = body.find("\"list\":[", squares_pos);
            if (list_pos != string::npos) {
                size_t list_start = list_pos + 8;
                size_t list_end = body.find("]", list_start);
                string list_str = body.substr(list_start, list_end - list_start);
                
                size_t pos = 0;
                while (pos < list_str.length()) {
                    size_t quote1 = list_str.find("\"", pos);
                    if (quote1 == string::npos)
                        break;
                    size_t quote2 = list_str.find("\"", quote1 + 1);
                    if (quote2 == string::npos)
                        break;
                    
                    string square = list_str.substr(quote1 + 1, quote2 - quote1 - 1);
                    size_t escape_pos = 0;
                    while ((escape_pos = square.find("\\\"", escape_pos)) != string::npos) {
                        square.replace(escape_pos, 2, "\"");
                        escape_pos += 1;
                    }
                    entry.winning_squares.push_back(square);
                    pos = quote2 + 1;
                }
            }
        }
        
        leaderboard.push_back(entry);
        
        notify_sse_clients("{\"type\":\"leaderboard_update\",\"source\":\"submit_bingo\"}");
        
        send_http_response(client_socket, 201, "application/json", "{\"success\":true}");
    }
    else if (path == "/api/leaderboard") {
        lock_guard<mutex> lock(data_mutex);
        send_http_response(client_socket, 200, "application/json", leaderboard_to_json());
    }
    else if (path == "/api/leaderboard/stream") {
        send_sse_response(client_socket);
        return;
    }
    else {
        send_http_response(client_socket, 404, "text/plain", "Not Found");
    }
    
    closesocket(client_socket);
}

int main() {
    load_phrases();
    load_blocked_words();
    
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        cerr << "Socket creation failed" << endl;
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "0.0.0.0", &server_addr.sin_addr);

    int port = 5000;
    const char* port_env = getenv("PORT");
    if (port_env) {
        try {
            int p = stoi(port_env);
            if (p > 0 && p < 65536) port = p;
        } catch(...) { /* keep default */ }
    }
    server_addr.sin_port = htons(port);

    
    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Bind failed" << endl;
        closesocket(server_socket);
        return 1;
    }
    
    if (listen(server_socket, 10) == SOCKET_ERROR) {
        cerr << "Listen failed" << endl;
        closesocket(server_socket);
        return 1;
    }
    
    cout << "Server is running on port 5000" << endl;
    
    while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_len);
        
        if (client_socket == INVALID_SOCKET)
            continue;
        
        string client_ip = inet_ntoa(client_addr.sin_addr);
        
        thread(handle_client, client_socket, client_ip).detach();
    }
    
    closesocket(server_socket);
    return 0;
}