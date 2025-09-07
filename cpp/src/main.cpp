#include <iostream>
#include <cxxopts.hpp>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>

void run_server(int port) {

    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0) {
        spdlog::error("Error: cannot create socket");
        exit(1);
    }

    // Bind socket to addr
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if(bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        spdlog::error("Error: cannot bind socket");
        close(server_fd);
        exit(1);
    }

    // Listen for connection
    if(listen(server_fd, 1) < 0) {
        spdlog::error("Error: cannot listen on socket");
        close(server_fd);
        exit(1);
    }

    spdlog::info("iPerfer server has started");

    // Accept client
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
    if(client_fd < 0) {
        spdlog::error("Error: cannot accept client");
        exit(1);
    }

    spdlog::info("Client is connected");

    // 8 1-byte packets with RTT measurement
    char buf[8192];
    char ack = 'A';
    std::vector<double> rtts;
    using clock = std::chrono::high_resolution_clock;

    for(int i = 0; i < 8; i++) {
        ssize_t n = recv(client_fd, buf, 1, 0);
        if(n <= 0){
            break;
        }

        auto start = clock::now();
        send(client_fd, &ack, 1, 0);
        
        if(i < 7) {
            n = recv(client_fd, buf, 1, 0);
            auto end = clock::now();
            std::chrono::duration<double, std::milli> rtt = end - start;
            rtts.push_back(rtt.count());
        }
    }

    // 80KB packets
    long total_bytes = 0;
    auto start_time = clock::now();
    while (true) {
        ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            break;
        }

        total_bytes += n;
        send(client_fd, &ack, 1, 0);
    }

    auto end_time = clock::now();

    double duration_sec = std::chrono::duration<double>(end_time - start_time).count();
    int received_kb = total_bytes / 1000;
    double rate_mbps = (total_bytes * 8.0) / (duration_sec * 1000000.0);

    int avg_rtt = 0;
    if (!rtts.empty()) {
        double sum = 0;
        for (double r : rtts) sum += r;
        avg_rtt = static_cast<int>(sum / rtts.size());
    }

    spdlog::info("Received={} KB, Rate={:.3f} Mbps, RTT={}ms", received_kb, rate_mbps, avg_rtt);
    close(client_fd);
    close(server_fd);

}

void run_client(const std::string& host, int port, double time_sec) {

    if(port < 1024 || port > 65535) {
        spdlog::error("Error: port number must be in the range of [1024, 65535]");
        return;
    }

    if(time_sec <= 0) {
        spdlog::error("Error: time argument must be greater than 0");
        return;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
        spdlog::error("Error: cannot create socket");
        exit(1);
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if(inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        spdlog::error("Error: invalid address");
        close(sock);
        exit(1);
    }

    if(connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        spdlog::error("Error: cannot connect to server");
        close(sock);
        exit(1);
    }

    spdlog::info("Connected to server at {}:{}", host, port);

    char one = '1';
    char ack;
    std::vector<double> rtts;
    using clock = std::chrono::high_resolution_clock;

    // 8 1-byte packets
    for(int i=0; i<8; i++) {
        if(send(sock, &one, 1, 0) != 1) {
            spdlog::error("Error: failed to send 1-byte packet");
            close(sock);
            exit(1);
        }

        auto start = clock::now();
        if(recv(sock, &ack, 1, 0) <= 0) {
            spdlog::error("Error: failed to receive ACK");
            close(sock);
            exit(1);
        }

        if(i < 7) {
            if(recv(sock, &one, 1, 0) <= 0) {
                spdlog::error("Error: failed to receive next RTT packet");
                close(sock);
                exit(1);
            }
            auto end = clock::now();
            std::chrono::duration<double, std::milli> rtt = end - start;
            rtts.push_back(rtt.count());
        }
    }

    const size_t CHUNK_SIZE = 81920; // 80KB
    char buf[CHUNK_SIZE];
    memset(buf, '\0', CHUNK_SIZE);

    long total_bytes = 0;
    auto start_time = clock::now();

    while(true) {
        auto now = clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();

        if(elapsed >= time_sec) {
            break;
        }

        ssize_t sent = send(sock, buf, CHUNK_SIZE, 0);

        if(sent <= 0) {
            break;
        }

        total_bytes += sent;

        if(recv(sock, &ack, 1, 0) <= 0) {
            break;
        }
    }

    auto end_time = clock::now();
    double elapsed_sec = std::chrono::duration<double>(end_time - start_time).count();
    int sent_kb = total_bytes / 1000;
    double rate_mbps = (total_bytes * 8.0) / (elapsed_sec * 1000000.0);

    int avg_rtt = 0;

    if(!rtts.empty()) {
        double sum = 0;
        for(double r: rtts) sum += r;
        avg_rtt = static_cast<int>(sum / rtts.size());
    }

    spdlog::info("Sent={} KB, Rate={:.3f} Mbps, RTT={}ms", sent_kb, rate_mbps, avg_rtt);
    close(sock);
}


int main(int argc, char* argv[]) {
    try {
        cxxopts::Options options("iPerfer", "iPerfer network measurement tool");
        options.add_options()
            ("s,server", "Run as server")
            ("c,client", "Run as client")
            ("h,host", "Server hostname", cxxopts::value<std::string>())
            ("p,port", "Port number", cxxopts::value<int>())
            ("t,time", "Duration in seconds", cxxopts::value<double>())
            ("help", "Print usage");

        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        bool is_server = result.count("server") > 0;
        bool is_client = result.count("client") > 0;

        if (!result.count("port")) {
            spdlog::error("Error: missing port number");
            return 1;
        }

        int port = result["port"].as<int>();
        if (port < 1024 || port > 65535) {
            spdlog::error("Error: port number must be in the range of [1024, 65535]");
            return 1;
        }

        if (is_server) {
            spdlog::info("iPerfer server started on port {}", port);
            run_server(port);
        } else if(is_client) {
            if(!result.count("host") || !result.count("time")) {
                spdlog::error("Error: missing required client arguments (-h <host>, -t <time>)");
                return 1;
            }

            std::string host = result["host"].as<std::string>();
            int time = result["time"].as<int>();

            spdlog::info("iPerfer client started, host={}, port={}, time={}s", host, port, time);
            run_client(host, port, time);
        } else {
            spdlog::error("Error: must specify either -s (server) or -c (client)");
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    }
    

    return 0;
}
