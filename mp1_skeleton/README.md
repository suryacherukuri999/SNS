# SNS — Social Network Service (CSCE 662 MP1)

A distributed Social Network Service (SNS) built using **C++** and **gRPC**, implementing a bidirectional streaming architecture where clients can follow users and exchange messages in real time.

---

## Project Structure

```
mp1_skeleton/
├── tsd.cc          # gRPC server implementation (SNS daemon)
├── tsc.cc          # gRPC client implementation
├── client.h        # IClient interface definition
├── sns.proto       # Protobuf service and message definitions
├── Makefile        # Build configuration
└── tsn-service_start.sh  # Helper script to start the service
```

---

## Features

- **Login** — Register and authenticate users on the server
- **Follow / Unfollow** — Manage social connections between users
- **List** — View all registered users and your followers
- **Timeline** — Enter a real-time bidirectional stream to post and receive messages

---

## Build & Run

### Prerequisites
- gRPC and Protocol Buffers installed
- `glog` (Google Logging Library)
- C++17 compatible compiler

### Compile

```bash
make
```

### Clean build artifacts

```bash
make clean
```

### Run the Server

```bash
# Default port 3010
./tsd

# Custom port
./tsd -p <port>

# With verbose glog output
GLOG_logtostderr=1 ./tsd -p <port>
```

### Run the Client

```bash
# Default host (localhost) and port (3010)
./tsc -u <username>

# Custom host and port
./tsc -h <host_addr> -p <port> -u <username>

# With verbose glog output
GLOG_logtostderr=1 ./tsc -h <host_addr> -p <port> -u <username>
```

---

## Client Commands

Once connected, the following commands are available in command mode:

| Command               | Description                        |
|-----------------------|------------------------------------|
| `LIST`                | Show all users and your followers  |
| `FOLLOW <username>`   | Follow a user                      |
| `UNFOLLOW <username>` | Unfollow a user                    |
| `TIMELINE`            | Enter real-time timeline mode      |

> **Note:** Once in timeline mode, press `CTRL-C` to exit.

---

## gRPC Service Definition

Defined in `sns.proto`:

```proto
service SNSService {
  rpc Login(Request) returns (Reply)
  rpc List(Request) returns (ListReply)
  rpc Follow(Request) returns (Reply)
  rpc UnFollow(Request) returns (Reply)
  rpc Timeline(stream Message) returns (stream Message)  // Bidirectional streaming
}
```
