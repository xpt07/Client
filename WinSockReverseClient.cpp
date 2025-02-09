#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <conio.h>
#include <queue>
#include <mutex>
#include <map>
#include <condition_variable>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "GamesEngineeringBase.h"

#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_BUFFER_SIZE 1024

using namespace GamesEngineeringBase;

std::queue<std::string> send_queue;
std::vector<std::string> chat_history;
std::map<std::string, std::vector<std::string>> private_chat_history;
std::vector<std::string> user_list;
std::queue<std::string> receive_queue;
std::map<std::string, bool> private_chats;
std::map<std::string, char[256]> private_chat_input;
std::mutex queue_mutex;
std::condition_variable queue_cv;
std::atomic<bool> close = false;

// Helper to safely enqueue a message
void enqueue_message(std::queue<std::string>& queue, const std::string& message) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    queue.push(message);
    queue_cv.notify_one();
}

// Append messages to chat history in a thread-safe manner
void append_to_chat_history(const std::string& message) {
    std::lock_guard<std::mutex> lock(queue_mutex);

    // Check if it's a private message: Format -> "[Private] sender: message"
    if (message.rfind("[Private]", 0) == 0) {
        size_t sender_start = message.find(' ') + 1;  // Get index after "[Private] "
        size_t colon_pos = message.find(':', sender_start);
        if (colon_pos != std::string::npos) {
            std::string sender = message.substr(sender_start, colon_pos - sender_start);  // Extract sender
            std::string msg = message.substr(colon_pos + 2);        // Extract message content

            private_chats[sender] = true;
            private_chat_history[sender].push_back(sender + ": " + msg);

            // Play private message sound
            FMODManager::PlaySoundW(FMODManager::GetPrivateMessageSound());

            return;
        }
    }

    // Otherwise, append to public chat
    chat_history.emplace_back(message);

    // Play public message sound
    FMODManager::PlaySound(FMODManager::GetPublicMessageSound());
}

// Parse and update the user list from the "USERLIST:" message
void update_user_list(const std::string& userlist_message) {
    std::lock_guard<std::mutex> lock(queue_mutex);

    // Clear the existing user list
    user_list.clear();
    std::cout << "Updating user list: " << userlist_message << std::endl; // Debugging

    // Extract usernames from the message: "USERLIST:username1,username2,..."
    size_t start = 9; // Skip the "USERLIST:" prefix
    size_t end = userlist_message.find(',', start);
    while (end != std::string::npos) {
        user_list.push_back(userlist_message.substr(start, end - start));
        start = end + 1;
        end = userlist_message.find(',', start);
    }

    // Add the last username (if any)
    if (start < userlist_message.size()) {
        user_list.push_back(userlist_message.substr(start));
    }
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

        bool is_private = false;
        std::string recipient, msg_content;

        if (message[0] == '@') {
            size_t colon_pos = message.find(':');
            if (colon_pos != std::string::npos) {
                size_t recipient_start = 1; // Skip '@'

                // Trim spaces before username
                while (recipient_start < colon_pos && message[recipient_start] == ' ') {
                    recipient_start++;
                }

                recipient = message.substr(recipient_start, colon_pos - recipient_start);
                msg_content = message.substr(colon_pos + 1);
                is_private = true;

                private_chat_history[recipient].push_back("You: " + msg_content);
                message = "@" + recipient + ": " + msg_content;  // Reformat message properly before sending
            }
        }

        // Send the message to the server
        if (send(client_socket, message.c_str(), static_cast<int>(message.size()), 0) == SOCKET_ERROR) {
            std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
            break;
        }

        // Only store public messages in chat_history
        if (!is_private) {
            chat_history.push_back("You: " + message);
        }

        if (message == "!bye") {
            close = true;
        }
    }

    closesocket(client_socket); // Ensure socket is closed when done
}

void Receive(SOCKET client_socket) {
    int count = 0;
    std::string message_buffer = "";
    while (!close) {
        // Receive the reversed sentence from the server
        char buffer[DEFAULT_BUFFER_SIZE] = { 0 };
        int bytes_received = recv(client_socket, buffer, DEFAULT_BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0'; // Null-terminate the received data
            message_buffer += buffer;

            size_t pos;
            while ((pos = message_buffer.find('\n')) != std::string::npos) {
                std::string message = message_buffer.substr(0, pos);
                message_buffer.erase(0, pos + 1);

                if (message.rfind("USERLIST:", 0) == 0) {
                    update_user_list(message);
                }
                else {
                    append_to_chat_history(message);
                }

                std::cout << "Received(" << count++ << "): " << message << std::endl;
            }
        }
        else {
            std::cerr << "Receive failed with error: " << WSAGetLastError() << std::endl;
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

void render_main_chat(char input_buffer[256]) {
    ImGui::Begin("Chat Client", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::SetWindowSize(ImVec2(800, 600));
    ImGui::SetWindowPos(ImVec2(0, 0));

    // User list on the left
    ImGui::BeginChild("Users", ImVec2(150, -80), true);
    ImGui::Text("Users:");
    for (const auto& username : user_list) {
        if (ImGui::Selectable(username.c_str())) {
            private_chats[username] = true; // Open private chat window
        }
    }
    ImGui::EndChild();

    // Chat messages
    ImGui::SameLine();
    ImGui::BeginChild("ChatArea", ImVec2(0, -80), true);
    for (const auto& message : chat_history) {
        ImGui::TextWrapped(message.c_str());
    }
    ImGui::EndChild();

    // Message input box and Send button
    ImGui::InputText("Message", input_buffer, 256);
    ImGui::SameLine();
    if (ImGui::Button("Send")) {
        if (strlen(input_buffer) > 0) {
            enqueue_message(send_queue, input_buffer);
            memset(input_buffer, 0, sizeof(input_buffer));
        }
    }

    ImGui::End();
}

void render_private_chats() {
    for (auto& chat : private_chats) {
        if (chat.second) { // Window is open
            ImGui::Begin(("Private Chat - " + chat.first).c_str(), &chat.second);

            // Chat history
            ImGui::BeginChild("PrivateChatArea", ImVec2(350, 300), true);
            for (const auto& msg : private_chat_history[chat.first]) {
                ImGui::TextWrapped(msg.c_str());
            }
            ImGui::EndChild();

            // Ensure an input buffer exists for this user
            if (private_chat_input.find(chat.first) == private_chat_input.end()) {
                memset(private_chat_input[chat.first], 0, sizeof(private_chat_input[chat.first]));
            }

            ImGui::InputText("PM", private_chat_input[chat.first], 256);
            ImGui::SameLine();
            if (ImGui::Button("Send")) {
                if (strlen(private_chat_input[chat.first]) > 0) {
                    enqueue_message(send_queue, "@" + chat.first + ": " + private_chat_input[chat.first]);
                    memset(private_chat_input[chat.first], 0, sizeof(private_chat_input[chat.first]));
                }
            }

            ImGui::End();
        }
    }
}

void render_gui() {
    Window app_window;
    app_window.create(1200, 800, "Client Chat Window");

    ImGuiManager::Initialize(app_window.getHWND(), app_window.getDev(), app_window.getDevContext());

    static char input_buffer[256] = { 0 };

    while (!close) {
        app_window.checkInput();
        ImGuiManager::BeginFrame();

        render_main_chat(input_buffer);
        render_private_chats();

        ImGuiManager::EndFrame();

        app_window.present();

        // Clear the render target view
        float ClearColor[4] = { 0.0f, 0.0f, 1.0f, 1.0f }; // RGBA
        app_window.getDevContext()->ClearRenderTargetView(app_window.getRenderTargetView(), ClearColor);
    }

    ImGuiManager::Shutdown();
}

int main() {
    // Initialize FMOD
    FMODManager::Initialize();

    // Create a thread for networking
    std::thread network_thread(client);

    // Run the GUI rendering loop
    render_gui();

    // Wait for the networking thread to finish
    network_thread.join();

    // Cleanup FMOD before exiting
    FMODManager::Shutdown();

    return 0;
}