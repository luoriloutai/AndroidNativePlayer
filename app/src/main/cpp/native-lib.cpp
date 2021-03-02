#include <jni.h>
#include <string>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

extern "C" {
#include "include/ffmpeg/libavcodec/avcodec.h"
#include "include/ffmpeg/libavformat/avformat.h"
#include "include/ffmpeg/libswscale/swscale.h"
#include "include/ffmpeg/libavutil/imgutils.h"

}

#define TAG "my_out"
#define  LOGD(...) __android_log_print(ANDROID_LOG_DEBUG,TAG,__VA_ARGS__)
#define  LOGE(...) __android_log_print(ANDROID_LOG_ERROR,TAG,__VA_ARGS__)


extern "C"
JNIEXPORT jint JNICALL
Java_com_bug_nativeplayer_NativePlayer_nativeWindowPlayVideo(JNIEnv *env, jclass clazz, jstring url, jobject surface) {

    const char *file_name = env->GetStringUTFChars(url, nullptr);
    AVFormatContext *pFormatCtx = avformat_alloc_context();

    if (avformat_open_input(&pFormatCtx, file_name, nullptr, nullptr) != 0) {

        LOGE("Couldn't open file:%s\n", file_name);
        return -1;
    }

    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        LOGE("Couldn't find stream information.");
        return -1;
    }


    int videoStream = -1, i;
    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
            && videoStream < 0) {
            videoStream = i;
        }
    }
    if (videoStream == -1) {
        LOGE("Didn't find a video stream.");
        return -1;
    }

    AVCodecParameters *pCodecParameters = pFormatCtx->streams[videoStream]->codecpar;

    AVCodec *pCodec = avcodec_find_decoder(pCodecParameters->codec_id);
    if (pCodec == nullptr) {
        LOGE("Codec not found decoder.");
        return -1;
    }
    AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
    // 为 AVCodecContext 设置参数值，不调用的话该对象将获取不到各种参数
    if (avcodec_parameters_to_context(pCodecCtx, pCodecParameters) < 0) {
        LOGE("avcodec_parameters_to_context error.");
        return -1;
    }
    if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
        LOGE("Could not open codec.");
        return -1;
    }


    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);

    // 获取视频宽高
    int videoWidth = pCodecCtx->width;  // 上面调用了 avcodec_parameters_to_context 后 pCodecCtx->width 才有值
    int videoHeight = pCodecCtx->height;

    // 设置native window的buffer大小,可自动拉伸
    ANativeWindow_setBuffersGeometry(nativeWindow, videoWidth, videoHeight, WINDOW_FORMAT_RGBA_8888);
    ANativeWindow_Buffer windowBuffer;

    // 分配一个用于解码的帧
    AVFrame *pFrame = av_frame_alloc();

    // 分配一个用于存储转换格式后的帧，最终渲染到屏幕
    AVFrame *pFrameRGBA = av_frame_alloc();
    if (pFrameRGBA == nullptr || pFrame == nullptr) {
        LOGE("Could not allocate video frame.");
        return -1;
    }


    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, pCodecCtx->width, pCodecCtx->height, 1);
    //uint8_t *buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    auto *buffer = (uint8_t *) av_malloc(numBytes);  // malloc() 也行
    // 用buffer初始化渲染帧空间
    av_image_fill_arrays(pFrameRGBA->data, pFrameRGBA->linesize, buffer, AV_PIX_FMT_RGBA,
                         pCodecCtx->width, pCodecCtx->height, 1);

    // 由于解码出来的帧格式不是RGBA的,在渲染之前需要进行格式转换
    struct SwsContext *sws_ctx = sws_getContext(pCodecCtx->width,
                                                pCodecCtx->height,
                                                pCodecCtx->pix_fmt,
                                                pCodecCtx->width,
                                                pCodecCtx->height,
                                                AV_PIX_FMT_RGBA,
                                                SWS_FAST_BILINEAR,
                                                nullptr,
                                                nullptr,
                                                nullptr);


    AVPacket packet;

    while (av_read_frame(pFormatCtx, &packet) >= 0) {

        if (packet.stream_index == videoStream) {

            int sendRet = avcodec_send_packet(pCodecCtx, &packet);
            if (sendRet != 0) {
                if (sendRet == AVERROR_EOF) {
                    LOGD("Stream end.");
                    return 0;
                }
                LOGE("send_packet error.");
                return -1;
            }

            int receiveRet = avcodec_receive_frame(pCodecCtx, pFrame);
            if (receiveRet != 0) {
                if (receiveRet == AVERROR_EOF) { // 流结束
                    return 0;
                }
            }

            // 解码完一帧。如果返回值不为0且不为AVERROR_EOF则需进行下一次读取
            // 也就是说一帧并不一定一次就能解完，一个包内可能含有多个帧
            if (receiveRet == 0) {

                /* ===========================
                 * 使用 ANativeWindow 渲染视频
                 * ===========================*/
                ANativeWindow_lock(nativeWindow, &windowBuffer, nullptr);

                // ffmpeg解码完是YUV格式，需要转换成RGBA
                sws_scale(sws_ctx, pFrame->data,
                          pFrame->linesize, 0, pCodecCtx->height,
                          pFrameRGBA->data, pFrameRGBA->linesize);


//                // 做试验，创建新空间存储转换后的帧数据
//                // 结果可行
//                uint8_t * tmp = (uint8_t*)malloc(numBytes);
//                memcpy(tmp,pFrameRGBA->data[0],numBytes);
//                // 获取stride
//                uint8_t *dst = (uint8_t *) windowBuffer.bits;
//                int dstStride = windowBuffer.stride * 4; // 4是指4个字节，RGBA每一个分量一个字节
//                int srcStride = pFrameRGBA->linesize[0];
//
//                // 由于window的stride和帧的stride不同,因此需要逐行复制
//                int h;
//                for (h = 0; h < videoHeight; h++) {
//                    memcpy(dst + h * dstStride, tmp + h * srcStride, srcStride);
//                }
//                ANativeWindow_unlockAndPost(nativeWindow);
//                free(tmp);



                // 获取stride
                uint8_t *dst = (uint8_t *) windowBuffer.bits;
                int dstStride = windowBuffer.stride * 4; // 4是指4个字节，RGBA每一个分量一个字节
                uint8_t *src = (uint8_t *) (pFrameRGBA->data[0]);
                int srcStride = pFrameRGBA->linesize[0];

                // 由于window的stride和帧的stride不同,因此需要逐行复制
                int h;
                for (h = 0; h < videoHeight; h++) {
                    memcpy(dst + h * dstStride, src + h * srcStride, srcStride);
                }

                ANativeWindow_unlockAndPost(nativeWindow);

            }

        }


        av_packet_unref(&packet);
    }


    av_free(buffer);
    av_free(pFrameRGBA);
    av_free(pFrame);

    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);

    return 0;
}


extern "C"
JNIEXPORT jint JNICALL
Java_com_bug_nativeplayer_NativePlayer_openGlPlayVideo(JNIEnv *env, jclass clazz, jstring url, jobject surface) {

    /* ========
     * EGL 相关
     * ========*/
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        LOGE("create display failed:%d", eglGetError());
        return -1;
    }
    if (!eglInitialize(display, nullptr, nullptr)) {
        LOGE("initialize display failed:%d", eglGetError());
        return -1;
    }
    const EGLint configAttribs[] = {EGL_RED_SIZE, 8,
                                    EGL_GREEN_SIZE, 8,
                                    EGL_BLUE_SIZE, 8,
                                    EGL_ALPHA_SIZE, 8,
                                    EGL_BUFFER_SIZE, 32,
                                    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                    EGL_NONE}; // 必须以EGL_NONE结尾

    EGLint numConfigs = 0;
    EGLConfig config;

    // 选择符合配置选项的config
    // config_size:选择符合条件的多少个config输出到传入的参数Config中
    // numConfigs:符合条件的配置有多少个，输出到numConfigs
    // 第三个参数可以不传，这时numConfigs的值就是找到的符合条件的eglconfig的个数，
    // 然后，再将numConfigs传到第四个参数，同时传一个接收eglconfig的数组到第三个参数，可以获取所有符合条件的eglconfig
    if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs)) {
        LOGE("eglChooseConfig() error:%d", eglGetError());
        return -1;
    }
    EGLint contextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE};
    EGLContext context;
    if (!(context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                                     contextAttribs))) {
        LOGE("eglCreateContext() error:%d", eglGetError());
        return -1;
    }

    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);
    EGLint format;
    if (!eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format)) {
        LOGE("eglGetConfigAttrib() returned error %d", eglGetError());
        return -1;
    }
    ANativeWindow_setBuffersGeometry(nativeWindow, 0, 0, format);
    EGLSurface windowSurface;
    if (!(windowSurface = eglCreateWindowSurface(display, config, nativeWindow, nullptr))) {
        LOGE("eglCreateWindowSurface() returned error %d", eglGetError());
    }

    // 绑定到当前线程，opengl才可以操作设备
    if (!eglMakeCurrent(display, windowSurface, windowSurface, context)) {
        LOGE("eglMakeCurrent() error:%d\n", eglGetError());
        return -1;
    }

    /* ===============
     * OpenGL ES 相关
     * ===============*/

    // 顶点坐标，以物体中心为原点
    GLfloat vertices[] = {-1.0f, -1.0f,
                          1.0f, -1.0f,
                          -1.0f, 1.0f,
                          1.0f, 1.0f};

    // 纹理与屏幕坐标都以顶点坐标为基准进行转换
    // 把相对应的顶点坐标转换成纹理或屏幕坐标
    // 就产生下面两个坐标

    // 纹理坐标，以物体左下角为原点，将顶点转成如下坐标
    // 不直接在屏幕上绘制时使用，例如离屏渲染
    GLfloat texCoords[] = {0.0f, 0.0f,
                           1.0f, 0.0f,
                           0.0f, 1.0f,
                           1.0f, 1.0f};

    // 屏幕坐标，以屏幕或物体左上角为原点，将顶点转为如下坐标
    GLfloat screenCoords[] = {0.0f, 1.0f,
                              1.0f, 1.0f,
                              0.0f, 0.0f,
                              1.0f, 0.0f};

    const GLchar *vertexShaderSource = "attribute vec4 position; \n"
                                       "attribute vec2 texcoord; \n"
                                       "varying vec2 v_texcoord; \n"
                                       "void main(void) \n"
                                       "{ \n"
                                       " gl_Position =  position; \n"
                                       " v_texcoord = texcoord; \n"
                                       "} \n";
    const GLchar *fragmentShaderSource = "precision highp float; \n"
                                         "varying highp vec2 v_texcoord; \n"
                                         "uniform sampler2D texSampler; \n"
                                         "void main() { \n"
                                         " gl_FragColor = texture2D(texSampler, v_texcoord); \n"
                                         "} \n";
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);
    GLint compiled;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &compiled);
    if (compiled == 0) {
        glDeleteShader(vertexShader);
        LOGE("vertex shader compile failed");
        return -1;
    }

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);
    compiled = 0;
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &compiled);
    if (compiled == 0) {
        glDeleteShader(fragmentShader);
        LOGE("fragment shader compile failed.");
        return -1;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    int error;
    if ((error = glGetError()) != GL_NO_ERROR) {
        LOGE("attach vertex shader error:%d", error);
        return -1;
    }
    error = GL_NO_ERROR;
    glAttachShader(program, fragmentShader);
    if ((error = glGetError()) != GL_NO_ERROR) {
        LOGE("attach fragment shader error:%d", error);
        return -1;
    }
    glLinkProgram(program);
    GLint linkStatus = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        GLint len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        GLchar logInfo[len];
        glGetProgramInfoLog(program, len, nullptr, logInfo);
        LOGE("Could not link program: %s", logInfo);

        glDeleteProgram(program);
        program = 0;
        LOGE("link program falied");
        return -1;
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    glUseProgram(program);
    GLuint positionLoc = glGetAttribLocation(program, "position");
    GLuint texcoordLoc = glGetAttribLocation(program, "texcoord");
    GLuint texSamplerLoc = glGetUniformLocation(program, "texSampler");

    glVertexAttribPointer(positionLoc, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(positionLoc);
    glVertexAttribPointer(texcoordLoc, 2, GL_FLOAT, GL_FALSE, 0, screenCoords);
    glEnableVertexAttribArray(texcoordLoc);

    GLuint texture;
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0); // 纹理单元 texture0 默认是激活的,这句代码可以不要
    glBindTexture(GL_TEXTURE_2D, texture); // 绑定之前需要先激活纹理单元
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // 缩小参数设置
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // 放大参数设置
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // 纹理S方向的参数，拉伸、重复等
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // 纹理T方向的参数
    glUniform1i(texSamplerLoc,0); // 指明采样器连接到哪个纹理单元，从该纹理单元采样


    /*================
     * ffmpeg 解码相关
     *================ */
    const char *file_name = env->GetStringUTFChars(url, nullptr);
    AVFormatContext *pFormatCtx = avformat_alloc_context();

    if (avformat_open_input(&pFormatCtx, file_name, nullptr, nullptr) != 0) {

        LOGE("Couldn't open file:%s\n", file_name);
        return -1;
    }

    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        LOGE("Couldn't find stream information.");
        return -1;
    }

    int videoStream = -1, i;
    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
            && videoStream < 0) {
            videoStream = i;
        }
    }
    if (videoStream == -1) {
        LOGE("Didn't find a video stream.");
        return -1;
    }

    AVCodecParameters *pCodecParameters = pFormatCtx->streams[videoStream]->codecpar;

    AVCodec *pCodec = avcodec_find_decoder(pCodecParameters->codec_id);
    if (pCodec == nullptr) {
        LOGE("Codec not found decoder.");
        return -1;
    }
    AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
    // 为 AVCodecContext 设置参数值，不调用的话该对象将获取不到各种参数
    if (avcodec_parameters_to_context(pCodecCtx, pCodecParameters) < 0) {
        LOGE("avcodec_parameters_to_context error.");
        return -1;
    }
    if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
        LOGE("Could not open codec.");
        return -1;
    }

    // 分配一个用于解码的帧
    AVFrame *pFrame = av_frame_alloc();

    // 分配一个用于存储转换格式后的帧，最终渲染到屏幕
    AVFrame *pFrameRGBA = av_frame_alloc();
    if (pFrameRGBA == nullptr || pFrame == nullptr) {
        LOGE("Could not allocate video frame.");
        return -1;
    }

    // buffer中数据就是用于渲染的,且格式为RGBA
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, pCodecCtx->width, pCodecCtx->height, 1);
    //uint8_t *buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    auto *buffer = (uint8_t *) av_malloc(numBytes);  // malloc() 也行
    // 用buffer初始化渲染帧空间
    av_image_fill_arrays(pFrameRGBA->data, pFrameRGBA->linesize, buffer, AV_PIX_FMT_RGBA,
                         pCodecCtx->width, pCodecCtx->height, 1);

    // 由于解码出来的帧格式不是RGBA的,在渲染之前需要进行格式转换
    struct SwsContext *sws_ctx = sws_getContext(pCodecCtx->width,
                                                pCodecCtx->height,
                                                pCodecCtx->pix_fmt,
                                                pCodecCtx->width,
                                                pCodecCtx->height,
                                                AV_PIX_FMT_RGBA,
                                                SWS_FAST_BILINEAR,
                                                nullptr,
                                                nullptr,
                                                nullptr);

    AVPacket packet;

    while (av_read_frame(pFormatCtx, &packet) >= 0) {

        if (packet.stream_index == videoStream) {

            int sendRet = avcodec_send_packet(pCodecCtx, &packet);
            if (sendRet != 0) {
                if (sendRet == AVERROR_EOF) {
                    LOGD("Stream end.");
                    return 0;
                }
                LOGE("send_packet error.");
                return -1;
            }

            int receiveRet = avcodec_receive_frame(pCodecCtx, pFrame);
            if (receiveRet != 0) {
                if (receiveRet == AVERROR_EOF) { // 流结束
                    return 0;
                }
            }

            // 解码完一帧。如果返回值不为0且不为AVERROR_EOF则需进行下一次读取
            // 也就是说一帧并不一定一次就能解完，一个包内可能含有多个帧
            if (receiveRet == 0) {

                sws_scale(sws_ctx, pFrame->data,
                          pFrame->linesize, 0, pCodecCtx->height,
                          pFrameRGBA->data, pFrameRGBA->linesize);

                // 转换后的 pFrameRGBA 的 width 与 height 值都为0
                // 所以想要使用宽和高不能用转换后的缓存帧的宽高属性
                // 可以使用原帧的宽高


//                int bufSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA,pFrame->width,pFrame->height,1);
//                uint8_t * data = (uint8_t *)malloc(bufSize);
//                memcpy(data,pFrameRGBA->data[0],bufSize);
//                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pFrame->width, pFrame->height, 0, GL_RGBA,
//                             GL_UNSIGNED_BYTE, data);glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pFrame->width, pFrame->height, 0, GL_RGBA,
//                                                                  GL_UNSIGNED_BYTE, data);

                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pFrame->width, pFrame->height, 0, GL_RGBA,
                                                                  GL_UNSIGNED_BYTE, pFrameRGBA->data[0]);

                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                eglSwapBuffers(display,windowSurface);

                //free(data);
            }

        }


        av_packet_unref(&packet);
    }

    /* =========
     * 释放资源
     * =========*/

    // free ffmpeg

    av_free(buffer);
    av_free(pFrameRGBA);
    av_free(pFrame);

    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);

    // free gles
    glBindTexture(GL_TEXTURE_2D,0);
    glDisableVertexAttribArray(positionLoc);
    glDisableVertexAttribArray(texcoordLoc);

    // destroy egl
    eglDestroySurface(display, windowSurface);
    eglDestroyContext(display, context);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    display = EGL_NO_DISPLAY;
    context = EGL_NO_CONTEXT;

    return 0;
}