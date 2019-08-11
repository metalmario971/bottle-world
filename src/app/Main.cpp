

#include "../base/DebugHelper.h"
#include "../base/AppRunner.h"
#include "../base/Gu.h"
#include "../app/AppMain.h"
using namespace Game;
int main(int argc, char** argv) {

    DebugHelper::debugHeapBegin(false);
    {
        //Game::DebugHelper::setBreakAlloc(221975);
        std::shared_ptr<AppMain> cwr = std::make_shared<AppMain>();
        std::shared_ptr<AppRunner> ar = std::make_shared<AppRunner>();
        ar->runApp(Gu::argsToVectorOfString(argc, argv), "Shake", cwr);
    }
    DebugHelper::debugHeapEnd();


    return 0;
}