#include "core/Engine.hpp"
#include <iostream>
#include <exception>
#include <cstdlib>

int main() {
    try {
        Engine::EngineApp app;
        app.Run();
    } catch (const std::exception& e) {
        std::cerr << "[Engine Fatal Exception]: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
