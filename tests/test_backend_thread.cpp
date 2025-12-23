#include <catch2/catch_all.hpp>
#include <typr-io/sender.hpp>
#include <iostream>
#include <thread>
#include <string>
#include <future>

using namespace typr::io;
using namespace std::chrono_literals;

TEST_CASE("integration: simple STDIN loopback", "[integration]") {
    // 1. On s'assure que l'utilisateur est prêt et a le focus
    std::cout << "\n[PROMPT] Appuie sur ENTREE pour démarrer le test..." << std::endl;
    std::string dummy;
    std::getline(std::cin, dummy);

    Sender sender;
    
    // 2. Thread d'injection
    // On utilise une promise pour vérifier si l'injection s'est bien passée côté OS
    auto injection_task = std::async(std::launch::async, [&sender]() {
        // Petit délai pour laisser l'utilisateur relâcher sa propre touche Enter
        std::this_thread::sleep_for(500ms);
        
        bool okZ = sender.tap(Key::Z);
        bool okEnter = sender.tap(Key::Enter);
        
        return okZ && okEnter;
    });

    // 3. Lecture sur STDIN (Thread principal)
    std::cout << "[INFO] En attente de la réception des touches..." << std::endl;
    std::string received;
    std::getline(std::cin, received);

    // 4. Vérifications
    bool injectionSuccess = injection_task.get();
    
    // On vérifie si "Z" est présent dans ce qu'on a lu (en ignorant la casse)
    bool foundZ = (received.find('z') != std::string::npos || 
                   received.find('Z') != std::string::npos);

    REQUIRE(injectionSuccess == true);
    REQUIRE(foundZ == true);
    
    std::cout << "[SUCCESS] Reçu: '" << received << "'" << std::endl;
}