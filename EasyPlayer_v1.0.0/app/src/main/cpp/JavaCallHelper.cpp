#include "JavaCallHelper.h"

// JNIEnv不能跨线程使用
// jobject 一旦涉及方法跨线程，设置全局引用
JavaCallHelper::JavaCallHelper(JavaVM *javaVm,JNIEnv *env,jobject instance) {
    //onPrepared
    this->javaVm = javaVm;
    this->env = env;
    this->instance = env->NewGlobalRef(instance);

    jclass clazz = env->GetObjectClass(instance);
    jmd_prepared = env->GetMethodID(clazz,"onPrepared","()V");
    jmd_progress = env->GetMethodID(clazz,"onProgress","(I)V");
}

JavaCallHelper::~JavaCallHelper() {
    env->DeleteGlobalRef(instance);
    instance = 0;
    env = 0;
}

void JavaCallHelper::onPrepared() {
    // JNIEnv不能跨线程使用
    // 获取子线程JNIEnv
    JNIEnv *env_child;
    javaVm->AttachCurrentThread(&env_child,0);
    env_child->CallVoidMethod(instance,jmd_prepared);
    javaVm->DetachCurrentThread();

}

void JavaCallHelper::onProgress(int progress) {
    // JNIEnv不能跨线程使用
    // 获取子线程JNIEnv
    JNIEnv *env_child;
    javaVm->AttachCurrentThread(&env_child,0);
    env_child->CallVoidMethod(instance,jmd_progress,progress);
    javaVm->DetachCurrentThread();
}