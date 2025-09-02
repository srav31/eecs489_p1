#include <iostream>
#include <cxxopts.hpp>
#include <spdlog/spdlog.h>

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

        bool isServer = result.count("server") > 0;
        if (!result.count("port")) {
            spdlog::error("Error: missing port number");
            return 1;
        }

        int port = result["port"].as<int>();
        if (port < 1024 || port > 65535) {
            spdlog::error("Error: port number must be in the range of [1024, 65535]");
            return 1;
        }

        if (isServer) {
            spdlog::info("iPerfer server started on port {}", port);
            // TODO: implement server logic
        } else {
            spdlog::info("iPerfer client started on port {}", port);
            // TODO: implement client logic
        }

    } catch (const cxxopts::OptionException& e) {
        spdlog::error("Error parsing options: {}", e.what());
        return 1;
    }

    return 0;
}
