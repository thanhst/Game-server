#include <exception>
#include <iostream>
void runLegacyCodecTests();
void runMatrixChallengeTests();
void runEccChallengeTests();
void runLegacyContentTests();
void runLegacyCacheTests();
void runLegacyCombatTests();
void runLegacyWorldTests();
void runMapGeometryTests();
void runIdentityTests();
void runLegacyApplicationTests();
void runLegacyReplayTests();
int main() {
    try {
        runLegacyCodecTests(); runMatrixChallengeTests(); runEccChallengeTests();
        runLegacyContentTests(); runLegacyCacheTests(); runLegacyCombatTests();
        runLegacyWorldTests(); runMapGeometryTests(); runIdentityTests();
        runLegacyApplicationTests(); runLegacyReplayTests();
        std::cout<<"Legacy compatibility checks passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL legacy: "<<error.what()<<'\n'; return 1;
    }
}
