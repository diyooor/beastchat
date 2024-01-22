#include "../include/pgconnectionpool.hpp"
#include <boost/asio/query.hpp>
#include <iostream>
#include <mutex>
#include <random>
#include <spdlog/common.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
std::string PgConnectionPool::generateSession() {
  const std::string charset =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::random_device rd;
  std::mt19937 generator(rd());
  std::uniform_int_distribution<int> distribution(0, charset.length() - 1);

  std::string token;
  for (int i = 0; i < 16; ++i) {
    token += charset[distribution(generator)];
  }
  return token;
}
PgConnectionPool::PgConnectionPool(const std::string &conn_str,
                                   size_t pool_size, const dbSchema_ &schema)
    : conn_str_(conn_str), pool_size_(pool_size), schema_(schema) {
  auto console_logger = spdlog::stdout_color_mt("console_logger");
  console_logger->set_level(spdlog::level::trace);
  spdlog::set_default_logger(console_logger);
  spdlog::flush_every(std::chrono::seconds(3));
  for (size_t i = 0; i < pool_size_; ++i) {
    auto connection = std::make_shared<pqxx::connection>(conn_str_);
    connections_.emplace_back(connection);
  }
}
std::shared_ptr<pqxx::connection> PgConnectionPool::get_connection() {
  spdlog::get("console_logger")->info("get_connection");
  return connections_.front();
}
void PgConnectionPool::selectQuery() {}
pqxx::result PgConnectionPool::selectQuery(const std::string &table) {
  spdlog::get("console_logger")->info("selectQuery");
  try {
    auto connection = get_connection();
    pqxx::work transaction(*connection);
    pqxx::result result = transaction.exec_params("", "");
    transaction.commit();
    return result;
  } catch (const std::exception &e) {
    spdlog::get("console_logger")
        ->error("Exception acught in selectQuery: {}", e.what());
    throw e;
  }
}
bool PgConnectionPool::selectQuery(const std::string &table,
                                   const std::string &username) {
  spdlog::get("console_logger")->info("selectQuery");
  const std::string query = "SELECT * FROM " + table + " WHERE " +
                            schema_.db_schema.at(table)[0] + " = $1";
  try {
    auto connection = get_connection();
    pqxx::work transaction(*connection);
    pqxx::result result = transaction.exec_params(query, username);
    transaction.commit();
    return !result.empty();
  } catch (const std::exception &e) {
    spdlog::error("Exception caught in selectQuery: {}", e.what());
    throw e;
  }
}
bool PgConnectionPool::selectQuery(const std::string &table,
                                   const std::string &username,
                                   const std::string &password) {
  spdlog::get("console_logger")->info("selectQuery");
  const std::string query = "SELECT * FROM " + table + " WHERE " +
                            schema_.db_schema.at(table)[0] + " = $1 AND " +
                            schema_.db_schema.at(table)[1] + " = $2";
  try {
    auto connection = get_connection();
    pqxx::work transaction(*connection);
    pqxx::result result = transaction.exec_params(query, username, password);
    if (!result.empty()) {
      auto sessionIt = session_storage.find(username);
      if (sessionIt != session_storage.end()) {
        auto currentTime = std::chrono::steady_clock::now();
        if (currentTime < sessionIt->second.expiration_time) {
          spdlog::get("console_logger")
              ->info("Login failed. Active session exists for user: {}",
                     username);
          return false;
        }
      }
      std::string sessionToken = this->generateSession();
      session_storage[username] = {username, std::chrono::steady_clock::now() +
                                                 std::chrono::hours(1)};
      spdlog::get("console_logger")
          ->info("Session created for user: {}", username);
    }
    transaction.commit();
    return !result.empty();
  } catch (const std::exception &e) {
    spdlog::get("console_logger")
        ->error("Exception caught in selectQuery: {}", e.what());
    return false;
  }
}

bool PgConnectionPool::insertQuery(const std::string &table,
                                   const std::string &username,
                                   const std::string &password) {
  spdlog::get("console_logger")->info("insertQuery");
  if (selectQuery("user_table", username)) {
    spdlog::get("console_logger")
        ->error("Exception caught in insertQuery: Username already exists");
    return false;
  }
  const std::string query =
      "INSERT INTO " + table + "(" + schema_.db_schema.at(table)[0] + ", " +
      schema_.db_schema.at(table)[1] + ") VALUES ($1, $2)";
  try {
    auto connection = get_connection();
    pqxx::work transaction(*connection);
    transaction.exec_params(query, username, password);
    transaction.commit();
    return true;
  } catch (const std::exception &e) {
    spdlog::get("console_logger")
        ->error("Exception caught in insertQuery: {}", e.what());
    throw e;
  }
  return false;
}
