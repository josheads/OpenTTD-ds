plugins {
    id("com.android.application")
}

val hostBinaryDirProp = (project.findProperty("hostBinaryDir") as String?)?.trim()
val resolvedHostBinaryDir = (if (hostBinaryDirProp.isNullOrEmpty()) {
    rootProject.file("../build-tools")
} else {
    val candidate = rootProject.file(hostBinaryDirProp)
    if (candidate.exists()) candidate else rootProject.file("../$hostBinaryDirProp")
}).canonicalFile.absolutePath.replace('\\', '/')

val configuredNdkVersion = (project.findProperty("ndkVersion") as String?)
    ?.trim()
    ?.takeIf { it.isNotEmpty() }
    ?: "27.2.12479018"

val depsCacheDirProp = (project.findProperty("depsCacheDir") as String?)?.trim()
val resolvedDepsCacheDir = depsCacheDirProp
    ?.takeIf { it.isNotEmpty() }
    ?.let { rootProject.file(it) }
    ?.takeIf { it.isDirectory }
    ?.canonicalFile
    ?.absolutePath
    ?.replace('\\', '/')

val ccacheExecutableProp = (project.findProperty("ccacheExecutable") as String?)?.trim()
val resolvedCcacheExecutable = ccacheExecutableProp
    ?.takeIf { it.isNotEmpty() }
    ?.let { rootProject.file(it) }
    ?.takeIf { it.isFile }
    ?.canonicalFile
    ?.absolutePath
    ?.replace('\\', '/')

val releaseKeystorePath = System.getenv("ANDROID_KEYSTORE_PATH")?.takeIf { it.isNotBlank() }
val releaseKeystorePassword = System.getenv("ANDROID_KEYSTORE_PASSWORD")
val releaseKeyAlias = System.getenv("ANDROID_KEY_ALIAS")
val releaseKeyPassword = System.getenv("ANDROID_KEY_PASSWORD")
val hasReleaseSigning = releaseKeystorePath != null &&
    file(releaseKeystorePath).isFile &&
    !releaseKeystorePassword.isNullOrEmpty() &&
    !releaseKeyAlias.isNullOrEmpty() &&
    !releaseKeyPassword.isNullOrEmpty()

android {
    namespace = "com.openttd.ds"
    compileSdk = 35
    ndkVersion = configuredNdkVersion

    defaultConfig {
        applicationId = "com.openttd.ds"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "17.0-thor-ds"

        ndk {
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                arguments.addAll(
                    listOf(
                        "-DANDROID_STL=c++_static",
                        "-DANDROID_PLATFORM=android-26",
                        "-DHOST_BINARY_DIR=$resolvedHostBinaryDir",
                        "-DOPTION_DUAL_SCREEN=ON",
                        "-DOPTION_FETCH_DEPS=ON"
                    )
                )
                if (resolvedDepsCacheDir != null) {
                    arguments.add("-DOPENTTD_DEPS_CACHE_DIR=$resolvedDepsCacheDir")
                }
                if (resolvedCcacheExecutable != null) {
                    arguments.addAll(
                        listOf(
                            "-DCMAKE_C_COMPILER_LAUNCHER=$resolvedCcacheExecutable",
                            "-DCMAKE_CXX_COMPILER_LAUNCHER=$resolvedCcacheExecutable",
                            "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON"
                        )
                    )
                }
                // Explicitly include SDL3-shared alongside openttd (libmain.so) so AGP
                // always builds and packages both libSDL3.so and libmain.so into the APK.
                targets.addAll(listOf("openttd", "SDL3-shared"))
            }
        }
    }

    signingConfigs {
        if (hasReleaseSigning) {
            create("release") {
                storeFile = file(releaseKeystorePath!!)
                storePassword = releaseKeystorePassword
                keyAlias = releaseKeyAlias
                keyPassword = releaseKeyPassword
            }
        }
    }

    buildTypes {
        debug {
            isDebuggable = true
            isJniDebuggable = true
        }
        release {
            isMinifyEnabled = false
            isShrinkResources = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            // Default to debug-key signing when no release keystore env vars are set
            // so CI-built release APKs can be sideloaded directly onto AYN Thor.
            signingConfig = if (hasReleaseSigning) {
                signingConfigs.getByName("release")
            } else {
                signingConfigs.getByName("debug")
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    packaging {
        jniLibs {
            // Keep libSDL3.so and libmain.so extracted on disk for SDLActivity.getMainSharedObject()
            useLegacyPackaging = true
        }
    }

    lint {
        abortOnError = false
    }
}

dependencies {
    implementation(fileTree(mapOf("dir" to "libs", "include" to listOf("*.jar"))))
}
