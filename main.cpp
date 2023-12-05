#include <iostream>
#include "sc2api/sc2_api.h"
#include "sc2lib/sc2_lib.h"
#include "sc2utils/sc2_manage_process.h"
#include "sc2utils/sc2_arg_parser.h"
#include "cpp-sc2/examples/common/bot_examples.h"

#include "zerg_crush.h"
#include "LadderInterface.h"

// LadderInterface allows the bot to be tested against the built-in AI or
// played against other bots
int main(int argc, char* argv[]) {
	/*
	sc2::Coordinator coordinator;
    coordinator.LoadSettings(argc, argv);

    coordinator.SetParticipants({
        CreateParticipant(sc2::Race::Terran, new ZergCrush()),
        CreateParticipant(sc2::Race::Terran, new MultiplayerBot())
    });

    coordinator.LaunchStarcraft();
    coordinator.StartGame(sc2::kMapBelShirVestigeLE);

    while (coordinator.Update()) {}
	*/
	RunBot(argc, argv, new ZergCrush(), sc2::Race::Terran);
	return 0;
}