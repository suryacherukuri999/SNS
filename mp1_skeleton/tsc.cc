#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <string>
#include <unistd.h>
#include <csignal>
#include <grpc++/grpc++.h>
#include "client.h"

#include "sns.grpc.pb.h"
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using csce662::Message;
using csce662::ListReply;
using csce662::Request;
using csce662::Reply;
using csce662::SNSService;

void sig_ignore(int sig) {
  std::cout << "Signal caught " + sig;
}

Message MakeMessage(const std::string& username, const std::string& msg) {
    Message m;
    m.set_username(username);
    m.set_msg(msg);
    google::protobuf::Timestamp* timestamp = new google::protobuf::Timestamp();
    timestamp->set_seconds(time(NULL));
    timestamp->set_nanos(0);
    m.set_allocated_timestamp(timestamp);
    return m;
}


class Client : public IClient
{
public:
  Client(const std::string& hname,
	 const std::string& uname,
	 const std::string& p)
    :hostname(hname), username(uname), port(p) {}

  
protected:
  virtual int connectTo();
  virtual IReply processCommand(std::string& input);
  virtual void processTimeline();

private:
  std::string hostname;
  std::string username;
  std::string port;
  
  // You can have an instance of the client stub
  // as a member variable.
  std::unique_ptr<SNSService::Stub> stub_;
  
  IReply Login();
  IReply List();
  IReply Follow(const std::string &username);
  IReply UnFollow(const std::string &username);
  void   Timeline(const std::string &username);
};


///////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////
int Client::connectTo()
{
  
  std::string server_address = hostname + ":" + port;
  stub_ = SNSService::NewStub(grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials())); // create a stub to interact with server
  
  // Check if the connection is successful by attempting to log in
  IReply reply = Login();
  if (reply.grpc_status.ok() && reply.comm_status == SUCCESS) {
      return 1;  // Successfully connected
  } else {
      //std::cerr << "Failed to connect to server: " << reply.grpc_status.error_message() << std::endl;
      return -1;  // Connection failed
  }
  return 1;
}

IReply Client::processCommand(std::string& input)
{
    IReply ire;
    std::istringstream iss(input);
    std::string command;
    std::string ignore;
    iss >> command;

    if (command == "FOLLOW") {
        std::string user_to_follow;
        iss >> user_to_follow;
        iss >> ignore;
        if(iss)
        {
            ire.comm_status = FAILURE_INVALID;
            return ire;
        }
        if (!user_to_follow.empty()) {
            ire = Follow(user_to_follow);
        } else {
            std::cerr << "No user specified to follow." << std::endl;
            ire.comm_status = FAILURE_INVALID;
        }
    } else if (command == "UNFOLLOW") {
        std::string user_to_unfollow;
        iss >> user_to_unfollow;
        iss >> ignore;
        if(iss)
        {
            ire.comm_status = FAILURE_INVALID;
            return ire;
        }
        if (!user_to_unfollow.empty()) {
            ire = UnFollow(user_to_unfollow);
        } else {
            std::cerr << "No user specified to unfollow." << std::endl;
            ire.comm_status = FAILURE_INVALID;
        }
    } else if (command == "LIST") {
        iss >> ignore;
        if(iss)
        {
            ire.comm_status = FAILURE_INVALID;
            return ire;
        }
        ire = List();
    } else if (command == "TIMELINE") {
        iss >> ignore;
        if(iss)
        {
            ire.comm_status = FAILURE_INVALID;
            return ire;
        }
       // processTimeline();
        ire.comm_status = SUCCESS;
    } else {
        std::cerr << "Invalid command: " << command << std::endl;
        ire.comm_status = FAILURE_INVALID;
    }

    return ire;
}


void Client::processTimeline()
{
    Timeline(username);
}

// List Command
IReply Client::List() {
    IReply ire;
    Request request;
    request.set_username(username);  // set the username

    ClientContext context;

    ListReply server_reply;
    grpc::Status status = stub_->List(&context, request, &server_reply); // call list fun in tsd.cc (server)

    ire.grpc_status = status;
    if (status.ok()) { // if success
        ire.comm_status = SUCCESS;
        for (const auto& user : server_reply.all_users()) {
            ire.all_users.push_back(user);
        }
        for (const auto& follower : server_reply.followers()) {
            ire.followers.push_back(follower);
        }
    } else {
        ire.comm_status = FAILURE_UNKNOWN; // failed
        std::cerr << "Failed to list users: " << status.error_message() << std::endl;
    }

    return ire;
}

// Follow Command        
IReply Client::Follow(const std::string& username2) {
    IReply ire; 
      
    Request request;
    request.set_username(username); // current user
    request.add_arguments(username2);  // The username to follow

    ClientContext context;

    Reply server_reply;
    grpc::Status status = stub_->Follow(&context, request, &server_reply); // call follow function in tsd.cc
    
    ire.grpc_status = status;
    if (status.ok()) {   // success
        ire.comm_status = SUCCESS;
        std::cout << "Successfully followed " << username2 << std::endl;
    } 
    else if(status.error_code() == grpc::ALREADY_EXISTS) // if we try to follow ourself or already followed
    {
        ire.grpc_status = Status::OK;
        ire.comm_status = FAILURE_ALREADY_EXISTS; 
    }
    else {
        ire.grpc_status = Status::OK;  // if follower doesnt exist
        ire.comm_status = FAILURE_INVALID_USERNAME;
    }

    return ire;
}

// UNFollow Command  
IReply Client::UnFollow(const std::string& username2) {
    IReply ire;

    Request request;
    request.set_username(username); // current user
    request.add_arguments(username2);  // The username to unfollow

    ClientContext context;

    Reply server_reply;
    grpc::Status status = stub_->UnFollow(&context, request, &server_reply); // call unfollow fun in tsd.cc(server)

    ire.grpc_status = status;
    if (status.ok()) {
        ire.comm_status = SUCCESS;
        std::cout << "Successfully unfollowed " << username2 << std::endl; // success
    } 
    else if(status.error_code() == grpc::ALREADY_EXISTS) // if we try to unfollow ourself or already unfollowed
    {
        ire.grpc_status = Status::OK;
        ire.comm_status = FAILURE_ALREADY_EXISTS; 
    }
    else {
        ire.grpc_status = Status::OK;
        ire.comm_status = FAILURE_INVALID_USERNAME; // if the username to unfollow doesn't exist
       
    }

    return ire;
}

// Login Command  
IReply Client::Login() {

    IReply ire;
  
    Request request;
    request.set_username(username);  // Set the username

    ClientContext context;
    
    // Make a gRPC call to the Login RPC
    Reply server_reply;
    grpc::Status status = stub_->Login(&context, request, &server_reply); // call login function in tsd.cc(server side)
    
    ire.grpc_status = status;
    if (status.ok()) {
        ire.comm_status = SUCCESS;    // succesful login
        std::cout << server_reply.msg()<< std::endl;
    } else if(status.error_code() == grpc::ALREADY_EXISTS){  // if user already loggedin
        ire.comm_status = FAILURE_ALREADY_EXISTS;
        std::cerr << "Login failed: " << status.error_message() << std::endl;
    }
    else
    {
        ire.comm_status = FAILURE_NOT_EXISTS;
        std::cerr << "Login failed: " << status.error_message() << std::endl;
    }
    
    return ire;
}

// Timeline Command
void Client::Timeline(const std::string& username) {

    // ------------------------------------------------------------
    // In this function, you are supposed to get into timeline mode.
    // You may need to call a service method to communicate with
    // the server. Use getPostMessage/displayPostMessage functions 
    // in client.cc file for both getting and displaying messages 
    // in timeline mode.
    // ------------------------------------------------------------

    // ------------------------------------------------------------
    // IMPORTANT NOTICE:
    //
    // Once a user enter to timeline mode , there is no way
    // to command mode. You don't have to worry about this situation,
    // and you can terminate the client program by pressing
    // CTRL-C (SIGINT)
    // ------------------------------------------------------------
  
    ClientContext context;
    std::shared_ptr<ClientReaderWriter<Message, Message>> stream(
        stub_->Timeline(&context));   // bidirectional stream

    Message initial_message;
    initial_message.set_username(username);
    stream->Write(initial_message);    // When user enters timeline for the 1st time we just trigger the serve to store the stream correspond to client

    // Separate thread to read messages from the server
    std::thread reader([stream]() { 
        Message server_message;
        while (stream->Read(&server_message)) { //  reading messages from server
            std::time_t timestamp = server_message.timestamp().seconds();
            displayPostMessage(server_message.username(), server_message.msg(), timestamp); // if msg exist post the msg in timeline
        }
    });
    // Sending messages
    std::string input;
    while (true) {  // This is for stdinput
        input = getPostMessage();  // capture the client input
        Message message = MakeMessage(username, input);
        stream->Write(message);  // write to the stream so that server process this msg
    }
    stream->WritesDone();
    reader.join();  // Wait for the reader thread to finish

}



//////////////////////////////////////////////
// Main Function
/////////////////////////////////////////////
int main(int argc, char** argv) {

  std::string hostname = "localhost";
  std::string username = "default";
  std::string port = "3010";
    
  int opt = 0;
  while ((opt = getopt(argc, argv, "h:u:p:")) != -1){
    switch(opt) {
    case 'h':
      hostname = optarg;break;
    case 'u':
      username = optarg;break;
    case 'p':
      port = optarg;break;
    default:
      std::cout << "Invalid Command Line Argument\n";
    }
  }
      
  std::cout << "Logging Initialized. Client starting..."<<std::endl;
  
  Client myc(hostname, username, port);
  
  myc.run();
  
  return 0;
}
