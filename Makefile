CC = gcc
CFLAGS = -Wall -g -pthread
SRC_DIR = src

# 服务端源文件
SERVER_SRC = $(SRC_DIR)/server.c
# 客户端源文件
CLIENT_SRC = $(SRC_DIR)/slient.c

# 可执行文件输出到当前目录
SERVER_TARGET = server
CLIENT_TARGET = client

all: $(SERVER_TARGET) $(CLIENT_TARGET)

$(SERVER_TARGET): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $@ $^

$(CLIENT_TARGET): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET)