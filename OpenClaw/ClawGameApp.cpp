#include "Engine/Util/StartupTrace.h"
#include "ClawGameApp.h"
#include "ClawHumanView.h"
#include "ClawGameLogic.h"

BaseGameLogic* ClawGameApp::VCreateGameAndView()
{
    m_pGame = new ClawGameLogic();
    STARTUP_TRACE("game logic created");
    if (!m_pGame->Initialize())
    {
        LOG_ERROR("Could not initialize Claw Game Logic.");
        return NULL;
    }

    STARTUP_TRACE("game logic initialized");
    shared_ptr<HumanView> clawHumanView(new ClawHumanView(GetRenderer()));
    STARTUP_TRACE("human view created");
    m_pGame->VAddView(clawHumanView);
    STARTUP_TRACE("human view added");

    // Register command handler
    clawHumanView->RegisterConsoleCommandHandler(CommandHandler::HandleCommand, NULL);

    return m_pGame;
}