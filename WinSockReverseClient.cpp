#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <conio.h>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "GamesEngineeringBase.h"

#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_BUFFER_SIZE 1024

using namespace GamesEngineeringBase;

std::queue<std::string> send_queue;
std::queue<std::string> receive_queue;
std::mutex queue_mutex;
std::condition_variable queue_cv;
std::atomic<bool> close = false;

// Helper to safely enqueue a message
void enqueue_message(std::queue<std::string>& queue, const std::string& message) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    queue.push(message);
    queue_cv.notify_one();
}

// Helper to safely dequeue a message
bool dequeue_message(std::queue<std::string>& queue, std::string& message) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    if (queue.empty()) return false;
    message = queue.front();
    queue.pop();
    return true;
}

void Send(SOCKET client_socket) {
    while (!close) {
        std::string message;
        // Wait for a message from the send queue
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [] { return !send_queue.empty() || close; });
            if (close) break;

            message = send_queue.front();
            send_queue.pop();
        }

        // Send the message to the server
        if (send(client_socket, message.c_str(), static_cast<int>(message.size()), 0) == SOCKET_ERROR) {
            std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
            break;
        }

        if (message == "!bye") {
            close = true;
        }
    }

    closesocket(client_socket); // Ensure socket is closed when done
}

void Receive(SOCKET client_socket) {
    int count = 0;
    while (!close) {
        // Receive the reversed sentence from the server
        char buffer[DEFAULT_BUFFER_SIZE] = { 0 };
        int bytes_received = recv(client_socket, buffer, DEFAULT_BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0'; // Null-terminate the received data
            enqueue_message(receive_queue, buffer); // Enqueue the received message for GUI
            std::cout << "Received(" << count++ << "): " << buffer << std::endl;
        }
        else if (bytes_received == 0) {
            std::cout << "Connection closed by server." << std::endl;
        }
        else if (close) {
            std::cout << "Terminating connection\n";
        }
        else {
            std::cerr << "Receive failed with error: " << WSAGetLastError() << std::endl;
        }
        if (strcmp(buffer, "!bye") == 0) {
            close = true;
        }
    }
}

void client() {

    const char* host = "127.0.0.1"; // Server IP address
    unsigned int port = 65432;

    // Initialize WinSock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
        return;
    }

    // Create a socket
    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return;
    }

    // Resolve the server address and port
    sockaddr_in server_address = {};
    server_address.sin_family = AF_INET;
    //server_address.sin_port = htons(std::stoi(port));
    server_address.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &server_address.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        closesocket(client_socket);
        WSACleanup();
        return;
    }

    // Connect to the server
    if (connect(client_socket, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) == SOCKET_ERROR) {
        std::cerr << "Connection failed with error: " << WSAGetLastError() << std::endl;
        closesocket(client_socket);
        WSACleanup();
        return;
    }

    std::cout << "Connected to the server." << std::endl;

    //  Send(client_socket);
     // Receive(client_socket);
    std::thread t1 = std::thread(Send, client_socket);
    std::thread t2 = std::thread(Receive, client_socket);

    t1.join();
    t2.join();

    // Cleanup
  //  closesocket(client_socket);
    WSACleanup();
}

void render_gui() {
    // Create the main application window
    Window app_window;
    app_window.create(1200, 800, "Client Chat Window");

    // Initialize ImGui with the window and Direct3D context
    ImGuiManager::Initialize(app_window.getHWND(), app_window.getDev(), app_window.getDevContext());

    char input_buffer[256] = { 0 };

    // Main rendering loop
    while (true) {
        app_window.checkInput();

        // Begin the ImGui frame
        ImGuiManager::BeginFrame();

        // Display any ImGui elements (can be extended later)
        ImGui::Begin("Chat Client", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        ImGui::SetWindowSize(ImVec2(800, 600));
        ImGui::SetWindowPos(ImVec2(0, 0));

        // User list on the left
        ImGui::BeginChild("Users", ImVec2(100, -80), true);
        ImGui::Text("Users:");
        ImGui::EndChild();

        // Chat messages on the right
        ImGui::SameLine();
        ImGui::BeginChild("ChatArea", ImVec2(0, -80), true);

        // Display messages from the receive queue
        std::string received_message;
        while (dequeue_message(receive_queue, received_message)) {
            ImGui::TextWrapped(received_message.c_str());
        }

        ImGui::EndChild();

        // Message input box and Send button at the bottom
        ImGui::InputText("Message", input_buffer, sizeof(input_buffer));
        ImGui::SameLine();
        if (ImGui::Button("Send")) {
            enqueue_message(send_queue, input_buffer); // Enqueue message for sending
            memset(input_buffer, 0, sizeof(input_buffer)); // Clear input buffer
        }

        ImGui::End();

        // End the ImGui frame
        ImGuiManager::EndFrame();

        // Present the final rendered frame
        app_window.present();
    }

    // Shutdown ImGui
    ImGuiManager::Shutdown();
}

int main() {
    // Create a thread for networking
    std::thread network_thread(client);

    // Run the GUI rendering loop
    render_gui();

    // Wait for the networking thread to finish
    network_thread.join();

    return 0;
}