#include "baldr/graphreader.h"
#include "config.h"
#include "loki/worker.h"
#include "midgard/logging.h"

#include <prime_server/http_protocol.hpp>
#include <prime_server/prime_server.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: valhalla_locate_service CONFIG_JSON [CONCURRENCY]\n";
    return EXIT_FAILURE;
  }

  try {
    auto config = valhalla::config(argv[1]);
    const auto concurrency = argc == 3 ? std::stoul(argv[2]) : 1;
    if (concurrency == 0) {
      throw std::runtime_error("Concurrency must be greater than zero");
    }
    valhalla::midgard::logging::ConfigureFromPtree(config);

    {
      auto reader = std::make_shared<valhalla::baldr::GraphReader>(config.get_child("mjolnir"));
      if (reader->tile_extract().empty()) {
        throw std::runtime_error("A readable tile_extract containing .gph tiles is required");
      }
      valhalla::loki::loki_worker_t worker(config, reader);
    }

    prime_server::quiesce(config.get<unsigned int>("httpd.service.drain_seconds", 28),
                          config.get<unsigned int>("httpd.service.shutdown_seconds", 1));
    const auto listen = config.get<std::string>("httpd.service.listen");
    const auto loopback = config.get<std::string>("httpd.service.loopback");
    const auto interrupt = config.get<std::string>("httpd.service.interrupt");
    const auto proxy = config.get<std::string>("loki.service.proxy");
    const auto timeout = config.get<uint32_t>("httpd.service.timeout_seconds");

    zmq::context_t context;
    std::thread proxy_thread(
        [&]() { prime_server::proxy_t(context, proxy + "_in", proxy + "_out").forward(); });
    proxy_thread.detach();
    for (size_t i = 0; i < concurrency; ++i) {
      std::thread(valhalla::loki::run_service, config).detach();
    }
    prime_server::http_server_t(context, listen, proxy + "_in", loopback, interrupt, true,
                                prime_server::DEFAULT_MAX_REQUEST_SIZE, timeout)
        .serve();
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
