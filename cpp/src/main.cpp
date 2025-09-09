#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cxxopts.hpp>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <cmath>

// test RTT time by putting delay on part 2 topology links

using clck = std::chrono::high_resolution_clock;

static inline int avg_last4_ms(const std::vector<double>& v) {
    if(v.size() < 4) {
        return 0;
    }

    double sum = 0.0;
    for(size_t i = v.size() - 4; i < v.size(); ++i) {
        sum += v[i];
    }

    return static_cast<int>(std::round(sum / 4.0));
}


void run_server(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0) {
        exit(1);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if(bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(server_fd);
        exit(1);
    }

    if(listen(server_fd, 1) < 0) {
        close(server_fd);
        exit(1);
    }

    spdlog::info("iPerfer server started");

    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
    if(client_fd < 0) {
        close(server_fd);
        exit(1);
    }

    spdlog::info("Client connected");

    char probe_buf;
    char ack = 'A';
    std::vector<double> rtts_ms;
    bool rec_ack = false;
    clck::time_point sent_time;

    for(int i = 0; i < 8; ++i) {
        ssize_t n = recv(client_fd, &probe_buf, 1, 0);
        if(n <= 0) { 
            // spdlog::info("Server: failed to recv probe {}", i); // DEBUG
            close(client_fd); 
            close(server_fd); 
            return; 
        }

        if(rec_ack) {
            auto recv_time = clck::now();
            double ms = std::chrono::duration<double, std::milli>(recv_time - sent_time).count();
            rtts_ms.push_back(ms);
            // spdlog::info("Server: RTT measured={} ms", ms); // DEBUG
        }

        if(send(client_fd, &ack, 1, 0) != 1) { 
            // spdlog::info("Server: failed to send ACK {}", i); // DEBUG
            close(client_fd); 
            close(server_fd); 
            return;
        }

        // spdlog::info("Server: sent ACK {}", i); // DEBUG

        sent_time = clck::now();
        rec_ack = true;
    }

    const size_t BUF_SIZE = 8192;
    char data_buf[BUF_SIZE];
    long total_bytes = 0;
    bool started = false;
    clck::time_point start_time, end_time;
    
    while(true) {
        size_t bytes_received_in_chunk = 0;
    
        // read a full buffer, handling partial reads
        do {
            ssize_t n = recv(client_fd, data_buf + bytes_received_in_chunk, BUF_SIZE - bytes_received_in_chunk, 0);
            if(n <= 0) {
                goto end_receiving; // exit both loops on error or disconnect
            }
    
            if(!started) {
                started = true;
                start_time = clck::now();
            }
    
            bytes_received_in_chunk += n;
            total_bytes += n;
    
        } while(bytes_received_in_chunk < BUF_SIZE);
    
        // send 1-byte ACK
        if(send(client_fd, &ack, 1, 0) != 1) {
            break;
        }
    
        end_time = clck::now();
    }
    
    end_receiving:
    
    if(!started) {
        end_time = clck::now();
        start_time = end_time;
    }
    

    double duration_sec = std::chrono::duration<double>(end_time - start_time).count();
    int received_kb = static_cast<int>(total_bytes / 1000); // KB
    double rate_mbps = duration_sec > 0.0 ? (total_bytes * 8.0) / (duration_sec * 1'000'000.0) : 0.0;
    int avg_rtt = avg_last4_ms(rtts_ms);

    spdlog::info("Received={} KB, Rate={:.3f} Mbps, RTT={} ms", received_kb, rate_mbps, avg_rtt);

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
        exit(1);
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if(inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        close(sock);
        exit(1);
    }

    if(connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sock);
        exit(1);
    }

    spdlog::info("Connected to server at {}:{}", host, port);

    char one = 'M';
    char ack;
    std::vector<double> rtts_ms;

    for(int i = 0; i < 8; ++i) {
        auto t0 = clck::now();

        if(send(sock, &one, 1, 0) != 1) { 
            close(sock); 
            exit(1); 
        }

        if(recv(sock, &ack, 1, 0) <= 0) { 
            close(sock); 
            exit(1); 
        }

        auto t1 = clck::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        rtts_ms.push_back(ms);
    }

    const size_t CHUNK_SIZE = 80 * 1000; // 80KB
    char buf[CHUNK_SIZE];
    std::memset(buf, 0, CHUNK_SIZE);

    long total_bytes = 0;
    auto start_time = clck::now();

    while(true) {
        size_t bytes_sent_in_chunk = 0;

        // send the full CHUNK_SIZE, handling partial sends
        do {
            ssize_t sent = send(sock, buf + bytes_sent_in_chunk, CHUNK_SIZE - bytes_sent_in_chunk, 0);
            if(sent <= 0) {
                goto end_sending; // exit both loops on error
            }
            bytes_sent_in_chunk += sent;
            total_bytes += sent;
        } while(bytes_sent_in_chunk < CHUNK_SIZE);

        // wait for 1-byte ACK
        if(recv(sock, &ack, 1, 0) <= 0) {
            break;
        }

        auto now = clck::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();
        if(elapsed >= time_sec) { // stop when elapsed >= requested duration
            break;
        }
    }

    end_sending:
    

    auto end_time = clck::now();
    double elapsed_sec = std::chrono::duration<double>(end_time - start_time).count();
    int sent_kb = static_cast<int>(total_bytes / 1000); // KB (1000-based to match spec’s examples)
    double rate_mbps = elapsed_sec > 0.0 ? (total_bytes * 8.0) / (elapsed_sec * 1'000'000.0) : 0.0;
    int avg_rtt = avg_last4_ms(rtts_ms);

    spdlog::info("Sent={} KB, Rate={:.3f} Mbps, RTT={} ms", sent_kb, rate_mbps, avg_rtt);
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
        if(result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        if(!result.unmatched().empty()) {
            spdlog::error("Error: extra arguments provided");
            return 1;
        }

        bool is_server = result.count("server") > 0;
        bool is_client = result.count("client") > 0;

        if(is_server == is_client) {
            spdlog::error("Error: must specify either -s (server) or -c (client)");
            return 1;
        }

        if(!result.count("port")) {
            spdlog::error("Error: missing port number");
            return 1;
        }

        int port = result["port"].as<int>();
        if(port < 1024 || port > 65535) {
            spdlog::error("Error: port number must be in the range of [1024, 65535]");
            return 1;
        }

        if(is_server) {
            run_server(port);
        } else {
            if(!result.count("host") || !result.count("time")) {
                spdlog::error("Error: missing required client arguments (-h <host>, -t <time>)");
                return 1;
            }
            std::string host = result["host"].as<std::string>();
            double time = result["time"].as<double>();
            if(time <= 0) {
                spdlog::error("Error: time argument must be greater than 0");
                return 1;
            }
            spdlog::info("iPerfer client started, host={}, port={}, time={}s", host, port, time);
            run_client(host, port, time);
        }
    } catch (const std::exception&) {
        return 1;
    }
    return 0;
}