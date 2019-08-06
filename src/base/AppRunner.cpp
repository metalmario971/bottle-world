#include <signal.h>
#include "../base/AppRunner.h"
#include "../base/DebugHelper.h"
#include "../math/MathAll.h"

#include "../base/SoundCache.h"
#include "../base/GLContext.h"
#include "../base/Fingers.h"
#include "../base/Logger.h"
#include "../base/TStr.h"
#include "../base/oglErr.h"
#include "../base/OperatingSystem.h"
#include "../base/Engine.h"
#include "../model/ModelCache.h"
#include "../display/GraphicsApi.h"
#include "../display/OpenGLApi.h"

namespace Game { ;

void loadCheckProc() {
    PFNGLUSEPROGRAMPROC proc = (PFNGLUSEPROGRAMPROC)SDL_GL_GetProcAddress("glUseProgram");
    if (proc == nullptr) {
        t_string exep;
        exep += "glUseProgram was not found in your graphics driver.  There can be a few reasons for this:\n";
        exep += ("  1. Your primary graphics card is not correct.  You can set your primary graphics card in Windows.\n");
        exep += ("  2. Your graphics card is outdated.  Consider upgrading.\n");
        exep += ("  3. Your Operating System isn't Windows 7 or above.\n");
        BroThrowException(exep);
    }
}
void AppRunner::doShowError(t_string err, Exception* e)
{
    //Just in case - this is needed so we don't regress.
  //  _setupState = EngineSetupState::ErrorExit;

    //    SyncDisable();
    if (e != nullptr)
    {
        OperatingSystem::showErrorDialog(e->what() + err);
    }
    else
    {
        OperatingSystem::showErrorDialog("No Error Message" + err);
    }
    // Runtime Error

    //Do not log a second time!
    //BroLogError(err+ e->what());

    //Don't destroy engine in error msg.
    //destroyEngineAndExit();
    //  SyncEnable();
}
void AppRunner::initSDL(t_string windowTitle, std::shared_ptr<AppBase> app)
{
    // int w, h;
    //  SDL_DisplayMode fullscreen_mode;
    SDL_SetMainReady();

    //You have to call this in order to initialize the SDL pointer before anyhing
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) == -1) {
        exitApp(Stz "SDL could not be initialized: " + SDL_GetError() , -1);
    }

    checkVideoCard();

    if (Gu::getEngineConfig()->getRenderSystem() == RenderSystem::OpenGL) {
        _pGraphicsApi = std::make_shared<OpenGLApi>();
    }
    else if (Gu::getEngineConfig()->getRenderSystem() == RenderSystem::Vulkan) {
       // _pGraphicsApi = std::make_shared<VulkanGraphicsApi>();
    }
    else {
        BroThrowException("Invalid render engine.");
    }

    _pGraphicsApi->createWindow(windowTitle);

    loadCheckProc();
    OglErr::checkSDLErr();

    initAudio();
    OglErr::checkSDLErr();

    loadCheckProc();
    OglErr::checkSDLErr();

    initNet();
    OglErr::checkSDLErr();

    if (Gu::getEngineConfig()->getGameHostAttached()) {
        updateWindowHandleForGamehost();
        attachToGameHost();
    }

    _pGraphicsApi->createContext(app);
}
void AppRunner::attachToGameHost() {
    int GameHostPort = Gu::getEngineConfig()->getGameHostPort();

    IPaddress ip;
    if (SDLNet_ResolveHost(&ip, NULL, GameHostPort) == -1) {
        exitApp(std::string("") + "SDLNet_ResolveHost: "+ SDLNet_GetError() , -1);
    }

    _server_tcpsock = SDLNet_TCP_Open(&ip);
    if (!_server_tcpsock) {
        exitApp(std::string("") + "SDLNet_TCP_Open:" + SDLNet_GetError(), -1);
    }
    
    //Wait a few, then exit if we don't get a response.
    t_timeval t0 = Gu::getMilliSeconds();

    while (true) 
    {
        int timeout = Gu::getEngineConfig()->getGameHostTimeoutMs();

        if (Gu::getMilliSeconds() - t0 > timeout) {
            exitApp( Stz "Failed to connect to game host, timeout "+ timeout+ "ms exceeded." , -1);
            break;//Unreachable, but just in case.
        }
        
        _gameHostSocket = SDLNet_TCP_Accept(_server_tcpsock);
        if (_gameHostSocket) {
          //  char data[512];

            //while(SDLNet_TCP_Recv(_gameHostSocket, data, 512) <= 0) {
            //    //DebugBreak();
            //    int n = 0;
            //    n++;
            //}
            //DebugBreak();

            // communicate over new_tcpsock
            break;
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

}
void AppRunner::checkVideoCard(){
    int i;

    // Declare display mode structure to be filled in.
    SDL_DisplayMode current;

    SDL_Init(SDL_INIT_VIDEO);
    int nDisplays = SDL_GetNumVideoDisplays();
    BroLogInfo(nDisplays + " Displays:");

    // Get current display mode of all displays.
    for (i = 0; i < SDL_GetNumVideoDisplays(); ++i) {

        int should_be_zero = SDL_GetCurrentDisplayMode(i, &current);

        if (should_be_zero != 0){
            // In case of error...
            BroLogInfo("  Could not get display mode for video display #%d: %s"+ i);
            OglErr::checkSDLErr();
        }
        else{
            // On success, print the current display mode.
            BroLogInfo("  Display "+ i+ ": "+ current.w+ "x"+ current.h+ ", "+current.refresh_rate+"hz");
            OglErr::checkSDLErr();
        }
    }
}

void AppRunner::updateWindowHandleForGamehost() {
#ifdef _WINDOWS_
    //For the WPF app container we need to set the window handle to be the top window
    //https://docs.microsoft.com/en-us/dotnet/api/system.diagnostics.process.mainwindowhandle?view=netframework-4.8
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(_pGraphicsApi->getWindow(), &wmInfo);
    HWND hwnd = wmInfo.info.win.window;
    HWND old = GetActiveWindow();
    SetActiveWindow(hwnd);
#endif
}

SDL_bool AppRunner::initAudio() {
    //AUDIO
    if (SDL_AudioInit(NULL) < 0) {
        fprintf(stderr, "Couldn't initialize audio driver: %s\n", SDL_GetError());
        return SDL_FALSE;
    }
    return SDL_TRUE;
}
void AppRunner::initNet() {
    BroLogInfo("Initializing SDL Net");
    if (SDLNet_Init() == -1) {
        exitApp(Stz "SDL Net could not be initialized: "+ SDL_GetError() , -1);
    }
}

void AppRunner::runApp(std::vector<t_string>& args, t_string windowTitle, std::shared_ptr<AppBase> app) {
    _tvInitStartTime = Gu::getMicroSeconds();

	//Root the engine FIRST so we can find the EngineConfig.dat
	FileSystem::setExecutablePath(args[0]);
	t_string a = FileSystem::getCurrentDirectory();
	FileSystem::setCurrentDirectory(FileSystem::getExecutableDirectory());
	t_string b = FileSystem::getCurrentDirectory();

    //**Must come first before other logic
    Gu::initGlobals(app, args);
    {
        initSDL(windowTitle, app);
        

        if(runCommands(args) == false) {
            // - Game Loop
            //*Init room
            //*Setup everything
            //Most processing
            runGameLoopTryCatch(app);
        }
    }
    Gu::deleteGlobals();
}
void SignalHandler(int signal)
{
    BroThrowException(Stz "VC Access Violation. signal=" + signal + "  This shouldn't work in release build.");
}
void AppRunner::runGameLoopTryCatch(std::shared_ptr<AppBase> rb){
    typedef void(*SignalHandlerPointer)(int);
    
    //https://stackoverflow.com/questions/457577/catching-access-violation-exceptions
    SignalHandlerPointer previousHandler;
    previousHandler = signal(SIGSEGV, SignalHandler);

    BroLogInfo("Entering Game Loop");
    try
    {
        runGameLoop(rb);
    }
    catch (Exception* e)
    {
        doShowError("Runtime Error", e);
    }
    catch (...)
    {
        doShowError("Runtime Error, Unhandled exception.", nullptr);
    }
}
bool AppRunner::handleEvents(SDL_Event * event)
{
    static SDL_MouseMotionEvent lastEvent;
    int n = 0;
    vec2 delta;
    SDL_Scancode keyCode;
    std::shared_ptr<Fingers> pFingers = Gu::getContext()->getFingers();

    if (event == nullptr)
        return true;

    switch (event->type) {
    case SDL_MOUSEMOTION:
        //Don't use this.  Because of "SDL_warpMouse" which will mess stuff up.
        break;
    case SDL_KEYDOWN:
        keyCode = event->key.keysym.scancode;
        pFingers->setKeyDown(keyCode);
        break;
    case SDL_KEYUP:
        keyCode = event->key.keysym.scancode;
        pFingers->setKeyUp(keyCode);
        break;
    case SDL_MOUSEBUTTONDOWN:
            switch (event->button.button) {
            case SDL_BUTTON_LEFT:
                pFingers->setLmbState(ButtonState::Press);
                break;
            case SDL_BUTTON_MIDDLE:
                break;
            case SDL_BUTTON_RIGHT:
                pFingers->setRmbState(ButtonState::Press);
                break;
            }
        break;
    case SDL_MOUSEBUTTONUP:
        switch (event->button.button) {
        case SDL_BUTTON_LEFT:
            pFingers->setLmbState(ButtonState::Release);
            break;
        case SDL_BUTTON_MIDDLE:
            break;
        case SDL_BUTTON_RIGHT:
            pFingers->setRmbState(ButtonState::Release);
            break;
        }
        break;
    case SDL_WINDOWEVENT:
        switch (event->window.event) {
        case SDL_WINDOWEVENT_CLOSE:
            return false;
            break;
        }
        break;
    case SDL_MOUSEWHEEL:
        if (event->wheel.y != 0) {

            int n = MathUtils::broMin(10, MathUtils::broMax(-10, event->wheel.y));

            _pEngine->userZoom(n);
            n++;
        }
        if (event->wheel.x != 0) {
            n++;
        }
        break;
    case SDL_QUIT:
        return false;
        break;
    }

    return true;
}

void AppRunner::runGameLoop(std::shared_ptr<AppBase> rb)
{
    SDL_Event event;
    bool done = false;
    int w, h;

#ifdef __WINDOWS__
    SDL_ShowCursor(SDL_DISABLE);
#endif

    SDL_Window* win = _pGraphicsApi->getWindow();

    SDL_GL_GetDrawableSize(win, &w, &h);
    
    _pEngine = new Engine(rb, _pGraphicsApi, w, h);
    _pEngine->init();

    _pGraphicsApi->makeCurrent();

    //Print the setup time.
    t_string str = "";
    str = str+ "\r\n======================================================\r\n";
    str = str+ "  **Total initialization time: "+ MathUtils::round((float)((Gu::getMicroSeconds() - _tvInitStartTime) / 1000) / 1000.0f, 2)+ " seconds\r\n";
    str = str+ "======================================================\r\n";
    BroLogInfo(str);


    std::shared_ptr<Fingers> pFingers = Gu::getContext()->getFingers();

    while (done == false)
    {
        Gu::beginPerf();
        Gu::pushPerf();
        if(false){
            //AV Exception test **doesn't work in anything but debug build.
            *(int *)0 = 0;
        }
        _pEngine->updateDelta();

        while (SDL_PollEvent(&event))
        {
            if (handleEvents(&event) == false)
            {
                done = true;
            }
        }
        pFingers->preUpdate();
        
        doAnnoyingSoundTest();

        _pEngine->updateTouch(pFingers);

        _pGraphicsApi->getDrawableSize(&w, &h);

        _pEngine->step(w, h);

        _pGraphicsApi->swapBuffers();


        //Update all button states.
        pFingers->postUpdate();
        Gu::popPerf();
        Gu::endPerf();
        DebugHelper::checkMemory();


        //**End of loop error -- Don't Remove** 
        Gu::getContext()->chkErrRt();
        //**End of loop error -- Don't Remove** 
    }

    _pEngine->cleanup();
    DebugHelper::checkMemory();

    //quit(0);
}
void AppRunner::exitApp(t_string error, int rc)
{
    OperatingSystem::showErrorDialog(error+ SDLNet_GetError());

    Gu::debugBreak();

    _pGraphicsApi->cleanup();

    SDLNet_Quit();
    SDL_Quit();

    exit(rc);
}

void AppRunner::doAnnoyingSoundTest(){
    // TEST
    //no
  //  static bool bSoundTestSpace = false;
  //  if (GetAsyncKeyState(VK_SPACE) & 0x8000) {
  //      if (bSoundTestSpace == false) {
  //          bSoundTestSpace = true;
  //          std::shared_ptr<SoundSpec> ss = _pEngine->getContext()->getSoundCache()->getOrLoad("./data-dc/snd/ogg-test.ogg");
  //          ss->play();
  //      }
  //  }
  //  else {
  //      bSoundTestSpace = false;
  //  }
}
bool AppRunner::argMatch(std::vector<t_string>& args, t_string arg1,  int32_t iCount) {
    if(args.size() <= 1){
        return false;
    }
    if (StringUtil::lowercase(arg1) == StringUtil::lowercase(args[1])) {
        
        return true;
    }
    return false;
}
bool AppRunner::runCommands(std::vector<t_string>& args) {
    if (argMatch(args, "/c", 4)) {
        //Convert Mob
        t_string strMob = args[2];
        t_string strFriendlyName = args[3];
        Gu::getContext()->getModelCache()->convertMobToBin(strMob, false, strFriendlyName);
        return true;
    }
    else if (argMatch(args, "/s", 3)) {
        //Compile Shader
        // cw.exe /s "ShaderName"  "dir/ShaderFileDesc.dat"

        return true;
    }
    else{
        return false;
    }

}



}
