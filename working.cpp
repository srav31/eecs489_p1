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

int main(int argc, char* argv[]) {
    try {
        cxxopts::Options options("iPerfer", "iPerfer network measurement tool");
        options.add_options()
            ("s,server", "Run as server")
            ("p,port", "Port number", cxxopts::value<int>())
            ("h,help", "Print usage");

        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        bool is_server = result.count("server") > 0;
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
        } else {
            spdlog::info("iPerfer client started on port {}", port);
            // TODO: implement client logic
        }

    } catch (const std::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    }
    

    return 0;
}