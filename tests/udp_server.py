import socket

# Define the server's IP address and port
server_ip = "127.0.0.1"
server_port = 12345

# Create a UDP socket
server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# Bind the socket to the IP address and port
server_socket.bind((server_ip, server_port))

print("UDP Echo Server is listening on {}:{}".format(server_ip, server_port))

while True:
    # Receive data and the client's address
    data, client_address = server_socket.recvfrom(1024)

    # Print received data
    print("Received from {}: '{}'".format(client_address, data.decode()))

    # Echo the data back to the client
    print("Sending '{}' back to {}".format(data.decode(), client_address))
    server_socket.sendto(data, client_address)
