//
// Created by 唐宇 on 2018/5/20.
//

#include "opengl_render_controller.h"
#include "cube_texture_render.h"
#include "pbo_render.h"

#define LOG_TAG "PicPreviewController"


void *OpenGlRenderController::threadStartCallback(void *myself) {
    OpenGlRenderController *controller = (OpenGlRenderController *) myself;
    controller->renderLoop();
//    pthread_exit(0);
    return NULL;
}

void OpenGlRenderController::renderLoop() {
    bool renderingEnabled = true;
    LOGI("renderLoop()");
    while (renderingEnabled) {
        pthread_mutex_lock(&mLock);
        /*process incoming messages*/
        switch (_msg) {
            case MSG_WINDOW_SET:
                initializeEnv();
                break;
            case MSG_RENDER_LOOP_EXIT:
                renderingEnabled = false;
                destroy();
                break;
            case MSG_RENDER_CHANGE:
                initializeRenderObj();
                break;
            default:
                break;
        }
        _msg = MSG_NONE;
        if (eglCore) {
            eglCore->makeCurrent(previewSurface);
            this->drawFrame();
            if (!render->isRenderContinus) {
                pthread_cond_wait(&mCondition, &mLock);
                usleep(16 * 1000);
            }
        }

        pthread_mutex_unlock(&mLock);
    }
    LOGI("Render loop exits");

    return;
}

bool OpenGlRenderController::initializeEnv() {
    if (!eglCore) {
        eglCore = new EGLCore();
        eglCore->init();
    }
    if (previewSurface != EGL_NO_SURFACE) {
        eglCore->destorySurface(previewSurface);
        previewSurface = EGL_NO_SURFACE;
    }
    previewSurface = eglCore->createWindowSurface(_window);
    eglCore->makeCurrent(previewSurface);
    if (!isRendererInitialized) {
        isRendererInitialized = render->init(screenWidth, screenHeight);
    }
    if (!isRendererInitialized) {
        LOGI("Renderer failed on initialized...");
        return false;
    }
    LOGI("eglinit %d", eglInit);
    if (!eglInit || _msg == MSG_WINDOW_SET) {
        initializeRenderObj();
    }

    eglInit = true;
    LOGI("Initializing context Success");
    return true;
}


void OpenGlRenderController::drawFrame() {
    render->render();
    if (!eglCore->swapBuffers(previewSurface)) {
        LOGE("eglSwapBuffers() returned error %d", eglGetError());
    }
    fps();

}

void OpenGlRenderController::destroy() {
    LOGI("dealloc renderer ...");
    if (NULL != render) {
        render->dealloc();
        delete render;
        render = NULL;
    }
    if (eglCore) {
        eglCore->releaseSurface(previewSurface);
        eglCore->release();
        eglCore = NULL;
    }
    if (jObj) {
        int status;
        JNIEnv *env;
        bool isAttached = false;
        status = g_jvm->GetEnv((void **) &env, JNI_VERSION_1_6);
        if (status < 0) {
            g_jvm->AttachCurrentThread(&env, NULL);//将当前线程注册到虚拟机中．
            isAttached = true;
        }
        env->DeleteGlobalRef(jObj);
        if (isAttached)
            g_jvm->DetachCurrentThread();
    }
}


OpenGlRenderController::~OpenGlRenderController() {
    LOGI("VideoDutePlayerController instance destroyed");
    pthread_mutex_destroy(&mLock);
    pthread_cond_destroy(&mCondition);
}

bool OpenGlRenderController::start() {
    LOGI("Creating VideoDutePlayerController thread");
    pthread_create(&_threadId, NULL, threadStartCallback, this);
    return true;
}

void OpenGlRenderController::stop() {
    LOGI("Stopping VideoDutePlayerController Render thread");
    /*send message to render thread to stop rendering*/
    pthread_mutex_lock(&mLock);
    _msg = MSG_RENDER_LOOP_EXIT;
    pthread_cond_signal(&mCondition);
    pthread_mutex_unlock(&mLock);
    LOGI("we will join render thread stop");
    pthread_join(_threadId, NULL);
    LOGI("VideoDutePlayerController Render thread stopped");
}

void OpenGlRenderController::setWindow(ANativeWindow *window) {
    pthread_mutex_lock(&mLock);
    _msg = MSG_WINDOW_SET;
    _window = window;
    pthread_cond_signal(&mCondition);
    pthread_mutex_unlock(&mLock);
}

void OpenGlRenderController::resetSize(jint width, jint height, ANativeWindow *pWindow) {
    LOGI("VideoDutePlayerController::resetSize width:%d; height:%d", width, height);
    pthread_mutex_lock(&mLock);
    this->screenWidth = width;
    this->screenHeight = height;
    _window = pWindow;
    _msg = MSG_WINDOW_SET;
    render->resetRenderSize(0, 0, width, height);
    pthread_cond_signal(&mCondition);
    pthread_mutex_unlock(&mLock);
}

OpenGlRenderController::OpenGlRenderController(JNIEnv *env, jobject assetManager
) {
    LOGI("VideoDutePlayerController instance created");
    pthread_mutex_init(&mLock, NULL);
    pthread_cond_init(&mCondition, NULL);
    screenWidth = 720;
    screenHeight = 720;
    g_jvm = NULL;
    env->GetJavaVM(&g_jvm);
    render = new ShapeRener("shape/vertex_shader.glsl", "shape/fragment_shader.glsl",
                            env->NewGlobalRef(assetManager), g_jvm);
}

void OpenGlRenderController::initializeRenderObj() {
    render->initRenderObj();
}

OpenGlRenderController::OpenGlRenderController(JNIEnv *env, jobject assetManager, jobject saber) {
    LOGI("VideoDutePlayerController instance saber");
    pthread_mutex_init(&mLock, NULL);
    pthread_cond_init(&mCondition, NULL);
    screenWidth = 720;
    screenHeight = 720;
    g_jvm = NULL;
    env->GetJavaVM(&g_jvm);
    render = new CubeTextureRender("texture/vertex_shader.glsl", "texture/fragment_shader.glsl",
                                   env->NewGlobalRef(assetManager), g_jvm,
                                   env->NewGlobalRef(saber));
}

void OpenGlRenderController::rotate(jfloat x, jfloat y, jfloat degree) {
    pthread_mutex_lock(&mLock);
    CubeTextureRender *render = dynamic_cast<CubeTextureRender *>(this->render);
    render->rotate(x, y, degree);
    pthread_cond_signal(&mCondition);
    pthread_mutex_unlock(&mLock);
}

void OpenGlRenderController::scale(jfloat scale) {
    pthread_mutex_lock(&mLock);
    CubeTextureRender *render = dynamic_cast<CubeTextureRender *>(this->render);
    render->setScale(scale);
    pthread_cond_signal(&mCondition);
    pthread_mutex_unlock(&mLock);
}


int OpenGlRenderController::fps() {
    if (!render->caculateFps) return 0;
    static int fps = 0;
    static long long lastTime = getCurrentTime(); // ms
    static int frameCount = 0;

    ++frameCount;

    long long curTime = getCurrentTime();
    if (curTime - lastTime > 1000) // 取固定时间间隔为1秒
    {
        fps = frameCount;
        frameCount = 0;
        lastTime = curTime;
        int status;
        JNIEnv *env;
        bool isAttached = false;
        status = g_jvm->GetEnv((void **) &env, JNI_VERSION_1_6);
        if (status < 0) {
            g_jvm->AttachCurrentThread(&env, NULL);//将当前线程注册到虚拟机中．
            isAttached = true;
        }
        jclass clazz = env->GetObjectClass(jObj);
        jmethodID methodId = env->GetMethodID(clazz, "onFrame", "(I)V");
        env->CallVoidMethod(jObj, methodId, fps);
        if (isAttached)
            g_jvm->DetachCurrentThread();

    }
    return fps;
}

OpenGlRenderController::OpenGlRenderController(JNIEnv *env, jobject thiz, jobject assetManager,
                                               jobjectArray bitmapArray) {
    LOGI("VideoDutePlayerController instance saber");
    pthread_mutex_init(&mLock, NULL);
    pthread_cond_init(&mCondition, NULL);
    screenWidth = 720;
    screenHeight = 720;
    g_jvm = NULL;
    this->jObj = env->NewGlobalRef(thiz);
    env->GetJavaVM(&g_jvm);
    jsize length = env->GetArrayLength(bitmapArray);
    jobject *bitmaps = new jobject[length];
    for (size_t i = 0; i < length; i++) {
        bitmaps[i] = env->NewGlobalRef(env->GetObjectArrayElement(bitmapArray, i));
    }
    render = new PboRender("simplePBO/vertex_shader.glsl", "simplePBO/fragment_shader.glsl",
                           env->NewGlobalRef(assetManager), g_jvm, bitmaps,
                           length);

}


