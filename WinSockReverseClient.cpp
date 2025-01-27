#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <map>
#include <mutex>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "GamesEngineeringBase.h"

#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_BUFFER_SIZE 1024

using namespace GamesEngineeringBase;

void run_client_loop() {
    const char* host = "127.0.0.1"; // Server IP address
    unsigned int port = 65432;
    std::string sentence = "Hello, server!";

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
    while (true) {
        //std::cin >> sentence; 
        std::cout << "Send to server: ";
        std::getline(std::cin, sentence);


        // Send the sentence to the server
        if (send(client_socket, sentence.c_str(), static_cast<int>(sentence.size()), 0) == SOCKET_ERROR) {
            std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
            closesocket(client_socket);
            WSACleanup();
            return;
        }

        if (sentence == "!bye") break;

        std::cout << "Sent: " << sentence << std::endl;

        // Receive the reversed sentence from the server
        char buffer[DEFAULT_BUFFER_SIZE] = { 0 };
        int bytes_received = recv(client_socket, buffer, DEFAULT_BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0'; // Null-terminate the received data
            std::cout << "Received from server: " << buffer << std::endl;
        }
        else if (bytes_received == 0) {
            std::cout << "Connection closed by server." << std::endl;
        }
        else {
            std::cerr << "Receive failed with error: " << WSAGetLastError() << std::endl;
        }

    }

    // Cleanup
    closesocket(client_socket);
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
        ImGui::EndChild();

        // Message input box and Send button at the bottom
        ImGui::InputText("Message", input_buffer, sizeof(input_buffer));
        ImGui::SameLine();
        if (ImGui::Button("Send")) {
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
    std::thread network_thread(run_client_loop);

    run_client_loop();
    // Run the GUI rendering loop
    render_gui();

    // Wait for the networking thread to finish
    network_thread.join();

    return 0;
}