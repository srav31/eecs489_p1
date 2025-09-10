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

    for(int i = 0; i < 8; ++i) {
        // receive 1-byte probe from client
        ssize_t n = recv(client_fd, &probe_buf, 1, 0);
        if(n <= 0) { 
            close(client_fd); 
            close(server_fd); 
            return; 
        }
    
        auto send_time = clck::now();

        // std::this_thread::sleep_for(std::chrono::milliseconds(50)); //artificial delay
    
        // send 1-byte ACK
        if(send(client_fd, &ack, 1, 0) != 1) { 
            close(client_fd); 
            close(server_fd); 
            return; 
        }
    
        auto recv_ack_time = clck::now();
        double ms = std::chrono::duration<double, std::milli>(recv_ack_time - send_time).count();
        rtts_ms.push_back(ms);
    }  

    const size_t CHUNK_SIZE = 80 * 1000; // 80KB
    char data_buf[CHUNK_SIZE];
    long total_bytes = 0;
    bool started = false;
    clck::time_point start_time, end_time;

    while(true) {
        size_t bytes_received_in_chunk = 0;
        do {
            ssize_t n = recv(client_fd, data_buf + bytes_received_in_chunk,
                             CHUNK_SIZE - bytes_received_in_chunk, 0);
            if(n <= 0) {
                goto end_receiving;
            }
            bytes_received_in_chunk += n;
            total_bytes += n;
            if(!started) {
                started = true;
                start_time = clck::now();
            }
        } while(bytes_received_in_chunk < CHUNK_SIZE);
    
        // send ACK
        if(send(client_fd, &ack, 1, 0) != 1) {
            break;
        }

        // mark end_time AFTER ACK is sent
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
    clck::time_point end_time;

    // loop sending data
    while(true) {
        size_t bytes_sent_in_chunk = 0;
        do {
            ssize_t sent = send(sock, buf + bytes_sent_in_chunk, CHUNK_SIZE - bytes_sent_in_chunk, 0);
            if(sent <= 0) {
                end_time = clck::now(); // just assign, don't redeclare
                goto end_sending;
            }
            bytes_sent_in_chunk += sent;
            total_bytes += sent;
        } while(bytes_sent_in_chunk < CHUNK_SIZE);

        // check elapsed time
        auto now = clck::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();
        if(elapsed >= time_sec) {
            end_time = now;  // just assign
            break;
        }

        // wait for ACK
        if(recv(sock, &ack, 1, 0) <= 0) {
            end_time = clck::now(); // just assign
            break;
        }
    }

    end_sending:
    
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