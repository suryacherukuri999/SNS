/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <ctime>
#include <regex>


#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include<glog/logging.h>
#include <iomanip>
#define log(severity, msg) LOG(severity) << msg; google::FlushLogFiles(google::severity); 

#include "sns.grpc.pb.h"


using google::protobuf::Timestamp;
using google::protobuf::Duration;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using csce662::Message;
using csce662::ListReply;
using csce662::Request;
using csce662::Reply;
using csce662::SNSService;


bool del_txt_files = false; // This is because when we restart the server, we need to delete previous .txt files. 

namespace fs = std::filesystem;

void delete_text_files() {  // Delete .txt files
    fs::path dir = "."; // Current directory

    try {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".txt") {
                fs::remove(entry.path());
               // std::cout << "Deleted: " << entry.path() << std::endl;
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "General error: " << e.what() << std::endl;
    }
}

struct Client {
  std::string username;
  bool connected = true;
  int following_file_size = 0;
  std::vector<Client*> client_followers;
  std::vector<Client*> client_following;
  ServerReaderWriter<Message, Message>* stream = 0;
  bool operator==(const Client& c1) const{
    return (username == c1.username);
  }
};

//Vector that stores every client that has been created
std::vector<Client*> client_db;

// Convert protobuf Timestamp to valid string format so we can store in .txt files
std::string timestamp_to_string(const google::protobuf::Timestamp& timestamp) {
    // Convert Timestamp to std::time_t to use with standard C++ time functions
    std::time_t raw_time = timestamp.seconds();
    char buffer[80]={0,};

    // Convert time_t to tm struct for conversion to local time
    std::tm* timeinfo = std::localtime(&raw_time);

    // Use strftime to format the time into a readable string
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

    std::stringstream ss;
    ss << buffer;

    return ss.str();
}

// We store (username,msg,timestamp) in the .txt files.
std::string format_file_output(const std::string& username, const std::string& message, const std::string& timestamp) 
{
    std::regex newline_regex("[\r\n]+");  // Regex to match one or more newline characters
    // Regex to filter out any new lines

    std::stringstream ss;
    ss << std::regex_replace(username, newline_regex, "") << ","
       << std::regex_replace(message, newline_regex, "") << ","
       << std::regex_replace(timestamp, newline_regex, "");

    return ss.str();
}

// parsing .txt data and pushing them to vector. Split based on (,) (username,msg,timestamp)
std::vector<std::string> parse_data(const std::string& data) { 
    std::vector<std::string> components; // vector to store (username,msg,timestamp)
    std::istringstream ss(data);
    std::string token;

    while (std::getline(ss, token, ',')) {
        components.push_back(token); // push to vector
    }

    return components;
}

// function to convert time string (2024-09-15 04:55:43) to google protobuf timestamp format
google::protobuf::Timestamp convert_to_timestamp(const std::string& time_str) {
    std::tm tm = {};
    std::istringstream ss(time_str);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S"); // Modify format as needed
    std::time_t time = mktime(&tm);

    google::protobuf::Timestamp timestamp;
    timestamp.set_seconds(time);
    timestamp.set_nanos(0);  // Set nanoseconds if your input includes finer resolution

    return timestamp;
}

class SNSServiceImpl final : public SNSService::Service {
  
  Status List(ServerContext* context, const Request* request, ListReply* list_reply) override {
    Client* client = nullptr;
    for (Client* c : client_db) { // traverse through client database to find client object
        if (c->username == request->username()) { // if found then store it in client
            client = c;
            break;
        }
    }

    if (client == nullptr) {
        return Status::CANCELLED; // Client not found
    }

    // Add all users to the response
    for (Client* c : client_db) {
        list_reply->add_all_users(c->username);
    }

    // Add all users the client is following
    for (Client* followers : client->client_followers) {
        list_reply->add_followers(followers->username);
    }

    return Status::OK;
  }

  Status Follow(ServerContext* context, const Request* request, Reply* reply) override {
    Client* follower = nullptr;
    for (Client* c : client_db) {// traverse through client database to find client object
        if (c->username == request->username()) {
            follower = c;
            break;
        }
    }

    if (follower == nullptr) { // if user doesn't exist then return
        return Status::CANCELLED; 
    }

    Client* to_follow = nullptr;
    for (Client* c : client_db) {
        if (c->username == request->arguments(0)) { // find the user we need to follow
            to_follow = c;
            break;
        }
    }

    if (to_follow == nullptr) { // if user we need to follow doesn't exist
        return Status::CANCELLED; 
    }
    if(to_follow==follower) // followee and follower are same
    {
        return Status(grpc::ALREADY_EXISTS,"followee and follower are same");
    }

    // check if user we need to follow already followed or not
    if(std::find(follower->client_following.begin(), follower->client_following.end(), to_follow) == follower->client_following.end()) {
        follower->client_following.push_back(to_follow); // add the client we need to follow in client_following
        to_follow->client_followers.push_back(follower); // add the follower to the followers list of the one we follow
        return Status::OK;
    }
    return Status(grpc::ALREADY_EXISTS,"Already followed");

  }

  Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override {

    Client* follower = nullptr;
    for (Client* c : client_db) {
        if (c->username == request->username()) { // traverse through client database to find client object
            follower = c;
            break;
        }
    }

    if (follower == nullptr) {
        return Status::CANCELLED; // Client not found
    }

    Client* to_unfollow = nullptr;
    for (Client* c : client_db) {
        if (c->username == request->arguments(0)) {
            to_unfollow = c;
            break;
        }
    }

    if (to_unfollow == nullptr) {
        return Status::CANCELLED; // to_follow user not found 
    }
    if(to_unfollow==follower) // followee and follower are same
    {
        return Status(grpc::ALREADY_EXISTS,"followee and follower are same");
    }
     if(std::find(follower->client_following.begin(), follower->client_following.end(), to_unfollow) != follower->client_following.end())
     {
        follower->client_following.erase(std::remove(follower->client_following.begin(), follower->client_following.end(), to_unfollow), follower->client_following.end());
        to_unfollow->client_followers.erase(std::remove(to_unfollow->client_followers.begin(), to_unfollow->client_followers.end(), follower), to_unfollow->client_followers.end());
        return Status::OK;
     }
    return Status(grpc::ALREADY_EXISTS,"Already Unfollowed");
    
  }

  // RPC Login
  Status Login(ServerContext* context, const Request* request, Reply* reply) override {
    for (Client* c : client_db) {
        if (c->username == request->username()) { // if user already logged in
            reply->set_msg("User "+c->username+" already logged in.");
            return grpc::Status(grpc::ALREADY_EXISTS,"User "+c->username+" already logged in");
        }
    }
    Client* user = new Client;
    user->username = request->username();
    client_db.push_back(user); // if new user logs in then add to the client database.
    reply->set_msg("Login Success for "+user->username);
    return Status::OK;
  }

  Status Timeline(ServerContext* context, 
		ServerReaderWriter<Message, Message>* stream) override {

    Message message;
    if(del_txt_files)
    {
      delete_text_files();  // delete previous session .txt files (happens only once when the first user enter the timeline)
      del_txt_files = false;
    }
   // Read the first message to get the username
    if (!stream->Read(&message)) {  // when user enter timeline for 1st time
        return Status::CANCELLED;  // No message received
    }
    
    Client* client = nullptr;
    for (Client* c : client_db) {
    if (c->username == message.username()) { // find client object 
        client = c;
        break;
      }
    }
    if (client == nullptr) {
        return Status::CANCELLED;  // Client not found
    }
    // Set the stream for the client so that they can receive messages
    client->stream = stream;
    
    // If it is the first, read the last 20 messages from the user's followers file
    std::deque<std::string> last20; // store latest 20 msgs in deque
    std::ifstream in(message.username() + "_following.txt"); // create user_name.following.txt
    std::string line;
    
    while (getline(in, line)) {
        last20.push_front(line);
        if (last20.size() > 20) {  // get the latest msgs from all users he is following
            last20.pop_back(); // Keep only the last 20 entries
        }
    }
    in.close();

    // Send these last 20 messages back through the stream to the user
    for (const auto& data_line : last20) {

      auto components = parse_data(data_line); // parse before sending as txt is (username,msg,timestamp)
     // std::cout<<components.size()<<std::endl;

      if (components.size() == 3) {  // Ensure there are exactly three components(username,msg,timestamp)
          Message response;
          response.set_username(components[0]); // set username
          response.set_msg(components[1]);  // set message

          // Convert the timestamp string to Timestamp
          auto timestamp = convert_to_timestamp(components[2]); 
          response.set_allocated_timestamp(new google::protobuf::Timestamp(timestamp)); // set timestamp

          stream->Write(response);  // send reply back to client
      }
    }
    // Broadcast messages to followers in a loop
    while (stream->Read(&message)) {
        // Format the incoming message for file output
        std::string formatted_timestamp = timestamp_to_string(message.timestamp());

        std::string ffo = format_file_output(message.username(), message.msg(), formatted_timestamp);
        
        // If not the first, append the formatted message to the user's personal file
        std::ofstream user_file(message.username() + ".txt", std::ios::app);
        user_file << ffo << std::endl;
        user_file.close();
        // Broadcast the received message to all of the client's followers
        for (Client* follower : client->client_followers) {
            if (follower->stream) {
                follower->stream->Write(message);  // Forward the message to each follower
            }
            else
            {
                // Append the message to each follower's following file
                std::ofstream fout(follower->username + "_following.txt", std::ios::app);
                fout << ffo << std::endl;
                fout.close();
            }
        }
    }

    // If the stream is closed, reset the client stream pointer
    client->stream = nullptr;
    
    return Status::OK;
  }

};

void RunServer(std::string port_no) {
  std::string server_address = "0.0.0.0:"+port_no;
  SNSServiceImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  log(INFO, "Server listening on "+server_address);

  server->Wait();
}

int main(int argc, char** argv) {

  std::string port = "3010";
  
  int opt = 0;
  while ((opt = getopt(argc, argv, "p:")) != -1){
    switch(opt) {
      case 'p':
          port = optarg;break;
      default:
	  std::cerr << "Invalid Command Line Argument\n";
    }
  }
  
  std::string log_file_name = std::string("server-") + port;
  google::InitGoogleLogging(log_file_name.c_str());
  log(INFO, "Logging Initialized. Server starting...");
  del_txt_files = true;
  RunServer(port);

  return 0;
}
