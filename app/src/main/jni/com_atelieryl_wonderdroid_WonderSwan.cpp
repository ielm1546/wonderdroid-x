//
// Created by Willie Chang on 9/18/21.
//

#include "mednafen/mednafen.h"
#include "mednafen/state-driver.h"
#include "mednafen/mednafen-driver.h"
#include "mednafen/wswan/net_link.h"

#include "com_atelieryl_wonderdroid_WonderSwan.h"

#define setBit(dest, bit, idx) (dest ^ ((-bit ^ dest) & (1 << idx)));

using namespace Mednafen;

const int GAME_INFO_ARRAY_SIZE = 9;
const int FRAME_INFO_ARRAY_SIZE = 6;
const int SOUND_BUF_SIZE = 4096;
const double SAMPLE_RATE = 24000;

bool _initialized = false;
bool _runGame = false;

static MDFNGI *_game;
static MDFN_Surface *_surf;

uint32_t *_inputBuffer[13];

double _masterClock;

extern "C" {

    JNIEXPORT jboolean JNICALL
    Java_com_atelieryl_wonderdroid_WonderSwan_startHost(JNIEnv *env, jclass obj, jint port) {
        if (port <= 0 || port > 65535) {
            return JNI_FALSE;
        }
        return NetLink_StartHost((int)port) ? JNI_TRUE : JNI_FALSE;
    }

    JNIEXPORT jboolean JNICALL
    Java_com_atelieryl_wonderdroid_WonderSwan_connectToHost(JNIEnv *env, jclass obj, jstring ip, jint port) {
        if (!ip || port <= 0 || port > 65535) {
            return JNI_FALSE;
        }
        const char *ip_chars = env->GetStringUTFChars(ip, NULL);
        if (!ip_chars) {
            return JNI_FALSE;
        }
        bool ok = NetLink_ConnectToHost(ip_chars, (int)port);
        env->ReleaseStringUTFChars(ip, ip_chars);
        return ok ? JNI_TRUE : JNI_FALSE;
    }

    JNIEXPORT void JNICALL
    Java_com_atelieryl_wonderdroid_WonderSwan_disconnect(JNIEnv *env, jclass obj) {
        NetLink_Disconnect();
    }

    JNIEXPORT jboolean JNICALL
    Java_com_atelieryl_wonderdroid_WonderSwan_isNetConnected(JNIEnv *env, jclass obj) {
        return NetLink_IsConnected() ? JNI_TRUE : JNI_FALSE;
    }

    JNIEXPORT void JNICALL Java_com_atelieryl_wonderdroid_WonderSwan_exit(JNIEnv *env, jclass obj) {
        _runGame = false;
        NetLink_Disconnect();
        MDFNI_CloseGame();
        delete _surf;
        _surf = NULL;
        _game = NULL;
    }

    JNIEXPORT void JNICALL Java_com_atelieryl_wonderdroid_WonderSwan_reset(JNIEnv *env, jclass obj) {
        MDFNI_Reset();
    }

    JNIEXPORT jlongArray JNICALL
    Java_com_atelieryl_wonderdroid_WonderSwan_load(JNIEnv *env, jclass obj, jstring rom_path,
                                                   jstring dir_path, jstring name, jint year, jint month, jint day,
                                                   jstring blood, jstring sex, jstring language) {
        const char *rom_path_c = rom_path ? env->GetStringUTFChars(rom_path, NULL) : NULL;
        const char *dir_path_c = dir_path ? env->GetStringUTFChars(dir_path, NULL) : NULL;
        const char *name_c = name ? env->GetStringUTFChars(name, NULL) : NULL;
        const char *blood_c = blood ? env->GetStringUTFChars(blood, NULL) : NULL;
        const char *sex_c = sex ? env->GetStringUTFChars(sex, NULL) : NULL;
        const char *language_c = language ? env->GetStringUTFChars(language, NULL) : NULL;

        // Initialize Mednafen
        if (!_initialized) {
            MDFNI_Init();
            MDFNI_InitFinalize(dir_path_c ? dir_path_c : "");
            MDFNI_SetSetting("filesys.path_sav", dir_path_c ? dir_path_c : "");
            //MDFNI_SetSetting("filesys.path_state", dir_path_c ? dir_path_c : "");
            MDFNI_SetSetting("filesys.fname_sav", "%f%e.%x");
            MDFNI_SetSetting("filesys.fname_state", "%f%e.%X");
            MDFNI_SetSetting("wswan.name", name_c ? name_c : "");
            MDFNI_SetSetting("wswan.byear", std::to_string(year).c_str());
            MDFNI_SetSetting("wswan.bmonth", std::to_string(month).c_str());
            MDFNI_SetSetting("wswan.bday", std::to_string(day).c_str());
            MDFNI_SetSetting("wswan.blood", blood_c ? blood_c : "");
            MDFNI_SetSetting("wswan.sex", sex_c ? sex_c : "");
            MDFNI_SetSetting("wswan.language", language_c ? language_c : "");
            MDFNI_SetSetting("ngp.language", language_c ? language_c : "");
            _initialized = true;
        }

        // Load ROM
        _game = MDFNI_LoadGame(NULL, &::Mednafen::NVFS, rom_path_c ? rom_path_c : "");

        if (rom_path && rom_path_c) env->ReleaseStringUTFChars(rom_path, rom_path_c);
        if (dir_path && dir_path_c) env->ReleaseStringUTFChars(dir_path, dir_path_c);
        if (name && name_c) env->ReleaseStringUTFChars(name, name_c);
        if (blood && blood_c) env->ReleaseStringUTFChars(blood, blood_c);
        if (sex && sex_c) env->ReleaseStringUTFChars(sex, sex_c);
        if (language && language_c) env->ReleaseStringUTFChars(language, language_c);

        if (!_game) return NULL;

        // Prevent Master System games from running
        if (strcmp(_game->shortname, "sms") == 0) {
            MDFNI_CloseGame();
            _game = NULL;
            return env->NewLongArray(GAME_INFO_ARRAY_SIZE);
        }

        // Set up input
        for (unsigned i = 0; i < 13; i++)
            _inputBuffer[i] = (uint32_t *) calloc(9, sizeof(uint32_t));
        _game->SetInput(0, "gamepad", (uint8_t *)_inputBuffer[0]);

        // Set up surface -- might move to execute_frame
        MDFN_PixelFormat pix_fmt(MDFN_COLORSPACE_RGB, 4, 0, 8, 16, 24);
        _surf = new MDFN_Surface(NULL, _game->fb_width, _game->fb_height, _game->fb_width, pix_fmt);

        _runGame = true;

        jlong gameInfo[GAME_INFO_ARRAY_SIZE];
        gameInfo[0] = _game->fps;
        gameInfo[1] = _game->nominal_width;
        gameInfo[2] = _game->nominal_height;
        gameInfo[3] = _game->fb_width;
        gameInfo[4] = _game->fb_height;
        gameInfo[5] = _game->soundchan;
        gameInfo[6] = _game->rotated;
        gameInfo[7] = _game->shortname[0];
        gameInfo[8] = _game->MasterClock >> 32;

        jlongArray gameInfoArray = env->NewLongArray(GAME_INFO_ARRAY_SIZE);
        env->SetLongArrayRegion(gameInfoArray, 0, GAME_INFO_ARRAY_SIZE, gameInfo);
        return gameInfoArray;
    }

    JNIEXPORT jintArray JNICALL
    Java_com_atelieryl_wonderdroid_WonderSwan__1execute_1frame(JNIEnv *env, jclass obj, jboolean skip,
                                                               jboolean audio, jobject framebuffer,
                                                               jshortArray audiobuffer) {
        if (!_runGame || !_game || !_surf) return NULL;

        // Mednafen emulate
        static int16_t sound_buf[SOUND_BUF_SIZE];
        int32 rects[_game->fb_height];

        memset(rects, 0, _game->fb_height*sizeof(int32));
        rects[0] = ~0;
        EmulateSpecStruct spec;
        spec.surface = _surf;
        spec.SoundRate = SAMPLE_RATE;
        spec.SoundBuf = sound_buf;
        spec.LineWidths = rects;
        spec.SoundBufMaxSize = sizeof(sound_buf) / 2;
        spec.SoundVolume = 1.0;
        spec.soundmultiplier = 1.0;

        MDFNI_Emulate(&spec);

        // Copy framebuffer
        uint32_t* fb = (uint32_t*) env->GetDirectBufferAddress(framebuffer);
        if (fb && _surf->pixels) {
            memcpy(fb, _surf->pixels, _game->fb_width * _game->fb_height * sizeof(uint32_t));
        }

        // Audio
        if (audiobuffer) {
            env->SetShortArrayRegion(audiobuffer, 0, spec.SoundBufSize * _game->soundchan, sound_buf);
        }

        jint frameInfo[FRAME_INFO_ARRAY_SIZE];
        frameInfo[0] = spec.SoundBufSize;
        frameInfo[1] = spec.DisplayRect.x;
        frameInfo[2] = spec.DisplayRect.y;
        if (_game->multires || !spec.DisplayRect.w) {
            frameInfo[3] = spec.LineWidths[spec.DisplayRect.y];
        } else {
            frameInfo[3] = spec.DisplayRect.w;
        }
        frameInfo[4] = spec.DisplayRect.h;
        frameInfo[5] = spec.MasterCycles;

        jintArray frameInfoArray = env->NewIntArray(FRAME_INFO_ARRAY_SIZE);
        env->SetIntArrayRegion(frameInfoArray, 0, FRAME_INFO_ARRAY_SIZE, frameInfo);
        return frameInfoArray;
    }

    JNIEXPORT void JNICALL
    Java_com_atelieryl_wonderdroid_WonderSwan_updatebuttons(JNIEnv *env, jclass obj, jboolean y1,
                                                            jboolean y2, jboolean y3, jboolean y4,
                                                            jboolean x1, jboolean x2, jboolean x3,
                                                            jboolean x4, jboolean a, jboolean b,
                                                            jboolean start, jboolean select) {
        if (!_game || !_inputBuffer[0]) return;

        if (_game->shortname[0] == 'w') {
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x1, 0);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x2, 1);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x3, 2);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x4, 3);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], y1, 4);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], y2, 5);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], y3, 6);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], y4, 7);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], a, 9);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], b, 10);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], start, 8);
        } else if (_game->shortname[0] == 'g') {
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x1, 0);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x2, 3);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x3, 1);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x4, 2);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], a, 4);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], b, 5);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], start, 6);
        } else if (_game->shortname[0] == 'n') {
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x1, 0);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x2, 3);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x3, 1);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x4, 2);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], a, 5);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], b, 4);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], start, 6);
        } else if (_game->shortname[0] == 'p') {
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x1, 4);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x2, 5);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x3, 6);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], x4, 7);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], a, 0);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], b, 1);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], start, 3);
            *_inputBuffer[0] = setBit(*_inputBuffer[0], select, 2);
        }
    }

    JNIEXPORT void JNICALL
    Java_com_atelieryl_wonderdroid_WonderSwan_loadbackup(JNIEnv *env, jclass obj, jstring filename) {}

    JNIEXPORT void JNICALL
    Java_com_atelieryl_wonderdroid_WonderSwan_savebackup(JNIEnv *env, jclass obj, jstring filename) {}

    JNIEXPORT void JNICALL
    Java_com_atelieryl_wonderdroid_WonderSwan_loadstate(JNIEnv *env, jclass obj, jstring filename) {
        if (!filename) return;
        const char *filename_c = env->GetStringUTFChars(filename, NULL);
        if (!filename_c) return;
        _runGame = false;
        MDFNI_LoadState(filename_c, "");
        env->ReleaseStringUTFChars(filename, filename_c);
        _runGame = true;
    }

    JNIEXPORT void JNICALL
    Java_com_atelieryl_wonderdroid_WonderSwan_savestate(JNIEnv *env, jclass obj, jstring filename) {
        if (!filename) return;
        const char *filename_c = env->GetStringUTFChars(filename, NULL);
        if (!filename_c) return;
        _runGame = false;
        MDFNI_SaveState(filename_c, "", NULL, NULL, NULL);
        env->ReleaseStringUTFChars(filename, filename_c);
        _runGame = true;
    }

}
