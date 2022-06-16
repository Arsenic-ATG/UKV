/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class com_unum_ukv_DataBase */

#ifndef _Included_com_unum_ukv_DataBase
#define _Included_com_unum_ukv_DataBase
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     com_unum_ukv_DataBase
 * Method:    put
 * Signature: (J[B)V
 */
JNIEXPORT void JNICALL Java_com_unum_ukv_DataBase_put
  (JNIEnv *, jobject, jlong, jbyteArray);

/*
 * Class:     com_unum_ukv_DataBase
 * Method:    putIfAbsent
 * Signature: (J[B)V
 */
JNIEXPORT void JNICALL Java_com_unum_ukv_DataBase_putIfAbsent
  (JNIEnv *, jobject, jlong, jbyteArray);

/*
 * Class:     com_unum_ukv_DataBase
 * Method:    putAll
 * Signature: (Ljava/util/Map;)V
 */
JNIEXPORT void JNICALL Java_com_unum_ukv_DataBase_putAll
  (JNIEnv *, jobject, jobject);

/*
 * Class:     com_unum_ukv_DataBase
 * Method:    replace
 * Signature: (J[B)[B
 */
JNIEXPORT jbyteArray JNICALL Java_com_unum_ukv_DataBase_replace
  (JNIEnv *, jobject, jlong, jbyteArray);

/*
 * Class:     com_unum_ukv_DataBase
 * Method:    containsKey
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_com_unum_ukv_DataBase_containsKey
  (JNIEnv *, jobject, jlong);

/*
 * Class:     com_unum_ukv_DataBase
 * Method:    get
 * Signature: (J)[B
 */
JNIEXPORT jbyteArray JNICALL Java_com_unum_ukv_DataBase_get
  (JNIEnv *, jobject, jlong);

/*
 * Class:     com_unum_ukv_DataBase
 * Method:    getOrDefault
 * Signature: (J[B)[B
 */
JNIEXPORT jbyteArray JNICALL Java_com_unum_ukv_DataBase_getOrDefault
  (JNIEnv *, jobject, jlong, jbyteArray);

/*
 * Class:     com_unum_ukv_DataBase
 * Method:    remove
 * Signature: (J)[B
 */
JNIEXPORT jbyteArray JNICALL Java_com_unum_ukv_DataBase_remove__J
  (JNIEnv *, jobject, jlong);

/*
 * Class:     com_unum_ukv_DataBase
 * Method:    remove
 * Signature: (J[B)Z
 */
JNIEXPORT jboolean JNICALL Java_com_unum_ukv_DataBase_remove__J_3B
  (JNIEnv *, jobject, jlong, jbyteArray);

/*
 * Class:     com_unum_ukv_DataBase
 * Method:    clear
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_unum_ukv_DataBase_clear
  (JNIEnv *, jobject);

/*
 * Class:     com_unum_ukv_DataBase
 * Method:    open
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_com_unum_ukv_DataBase_open
  (JNIEnv *, jobject, jstring);

/*
 * Class:     com_unum_ukv_DataBase
 * Method:    close_
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_unum_ukv_DataBase_close_1
  (JNIEnv *, jobject);

/*
 * Class:     com_unum_ukv_DataBase
 * Method:    isEmpty
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_com_unum_ukv_DataBase_isEmpty
  (JNIEnv *, jobject);

/*
 * Class:     com_unum_ukv_DataBase
 * Method:    size
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_com_unum_ukv_DataBase_size
  (JNIEnv *, jobject);

/*
 * Class:     com_unum_ukv_DataBase
 * Method:    enumerate
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_unum_ukv_DataBase_enumerate
  (JNIEnv *, jobject);

#ifdef __cplusplus
}
#endif
#endif