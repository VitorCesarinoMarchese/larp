#include "transport/session.hpp"
#include <iostream>
int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: larp-keygen PATH\n";
            return 1;
        }
        larp::generate_session_key(argv[1]);
        std::cout << "Created private session key file\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
