#include "data_win.h"
#include "ps3gl.h"
#include "rsxutil.h"
#include "vm.h"
#include "wad_versions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <malloc.h>

#include "runner_keyboard.h"
#include "runner.h"
#include "input_recording.h"
#include "debug_overlay.h"
#include "gl_legacy_renderer.h"
#include "overlay_file_system.h"
#include "ps3_overlay.h"
#include "ps3_session_log.h"
#include "ps3_textures.h"
#include "ps3_wad_picker.h"
#ifdef USE_OPENAL
#include "al_audio_system.h"
#endif
#include "stb_ds.h"
#include "stb_image_write.h"

#include "utils.h"
#include "profiler.h"
#include "gettime.h"

// Paletted fragment shader.
extern unsigned char paletted_fpo[];
extern unsigned int  paletted_fpo_len;
GLuint gPalettedProgram = 0;
GLint  gPalettedUPaletteVLoc = -1;

#include <io/pad.h>
#include <sys/systime.h>
#include <sys/thread.h>
#include <sysutil/sysutil.h>
#include <ppu_intrinsics.h>
#include <sys/stat.h>

typedef struct {
    uint8_t digital;
    uint8_t mask;
    int32_t gmlKey;
} PadMapping;

const PadMapping PAD_MAPPINGS[] = {
    { PAD_BUTTON_OFFSET_DIGITAL1, PAD_CTRL_UP,       VK_UP },
    { PAD_BUTTON_OFFSET_DIGITAL1, PAD_CTRL_DOWN,     VK_DOWN },
    { PAD_BUTTON_OFFSET_DIGITAL1, PAD_CTRL_LEFT,     VK_LEFT },
    { PAD_BUTTON_OFFSET_DIGITAL1, PAD_CTRL_RIGHT,    VK_RIGHT },
    { PAD_BUTTON_OFFSET_DIGITAL1, PAD_CTRL_START,    'C' },
#ifdef DONUT_SPIDER_DEV_BUILD
    { PAD_BUTTON_OFFSET_DIGITAL1, PAD_CTRL_SELECT,   VK_F12 },
#endif
    { PAD_BUTTON_OFFSET_DIGITAL2, PAD_CTRL_CROSS,    'Z' },
    { PAD_BUTTON_OFFSET_DIGITAL2, PAD_CTRL_SQUARE,   'X' },
    { PAD_BUTTON_OFFSET_DIGITAL2, PAD_CTRL_TRIANGLE, 'C' },
    { PAD_BUTTON_OFFSET_DIGITAL2, PAD_CTRL_L1,       VK_PAGEDOWN },
    { PAD_BUTTON_OFFSET_DIGITAL2, PAD_CTRL_R1,       VK_PAGEUP },
    { PAD_BUTTON_OFFSET_DIGITAL2, PAD_CTRL_L2,       VK_F10 },
};
#define PAD_MAPPING_COUNT (sizeof(PAD_MAPPINGS) / sizeof(PAD_MAPPINGS[0]))
static bool prevState[sizeof(PAD_MAPPINGS) / sizeof(PAD_MAPPINGS[0])] = {0};

#ifdef DONUT_SPIDER_DEV_BUILD
static void runSurfaceReadbackSelfTest(void) {
    GLuint texture = 0;
    GLuint framebuffer = 0;
    uint8_t pixel[4] = {0, 0, 0, 0};

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glViewport(0, 0, 2, 2);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glColorMask(true, true, true, true);
    glClearColor(0.25f, 0.50f, 0.75f, 0.875f);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    fprintf(stderr, "GLSELFTEST: clear expected RGBA~64,128,191,223 read=%u,%u,%u,%u\n",
            (unsigned)pixel[0], (unsigned)pixel[1], (unsigned)pixel[2], (unsigned)pixel[3]);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, 0);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glColorMask(true, true, true, true);
}
#endif

#define STICK_CENTER 0x80 // The center of the stick (range 0x00-0xFF)
#define STICK_THRESHOLD 0x40 // The threshold for treating stick movement as a d-pad press

typedef struct {
    uint8_t axis;
    int8_t  sign;
    int32_t gmlKey;
} StickMapping;

const StickMapping STICK_MAPPINGS[] = {
    { PAD_BUTTON_OFFSET_ANALOG_LEFT_X, -1, VK_LEFT  },
    { PAD_BUTTON_OFFSET_ANALOG_LEFT_X, +1, VK_RIGHT },
    { PAD_BUTTON_OFFSET_ANALOG_LEFT_Y, -1, VK_UP    },
    { PAD_BUTTON_OFFSET_ANALOG_LEFT_Y, +1, VK_DOWN  },
};
#define STICK_MAPPING_COUNT (sizeof(STICK_MAPPINGS) / sizeof(STICK_MAPPINGS[0]))
static bool prevStickState[sizeof(STICK_MAPPINGS) / sizeof(STICK_MAPPINGS[0])] = {0};

// ===[ MAIN ]===
static double freq = 0;
#define PS3_GET_TIME ((double)__builtin_ppc_get_timebase() / (double)freq)
bool shouldExit = false;

// ===[ MAIN ]===

static void sys_callback(uint64_t status, uint64_t param, void* userdata) {
    (void)param;
    (void)userdata;
    switch (status) {
        case SYSUTIL_EXIT_GAME:
            shouldExit = true;
            break;

        case SYSUTIL_MENU_OPEN:
        case SYSUTIL_MENU_CLOSE:
            break;

        default:
            break;
    }
}

static char* directoryOfPath(const char* path) {
    if (path == nullptr || path[0] == '\0') return safeStrdup("./");
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    if (backslash != nullptr && (slash == nullptr || backslash > slash)) slash = backslash;
    if (slash == nullptr) return safeStrdup("./");
    size_t len = (size_t) (slash - path + 1);
    char* result = (char*) safeMalloc(len + 1);
    memcpy(result, path, len);
    result[len] = '\0';
    return result;
}

static char* joinPath(const char* directory, const char* name) {
    size_t dirLen = strlen(directory);
    bool needsSlash = dirLen > 0 && directory[dirLen - 1] != '/' && directory[dirLen - 1] != '\\';
    size_t nameLen = strlen(name);
    char* result = (char*) safeMalloc(dirLen + (needsSlash ? 1 : 0) + nameLen + 1);
    memcpy(result, directory, dirLen);
    size_t pos = dirLen;
    if (needsSlash) result[pos++] = '/';
    memcpy(result + pos, name, nameLen + 1);
    return result;
}

static bool regularFileExists(const char* path) {
    struct stat st;
    return path != nullptr && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void ensureDirectory(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) mkdir(path, 0777);
}

static void wadLoadProgress(const char* chunkName, int chunkIndex, int totalChunks,
                            DataWin* dataWin, void* userData) {
    (void)dataWin;
    (void)userData;
    printf("WAD chunk %.4s (%d/%d)\n", chunkName, chunkIndex + 1, totalChunks);
    PS3SessionLog_event("WAD chunk %.4s (%d/%d)", chunkName, chunkIndex + 1, totalChunks);
}

int main(int argc, char* argv[]) {
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sys_callback, NULL);
    freq = sysGetTimebaseFrequency();

    // Public builds boot into the storage picker before allocating or parsing
    // any game-owned data. Private development builds may opt into the bundled
    // WAD fast path for repeatable RPCS3 tests.
    ps3glInit();
#ifdef DONUT_SPIDER_DEV_BUILD
    runSurfaceReadbackSelfTest();
#endif
    ioPadInit(7);
    PS3Overlay_init();

    const char* executablePath = argc > 0 && argv[0] != nullptr ? argv[0] : "./EBOOT.BIN";
    printf("%s %s executable: %s\n", DONUT_SPIDER_DISPLAY_NAME, DONUT_SPIDER_VERSION, executablePath);
    char* appDirectory = directoryOfPath(executablePath);
    PS3SessionLog_open(appDirectory);
    PS3SessionLog_event("SESSION START executable=%s", executablePath);
    char* bundledWadPath = joinPath(appDirectory, "data.win");
    char* dataWinPath = nullptr;
#ifdef DONUT_SPIDER_DEV_PRELOAD_WAD
    char* bundledTexturesPath = joinPath(appDirectory, "TEXTURES.BIN");
    char* bundledTexturesLowerPath = joinPath(appDirectory, "textures.bin");
    if (regularFileExists(bundledWadPath) &&
        (regularFileExists(bundledTexturesPath) || regularFileExists(bundledTexturesLowerPath))) {
        dataWinPath = safeStrdup(bundledWadPath);
        printf("Donut Spider: using private preloaded development WAD\n");
    }
    free(bundledTexturesPath);
    free(bundledTexturesLowerPath);
#endif
    if (dataWinPath == nullptr) dataWinPath = PS3WadPicker_select(bundledWadPath, &shouldExit);
    free(bundledWadPath);
    if (dataWinPath == nullptr) {
        PS3SessionLog_close(shouldExit ? "system exit in WAD picker" : "WAD picker cancelled");
        PS3Overlay_deinit();
        ioPadEnd();
        sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
        free(appDirectory);
        return 0;
    }

    printf("Loading %s...\n", dataWinPath);
    PS3SessionLog_event("WAD SELECTED path=%s", dataWinPath);

    DataWinParserOptions options = {0};
    options.parseGen8 = true;
    options.parseOptn = true;
    options.parseLang = true;
    options.parseExtn = true;
    options.parseSond = true;
    options.parseAgrp = true;
    options.parseSprt = true;
    options.parseBgnd = true;
    options.parsePath = true;
    options.parseScpt = true;
    options.parseGlob = true;
    options.parseShdr = true;
    options.parseFont = true;
    options.parseTmln = true;
    options.parseObjt = true;
    options.parseRoom = true;
    options.parseTpag = true;
    options.parseCode = true;
    options.parseVari = true;
    options.parseFunc = true;
    options.parseStrg = true;
    // TXTR pages live in TEXTURES.BIN on PS3, not in data.win.
    options.parseTxtr = false;
    options.parseAudo = true;
    options.skipLoadingPreciseMasksForNonPreciseSprites = true;
    options.lazyLoadRooms = true;
    options.lazyLoadAudio = true;
    options.streamChunks = true;
    options.progressCallback = wadLoadProgress;
    //options.eagerlyLoadedRooms = args.eagerRooms;

    DataWin* dataWin = DataWin_parse(dataWinPath, options);

    Gen8* gen8 = &dataWin->gen8;
    printf("Loaded \"%s\" (%d) successfully! [WAD Version %u / GameMaker version %u.%u.%u.%u]\n", gen8->name, gen8->gameID, gen8->wadVersion, dataWin->detectedFormat.major, dataWin->detectedFormat.minor, dataWin->detectedFormat.release, dataWin->detectedFormat.build);
    PS3SessionLog_event("WAD LOADED name=%s game_id=%d wad=%u gamemaker=%u.%u.%u.%u",
                        gen8->name, gen8->gameID, gen8->wadVersion,
                        dataWin->detectedFormat.major, dataWin->detectedFormat.minor,
                        dataWin->detectedFormat.release, dataWin->detectedFormat.build);

    if (!WAD_isVersionSupported(gen8->wadVersion)) {
        printf("Unsupported WAD version %u in this build. Rebuild with the matching ENABLE_WAD option.\n", gen8->wadVersion);
        PS3SessionLog_event("FATAL unsupported WAD version=%u", gen8->wadVersion);
        PS3SessionLog_close("unsupported WAD version");
        DataWin_free(dataWin);
        free(dataWinPath);
        free(appDirectory);
        PS3Overlay_deinit();
        ioPadEnd();
        sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
        return 1;
    }

    // Initialize VM
    VMContext* vm = VM_create(dataWin);

    Profiler_setEnabled(&vm->profiler, false);
#ifdef ENABLE_VM_OPCODE_PROFILER
    vm->opcodeProfilerEnabled = true;
    if (vm->opcodeProfilerEnabled) {
        vm->opcodeVariantCounts = (uint64_t *)safeCalloc(256 * 256, sizeof(uint64_t));
        vm->opcodeRValueTypeCounts = (uint64_t *)safeCalloc(256 * 256, sizeof(uint64_t));
    }
#endif

    // Keep saves in Donut Spider's own writable package directory instead of next
    // to a selected WAD (which may live on read-only optical/USB media).
    char* dataWinDir = directoryOfPath(dataWinPath);
    char* saveRoot = joinPath(appDirectory, "saves");
    ensureDirectory(saveRoot);
    char saveName[48];
    snprintf(saveName, sizeof(saveName), "%08X", (unsigned int) gen8->gameID);
    char* savePath = joinPath(saveRoot, saveName);
    ensureDirectory(savePath);
    OverlayFileSystem* overlayFs = OverlayFileSystem_create(dataWinDir, savePath);
    free(saveRoot);
    free(savePath);

    // Chapter bundles may keep shared music one directory above chapterN_windows.
    char* fallbackBundle = safeStrdup(dataWinDir);
    size_t fallbackLen = strlen(fallbackBundle);
    while (fallbackLen > 1 && fallbackBundle[fallbackLen - 1] == '/') fallbackBundle[--fallbackLen] = '\0';
    char* fallbackSlash = strrchr(fallbackBundle, '/');
    if (fallbackSlash != nullptr) {
        fallbackSlash[1] = '\0';
        OverlayFileSystem_setFallbackBundlePath(overlayFs, fallbackBundle);
    }
    free(fallbackBundle);

    // The official preprocessor emits uppercase TEXTURES.BIN. Accept the old
    // lowercase spelling as well for bundles made by earlier development builds.
    char* texturesBinPath = joinPath(dataWinDir, "TEXTURES.BIN");
    if (!regularFileExists(texturesBinPath)) {
        free(texturesBinPath);
        texturesBinPath = joinPath(dataWinDir, "textures.bin");
    }
    if (!PS3Textures_init(texturesBinPath)) {
        fprintf(stderr, "FATAL: failed to load %s\n", texturesBinPath);
        PS3SessionLog_event("FATAL texture bundle failed path=%s", texturesBinPath);
        PS3SessionLog_close("texture bundle failed");
        free(texturesBinPath);
        OverlayFileSystem_destroy(overlayFs);
        VM_free(vm);
        DataWin_free(dataWin);
        free(dataWinDir);
        free(dataWinPath);
        free(appDirectory);
        PS3Overlay_deinit();
        ioPadEnd();
        sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
        return 1;
    }
    free(texturesBinPath);

    // Initialize the renderer
    Renderer* renderer = GLLegacyRenderer_create();

    // Initialize the audio system
#ifdef USE_OPENAL
    AudioSystem* audioSystem = (AudioSystem*) AlAudioSystem_create();
#else
    AudioSystem* audioSystem = (AudioSystem*) NoopAudioSystem_create();
#endif

    // Initialize the paletted shader
    // The palette must ALWAYS be in TEXUNIT1!
    {
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderBinary(1, &fs, PS3GL_SHADER_BINARY_FPO, paletted_fpo, (GLsizei) paletted_fpo_len);
        gPalettedProgram = glCreateProgram();
        glAttachShader(gPalettedProgram, fs);
        glLinkProgram(gPalettedProgram);
        gPalettedUPaletteVLoc = glGetUniformLocation(gPalettedProgram, "uPaletteV");
        GLint uPaletteLoc = glGetUniformLocation(gPalettedProgram, "uPalette");
        glUseProgram(gPalettedProgram);
        glUniform1i(uPaletteLoc, 1);
        glUseProgram(0);
        printf("Paletted shader: program=%u uPaletteV=%d uPalette=%d\n", gPalettedProgram, gPalettedUPaletteVLoc, uPaletteLoc);
    }

    // Initialize the runner
    Runner* runner = Runner_create(dataWin, vm, renderer, (FileSystem*) overlayFs, audioSystem);
    runner->debugMode = false;
    //runner->osType = OS_PS3;

    // Initialize the first room and fire Game Start / Room Start events
    Runner_initFirstRoom(runner);
    PS3SessionLog_event("RUNNER START first_room=%s room_index=%d",
                        runner->currentRoom != nullptr && runner->currentRoom->name != nullptr ? runner->currentRoom->name : "?",
                        runner->currentRoomIndex);

    // Main loop
    bool debugPaused = false;
    bool debugShowCollisionMasks = false;
    double lastFrameStartTime = PS3_GET_TIME; // for delta_time and frame pacing
    double nextSessionHeartbeat = lastFrameStartTime;
    int32_t lastLoggedRoomIndex = -1;
    while (!shouldExit && !runner->shouldExit) {
        // Clear last frame's pressed/released state, then poll new input events
        RunnerKeyboard_beginFrame(runner->keyboard);
        RunnerGamepad_beginFrame(runner->gamepads);


        // Run the game step if the game is paused
        bool shouldStep = true;
        if (runner->debugMode && debugPaused) {
            shouldStep = RunnerKeyboard_checkPressed(runner->keyboard, 'O');
            if (shouldStep) fprintf(stderr, "Debug: Frame advance (frame %d)\n", runner->frameCount);
        }


        padInfo padinfo;
        ioPadGetInfo(&padinfo);

        if (padinfo.status[0])
        {
            padData paddata;
            ioPadGetData(0, &paddata);

            // "The padData structure is only filled if there is a change in input since the last call.
            // If there is no change, the structure is zero-filled. If the len member is zero, there was no new input."
            // So we'll check if there WAS a change before trying to process the keys, to avoid releasing the keys on every frame.
            // -ioPadGetData
            if (paddata.len > 0) {
                repeat(PAD_MAPPING_COUNT, i) {
                    uint8_t byte = (uint8_t) paddata.button[PAD_MAPPINGS[i].digital];
                    uint8_t mask = PAD_MAPPINGS[i].mask;
                    int32_t gmlKey = PAD_MAPPINGS[i].gmlKey;

                    bool isPressed = (byte & mask) != 0;
                    bool wasPressed = prevState[i];

                    if (isPressed && !wasPressed) {
                        RunnerKeyboard_onKeyDown(runner->keyboard, gmlKey);
                    } else if (!isPressed && wasPressed) {
                        RunnerKeyboard_onKeyUp(runner->keyboard, gmlKey);
                    }

                    prevState[i] = isPressed;
                }

                repeat(STICK_MAPPING_COUNT, i) {
                    int axisValue = (int) paddata.button[STICK_MAPPINGS[i].axis];
                    int signedDelta = STICK_MAPPINGS[i].sign * (axisValue - STICK_CENTER);

                    bool isPressed = signedDelta > STICK_THRESHOLD;
                    bool wasPressed = prevStickState[i];
                    int32_t gmlKey = STICK_MAPPINGS[i].gmlKey;

                    if (isPressed && !wasPressed) {
                        RunnerKeyboard_onKeyDown(runner->keyboard, gmlKey);
                    } else if (!isPressed && wasPressed) {
                        RunnerKeyboard_onKeyUp(runner->keyboard, gmlKey);
                    }

                    prevStickState[i] = isPressed;
                }
            }
        }

#ifdef DONUT_SPIDER_DEV_BUILD
        if (RunnerKeyboard_checkPressed(runner->keyboard, VK_F12)) {
            PS3Overlay_toggleDebugOverlay(runner);
        }
#endif

        double frameStartTime = PS3_GET_TIME;
        runner->deltaTime = (frameStartTime - lastFrameStartTime) * 1000000.0;
        lastFrameStartTime = frameStartTime;

#ifdef DONUT_SPIDER_DEV_BUILD
        double stepTime = 0.0;
        double audioTime = 0.0;
#endif
        if (shouldStep) {
            // Run one game step (Begin Step, Keyboard, Alarms, Step, End Step, room transitions)
#ifdef DONUT_SPIDER_DEV_BUILD
            double stepStart = PS3_GET_TIME;
#endif
            Runner_step(runner);
#ifdef DONUT_SPIDER_DEV_BUILD
            stepTime = PS3_GET_TIME - stepStart;
#endif

            // Update audio system (gain fading, cleanup ended sounds)
            float dt = (float) (runner->deltaTime / 1000000.0);
            if (0.0f > dt) dt = 0.0f;
            if (dt > 0.1f) dt = 0.1f; // cap delta to avoid huge fades on lag spikes
#ifdef DONUT_SPIDER_DEV_BUILD
            double audioStart = PS3_GET_TIME;
#endif
            runner->audioSystem->vtable->update(runner->audioSystem, dt);
#ifdef DONUT_SPIDER_DEV_BUILD
            audioTime = PS3_GET_TIME - audioStart;
#endif
        }

        // Query actual framebuffer size (differs from window size on Wayland with fractional scaling)
        int fbWidth = display_width, fbHeight = display_height;

        // Clear the default framebuffer (window background) to black
        glClear(GL_COLOR_BUFFER_BIT);

        int32_t gameW = (int32_t) gen8->defaultWindowWidth;
        int32_t gameH = (int32_t) gen8->defaultWindowHeight;

        Runner_drawPre(runner, fbWidth, fbHeight);

        Runner_beginFrame(runner, gameW, gameH, fbWidth, fbHeight, fbWidth, fbHeight);

#ifdef DONUT_SPIDER_DEV_BUILD
        double drawStart = PS3_GET_TIME;
#endif
        Runner_drawViews(runner, gameW, gameH, debugShowCollisionMasks);
        renderer->vtable->endFrameInit(renderer);
        Runner_drawPost(runner, fbWidth, fbHeight);
        renderer->vtable->endFrameEnd(renderer);
        Runner_drawGUI(runner, fbWidth, fbHeight, gameW, gameH);
#ifdef DONUT_SPIDER_DEV_BUILD
        double drawTime = PS3_GET_TIME - drawStart;

        // ===[ Debug Overlay ]===
        double tickTime = PS3_GET_TIME - frameStartTime;
        PS3Overlay_drawDebugOverlay(runner, (float) (tickTime * 1000.0), (float) (stepTime * 1000.0), (float) (drawTime * 1000.0), (float) (audioTime * 1000.0), fbWidth, fbHeight);
#endif

        sysUtilCheckCallback();
        // Only swap when there isn't a room change to match the original runner.
        if (runner->pendingRoom == -1) {
            ps3glSwapBuffers();
        }
        Runner_handlePendingRoomChange(runner);

        if (runner->currentRoomIndex != lastLoggedRoomIndex) {
            lastLoggedRoomIndex = runner->currentRoomIndex;
            PS3SessionLog_event("ROOM ENTER name=%s room_index=%d frame=%d",
                                runner->currentRoom != nullptr && runner->currentRoom->name != nullptr ? runner->currentRoom->name : "?",
                                runner->currentRoomIndex, runner->frameCount);
        }
        const double sessionNow = PS3_GET_TIME;
        if (sessionNow >= nextSessionHeartbeat) {
            const GLLegacyRenderer* gl = (const GLLegacyRenderer*)renderer;
            size_t surfaceBytes = 0;
            for (uint32_t i = 0; i < gl->surfaceCount; i++) {
                if (gl->surfaces[i] == 0) continue;
                surfaceBytes += (size_t)gl->surfaceWidth[i] * (size_t)gl->surfaceHeight[i] * 4u;
            }
            const struct mallinfo heap = mallinfo();
            PS3SessionLog_heartbeat(
                runner->currentRoom != nullptr ? runner->currentRoom->name : "?",
                runner->currentRoomIndex, runner->frameCount,
                (int32_t)arrlen(runner->instances), (int32_t)arrlen(runner->structInstances),
                (size_t)heap.uordblks, gl->textureBytesResident, surfaceBytes);
            nextSessionHeartbeat = sessionNow + 10.0;
        }

        // Limit frame rate to room speed
        if (runner->currentRoom->speed > 0) {
            double targetFrameTime = 1.0 / runner->currentRoom->speed;
            double nextFrameTime = lastFrameStartTime + targetFrameTime;
            while (PS3_GET_TIME < nextFrameTime) {
                __sync();
                sysUtilCheckCallback();
                sysUsleep(5);
            }
        }
    }


    // Runner teardown may release video surfaces and audio streams, so both
    // backends must remain alive until it has finished.
    Runner_free(runner);
    audioSystem->vtable->destroy(audioSystem);
    renderer->vtable->destroy(renderer);
    OverlayFileSystem_destroy(overlayFs);
#ifdef ENABLE_VM_OPCODE_PROFILER
    VM_printOpcodeProfilerReport(vm);
#endif
    VM_free(vm);
    DataWin_free(dataWin);
    PS3Textures_free();
    PS3SessionLog_close(shouldExit ? "system exit" : "game exit");
    PS3Overlay_deinit();
    ioPadEnd();
    free(dataWinDir);
    free(dataWinPath);
    free(appDirectory);

    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    gcmSetWaitFlip(context);
    rsxFinish(context,1);
    printf("Bye! :3\n");
    return 0;
}
