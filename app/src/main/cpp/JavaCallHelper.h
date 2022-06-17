#ifndef EASYPLAYER_JAVACALLHELPER_H
#define EASYPLAYER_JAVACALLHELPER_H
#include <jni.h>

class JavaCallHelper {

public:
    JavaCallHelper(JavaVM *javaVm,JNIEnv *env,jobject instance);
    ~JavaCallHelper();

    void onPrepared();
    void onProgress(int progress);

    JNIEnv *env;
    jobject instance;
    jmethodID jmd_prepared;
    jmethodID jmd_progress;
    JavaVM *javaVm;


};

#endif //EASYPLAYER_JAVACALLHELPER_H
