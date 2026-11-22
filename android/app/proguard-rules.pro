# Keep SDL3 Java/JNI bridge classes and native entry points intact.
-keep class org.libsdl.app.** { *; }
-keep class com.openttd.ds.OpenTTDNative { *; }
-keep class com.openttd.ds.OpenTTDActivity { *; }
-keep class com.openttd.ds.SecondaryScreenPresentation { *; }
